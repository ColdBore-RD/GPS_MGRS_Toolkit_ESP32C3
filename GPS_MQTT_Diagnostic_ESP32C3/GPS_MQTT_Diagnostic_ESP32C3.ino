#include <WiFi.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <MGRS.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASSWORD

// ============================================================
// XIAO ESP32-C3 + GPS + MQTT Diagnostic Publisher
//
// Same GPS field set as GPS_KitchenSink_FullFunctionTest_ESP32C3_v2,
// but published as a single JSON payload over MQTT once per second
// instead of (in addition to) dumping to the serial monitor. Meant
// for viewing live GPS status in Node-RED without a laptop tethered
// to the serial port.
//
// Wiring:
// GPS TX  -> XIAO D7 / GPIO20
// GPS RX  -> XIAO D6 / GPIO21
// GPS GND -> XIAO GND
// GPS VCC -> Appropriate supply for your GPS module
//
// MQTT:
// Broker credentials live in secrets.h ONLY — never commit that file.
// Data   topic: sensors/gps-tracker/diagnostic
// Status topic: sensors/gps-tracker/status  ("online" / "offline" LWT)
//
// Arduino Serial Monitor: 115200 baud
// GPS module baud:        9600 baud
// ============================================================

static const int GPS_RX_PIN = 20;  // XIAO D7, receives from GPS TX
static const int GPS_TX_PIN = 21;  // XIAO D6, sends to GPS RX
static const uint32_t GPS_BAUD = 9600;

#define MQTT_TOPIC_DATA    "sensors/gps-tracker/diagnostic"
#define MQTT_TOPIC_STATUS  "sensors/gps-tracker/status"

static const unsigned long PUBLISH_INTERVAL_MS  = 1000;
static const unsigned long RECONNECT_INTERVAL_MS = 5000;
static const unsigned long WARNING_INTERVAL_MS   = 5000;

TinyGPSPlus gps;
HardwareSerial GPSSerial(1);

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastPublish       = 0;
unsigned long lastReconnect     = 0;
unsigned long lastNoDataWarning = 0;

// Forward declarations
void connectWiFi();
bool connectMQTT();
void publishDiagnostic();
size_t buildDiagnosticPayload(char* buffer, size_t bufferSize);

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 5000)
  {
    delay(10);
  }

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F("XIAO ESP32-C3 GPS + MQTT Diagnostic"));
  Serial.println(F("=========================================="));
  Serial.print(F("TinyGPSPlus version: "));
  Serial.println(TinyGPSPlus::libraryVersion());
  Serial.println(F("GPS RX: GPIO20 / XIAO D7"));
  Serial.println(F("GPS TX: GPIO21 / XIAO D6"));
  Serial.println(F("GPS baud: 9600"));
  Serial.println();

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(768);
  connectMQTT();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  while (GPSSerial.available() > 0)
  {
    gps.encode(GPSSerial.read());
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[WiFi] Lost — reconnecting..."));
    connectWiFi();
  }

  if (!mqtt.connected())
  {
    unsigned long now = millis();
    if (now - lastReconnect > RECONNECT_INTERVAL_MS)
    {
      lastReconnect = now;
      connectMQTT();
    }
  }
  mqtt.loop();

  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS)
  {
    lastPublish = millis();
    publishDiagnostic();
  }

  if (
    millis() > 5000 &&
    gps.charsProcessed() < 10 &&
    millis() - lastNoDataWarning >= WARNING_INTERVAL_MS
  )
  {
    lastNoDataWarning = millis();
    Serial.println(F("WARNING: No GPS serial data detected."));
    Serial.println(F("Check power, ground, TX/RX wiring and GPS baud."));
  }
}

// ============================================================
// WIFI / MQTT
// ============================================================

void connectWiFi()
{
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

bool connectMQTT()
{
  Serial.printf("[MQTT] Connecting to %s:%d ... ", MQTT_HOST, MQTT_PORT);

  String clientId = "xiao-gps-tracker-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  bool ok = mqtt.connect(
    clientId.c_str(),
    MQTT_USER,
    MQTT_PASSWORD,
    MQTT_TOPIC_STATUS, 1, true, "offline"  // LWT
  );

  if (ok)
  {
    Serial.println("connected.");
    mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
  }
  else
  {
    Serial.printf("failed (rc=%d)\n", mqtt.state());
  }

  return ok;
}

// ============================================================
// DIAGNOSTIC PAYLOAD
// ============================================================

// Builds the JSON diagnostic payload into buffer, returns the
// number of bytes written (not including the terminator).
size_t buildDiagnosticPayload(char* buffer, size_t bufferSize)
{
  bool hasFix = gps.location.isValid();

  double  latitude  = hasFix ? gps.location.lat() : 0.0;
  double  longitude = hasFix ? gps.location.lng() : 0.0;

  char mgrsString[24] = "N/A";
  if (hasFix)
  {
    MGRSCoordinate position;
    if (latLonToMGRS(latitude, longitude, position))
    {
      strncpy(mgrsString, position.mgrs, sizeof(mgrsString) - 1);
      mgrsString[sizeof(mgrsString) - 1] = '\0';
    }
    else
    {
      strncpy(mgrsString, "OUT_OF_RANGE", sizeof(mgrsString) - 1);
    }
  }

  float altitudeM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0f;
  float speedMph  = gps.speed.isValid() ? gps.speed.mph() : 0.0f;
  float courseDeg = gps.course.isValid() ? gps.course.deg() : 0.0f;
  int   satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  float hdop = (gps.hdop.isValid() && gps.hdop.hdop() < 99.0) ? gps.hdop.hdop() : 99.9f;

  char dateStr[11] = "N/A";
  if (gps.date.isValid())
  {
    snprintf(dateStr, sizeof(dateStr), "%04u-%02u-%02u",
      gps.date.year(), gps.date.month(), gps.date.day());
  }

  char timeStr[9] = "N/A";
  if (gps.time.isValid())
  {
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u",
      gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  return snprintf(buffer, bufferSize,
    "{"
      "\"fix\":%s,"
      "\"lat\":%.6f,"
      "\"lon\":%.6f,"
      "\"mgrs\":\"%s\","
      "\"altitude_m\":%.1f,"
      "\"speed_mph\":%.2f,"
      "\"course_deg\":%.2f,"
      "\"satellites\":%d,"
      "\"hdop\":%.2f,"
      "\"date_utc\":\"%s\","
      "\"time_utc\":\"%s\","
      "\"chars_processed\":%lu,"
      "\"chars_failed_checksum\":%lu,"
      "\"fix_sentences\":%lu,"
      "\"uptime_s\":%lu"
    "}",
    hasFix ? "true" : "false",
    latitude,
    longitude,
    mgrsString,
    altitudeM,
    speedMph,
    courseDeg,
    satellites,
    hdop,
    dateStr,
    timeStr,
    gps.charsProcessed(),
    gps.failedChecksum(),
    gps.sentencesWithFix(),
    millis() / 1000
  );
}

void publishDiagnostic()
{
  char payload[512];
  buildDiagnosticPayload(payload, sizeof(payload));

  if (mqtt.connected())
  {
    mqtt.publish(MQTT_TOPIC_DATA, payload);
  }

  Serial.print(F("[MQTT] "));
  Serial.println(payload);
}
