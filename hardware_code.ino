// =============================================================
//  Soil IoT Monitor — NodeMCU ESP8266
//  Sensors: MH Moisture | DS18B20 Temp | NPK RS485
//  Cloud:   Firebase Realtime Database
// =============================================================

#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>

// ----------------------------------------------------------
// 1. CONFIGURATION — fill in your details
// ----------------------------------------------------------
#define WIFI_SSID        "Oppo A78"
#define WIFI_PASSWORD    "sweety.."
#define FIREBASE_HOST    "soil-health-monitoring-system-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH    "c8CzUmNnhV5dSdkykWvDPErac1ERIaNd9lMZMXgG"

// ----------------------------------------------------------
// 2. PIN DEFINITIONS
// ----------------------------------------------------------
#define MOISTURE_ANALOG_PIN   A0    // MH sensor AO → A0
#define ONE_WIRE_PIN          D4    // DS18B20 Data → D4

// NPK RS485 pins — using same pins as your working NPK code
// NodeMCU D5=GPIO14, D6=GPIO12, D3=GPIO0, D8=GPIO2
#define RS485_RX_PIN          D6    // GPIO14 → MAX485 RO
#define RS485_TX_PIN          D5    // GPIO12 → MAX485 DI
#define DE_PIN                D7    // GPIO0  → MAX485 DE
#define RE_PIN                D0    // GPIO2  → MAX485 RE

// ----------------------------------------------------------
// 3. TIMING
// ----------------------------------------------------------
#define READ_INTERVAL_MS  10000

// ----------------------------------------------------------
// 4. NPK MODBUS QUERY FRAMES
// ----------------------------------------------------------
//const byte nitro[] = {0x01, 0x03, 0x00, 0x1e, 0x00, 0x01, 0xe4, 0x0c};
//const byte nitro[] = {0x01, 0x03, 0x00, 0x1d, 0x00, 0x01, 0x15, 0xcc};
const byte nitro[] = { 0x01, 0x03, 0x00, 0x1e, 0x00, 0x01, 0xe4, 0x0c };
const byte phos[] = { 0x01, 0x03, 0x00, 0x1f, 0x00, 0x01, 0xb5, 0xcc };
const byte pota[] = { 0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xc0 };

byte values[11];

// ----------------------------------------------------------
// 5. OBJECTS
// ----------------------------------------------------------
OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);

// SoftwareSerial with 4800 baud — exactly as in your working NPK code
SoftwareSerial    mod(RS485_RX_PIN, RS485_TX_PIN);

FirebaseData      fbData;
FirebaseConfig    fbConfig;
FirebaseAuth      fbAuth;

unsigned long lastReadTime = 0;

// ----------------------------------------------------------
// 6. SENSOR HELPERS
// ----------------------------------------------------------

/*int readMoisture() {
  int raw = analogRead(A0);
  Serial.print("[RAW MOISTURE] ");
  int moisture = map(raw, 1023, 0, 0, 100);
  return moisture;
}*/
int readMoisture() {
  int raw = analogRead(MOISTURE_ANALOG_PIN);
  return constrain(map(raw, 1023, 0, 0, 100), 0, 100);
}
/*int readMoisture() {
  int raw = analogRead(A0);

  Serial.print("[RAW] ");
  Serial.println(raw);

  // temporary return raw for debugging
  return raw;
}*/

float readTemperature() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("[TEMP] Sensor not found!");
    return -999.0;
  }
  return t;
}

// ----------------------------------------------------------
// 7. NPK FUNCTIONS — copied exactly from your working code
//    Only change: DE_PIN / RE_PIN instead of hardcoded 0/2
// ----------------------------------------------------------

byte readNPKValue(const byte* cmd) {
  // Flush old data
  while (mod.available()) mod.read();

  digitalWrite(DE_PIN, HIGH);
  digitalWrite(RE_PIN, HIGH);
  delay(50);
  mod.write(cmd, 8);
  mod.flush();                  // wait until all bytes are sent

  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, LOW);

  // Wait up to 1 second for 7 bytes response
  unsigned long start = millis();
  int index = 0;
  while (millis() - start < 2000) {
    if (mod.available()) {
      values[index++] = mod.read();
      if (index >= 7) break;
    }
  }

  if (index < 7) {
    Serial.printf("[NPK] Got %d/7 bytes\n", index);
    return 0;
  }
  return values[4];
}

byte nitrogen()    { return readNPKValue(nitro); }
byte phosphorous() { return readNPKValue(phos);  }
byte potassium()   { return readNPKValue(pota);  }

// ----------------------------------------------------------
// 8. SETUP
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Soil IoT Monitor booting ===");

  // RS485 DE/RE pins
  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, LOW);   // start in receive mode

  // NPK serial at 4800 baud — same as your working NPK code
  mod.begin(4800);

  // DS18B20
  pinMode(ONE_WIRE_PIN, INPUT_PULLUP);
  tempSensor.begin();
  Serial.print("[TEMP] DS18B20 sensors found: ");
  Serial.println(tempSensor.getDeviceCount());

  // Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());

  // Firebase
  Firebase.setReadTimeout(fbData, 1000 * 60);
  Firebase.setwriteSizeLimit(fbData, "tiny");
  fbConfig.host         = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  Serial.println("[Firebase] Ready");
}

// ----------------------------------------------------------
// 9. LOOP
// ----------------------------------------------------------
void loop() {
  if (millis() - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = millis();

    // --- Moisture ---
    int moisture = readMoisture();
    Serial.printf("[MOISTURE] %d%%\n", moisture);

    // --- Temperature ---
    float tempC = readTemperature();
    if (tempC != -999.0)
      Serial.printf("[TEMP] %.2f C\n", tempC);

    // --- NPK (using your proven working functions) ---
    byte N = nitrogen();
    delay(1000);
    byte P = phosphorous();
    delay(1000);
    byte K = potassium();
    delay(1000);
    Serial.printf("[NPK] N=%d  P=%d  K=%d  mg/kg\n", N, P, K);

    // --- Firebase upload ---
    if (WiFi.status() == WL_CONNECTED) {
      String path = "/soil";
      Firebase.setInt(fbData,   path + "/moisture_pct",    moisture);
      if (tempC != -999.0)
        Firebase.setFloat(fbData, path + "/temperature_c", tempC);
      Firebase.setInt(fbData,   path + "/nitrogen_mgkg",   N);
      Firebase.setInt(fbData,   path + "/phosphorus_mgkg", P);
      Firebase.setInt(fbData,   path + "/potassium_mgkg",  K);
      Firebase.setInt(fbData,   path + "/uptime_ms",       (int)millis());
      Serial.println("[Firebase] Upload done");
    } else {
      Serial.println("[WiFi] Disconnected — skipping upload");
    }
    Serial.println("---");
  }
}