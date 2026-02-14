#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPUpdateServer.h>
#include <PubSubClient.h>   

#define BUZZER_PIN 4  // Pin für Soundbuzzer
#define SWITCH_PIN 3  // Pin für Mikroschalter (gegen GND)
//#define LED_PIN LED_BUILTIN     // interne LED
#define LED_PIN 8
#define FIRMWARE_VERSION "micronano v.13.02.26_mqtt_F"

WebServer server(80);
HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
String clientId = "Buzzer-" + String(random(0xffff), HEX);
bool mqttok = false;

// Für WebUI-Kompatibilität
bool inAPMode = false;

unsigned long lastLEDToggle = 0;
bool ledState = false;
const unsigned long LED_BLINK_INTERVAL = 500;  // 500ms


struct Config {
  String ssid;
  String password;
  String command;
  String localIP;
  String gateway;
  bool buzzerEnabled;
  bool mqttEnabled;
  String mqttServer;
  String mqttUser;
  String mqttPassword;
  int mqttPort;
  String mqttTopic;
  String mqttPayloadSuccess;
};

Config config;

// --- Taster Handling ---
bool lastSwitchState = HIGH;
unsigned long switchLockUntil = 0;
const unsigned long SWITCH_LOCK_TIME = 1000;  // 1s Sperre

// ------------------------------------------------
// ---------------- BEEPER ------------------------
// ------------------------------------------------
void beepAsync(int count, int delayMs = 80) {

  static int remaining = 0;
  static unsigned long lastToggle = 0;
  static bool state = false;
  static int interval = 80;

  if (!config.buzzerEnabled) {
    digitalWrite(BUZZER_PIN, LOW);
    remaining = 0;
    return;
  }

  // 🔥 NEU: nur starten wenn aktuell nichts läuft
  if (count > 0 && remaining == 0) {
    remaining = count * 2;
    interval = delayMs;
    lastToggle = millis();
    state = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (remaining > 0 && millis() - lastToggle >= interval) {
    lastToggle = millis();
    state = !state;
    digitalWrite(BUZZER_PIN, state);
    remaining--;

    if (remaining == 0) {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}


void blinkLED(int count, int delayMs = 300) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, LOW);   // AN (blau)
    delay(delayMs);
    digitalWrite(LED_PIN, HIGH);  // AUS
    delay(delayMs);
  }
}

// ------------------------------------------------
// ---------------- MQTT --------------------------
// ------------------------------------------------
bool connectMQTT() {
    if (!config.mqttEnabled) return false;

    // Server setzen
    mqttClient.setServer(config.mqttServer.c_str(), config.mqttPort);

    // Wenn schon verbunden, nichts tun
    if (mqttClient.connected()) return true;

    // --- Serial Debug ---
    Serial.println("=== MQTT Connect Debug ===");
    Serial.print("Server: "); Serial.println(config.mqttServer);
    Serial.print("Port: "); Serial.println(config.mqttPort);
    
    String safePassword = config.mqttPassword.length() ? "***" : "(leer)";
    Serial.print("ClientID: "); Serial.println(clientId);
    Serial.print("User: "); Serial.println(config.mqttUser.length() ? config.mqttUser : "(leer)");
    Serial.print("Passwort: "); Serial.println(safePassword);
    Serial.println("==========================");

    // Verbindung versuchen
    Serial.print("MQTT verbinde... ");
    if (mqttClient.connect(clientId.c_str(), config.mqttUser.c_str(), config.mqttPassword.c_str())) {
        Serial.println("OK");
        return true;
    } else {
        Serial.print("Fehler, mqttClient.state() = ");
        Serial.println(mqttClient.state());
        return false;
    }
}


void sendMQTTMessage(String payload) {
  if (!config.mqttEnabled) return;

  if (!mqttClient.connected()) {
    if (!connectMQTT()) return;
  }

  mqttClient.publish(config.mqttTopic.c_str(), payload.c_str());
}


// ------------------------------------------------
// ---------------- CONFIG ------------------------
// ------------------------------------------------
void saveConfig() {
  DynamicJsonDocument doc(1024);

  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["command"] = config.command;
  doc["localIP"] = config.localIP;
  doc["gateway"] = config.gateway;
  doc["buzzerEnabled"] = config.buzzerEnabled;
  doc["mqttEnabled"] = config.mqttEnabled;
  doc["mqttServer"] = config.mqttServer;
  doc["mqttUser"] = config.mqttUser;
  doc["mqttPassword"] = config.mqttPassword;
  doc["mqttPort"] = config.mqttPort;
  doc["mqttTopic"] = config.mqttTopic;
  doc["mqttPayloadSuccess"] = config.mqttPayloadSuccess;
   File file = LittleFS.open("/config.json", "w");
  serializeJson(doc, file);
  file.close();
}

bool loadConfig() {
  if (!LittleFS.exists("/config.json")) return false;

  File file = LittleFS.open("/config.json", "r");
  DynamicJsonDocument doc(1024);

  if (deserializeJson(doc, file)) {
    file.close();
    return false;
  }

  config.ssid = doc["ssid"] | "";
  config.password = doc["password"] | "";
  config.command = doc["command"] | "";
  config.localIP = doc["localIP"] | "";
  config.gateway = doc["gateway"] | "";
  config.buzzerEnabled = doc["buzzerEnabled"] | true;
  config.mqttEnabled = doc["mqttEnabled"] | false;
  config.mqttServer = doc["mqttServer"] | "";
  config.mqttUser = doc["mqttUser"] | "";
  config.mqttPassword = doc["mqttPassword"] | "";
  config.mqttPort = doc["mqttPort"] | 1883;
  config.mqttTopic = doc["mqttTopic"] | "";
  config.mqttPayloadSuccess = doc["mqttPayloadSuccess"] | "success";
  file.close();
  return true;
}

// ---------- NEU: IP Erreichbarkeit prüfen (einfach HTTP Connect) ----------
bool isIPReachable(String url) {
  HTTPClient http;
  http.setConnectTimeout(500);
  if (!http.begin(espClient, url)) return false;
  int code = http.GET();
  http.end();
  return (code > 0);
}

// ---------- WLAN Logik ----------
bool connectWiFi() {
  if (config.ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);

  // Statische IP nur setzen, wenn beide Werte vorhanden
  if (config.localIP.length() > 0 && config.gateway.length() > 0) {
    IPAddress local, gw, sn(255, 255, 255, 0);
    if (local.fromString(config.localIP) && gw.fromString(config.gateway)) {
      WiFi.config(local, gw, sn);
      Serial.println("Statische IP gesetzt: " + local.toString());
    } else {
      Serial.println("Ungültige IP/Gateway, DHCP wird verwendet");
    }
  } else {
    Serial.println("DHCP wird verwendet");
  }

  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  Serial.print("Verbinde mit WLAN");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {  // 15s Timeout
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WLAN Verbindung fehlgeschlagen");
    return false;
  }

  Serial.println("✅ WLAN verbunden, IP: " + WiFi.localIP().toString());
  return true;
}

// ------------------------------------------------
// ---------------- HTTP --------------------------
// ------------------------------------------------
bool sendHttp() {

  HTTPClient http;
  http.setConnectTimeout(500);

  if (!http.begin(espClient, config.command)) return false;

  int httpCode = http.GET();
  http.end();

  if (httpCode == 200) {
    beepAsync(1);
    return true;
  } else {
    beepAsync(2);
    return false;
  }
}

bool sendMqtt() {
  if (!config.mqttEnabled) return false;

  if (!mqttClient.connected()) {
    if (!connectMQTT()) {
      beepAsync(2);   // Fehler
      return false;
    }
  }

  if (mqttClient.publish(config.mqttTopic.c_str(),
                         config.mqttPayloadSuccess.c_str())) {
    beepAsync(1);     // Erfolg
    return true;
  } else {
    beepAsync(2);     // Fehler
    return false;
  }
}


void handleSave() {
  config.ssid = server.arg("ssid");

  if (server.arg("password").length() > 0)
    config.password = server.arg("password");

  config.localIP = server.arg("localIP");   // neu
  config.gateway = server.arg("gateway");   // neu
  config.command = server.arg("command");

  config.buzzerEnabled = server.hasArg("buzzerEnabled");
  config.mqttEnabled = server.hasArg("mqttEnabled");

  config.mqttServer = server.arg("mqttServer");
  config.mqttUser = server.arg("mqttUser");

  if (server.arg("mqttPassword").length() > 0)
    config.mqttPassword = server.arg("mqttPassword");

  config.mqttPort = server.arg("mqttPort").toInt();
  config.mqttTopic = server.arg("mqttTopic");
  config.mqttPayloadSuccess = server.arg("mqttPayloadSuccess");

  saveConfig();

  if (!config.mqttEnabled) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
      Serial.println("MQTT deaktiviert – Verbindung getrennt");
    }
  }

  server.send(200,"text/html","Gespeichert. Neustart...");
  delay(1500);
  ESP.restart();
}


// ---------------- WebUI -----------------
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='de'><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>BuzzerConfig</title>"
                "<style>"
                "body{font-family:sans-serif;background:#f0f4ff;text-align:center;padding:10px;margin:0;}"
                ".card{max-width:420px;width:95%;margin:20px auto;background:white;padding:20px;"
                "border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,0.15);}"
                ".header-block{background:#e6f0ff;padding:15px 20px;margin-bottom:15px;"
                "border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.1); text-align:center;}"
                ".header-block h2, .header-block p{margin:0;}"
                ".header-block h2{font-size:26px;color:#0078ff;}"
                ".header-block p{margin-top:4px;font-size:12px;color:#555;}"
                "form{display:flex;flex-direction:column;align-items:center;}"
                "form label{width:100%;text-align:center;margin-top:10px;font-weight:500;}"
                "form input{width:90%;padding:10px;margin:5px 0;border-radius:8px;border:1px solid #ccc; text-align:center;}"
                "button{border:none;padding:10px 0;border-radius:10px;font-size:16px;width:95%;margin:10px 0;cursor:pointer;transition:0.2s;}"
                "button.primary{background:#0078ff;color:white;}"
                "button.orange{background:#f4a261;color:white;}"
                "button.gray{background:#e5e7eb;color:#333;}"
                "button.red{background:#e53935;color:white;}"
                "button:hover{opacity:0.9;}"
                "a button{width:95%;margin:5px 0;}"
                "@media(max-width:480px){.header-block h2{font-size:22px;} form input{width:95%;} button{width:100%;}}"
                "</style></head><body>"
                "<div class='card'>"
                "<div class='header-block'>"
                "<h2>BuzzerConfig</h2>"
                "<p>Firmware: " FIRMWARE_VERSION "</p>"
                "</div>"
                "<div style='height:10px;'></div>"
                "<form action='/save' method='POST'>"
                "<label>WLAN-SSID:</label><input name='ssid' value='" + config.ssid + "'>"
                "<label>WLAN-Passwort:</label><input type='password' name='password' placeholder='********'>"
                "<label>Befehl:</label><input name='command' value='" + config.command + "'>"
                "<label>Statische IP:</label><input name='localIP' value='" + (config.localIP.length() ? config.localIP : "") + "' placeholder='192.168.1.50'>"
                "<label>Gateway:</label><input name='gateway' value='" + (config.gateway.length() ? config.gateway : "") + "' placeholder='192.168.1.1'>"
                "<input type='checkbox' name='buzzerEnabled' " + String(config.buzzerEnabled ? "checked" : "") + "> Buzzer aktiv"
                "<input type='checkbox' name='mqttEnabled' " + String(config.mqttEnabled ? "checked" : "") + "> MQTT aktiv<br><br>"
                "<label>MQTT Server:</label><input name='mqttServer' value='" + config.mqttServer + "'>"
                "<label>MQTT Port:</label><input name='mqttPort' value='" + String(config.mqttPort) + "'>"
                "<label>MQTT User:</label><input name='mqttUser' value='" + config.mqttUser + "'>"
                "<label>MQTT Passwort:</label><input type='password' name='mqttPassword' placeholder='********'>"
                "<label>MQTT Topic:</label><input name='mqttTopic' value='" + config.mqttTopic + "'>"
                "<label>MQTT Nachricht:</label><input name='mqttPayloadSuccess' value='" + config.mqttPayloadSuccess + "'>"
                "<button type='submit' class='primary'>💾 speichern</button>"
                "</form>"
                "<a href='/trigger'><button class='orange'>▶️ Befehl testen</button></a>"
                "<a href='/update'><button class='gray'>🔄 Firmware aktualisieren</button></a>"
                "<a href='/upload'><button class='gray'>⬆ Konfiguration hochladen</button></a>"
                "</div></body></html>";

  // Einfaches server.send(), kein sendContent, kein CONTENT_LENGTH_UNKNOWN
  server.send(200, "text/html", html);
}

// ----------------- Upload Handler ----------------
void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.print("Upload Start: "); Serial.println(upload.filename);
        // Nur config.json zulassen
        if(upload.filename != "config.json"){
            Serial.println("Nur config.json erlaubt!");
            return;
        }
        File fsUploadFile = LittleFS.open("/config.json", "w");
        if(fsUploadFile) fsUploadFile.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File fsUploadFile = LittleFS.open("/config.json", "a");
        if (fsUploadFile) {
            fsUploadFile.write(upload.buf, upload.currentSize);
            fsUploadFile.close();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.print("Upload fertig: "); Serial.println(upload.filename);
        loadConfig(); // Config direkt nach Upload laden
    }
}

void handleTrigger() {
 
  bool httpOK = sendHttp();
  if (!httpOK) {
    beepAsync(2);
  }


  if (mqttok && config.mqttEnabled) {
    bool oldLedState = digitalRead(LED_PIN);
    bool mqttOK = sendMqtt();
    if (mqttOK) {
      blinkLED(1);   // Erfolg → 1x
    } else {
      //blinkLED(2);   // Fehler → 2x
    }
    digitalWrite(LED_PIN, oldLedState);
  }



  server.sendHeader("Location", "/");
  server.send(303);
}




// ---------------- WebUI Setup -----------------
void setupWebUI() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);

    // ---------------- Upload Form (GET) ----------------
    server.on("/upload", HTTP_GET, []() {
        server.send(200, "text/html",
            "<h3>config.json Upload</h3>"
            "<form method='POST' action='/upload' enctype='multipart/form-data'>"
            "<input type='file' name='config'>"
            "<input type='submit' value='Hochladen'>"
            "</form>"
        );
    });

// ---------------- Upload Verarbeiten (POST) ----------------
server.on("/upload", HTTP_POST, []() {
    // Nach Abschluss des Uploads entscheiden wir, ob Neustart nötig ist
    if (LittleFS.exists("/config.json")) {  // Beispiel: config hochgeladen
        // Optional: kurze Rückmeldung an Browser vor Neustart
        server.send(200, "text/html", "<html><body>Konfiguration gespeichert. Neustart...</body></html>");
        delay(500);           // kleine Pause, damit Browser Response bekommt
        ESP.restart();        // Neustart initiieren
    } else {
        // Normale Weiterleitung auf Startseite
        server.sendHeader("Location", "/");
        server.send(303);
    }
}, handleFileUpload);

  server.on("/trigger", handleTrigger);

  httpUpdater.setup(&server, "/update");
  server.begin();
  Serial.println("Webserver läuft!");
}


void updateWiFiLED() {
  bool wifiConnected =
    (WiFi.getMode() == WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    // Dauerhaft AN (blau)
    digitalWrite(LED_PIN, LOW);
  } else {
    // Blinken wenn nicht verbunden
    if (millis() - lastLEDToggle >= 500) {
      lastLEDToggle = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }
  }
}


// ---------- Setup ----------
void setup() {
    Serial.begin(9600);

    // --- Pins initialisieren ---
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(SWITCH_PIN, INPUT_PULLUP);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // --- LittleFS mounten ---
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Fehler");
        return;
    }

    // --- Config laden ---
    loadConfig();

    // --- Taster prüfen: AP erzwingen? ---
    bool forceAP = digitalRead(SWITCH_PIN) == LOW;

    // --- WLAN verbinden ---
    bool wifiOK = !forceAP && connectWiFi();

    if (!wifiOK || forceAP) {
        // AP-Modus starten
        Serial.println("➡️ AP-Modus aktiv");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("Buzzer", "12345678");
        inAPMode = true;
    } else {
        inAPMode = false;
        Serial.println("✅ WLAN verbunden, IP: " + WiFi.localIP().toString());

        // --- MQTT initialisieren ---
        mqttClient.setServer(config.mqttServer.c_str(), config.mqttPort);
        mqttClient.setSocketTimeout(2);  // 2 Sekunden
        mqttClient.setKeepAlive(10);     // optional

        // --- MQTT-Broker testen ---
        WiFiClient test;
        Serial.print("Teste MQTT-Broker...");
        if (test.connect(config.mqttServer.c_str(), config.mqttPort)) {
            mqttok = true;
            //blinkLED(1);   // Erfolg → 1x
            Serial.println("OK, Broker erreichbar");
        } else {
            mqttok = false;
            //blinkLED(2);   // Fehler → 2x
            Serial.println("NICHT erreichbar");
        }
    }

    // --- WebUI starten ---
    setupWebUI();
}


// ---------- Loop ----------
void loop() {
  server.handleClient();
  // --- WLAN-LEDs aktualisieren ---
  updateWiFiLED();

  beepAsync(0);

  if (config.mqttEnabled)
    mqttClient.loop();

  // --- Taster Logik ---
  bool currentState = digitalRead(SWITCH_PIN);

  if (lastSwitchState == HIGH && currentState == LOW) {
    if (millis() > switchLockUntil && WiFi.status() == WL_CONNECTED) {
      switchLockUntil = millis() + SWITCH_LOCK_TIME;
      handleTrigger();
    }
  }
//digitalWrite(BUZZER_PIN, LOW);

  lastSwitchState = currentState;
}
