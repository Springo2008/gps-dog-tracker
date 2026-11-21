// HAND (Rx): GPS + LoRa receiver + 1.8" ST7735S. No compass.
// Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262), board: ESP32S3 Dev Module
// Libraries: Adafruit GFX, Adafruit ST7735 and ST7789, RadioLib, TinyGPSPlus
//
// Tx packet (ASCII over LoRa, 869.525 MHz): "lat,lon" | "lat,lon,sats,hdop" | "lat,lon,sats,hdop,batt"
//
// Arrow direction:
//   - walking (GPS speed > 3 km/h): arrow is relative to your walking direction (up = straight ahead)
//   - standing still: arrow is north-up (up = north), since there is no compass

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>

// TFT. SCLK moved from GPIO12 because GPIO12 is the SX1262 reset line.
#define TFT_SCLK 47
#define TFT_MOSI 35
#define TFT_CS   36
#define TFT_DC   48   // moved from 37: GPIO37 is the battery-divider enable (ADC_Ctrl)
#define TFT_RST  38
#define TFT_BLK  26

// Battery voltage: GPIO1 reads the on-board divider (390k/100k), enabled via GPIO37
#define VBAT_ADC  1
#define VBAT_CTRL 37

// GPS
#define GPS_RX 18   // GPS TXD -> GPIO18
#define GPS_TX 17   // GPS RXD -> GPIO17

// SX1262 on-board wiring (Heltec V3 / Wireless Stick Lite V3)
#define LORA_NSS  8
#define LORA_SCK  9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_DIO1 14

// Must be identical on Tx and Rx
#define LORA_FREQ 869.525
#define LORA_BW   125.0
#define LORA_SF_FAST 10   // must match Tx moving mode
#define LORA_SF_FAR  12   // must match Tx still mode
#define LORA_SF      LORA_SF_FAR
#define LOCK_LOST_MS 45000UL      // no packet this long -> scan both SFs (longest SF12 interval is ~23 s)
#define DWELL_FAST_MS 12000UL     // > one SF10 interval (~6-8 s)
#define DWELL_FAR_MS  30000UL     // > longest SF12 interval (~23 s)
#define LORA_CR   5
#define LORA_SYNC 0x12

#define DEADZONE_M   15.0
#define WALK_KMH     3.0
#define RX_TIMEOUT_MS 180000UL
#define STALE_MS      60000UL   // data older than this is shown as old (grey, banner)

Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(128, 160);
SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

volatile bool rxFlag = false;
void IRAM_ATTR onRx() { rxFlag = true; }

bool dogValid = false;
double dogLat = 0, dogLon = 0;
int dogBatt = -1;
Preferences prefs;
bool saveDirty = false;
uint32_t lastSaveMs = 0;
#define SAVE_INTERVAL_MS 30000UL   // limit flash writes

void loadSavedDog() {
  prefs.begin("dog", true);
  if (prefs.isKey("lat")) {
    dogLat = prefs.getDouble("lat", 0);
    dogLon = prefs.getDouble("lon", 0);
    dogBatt = prefs.getInt("batt", -1);
    dogValid = !(dogLat == 0 && dogLon == 0);
    Serial.printf("Restored dog pos %.6f,%.6f\n", dogLat, dogLon);
  }
  prefs.end();
}

void saveDog() {
  prefs.begin("dog", false);
  prefs.putDouble("lat", dogLat);
  prefs.putDouble("lon", dogLon);
  prefs.putInt("batt", dogBatt);
  prefs.end();
  saveDirty = false;
  lastSaveMs = millis();
}
int dogSats = -1;
float dogHdop = -1;
int ownBatt = -1;
float ownVolt = 0;
float prevVolt = 0;
bool ownCharging = false;
bool dogCharging = false;
#define CHG_STEP_V 0.04f   // voltage step that indicates USB plugged/unplugged
uint32_t lastRxMs = 0;
uint32_t lastPosMs = 0;
bool dogNoFix = false;
float lastRssi = 0;
float lastSnr = 0;
bool loraOk = false;
uint8_t curSf = LORA_SF_FAR;
uint32_t sfSwitchMs = 0;
float heading = 0;
bool headingFromCog = false;

double haversineM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double p1 = radians(lat1), p2 = radians(lat2);
  double dp = p2 - p1, dl = radians(lon2 - lon1);
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 2 * R * asin(sqrt(a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = radians(lat1), p2 = radians(lat2), dl = radians(lon2 - lon1);
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = degrees(atan2(y, x));
  return fmod(b + 360.0, 360.0);
}

void parsePacket(const String &s) {
  char buf[96];
  s.toCharArray(buf, sizeof(buf));
  if (buf[0] == '!') {   // heartbeat: dog alive but no GPS fix
    strtok(buf, ",");
    char *a = strtok(nullptr, ","), *b = strtok(nullptr, ",");
    if (a) dogSats = atoi(a);
    if (b) { int bv = atoi(b); dogCharging = bv >= 200; dogBatt = dogCharging ? bv - 200 : bv; }
    dogNoFix = true;
    lastRxMs = millis();
    return;
  }
  double v[5];
  int n = 0;
  for (char *t = strtok(buf, ","); t && n < 5; t = strtok(nullptr, ",")) v[n++] = atof(t);
  if (n != 2 && n != 4 && n != 5) return;
  if (fabs(v[0]) > 90 || fabs(v[1]) > 180 || (v[0] == 0 && v[1] == 0)) return;
  dogLat = v[0];
  dogLon = v[1];
  dogValid = true;
  dogNoFix = false;
  lastPosMs = millis();
  if (n >= 4) { dogSats = (int)v[2]; dogHdop = (float)v[3]; }
  if (n == 5) { int bv = (int)v[4]; dogCharging = bv >= 200; dogBatt = dogCharging ? bv - 200 : bv; }
  lastRxMs = millis();
  saveDirty = true;
}

float readVbatOnce(int ctrlLevel) {
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, ctrlLevel);
  delay(5);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(VBAT_ADC);
  pinMode(VBAT_CTRL, INPUT);
  return (sum / 16.0f) * 4.9f / 1000.0f;
}

// Different board revisions enable the divider with different levels, so use the plausible reading
float readVbat() {
  float a = readVbatOnce(LOW), b = readVbatOnce(HIGH);
  bool aOk = a > 2.8f && a < 4.5f, bOk = b > 2.8f && b < 4.5f;
  if (aOk && bOk) return max(a, b);
  return aOk ? a : b;
}

int voltToPct(float v) {
  static const float tv[] = {3.30f, 3.50f, 3.65f, 3.73f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
  static const int tp[]   = {0, 5, 20, 40, 55, 70, 82, 93, 100};
  if (v <= tv[0]) return 0;
  if (v >= tv[8]) return 100;
  for (int i = 1; i < 9; i++)
    if (v < tv[i]) return tp[i - 1] + (int)((v - tv[i - 1]) / (tv[i] - tv[i - 1]) * (tp[i] - tp[i - 1]));
  return 100;
}

void updateOwnBatt() {
  float v = readVbat();
  if (v < 2.8f || v > 4.5f) { ownBatt = -1; return; }
  // USB plugged in raises the measured voltage in a step, unplugging drops it
  if (prevVolt > 0) {
    if (v - prevVolt > CHG_STEP_V) { ownCharging = true; ownVolt = v; }
    else if (prevVolt - v > CHG_STEP_V) { ownCharging = false; ownVolt = v; }
  }
  prevVolt = v;
  ownVolt = ownVolt == 0 ? v : ownVolt * 0.8f + v * 0.2f;
  ownBatt = voltToPct(ownVolt);
}

void initRadio() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  const float tcxo[] = {1.8, 1.6, 0.0};
  int state = -1;
  for (float v : tcxo) {
    state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC, 14, 8, v, false);
    if (state == RADIOLIB_ERR_NONE) break;
  }
  Serial.printf("LoRa init: %d\n", state);
  curSf = LORA_SF;
  if (state != RADIOLIB_ERR_NONE) return;
  radio.setDio1Action(onRx);
  radio.startReceive();
  loraOk = true;
}

// 5-segment battery bar: red = low (<=20 %), green = normal, cyan = charging, grey outline = unknown
void drawBattBar(int x, int y, const char *label, int pct, bool charging) {
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(x, y);
  canvas.print(label);
  int segs = pct < 0 ? 0 : (pct + 19) / 20;
  uint16_t col = pct < 0 ? 0x4208 : charging ? 0x07FF : pct <= 20 ? ST77XX_RED : ST77XX_GREEN;
  for (int i = 0; i < 5; i++) {
    int sx = x + 15 + i * 6;
    if (i < segs) canvas.fillRect(sx, y, 5, 7, col);
    else canvas.drawRect(sx, y, 5, 7, 0x4208);
  }
}

void drawScreen() {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextSize(1);

  canvas.setTextColor(gps.location.isValid() ? ST77XX_GREEN : ST77XX_RED);
  canvas.setCursor(3, 3);
  canvas.printf("SAT:%d", gps.satellites.isValid() ? (int)gps.satellites.value() : 0);
  canvas.setCursor(46, 3);
  canvas.setTextColor(ST77XX_GREEN);
  if (gps.hdop.isValid()) canvas.printf("H:%.1f", gps.hdop.hdop());
  else canvas.print("H:--");
  // Signal bars (0-4) from last packet RSSI; empty when data is stale
  bool fresh = lastRxMs != 0 && millis() - lastRxMs < STALE_MS;
  int bars = 0;
  if (fresh) bars = lastRssi > -90 ? 4 : lastRssi > -102 ? 3 : lastRssi > -112 ? 2 : lastRssi > -120 ? 1 : 0;
  uint16_t barCol = bars >= 3 ? ST77XX_GREEN : bars == 2 ? ST77XX_YELLOW : ST77XX_RED;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2, x = 100 + i * 7;
    if (i < bars) canvas.fillRect(x, 12 - h, 5, h, barCol);
    else canvas.drawRect(x, 12 - h, 5, h, 0x4208);
  }
  canvas.drawFastHLine(0, 14, 128, ST77XX_WHITE);

  bool posFresh = lastPosMs != 0 && millis() - lastPosMs < STALE_MS;
  const int cx = 64, cy = 64, r = 38;
  canvas.drawCircle(cx, cy, r, 0x03E0);

  bool haveBoth = dogValid && gps.location.isValid();
  double dist = 0, brg = 0;
  if (haveBoth) {
    dist = haversineM(gps.location.lat(), gps.location.lng(), dogLat, dogLon);
    brg = bearingDeg(gps.location.lat(), gps.location.lng(), dogLat, dogLon);
  }

  if (!gps.location.isValid()) {
    canvas.setTextColor(ST77XX_RED);
    canvas.setCursor(22, cy - 8); canvas.print("NO GPS FIX");
  } else if (!dogValid) {
    canvas.setTextColor(ST77XX_YELLOW);
    canvas.setCursor(22, cy - 8); canvas.print("WAITING TX");
  } else if (dist < DEADZONE_M) {
    canvas.setTextColor(ST77XX_RED);
    canvas.setTextSize(2);
    canvas.setCursor(cx - 18, cy - 16); canvas.print("TOO");
    canvas.setCursor(cx - 30, cy + 2);  canvas.print("CLOSE");
    canvas.setTextSize(1);
  } else {
    double rel = radians(fmod(brg - heading + 360.0, 360.0));
    auto pt = [&](double l, double a, int &x, int &y) {
      x = cx + (int)round(l * sin(rel + a));
      y = cy - (int)round(l * cos(rel + a));
    };
    int tx, ty, lx, ly, nx, ny, rx, ry;
    pt(36, 0, tx, ty); pt(17, 2.5, lx, ly); pt(5, PI, nx, ny); pt(17, -2.5, rx, ry);
    uint16_t ac = posFresh ? ST77XX_GREEN : 0x7BEF;   // grey arrow = old position
    canvas.fillTriangle(tx, ty, lx, ly, nx, ny, ac);
    canvas.fillTriangle(tx, ty, nx, ny, rx, ry, ac);
  }

  canvas.setTextColor(ST77XX_YELLOW);
  canvas.setCursor(cx - 3, cy - r - 10);
  canvas.print(headingFromCog ? "^" : "N");

  if (haveBoth) {
    char d[16];
    if (dist >= 1000) snprintf(d, sizeof(d), "%.2fkm", dist / 1000.0);
    else snprintf(d, sizeof(d), "%.0fm", dist);
    canvas.setTextColor(!posFresh ? 0x7BEF : dist < 200 ? ST77XX_GREEN : dist < 1000 ? ST77XX_YELLOW : ST77XX_WHITE);
    canvas.setTextSize(3);
    canvas.setCursor(64 - (int)strlen(d) * 9, 108);
    canvas.print(d);
    canvas.setTextSize(1);
  }

  // Large status banner when data is old, missing or the radio failed
  const char *banner = nullptr;
  uint16_t bannerCol = ST77XX_YELLOW;
  if (!loraOk) { banner = "LORA ERR"; bannerCol = ST77XX_RED; }
  else if (lastRxMs == 0) { banner = dogValid ? "SAVED POS" : "NO SIGNAL"; bannerCol = dogValid ? ST77XX_YELLOW : ST77XX_RED; }
  else if (!fresh) { banner = "NO SIGNAL"; bannerCol = ST77XX_RED; }
  else if (dogNoFix) { banner = "DOG NO GPS"; bannerCol = 0xFD20; }
  if (banner) {
    canvas.fillRect(0, 17, 128, 18, bannerCol);
    canvas.setTextColor(ST77XX_BLACK);
    canvas.setTextSize(2);
    canvas.setCursor(64 - (int)strlen(banner) * 6, 19);
    canvas.print(banner);
    canvas.setTextSize(1);
  }

  canvas.setTextColor(ST77XX_GREEN);
  canvas.setCursor(3, 142);
  canvas.printf("BRG:%03.0f", brg);
  canvas.setCursor(3, 151);
  canvas.print(headingFromCog ? "MODE:WALK" : "MODE:N-UP");

  drawBattBar(80, 133, "RX", ownBatt, ownCharging);
  drawBattBar(80, 142, "TX", dogBatt, dogCharging);
  canvas.setCursor(80, 151);
  if (lastRxMs != 0) {
    uint32_t age = (millis() - lastRxMs) / 1000;
    canvas.setTextColor(age < 15 ? ST77XX_GREEN : age < 60 ? ST77XX_YELLOW : ST77XX_RED);
    if (age < 60) canvas.printf("%lu", (unsigned long)age);
    else canvas.printf("%lum%02lu", (unsigned long)(age / 60), (unsigned long)(age % 60));
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 128, 160);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("RX boot");
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.setSPISpeed(20000000);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("TFT ok");
  delay(500);
  tft.fillScreen(ST77XX_BLACK);

  analogSetPinAttenuation(VBAT_ADC, ADC_11db);
  updateOwnBatt();
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  loadSavedDog();
  initRadio();
}

uint32_t lastDraw = 0, lastBatt = 0;

void loop() {
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 5000) {
    lastDbg = millis();
    Serial.printf("GPS chars=%lu sats=%d fix=%d\n", (unsigned long)gps.charsProcessed(), gps.satellites.isValid() ? (int)gps.satellites.value() : -1, (int)gps.location.isValid());
  }
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (gps.speed.isValid() && gps.course.isValid() && gps.speed.kmph() > WALK_KMH) {
    heading = gps.course.deg();
    headingFromCog = true;
  } else {
    heading = 0;
    headingFromCog = false;
  }

  if (rxFlag) {
    rxFlag = false;
    String s;
    int state = radio.readData(s);
    if (state == RADIOLIB_ERR_NONE) {
      lastRssi = radio.getRSSI();
      lastSnr = radio.getSNR();
      Serial.printf("RX SF%d \"%s\" RSSI %.0f SNR %.1f\n", curSf, s.c_str(), lastRssi, lastSnr);
      parsePacket(s);
    }
    radio.startReceive();
  }

  if (loraOk && (lastRxMs == 0 || millis() - lastRxMs > LOCK_LOST_MS)) {
    uint32_t dwell = curSf == LORA_SF_FAST ? DWELL_FAST_MS : DWELL_FAR_MS;
    if (millis() - sfSwitchMs >= dwell) {
      sfSwitchMs = millis();
      curSf = curSf == LORA_SF_FAST ? LORA_SF_FAR : LORA_SF_FAST;
      radio.setSpreadingFactor(curSf);
      radio.startReceive();
    }
  }

  if (saveDirty && millis() - lastSaveMs >= SAVE_INTERVAL_MS) saveDog();

  if (millis() - lastBatt >= 5000) {
    lastBatt = millis();
    updateOwnBatt();
  }

  if (millis() - lastDraw >= 250) {
    lastDraw = millis();
    drawScreen();
  }
}
