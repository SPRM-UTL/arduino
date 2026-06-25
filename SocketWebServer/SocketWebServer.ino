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
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 100
#define URL_ADDR 200
#define MAX_SSID_LEN 32 
#define MAX_PASS_LEN 64
#define MAX_URL_LEN 128

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
String esp32IP = "";
bool shouldRestart = false;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Desconectado del servidor WebSocket");
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Conectado a la url: %s\n", payload);
            break;
        case WStype_TEXT: {
            String text = String((char*)payload);
            Serial.printf("[WSc] Mensaje recibido: %s\n", text.c_str());
            
            // Simulación del Relé
            if (text == "ON") {
                Serial.println("=========================================");
                Serial.println(">> COMANDO RECIBIDO: ENCHUFAR/ENCENDER <<");
                Serial.println("=========================================");
            } else if (text == "OFF") {
                Serial.println("=========================================");
                Serial.println(">> COMANDO RECIBIDO: DESENCHUFAR/APAGAR<<");
                Serial.println("=========================================");
            }
            break;
        }
    }
}

void saveWiFiCredentials(String ssid, String password, String url) {
    Serial.println("Guardando credenciales WiFi y URL...");
    for (int i = SSID_ADDR; i < SSID_ADDR + MAX_SSID_LEN; i++) EEPROM.write(i, 0);
    for (int i = PASS_ADDR; i < PASS_ADDR + MAX_PASS_LEN; i++) EEPROM.write(i, 0);
    for (int i = URL_ADDR; i < URL_ADDR + MAX_URL_LEN; i++) EEPROM.write(i, 0);
    
    for (int i = 0; i < ssid.length() && i < MAX_SSID_LEN; i++) EEPROM.write(SSID_ADDR + i, ssid[i]);
    for (int i = 0; i < password.length() && i < MAX_PASS_LEN; i++) EEPROM.write(PASS_ADDR + i, password[i]);
    for (int i = 0; i < url.length() && i < MAX_URL_LEN; i++) EEPROM.write(URL_ADDR + i, url[i]);
    
    if (EEPROM.commit()) {
        Serial.println("Credenciales y URL guardadas exitosamente");
        currentSSID = ssid;
        currentPassword = password;
        currentBackendUrl = url;
    } else {
        Serial.println("Error al guardar en EEPROM");
    }
}

void loadWiFiCredentials() {
    char ssid[MAX_SSID_LEN + 1] = {0};
    char password[MAX_PASS_LEN + 1] = {0};
    char url[MAX_URL_LEN + 1] = {0};
    
    for (int i = 0; i < MAX_SSID_LEN; i++) ssid[i] = EEPROM.read(SSID_ADDR + i);
    ssid[MAX_SSID_LEN] = '\0';
    for (int i = 0; i < MAX_PASS_LEN; i++) password[i] = EEPROM.read(PASS_ADDR + i);
    password[MAX_PASS_LEN] = '\0';
    for (int i = 0; i < MAX_URL_LEN; i++) url[i] = EEPROM.read(URL_ADDR + i);
    url[MAX_URL_LEN] = '\0';
    
    currentSSID = String(ssid);
    currentPassword = String(password);
    currentBackendUrl = String(url);
    
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
            if (firstSep != -1 && secondSep != -1) {
                String ssid = value.substring(0, firstSep);
                String password = value.substring(firstSep + 1, secondSep);
                String url = value.substring(secondSep + 1);
                saveWiFiCredentials(ssid, password, url);
                
                Serial.println("Configuración recibida, reiniciando pronto...");
                shouldRestart = true;
            } else {
                Serial.println("Formato incorrecto. Use SSID|PASSWORD|BACKEND_URL");
            }
        }
    }
};

void initBLE() {
    BLEDevice::init("ESP32_Socket_Manordomo");
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
    Serial.println();
    Serial.println("Iniciando Socket ESP32...");

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

            // Intentar conectar al WebSocket
            if (currentBackendUrl.length() > 0) {
                uint8_t btMac[6];
                esp_read_mac(btMac, ESP_MAC_BT);
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
                String macAddress = String(macStr);
                
                String wsPath = "/ws?deviceKey=" + macAddress;
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
                
                if (isWss) {
                    webSocket.beginSSL(host, port, wsPath);
                } else {
                    webSocket.begin(host, port, wsPath);
                }
                webSocket.onEvent(webSocketEvent);
                webSocket.setReconnectInterval(5000);
                webSocket.enableHeartbeat(10000, 3000, 2);
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
    if (WiFi.status() == WL_CONNECTED) {
        if (millis() - lastUpdate > 5000) {
            esp32IP = WiFi.localIP().toString();
            if (deviceConnected && pIpCharacteristic != nullptr) {
                pIpCharacteristic->setValue(esp32IP.c_str());
                pIpCharacteristic->notify();
            }
            lastUpdate = millis();
        }
        webSocket.loop();
    }
    delay(10);
}
