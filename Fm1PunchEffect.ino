#include <M5Unified.h>
#include <MIDIHandler.h>
#include <USBConnection.h>

USBConnection usbHost;

static const uint8_t FX_CHANNEL = 1;
static const uint8_t FX_COUNT   = 27;

static const uint8_t FX_NOTE[FX_COUNT] = {
    53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79,
    54, 56, 58, 61, 63, 66, 68, 70, 73, 75, 78
};

static const char* const FX_NAME[FX_COUNT] = {
    "LOOP 4", "LOOP 8", "LOOP 16", "LOOP 32", "STUTTER", "REVERSE", "STOP", "HALF",
    "LOW", "HIGH", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOBBLE",
    "KILL LOW", "KILL MID", "KILL HIGH", "REWIND", "OCT UP", "FLANGER", "AUTO-PAN",
    "FREEZE", "LOOP 8T", "MONO", "FADE OUT"
};

static const unsigned long CLICK_MAX = 250;
static const unsigned long GAP_MAX   = 300;

static int8_t        curIdx       = 0;
static int8_t        latchIdx     = -1;
static int8_t        sentNote     = -1;
static unsigned long pressStart   = 0;
static unsigned long lastRelease  = 0;
static bool          lastWasShort = false;
static bool          pendDbl      = false;
static bool          pressNoop    = false;
static int           sendErrors   = 0;
static bool          wasConnected = false;

static float potMin = 4095.0f;
static float potMax = 0.0f;
static float potAvg = -1.0f;

static const int OUTBOX_MAX = 32;
struct OutMsg { uint8_t note; bool on; };
static OutMsg outbox[OUTBOX_MAX];
static int    outHead  = 0;
static int    outCount = 0;

static void outPush(uint8_t note, bool on) {
    if (outCount >= OUTBOX_MAX) { sendErrors++; return; }
    OutMsg& m = outbox[(outHead + outCount) % OUTBOX_MAX];
    m.note = note;
    m.on = on;
    outCount++;
}

static void outClear() {
    outHead = 0;
    outCount = 0;
}

static void outFlush() {
    while (outCount > 0) {
        OutMsg& m = outbox[outHead];
        bool ok = m.on ? midiHandler.sendNoteOn(FX_CHANNEL, m.note, 100, &usbHost)
                       : midiHandler.sendNoteOff(FX_CHANNEL, m.note, 0, &usbHost);
        if (!ok) break;
        outHead = (outHead + 1) % OUTBOX_MAX;
        outCount--;
    }
}

static void noteOn(uint8_t idx) {
    outPush(FX_NOTE[idx], true);
    sentNote = (int8_t)idx;
}

static void noteOff(int8_t idx) {
    outPush(FX_NOTE[idx], false);
    sentNote = -1;
}

static void panicAll() {
    for (uint8_t i = 0; i < FX_COUNT; i++)
        outPush(FX_NOTE[i], false);
    sentNote = -1;
}

static void redraw() {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(usbHost.isConnected() ? TFT_GREEN : TFT_RED, TFT_BLACK);
    d.setCursor(0, 0);
    d.print(usbHost.isConnected() ? "USB OK" : "USB --");
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(54, 0);
    d.printf("ch%u", FX_CHANNEL);

    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(0, 14);
    d.printf("%d/%d", curIdx + 1, FX_COUNT);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(0, 38);
    d.print(FX_NAME[curIdx]);

    d.setTextSize(1);
    d.setTextColor(sentNote >= 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    d.setCursor(0, 62);
    d.print(sentNote >= 0 ? "FX: ON" : "FX: off");

    d.setCursor(0, 78);
    if (latchIdx >= 0) {
        d.setTextColor(TFT_GREEN, TFT_BLACK);
        d.printf("HOLD: %s", FX_NAME[latchIdx]);
    } else {
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.print("HOLD: -");
    }

    d.setTextColor(sendErrors > 0 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    d.setCursor(0, 94);
    d.printf("err:%d cal:%d-%d", sendErrors, (int)potMin, (int)potMax);

    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(0, 110);
    d.print("press=FX  2x=hold");
}

static void handleButton() {
    if (M5.BtnA.wasPressed()) {
        unsigned long now = millis();
        pressStart = now;
        bool hadHold = (latchIdx >= 0);
        pendDbl = !hadHold && lastWasShort && lastRelease != 0 && (now - lastRelease) <= GAP_MAX;
        pressNoop = (hadHold && curIdx == latchIdx);
        if (!pressNoop) {
            if (sentNote >= 0) noteOff(sentNote);
            noteOn(curIdx);
        }
        redraw();
    }
    if (M5.BtnA.wasReleased()) {
        unsigned long now = millis();
        bool wasShort = (now - pressStart) <= CLICK_MAX;
        lastRelease = now;
        lastWasShort = wasShort;
        if (pendDbl && wasShort) {
            lastWasShort = false;
            if (latchIdx == curIdx) {
                if (sentNote >= 0) noteOff(sentNote);
                latchIdx = -1;
            } else {
                latchIdx = curIdx;
            }
        } else if (latchIdx >= 0 && wasShort) {
            if (sentNote >= 0) noteOff(sentNote);
            latchIdx = -1;
            lastWasShort = false;
        } else if (!pressNoop) {
            if (sentNote >= 0) noteOff(sentNote);
            if (latchIdx >= 0) noteOn(latchIdx);
        }
        pendDbl = false;
        pressNoop = false;
        redraw();
    }
}

static void pollPot() {
    int raw = analogRead(1);
    potAvg = potAvg < 0 ? (float)raw : (potAvg * 7.0f + (float)raw) / 8.0f;
    if (potAvg < potMin) potMin = potAvg;
    if (potAvg > potMax) potMax = potAvg;
    if (potMax - potMin < 50.0f) return;

    float pos = (potAvg - potMin) * (float)FX_COUNT / (potMax - potMin);
    pos = constrain(pos, 0.0f, (float)FX_COUNT - 0.001f);
    int idx = (int)pos;
    if (idx == curIdx) return;

    float frac = pos - (float)idx;
    bool accept = (frac > 0.15f && frac < 0.85f)
                  || (idx == 0 && frac <= 0.15f)
                  || (idx == FX_COUNT - 1 && frac >= 0.85f);
    if (accept) {
        curIdx = (int8_t)idx;
        if (M5.BtnA.isPressed()) {
            if (sentNote >= 0 && sentNote != curIdx) {
                noteOff(sentNote);
                noteOn(curIdx);
                pressNoop = false;
            }
        } else if (latchIdx >= 0 && sentNote == latchIdx) {
            noteOff(latchIdx);
            noteOn(curIdx);
            latchIdx = curIdx;
        }
        redraw();
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(128);
    M5.Display.setTextWrap(false);

    analogReadResolution(12);
    analogSetPinAttenuation(1, ADC_11db);

    midiHandler.begin();
    midiHandler.addTransport(&usbHost);
    usbHost.begin();

    panicAll();
    redraw();
}

void loop() {
    M5.update();
    midiHandler.task();
    outFlush();

    bool connected = usbHost.isConnected();
    if (connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            outClear();
            panicAll();
            if (latchIdx >= 0) noteOn(latchIdx);
        } else {
            outClear();
        }
        redraw();
    }

    handleButton();
    pollPot();
}
