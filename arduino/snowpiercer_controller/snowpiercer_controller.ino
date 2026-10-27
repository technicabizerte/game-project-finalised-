

#include <Wire.h>
#include <DIYables_LCD_I2C.h>

static const uint8_t PIN_SHOOT  = 4;
static const uint8_t PIN_MELEE  = 5;
static const uint8_t PIN_MENU   = 6;
static const uint8_t PIN_SELECT = 7;
static const uint8_t PIN_BACK   = 8;
static const uint8_t PIN_SW     = 9;
static const uint8_t PIN_LED_R  = 10;
static const uint8_t PIN_LED_Y  = 11;
static const uint8_t PIN_X      = A0;
static const uint8_t PIN_Y      = A1;

static const uint8_t LCD_ADDR = 0x27;

DIYables_LCD_I2C lcd(LCD_ADDR, 16, 2);

static unsigned long g_redOffAt    = 0;
static unsigned long g_yellowOffAt = 0;
static unsigned long g_eventCount  = 0;

static char    g_inBuf[32];
static uint8_t g_inLen = 0;

static void lcdShowEvent(const char *evt) {
  lcd.setCursor(0, 0);
  lcd.print(F("EVT: "));

  char pad[12];
  uint8_t i = 0;
  while (i < 11 && evt[i]) { pad[i] = evt[i]; i++; }
  while (i < 11)           { pad[i] = ' ';    i++; }
  pad[11] = 0;
  lcd.print(pad);

  lcd.setCursor(0, 1);
  lcd.print(F("COUNT: "));
  lcd.print(g_eventCount);
  lcd.print(F("        "));
}

static void handleEvent(const char *evt) {
  unsigned long now = millis();
  g_eventCount++;

  if (!strcmp(evt, "HIT")) {
    digitalWrite(PIN_LED_R, HIGH);
    g_redOffAt = now + 250;
  } else if (!strcmp(evt, "DEAD")) {
    digitalWrite(PIN_LED_R, HIGH);
    g_redOffAt = now + 1500;
  } else if (!strcmp(evt, "SHOOT") || !strcmp(evt, "MELEE")) {
    digitalWrite(PIN_LED_Y, HIGH);
    g_yellowOffAt = now + 120;
  } else if (!strcmp(evt, "WIN")) {
    digitalWrite(PIN_LED_Y, HIGH);
    g_yellowOffAt = now + 1500;
  } else {

  }

  lcdShowEvent(evt);
}

static void pollIncoming() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_inLen == 0) continue;
      g_inBuf[g_inLen] = 0;

      const char *payload = g_inBuf;
      if (!strncmp(g_inBuf, "EVT:", 4)) payload = g_inBuf + 4;

      handleEvent(payload);
      g_inLen = 0;
    } else if (g_inLen < sizeof(g_inBuf) - 1) {
      g_inBuf[g_inLen++] = c;
    } else {
      g_inLen = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_SHOOT,  INPUT_PULLUP);
  pinMode(PIN_MELEE,  INPUT_PULLUP);
  pinMode(PIN_MENU,   INPUT_PULLUP);
  pinMode(PIN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BACK,   INPUT_PULLUP);
  pinMode(PIN_SW,     INPUT_PULLUP);
  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_Y,  OUTPUT);
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_Y, LOW);

  Wire.begin();

  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0); lcd.print(F("SNOWPIERCER"));
  lcd.setCursor(0, 1); lcd.print(F("Ready..."));
}

void loop() {

  pollIncoming();

  unsigned long now = millis();
  if (g_redOffAt    && now >= g_redOffAt)    { digitalWrite(PIN_LED_R, LOW); g_redOffAt    = 0; }
  if (g_yellowOffAt && now >= g_yellowOffAt) { digitalWrite(PIN_LED_Y, LOW); g_yellowOffAt = 0; }

  int shoot = (digitalRead(PIN_SHOOT)  == LOW) ? 1 : 0;
  int melee = (digitalRead(PIN_MELEE)  == LOW) ? 1 : 0;
  int menu  = (digitalRead(PIN_MENU)   == LOW) ? 1 : 0;
  int sel   = (digitalRead(PIN_SELECT) == LOW) ? 1 : 0;
  int back  = (digitalRead(PIN_BACK)   == LOW) ? 1 : 0;
  int sw    = (digitalRead(PIN_SW)     == LOW) ? 1 : 0;
  int xv    = analogRead(PIN_X);
  int yv    = analogRead(PIN_Y);

  Serial.print(F("SHOOT:"));   Serial.print(shoot);
  Serial.print(F(",MELEE:"));  Serial.print(melee);
  Serial.print(F(",MENU:"));   Serial.print(menu);
  Serial.print(F(",SELECT:")); Serial.print(sel);
  Serial.print(F(",BACK:"));   Serial.print(back);
  Serial.print(F(",X:"));      Serial.print(xv);
  Serial.print(F(",Y:"));      Serial.print(yv);
  Serial.print(F(",SW:"));     Serial.println(sw);

  delay(20);
}
