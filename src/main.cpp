#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cc1101.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <esp_task_wdt.h>
#include <WebServer.h>
#include <secrets.h>
#include "HardwareSerial.h"

using namespace CC1101;

// ------------ USER CONFIG ------------

// Reboot options
unsigned long last_reboot = 0;
const unsigned long REBOOT_INTERVAL = 43200000;  // 12 hours

// MQTT broker
bool mqtt_enabled = false;
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_CLIENT_ID = "esp32_fireplace_1";

// MQTT topics
const char* MQTT_CMND_TOPIC  = "fireplace/cmnd";
const char* MQTT_STATE_TOPIC = "fireplace/state";

// Home Assistant MQTT discovery topic
const char* HA_DISCOVERY_TOPIC =
  "homeassistant/switch/esp32_fireplace_switch/config";

// ------------ RADIO PINS / INSTANCE ------------

// ESP32 VSPI: SCK=18, MISO=19, MOSI=23
static constexpr uint8_t PIN_CS   = 5;
static constexpr uint8_t PIN_CLK  = 18;
static constexpr uint8_t PIN_MISO = 19;
static constexpr uint8_t PIN_MOSI = 23;
static constexpr uint8_t PIN_GDO0 = 21;  // GDO0 pin

// Radio(cs, clk, miso, mosi, gd0, gd2)
Radio radio(PIN_CS, PIN_CLK, PIN_MISO, PIN_MOSI, PIN_GDO0);

// ------------ PAYLOADS ------------

uint8_t on_payload[] = {
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x4A, 0x48, 0x60, 0xCE, 0xC2, 0xE2, 0x00, 0x20, 0x00, 0x00, 0x00, 0x1F, 0x32, 0xB2, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA9, 0x49, 0x0C, 0x19, 0xD8, 0x5C, 0x40, 0x04, 0x00, 0x00, 0x00, 0x03, 0xE6, 0x56, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x29, 0x21, 0x83, 0x3B, 0x0B, 0x88, 0x00, 0x80, 0x00, 0x00, 0x00, 0x7C, 0xCA, 0xCA
};

uint8_t off_payload[] = {
  // TODO: replace with real OFF bytes when you capture them
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x29, 0x21, 0x83, 0x3B, 0x0B, 0x88, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7D, 0x35, 0x8B, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA5, 0x24, 0x30, 0x67, 0x61, 0x71, 0x00, 0x20, 0x00, 0x00, 0x00, 0x0F, 0xA6, 0xB1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x54, 0xA4, 0x86, 0x0C, 0xEC, 0x2E, 0x20, 0x04, 0x00, 0x00, 0x00, 0x01, 0xF4, 0xD6, 0x2E
};

// TODO: Reverse more button options and add them here

bool fireplace_state_on = false;

// -------------------- NETWORK OBJECTS --------------------

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);

// -------------------- HELPERS --------------------

void configure_radio_for_fireplace() {
  Serial.println(F("[RF] Configuring CC1101 for fireplace..."));

  Status s;

  radio.setModulation(MOD_2FSK);

  s = radio.setFrequency(433.913);        // MHz
  Serial.print(F("[RF] setFrequency: ")); Serial.println(s);

  s = radio.setFrequencyDeviation(20.0);  // kHz
  Serial.print(F("[RF] setFreqDev: ")); Serial.println(s);

  s = radio.setDataRate(20.0);            // kBaud (≈ 50 µs/bit)
  Serial.print(F("[RF] setDataRate: ")); Serial.println(s);

  s = radio.setRxBandwidth(58.0);         // kHz
  Serial.print(F("[RF] setRxBW: ")); Serial.println(s);

  // Power (dBm)
  radio.setOutputPower(10);

  // Packet settings – fixed length, no CRC/whitening/etc.
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(on_payload));
  radio.setAddressFilteringMode(ADDR_FILTER_MODE_NONE);
  radio.setPreambleLength(64);            // bits
  radio.setSyncWord(0xA55A);              // arbitrary sync word
  radio.setSyncMode(SYNC_MODE_16_16);

  radio.setCrc(false);
  radio.setDataWhitening(false);
  radio.setManchester(false);
  radio.setFEC(false);

  Serial.println(F("[RF] CC1101 configured."));
}

void send_fireplace_on() {
  Serial.println(F("[RF] Sending ON payload..."));
  Status tx = radio.transmit(on_payload, sizeof(on_payload));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_fireplace_off() {
  if (sizeof(off_payload) == 0) {
    Serial.println(F("[RF] OFF payload is empty – no RF sent."));
    return;
  }
  Serial.println(F("[RF] Sending OFF payload..."));
  Status tx = radio.transmit(off_payload, sizeof(off_payload));
  Serial.print(F("[RF] TX OFF status: ")); Serial.println(tx);
}

// TODO: Implement logic to switch between TX & RX as needed
// As cc1101 is half-duplex we can only do one action at a time
// We _should_ be able to RX, stop, then TX as commands come in (in theory)
// Then just listen and update the state when the actual remote has been used.
void fireplace_listen() {
  // Create buffer
  char buff[32];
  size_t read;

  Serial.println("Recieving...");
  Status status = radio.receive((uint8_t *)buff, sizeof(buff) -1, &read);

  if (status == STATUS_OK) {
    Serial.println("Recieved Message: ");
    Serial.println(buff);
  } else {
    Serial.println("Error receiving data: ");
    Serial.println(buff);
  }
  Serial.println();
}

void publish_state(const char* state) {
  Serial.print(F("[MQTT] Publishing state: "));
  Serial.println(state);
  mqttClient.publish(MQTT_STATE_TOPIC, state, true);  // retained
}

void connect_wifi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  Serial.println("SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (++tries > 60) {
      Serial.println(F("\n[WiFi] Failed to connect, rebooting..."));
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print(F("[WiFi] Connected, IP: "));
  Serial.println(WiFi.localIP());
}

// HA MQTT Discovery payload for a single switch
void publish_ha_discovery() {
  // Home Assistant MQTT Discovery JSON (raw string for sanity)
  const char discovery_payload[] = R"({
    "name": "Fireplace",
    "unique_id": "esp32_fireplace_switch",
    "command_topic": "fireplace/cmnd",
    "state_topic": "fireplace/state",
    "payload_on": "ON",
    "payload_off": "OFF",
    "state_on": "ON",
    "state_off": "OFF",
    "device": {
      "identifiers": ["esp32_fireplace"],
      "name": "ESP32 Fireplace Controller",
      "manufacturer": "Custom",
      "model": "ESP32 + CC1101 FSK"
    }
  })";

  Serial.println(F("[MQTT] Publishing Home Assistant discovery config..."));
  mqttClient.publish(HA_DISCOVERY_TOPIC, discovery_payload, true); // retain the msg
  Serial.println(F("[MQTT] Published discovery event."));
}

void connect_mqtt() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  while (!mqttClient.connected()) {
    Serial.print(F("[MQTT] Connecting to broker... "));
    bool ok;
    if (strlen(MQTT_USER) > 0) {
      ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    } else {
      ok = mqttClient.connect(MQTT_CLIENT_ID);
    }

    if (ok) {
      Serial.println(F("connected."));
      // Subscribe to command topic
      mqttClient.subscribe(MQTT_CMND_TOPIC);
      Serial.print(F("[MQTT] Subscribed to ")); Serial.println(MQTT_CMND_TOPIC);

      // Publish discovery config
      publish_ha_discovery();

      // Publish current state
      publish_state(fireplace_state_on ? "ON" : "OFF");
    } else {
      Serial.print(F("failed, rc="));
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String topicStr(topic);
  String msg;
  msg.reserve(length);

  for (unsigned int i = 0; i < length; i++) {
    msg += static_cast<char>(payload[i]);
  }
  msg.trim();
  msg.toUpperCase();

  Serial.print(F("[MQTT] Message on "));
  Serial.print(topicStr);
  Serial.print(F(": '"));
  Serial.print(msg);
  Serial.println('\'');

  if (topicStr == MQTT_CMND_TOPIC) {
    if (msg == "ON") {
      send_fireplace_on();
      fireplace_state_on = true;
      publish_state("ON");
    } else if (msg == "OFF") {
      send_fireplace_off();
      fireplace_state_on = false;
      publish_state("OFF");
    } else {
      Serial.println(F("[MQTT] Unknown command (expected 'ON' or 'OFF')."));
    }
  }
}

// -------------------- WEB SERVER HANDLERS --------------------

String html_page() {
  String state = fireplace_state_on ? "ON" : "OFF";
  String html = F(
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Fireplace Controller</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:2rem;}"
    "button{font-size:1.2rem;padding:0.7rem 1.5rem;margin:0.5rem;border-radius:0.5rem;border:none;cursor:pointer;}"
    ".on{background:#2ecc71;color:#000;}"
    ".off{background:#e74c3c;color:#000;}"
    ".state{margin-top:1rem;font-size:1.1rem;}"
    "</style>"
    "</head><body>"
    "<h1>Fireplace Controller</h1>"
    "<div>"
    "<button class='on' onclick=\"fetch('/on')\">ON</button>"
    "<button class='off' onclick=\"fetch('/off')\">OFF</button>"
    "</div>"
    "<div class='state'>Current state: <span id='st'></span></div>"
    "<script>"
    "async function updateState(){"
      "let r = await fetch('/state');"
      "let j = await r.json();"
      "document.getElementById('st').innerText = j.state;"
    "}"
    "updateState();"
    "setInterval(updateState, 3000);"
    "</script>"
    "</body></html>"
  );
  return html;
}

void handleRoot() {
  server.send(200, "text/html", html_page());
}

void handleOn() {
  send_fireplace_on();
  fireplace_state_on = true;
  publish_state("ON");
  server.send(200, "application/json", "{\"result\":\"ON\"}");
}

void handleOff() {
  send_fireplace_off();
  fireplace_state_on = false;
  publish_state("OFF");
  server.send(200, "application/json", "{\"result\":\"OFF\"}");
}

void handleState() {
  String json = String("{\"state\":\"") + (fireplace_state_on ? "ON" : "OFF") + "\"}";
  server.send(200, "application/json", json);
}

// -------------------- ARDUINO SETUP / LOOP --------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== ESP32 Fireplace Controller ==="));

  // Start reboot watchdog
  last_reboot = millis();

  connect_wifi();

  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/state", HTTP_GET, handleState);
  server.begin();
  Serial.println(F("[HTTP] Web server started on port 80."));

  if (strlen(MQTT_HOST) > 0) {
    mqtt_enabled = true;
  }

  if (mqtt_enabled) {
    mqttClient.setCallback(mqtt_callback);
    connect_mqtt();
  }
  // Init radio *after* WiFi is stable
  Status st = radio.begin();
  Serial.print(F("[RF] radio.begin() = "));
  Serial.println(st);
  if (st == STATUS_CHIP_NOT_FOUND) {
    Serial.println(F("[RF] ERROR: CC1101 chip not found!"));
    while (true) { delay(1000); }
  }

  configure_radio_for_fireplace();

  fireplace_state_on = false;
  publish_state("OFF");
}

void loop() {
  if (mqtt_enabled && !mqttClient.connected()) {
    connect_mqtt();
  }

  if (mqtt_enabled) {
    mqttClient.loop();
  }

  server.handleClient();

  // Listen for fireplace remote
  // fireplace_listen();

  // Check last reboot
  if (millis() - last_reboot > REBOOT_INTERVAL) {
    Serial.println("[SYS] Rebooting (12-hour scheduled reset)");
    delay(100);
    esp_restart();
  }
}

