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

// Configuración EEPROM
#define EEPROM_SIZE 1024
#define SSID_ADDR 0
#define PASS_ADDR 100
#define URL_ADDR 200
#define TOKEN_ADDR 350
#define MAX_SSID_LEN 32 
#define MAX_PASS_LEN 64
#define MAX_URL_LEN 128
#define MAX_TOKEN_LEN 512

// Configuración BLE (Usamos los mismos UUIDs que la cámara para compatibilidad)
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
bool relayEncendido = false;
bool simularDatos = false; // Cambiar a false para usar los sensores reales

const int RELAY_PIN = 26;
const int ACS712_PIN = 34;
const int STATUS_LED_PIN = 2;
const bool RELAY_ACTIVE_LOW = false;
const float VOLTAJE_RED = 127.0f;
const float SENSIBILIDAD_ACS712 = 0.185f;
const unsigned long TELEMETRY_INTERVAL_MS = 30000;

float baseAdc = 0;
float energiaWh = 0;
unsigned long lastEnergySampleMs = 0;
unsigned long lastTelemetryMs = 0;
bool sensorCalibrado = false;

String getBluetoothMac() {
    uint8_t btMac[6];
    esp_read_mac(btMac, ESP_MAC_BT);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    return String(macStr);
}

String buildStatusPayload() {
    float corriente = getCorriente();
    float potencia = getPotencia(corriente);

    String payload = "{\"event\":\"state\",\"value\":";
    payload += relayEncendido ? "true" : "false";
    payload += ",\"estado\":\"";
    payload += relayEncendido ? "ON" : "OFF";
    payload += "\",\"corriente\":";
    payload += String(corriente, 3);
    payload += ",\"potencia\":";
    payload += String(potencia, 2);
    payload += ",\"energia\":";
    payload += String(energiaWh, 3);
    payload += "}";
    return payload;
}

String buildTelemetryPayload() {
    float corriente = getCorriente();
    float potencia = getPotencia(corriente);

    String payload = "{\"event\":\"telemetry\",\"corriente\":";
    payload += String(corriente, 3);
    payload += ",\"potencia\":";
    payload += String(potencia, 2);
    payload += ",\"energia\":";
    payload += String(energiaWh, 3);
    payload += ",\"estado\":";
    payload += relayEncendido ? "true" : "false";
    payload += "}";
    return payload;
}

void calibrarACS712() {
    if (simularDatos) {
        sensorCalibrado = true;
        Serial.println("[Meter] ACS712 simulación calibrada.");
        return;
    }

    long suma = 0;

    for (int i = 0; i < 1000; i++) {
        suma += analogRead(ACS712_PIN);
        delay(2);
    }

    baseAdc = suma / 1000.0f;
    sensorCalibrado = true;
    Serial.printf("[Meter] ACS712 calibrado. Base ADC: %.2f\n", baseAdc);
}

float simularInformacion() {
    if (!simularDatos) return 0.0f;
    
    if (relayEncendido) {
        // Simulamos una corriente fluctuante alrededor de 1.3A para que la potencia varíe
        return 1.3f + (random(-10, 10) / 100.0f);
    } else {
        // Cuando está apagado mandamos un valor muy bajito para que veas que la simulación está activa
        return 0.08f + (random(-2, 2) / 100.0f);
    }
}

float getCorriente() {
    if (simularDatos) {
        return simularInformacion();
    }

    if (!sensorCalibrado) {
        return 0.0f;
    }

    int adc = analogRead(ACS712_PIN);
    float voltaje = (adc / 4095.0f) * 3.3f;
    float voltajeBase = (baseAdc / 4095.0f) * 3.3f;
    float corriente = (voltajeBase - voltaje) / SENSIBILIDAD_ACS712;
    corriente = abs(corriente);

    if (corriente < 0.05f || !relayEncendido) {
        corriente = 0.0f;
    }

    return corriente;
}

float getPotencia(float corriente) {
    return VOLTAJE_RED * corriente;
}

void actualizarEnergia(float potencia) {
    unsigned long now = millis();

    if (lastEnergySampleMs == 0) {
        lastEnergySampleMs = now;
        return;
    }

    float horas = (now - lastEnergySampleMs) / 3600000.0f;

    if (relayEncendido) {
        energiaWh += potencia * horas;
    }

    lastEnergySampleMs = now;
}

void sendTelemetry() {
    if (WiFi.status() == WL_CONNECTED && webSocket.isConnected()) {
        String payload = buildTelemetryPayload();
        Serial.println("[WebSocket] Enviando telemetría: " + payload);
        webSocket.sendTXT(payload);
    }
}

void updateMetering() {
    float corriente = getCorriente();
    float potencia = getPotencia(corriente);
    actualizarEnergia(potencia);
}

String urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String encoded = "";
    for (size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        bool safe =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';

        if (safe) {
            encoded += c;
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

void sendCurrentState() {
    if (WiFi.status() == WL_CONNECTED) {
        String payload = buildStatusPayload();
        Serial.println("[WebSocket] Enviando estado: " + payload);
        webSocket.sendTXT(payload);
    }
}

void setPowerState(bool encendido) {
    relayEncendido = encendido;
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? !encendido : encendido);
    digitalWrite(STATUS_LED_PIN, encendido ? HIGH : LOW);
    Serial.println(encendido ? "--- COMANDO: ENCENDER ---" : "--- COMANDO: APAGAR ---");
    sendCurrentState();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Desconectado del servidor WebSocket");
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Conectado a la url: %s\n", payload);
            sendCurrentState();
            break;
        case WStype_TEXT: {
            String text = String((char*)payload);
            text.trim();
            Serial.printf("[WSc] Comando recibido: '%s'\n", text.c_str());
            
            if (text == "ON" || text == "LED_ON" || text == "FAN_ON") {
                setPowerState(true);
            } else if (text == "OFF" || text == "LED_OFF" || text == "FAN_OFF") {
                setPowerState(false);
            } else {
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

void saveWiFiCredentials(String ssid, String password, String url, String token) {
    Serial.println("Guardando credenciales WiFi, URL y Token...");
    for (int i = SSID_ADDR; i < SSID_ADDR + MAX_SSID_LEN; i++) EEPROM.write(i, 0);
    for (int i = PASS_ADDR; i < PASS_ADDR + MAX_PASS_LEN; i++) EEPROM.write(i, 0);
    for (int i = URL_ADDR; i < URL_ADDR + MAX_URL_LEN; i++) EEPROM.write(i, 0);
    for (int i = TOKEN_ADDR; i < TOKEN_ADDR + MAX_TOKEN_LEN; i++) EEPROM.write(i, 0);
    
    for (int i = 0; i < ssid.length() && i < MAX_SSID_LEN; i++) EEPROM.write(SSID_ADDR + i, ssid[i]);
    for (int i = 0; i < password.length() && i < MAX_PASS_LEN; i++) EEPROM.write(PASS_ADDR + i, password[i]);
    for (int i = 0; i < url.length() && i < MAX_URL_LEN; i++) EEPROM.write(URL_ADDR + i, url[i]);
    for (int i = 0; i < token.length() && i < MAX_TOKEN_LEN; i++) EEPROM.write(TOKEN_ADDR + i, token[i]);
    
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
    BLEDevice::init("ESP32_Socket");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pWifiCharacteristic = pService->createCharacteristic(
        WIFI_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pWifiCharacteristic->setCallbacks(new WifiConfigCallback());
    
    pIpCharacteristic = pService->createCharacteristic(
        IP_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pIpCharacteristic->addDescriptor(new BLE2902());
    
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE iniciado (Socket ESP32), esperando conexiones...");
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(ACS712_PIN, INPUT);
    setPowerState(false);
    Serial.println();
    Serial.println("=== Iniciando Socket ESP32 (Manordomo) ===");

    if (!EEPROM.begin(EEPROM_SIZE)) {
        Serial.println("Error al inicializar EEPROM");
    }

    // Descomentar estas lineas para borrar la memoria
    //for (int i = 0; i < 512; i++) EEPROM.write(i, 0);
    //EEPROM.commit();

    loadWiFiCredentials();

    if (currentSSID.length() > 0) {
        currentSSID.trim();
        currentPassword.trim();
        
        Serial.println("Conectando a Wi-Fi: [" + currentSSID + "]");
        
        Serial.println("[DEBUG] Iniciando stack de Wi-Fi...");
        WiFi.mode(WIFI_STA);
        delay(100);
        
        Serial.println("[DEBUG] Ejecutando WiFi.begin()...");
        WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
        
        Serial.println("[DEBUG] WiFi.begin() completado. Desactivando Sleep...");
        WiFi.setSleep(false);
        
        Serial.println("[DEBUG] Entrando al bucle de espera...");
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            esp32IP = WiFi.localIP().toString();
            Serial.println("WiFi conectado");

            Serial.println("Iniciando BLE para permitir configuración mientras está conectado a WiFi...");
            initBLE();

            calibrarACS712();

            // Intentar conectar al WebSocket
            if (currentBackendUrl.length() > 0) {
                String macAddress = getBluetoothMac();
                
                String wsPath = "/ws?deviceKey=" + urlEncode(macAddress);
                if (currentToken.length() > 0) {
                    wsPath += "&token=" + urlEncode(currentToken);
                }
                Serial.println("[DEBUG] MAC BT Leída: " + macAddress);
                Serial.println("[DEBUG] Intentando WebSocket WS Path: " + wsPath);

                
                bool isWss = currentBackendUrl.startsWith("wss://");
                String host = currentBackendUrl;
                if (isWss) host.replace("wss://", "");
                else host.replace("ws://", "");
                
                int slashIndex = host.indexOf('/');
                if (slashIndex != -1) host = host.substring(0, slashIndex);
                
                int port = isWss ? 443 : 80;
                int colonIndex = host.indexOf(':');
                if (colonIndex != -1) {
                    port = host.substring(colonIndex + 1).toInt();
                    host = host.substring(0, colonIndex);
                }

                Serial.println("[DEBUG] WS Host: " + host);
                Serial.println("[DEBUG] WS Port: " + String(port));
                Serial.println("[DEBUG] WS SSL: " + String(isWss ? "true" : "false"));
                
                if (isWss) {
                    webSocket.beginSSL(host, port, wsPath);
                } else {
                    webSocket.begin(host, port, wsPath);
                }
                webSocket.onEvent(webSocketEvent);
                webSocket.setReconnectInterval(5000);
                // Aumentamos los tiempos del heartbeat para evitar desconexiones por latencia
                webSocket.enableHeartbeat(15000, 10000, 2);
            }
                        
        } else {
            Serial.println("Error al conectar a WiFi. Apagando antena Wi-Fi...");
            
            // Apagamos la antena WiFi para liberar toda su memoria
            WiFi.disconnect(true, true);
            WiFi.mode(WIFI_OFF);
            delay(500);
            
            Serial.println("Iniciando BLE de rescate...");
            initBLE();
        }
    } else {
        Serial.println("No hay configuración Wi-Fi guardada.");
        Serial.println("Iniciando BLE para configuración inicial...");
        initBLE();
    }
}

void loop() {
    if (shouldRestart) {
        delay(1000);
        ESP.restart();
    }

    static unsigned long lastUpdate = 0;
    static unsigned long lastMeterTick = 0;
    if (WiFi.status() == WL_CONNECTED) {
        if (millis() - lastMeterTick >= 1000) {
            updateMetering();
            lastMeterTick = millis();
        }

        if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
            sendTelemetry();
            lastTelemetryMs = millis();
        }

        if (millis() - lastUpdate > 5000) {
            esp32IP = WiFi.localIP().toString();
            if (deviceConnected && pIpCharacteristic != nullptr) {
                String estadoBle = esp32IP + "|" + getBluetoothMac();
                pIpCharacteristic->setValue(estadoBle.c_str());
                pIpCharacteristic->notify();
            }
            lastUpdate = millis();
        }
        webSocket.loop();
    }
    delay(10);
}
