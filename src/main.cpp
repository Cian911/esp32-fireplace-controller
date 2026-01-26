#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cc1101.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <esp_task_wdt.h>
#include <WebServer.h>
#include <Preferences.h>
#include <secrets.h>
#include <mqtt.h>
#include <profiles.h>
#include "HardwareSerial.h"
#if defined(REMOTE_PROFILE_IRANGE)
  #include "irange_payloads.h"
#elif defined(REMOTE_PROFILE_NON_IRANGE)
  #include "non_irange_payloads.h"
#else
  #error "Select a payload profile"
#endif

using namespace CC1101;

// ------------ USER CONFIG ------------

// Reboot options
unsigned long last_reboot = 0;
const unsigned long REBOOT_INTERVAL = 43200000;  // 12 hours

// MQTT broker
bool mqtt_enabled = false;
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_CLIENT_ID = "esp32_fireplace_1";
const unsigned int MQTT_MAX_RETRIES = 5;

// ------------ RADIO PINS / INSTANCE ------------

// ESP32 VSPI: SCK=18, MISO=19, MOSI=23
static constexpr uint8_t PIN_CS   = 5;
static constexpr uint8_t PIN_CLK  = 18;
static constexpr uint8_t PIN_MISO = 19;
static constexpr uint8_t PIN_MOSI = 23;
static constexpr uint8_t PIN_GDO0 = 21;  // GDO0 pin

// Radio(cs, clk, miso, mosi, gd0, gd2)
Radio radio(PIN_CS, PIN_CLK, PIN_MISO, PIN_MOSI, PIN_GDO0);

bool fireplace_state_on = false;

// -------------------- NETWORK OBJECTS --------------------

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);
Preferences prefs;

static constexpr char PREF_NAMESPACE[] = "fireplace";
static constexpr char PREF_KEY_STATE[] = "state";

// -------------------- HELPERS --------------------

static inline bool hasFeature(uint32_t feat) {
  return (ACTIVE_PROFILE.features & feat) != 0;
}

void configure_radio_for_fireplace(const RadioConfig& cfg) {
  Serial.println(F("[RF] Configuring CC1101 for fireplace..."));

  Status s;

  radio.setModulation(cfg.modulation);

  s = radio.setFrequency(cfg.freq_mhz);        // MHz
  Serial.print(F("[RF] setFrequency: ")); Serial.println(s);

  s = radio.setFrequencyDeviation(20.0);  // kHz
  Serial.print(F("[RF] setFreqDev: ")); Serial.println(s);

  s = radio.setDataRate(cfg.datarate_kbaud);            // kBaud (≈ 50 µs/bit)
  Serial.print(F("[RF] setDataRate: ")); Serial.println(s);

  s = radio.setRxBandwidth(cfg.rx_bw_khz);         // kHz
  Serial.print(F("[RF] setRxBW: ")); Serial.println(s);

  // Power (dBm)
  radio.setOutputPower(10);

  // Packet settings – fixed length, no CRC/whitening/etc.
  // radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(on_payload));
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

void send_on_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(on_payload));
  Serial.println(F("[RF] Sending ON payload..."));
  Status tx = radio.transmit(on_payload, sizeof(on_payload));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_off_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(off_payload));
  if (sizeof(off_payload) == 0) {
    Serial.println(F("[RF] OFF payload is empty – no RF sent."));
    return;
  }
  Serial.println(F("[RF] Sending OFF payload..."));
  Status tx = radio.transmit(off_payload, sizeof(off_payload));
  Serial.print(F("[RF] TX OFF status: ")); Serial.println(tx);
}

void send_flame_effect_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(flame_effect_btn));
  Serial.println(F("[RF] Sending FLAME_EFT payload..."));
  Status tx = radio.transmit(flame_effect_btn, sizeof(flame_effect_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_sound_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(sound_btn));
  Serial.println(F("[RF] Sending SND_BTN payload..."));
  Status tx = radio.transmit(sound_btn, sizeof(sound_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_left_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(left_btn));
  Serial.println(F("[RF] Sending LFT_BTN payload..."));
  Status tx = radio.transmit(left_btn, sizeof(left_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_right_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(right_btn));
  Serial.println(F("[RF] Sending RGT_BTN payload..."));
  Status tx = radio.transmit(right_btn, sizeof(right_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_plus_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(plus_btn));
  Serial.println(F("[RF] Sending PLUS_BTN payload..."));
  Status tx = radio.transmit(plus_btn, sizeof(plus_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void send_minus_btn_payload() {
  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(minus_btn));
  Serial.println(F("[RF] Sending MINUS_BTN payload..."));
  Status tx = radio.transmit(minus_btn, sizeof(minus_btn));
  Serial.print(F("[RF] TX ON status: ")); Serial.println(tx);
}

void publish_state(const char* state) {
  Serial.print(F("[MQTT] Publishing state: "));
  Serial.println(state);
  mqttClient.publish(MQTT_STATE_TOPIC, state, true);  // retained
}

void persist_and_publish_state(bool on) {
  fireplace_state_on = on;
  prefs.putBool(PREF_KEY_STATE, fireplace_state_on);
  publish_state(fireplace_state_on ? "ON" : "OFF");
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
  Serial.println(F("[MQTT] Publishing Home Assistant discovery config..."));
  mqttClient.publish(HA_DISCOVERY_TOPIC, discovery_payload, true);
  mqttClient.publish(HA_DISCOVERY_LEFT_TOPIC , left_payload_discovery, true);
  mqttClient.publish(HA_DISCOVERY_RIGHT_TOPIC , right_payload_discovery, true);
  mqttClient.publish(HA_DISCOVERY_FLAME_EFFECT_TOPIC , flame_effect_payload_discovery, true);
  mqttClient.publish(HA_DISCOVERY_PLUS_TOPIC , plus_payload_discovery, true);
  mqttClient.publish(HA_DISCOVERY_MINUS_TOPIC , minus_payload_discovery, true);

  if (hasFeature(FEAT_SOUND)) {
    mqttClient.publish(HA_DISCOVERY_MINUS_TOPIC , sound_payload_discovery, true);
  }

  Serial.println(F("[MQTT] Published discovery event."));
}

void connect_mqtt() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  int tries = 0;
  while (!mqttClient.connected() && tries < MQTT_MAX_RETRIES) {
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
      tries++;
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
      send_on_btn_payload();
      persist_and_publish_state(true);
    } else if (msg == "OFF") {
      send_off_btn_payload();
      persist_and_publish_state(false);
    } else if (msg == "FLAME") {
      send_flame_effect_btn_payload();
    } else if (msg == "SOUND") {
      send_sound_btn_payload();
    } else if (msg == "RIGHT") {
      send_right_btn_payload();
    } else if (msg == "LEFT") {
      send_left_btn_payload();
    } else if (msg == "PLUS") {
      send_plus_btn_payload();
    } else if (msg == "MINUS") {
      send_minus_btn_payload();
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
  );

  // SOUND / FLAME (only include if supported in profilez)
  html += F("<div>");
  if (hasFeature(FEAT_SOUND)) {
    html += F("<button class='sound' onclick=\"fetch('/sound')\">SOUND</button>");
  }
  if (hasFeature(FEAT_FLAME)) {
    html += F("<button class='flame' onclick=\"fetch('/flame')\">FLAME</button>");
  }
  html += F("</div>");

  html += F("<div>");
  if (hasFeature(FEAT_LEFT))  html += F("<button class='left' onclick=\"fetch('/left')\">LEFT</button>");
  if (hasFeature(FEAT_RIGHT)) html += F("<button class='right' onclick=\"fetch('/right')\">RIGHT</button>");
  html += F("</div>");

  html += F("<div>");
  if (hasFeature(FEAT_PLUS))  html += F("<button class='plus' onclick=\"fetch('/plus')\">PLUS</button>");
  if (hasFeature(FEAT_MINUS)) html += F("<button class='minus' onclick=\"fetch('/minus')\">MINUS</button>");
  html += F("</div>");

  html += F(
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
  send_on_btn_payload();
  persist_and_publish_state(true);
  server.send(200, "application/json", "{\"result\":\"ON\"}");
}

void handleOff() {
  send_off_btn_payload();
  persist_and_publish_state(false);
  server.send(200, "application/json", "{\"result\":\"OFF\"}");
}

void handleFlame() {
  send_flame_effect_btn_payload();
  server.send(200, "application/json", "{\"result\":\"FLAME\"}");
}

void handleSound() {
  send_sound_btn_payload();
  server.send(200, "application/json", "{\"result\":\"SOUND\"}");
}

void handleLeft() {
  send_left_btn_payload();
  server.send(200, "application/json", "{\"result\":\"LEFT\"}");
}

void handleRight() {
  send_right_btn_payload();
  server.send(200, "application/json", "{\"result\":\"RIGHT\"}");
}

void handlePlus() {
  send_plus_btn_payload();
  server.send(200, "application/json", "{\"result\":\"PLUS\"}");
}

void handleMinus() {
  send_minus_btn_payload();
  server.send(200, "application/json", "{\"result\":\"MINUS\"}");
}

void handleState() {
  String json = String("{\"state\":\"") + (fireplace_state_on ? "ON" : "OFF") + "\"}";
  server.send(200, "application/json", json);
}

bool decode_irange_remote(const uint8_t* data, size_t len, char* out, size_t out_len) {
  (void)data; (void)len;
  if (out && out_len) out[0] = '\0';
  return false;
}

bool decode_non_irange_remote(const uint8_t* data, size_t len, char* out, size_t out_len) {
  (void)data; (void)len;
  if (out && out_len) out[0] = '\0';
  return false;
}

// -------------------- ARDUINO SETUP / LOOP --------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== ESP32 Fireplace Controller ==="));

  // Start reboot watchdog
  last_reboot = millis();

  prefs.begin(PREF_NAMESPACE, false);
  fireplace_state_on = prefs.getBool(PREF_KEY_STATE, false);
  Serial.print(F("[STATE] Restored persisted state: "));
  Serial.println(fireplace_state_on ? F("ON") : F("OFF"));

  connect_wifi();

  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/flame", HTTP_GET, handleFlame);
  server.on("/flame", HTTP_GET, handleFlame);
  server.on("/sound", HTTP_GET, handleSound);
  server.on("/left", HTTP_GET, handleLeft);
  server.on("/right", HTTP_GET, handleRight);
  server.on("/plus", HTTP_GET, handlePlus);
  server.on("/minus", HTTP_GET, handleMinus);
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

  configure_radio_for_fireplace(ACTIVE_PROFILE.radio);
  Serial.println("Using Profile: "); Serial.print(ACTIVE_PROFILE.name);

  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connect_wifi();
  }

  if (mqtt_enabled && !mqttClient.connected()) {
    connect_mqtt();
  }

  if (mqtt_enabled && mqttClient.connected()) {
    mqttClient.loop();
  }

  server.handleClient();

  // Check last reboot
  if (millis() - last_reboot > REBOOT_INTERVAL) {
    Serial.println("[SYS] Rebooting (12-hour scheduled reset)");
    delay(100);
    esp_restart();
  }
}

