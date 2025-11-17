#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cc1101.h>

using namespace CC1101;

// Wi-Fi
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

// MQTT broker
const char* MQTT_HOST      = "";  // your HA/Mosquitto IP or hostname
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_USER      = "rfid";     // or nullptr if no auth
const char* MQTT_PASSWORD  = "";     // or nullptr if no auth
const char* MQTT_CLIENT_ID = "esp32_fireplace_1";

// MQTT topics
const char* MQTT_CMND_TOPIC  = "home/fireplace/cmnd";
const char* MQTT_STATE_TOPIC = "home/fireplace/state";

// Home Assistant MQTT discovery topic
const char* HA_DISCOVERY_TOPIC =
  "homeassistant/switch/esp32_fireplace_switch/config";

// ------------ RADIO PINS / INSTANCE ------------

// ESP32 VSPI: SCK=18, MISO=19, MOSI=23
static constexpr uint8_t PIN_CS   = 5;
static constexpr uint8_t PIN_CLK  = 18;
static constexpr uint8_t PIN_MISO = 19;
static constexpr uint8_t PIN_MOSI = 23;
static constexpr uint8_t PIN_GDO0 = 21;  // your GDO0 pin

// Radio(cs, clk, miso, mosi, gd0, gd2)
Radio radio(PIN_CS, PIN_CLK, PIN_MISO, PIN_MOSI, PIN_GDO0);

// ------------ PAYLOADS ------------

// Your working ON payload (unchanged)
uint8_t on_payload[] = {
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x4A, 0x48, 0x60, 0xCE, 0xC2, 0xE2, 0x00, 0x20, 0x00, 0x00, 0x00, 0x1F, 0x32, 0xB2, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA9, 0x49, 0x0C, 0x19, 0xD8, 0x5C, 0x40, 0x04, 0x00, 0x00, 0x00, 0x03, 0xE6, 0x56, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x29, 0x21, 0x83, 0x3B, 0x0B, 0x88, 0x00, 0x80, 0x00, 0x00, 0x00, 0x7C, 0xCA, 0xCA
};

// Placeholder OFF payload – fill this when you have it
uint8_t off_payload[] = {
  // TODO: replace with real OFF bytes when you capture them
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x29, 0x21, 0x83, 0x3B, 0x0B, 0x88, 0x01, 0x00, 0x00, 0x00, 0x00, 0x7D, 0x35, 0x8B, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA5, 0x24, 0x30, 0x67, 0x61, 0x71, 0x00, 0x20, 0x00, 0x00, 0x00, 0x0F, 0xA6, 0xB1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x54, 0xA4, 0x86, 0x0C, 0xEC, 0x2E, 0x20, 0x04, 0x00, 0x00, 0x00, 0x01, 0xF4, 0xD6, 0x2E
};


bool fireplace_state_on = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

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

void publish_state(const char* state) {
  Serial.print(F("[MQTT] Publishing state: "));
  Serial.println(state);
  mqttClient.publish(MQTT_STATE_TOPIC, state, true);  // retained
}

void connect_wifi() {
  Serial.print(F("[WiFi] Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
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
  mqttClient.publish(HA_DISCOVERY_TOPIC, discovery_payload, true);
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== ESP32 Fireplace Controller ==="));

  connect_wifi();

  mqttClient.setCallback(mqtt_callback);
  connect_mqtt();

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
  if (!mqttClient.connected()) {
    connect_mqtt();
  }
  mqttClient.loop();
}

