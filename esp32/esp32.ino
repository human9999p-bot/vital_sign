#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <heartRate.h>
#include <Adafruit_GFX.h>
#include "Adafruit_SSD1306.h"
#include <ArduinoJson.h>

#include <WebServer.h>
#include <EEPROM.h>

#define EEPROM_SIZE 512
void printToScreen();
void sendToServer();
void handleBuzzer();
String getISOTime();
WebServer server(80);

// ===== Pins =====
int LED1 = 16;
int button = 17;
int buzzerPin =18;
unsigned long buzzerMillis = 0;
bool buzzerState = false;
const long buzzerInterval = 500; 
unsigned long currentMillis= millis();

// ===== Button =====
long buttonTimer = 0;
long longPressTime = 1000;
bool buttonActive = false;
bool longPressActive = false;

// ===== WiFi =====
IPAddress local_IP(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);
const char* serverUrl = "https://vitalsign-psi.vercel.app/api/sensor";
String esid = "";
String epass = "";
String wifiListHTML = "";

//============= MAX30102 & OLED =============
String dn = "max1";

MAX30105 particleSensor;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint32_t irBuffer[100];
uint32_t redBuffer[100];

int32_t bufferLength = 75;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 500;

// ================= WIFI SCAN =================
void scanWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);

  int n = WiFi.scanNetworks();
  wifiListHTML = "";
  if (n == 0) {
    wifiListHTML = "<p>No WiFi networks found</p>";
  } else {
    wifiListHTML = "<ul>";
    for (int i = 0; i < n; i++) {
      wifiListHTML += "<li onclick=\"setSSID('" + WiFi.SSID(i) + "')\">";
      wifiListHTML += WiFi.SSID(i);
      wifiListHTML += " (" + String(WiFi.RSSI(i)) + " dBm)</li>";
    }
    wifiListHTML += "</ul>";
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-SETUP","12345678",1);
}

// ================= WEB PAGE =================
String mainPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8" name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 WiFi Setup</title>
<style>
body { font-family: Arial; background:#0f172a; color:white; text-align:center; }
.container { background:#1e293b; padding:20px; margin:20px auto; max-width:420px; border-radius:12px; }
input { width:90%; padding:10px; margin:6px; border-radius:6px; border:none; font-size:16px; }
button { width:95%; padding:12px; background:#22c55e; border:none; border-radius:6px; font-weight:bold; cursor:pointer; font-size:16px; }

ul { list-style:none; padding:0; }
li { background:#334155; margin:5px; padding:12px; border-radius:6px; cursor:pointer; font-size:16px; transition:0.2s; }
li:hover { background:#22c55e; color:black; transform:scale(1.02); }

.password-box { position:relative; width:90%; margin:auto; }
.password-box input { padding-right:45px; font-size:16px; }
.password-box svg {
  position:absolute;
  right:10px;
  top:50%;
  transform:translateY(-50%);
  cursor:pointer;
  fill:none;
  stroke:#94a3b8;
  stroke-width:2;
}
.password-box svg:hover { stroke:white; }
</style>

<script>
function setSSID(name){
  document.getElementById("ssid").value = name;
}

function togglePassword(){
  const p = document.getElementById("pass");
  const eye = document.getElementById("eye");
  if(p.type === "password"){
    p.type = "text";
    eye.style.opacity = "0.4";
  } else {
    p.type = "password";
    eye.style.opacity = "1";
  }
}
</script>
</head>

<body>
<div class="container">
<h2>ESP32 WiFi Setup</h2>

<form action="/save">
  <!-- SSID -->
  <div style="margin-bottom:12px;">
    <input id="ssid" name="ssid" placeholder="WiFi SSID" required style="width:100%; padding:10px; font-size:16px; border-radius:6px; border:none;">
  </div>

  <!-- Password -->
  <div class="password-box" style="position:relative; width:100%; margin-bottom:12px;">
    <input id="pass" name="pass" type="password" placeholder="Password" required 
           style="width:100%; padding:10px 45px 10px 10px; font-size:16px; border-radius:6px; border:none;">
    <svg id="eye" onclick="togglePassword()" 
         xmlns="http://www.w3.org/2000/svg" 
         width="24" height="24" 
         viewBox="0 0 24 24"
         style="position:absolute; right:10px; top:50%; transform:translateY(-50%); cursor:pointer; stroke:#94a3b8; stroke-width:2; fill:none;">
      <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
      <circle cx="12" cy="12" r="3"/>
    </svg>
  </div>

  <!-- Submit Button -->
  <button type="submit" style="width:100%; padding:12px; background:#22c55e; border:none; border-radius:6px; font-size:16px; font-weight:bold; cursor:pointer;">
    Save & Connect
  </button>
</form>


<hr>
<h3>Available Networks</h3>
)rawliteral";

  page += wifiListHTML;

  page += R"rawliteral(
<form action="/rescan">
<button type="submit">🔄 Refresh Scan</button>
</form>
</div>
</body>
</html>
)rawliteral";

  return page;
}

// ================= SETUP AP =================
void setupAP() {
  WiFi.disconnect(true);
  delay(500);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("ESP32-SETUP","12345678",1);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  scanWiFi();

  server.on("/", []() {
    server.send(200, "text/html", mainPage());
  });

  server.on("/rescan", []() {
    scanWiFi();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/save", []() {
    String qsid  = server.arg("ssid");
    String qpass = server.arg("pass");

    for (int i = 0; i < 96; i++) EEPROM.write(i, 0);
    for (int i = 0; i < qsid.length(); i++) EEPROM.write(i, qsid[i]);
    for (int i = 0; i < qpass.length(); i++) EEPROM.write(32 + i, qpass[i]);
    EEPROM.commit();

    server.send(200, "text/html", "<h2>Saved!</h2><p>Connecting...</p>");

    WiFi.mode(WIFI_STA);
    WiFi.begin(qsid.c_str(), qpass.c_str());
    Serial.println("Connecting to WiFi...");
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
      delay(500);
      Serial.print(".");
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.println("✅ Connected successfully!");
      Serial.print("SSID: ");
      Serial.println(qsid);
      Serial.print("Password: ");
      Serial.println(qpass);
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      digitalWrite(LED1, HIGH);
    } else {
      Serial.println();
      Serial.println("❌ Failed to connect!");
      digitalWrite(LED1, LOW);
    }

    delay(2000);
    ESP.restart();
  });

  server.begin();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  pinMode(LED1, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  digitalWrite(buzzerPin,LOW);

  oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.clearDisplay();

  for (int i = 0; i < 32; i++) esid += char(EEPROM.read(i));
  for (int i = 32; i < 96; i++) epass += char(EEPROM.read(i));

  if (esid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(esid.c_str(), epass.c_str());
    delay(5000);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    setupAP();
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) 
  {
    Serial.println("MAX30102 not found");
    while (1);
  }
  particleSensor.setup(60,4,2,200,411,4096);
  Serial.println("Sensor ready");
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  // Long press لمسح EEPROM
  if (digitalRead(button) == LOW) {
    if (!buttonActive) {
      buttonActive = true;
      buttonTimer = millis();
    }
    if (millis() - buttonTimer > longPressTime && !longPressActive) {
      longPressActive = true;
      for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
      EEPROM.commit();
      digitalWrite(LED1, HIGH);
      Serial.println("EEPROM cleared, LED turned off");
      delay(1000);
      ESP.restart();
    }
  } else {
    buttonActive = false;
    longPressActive = false;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    // Collect samples
    for (byte i = 0; i < bufferLength; i++) {
      while (!particleSensor.available())
        particleSensor.check();

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i]  = particleSensor.getIR();
      particleSensor.nextSample();
      handleBuzzer();
    }

    maxim_heart_rate_and_oxygen_saturation(
      irBuffer, bufferLength, redBuffer,
      &spo2, &validSPO2, &heartRate, &validHeartRate
    );

    printToScreen(); 
    // Send every 0.5 seconds
    if (millis() - lastSend > SEND_INTERVAL) {
      lastSend = millis();
      sendToServer();
    }
  }
  handleBuzzer(); 
}

// ===================== SEND TO SERVER =====================
void sendToServer() {

  if (WiFi.status() != WL_CONNECTED) return;
  if (!validHeartRate || !validSPO2) return;
  if (heartRate < 30 || heartRate > 220) return;
  if (spo2 < 80 || spo2 > 100) return;

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["device_id"] = dn;
  doc["heartrate"] = heartRate;
  doc["spo2"] = spo2;
  doc["time"] = getISOTime();

  String payload;
  serializeJson(doc, payload);

  int res = http.POST(payload);
  Serial.println(payload);
  Serial.printf("POST: %d\n", res);

  http.end();
}

// ===================== ISO Timestamp =====================
String getISOTime() {
  time_t now = time(nullptr);
  struct tm *t = gmtime(&now);

  char buf[30];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
    t->tm_year + 1900,
    t->tm_mon + 1,
    t->tm_mday,
    t->tm_hour,
    t->tm_min,
    t->tm_sec
  );
  return String(buf);
}

// ===================== OLED =====================
void printToScreen() {
  oled.clearDisplay();
  oled.setCursor(0,0);

  if (validHeartRate && validSPO2) {
    oled.print("HR:");
    oled.println(heartRate);
    oled.print("SpO2:");
    oled.println(spo2);
  } else {
    oled.println("No Signal");
  }

  oled.display();
}

// ===================== Buzzer =====================
void handleBuzzer() {
  uint32_t irValue = irBuffer[bufferLength-1];

  if (irValue > 5000 && validSPO2 && (spo2 < 96 || spo2 > 100)) {
    unsigned long currentMillis = millis();
    if (currentMillis - buzzerMillis >= buzzerInterval) {
      buzzerMillis = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState);
    }
  } else {
    buzzerState = false;
    digitalWrite(buzzerPin, LOW);
  }
}
