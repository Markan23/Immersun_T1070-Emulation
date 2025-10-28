#include <WiFi.h>
#include <PubSubClient.h>
#include <RH_RF69.h>
#include <SPI.h>

// RF69 Settings
#define RF69_FREQ 869.2 // Channel 1 on ImmerSun
#define RFM69_CS 5      // Chip select pin
#define RFM69_INT 2     // Interrupt pin
#define RFM69_RST 14    // Reset pin

// WiFi & MQTT Config (EDIT THESE LOCALLY - NEVER SHARE)
const char* ssid = "my wifi";
const char* wifiPassword = "my password";
const char* mqttServer = "my broker ip";
const int mqttPort = 1883;
const char* mqttUser = "my mqtt username";
const char* mqttPassword = "my mqtt password";

// MQTT Topics
const char* housePowerTopic = "solar_assistant/inverter_1/grid_power/state";
const char* pvGenTopic = "solar_assistant/inverter_1/pv_power/state";
const char* voltageTopic = "solar_assistant/inverter_1/grid_voltage/state";
const char* immersunTopic = "immersun/T1070/status";
const char* immersunCmd = "immersun/T1070/cmd";
const char* immersunState ="immersun/T1070/state";

// RFM69
RH_RF69 rf69(RFM69_CS, RFM69_INT);
uint8_t pkt[32]={
  0xD0, 0x00, 0x00, 0x02, 0x01, 0x02, 0x18, 0x4E,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0,
  0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF3,
  0x00, 0x00, 0x00, 0xD3, 0x07, 0x00, 0x00, 0x80 
  };

// Globals
float meterCT = 250.0;      // Default import
float pvGen = 300.0;        // Default gen
float excess = 50.0;         // Default excess   
int32_t v = 239;            // default voltage
int cmd = 1;                // default enable diverter
unsigned long lastMqttReconnect = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastDebugPrint = 0;
unsigned long lastTx = 0;
uint8_t seq = 0x20;         // Start at 0x20 per your note/logs
bool mqttConnected = false;

uint8_t spiReadRegister(uint8_t address) {
  digitalWrite(RFM69_CS, LOW);
  SPI.transfer(address & 0x7F);
  uint8_t value = SPI.transfer(0);
  digitalWrite(RFM69_CS, HIGH);
  return value;
  }

void spiWriteRegister(uint8_t address, uint8_t value) {
  digitalWrite(RFM69_CS, LOW);
  SPI.transfer(address | 0x80);
  SPI.transfer(value);
  digitalWrite(RFM69_CS, HIGH);
}
WiFiClient T1070Client;
PubSubClient mqttClient(T1070Client);


//  MQTT Callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Plain float fallback for Solar Assistant (e.g., "500.0")
  float value = message.toFloat();
  if (strcmp(topic, housePowerTopic) == 0) {
    meterCT = value;
  } else if (strcmp(topic, pvGenTopic) == 0) {
    pvGen = value;
  } else if (strcmp(topic, voltageTopic) == 0) {
    v = value;
  } else if (strcmp(topic, immersunCmd) == 0) {
    cmd = int(trunc(value));
  }
  //excess = meterCT + pvGen;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(1); }
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);  
  delay(100);
  digitalWrite(RFM69_RST, HIGH); 
  delay(100);
  digitalWrite(RFM69_RST, LOW);
  delay(1000);
  Serial.println("ESP32 T1070 Clone with MQTT");

  // WiFi
  WiFi.begin(ssid, wifiPassword);
  Serial.print("WiFi connecting");
  int wifiTries = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTries < 20) {
    delay(500);
    Serial.print(".");
    wifiTries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAIL - Check SSID/PW");
  }

  // MQTT
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);

  // RFM69
  if (!rf69.init()) {
    Serial.println("RFM69 init fail");
    while (1);
  }

  if (!rf69.setFrequency(RF69_FREQ)) {
    Serial.println("setFrequency failed");
    while (1);
  }

  rf69.setModemConfig(RH_RF69::CUSTOM_CONFIG);  // Revert to this if custom fails; comment below lines to test
  rf69.setTxPower(20, true);
  rf69.setThisAddress(0xF5); // Match URH
  rf69.setHeaderFrom(0x0F5);
  rf69.setHeaderTo(0x00);
  spiWriteRegister(0x39, 0xF5);
  spiWriteRegister(0x37, 0x9C);
  spiWriteRegister(0x3A, 0x00);
  rf69.setHeaderId(0x4E);
  rf69.setHeaderFlags(0xFF);
  rf69.setPromiscuous(true);
  rf69.setPreambleLength(3);
  uint8_t syncwords[] = {0x69, 0x81, 0x7e, 0x96};
  rf69.setSyncWords(syncwords, sizeof(syncwords));
  rf69.setModeRx();
  lastHeartbeat = millis();
  Serial.println("RFM69 RX ready - Heartbeat timeout: 3s");
}

void loop() {
  unsigned long now = millis();

  // MQTT Reconnect every 5s
  if (!mqttClient.connected() && now - lastMqttReconnect > 5000) {
    Serial.print("MQTT reconnect...");
    if (mqttClient.connect("ESP32Clone", mqttUser, mqttPassword)) {
      mqttClient.subscribe(housePowerTopic);
      mqttClient.subscribe(pvGenTopic);
      mqttClient.subscribe(voltageTopic);
      mqttClient.subscribe(immersunCmd);
      mqttClient.subscribe(immersunTopic);
      mqttConnected = true;
      Serial.println("OK");
    } else {
      Serial.printf("FAIL state=%d\n", mqttClient.state());
    }
    lastMqttReconnect = now;
  }
  mqttClient.loop();

  // Status debug every 10s
  if (now - lastDebugPrint > 10000) {
    Serial.printf("Status: WiFi=%s, MQTT=%s, LastHB=%lums ago, MeterCT=%.1fW, PV=%.1fW\n\r",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
                  mqttConnected ? "OK" : "DOWN",
                  now - lastHeartbeat, meterCT, pvGen);
    lastDebugPrint = now;
  }

  // Heartbeat RX

  if (rf69.available()) {
    uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf69.recv(buf, &len)) {
       //    Serial.printf("RX len=%d, first bytes: %02X %02X %02X...\n", len, buf[0], buf[1], buf[2]);
      if (len == 7 && buf[0] == 0xD0 && buf[1] == 0xFF && buf[2] == 0x00 && 
          buf[3] == 0x01 && buf[4] == 0x02 && buf[5] == 0x01 && buf[6] == 0x00) {
        Serial.println("Heartbeat confirmed");
        lastHeartbeat = now;
        //rf69.setModeTx();
        sendResponse();
        delay(50);  // Settle TX->RX
        rf69.setModeRx();
        lastTx = now;
      }
    }
  }

  // Fallback TX every 2s if no HB >3s
  if (now - lastHeartbeat > 5000 && now - lastTx > 2000) {
    Serial.println("No HB timeout - Fallback TX");
    //rf69.setModeTx();
    sendResponse();
    delay(50);
    rf69.setModeRx();
    lastTx = now;
  }
}

void sendResponse() {
  // Fixed headers
  rf69.setThisAddress(0xf5);
  rf69.setHeaderTo(0x00);
  rf69.setHeaderFrom(0xF5);
  rf69.setHeaderId(0x4E);
  rf69.setHeaderFlags(0xFF);  
  pkt[0] = 0xD0; pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x02; pkt[4] = 0x01; pkt[5] = 0x02; pkt[6] = 0x18;
  memset(pkt + 8, 0x00, 3); memset(pkt + 12, 0x00, 3); memset(pkt + 24, 0x00, 3);

  // MQTT values + jitter
  int32_t ct1 = (int32_t)(meterCT + ((millis() % 1000) / 500.0 - 1.0));  // ±1W
  if (ct1 < -50) {
    excess = (ct1 * -1);
  }
  // My inverter takes care of charging battery first so the 2 CTs are not necessary
  // I just need to sense when the excess is being exported and how much
  //int32_t ct1 = (int32_t)(meterCT + ((millis() % 1000) / 500.0 - 1.0));  // ±1W
  int32_t ct2 = (int32_t)(pvGen + ((millis() % 1000) / 500.0 - 1.0));
  //int32_t excess = ct2 - ct1;
  uint8_t cf1 = (uint8_t)(abs(ct1) / 24);
  uint8_t cf2 = (uint8_t)(abs(ct2) / 24);
  auto encodeLE32 = [](uint8_t* buf, int32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
  };
  if (cmd == 0) {
    ct1 = 0;
    ct2 = 0;
     mqttClient.publish(immersunState, "OFF");
    // publish immersun off
  } else {
     mqttClient.publish(immersunState, "ON");
    // publish immersun on
  }
  encodeLE32(pkt + 15, ct1);
  encodeLE32(pkt + 19, ct2);

  // Myst
  int32_t myst;
  if (abs(excess) < 200) {
    myst = excess < 0 ? ct1 : ct2;  // Low-power mirror
  } else {
    myst = (int32_t)round((double)excess * 1.537) + ((millis() % 5) - 2);  // ±2 jitter
  }
  encodeLE32(pkt + 27, myst);
  pkt[7] = cf1; pkt[11] = cf2; pkt[23] = (uint8_t)v;
  pkt[31] = seq++;  // Inc always
  // Hex dump
  //Serial.print("To be TXd Hex: ");
  //for (int i = 0; i < 32; i++) {
  //  Serial.printf("%02X ", pkt[i]);
  //  } 
  Serial.println();
  Serial.printf("Updated: MeterCT=%.1fW, PV=%.1fW Excess=%.1fW\n\r", meterCT, pvGen, excess);
  rf69.send(pkt, 32);
  if (rf69.waitPacketSent()) {
    Serial.println("Data sent OK");
    } else {
    Serial.println("Data send failed/timeout");
    }
}
