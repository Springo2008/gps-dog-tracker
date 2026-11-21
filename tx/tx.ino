// HUND (Tx): GPS + LoRa transmitter. No display, no compass.
// Heltec Wireless Stick Lite V3 (ESP32-S3 + SX1262), board: Heltec Wireless Stick Lite(V3)
// Libraries: RadioLib, TinyGPSPlus
//
// Packets (ASCII, 869.525 MHz), sent as fast as the 10 % duty cycle allows (~6 s at SF10 when moving, ~17-23 s at SF12 when still):
//   "lat,lon"                       normal (GPS fix)
//   "lat,lon,sats,hdop"             every 5th packet
//   "lat,lon,sats,hdop,batt"        every 10th packet (batt = percent, +200 while charging)
//   "!,sats,batt"                   heartbeat when there is NO GPS fix (Rx shows "DOG NO GPS")

#include <Arduino.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>

// GPS
#define GPS_RX 18   // GPS TXD -> GPIO18
#define GPS_TX 17   // GPS RXD -> GPIO17

// Battery voltage: GPIO1 reads the on-board divider (390k/100k), enabled via GPIO37
#define VBAT_ADC  1
#define VBAT_CTRL 37
#define CHG_STEP_V 0.04f   // voltage step that indicates USB plugged/unplugged

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
#define LORA_SF_FAST 10   // dog moving: faster updates
#define LORA_SF_FAR  12   // dog still: max range
#define MOVE_KMH      2.0
#define STILL_HOLD_MS 30000UL   // stay in fast mode this long after last movement
#define LORA_CR   5
#define LORA_SYNC 0x12
#define LORA_PWR  22   // dBm; 869.525 MHz allows 500 mW ERP, 10% duty cycle

// Duty cycle: wait airtime x 10 (+15% margin) after each packet to stay under 10 %
#define DUTY_WAIT_FACTOR 11.5
#define MIN_INTERVAL_MS  6000UL
#define LED_PIN 35     // on-board LED (flashes on each transmit)

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

bool loraOk = false;
uint32_t packetCount = 0;
uint32_t nextSend = 0;
uint32_t lastMoveMs = 0;
uint8_t curSf = LORA_SF_FAR;
float battVolt = 0;
float prevVolt = 0;
bool charging = false;
int lastBattPct = -1;
uint32_t lastBattMs = 0;

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

int readBattPct() {
  float v = readVbat();
  if (v < 2.8f || v > 4.5f) return -1;
  // USB plugged in raises the measured voltage in a step, unplugging drops it
  if (prevVolt > 0) {
    if (v - prevVolt > CHG_STEP_V) { charging = true; battVolt = v; }
    else if (prevVolt - v > CHG_STEP_V) { charging = false; battVolt = v; }
  }
  prevVolt = v;
  battVolt = battVolt == 0 ? v : battVolt * 0.8f + v * 0.2f;
  return voltToPct(battVolt);
}

// Value sent over LoRa: percent, +200 while charging
int battField() {
  int pct = lastBattPct < 0 ? 0 : lastBattPct;
  return charging ? pct + 200 : pct;
}

void initRadio() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  const float tcxo[] = {1.8, 1.6, 0.0};
  int state = -1;
  for (float v : tcxo) {
    state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF_FAR, LORA_CR, LORA_SYNC, LORA_PWR, 8, v, false);
    if (state == RADIOLIB_ERR_NONE) break;
  }
  Serial.printf("LoRa init: %d\n", state);
  curSf = LORA_SF_FAR;
  loraOk = (state == RADIOLIB_ERR_NONE);
}

void sendPacket(bool hasFix) {
  uint8_t wantSf = (lastMoveMs != 0 && millis() - lastMoveMs < STILL_HOLD_MS) ? LORA_SF_FAST : LORA_SF_FAR;
  if (wantSf != curSf && radio.setSpreadingFactor(wantSf) == RADIOLIB_ERR_NONE) curSf = wantSf;

  char buf[96];
  int n;
  packetCount++;
  if (!hasFix) {
    n = snprintf(buf, sizeof(buf), "!,%d,%d",
                 gps.satellites.isValid() ? (int)gps.satellites.value() : 0, battField());
  } else {
    n = snprintf(buf, sizeof(buf), "%.5f,%.5f", gps.location.lat(), gps.location.lng());
    bool withBatt = (packetCount % 10 == 0);
    bool withInfo = withBatt || (packetCount % 5 == 0);
    if (withInfo) {
      n += snprintf(buf + n, sizeof(buf) - n, ",%d,%.1f",
                    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
                    gps.hdop.isValid() ? gps.hdop.hdop() : 99.9);
    }
    if (withBatt) n += snprintf(buf + n, sizeof(buf) - n, ",%d", battField());
  }

  digitalWrite(LED_PIN, HIGH);
  uint32_t airUs = radio.getTimeOnAir(strlen(buf));
  int state = radio.transmit(buf);
  digitalWrite(LED_PIN, LOW);
  if (state != RADIOLIB_ERR_NONE) loraOk = false;   // re-init on next cycle
  uint32_t wait = (uint32_t)(airUs / 1000.0 * DUTY_WAIT_FACTOR);
  if (wait < MIN_INTERVAL_MS) wait = MIN_INTERVAL_MS;
  nextSend = millis() + wait;
  Serial.printf("TX SF%d \"%s\" -> %d, airtime %lu ms, next in %lu s\n", curSf, buf, state,
                (unsigned long)(airUs / 1000), (unsigned long)(wait / 1000));
}

void setup() {
  Serial.begin(115200);
  Serial.println("TX boot");
  pinMode(LED_PIN, OUTPUT);
  analogSetPinAttenuation(VBAT_ADC, ADC_11db);
  gpsSerial.setRxBufferSize(1024);   // LoRa transmit blocks for up to ~2 s; avoid losing NMEA data
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  lastBattPct = readBattPct();
  initRadio();
}

void loop() {
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 5000) {
    lastDbg = millis();
    Serial.printf("GPS chars=%lu sats=%d fix=%d\n", (unsigned long)gps.charsProcessed(), gps.satellites.isValid() ? (int)gps.satellites.value() : -1, (int)gps.location.isValid());
  }
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (millis() - lastBattMs >= 2000) {
    lastBattMs = millis();
    lastBattPct = readBattPct();
  }
  // Only trust speed with a decent fix, so GPS jitter while still doesn't trigger fast mode
  if (gps.speed.isValid() && gps.hdop.isValid() && gps.hdop.hdop() < 3.0 && gps.speed.kmph() > MOVE_KMH) lastMoveMs = millis();

  if ((int32_t)(millis() - nextSend) >= 0) {
    nextSend = millis() + MIN_INTERVAL_MS;
    if (!loraOk) {
      Serial.println("LoRa not ready, retrying init");
      initRadio();
    } else {
      sendPacket(gps.location.isValid() && gps.location.age() < 5000);
    }
  }
}
