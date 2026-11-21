#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define TFT_SCLK 47
#define TFT_MOSI 35
#define TFT_CS   36
#define TFT_DC   48
#define TFT_RST  38
#define TFT_BLK  26

Adafruit_ST7735 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);        // backlight on (if dark, try LOW)

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);          // if offset/wrong colours: INITR_GREENTAB or INITR_REDTAB
  tft.setRotation(0);                 // 128x160 portrait
  tft.setSPISpeed(20000000);
}

void loop() {
  uint16_t cols[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE, ST77XX_BLACK};
  for (uint16_t c : cols) { tft.fillScreen(c); delay(600); }

  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0, 0, 128, 160, ST77XX_WHITE);   // border must touch all 4 edges
  tft.drawLine(0, 0, 127, 159, ST77XX_YELLOW);
  tft.drawLine(127, 0, 0, 159, ST77XX_YELLOW);
  tft.drawCircle(64, 80, 30, ST77XX_GREEN);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(14, 10);
  tft.print("HAND RX");
  tft.setTextSize(1);
  tft.setCursor(14, 140);
  tft.print("128x160 ST7735S");
  delay(4000);
}
