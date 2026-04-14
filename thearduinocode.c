#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include "mbedtls/md.h"
#include <time.h>

// ================= CONFIG =================
#define SS_PIN   5
#define RST_PIN  22

const char* SECRET_KEY = "rfid-ESP32-HMAC-v1-9F2C7A4D1E8B6F3C";

#define SCAN "https://rfid-api-sfmx.onrender.com/scan"

const char* ssid = "dlink";
const char* password = "zamecek48";

String MASTER_UID = "4A19EC80"; // ⚠ normalized (no colons)

// ================= STATE =================
MFRC522 rfid(SS_PIN, RST_PIN);

bool offlineMode = false;
bool enrollMode = false;
bool masterTriggered = false;

unsigned long enrollStart = 0;

String lastUID = "";
bool cardPresent = false;

// ================= HMAC =================
String hmacSHA256(String message, String key) {
  byte hmacResult[32];

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t* info =
    mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  mbedtls_md_setup(&ctx, info, 1);

  mbedtls_md_hmac_starts(&ctx,
    (const unsigned char*)key.c_str(),
    key.length());

  mbedtls_md_hmac_update(&ctx,
    (const unsigned char*)message.c_str(),
    message.length());

  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + (i * 2), "%02x", hmacResult[i]);
  }
  hex[64] = 0;

  return String(hex);
}

// ================= UID =================
String readUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();
  return uid; // ⚠ NO colons (matches backend)
}

// ================= WIFI =================
void autoconfig() {
  Serial.println("Connecting WiFi...");

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 60) {
      Serial.println("\nWiFi FAIL");
      return;
    }
  }

  Serial.println("\nWiFi OK");
}

// ================= TIME =================
void waitForTime() {
  configTime(0, 0, "pool.ntp.org");

  Serial.println("Syncing time...");

  while (time(nullptr) < 1700000000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nTime OK");
}

// ================= HTTP =================
void sendUID(String uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  time_t now = time(nullptr);

  String payload = "esp_1|" + uid + "|" + String(now);
  String signature = hmacSHA256(payload, SECRET_KEY);

  String json = "{";
  json += "\"device_id\":\"esp_1\",";
  json += "\"uid\":\"" + uid + "\",";
  json += "\"timestamp\":" + String(now) + ",";
  json += "\"signature\":\"" + signature + "\"";
  json += "}";

  Serial.println("POST:");
  Serial.println(json);

  HTTPClient http;
  http.begin(SCAN);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(json);

  Serial.print("HTTP: ");
  Serial.println(code);

  http.end();
}

// ================= EEPROM (simplified safe) =================
bool isCardStored(String uid) {
  return false; // keep simple for now
}

void saveCard(String uid) {
  Serial.println("Saved (mock)");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  SPI.begin(18, 19, 23, SS_PIN);
  rfid.PCD_Init();

  autoconfig();
  waitForTime();

  Serial.println("READY");
}

// ================= LOOP =================
void loop() {

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    cardPresent = false;
    return;
  }

  String uid = readUID();

  // debounce
  if (uid == lastUID && cardPresent) return;
  lastUID = uid;
  cardPresent = true;

  Serial.println("Card: " + uid);

  // ================= MASTER =================
  if (uid == MASTER_UID && !masterTriggered) {
    Serial.println("MASTER MODE");

    enrollMode = true;
    enrollStart = millis();
    masterTriggered = true;
  }

  // ================= ENROLL =================
  if (enrollMode) {

    if (millis() - enrollStart <= 10000) {

      if (uid != MASTER_UID) {
        saveCard(uid);
      }

    } else {
      Serial.println("Enroll timeout");
      enrollMode = false;
      masterTriggered = false;
    }
  }

  // ================= NORMAL MODE =================
  if (!enrollMode) {

    if (offlineMode) {
      Serial.println("OFFLINE MODE");
    } else {
      sendUID(uid);
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}