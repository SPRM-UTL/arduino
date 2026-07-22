#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WebSocketsClient.h>

WebSocketsClient webSocket;

const char* rootCACertificate =
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n" \
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n" \
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n" \
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n" \
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n" \
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n" \
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n" \
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n" \
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n" \
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n" \
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n" \
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n" \
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n" \
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n" \
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n" \
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n" \
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n" \
"ORAzI4JMPJ+GslWYHb4phow\n" \
"-----END CERTIFICATE-----\n";

// EEPROM
#define EEPROM_SIZE 1024
#define SSID_ADDR 0
#define PASS_ADDR 100
#define URL_ADDR 200
#define TOKEN_ADDR 350
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_URL_LEN 128
#define MAX_TOKEN_LEN 512

// BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define WIFI_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define IP_CHAR_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26aa"

BLECharacteristic *pWifiCharacteristic = nullptr;
BLECharacteristic *pIpCharacteristic = nullptr;
bool deviceConnected = false;
String currentSSID = "";
String currentPassword = "";
String currentBackendUrl = "";
String currentToken = "";
String esp32IP = "";
bool shouldRestart = false;

// Pins
const int RELE_PINS[4] = {26, 25, 33, 32};
const int ACS712_PIN = 34;
const int STATUS_LED_PIN = 2;
const bool RELAY_ACTIVE_LOW = false;

// Energy monitoring
const float VOLTAJE_RED = 127.0f;
const float SENSIBILIDAD_ACS712 = 0.185f;
float baseAdc = 0;
float energiaWh = 0;
unsigned long lastEnergySampleMs = 0;
unsigned long lastTelemetryMs = 0;
bool sensorCalibrado = false;
const unsigned long TELEMETRY_INTERVAL_MS = 30000;

// State - solo uno encendido a la vez
bool relayEncendido[4] = {false, false, false, false};
int relayActivo = 0; // 0 = ninguno, 1-4 = canal activo

// ─── Bluetooth MAC ───
String getBluetoothMac() {
    uint8_t btMac[6];
    esp_read_mac(btMac, ESP_MAC_BT);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    return String(macStr);
}

// ─── ACS712 ───
void calibrarACS712() {
    long suma = 0;
    for (int i = 0; i < 1000; i++) {
        suma += analogRead(ACS712_PIN);
        delay(2);
    }
    baseAdc = suma / 1000.0f;
    sensorCalibrado = true;
    Serial.printf("[Meter] ACS712 calibrado. Base ADC: %.2f\n", baseAdc);
}

float getCorriente() {
    if (!sensorCalibrado) return 0.0f;
    int adc = analogRead(ACS712_PIN);
    float voltaje = (adc / 4095.0f) * 3.3f;
    float voltajeBase = (baseAdc / 4095.0f) * 3.3f;
    float corriente = abs((voltajeBase - voltaje) / SENSIBILIDAD_ACS712);
    bool anyOn = relayEncendido[0] || relayEncendido[1] || relayEncendido[2] || relayEncendido[3];
    if (corriente < 0.05f || !anyOn) corriente = 0.0f;
    return corriente;
}

float getPotencia(float corriente) {
    return VOLTAJE_RED * corriente;
}

void actualizarEnergia(float potencia) {
    unsigned long now = millis();
    if (lastEnergySampleMs == 0) { lastEnergySampleMs = now; return; }
    float horas = (now - lastEnergySampleMs) / 3600000.0f;
    bool anyOn = relayEncendido[0] || relayEncendido[1] || relayEncendido[2] || relayEncendido[3];
    if (anyOn && potencia > 0) energiaWh += potencia * horas;
    lastEnergySampleMs = now;
}

// ─── Relay Control (exclusivo: solo uno a la vez) ───
void activarCanal(int canal) {
    // Apaga todos
    for (int i = 0; i < 4; i++) {
        digitalWrite(RELE_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW);
        relayEncendido[i] = false;
    }
    relayActivo = 0;

    // Si canal = 0 significa apagar todos
    if (canal == 0 || canal < 1 || canal > 3) {
        digitalWrite(STATUS_LED_PIN, LOW);
        sendCurrentState();
        return;
    }

    // Enciende solo el canal solicitado
    digitalWrite(RELE_PINS[canal - 1], RELAY_ACTIVE_LOW ? LOW : HIGH);
    relayEncendido[canal - 1] = true;
    relayActivo = canal;
    digitalWrite(STATUS_LED_PIN, HIGH);

    Serial.printf("--- COMANDO: ENCENDER CANAL %d ---\n", canal);
    sendCurrentState();
}

// ─── State Reporting ───
String buildStatusPayload() {
    float corriente = getCorriente();
    float potencia = getPotencia(corriente);

    String payload = "{\"event\":\"state\"";
    payload += ",\"estado1\":" + String(relayEncendido[0] ? "true" : "false");
    payload += ",\"estado2\":" + String(relayEncendido[1] ? "true" : "false");
    payload += ",\"estado3\":" + String(relayEncendido[2] ? "true" : "false");
    payload += ",\"corriente\":" + String(corriente, 3);
    payload += ",\"potencia\":" + String(potencia, 2);
    payload += ",\"energia\":" + String(energiaWh, 3);
    payload += "}";
    return payload;
}

void sendCurrentState() {
    if (webSocket.isConnected()) {
        webSocket.sendTXT(buildStatusPayload());
    }
}

// ─── WebSocket Events ───
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Desconectado del servidor WebSocket");
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Conectado a: %s\n", payload);
            sendCurrentState();
            break;
        case WStype_TEXT: {
            String text = String((char*)payload);
            text.trim();
            Serial.printf("[WSc] Comando recibido: '%s'\n", text.c_str());

            // Formato MultiSocket compatible: ON1/OFF1, ON2/OFF2, ON3/OFF3
            if (text == "ON1")      activarCanal(1);
            else if (text == "OFF1") activarCanal(0);
            else if (text == "ON2")  activarCanal(2);
            else if (text == "OFF2") activarCanal(0);
            else if (text == "ON3")  activarCanal(3);
            else if (text == "OFF3") activarCanal(0);
            else if (text == "OFF")  activarCanal(0);
            // Compatibilidad: ON por defecto activa canal 1
            else if (text == "ON")   activarCanal(1);
            else {
                Serial.printf("[WSc] Comando desconocido: '%s'\n", text.c_str());
            }
            break;
        }
        case WStype_ERROR:
            Serial.println("[WSc] Error en WebSocket");
            break;
        default:
            break;
    }
}

// ─── EEPROM ───
void saveWiFiCredentials(String ssid, String password, String url, String token) {
    Serial.println("Guardando credenciales WiFi, URL y Token...");
    for (int i = SSID_ADDR; i < SSID_ADDR + MAX_SSID_LEN; i++) EEPROM.write(i, 0);
    for (int i = PASS_ADDR; i < PASS_ADDR + MAX_PASS_LEN; i++) EEPROM.write(i, 0);
    for (int i = URL_ADDR; i < URL_ADDR + MAX_URL_LEN; i++) EEPROM.write(i, 0);
    for (int i = TOKEN_ADDR; i < TOKEN_ADDR + MAX_TOKEN_LEN; i++) EEPROM.write(i, 0);

    for (unsigned int i = 0; i < ssid.length() && i < MAX_SSID_LEN; i++) EEPROM.write(SSID_ADDR + i, ssid[i]);
    for (unsigned int i = 0; i < password.length() && i < MAX_PASS_LEN; i++) EEPROM.write(PASS_ADDR + i, password[i]);
    for (unsigned int i = 0; i < url.length() && i < MAX_URL_LEN; i++) EEPROM.write(URL_ADDR + i, url[i]);
    for (unsigned int i = 0; i < token.length() && i < MAX_TOKEN_LEN; i++) EEPROM.write(TOKEN_ADDR + i, token[i]);

    if (EEPROM.commit()) {
        Serial.println("Credenciales, URL y Token guardados exitosamente");
        currentSSID = ssid;
        currentPassword = password;
        currentBackendUrl = url;
        currentToken = token;
    } else {
        Serial.println("Error al guardar en EEPROM");
    }
}

void loadWiFiCredentials() {
    char ssid[MAX_SSID_LEN + 1] = {0};
    char password[MAX_PASS_LEN + 1] = {0};
    char url[MAX_URL_LEN + 1] = {0};
    char token[MAX_TOKEN_LEN + 1] = {0};

    for (int i = 0; i < MAX_SSID_LEN; i++) ssid[i] = EEPROM.read(SSID_ADDR + i);
    ssid[MAX_SSID_LEN] = '\0';
    for (int i = 0; i < MAX_PASS_LEN; i++) password[i] = EEPROM.read(PASS_ADDR + i);
    password[MAX_PASS_LEN] = '\0';
    for (int i = 0; i < MAX_URL_LEN; i++) url[i] = EEPROM.read(URL_ADDR + i);
    url[MAX_URL_LEN] = '\0';
    for (int i = 0; i < MAX_TOKEN_LEN; i++) token[i] = EEPROM.read(TOKEN_ADDR + i);
    token[MAX_TOKEN_LEN] = '\0';

    if (ssid[0] == 0 || ssid[0] == '\xFF') {
        currentSSID = "";
        currentPassword = "";
        currentBackendUrl = "";
        currentToken = "";
    } else {
        currentSSID = String(ssid);
        currentPassword = String(password);
        currentBackendUrl = String(url);
        currentToken = String(token);
    }

    if (currentSSID.length() > 0) {
        Serial.println("Credenciales WiFi cargadas: " + currentSSID);
    } else {
        Serial.println("No se encontraron credenciales guardadas");
    }
}

// ─── BLE ───
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Dispositivo conectado vía BLE");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Dispositivo desconectado");
        BLEDevice::startAdvertising();
    }
};

class WifiConfigCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        if (value.length() > 0) {
            Serial.println("Datos de config recibidos: " + value);
            int firstSep = value.indexOf('|');
            int secondSep = value.indexOf('|', firstSep + 1);
            int thirdSep = value.indexOf('|', secondSep + 1);
            if (firstSep != -1 && secondSep != -1 && thirdSep != -1) {
                String ssid = value.substring(0, firstSep);
                String password = value.substring(firstSep + 1, secondSep);
                String url = value.substring(secondSep + 1, thirdSep);
                String token = value.substring(thirdSep + 1);
                saveWiFiCredentials(ssid, password, url, token);
                if (deviceConnected && pIpCharacteristic != nullptr) {
                    String estadoBle = "0.0.0.0|" + getBluetoothMac();
                    pIpCharacteristic->setValue(estadoBle.c_str());
                    pIpCharacteristic->notify();
                }
                Serial.println("Configuración recibida, reiniciando pronto...");
                shouldRestart = true;
            } else {
                Serial.println("Formato incorrecto. Use SSID|PASSWORD|BACKEND_URL|TOKEN");
            }
        }
    }
};

void initBLE() {
    BLEDevice::init("VentiladorInteligente");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pWifiCharacteristic = pService->createCharacteristic(
        WIFI_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE
    );
    pWifiCharacteristic->setCallbacks(new WifiConfigCallback());

    pIpCharacteristic = pService->createCharacteristic(
        IP_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pIpCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE Ventilador Inteligente listo para configuración");
}

// ─── WiFi + WebSocket ───
void connectWiFi() {
    Serial.printf("Conectando a WiFi: %s\n", currentSSID.c_str());
    WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        esp32IP = WiFi.localIP().toString();
        Serial.println("\nWiFi conectado: " + esp32IP);
    } else {
        Serial.println("\nError al conectar WiFi");
    }
}

void connectWebSocket() {
    if (currentBackendUrl.length() == 0) {
        Serial.println("[WSc] No hay URL del backend configurada");
        return;
    }

    String url = currentBackendUrl;
    int port = 443;
    bool useSSL = true;

    if (url.startsWith("https://")) {
        url = url.substring(8);
    } else if (url.startsWith("http://")) {
        url = url.substring(7);
        port = 80;
        useSSL = false;
    }

    int colonPos = url.indexOf(':');
    int slashPos = url.indexOf('/');
    if (colonPos != -1 && (slashPos == -1 || colonPos < slashPos)) {
        port = url.substring(colonPos + 1, slashPos != -1 ? slashPos : url.length()).toInt();
        url = url.substring(0, colonPos);
    }

    String path = "/ws/device";
    if (slashPos != -1) path = url.substring(slashPos);

    Serial.printf("[WSc] Conectando a %s:%d%s (SSL=%d)\n", url.c_str(), port, path.c_str(), useSSL);

    if (useSSL) {
        webSocket.setCACert(rootCACertificate);
    }
    webSocket.begin(url.c_str(), port, path.c_str());
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

// ─── Setup & Loop ───
void setup() {
    Serial.begin(115200);

    for (int i = 0; i < 4; i++) {
        pinMode(RELE_PINS[i], OUTPUT);
        digitalWrite(RELE_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW);
    }
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    pinMode(ACS712_PIN, INPUT);

    EEPROM.begin(EEPROM_SIZE);
    loadWiFiCredentials();

    calibrarACS712();

    if (currentSSID.length() > 0) {
        connectWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            connectWebSocket();
        }
    } else {
        Serial.println("No hay WiFi configurado. Modo AP + BLE.");
    }

    initBLE();
}

void loop() {
    webSocket.loop();

    if (shouldRestart) {
        delay(1000);
        ESP.restart();
    }

    if (WiFi.status() == WL_CONNECTED && currentBackendUrl.length() > 0) {
        webSocket.loop();
    }

    static unsigned long last = 0;
    if (millis() - last >= 1000) {
        last = millis();
        float corriente = getCorriente();
        float potencia = getPotencia(corriente);
        actualizarEnergia(potencia);
    }

    if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMs = millis();
        sendCurrentState();
    }
}
