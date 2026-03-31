#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

// -------- CONFIG --------
#define SS_PIN   5
#define RST_PIN  22

#define EEPROM_SIZE 512
#define MAX_CARDS 20
#define UID_LENGTH 20

#define SCAN "https://rfid-api-sfmx.onrender.com/scan"
#define CHECK "https://rfid-api-sfmx.onrender.com/check/"
#define USERS "https://rfid-api-sfmx.onrender.com/users"

//------for-auto-config----------//
const char* ssid = "dlink";       // <-- replace with your WiFi SSID
const char* password = "zamecek48"; //<-- replace with the password

String MASTER_UID = "0A:DE:C9:80"; // CHANGE THIS

MFRC522 rfid(SS_PIN, RST_PIN);

// -------- MODE --------
bool offlineMode = false;
unsigned long enrollStart = 0;
bool enrollMode = false;

// -------- SERVER --------
const char* apiUrl = "https://rfid-api-sfmx.onrender.com";

// -------- FUNCTIONS --------

String readUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

// -------- EEPROM --------

void saveCard(String uid) {
  for (int i = 0; i < MAX_CARDS; i++) {
    int addr = i * UID_LENGTH;

    if (EEPROM.read(addr) == 0xFF) {
      for (int j = 0; j < uid.length(); j++) {
        EEPROM.write(addr + j, uid[j]);
      }
      EEPROM.write(addr + uid.length(), '\0');
      EEPROM.commit();

      Serial.println("Card saved ✅");
      return;
    }
  }
  Serial.println("EEPROM full ❌");
}

bool isCardStored(String uid) {
  for (int i = 0; i < MAX_CARDS; i++) {
    int addr = i * UID_LENGTH;

    String stored = "";
    for (int j = 0; j < UID_LENGTH; j++) {
      char c = EEPROM.read(addr + j);
      if (c == '\0' || c == 0xFF) break;
      stored += c;
    }

    if (stored == uid) return true;
  }
  return false;
}

// -------- WIFI --------

void connectWiFi() {
  Serial.println("Scanning WiFi...");

  int n = WiFi.scanNetworks();

  for (int i = 0; i < n; i++) {
    Serial.printf("%d: %s\n", i + 1, WiFi.SSID(i).c_str());
  }

  Serial.println("Select network:");

  while (!Serial.available());
  int choice = Serial.parseInt();

  String ssid = WiFi.SSID(choice - 1);

  Serial.println("Enter password:");
  String pass = Serial.readStringUntil('\n');

  WiFi.begin(ssid.c_str(), pass.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi ready ✅");
}

void autoconfig(){

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 60) { // timeout after ~30 seconds
      Serial.println("\nFailed to connect to WiFi ❌");
      return;
    }
  }

  Serial.println("\nWiFi connected ✅");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}
// -------- HTTP --------

void sendUID(String uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;

  http.begin(client, SCAN);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"rfid\":\"" + uid + "\"}";
  http.POST(payload);

  http.end();
}

// -------- SETUP --------

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  Serial.println("Offline mode? (y/n) or for auto config (c)");

  while (!Serial.available());
  char choice = Serial.read();

  if (choice == 'y') {
    offlineMode = true;
    Serial.println("Offline mode enabled 📴");
  } else if (choice == 'n'){
    connectWiFi();
  } else if (choice == 'c'){
    autoconfig();
  }
  

  SPI.begin(18, 19, 23, SS_PIN);
  rfid.PCD_Init();

  Serial.println("RFID ready");
}



// -------- LOOP --------

void loop() {
  // Look for new card
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = readUID();
  // Remove trailing colon if present
  if (uid.endsWith(":")) uid = uid.substring(0, uid.length() - 1);

  Serial.print("Card: ");
  Serial.println(uid);

  // -------- MASTER CARD --------
  if (uid == MASTER_UID) {
    Serial.println("MASTER CARD DETECTED 🧠");
    Serial.println("Place new card within 10 seconds...");
    enrollMode = true;
    enrollStart = millis();
    // Do NOT halt here, allow next scan
  }

  // -------- ENROLL MODE --------
  if (enrollMode) {
    if (millis() - enrollStart <= 10000) {
      if (uid != MASTER_UID) { // ignore master card
        if (!isCardStored(uid)) {
          saveCard(uid);
        } else {
          Serial.println("Already exists ⚠️");
        }
      }
    } else {
      Serial.println("Enroll timeout ❌");
      enrollMode = false;
    }
  }
  // -------- OFFLINE / ONLINE MODE --------
  if (!enrollMode) {
    if (offlineMode) {
      if (isCardStored(uid)) Serial.println("ACCESS GRANTED ✅");
      else Serial.println("ACCESS DENIED ❌");
    } else {
      sendUID(uid);
    }
  }

  // Halt card & stop crypto
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}




