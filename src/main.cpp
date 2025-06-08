#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SDA_PIN       D6
#define SCL_PIN       D5
#define BUTTON_PIN    D3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int currentScreen = 0;
unsigned long lastButtonPress = 0;
unsigned long buttonDownSince = 0;
bool buttonHeld = false;


void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("WiFi konfiguracia...");
  display.display();

  // 🧠 Vytvor WiFiManager a spusti konfiguraciu
  WiFiManager wm;
  if (!wm.autoConnect("IdeaSpark-Setup", "setup1234")) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Zlyhanie WiFi");
    display.display();
    delay(3000);
    ESP.restart();
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi OK");
  display.println(WiFi.localIP());
  display.display();

  configTime(3600, 0, "pool.ntp.org", "time.nist.gov"); // CET
}

void loop() {
  int btn = digitalRead(BUTTON_PIN);

  if (btn == LOW) {
    if (buttonDownSince == 0) {
      buttonDownSince = millis();  // začiatok držania
    } else if (!buttonHeld && millis() - buttonDownSince > 3000) {
      buttonHeld = true;

      // === Reset WiFi ===
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.println("Resetujem WiFi...");
      display.display();
      delay(1000);

      WiFiManager wm;
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    if (buttonDownSince != 0 && !buttonHeld && millis() - buttonDownSince > 100) {
      // Krátky stisk = prepni obrazovku
      currentScreen = (currentScreen + 1) % 2;
    }
    buttonDownSince = 0;
    buttonHeld = false;
  }

  // === Zobrazenie na OLED ===
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  display.clearDisplay();
  display.setCursor(0, 0);

  if (currentScreen == 0) {
    display.setTextSize(1);
    display.println("Info:");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.print("RSSI: ");
    display.print(WiFi.RSSI());
    display.println(" dBm");
    display.print("Cas: ");
    display.printf("%02d:%02d:%02d\n",
      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.printf("%02d:%02d:%02d",
      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }

  display.display();
  delay(100);
}