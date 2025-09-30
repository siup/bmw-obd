/*
  ESP32 OBD-II Dashboard dla BMW E92 328i (N52)
  Arduino IDE (ESP32 core) + ELM327 Bluetooth SPP + TFT 3.2" ILI9341 + dotyk XPT2046

  Funkcje:
  - Łączenie z ELM327 przez Bluetooth Classic SPP
  - Inicjalizacja ELM (ATZ, ATE0, ATL0, ATS0, ATH1, ATSP0)
  - Odczyt podstawowych PID-ów OBD-II (Mode 01)
  - Wyświetlanie danych na TFT (dwa ekrany z kafelkami)
  - Obsługa dotyku do zmiany ekranu

  Wymagane biblioteki (Arduino IDE -> Library Manager):
  - Adafruit GFX Library
  - Adafruit ILI9341
  - XPT2046_Touchscreen (Paul Stoffregen)
  - ESP32 BLE Arduino (dołączona w rdzeniu ESP32) -> BluetoothSerial

  Sprzęt (przykład):
  - ESP32-WROOM / ESP32-S3
  - TFT ILI9341 320x240 SPI + dotyk XPT2046
  - Moduł ELM327 Bluetooth (SPP)

  Autor: oryginalny kod przeniesiony do struktury Arduino IDE.
*/

#ifndef ARDUINO_ARCH_ESP32
#error Ten szkic wymaga płytki z architekturą ESP32 (BluetoothSerial).
#endif

#include <Arduino.h>
#include <SPI.h>
#include <BluetoothSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ===== Konfiguracja użytkownika =====
// Nazwa/parowanie ELM327 (SPP)
#ifndef ELM_BT_NAME
#define ELM_BT_NAME "OBDII"
#endif

// Ustawienia TFT (SPI)
#ifndef TFT_CS
#define TFT_CS 5
#endif
#ifndef TFT_DC
#define TFT_DC 2
#endif
#ifndef TFT_RST
#define TFT_RST 4 // lub -1 jeśli wspólny reset z ESP32
#endif

// Ustawienia dotyku XPT2046 (współdzieli SPI z TFT)
#ifndef TOUCH_CS
#define TOUCH_CS 15
#endif
#ifndef TOUCH_IRQ
#define TOUCH_IRQ 27 // ustaw -1 gdy nie używasz pinu przerwania
#endif

// Częstotliwości SPI
constexpr uint32_t SPI_FREQ_TFT = 40'000'000; // 40 MHz
constexpr uint32_t SPI_FREQ_TOUCH = 2'000'000; // 2 MHz

// Interwały odświeżania
constexpr uint32_t POLL_FAST_MS = 200; // szybkie PID-y (RPM, prędkość)
constexpr uint32_t POLL_SLOW_MS = 800; // wolniejsze PID-y (temp itd.)

// ===== Obiekty globalne =====
BluetoothSerial SerialBT;
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Stan GUI
enum Page : uint8_t { PAGE_MAIN = 0, PAGE_EXTRA = 1 };
Page currentPage = PAGE_MAIN;

// Bufory
String elmBuffer;

// Dane PID
struct ObdData {
  int rpm = -1;       // 010C
  int speed = -1;     // 010D (km/h)
  int coolant = -128; // 0105 (°C)
  int iat = -128;     // 010F (°C)
  int throttle = -1;  // 0111 (%)
  int fuelLevel = -1; // 012F (%)
  int load = -1;      // 0104 (%)
  int map = -1;       // 010B (kPa)
  int maf = -1;       // 0110 (g/s)
} obd;

// Timery
uint32_t tFast = 0;
uint32_t tSlow = 0;

// ===== Funkcje pomocnicze GUI =====
void tftHeader(const char *title) {
  tft.fillRect(0, 0, tft.width(), 26, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 4);
  tft.print(title);
}

void drawTile(int x, int y, int w, int h, const __FlashStringHelper *label, const String &value) {
  tft.drawRect(x, y, w, h, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(2);
  tft.setCursor(x + 8, y + 6);
  tft.print(label);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(x + 8, y + 34);
  tft.print(value);
}

String dashVal(int v, const char *unit = "") {
  if (v < 0) return F("—");
  return String(v) + unit;
}

String dashTemp(int v) {
  if (v <= -100) return F("—");
  return String(v) + F(" °C");
}

void drawMainPage() {
  tft.fillScreen(ILI9341_BLACK);
  tftHeader("BMW E92 — OBD (MAIN)");

  const int w = 150;
  const int h = 70;
  const int gap = 10;
  const int x1 = 10;
  const int x2 = 160;
  const int y1 = 36;
  const int y2 = y1 + h + gap;
  const int y3 = y2 + h + gap;

  drawTile(x1, y1, w, h, F("RPM"), obd.rpm < 0 ? F("—") : String(obd.rpm));
  drawTile(x2, y1, w, h, F("Speed"), dashVal(obd.speed, " km/h"));

  drawTile(x1, y2, w, h, F("Coolant"), dashTemp(obd.coolant));
  drawTile(x2, y2, w, h, F("Throttle"), obd.throttle < 0 ? F("—") : String(obd.throttle) + "%");

  drawTile(x1, y3, w, h, F("IAT"), dashTemp(obd.iat));
  drawTile(x2, y3, w, h, F("Fuel"), obd.fuelLevel < 0 ? F("—") : String(obd.fuelLevel) + "%");

  // Przycisk przełączający
  tft.fillRoundRect(10, 220, 90, 20, 6, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(16, 222);
  tft.print("EXTRA");
}

void drawExtraPage() {
  tft.fillScreen(ILI9341_BLACK);
  tftHeader("BMW E92 — OBD (EXTRA)");

  const int w = 220;
  const int h = 50;
  drawTile(10, 40, w, h, F("Engine Load"), obd.load < 0 ? F("—") : String(obd.load) + "%");
  drawTile(10, 100, w, h, F("MAP"), obd.map < 0 ? F("—") : String(obd.map) + " kPa");
  drawTile(10, 160, w, h, F("MAF"), obd.maf < 0 ? F("—") : String(obd.maf) + " g/s");

  tft.fillRoundRect(10, 220, 110, 20, 6, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(16, 222);
  tft.print("MAIN");
}

bool touchHit(int x, int y, int w, int h) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();

  // Prosta normalizacja — dostosuj zakresy do kalibracji własnego panelu
  constexpr int16_t RAW_MIN_X = 200;
  constexpr int16_t RAW_MAX_X = 3900;
  constexpr int16_t RAW_MIN_Y = 200;
  constexpr int16_t RAW_MAX_Y = 3900;

  int16_t px = map(p.x, RAW_MIN_X, RAW_MAX_X, 0, tft.width());
  int16_t py = map(p.y, RAW_MIN_Y, RAW_MAX_Y, 0, tft.height());
  return (px >= x && px <= x + w && py >= y && py <= y + h);
}

void drawAll() {
  if (currentPage == PAGE_MAIN) {
    drawMainPage();
  } else {
    drawExtraPage();
  }
}

// ===== Obsługa ELM327 =====

bool elmWrite(const String &cmd) {
  if (!SerialBT.hasClient()) return false;
  SerialBT.print(cmd);
  SerialBT.print('\r');
  return true;
}

bool elmReadLine(String &line, uint32_t timeoutMs = 500) {
  uint32_t t0 = millis();
  line = "";
  while (millis() - t0 < timeoutMs) {
    while (SerialBT.available()) {
      char c = static_cast<char>(SerialBT.read());
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.length()) return true;
        continue;
      }
      if (c == '>') {
        if (line.length()) return true;
        continue;
      }
      line += c;
    }
    delay(2);
  }
  return line.length();
}

bool parseOBD_41(const String &line, uint8_t pidExpected, uint8_t &A, uint8_t &B) {
  String s;
  s.reserve(line.length());
  for (char c : line) {
    if (c != ' ') {
      s += static_cast<char>(toupper(c));
    }
  }

  if (s.length() < 4 || s.substring(0, 2) != "41") return false;

  char pidStr[3] = { s[2], s[3], '\0' };
  uint8_t pid = static_cast<uint8_t>(strtoul(pidStr, nullptr, 16));
  if (pid != pidExpected) return false;

  if (s.length() < 6) return false;
  if (s.length() >= 6) {
    char aStr[3] = { s[4], s[5], '\0' };
    A = static_cast<uint8_t>(strtoul(aStr, nullptr, 16));
  }
  if (s.length() >= 8) {
    char bStr[3] = { s[6], s[7], '\0' };
    B = static_cast<uint8_t>(strtoul(bStr, nullptr, 16));
  } else {
    B = 0;
  }
  return true;
}

bool requestPID(uint8_t pid, uint8_t &A, uint8_t &B) {
  String cmd = "01" + String(pid, HEX);
  cmd.toUpperCase();
  elmWrite(cmd);

  String line;
  uint32_t t0 = millis();
  while (millis() - t0 < 700) {
    if (elmReadLine(line, 300)) {
      if (line.indexOf("NO DATA") >= 0) return false;
      if (line.indexOf("STOPPED") >= 0) return false;
      if (line.indexOf("ERROR") >= 0) return false;
      uint8_t a = 0;
      uint8_t b = 0;
      if (parseOBD_41(line, pid, a, b)) {
        A = a;
        B = b;
        return true;
      }
    }
  }
  return false;
}

void updateFastPIDs() {
  uint8_t A = 0;
  uint8_t B = 0;

  if (requestPID(0x0C, A, B)) {
    obd.rpm = ((A * 256) + B) / 4;
  }
  if (requestPID(0x0D, A, B)) {
    obd.speed = A;
  }
}

void updateSlowPIDs() {
  uint8_t A = 0;
  uint8_t B = 0;

  if (requestPID(0x05, A, B)) obd.coolant = static_cast<int>(A) - 40;
  if (requestPID(0x0F, A, B)) obd.iat = static_cast<int>(A) - 40;
  if (requestPID(0x11, A, B)) obd.throttle = static_cast<int>((A * 100UL) / 255UL);
  if (requestPID(0x2F, A, B)) obd.fuelLevel = static_cast<int>((A * 100UL) / 255UL);
  if (requestPID(0x04, A, B)) obd.load = static_cast<int>((A * 100UL) / 255UL);
  if (requestPID(0x0B, A, B)) obd.map = A;
  if (requestPID(0x10, A, B)) obd.maf = static_cast<int>(((256UL * A) + B) / 100UL);
}

bool elmInit() {
  SerialBT.begin("ESP32-OBD");
  delay(300);

  if (!SerialBT.connect(ELM_BT_NAME)) {
    uint32_t t0 = millis();
    while (!SerialBT.connected() && millis() - t0 < 8000) {
      SerialBT.connect(ELM_BT_NAME);
      delay(500);
    }
  }
  if (!SerialBT.connected()) return false;

  auto sendAT = [&](const char *at, uint32_t waitMs = 300) {
    elmWrite(at);
    String line;
    uint32_t t0 = millis();
    while (millis() - t0 < waitMs) {
      if (elmReadLine(line, waitMs)) {
        // opcjonalnie wypisz line do Serial
      }
    }
  };

  sendAT("ATZ", 800);
  sendAT("ATE0", 200);
  sendAT("ATL0", 200);
  sendAT("ATS0", 200);
  sendAT("ATH1", 200);
  sendAT("ATSP0", 300); // auto protocol (zmień na ATSP6 dla CAN 11-bit 500k)

  elmWrite("0100");
  String line;
  elmReadLine(line, 600);

  return true;
}

// ===== Szkic Arduino =====

void setup() {
  Serial.begin(115200);
  delay(200);

  tft.begin(SPI_FREQ_TFT);
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  ts.begin(SPI_FREQ_TOUCH);
  ts.setRotation(1);

  tftHeader("ELM327 — Connecting...");
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.print("BT: ");
  tft.print(ELM_BT_NAME);

  if (!elmInit()) {
    tft.setCursor(10, 70);
    tft.setTextColor(ILI9341_RED);
    tft.print("Nie polaczono z ELM!");
  } else {
    tft.setCursor(10, 70);
    tft.setTextColor(ILI9341_GREEN);
    tft.print("Polaczono.");
  }

  delay(500);
  drawAll();
}

void loop() {
  uint32_t now = millis();

  if (now - tFast >= POLL_FAST_MS && SerialBT.connected()) {
    tFast = now;
    updateFastPIDs();
    if (currentPage == PAGE_MAIN) {
      drawMainPage();
    }
  }

  if (now - tSlow >= POLL_SLOW_MS && SerialBT.connected()) {
    tSlow = now;
    updateSlowPIDs();
    if (currentPage == PAGE_EXTRA) {
      drawExtraPage();
    }
  }

  if (ts.touched()) {
    if (currentPage == PAGE_MAIN) {
      if (touchHit(10, 220, 90, 20)) {
        currentPage = PAGE_EXTRA;
        drawAll();
        delay(250);
      }
    } else {
      if (touchHit(10, 220, 110, 20)) {
        currentPage = PAGE_MAIN;
        drawAll();
        delay(250);
      }
    }
  }

  static uint32_t tReconnect = 0;
  if (!SerialBT.connected() && (now - tReconnect > 2000)) {
    tReconnect = now;
    SerialBT.connect(ELM_BT_NAME);
  }
}

/*
  ===== Dodatkowe PID-y producenta (Mode 22) =====
  Aby dodać dane specyficzne dla BMW, użyj funkcji requestPID w wariancie z komendą 22XXXX.
  Po otrzymaniu odpowiedzi 62 XXXX możesz sparsować dodatkowe bajty w pętli i zaktualizować strukturę `obd`.
*/
