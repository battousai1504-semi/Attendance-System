# DFPlayer Mini test

Sketch Arduino IDE: `dfplayer_test.ino`

## Wiring for the current ESP32-S3 camera board

| DFPlayer Mini | ESP32-S3 |
|---|---|
| VCC | 5V / VBUS |
| GND | GND |
| TX | GPIO42 |
| RX | GPIO41 through about 1K resistor |
| SPK1 | speaker wire 1 |
| SPK2 | speaker wire 2 |

The speaker must connect to `SPK1` and `SPK2`. Do not connect this speaker
between `SPK1` and GND because the DFPlayer speaker output is bridged.

These are the same pins used by the production ESP-IDF firmware. Flashing this
standalone Arduino sketch replaces that firmware until it is flashed back; use
the production maintenance portal for the final end-to-end acceptance test.

Use a FAT32 microSD card. Put a test file at:

```text
/mp3/0001.mp3
```

Optional second test file:

```text
/mp3/0002.mp3
```

Install the `DFRobotDFPlayerMini` library from Arduino IDE Library Manager.
Select the same ESP32-S3 board package used by the project, upload the sketch,
then open Serial Monitor at `115200 baud` with line ending `Newline`.

Commands: `1`, `2`, `p`, `r`, `s`, `n`, `b`, `+`, `-`, `v20`, `i`, `h`.

## Testing before having an SD card

The sketch will not stop if the SD card is missing. It enters UART diagnostic
mode instead. In this mode you can check that the module is powered and that
the UART wiring is correct. Send `t` in Serial Monitor to send a play command;
the module should report a missing/busy card or a timeout.

Without an SD card, actual audio playback cannot be tested. The speaker path
can only be confirmed after putting an MP3 file on the card.
