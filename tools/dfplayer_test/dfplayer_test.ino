/*
 * DFPlayer Mini hardware test for ESP32-S3 N16R8 + OV2640 CAM.
 *
 * This is an Arduino IDE standalone sketch. It does not use Wi-Fi,
 * camera, face recognition, or Google Sheets.
 *
 * UART wiring used by this test:
 *   DFPlayer TX  -> ESP32 GPIO1  (ESP32 RX)
 *   DFPlayer RX  <- ESP32 GPIO2  (ESP32 TX, preferably through 1K resistor)
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// These pins are free in the current camera pin configuration.
// If your board exposes different free pins, change only these two values.
constexpr int DFPLAYER_RX_PIN = 1;  // ESP32 receives from DFPlayer TX
constexpr int DFPLAYER_TX_PIN = 2;  // ESP32 sends to DFPlayer RX

constexpr uint8_t DEFAULT_VOLUME = 18; // Range: 0..30

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;

void printDfPlayerDetail(uint8_t type, int value)
{
  switch (type) {
    case TimeOut:
      Serial.println(F("[DFP] Timeout - check power and UART wiring"));
      break;

    case WrongStack:
      Serial.println(F("[DFP] Wrong stack - UART noise or wrong baud rate"));
      break;

    case DFPlayerCardInserted:
      Serial.println(F("[DFP] SD card inserted"));
      break;

    case DFPlayerCardRemoved:
      Serial.println(F("[DFP] SD card removed"));
      break;

    case DFPlayerCardOnline:
      Serial.println(F("[DFP] SD card online"));
      break;

    case DFPlayerPlayFinished:
      Serial.print(F("[DFP] Finished file #"));
      Serial.println(value);
      break;

    case DFPlayerError:
      Serial.print(F("[DFP] Error: "));
      switch (value) {
        case Busy:
          Serial.println(F("card not found / busy"));
          break;
        case Sleeping:
          Serial.println(F("sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("serial wrong stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("checksum mismatch"));
          break;
        case FileIndexOut:
          Serial.println(F("file index out of bounds"));
          break;
        case FileMismatch:
          Serial.println(F("file not found"));
          break;
        default:
          Serial.println(value);
          break;
      }
      break;

    default:
      break;
  }
}

void printHelp()
{
  Serial.println();
  Serial.println(F("Commands in Serial Monitor (115200 baud):"));
  Serial.println(F("  1       play /mp3/0001.mp3"));
  Serial.println(F("  2       play /mp3/0002.mp3"));
  Serial.println(F("  p       pause"));
  Serial.println(F("  r       resume"));
  Serial.println(F("  s       stop"));
  Serial.println(F("  n       next track"));
  Serial.println(F("  b       previous track"));
  Serial.println(F("  +       volume up"));
  Serial.println(F("  -       volume down"));
  Serial.println(F("  v20     set volume 0..30, e.g. v12"));
  Serial.println(F("  i       print SD/file information"));
  Serial.println(F("  t       send a test play command (useful for no-SD diagnosis)"));
  Serial.println(F("  h       show this help"));
  Serial.println();
}

void printDfPlayerInfo()
{
  Serial.println(F("[DFP] Reading information..."));
  Serial.print(F("  volume: "));
  Serial.println(dfPlayer.readVolume());
  Serial.print(F("  files on SD: "));
  Serial.println(dfPlayer.readFileCounts());
  Serial.print(F("  current file: "));
  Serial.println(dfPlayer.readCurrentFileNumber());
}

void handleCommand(String command)
{
  command.trim();
  command.toLowerCase();

  if (command == "1") {
    dfPlayer.playMp3Folder(1);
  } else if (command == "2") {
    dfPlayer.playMp3Folder(2);
  } else if (command == "p") {
    dfPlayer.pause();
  } else if (command == "r") {
    dfPlayer.start();
  } else if (command == "s") {
    dfPlayer.stop();
  } else if (command == "n") {
    dfPlayer.next();
  } else if (command == "b") {
    dfPlayer.previous();
  } else if (command == "+") {
    dfPlayer.volumeUp();
  } else if (command == "-") {
    dfPlayer.volumeDown();
  } else if (command == "i") {
    printDfPlayerInfo();
  } else if (command == "t") {
    Serial.println(F("[DFP] Sending play command for file #1..."));
    dfPlayer.playMp3Folder(1);
  } else if (command == "h") {
    printHelp();
  } else if (command.startsWith("v")) {
    int volume = command.substring(1).toInt();
    if (volume >= 0 && volume <= 30) {
      dfPlayer.volume(volume);
      Serial.print(F("[DFP] Volume set to "));
      Serial.println(volume);
    } else {
      Serial.println(F("[DFP] Volume must be between 0 and 30"));
    }
  } else if (command.length() > 0) {
    Serial.println(F("Unknown command. Press h for help."));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println(F("=== DFPlayer Mini hardware test ==="));
  Serial.print(F("UART RX/TX: GPIO"));
  Serial.print(DFPLAYER_RX_PIN);
  Serial.print(F("/GPIO"));
  Serial.println(DFPLAYER_TX_PIN);

  // ESP32-S3 UART1 can be routed to arbitrary GPIOs.
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

  Serial.println(F("Initializing DFPlayer (wait 3-5 seconds)..."));
  if (!dfPlayer.begin(dfSerial, true, true)) {
    Serial.println(F("DFPlayer did not complete normal init."));
    Serial.println(F("This is expected when no SD card is inserted."));
    Serial.println(F("Continuing in UART-only diagnostic mode."));
    Serial.println(F("Use command 't' to send a play command and watch responses."));
  } else {
    dfPlayerReady = true;
    Serial.println(F("DFPlayer online."));
  }

  dfPlayer.setTimeOut(500);
  if (dfPlayerReady) {
    dfPlayer.volume(DEFAULT_VOLUME);
    dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  }

  Serial.println(F("SD layout required: /mp3/0001.mp3 (and optionally 0002.mp3)"));
  printHelp();

  if (dfPlayerReady) {
    delay(1000);
    Serial.println(F("Playing test file #1..."));
    dfPlayer.playMp3Folder(1);
  }
}

void loop()
{
  if (dfPlayer.available()) {
    printDfPlayerDetail(dfPlayer.readType(), dfPlayer.read());
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    handleCommand(command);
  }

  delay(10);
}
