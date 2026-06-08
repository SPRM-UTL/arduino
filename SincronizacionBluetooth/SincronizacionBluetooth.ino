#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// Configuración EEPROM
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 100
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

// Configuración BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define WIFI_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define IP_CHAR_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// Variables globales
WebServer server(80);
BLECharacteristic *pWifiCharacteristic = nullptr;
BLECharacteristic *pIpCharacteristic = nullptr;
bool deviceConnected = false;
String currentSSID = "";
String currentPassword = "";
String esp32IP = "";

// Pines LED indicador
#define LED_CONFIG_PIN 2
#define LED_CONNECTED_PIN 4

// Declaraciones adelantadas de funciones
void saveWiFiCredentials(String ssid, String password);
void connectToWiFi(String ssid, String password);
void initBLE();
void loadWiFiCredentials();
void startWebServer();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(LED_CONFIG_PIN, HIGH);
        Serial.println("Dispositivo conectado vía BLE");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(LED_CONFIG_PIN, LOW);
        Serial.println("Dispositivo desconectado");
        // Reiniciar advertising
        BLEDevice::startAdvertising();
    }
};

class WifiConfigCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        // Obtener el valor como String de Arduino directamente
        String value = pCharacteristic->getValue().c_str();
        
        if (value.length() > 0) {
            Serial.println("Datos WiFi recibidos: " + value);
            
            // Formato esperado: "SSID|PASSWORD"
            int separator = value.indexOf('|');
            if (separator != -1) {
                String ssid = value.substring(0, separator);
                String password = value.substring(separator + 1);
                
                Serial.println("SSID: " + ssid);
                Serial.println("Password: " + password);
                
                saveWiFiCredentials(ssid, password);
                connectToWiFi(ssid, password);
            } else {
                Serial.println("Formato incorrecto. Use SSID|PASSWORD");
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nIniciando ESP32 Configurador WiFi...");
    
    // Configurar pines LED
    pinMode(LED_CONFIG_PIN, OUTPUT);
    pinMode(LED_CONNECTED_PIN, OUTPUT);
    digitalWrite(LED_CONFIG_PIN, LOW);
    digitalWrite(LED_CONNECTED_PIN, LOW);
    
    // Inicializar EEPROM
    if (!EEPROM.begin(EEPROM_SIZE)) {
        Serial.println("Error al inicializar EEPROM");
    }
    
    // Cargar credenciales guardadas
    loadWiFiCredentials();
    
    // Inicializar BLE
    initBLE();
    
    // Intentar conectar con credenciales guardadas
    if (currentSSID.length() > 0) {
        Serial.println("Intentando conectar con credenciales guardadas...");
        connectToWiFi(currentSSID, currentPassword);
    } else {
        Serial.println("No hay credenciales guardadas. Esperando configuración BLE...");
    }
}

void loop() {
    server.handleClient();
    
    // Actualizar IP cada 5 segundos si está conectado
    static unsigned long lastUpdate = 0;
    if (WiFi.status() == WL_CONNECTED && millis() - lastUpdate > 5000) {
        esp32IP = WiFi.localIP().toString();
        if (deviceConnected && pIpCharacteristic != nullptr) {
            pIpCharacteristic->setValue(esp32IP.c_str());
            pIpCharacteristic->notify();
            Serial.println("IP actualizada vía BLE: " + esp32IP);
        }
        lastUpdate = millis();
    }
    
    delay(10);
}

void initBLE() {
    BLEDevice::init("ESP32_Config");
    
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    // Característica para recibir configuración WiFi
    pWifiCharacteristic = pService->createCharacteristic(
        WIFI_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pWifiCharacteristic->setCallbacks(new WifiConfigCallback());
    
    // Característica para enviar IP
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
    
    Serial.println("BLE iniciado, esperando conexiones...");
}

void saveWiFiCredentials(String ssid, String password) {
    Serial.println("Guardando credenciales WiFi...");
    
    // Limpiar EEPROM para SSID
    for (int i = SSID_ADDR; i < SSID_ADDR + MAX_SSID_LEN; i++) {
        EEPROM.write(i, 0);
    }
    
    // Limpiar EEPROM para contraseña
    for (int i = PASS_ADDR; i < PASS_ADDR + MAX_PASS_LEN; i++) {
        EEPROM.write(i, 0);
    }
    
    // Guardar SSID
    for (int i = 0; i < ssid.length() && i < MAX_SSID_LEN; i++) {
        EEPROM.write(SSID_ADDR + i, ssid[i]);
    }
    
    // Guardar contraseña
    for (int i = 0; i < password.length() && i < MAX_PASS_LEN; i++) {
        EEPROM.write(PASS_ADDR + i, password[i]);
    }
    
    if (EEPROM.commit()) {
        Serial.println("Credenciales WiFi guardadas exitosamente");
        currentSSID = ssid;
        currentPassword = password;
    } else {
        Serial.println("Error al guardar credenciales en EEPROM");
    }
}

void loadWiFiCredentials() {
    char ssid[MAX_SSID_LEN + 1] = {0};
    char password[MAX_PASS_LEN + 1] = {0};
    
    // Cargar SSID
    for (int i = 0; i < MAX_SSID_LEN; i++) {
        ssid[i] = EEPROM.read(SSID_ADDR + i);
    }
    ssid[MAX_SSID_LEN] = '\0'; // Asegurar terminación
    
    // Cargar contraseña
    for (int i = 0; i < MAX_PASS_LEN; i++) {
        password[i] = EEPROM.read(PASS_ADDR + i);
    }
    password[MAX_PASS_LEN] = '\0'; // Asegurar terminación
    
    currentSSID = String(ssid);
    currentPassword = String(password);
    
    if (currentSSID.length() > 0) {
        Serial.println("Credenciales WiFi cargadas: " + currentSSID);
    } else {
        Serial.println("No se encontraron credenciales guardadas");
    }
}

void connectToWiFi(String ssid, String password) {
    Serial.println("Conectando a WiFi...");
    Serial.println("SSID: " + ssid);
    
    WiFi.disconnect(true); // Desconectar de cualquier red anterior
    delay(1000);
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Intentar conectar por 20 segundos
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
        digitalWrite(LED_CONNECTED_PIN, !digitalRead(LED_CONNECTED_PIN));
    }
    
    Serial.println(); // Nueva línea después de los puntos
    
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(LED_CONNECTED_PIN, HIGH);
        esp32IP = WiFi.localIP().toString();
        Serial.println("¡Conectado a WiFi exitosamente!");
        Serial.println("IP: " + esp32IP);
        Serial.print("Señal RSSI: ");
        Serial.println(WiFi.RSSI());
        
        // Iniciar servidor web
        startWebServer();
        
        // Actualizar IP via BLE si está conectado
        if (deviceConnected && pIpCharacteristic != nullptr) {
            pIpCharacteristic->setValue(esp32IP.c_str());
            pIpCharacteristic->notify();
            Serial.println("IP enviada vía BLE: " + esp32IP);
        }
    } else {
        digitalWrite(LED_CONNECTED_PIN, LOW);
        Serial.println("Error al conectar a WiFi");
        Serial.print("Estado WiFi: ");
        Serial.println(WiFi.status());
    }
}

void startWebServer() {
    // Ruta principal - Página de estado
    server.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>";
        html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
        html += "body { font-family: Arial, sans-serif; background: #f0f2f5; min-height: 100vh; display: flex; align-items: center; justify-content: center; }";
        html += ".container { background: white; border-radius: 15px; padding: 30px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); max-width: 400px; width: 90%; }";
        html += "h2 { color: #1a73e8; text-align: center; margin-bottom: 20px; }";
        html += ".status-card { background: #e8f0fe; border-radius: 10px; padding: 15px; margin-bottom: 20px; }";
        html += ".status-item { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #d2e3fc; }";
        html += ".status-item:last-child { border-bottom: none; }";
        html += ".label { color: #5f6368; }";
        html += ".value { color: #1a73e8; font-weight: bold; }";
        html += "h3 { color: #333; margin-bottom: 15px; }";
        html += ".form-group { margin-bottom: 15px; }";
        html += "input { width: 100%; padding: 12px; border: 2px solid #e0e0e0; border-radius: 8px; font-size: 16px; transition: border-color 0.3s; }";
        html += "input:focus { outline: none; border-color: #1a73e8; }";
        html += "button { width: 100%; padding: 12px; background: #1a73e8; color: white; border: none; border-radius: 8px; font-size: 16px; cursor: pointer; transition: background 0.3s; }";
        html += "button:hover { background: #1557b0; }";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<h2>⚙️ ESP32 Configurador</h2>";
        html += "<div class='status-card'>";
        html += "<div class='status-item'><span class='label'>Estado:</span><span class='value'>✅ Conectado</span></div>";
        html += "<div class='status-item'><span class='label'>IP:</span><span class='value'>" + esp32IP + "</span></div>";
        html += "<div class='status-item'><span class='label'>SSID:</span><span class='value'>" + currentSSID + "</span></div>";
        html += "<div class='status-item'><span class='label'>Señal:</span><span class='value'>" + String(WiFi.RSSI()) + " dBm</span></div>";
        html += "</div>";
        html += "<h3>Cambiar Red WiFi</h3>";
        html += "<form action='/wifi' method='POST'>";
        html += "<div class='form-group'><input type='text' name='ssid' placeholder='Nombre de red (SSID)' required></div>";
        html += "<div class='form-group'><input type='password' name='password' placeholder='Contraseña'></div>";
        html += "<button type='submit'>Conectar</button>";
        html += "</form>";
        html += "</div></body></html>";
        
        server.send(200, "text/html", html);
    });
    
    // Ruta para configurar WiFi
    server.on("/wifi", HTTP_POST, []() {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        
        if (newSSID.length() > 0) {
            saveWiFiCredentials(newSSID, newPassword);
            
            String response = "<!DOCTYPE html><html><head>";
            response += "<meta charset='UTF-8'>";
            response += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            response += "<style>";
            response += "body { font-family: Arial, sans-serif; background: #f0f2f5; min-height: 100vh; display: flex; align-items: center; justify-content: center; }";
            response += ".container { background: white; border-radius: 15px; padding: 30px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); text-align: center; }";
            response += ".success { color: #4CAF50; font-size: 48px; margin-bottom: 20px; }";
            response += "h3 { color: #333; margin-bottom: 10px; }";
            response += "p { color: #666; }";
            response += "</style>";
            response += "<meta http-equiv='refresh' content='3;url=/'>";
            response += "</head><body>";
            response += "<div class='container'>";
            response += "<div class='success'>✓</div>";
            response += "<h3>Configuración Guardada</h3>";
            response += "<p>Reconectando a " + newSSID + "...</p>";
            response += "</div></body></html>";
            
            server.send(200, "text/html", response);
            
            delay(1000);
            ESP.restart();
        } else {
            server.send(400, "text/plain", "Error: SSID requerido");
        }
    });
    
    // Ruta para obtener IP en formato JSON (útil para apps)
    server.on("/api/info", HTTP_GET, []() {
        String json = "{";
        json += "\"ip\":\"" + esp32IP + "\",";
        json += "\"ssid\":\"" + currentSSID + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"status\":\"connected\"";
        json += "}";
        server.send(200, "application/json", json);
    });
    
    server.begin();
    Serial.println("Servidor web iniciado en puerto 80");
    Serial.println("Accede a: http://" + esp32IP);
}