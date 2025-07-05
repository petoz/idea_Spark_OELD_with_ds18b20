// Kompletny kod pre IdeaSpark ESP8266 v2.1
// WiFiManager + MQTT (s menom/heslom) + OLED + LittleFS config + DS18B20 + reset tlacitkom

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
#define SDA_PIN       D6     // GPIO12
#define SCL_PIN       D5     // GPIO14
#define BUTTON_PIN    D3     // GPIO0
#define ONE_WIRE_PIN  D7     // GPIO13 for DS18B20
#define OLED_RESET    -1
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// OneWire and temperature sensor
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// MQTT and WiFi
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Timing and state
unsigned long lastMqtt = 0;
unsigned long buttonDownSince = 0;
bool buttonHeld = false;
int currentScreen = 0;
bool shouldSaveConfig = false;

// Configuration struct
struct Config {
  char mqtt_host[40]    = "192.168.1.100";
  int  mqtt_port        = 1883;
  char mqtt_topic[64]   = "ideaspark/status";
  char device_name[32]  = "ESP8266";
  char mqtt_user[32]    = "";
  char mqtt_pass[32]    = "";
} config;

// Callback for WiFiManager when saving config
void saveConfigCallback() {
  shouldSaveConfig = true;
}

// Load config from LittleFS
void loadConfig() {
  if (!LittleFS.begin()) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  strlcpy(config.mqtt_host,   doc["mqtt_host"]   | "", sizeof(config.mqtt_host));
  config.mqtt_port            = doc["mqtt_port"]   | 1883;
  strlcpy(config.mqtt_topic,  doc["mqtt_topic"]  | "", sizeof(config.mqtt_topic));
  strlcpy(config.device_name, doc["device_name"] | "", sizeof(config.device_name));
  strlcpy(config.mqtt_user,   doc["mqtt_user"]   | "", sizeof(config.mqtt_user));
  strlcpy(config.mqtt_pass,   doc["mqtt_pass"]   | "", sizeof(config.mqtt_pass));
  f.close();
}

// Save config to LittleFS
void saveConfig() {
  StaticJsonDocument<512> doc;
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
      Serial.print("MQTT failed rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize I2C and display early
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Inicializujem...");
    display.display();
    delay(500);
  }

  // Initialize temperature sensor
  sensors.begin();

  // Load existing config
  loadConfig();

  // WiFiManager setup
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter p_mqtt_host("mqtt",      "MQTT host",     config.mqtt_host,    40);
  WiFiManagerParameter p_mqtt_port("port",      "MQTT port",     String(config.mqtt_port).c_str(), 6);
  WiFiManagerParameter p_mqtt_topic("topic",    "MQTT topic",    config.mqtt_topic,   64);
  WiFiManagerParameter p_devname("devname",     "Device name",   config.device_name,  32);
  WiFiManagerParameter p_mqtt_user("mqtt_user", "MQTT user",     config.mqtt_user,    32);
  WiFiManagerParameter p_mqtt_pass("mqtt_pass", "MQTT password", config.mqtt_pass,    32);

  wm.addParameter(&p_mqtt_host);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_topic);
  wm.addParameter(&p_devname);
  wm.addParameter(&p_mqtt_user);
  wm.addParameter(&p_mqtt_pass);

  if (!wm.autoConnect("IdeaSpark-Setup", "setup1234")) {
    ESP.restart();
  }

  // Save new config values
  strlcpy(config.mqtt_host,   p_mqtt_host.getValue(),   sizeof(config.mqtt_host));
  config.mqtt_port            = atoi(p_mqtt_port.getValue());
  strlcpy(config.mqtt_topic,  p_mqtt_topic.getValue(),  sizeof(config.mqtt_topic));
  strlcpy(config.device_name, p_devname.getValue(),     sizeof(config.device_name));
  strlcpy(config.mqtt_user,   p_mqtt_user.getValue(),   sizeof(config.mqtt_user));
  strlcpy(config.mqtt_pass,   p_mqtt_pass.getValue(),   sizeof(config.mqtt_pass));

  if (shouldSaveConfig) saveConfig();

  // Setup MQTT
  mqtt.setServer(config.mqtt_host, config.mqtt_port);

  // Setup NTP time
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  int btn = digitalRead(BUTTON_PIN);
  if (btn == LOW) {
    if (buttonDownSince == 0) {
      buttonDownSince = millis();
    } else if (!buttonHeld && millis() - buttonDownSince > 3000) {
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
    if (buttonDownSince != 0 && !buttonHeld && millis() - buttonDownSince > 100) {
      currentScreen = (currentScreen + 1) % 2;
    }
    buttonDownSince = 0;
    buttonHeld = false;
  }

  // Ensure MQTT
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  // Read temperature
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  // Publish MQTT every 30s
  if (millis() - lastMqtt > 30000) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    String payload = "{";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
    payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    payload += "\"uptime\":" + String(millis() / 1000) + ",";
    payload += "\"time\":\"" + String(t->tm_hour) + ":" + (t->tm_min<10?"0":"") + String(t->tm_min) + ":" + (t->tm_sec<10?"0":"") + String(t->tm_sec) + "\",";
    payload += "\"temperature\":" + String(tempC,1) + "}";
    mqtt.publish(config.mqtt_topic, payload.c_str());
    lastMqtt = millis();
  }

  // Display info
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  display.clearDisplay();
  display.setCursor(0, 0);
  if (currentScreen == 0) {
    display.setTextSize(1);
    display.println(config.device_name);
    display.print("IP: "); display.println(WiFi.localIP());
    display.print("RSSI: "); display.print(WiFi.RSSI()); display.println(" dBm");
    display.print("Temp: "); display.print(tempC,1); display.println(" C");
    display.printf("Cas: %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.printf("%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  }
  display.display();

  delay(100);
}