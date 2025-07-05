// Kompletny kod pre IdeaSpark ESP8266 v2.1
// WiFiManager + MQTT (s menom/heslom) + OLED + LittleFS config + DS18B20 + reset tlacitkom + extra teplota na velkom casovom displeji

#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

// Pin definitions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define SDA_PIN      D6  // GPIO12
#define SCL_PIN      D5  // GPIO14
#define BUTTON_PIN   D3  // GPIO0
#define ONE_WIRE_PIN D7  // GPIO13 for DS18B20

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// Temperature sensor
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// MQTT client
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Configuration storage
struct Config {
  char mqtt_host[40];
  int  mqtt_port;
  char mqtt_topic[64];
  char device_name[32];
  char mqtt_user[32];
  char mqtt_pass[32];
};
Config config;
bool shouldSaveConfig = false;

// State
int currentScreen = 0;
unsigned long lastMqtt    = 0;
unsigned long buttonStart = 0;
bool buttonHeld = false;

// Callback when WiFiManager saves config
void saveConfigCallback() {
  shouldSaveConfig = true;
}

// Load settings from LittleFS
void loadConfig() {
  if (!LittleFS.begin()) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    strlcpy(config.mqtt_host,   doc["mqtt_host"],   sizeof(config.mqtt_host));
    config.mqtt_port            = doc["mqtt_port"];
    strlcpy(config.mqtt_topic,  doc["mqtt_topic"],  sizeof(config.mqtt_topic));
    strlcpy(config.device_name, doc["device_name"], sizeof(config.device_name));
    strlcpy(config.mqtt_user,   doc["mqtt_user"],   sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass,   doc["mqtt_pass"],   sizeof(config.mqtt_pass));
  }
  f.close();
}

// Save settings to LittleFS
void saveConfig() {
  DynamicJsonDocument doc(512);
  doc["mqtt_host"]   = config.mqtt_host;
  doc["mqtt_port"]   = config.mqtt_port;
  doc["mqtt_topic"]  = config.mqtt_topic;
  doc["device_name"] = config.device_name;
  doc["mqtt_user"]   = config.mqtt_user;
  doc["mqtt_pass"]   = config.mqtt_pass;
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// Ensure MQTT connection
void mqttReconnect() {
  while (!mqtt.connected()) {
    String clientId = String(config.device_name) + "-" + String(ESP.getChipId(), HEX);
    if (mqtt.connect(clientId.c_str(), config.mqtt_user, config.mqtt_pass)) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("MQTT failed rc="); Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  // I2C for OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed");
    for (;;) ;
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi konfiguracia...");
  display.display();
  delay(500);

  // Temperature sensor init
  sensors.begin();

  // Load saved config
  loadConfig();

  // WiFiManager with custom params
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  WiFiManagerParameter pmh("mqtt",      "MQTT host",     config.mqtt_host,    40);
  WiFiManagerParameter pmp("port",      "MQTT port",     String(config.mqtt_port).c_str(), 6);
  WiFiManagerParameter pmt("topic",     "MQTT topic",    config.mqtt_topic,   64);
  WiFiManagerParameter pdn("devname",   "Device name",   config.device_name,  32);
  WiFiManagerParameter pmu("mqtt_user", "MQTT user",     config.mqtt_user,    32);
  WiFiManagerParameter pmpw("mqtt_pass","MQTT password", config.mqtt_pass,    32);
  wm.addParameter(&pmh);
  wm.addParameter(&pmp);
  wm.addParameter(&pmt);
  wm.addParameter(&pdn);
  wm.addParameter(&pmu);
  wm.addParameter(&pmpw);
  if (!wm.autoConnect("IdeaSpark-Setup", "setup1234")) {
    ESP.restart();
  }

  // Copy new settings
  strlcpy(config.mqtt_host,   pmh.getValue(),   sizeof(config.mqtt_host));
  config.mqtt_port            = atoi(pmp.getValue());
  strlcpy(config.mqtt_topic,  pmt.getValue(),   sizeof(config.mqtt_topic));
  strlcpy(config.device_name, pdn.getValue(),   sizeof(config.device_name));
  strlcpy(config.mqtt_user,   pmu.getValue(),   sizeof(config.mqtt_user));
  strlcpy(config.mqtt_pass,   pmpw.getValue(),  sizeof(config.mqtt_pass));
  if (shouldSaveConfig) saveConfig();

  // MQTT setup
  mqtt.setServer(config.mqtt_host, config.mqtt_port);

  // NTP
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  // Button handling
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (buttonStart == 0) buttonStart = millis();
    else if (!buttonHeld && millis() - buttonStart > 3000) {
      buttonHeld = true;
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Reset config...");
      display.display();
      delay(1000);
      WiFiManager wm;
      wm.resetSettings();
      LittleFS.remove("/config.json");
      ESP.restart();
    }
  } else {
    if (buttonStart != 0 && !buttonHeld && millis() - buttonStart > 50) {
      currentScreen = (currentScreen + 1) % 2;
    }
    buttonStart = 0;
    buttonHeld = false;
  }

  // MQTT keepalive
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  // Read temperature
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  bool validTemp = (tempC > -100);

  // Publish every 10s
  if (millis() - lastMqtt > 10000) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timeBuf[9];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    String payload = String("{") +
      "\"ip\":\"" + WiFi.localIP().toString() + "\"," +
      "\"ssid\":\"" + String(WiFi.SSID()) + "\"," +
      "\"rssi\":" + String(WiFi.RSSI()) + "," +
      "\"uptime\":" + String(millis()/1000) + "," +
      "\"time\":\"" + timeBuf + "\"," +
      "\"temperature\":\"" + (validTemp ? String(tempC,1) : String("--.-")) + "\"}";
    mqtt.publish(config.mqtt_topic, payload.c_str());
    lastMqtt = millis();
  }

  // Display update
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  char timeBuf2[9];
  snprintf(timeBuf2, sizeof(timeBuf2), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (currentScreen == 0) {
    display.setTextSize(1);
    display.println(config.device_name);
    display.print("IP: "); display.println(WiFi.localIP());
    display.print("RSSI: "); display.print(WiFi.RSSI()); display.println(" dBm");
    display.print("Temp: "); if (validTemp) display.print(String(tempC,1)); else display.print("--.-"); display.println(" C");
    display.print("Cas: "); display.println(timeBuf2);
  } else {
    display.setTextSize(2);
    display.println(timeBuf2);
    display.setTextSize(3);
    display.setCursor(0, SCREEN_HEIGHT - 24);
    display.print("C:"); if (validTemp) display.print(String(tempC,2)); else display.print("--.-"); display.println(" C");
  }
  display.display();

  delay(100);
}