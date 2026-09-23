/*
 * BlackWire
 * Arduino-based hardware security/electronics project.
 *
 * Copyright (C) 2026 Piper_Lovejoy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>

#include <WiFiS3.h>

#include <EEPROM.h>

#include <LiquidCrystal.h>

#include <stdarg.h>

#include <string.h>

define ENABLE_BLE

ifdef ENABLE_BLE

#include <ArduinoBLE.h>

#endif

#define FW_VERSION "v6.1"

// ─── Limits ──────────────────────────────────────────────────

constexpr int SSID_MAX_LEN  = 32;

constexpr int MAX_KNOWN     = 150;

constexpr int MAX_SCAN      = 30;

constexpr int MAX_BLE       = 20;

constexpr int NUM_CHANNELS  = 14;      

// ─── Soft-AP for web dashboard ───────────────────────────────

#define AP_SSID "BlackWire-AP"

#define AP_PASS "survey1234"

// ─── Data types ──────────────────────────────────────────────

struct KnownNet    { char ssid[SSID_MAX_LEN + 1]; uint8_t enc; };          

struct ChanStat    { uint8_t count; int8_t bestRSSI; };

struct ScanEntry   { char ssid[SSID_MAX_LEN + 1]; int rssi; uint8_t enc; uint8_t channel; bool isNew; };

struct BleEntry    { char name[32]; char addr[18]; int8_t rssi; };

struct ZoneSummary { int total, score, strong, open, newCount, bestChan; bool valid; };

inline void copyStr(char* dst, const char* src, size_t size) {

if (!size) return;

  strncpy(dst, src ? src : "", size - 1);

  dst[size - 1] = '\0';

}

// -- Ui --

enum Button : uint8_t { BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_SELECT, BTN_NONE };

extern LiquidCrystal lcd;

void   uiBegin();

Button rawButton();

Button readButton();

Button waitButton(uint32_t timeoutMs);

void lcdRow (uint8_t row, const char* s);

void lcdRowf(uint8_t row, const char* fmt, ...);

void lcdShow(const char* r0, const char* r1 = "");

int  browse(int count, void (*draw)(int), uint32_t idleMs);

// -- Storage --

extern KnownNet knownNets[MAX_KNOWN];

extern int      knownCount;

void loadKnown();

int  findKnown(const char* ssid);     // index or -1*

bool addKnown(const char* ssid, uint8_t enc);

void wipeKnown();

extern ScanEntry   scanCache[MAX_SCAN];

extern int         scanCacheCount;

extern uint32_t    lastScanTime;

extern ChanStat    chanStats[NUM_CHANNELS + 1];

extern ZoneSummary lastZone;

int  rssiPercent(int rssi);

int  doScan(bool showOnLcd);          // fills cache/stats/zone; returns AP count or -1*

void clearScanData();

void scanNetworks();

void signalTracker();

void channelAdvisor();

void autoScan();

// -- BleScan --

extern BleEntry bleCache[MAX_BLE];

extern int      bleCacheCount;

void bleScan();

// -- System --

int  freeRam();

void uptimeStr(char* buf, size_t len);   

void wipeAll();                          

void systemInfo();

void clearDataMenu();


void playGame();   


void webDashboard();   



// ============================================================

//  Ui — LCD + button helpers

// ============================================================

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);   

static constexpr uint32_t DEBOUNCE_MS = 200;

static Button   lastBtn     = BTN_NONE;

static uint32_t lastBtnTime = 0;

void uiBegin() { lcd.begin(16, 2); }

Button rawButton() {

  int v = analogRead(A0);

if (v < 60)  return BTN_RIGHT;

if (v < 200) return BTN_UP;

if (v < 400) return BTN_DOWN;

if (v < 600) return BTN_LEFT;

if (v < 800) return BTN_SELECT;

return BTN_NONE;

}

Button readButton() {

  Button b = rawButton();

if (b == BTN_NONE) { lastBtn = BTN_NONE; return BTN_NONE; }

  uint32_t now = millis();

if (b == lastBtn && now - lastBtnTime < DEBOUNCE_MS) return BTN_NONE;

  lastBtn = b;

  lastBtnTime = now;

return b;

}

Button waitButton(uint32_t timeoutMs) {

  uint32_t start = millis();

while (millis() - start < timeoutMs) {

    Button b = readButton();

if (b != BTN_NONE) return b;

    delay(5);

  }

return BTN_NONE;

}

void lcdRow(uint8_t row, const char* s) {

  lcd.setCursor(0, row);

  uint8_t i = 0;

for (; s[i] && i < 16; i++) lcd.write(s[i]);

for (; i < 16; i++)         lcd.write(' ');

}

void lcdRowf(uint8_t row, const char* fmt, ...) {

  char buf[40];

  va_list ap;

  va_start(ap, fmt);

  vsnprintf(buf, sizeof buf, fmt, ap);

  va_end(ap);

  lcdRow(row, buf);

}

void lcdShow(const char* r0, const char* r1) {

  lcdRow(0, r0);

  lcdRow(1, r1);

}

int browse(int count, void (*draw)(int), uint32_t idleMs) {

  int idx = 0;

  uint32_t last = millis();

while (millis() - last < idleMs) {

    draw(idx);

switch (readButton()) {

case BTN_UP:     idx = (idx + count - 1) % count; last = millis(); break;

case BTN_DOWN:   idx = (idx + 1) % count;         last = millis(); break;

case BTN_SELECT: return idx;

case BTN_LEFT:   return -1;

default: break;

    }

    delay(100);

  }

return -1;

}



// ============================================================

//  Storage — known-network store (EEPROM backed)

//  Layout: [0-1] count lo/hi, [2-3] reserved, then 34-byte slots

//          (32 SSID + NUL + enc).  Same as v6.0 — old data loads.

// ============================================================

static constexpr int DATA_START = 4;

static constexpr int SLOT_SIZE  = SSID_MAX_LEN + 2;   // SSID + NUL + enc*

KnownNet knownNets[MAX_KNOWN];

int      knownCount = 0;

static int slotAddr(int i) { return DATA_START + i * SLOT_SIZE; }

static void saveCount() {

  EEPROM.update(0, knownCount & 0xFF);

  EEPROM.update(1, knownCount >> 8);

}


static void saveSlot(int i) {

  int base = slotAddr(i);

for (int j = 0; j <= SSID_MAX_LEN; j++) EEPROM.update(base + j, (uint8_t)knownNets[i].ssid[j]);

  EEPROM.update(base + SSID_MAX_LEN + 1, knownNets[i].enc);

}

void loadKnown() {

  knownCount = EEPROM.read(0) | (EEPROM.read(1) << 8);

if (knownCount > MAX_KNOWN) { knownCount = 0; return; }   

for (int i = 0; i < knownCount; i++) {

    int base = slotAddr(i);

for (int j = 0; j <= SSID_MAX_LEN; j++) knownNets[i].ssid[j] = (char)EEPROM.read(base + j);

    knownNets[i].ssid[SSID_MAX_LEN] = '\0';

    knownNets[i].enc = EEPROM.read(base + SSID_MAX_LEN + 1);

  }

}

int findKnown(const char* ssid) {

for (int i = 0; i < knownCount; i++)

if (strcmp(ssid, knownNets[i].ssid) == 0) return i;

return -1;

}

bool addKnown(const char* ssid, uint8_t enc) {

if (knownCount >= MAX_KNOWN) return false;

  KnownNet& k = knownNets[knownCount];

  memset(&k, 0, sizeof k);

  copyStr(k.ssid, ssid, sizeof k.ssid);

  k.enc = enc;

  saveSlot(knownCount);

  knownCount++;

  saveCount();

return true;

}

void wipeKnown() {

for (int i = 0; i < DATA_START + MAX_KNOWN * SLOT_SIZE; i++) EEPROM.update(i, 0);

  knownCount = 0;

}



// ============================================================

//  Scanner — Wi-Fi scanning, channel stats, related features

// ============================================================

ScanEntry   scanCache[MAX_SCAN];

int         scanCacheCount = 0;

uint32_t    lastScanTime   = 0;

ChanStat    chanStats[NUM_CHANNELS + 1];

ZoneSummary lastZone = {};

int rssiPercent(int rssi) {

return map(constrain(rssi, -100, -30), -100, -30, 0, 100);

}

void clearScanData() {

  scanCacheCount = 0;

  lastZone.valid = false;

}

static void resetChanStats() {

  memset(chanStats, 0, sizeof chanStats);

for (auto& c : chanStats) c.bestRSSI = -127;

}


static int bestChannel() {

  const int pref[] = { 1, 6, 11 };

  int best = pref[0];

for (int c : pref) if (chanStats[c].count < chanStats[best].count) best = c;

return best;

}

// ─── Core scan ───────────────────────────────────────────────

int doScan(bool showOnLcd) {

if (showOnLcd) lcdShow("SCANNING...", "PLEASE WAIT");

  int n = WiFi.scanNetworks();

if (n < 0) return -1;

  resetChanStats();

  scanCacheCount = 0;

  ZoneSummary z = { n, 0, 0, 0, 0, 0, true };

for (int i = 0; i < n && i < MAX_SCAN; i++) {

    ScanEntry& e = scanCache[scanCacheCount++];

    copyStr(e.ssid, WiFi.SSID(i), sizeof e.ssid);

    e.rssi    = WiFi.RSSI(i);

    e.enc     = WiFi.encryptionType(i);

    e.channel = WiFi.channel(i);

    bool open = (e.enc == ENC_TYPE_NONE);

if (e.channel >= 1 && e.channel <= NUM_CHANNELS) {

      ChanStat& cs = chanStats[e.channel];

      cs.count++;

if (e.rssi > cs.bestRSSI) cs.bestRSSI = e.rssi;

    }

    z.score += map(constrain(e.rssi, -100, -30), -100, -30, 0, 10);

if (e.rssi > -55) z.strong++;

if (open)         z.open++;

    e.isNew = e.ssid[0] && findKnown(e.ssid) < 0;

if (e.isNew) { addKnown(e.ssid, open ? 0 : 1); z.newCount++; }

    char line[96];

    snprintf(line, sizeof line, "[BW] %s,CH:%d,RSSI:%d,Q:%d%%,ENC:%s%s",

             e.ssid, e.channel, e.rssi, rssiPercent(e.rssi),

             open ? "OPEN" : "LOCK", e.isNew ? ",NEW" : "");

    Serial.println(line);

if (showOnLcd) {

      lcdRow(0, e.ssid);

      lcdRowf(1, "CH%-2d %3d%% %s", e.channel, rssiPercent(e.rssi), open ? "OPN" : "LCK");

      delay(500);

    }

  }

  z.bestChan   = bestChannel();

  lastZone     = z;

  lastScanTime = millis();

return n;

}

// ─── Feature: scan networks ──────────────────────────────────

void scanNetworks() {

  int n = doScan(true);

if (n < 0) { lcdShow("SCAN FAILED", "CHECK MODULE"); delay(2000); return; }

  char r0[17];

  snprintf(r0, sizeof r0, "NEW:%-3d TTL:%-3d", lastZone.newCount, n);

  lcdShow(r0, "SELECT=CONTINUE");

  waitButton(4000);

}

// ─── Feature: signal tracker ─────────────────────────────────

static void drawScanPick(int i) {

  lcdRow(0, scanCache[i].ssid);

  lcdRowf(1, "CH%-2d %3d%%", scanCache[i].channel, rssiPercent(scanCache[i].rssi));

}

static int liveRssi(const char* ssid) {

  int best = -127;

  int n = WiFi.scanNetworks();

for (int i = 0; i < n; i++) {

    const char* s = WiFi.SSID(i);

if (s && strcmp(s, ssid) == 0 && WiFi.RSSI(i) > best) best = WiFi.RSSI(i);

  }

return best;

}

void signalTracker() {

if (!scanCacheCount) { lcdShow("NO SCAN DATA", "RUN SCAN FIRST"); delay(2000); return; }

  lcdShow("SIGNAL TRACKER", "UP/DN SEL=OK");

  delay(800);

  int pick = browse(scanCacheCount, drawScanPick, 20000);

if (pick < 0) return;

  char target[SSID_MAX_LEN + 1];

  copyStr(target, scanCache[pick].ssid, sizeof target);

  lcdShow("TRACKING...", target);

  delay(600);

  int hist[8];

for (int& h : hist) h = -127;          

  uint8_t head = 0;

  uint32_t end = millis() + 60000UL;

while (millis() < end) {

    int rssi = liveRssi(target);

    hist[head++ % 8] = rssi;

    char bar[9];

for (int b = 0; b < 8; b++) {        
      int v = hist[(head + b) % 8];

      bar[b] = v > -50 ? '#' : v > -70 ? '+' : v > -90 ? '-' : '.';

    }

    bar[8] = '\0';

    lcdRow(0, target);

    lcdRowf(1, "%3d%% [%s]", rssiPercent(rssi), bar);

    Serial.print(F("[BW-TRACK] ")); Serial.print(target);

    Serial.print(F(",RSSI:"));      Serial.print(rssi);

    Serial.print(F(",Q:"));         Serial.print(rssiPercent(rssi)); Serial.println('%');

    Button b = waitButton(2000);

if (b == BTN_SELECT || b == BTN_LEFT) break;

  }

  lcdShow("TRACK ENDED"); delay(1000);

}

// ─── Feature: channel advisor ────────────────────────────────

void channelAdvisor() {

  lcdShow("CHANNEL ADVISOR", "SCANNING...");

if (doScan(false) < 0) { lcdShow("SCAN FAILED"); delay(2000); return; }

  int best = lastZone.bestChan;

  char r0[17];

  snprintf(r0, sizeof r0, "USE CHANNEL %d", best);

  lcdRow(0, r0);

  lcdRowf(1, "%d net(s) on CH%d", chanStats[best].count, best);

  Serial.println(F("[BW-CHAN] ===="));

for (int c = 1; c <= NUM_CHANNELS; c++) {

if (!chanStats[c].count) continue;

    Serial.print(F("  CH")); Serial.print(c);

    Serial.print(F(": "));   Serial.print(chanStats[c].count);

    Serial.print(F(" nets, best RSSI: ")); Serial.println(chanStats[c].bestRSSI);

  }

  Serial.print(F("[BW-CHAN] Recommended: CH")); Serial.println(best);

  Button b;

do { b = waitButton(5000); } while (b != BTN_SELECT && b != BTN_LEFT && b != BTN_NONE);

}

// ─── Feature: auto scan ──────────────────────────────────────

static const uint16_t AUTO_SECS[]   = { 30, 60, 300 };

static const char*    AUTO_LABELS[] = { "30 SEC", "60 SEC", "5 MIN" };

static void drawAutoChoice(int i) { lcdRow(1, AUTO_LABELS[i]); }

void autoScan() {

  lcdShow("AUTO SCAN EVERY", "UP/DN SEL=START");

  delay(800);

  int choice = browse(3, drawAutoChoice, 15000);

if (choice < 0) return;

  uint32_t interval = AUTO_SECS[choice] * 1000UL;

  uint32_t next = 0;

  char title[17];

  snprintf(title, sizeof title, "AUTO: %s", AUTO_LABELS[choice]);

  lcdShow(title, "LEFT=STOP");

  delay(800);

for (;;) {

    Button b = readButton();

if (b == BTN_LEFT || b == BTN_SELECT) break;

if ((int32_t)(millis() - next) >= 0) {

      int n = doScan(false);

      next = millis() + interval;

if (n < 0) {

        lcdShow("SCAN FAILED", "RETRYING...");

      } else {

        lcdRow(0, title);

        lcdRowf(1, "TTL:%-3d NEW:%-3d", n, lastZone.newCount);

      }

    }

    delay(200);

  }

  lcdShow("AUTO SCAN STOP"); delay(800);

}



// ============================================================

//  BleScan — BLE scanner (needs ENABLE_BLE, defined above)

// ============================================================

BleEntry bleCache[MAX_BLE];

int      bleCacheCount = 0;

#ifdef ENABLE_BLE

static void drawBlePick(int i) {

  lcdRow(0, bleCache[i].name);

  lcdRow(1, bleCache[i].addr);

}

void bleScan() {

  lcdShow("BLE SCAN", "INITIALIZING...");

  bleCacheCount = 0;

if (!BLE.begin()) { lcdShow("BLE FAILED", "CHECK HARDWARE"); delay(2000); return; }

  lcdShow("BLE SCANNING...", "10 SECONDS");

  BLE.scan();

  uint32_t end = millis() + 10000UL;

while (millis() < end && bleCacheCount < MAX_BLE) {

    BLEDevice dev = BLE.available();

if (!dev) continue;


    String addr = dev.address();

    String name = dev.hasLocalName() ? dev.localName() : String("[unknown]");

    bool dup = false;

for (int i = 0; i < bleCacheCount && !dup; i++) dup = (addr == bleCache[i].addr);

if (dup) continue;

    BleEntry& e = bleCache[bleCacheCount++];

    copyStr(e.name, name.c_str(), sizeof e.name);

    copyStr(e.addr, addr.c_str(), sizeof e.addr);

    e.rssi = dev.rssi();

    Serial.print(F("[BW-BLE] ")); Serial.print(e.name); Serial.print(',');

    Serial.print(e.addr); Serial.print(F(",RSSI:")); Serial.println(e.rssi);

    lcdRow(0, e.name);

    lcdRowf(1, "%.11s %ddBm", e.addr, e.rssi);

  }

  BLE.stopScan();

  BLE.end();

  char r0[17];

  snprintf(r0, sizeof r0, "BLE: %d DEVICES", bleCacheCount);

  lcdShow(r0, "SELECT=BROWSE");

  delay(1200);

if (bleCacheCount) browse(bleCacheCount, drawBlePick, 20000);

}

#else

void bleScan() {

  lcdShow("BLE SCAN", "LIB NOT ENABLED");

  Serial.println(F("[BW-BLE] Define ENABLE_BLE to use BLE."));

  delay(2000);

}

#endif



// ============================================================

//  System — system info + data wipe

// ============================================================


extern "C" char* sbrk(int incr);

int freeRam() {

  char stackMarker;

return (int)(&stackMarker - sbrk(0));

}

void uptimeStr(char* buf, size_t len) {

  unsigned long s = millis() / 1000UL;

  unsigned long m = s / 60; s %= 60;

  unsigned long h = m / 60; m %= 60;

if (h) snprintf(buf, len, "%luh %lum", h, m);

else   snprintf(buf, len, "%lum %lus", m, s);

}

void wipeAll() {

  wipeKnown();

  clearScanData();

}

// ─── System info ─────────────────────────────────────────────

static char sUp[17], sRam[17], sNodes[17];

static void drawSysPage(int p) {

if      (p == 0) lcdShow("UPTIME", sUp);

else if (p == 1) lcdShow(sRam);

else             lcdShow(sNodes);

}

void systemInfo() {

  uptimeStr(sUp, sizeof sUp);

  snprintf(sRam,   sizeof sRam,   "RAM: %d bytes", freeRam());

  snprintf(sNodes, sizeof sNodes, "NODES: %d/%d", knownCount, MAX_KNOWN);

  browse(3, drawSysPage, 15000);

  Serial.println(F("[BW-SYS] ===="));

  Serial.print(F("  Uptime  : ")); Serial.println(sUp);

  Serial.print(F("  Free RAM: ")); Serial.println(freeRam());

  Serial.print(F("  Nodes   : ")); Serial.print(knownCount);

  Serial.print('/');              Serial.println(MAX_KNOWN);

}

// ─── Clear data ──────────────────────────────────────────────

void clearDataMenu() {

  lcdShow("WIPE ALL NODES?", "SEL=YES  LFT=NO");

  Button b = waitButton(8000);

if (b == BTN_SELECT) {

    lcdShow("WIPING...");

    wipeAll();

    lcdShow("DONE", "0 NODES REMAIN");

    Serial.println(F("[BW] EEPROM wiped."));

    delay(2000);

  } else {

    lcdShow(b == BTN_NONE ? "TIMEOUT" : "CANCELLED", b == BTN_NONE ? "CANCELLED" : "");

    delay(800);

  }

}



// ============================================================

//  Game — break-time mini game (Space-Invaders style)

// ============================================================

namespace {

constexpr int8_t  W = 16, H = 4;      // logical grid: 2 pixel-rows per LCD row*

constexpr uint8_t ALIENS  = 8;

constexpr uint16_t STEP_MS = 100;

// Custom LCD character slots

enum : uint8_t { C_SHIP, C_BULLET_UP, C_BULLET_DOWN, C_SHIP_BULLET,

                 C_ALIEN1, C_ALIEN2, C_ALIEN1_BULLET, C_ALIEN2_BULLET };

byte spShip[]        = { 0b00000,0b00000,0b00000,0b00000,0b00000,0b00100,0b01110,0b11011 };

byte spShipBullet[]  = { 0b00000,0b00100,0b00100,0b00000,0b00000,0b00100,0b01110,0b11011 };

byte spBulletDown[]  = { 0b00000,0b00000,0b00000,0b00000,0b00000,0b00100,0b00100,0b00000 };

byte spBulletUp[]    = { 0b00000,0b00100,0b00100,0b00000,0b00000,0b00000,0b00000,0b00000 };

byte spAlien1[]      = { 0b01010,0b10101,0b01110,0b10001,0b00000,0b00000,0b00000,0b00000 };

byte spAlien2[]      = { 0b01010,0b10101,0b01110,0b01010,0b00000,0b00000,0b00000,0b00000 };

byte spAlien1Bullet[]= { 0b01010,0b10101,0b01110,0b10001,0b00000,0b00100,0b00100,0b00000 };

byte spAlien2Bullet[]= { 0b01010,0b10101,0b01110,0b01010,0b00000,0b00100,0b00100,0b00000 };

struct Bullet {

  int8_t x = 0, y = 0, dir = 0;

  bool active = false;

  void move() { y += dir; if (y < 0 || y >= H) { y -= dir; active = false; } }

};

struct Alien {

  int8_t x = 0, y = 0, dir = 1;

  bool alive = false, frame = false;

  void move() { x += dir; frame = !frame; if (x < 0 || x >= W) x -= dir; }

};

struct Ship { int8_t x = W / 2, y = 3; } ship;

Bullet shipBullet, alienBullets[ALIENS];

Alien  aliens[ALIENS];

uint8_t  tick = 0, alienStep = 5, aliensLeft = 0, level = 1;

int      fireOdds = 20, score = 0;

bool     running = false, shipHit = false;

void setupChars() {

  static bool done = false;

if (done) return;

  lcd.createChar(C_SHIP,          spShip);

  lcd.createChar(C_BULLET_UP,     spBulletUp);

  lcd.createChar(C_BULLET_DOWN,   spBulletDown);

  lcd.createChar(C_SHIP_BULLET,   spShipBullet);

  lcd.createChar(C_ALIEN1,        spAlien1);

  lcd.createChar(C_ALIEN2,        spAlien2);

  lcd.createChar(C_ALIEN1_BULLET, spAlien1Bullet);

  lcd.createChar(C_ALIEN2_BULLET, spAlien2Bullet);

  randomSeed(analogRead(A1));

  done = true;

}

void initLevel(uint8_t l) {

  level = l > 42 ? 42 : l;

  ship.x = W / 2;

  shipBullet.active = false;

for (uint8_t i = 0; i < ALIENS; i++) {

    aliens[i]  = Alien();

    aliens[i].x = i;

    aliens[i].alive = true;

    alienBullets[i].active = false;

  }

  tick       = 0;

  aliensLeft = ALIENS;

  alienStep  = max(1, 6 - level / 2);

  fireOdds   = max(10, 110 - level * 10);

  lcdRowf(0, "LEVEL %d", level);

  lcdRow(1, "");

  delay(1000);

  lcd.clear();

}

void render() {

  char buf[H / 2][W + 1];

for (auto& row : buf) { memset(row, ' ', W); row[W] = '\0'; }

  bool shipDrawn = false;

if (shipBullet.active) {

if (ship.x == shipBullet.x && shipBullet.y == 2) {

      buf[shipBullet.y / 2][shipBullet.x] = (char)C_SHIP_BULLET;

      shipDrawn = true;

    } else {

      buf[shipBullet.y / 2][shipBullet.x] = (char)(shipBullet.y % 2 ? C_BULLET_DOWN : C_BULLET_UP);

    }

  }

for (auto& a : aliens)

if (a.alive) buf[a.y / 2][a.x] = a.frame ? C_ALIEN1 : C_ALIEN2;

for (auto& b : alienBullets) {

if (!b.active) continue;

    bool merged = false;

for (auto& a : aliens) {                     

if (a.alive && a.x == b.x && b.y == 1) {

        buf[b.y / 2][b.x] = a.frame ? C_ALIEN1_BULLET : C_ALIEN2_BULLET;

        merged = true;

      }

    }

if (!merged) buf[b.y / 2][b.x] = (char)(b.y % 2 ? C_BULLET_DOWN : C_BULLET_UP);

  }

for (uint8_t r = 0; r < H / 2; r++) { lcd.setCursor(0, r); lcd.print(buf[r]); }

if (!shipDrawn) { lcd.setCursor(ship.x, ship.y / 2); lcd.write(byte(C_SHIP)); }

}

void handleInput() {

switch (rawButton()) {     

case BTN_RIGHT: if (ship.x < W - 1) ship.x++; break;

case BTN_LEFT:  if (ship.x > 0)     ship.x--; break;

case BTN_DOWN:

      lcdShow("PAUSED", "SEL=EXIT DN=RES");

while (rawButton() == BTN_DOWN) delay(10);

for (;;) {

        Button b = rawButton();

if (b == BTN_SELECT) { running = false; break; }

if (b == BTN_DOWN)   { while (rawButton() == BTN_DOWN) delay(10); break; }

      }

break;

case BTN_SELECT:

case BTN_UP:

if (!shipBullet.active) {

        shipBullet = Bullet();

        shipBullet.x = ship.x; shipBullet.y = ship.y; shipBullet.dir = -1; shipBullet.active = true;

      }

break;

default: break;

  }

}

void stepWorld() {

if (shipBullet.active) shipBullet.move();

  bool aliensMove = !(tick % alienStep);

for (uint8_t i = 0; i < ALIENS; i++) {

    Bullet& ab = alienBullets[i];

    Alien& a   = aliens[i];

if (ab.active) {

      ab.move();

if (ab.x == ship.x && ab.y == ship.y) { running = false; shipHit = true; }

    }

if (aliensMove) a.move();

if (a.alive && shipBullet.active && a.x == shipBullet.x && a.y == shipBullet.y) {

      a.alive = false;


      shipBullet.active = false;

      score += 10 * level;

      aliensLeft--;

    }

if (a.alive && !ab.active && !random(fireOdds)) {

      ab.x = a.x; ab.y = a.y + 1; ab.dir = 1; ab.active = true;

    }

  }

if (aliensMove && (aliens[0].x == 0 || aliens[ALIENS - 1].x == W - 1))

for (auto& a : aliens) a.dir = -a.dir;

}

void showGameOver() {

  char r1[17];

  snprintf(r1, sizeof r1, "SCORE: %d", score);

  lcdShow("GAME OVER", r1);

  Serial.print(F("[BW-GAME] Game over. Score: ")); Serial.println(score);

  waitButton(4000);

}

}  

void playGame() {

  setupChars();

  lcdShow("BREAK TIME!", "GET READY...");

  delay(1200);

  score = 0;

  shipHit = false;

  running = true;

  initLevel(1);

while (running) {

    handleInput();

if (!running) break;

    stepWorld();

if (!running) break;

    render();

    tick++;

    delay(STEP_MS);

if (!aliensLeft) initLevel(level + 1);

  }

if (shipHit) showGameOver();

else { lcdShow("EXITED GAME"); delay(700); }

}



// ============================================================

//  WebDash — soft-AP web dashboard

// ============================================================

static WiFiServer server(80);

// ─── Routing ─────────────────────────────────────────────────

enum Action : uint8_t { ACT_NONE, ACT_SCAN, ACT_ZONE, ACT_CHANMAP, ACT_BLE, ACT_SYSINFO, ACT_CLEAR };

struct Route { const char* path; Action act; };

static const Route ROUTES[] = {

  { "/scan", ACT_SCAN }, { "/zone", ACT_ZONE }, { "/chanmap", ACT_CHANMAP },

  { "/ble", ACT_BLE },   { "/sysinfo", ACT_SYSINFO }, { "/clear", ACT_CLEAR },

};

static Action      gAction = ACT_NONE;

static const char* gError  = nullptr;

static void runAction(const char* path) {
  gAction = ACT_NONE;
  gError  = nullptr;

  for (const Route& r : ROUTES) {
    if (!strcmp(path, r.path)) {
      gAction = r.act;
      break;
    }
  }

  switch (gAction) {
    case ACT_SCAN:
    case ACT_ZONE:
      lcdShow(gAction == ACT_SCAN ? "WEB: SCANNING..." : "WEB: ZONE...");
      if (doScan(false) < 0) gError = "Scan failed";
      lcdShow(gError ? "WEB SCAN FAILED" : "WEB SCAN DONE");
      break;

    case ACT_CHANMAP:
      if (!lastZone.valid) gError = "Run a scan first";
      break;

    case ACT_BLE:
#ifdef ENABLE_BLE
      bleScan();
#else
      gError = "BLE support disabled";
#endif
      break;

    case ACT_SYSINFO:
      break;

    case ACT_CLEAR:
      wipeAll();
      lcdShow("WEB: EEPROM", "CLEARED");
      break;

    default:
      break;
  }
}

// ─── Output helpers ──────────────────────────────────────────

static void wf(WiFiClient& c, const char* fmt, ...) {

  char buf[192];

  va_list ap;

  va_start(ap, fmt);

  vsnprintf(buf, sizeof buf, fmt, ap);

  va_end(ap);

  c.print(buf);

}

static void wEsc(WiFiClient& c, const char* s) {

for (; *s; s++) {

switch (*s) {

case '<': c.print(F("&lt;"));   break;

case '>': c.print(F("&gt;"));   break;

case '&': c.print(F("&amp;"));  break;

case '"': c.print(F("&quot;")); break;

default:  c.print(*s);

    }

  }

}

static void wEnc(WiFiClient& c, bool open) {

  c.print(open ? F("<span class='open'>OPEN</span>") : F("WPA/WPA2"));

}

static void wStat(WiFiClient& c, const char* label, const char* fmt, ...) {

  char v[24];

  va_list ap;

  va_start(ap, fmt);

  vsnprintf(v, sizeof v, fmt, ap);

  va_end(ap);

  wf(c, "<div class='stat'><b>%s</b><small>%s</small></div>", v, label);

}

static void wBox(WiFiClient& c, const char* fmt, ...) {

  char buf[192];

  va_list ap;

  va_start(ap, fmt);

  vsnprintf(buf, sizeof buf, fmt, ap);

  va_end(ap);

  c.print(F("<div class='info-box'>")); c.print(buf); c.print(F("</div>"));

}

// ─── Page sections ───────────────────────────────────────────

static const char PAGE_HEAD[] PROGMEM =

  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"

  "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"

  "<meta name='viewport' content='width=device-width,initial-scale=1'>"

  "<meta http-equiv='refresh' content='30'><title>BlackWire</title><style>"

  "body{font-family:Arial,sans-serif;font-size:14px;color:#222;background:#f5f5f5;margin:0}"

  "header{background:#fff;border-bottom:1px solid #ddd;padding:12px 20px;display:flex;"

    "justify-content:space-between;align-items:center}"

  "header h1{font-size:1.1em;margin:0}header small{color:#888;font-size:.8em}"

  "main{max-width:720px;margin:20px auto;padding:0 16px}"

  "h2{font-size:.85em;text-transform:uppercase;letter-spacing:.05em;color:#555;"

    "border-bottom:1px solid #ddd;padding-bottom:4px;margin:20px 0 10px}"

  ".stats,.actions{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:8px}"

  ".stat{flex:1;min-width:90px;background:#fff;border:1px solid #ddd;padding:10px;"

    "text-align:center;border-radius:4px}"

  ".stat b{display:block;font-size:1.6em;color:#333}"

  ".stat small{font-size:.75em;color:#888;text-transform:uppercase}"

  "a.btn{display:inline-block;padding:8px 14px;border:1px solid #bbb;border-radius:4px;"

    "background:#fff;color:#333;text-decoration:none;font-size:.85em}"

  "a.btn:hover{background:#eee}a.btn.danger{color:#c00;border-color:#f5a0a0}"

  "table{width:100%;border-collapse:collapse;font-size:.85em;background:#fff;border:1px solid #ddd}"

  "th{text-align:left;padding:7px 10px;border-bottom:1px solid #ddd;background:#f0f0f0;"

    "font-size:.8em;text-transform:uppercase;color:#555}"

  "td{padding:6px 10px;border-bottom:1px solid #eee}tr:last-child td{border-bottom:none}"

  ".open{color:#c07000;font-weight:bold}.new{color:#1a7a1a;font-weight:bold}"

  ".bar-wrap{display:inline-block;vertical-align:middle;width:60px;height:8px;"

    "background:#eee;border-radius:3px;margin-right:6px}"

  ".bar-fill{display:block;height:100%;background:#4a90d9;border-radius:3px}"

  ".warn{background:#fff8e1;border:1px solid #ffe082;padding:10px;border-radius:4px;"

    "margin-top:6px;font-size:.85em}"

  ".info-box{background:#fff;border:1px solid #ddd;border-radius:4px;padding:12px;"

    "font-size:.85em;line-height:1.8}"

  "footer{text-align:center;color:#aaa;font-size:.75em;padding:20px}"

  "</style></head><body>"

  "<header><h1>BlackWire</h1><small>UNO R4 WiFi &mdash; RF Site Survey</small></header><main>";

static const char PAGE_ACTIONS[] PROGMEM =

  "<h2>Actions</h2><div class='actions'>"

  "<a class='btn' href='/scan'>Scan Networks</a>"

  "<a class='btn' href='/zone'>Zone Analysis</a>"

  "<a class='btn' href='/chanmap'>Channel Map</a>"

  "<a class='btn' href='/ble'>BLE Devices</a>"

  "<a class='btn' href='/sysinfo'>System Info</a>"

  "<a class='btn danger' href='/clear' onclick=\\"return confirm('Wipe all stored nodes?')\\">Clear EEPROM</a>"

  "</div>";

static void sendStatus(WiFiClient& c) {

  c.print(F("<h2>Status</h2><div class='stats'>"));

  wStat(c, "Stored nodes", "%d", knownCount);

  wStat(c, "Last scan",    "%d", scanCacheCount);

if (lastZone.valid) {

    wStat(c, "Open APs",     "%d", lastZone.open);

    wStat(c, "Best channel", "CH%d", lastZone.bestChan);

  }

if (lastScanTime) {

    unsigned long age = (millis() - lastScanTime) / 1000UL;

if (age < 60) wStat(c, "Scan age", "%lus ago", age);

else          wStat(c, "Scan age", "%lum ago", age / 60);

  }

  wStat(c, "Free RAM", "%d", freeRam());

  c.print(F("</div>"));

  c.print(PAGE_ACTIONS);

if (lastZone.valid && lastZone.open > 0)

    wf(c, "<div class='warn'>&#9888; %d open (unencrypted) network%s detected.</div>",

       lastZone.open, lastZone.open > 1 ? "s" : "");

}

static void sendScanTable(WiFiClient& c) {

  c.print(F("<table><tr><th>#</th><th>SSID</th><th>Ch</th><th>Signal</th><th>Enc</th><th></th></tr>"));

for (int i = 0; i < scanCacheCount; i++) {

    const ScanEntry& e = scanCache[i];

    int pct = rssiPercent(e.rssi);

    wf(c, "<tr><td>%d</td><td>", i + 1);

    wEsc(c, e.ssid);

    wf(c, "</td><td>%d</td><td><span class='bar-wrap'><span class='bar-fill' style='width:%d%%'></span></span>%d%%</td><td>",

       e.channel, pct, pct);

    wEnc(c, e.enc == ENC_TYPE_NONE);

    c.print(F("</td><td>"));

if (e.isNew) c.print(F("<span class='new'>NEW</span>"));

    c.print(F("</td></tr>"));

  }

  c.print(F("</table><p><a class='btn' href='/csv'>Download CSV</a></p>"));

}

static void sendChanMap(WiFiClient& c) {

  c.print(F("<table><tr><th>Channel</th><th>Networks</th><th>Best RSSI</th><th></th></tr>"));

for (int ch = 1; ch <= NUM_CHANNELS; ch++) {

if (!chanStats[ch].count) continue;

    wf(c, "<tr><td>%d</td><td>%d</td><td>%d dBm</td><td>%s</td></tr>",

       ch, chanStats[ch].count, chanStats[ch].bestRSSI,

       ch == lastZone.bestChan ? "<span class='new'>Recommended</span>" : "");

  }

  c.print(F("</table>"));

}

static void sendBleTable(WiFiClient& c) {

if (!bleCacheCount) { wBox(c, "No BLE devices in cache. Run a BLE scan from the device first."); return; }

  c.print(F("<table><tr><th>#</th><th>Name</th><th>Address</th><th>RSSI</th></tr>"));

for (int i = 0; i < bleCacheCount; i++) {

    wf(c, "<tr><td>%d</td><td>", i + 1);

    wEsc(c, bleCache[i].name);

    wf(c, "</td><td>%s</td><td>%d dBm</td></tr>", bleCache[i].addr, bleCache[i].rssi);

  }

  c.print(F("</table>"));

}

static void sendResult(WiFiClient& c) {

if (gError) { wf(c, "<h2>Error</h2><div class='warn'>%s</div>", gError); return; }

if (gAction == ACT_NONE) return;

  c.print(F("<h2>Result</h2>"));

switch (gAction) {

case ACT_SCAN:    sendScanTable(c); break;

case ACT_CHANMAP: sendChanMap(c);   break;

case ACT_BLE:     sendBleTable(c);  break;

case ACT_ZONE:

      wBox(c, "Networks found: %d<br>Strong (&gt;-55 dBm): %d<br>Open APs: %d<br>"

              "Avg signal: %d%%<br>Recommended channel: CH%d",

           lastZone.total, lastZone.strong, lastZone.open,

           lastZone.total ? lastZone.score * 10 / lastZone.total : 0, lastZone.bestChan);

break;

case ACT_SYSINFO: {

      char up[20]; uptimeStr(up, sizeof up);

      wBox(c, "Uptime: %s<br>Free RAM: %d bytes<br>Stored nodes: %d/%d<br>Firmware: BlackWire " FW_VERSION,

           up, freeRam(), knownCount, MAX_KNOWN);

break;

    }

case ACT_CLEAR:   wBox(c, "EEPROM wiped. All nodes removed."); break;

default: break;

  }

}

static void sendStoredNodes(WiFiClient& c) {

  wf(c, "<h2>Stored Nodes (%d)</h2>", knownCount);

if (!knownCount) { c.print(F("<p style='color:#888'>No nodes stored &mdash; run a scan first.</p>")); return; }

  c.print(F("<table><tr><th>#</th><th>SSID</th><th>Encryption</th></tr>"));

for (int i = 0; i < knownCount; i++) {

    wf(c, "<tr><td>%d</td><td>", i + 1);

    wEsc(c, knownNets[i].ssid);

    c.print(F("</td><td>"));

    wEnc(c, knownNets[i].enc == 0);

    c.print(F("</td></tr>"));

  }

  c.print(F("</table>"));

}

static void sendHTML(WiFiClient& c) {

  c.print(PAGE_HEAD);

  sendStatus(c);

  sendResult(c);

  sendStoredNodes(c);

  c.print(F("</main><footer>BlackWire " FW_VERSION " &mdash; UNO R4 WiFi &mdash; Authorized use only</footer></body></html>"));

}

static void sendCSV(WiFiClient& c) {

  c.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\n"

            "Content-Disposition: attachment; filename=\\"blackwire_scan.csv\\"\r\n"

            "Connection: close\r\n\r\nSSID,Channel,RSSI,Quality%,Encryption\r\n"));

for (int i = 0; i < scanCacheCount; i++) {

    const ScanEntry& e = scanCache[i];

    c.print('"');

for (const char* p = e.ssid; *p; p++) { if (*p == '"') c.print('"'); c.print(*p); }

    wf(c, "\",%d,%d,%d,%s\r\n", e.channel, e.rssi, rssiPercent(e.rssi),

       e.enc == ENC_TYPE_NONE ? "OPEN" : "WPA/WPA2");

  }

}

static void send405(WiFiClient& c) {

  c.print(F("HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n"

            "Allow: GET\r\nConnection: close\r\n\r\n405 Method Not Allowed\n"));

}

// ─── Request handling ────────────────────────────────────────

static void readRequestLine(WiFiClient& c, char* buf, size_t size) {

  size_t n = 0;

  uint32_t end = millis() + 3000UL;

while (c.connected() && millis() < end) {

if (!c.available()) continue;

    char ch = c.read();

if (ch == '\n') break;

if (ch != '\r' && n < size - 1) buf[n++] = ch;

  }

  buf[n] = '\0';

while (c.connected() && c.available()) c.read();   // drain headers*

}

static void handleClient(WiFiClient& c) {

  char req[256];

  readRequestLine(c, req, sizeof req);

if (strncmp(req, "GET ", 4) != 0) { send405(c); return; }

  char* path = req + 4;

for (char* p = path; *p; p++) if (*p == ' ' || *p == '?') { *p = '\0'; break; }

if (!strcmp(path, "/csv")) { sendCSV(c); return; }

  runAction(path);

  sendHTML(c);

}

// ─── Menu feature ────────────────────────────────────────────

void webDashboard() {

  lcdShow("STARTING AP...", AP_SSID);

  WiFi.disconnect();

  delay(200);

if (WiFi.beginAP(AP_SSID, AP_PASS) != WL_AP_LISTENING) {

    lcdShow("AP FAILED", "CHECK MODULE");

    delay(2000);

return;

  }

  delay(800);

  server.begin();

  IPAddress ip = WiFi.localIP();

  char ipStr[17];

  snprintf(ipStr, sizeof ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  lcdShow(ipStr, "SEL=EXIT");

  Serial.print(F("[BW-WEB] SSID: ")); Serial.println(AP_SSID);

  Serial.print(F("[BW-WEB] URL : http://")); Serial.println(ipStr);

  uint32_t lastActivity = millis();

while (millis() - lastActivity < 180000UL) {

    Button b = readButton();

if (b == BTN_SELECT || b == BTN_LEFT) break;

    WiFiClient client = server.available();

if (!client) { delay(10); continue; }

    handleClient(client);

    client.flush();

    delay(5);

    client.stop();

    lcdShow(ipStr, "SEL=EXIT");

    lastActivity = millis();

  }

  server.end();

  WiFi.disconnect();

  WiFi.end();

  delay(300);

  lcdShow("AP CLOSED", "RETURNING...");

  delay(1200);

}



// ============================================================

//  Main menu / setup / loop

// ============================================================

struct MenuItem { const char* label; void (*run)(); };

static const MenuItem MENU[] = {

  { "SCAN NETWORKS",  scanNetworks   },

  { "SIGNAL TRACKER", signalTracker  },

  { "BLE SCAN",       bleScan        },

  { "CHANNEL ADVISE", channelAdvisor },

  { "AUTO SCAN",      autoScan       },

  { "WEB DASHBOARD",  webDashboard   },

  { "SYSTEM INFO",    systemInfo     },

  { "CLEAR DATA",     clearDataMenu  },

  { "PLAY GAME",      playGame       },

};

static constexpr int MENU_COUNT = sizeof MENU / sizeof MENU[0];

static int  menuIndex = 0;

static bool menuDirty = true;

static void drawMenu() {

  lcdRowf(0, ">%s", MENU[menuIndex].label);

  lcdRowf(1, " %s", MENU[(menuIndex + 1) % MENU_COUNT].label);

}

void setup() {

  Serial.begin(9600);

  uiBegin();

  lcdShow("BLACKWIRE " FW_VERSION, "RF SURVEY TOOL");

  delay(1200);

  loadKnown();

  lcdRowf(0, "LOADED: %d NODES", knownCount);

  lcdRow(1, "");

  delay(800);

  WiFi.disconnect();

  lcdShow("READY", "USE ARROW KEYS");

  delay(600);

  Serial.println(F("\n========================================="));

  Serial.println(F("  BLACKWIRE " FW_VERSION " // ONLINE"));

  Serial.println(F("  For authorized use only."));

  Serial.println(F("========================================="));

  Serial.print(F("  Loaded nodes : ")); Serial.println(knownCount);

  Serial.print(F("  Free RAM     : ")); Serial.println(freeRam());

  Serial.println();

}

void loop() {

  Button b = readButton();

if (b == BTN_UP)   { menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT; menuDirty = true; }

if (b == BTN_DOWN) { menuIndex = (menuIndex + 1) % MENU_COUNT;              menuDirty = true; }

if (b == BTN_SELECT) {

    MENU[menuIndex].run();

    menuDirty = true;

  }

if (menuDirty) { drawMenu(); menuDirty = false; }

  delay(80);

}
