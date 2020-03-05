#include <TFT_eSPI.h>
#include <SPI.h>

#include <NTPClient.h>
#include "WiFi.h"
#include <WiFiUdp.h>

#include "Alert.h" // Out of range alert icon

const char *ssid = "****";
const char *password = "****";

#ifndef TFT_DISPOFF
  #define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
  #define TFT_SLPIN 0x10
#endif

#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 23

#define TFT_BL 4 // Display backlight control pin
#define ADC_EN 14
#define ADC_PIN 34
#define BUTTON_1 35
#define BUTTON_2 0

void drawAlert(int x, int y, boolean draw);
void drawIcon(const unsigned short *icon, int16_t x, int16_t y, int8_t width, int8_t height);

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

WiFiUDP ntpUDP;

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

#define TFT_GREY 0xBDF7

float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0; // Saved H, M, S x & y multipliers
float sdeg = 0, mdeg = 0, hdeg = 0;
uint16_t osx = 64, osy = 64, omx = 64, omy = 64, ohx = 64, ohy = 64; // Saved H, M, S x & y coords
uint16_t x0 = 0, x1 = 0, yy0 = 0, yy1 = 0;
uint32_t targetTime = 0; // for next 1 second timeout
bool isTimeUpdated = false;

static uint8_t conv2d(const char *p)
{
  uint8_t v = 0;
  if ('0' <= *p && *p <= '9')
    v = *p - '0';
  return 10 * v + *++p - '0';
}

uint8_t hh = conv2d(__TIME__), mm = conv2d(__TIME__ + 3), ss = conv2d(__TIME__ + 6); // Get H, M, S from compile time

boolean initial = 1;

void setup(void)
{
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Adding a black background colour erases previous text automatically

  if (TFT_BL > 0)
  {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  // Draw clock face
  tft.fillCircle(64, 64, 61, TFT_BLUE);
  tft.fillCircle(64, 64, 57, TFT_BLACK);

  // Draw 12 lines
  for (int i = 0; i < 360; i += 30)
  {
    sx = cos((i - 90) * 0.0174532925);
    sy = sin((i - 90) * 0.0174532925);
    x0 = sx * 57 + 64;
    yy0 = sy * 57 + 64;
    x1 = sx * 50 + 64;
    yy1 = sy * 50 + 64;

    tft.drawLine(x0, yy0, x1, yy1, TFT_BLUE);
  }

  // Draw 60 dots
  for (int i = 0; i < 360; i += 6)
  {
    sx = cos((i - 90) * 0.0174532925);
    sy = sin((i - 90) * 0.0174532925);
    x0 = sx * 53 + 64;
    yy0 = sy * 53 + 64;

    tft.drawPixel(x0, yy0, TFT_BLUE);
    if (i == 0 || i == 180)
      tft.fillCircle(x0, yy0, 1, TFT_CYAN);
    if (i == 0 || i == 180)
      tft.fillCircle(x0 + 1, yy0, 1, TFT_CYAN);
    if (i == 90 || i == 270)
      tft.fillCircle(x0, yy0, 1, TFT_CYAN);
    if (i == 90 || i == 270)
      tft.fillCircle(x0 + 1, yy0, 1, TFT_CYAN);
  }

  tft.fillCircle(65, 65, 3, TFT_RED);

  // Draw text at position 64,125 using fonts 4
  // Only font numbers 2,4,6,7 are valid. Font 6 only contains characters [space] 0 1 2 3 4 5 6 7 8 9 : . a p m
  // Font 7 is a 7 segment font and only contains characters [space] 0 1 2 3 4 5 6 7 8 9 : .
  //tft.drawCentreString("Connecting ...", 64, 145, 2);

  drawAlert(64, 177, true);

  delay(5000);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  drawAlert(64, 177, false);

  tft.fillRect(0, 140, 135, 100, TFT_BLACK);
  tft.drawRect(0, 140, 135, 100, TFT_WHITE);
  tft.drawCentreString("Connected to", 64, 145, 2);
  tft.drawCentreString(ssid, 64, 185, 2);

  timeClient.setTimeOffset(7200);
  timeClient.begin();
  timeClient.update();
  ss = timeClient.getSeconds() + 2;
  mm = timeClient.getMinutes();
  hh = timeClient.getHours();

  targetTime = millis() + 1000;
}

void loop()
{
  if (targetTime < millis())
  {
    if (isTimeUpdated && (hh == 2))
    {
      isTimeUpdated = false;
    }

    if (!isTimeUpdated && (hh == 3))
    {
      timeClient.update();
      ss = timeClient.getSeconds() + 2;
      mm = timeClient.getMinutes();
      hh = timeClient.getHours();
    }

    targetTime = millis() + 1000;
    ss++; // Advance second
    if (ss > 59)
    {
      ss = 0;
      mm++; // Advance minute
      if (mm > 59)
      {
        mm = 0;
        hh++; // Advance hour
        if (hh > 23)
        {
          hh = 0;
        }
      }
    }

    // Pre-compute hand degrees, x & y coords for a fast screen update
    sdeg = ss * 6;                     // 0-59 -> 0-354
    mdeg = mm * 6 + sdeg * 0.01666667; // 0-59 -> 0-360 - includes seconds
    hdeg = hh * 30 + mdeg * 0.0833333; // 0-11 -> 0-360 - includes minutes and seconds
    hx = cos((hdeg - 90) * 0.0174532925);
    hy = sin((hdeg - 90) * 0.0174532925);
    mx = cos((mdeg - 90) * 0.0174532925);
    my = sin((mdeg - 90) * 0.0174532925);
    sx = cos((sdeg - 90) * 0.0174532925);
    sy = sin((sdeg - 90) * 0.0174532925);

    if (ss == 0 || initial)
    {
      initial = 0;
      // Erase hour and minute hand positions every minute
      tft.drawLine(ohx, ohy, 65, 65, TFT_BLACK);
      ohx = hx * 33 + 65;
      ohy = hy * 33 + 65;
      tft.drawLine(omx, omy, 65, 65, TFT_BLACK);
      omx = mx * 44 + 65;
      omy = my * 44 + 65;
    }

    // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
    tft.drawLine(osx, osy, 65, 65, TFT_BLACK);
    tft.drawLine(ohx, ohy, 65, 65, TFT_WHITE);
    tft.drawLine(omx, omy, 65, 65, TFT_WHITE);
    osx = sx * 47 + 65;
    osy = sy * 47 + 65;
    tft.drawLine(osx, osy, 65, 65, TFT_RED);

    tft.fillCircle(65, 65, 3, TFT_RED);
  }
}

void drawAlert(int x, int y, boolean draw)
{
  if (draw)
  {
    drawIcon(alert, x - alertWidth / 2, y - alertHeight / 2, alertWidth, alertHeight);
  }
  else if (!draw)
  {
    tft.fillRect(x - alertWidth / 2, y - alertHeight / 2, alertWidth, alertHeight, TFT_BLACK);
  }
}

//====================================================================================
// This is the function to draw the icon stored as an array in program memory (FLASH)
//====================================================================================

// To speed up rendering we use a 64 pixel buffer
#define BUFF_SIZE 64

// Draw array "icon" of defined width and height at coordinate x,y
// Maximum icon size is 255x255 pixels to avoid integer overflow

void drawIcon(const unsigned short *icon, int16_t x, int16_t y, int8_t width, int8_t height)
{

  uint16_t pix_buffer[BUFF_SIZE]; // Pixel buffer (16 bits per pixel)

  tft.startWrite();

  // Set up a window the right size to stream pixels into
  tft.setAddrWindow(x, y, width, height);

  // Work out the number whole buffers to send
  uint16_t nb = ((uint16_t)height * width) / BUFF_SIZE;

  // Fill and send "nb" buffers to TFT
  for (int i = 0; i < nb; i++)
  {
    for (int j = 0; j < BUFF_SIZE; j++)
    {
      pix_buffer[j] = pgm_read_word(&icon[i * BUFF_SIZE + j]);
    }
    tft.pushColors(pix_buffer, BUFF_SIZE);
  }

  // Work out number of pixels not yet sent
  uint16_t np = ((uint16_t)height * width) % BUFF_SIZE;

  // Send any partial buffer left over
  if (np)
  {
    for (int i = 0; i < np; i++)
      pix_buffer[i] = pgm_read_word(&icon[nb * BUFF_SIZE + i]);
    tft.pushColors(pix_buffer, np);
  }

  tft.endWrite();
}
