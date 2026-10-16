# Fm1PunchEffect

A USB-host MIDI controller for SLOOP's **27 punch-in effects**, built on an **M5Stack AtomS3** with an **M5Stack Unit Angle** (potentiometer). One knob and one button turn the M-VAVE FM-1's punch-in effects into a live performance tool.

The AtomS3 is the **USB host**: it enumerates the FM-1 directly (the FM-1 stays on its battery) and sends plain MIDI notes on the FX channel. No computer, no DAW, no MIDI adapter.

## How it works

SLOOP (2.7 and newer) plays the FM-1's 27 punch-in effects from MIDI: notes **F3..G5 (53..79)** on the channel set in **GLO > MIDI 3 > FX CH**. This controller sends exactly those notes.

### Controls

| Gesture | Action |
|---|---|
| Rotate the pot | Scroll through the 27 effects (live: the sounding effect switches as you turn) |
| Press and hold | Momentary: the selected effect is on while held. Rotating while pressed switches effects live |
| Double-click (two short clicks) | Latch **HOLD** on the selected effect |
| With HOLD active: short tap | Leave the HOLD |
| With HOLD active: long press | Momentary on top of the HOLD (release restores the held effect) |
| With HOLD active: rotate the pot | The HOLD moves to the selected effect (live) |

- A "short click" is released within 250 ms; a double-click needs both clicks short and less than 300 ms apart.
- On boot and on every (re)connect the controller sends a panic (all 27 effects off); if a HOLD was latched it is re-applied on reconnect.
- The display shows: USB state, effect number/name, FX on/off, HOLD, error counter and the pot calibration range.

## Hardware

- M5Stack AtomS3 (ESP32-S3)
- M5Stack Unit Angle (U005) - 10K potentiometer, analog output

### Wiring (Grove cable)

| Wire | Connects to |
|---|---|
| White | G1 (ADC1_CH0) - potentiometer wiper |
| Red | 5V |
| Black | GND |
| Yellow | G2 - not used |

The front button (M5.BtnA) is the only button needed.

## FM-1 setup (SLOOP)

1. Install SLOOP 2.7 (or newer) on the M-VAVE FM-1 - https://github.com/isod89/sloop-fm1
2. Set **GLO > MIDI 3 > FX CH = 1** (works with IN = NOTES or IN = CH n, not with IN = CLOCK). The sketch uses channel 1 (`FX_CHANNEL` in the code).
3. Connect the FM-1 to the AtomS3 with a USB-C cable.

That's all - no other MIDI settings are involved.

## FX map

Same layout as the FM-1's keys: white keys = effects 1-16, black keys = effects 17-27.

| # | MIDI note | FX | | # | MIDI note | FX |
|---|---|---|---|---|---|---|
| 1 | F3 (53) | LOOP 4 | | 15 | F5 (77) | ECHO |
| 2 | G3 (55) | LOOP 8 | | 16 | G5 (79) | WOBBLE |
| 3 | A3 (57) | LOOP 16 | | 17 | F#3 (54) | KILL LOW |
| 4 | B3 (59) | LOOP 32 | | 18 | G#3 (56) | KILL MID |
| 5 | C4 (60) | STUTTER | | 19 | A#3 (58) | KILL HIGH |
| 6 | D4 (62) | REVERSE | | 20 | C#4 (61) | REWIND |
| 7 | E4 (64) | STOP | | 21 | D#4 (63) | OCT UP |
| 8 | F4 (65) | HALF | | 22 | F#4 (66) | FLANGER |
| 9 | G4 (67) | LOW | | 23 | G#4 (68) | AUTO-PAN |
| 10 | A4 (69) | HIGH | | 24 | A#4 (70) | FREEZE |
| 11 | B4 (71) | PHONE | | 25 | C#5 (73) | LOOP 8T |
| 12 | C5 (72) | CRUSH | | 26 | D#5 (75) | MONO |
| 13 | D5 (74) | ALIAS | | 27 | F#5 (78) | FADE OUT |
| 14 | E5 (76) | GATE | | | | |

## Build & flash

Tested with Arduino IDE 2.x / arduino-cli 1.1.1 and **esp32 core 3.3.8**.

Libraries:
- M5Unified (includes M5GFX)
- [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) >= 7.3.0

Board settings (Tools menu, board "ESP32S3 Dev Module"):
- **USB Mode: "Hardware CDC and JTAG"** (required - see the note below)
- PSRAM: Disabled
- Flash Size: 8MB
- Partition Scheme: Default 8MB with spiffs (3MB APP/1.5MB SPIFFS)

Note: do not look for a "USB Host" USB mode - it does not exist (not even in the current core). The library takes the USB OTG port over itself at `usbHost.begin()`; "Hardware CDC and JTAG" is the required setting.

With arduino-cli:

```
arduino-cli compile -u -p <PORT> --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,PSRAM=disabled,FlashSize=8M,PartitionScheme=default_8MB" .
```

### Flashing notes

- **The COM port disappears** as soon as the sketch runs: the USB-C port becomes the host port for the FM-1, so there is no serial console anymore. The LCD is the only debug output.
- To flash again and the upload can't connect: put the AtomS3 in **download mode** (hold the front button until the LED turns green, about 2 seconds, then release); the COM port comes back.

## Notes on the implementation

- **Outbox FIFO** - ESP32_Host_MIDI's `sendMidiMessage()` returns false while a USB transfer is in flight (`_outTransferBusy`), so back-to-back messages (effect off + effect on) would be dropped. The sketch queues outgoing notes in a 32-slot ring buffer and flushes it every loop.
- **Matched off/on pairs** - SLOOP's FX handler only honors a note-off if it matches the note the FX channel is currently holding (`mfx_key`); the controller therefore always sends a matched off before the next on (and the panic sends all 27 offs).
- **Adaptive calibration** - the pot range is learned at runtime (min/max) with hysteresis on the effect boundaries; turn the pot across its full range once after power-up.
- The FM-1's own keys always win: pressing an FX key on the FM-1 takes the effect over.

## Credits

- [SLOOP](https://github.com/isod89/sloop-fm1) - the FM-1 firmware this controller speaks to (GPL-3.0), by isod89, based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hugelton Instruments.
- [ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) - USB host MIDI stack by sauloverissimo.
- M5Unified / M5GFX by M5Stack.

M-VAVE and FM-1 are trademarks of their owners; this project is not affiliated with M-VAVE or with the SLOOP project.

## License

MIT - see [LICENSE](LICENSE).
