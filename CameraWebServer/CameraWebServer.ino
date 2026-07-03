#include <Arduino.h>
#include "esp_camera.h"
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

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

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

// Configuración BLE
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

String getBluetoothMac() {
    uint8_t btMac[6];
    esp_read_mac(btMac, ESP_MAC_BT);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
    return String(macStr);
}

String urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String encoded = "";
    for (size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
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

void startCameraServer();
void setupLedFlash();

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Connected to url: %s\n", payload);
            break;
        case WStype_TEXT:
            Serial.printf("[WSc] get text: %s\n", payload);
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
        Serial.println("Credenciales, URL y Token guardadas exitosamente");
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
    
    currentSSID = String(ssid);
    currentPassword = String(password);
    currentBackendUrl = String(url);
    currentToken = String(token);
    
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
            Serial.println("Datos de config recibidos");
            int firstSep = value.indexOf('|');
            int secondSep = value.indexOf('|', firstSep + 1);
            int thirdSep = value.indexOf('|', secondSep + 1);
            
            if (firstSep != -1 && secondSep != -1 && thirdSep != -1) {
                String ssid = value.substring(0, firstSep);
                String password = value.substring(firstSep + 1, secondSep);
                String url = value.substring(secondSep + 1, thirdSep);
                String token = value.substring(thirdSep + 1);
                saveWiFiCredentials(ssid, password, url, token);
                
                Serial.println("Reiniciando ESP32 para aplicar configuración Wi-Fi y Backend...");
                delay(1000);
                ESP.restart();
            } else {
                Serial.println("Formato incorrecto. Use SSID|PASSWORD|BACKEND_URL|TOKEN");
            }
        }
    }
};

void initBLE() {
    BLEDevice::init("ESP32_Cam_Manordomo");
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
    Serial.println("BLE iniciado, esperando conexiones...");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  if (!EEPROM.begin(EEPROM_SIZE)) {
      Serial.println("Error al inicializar EEPROM");
  }
  loadWiFiCredentials();

  bool wifiConnected = false;

  if (currentSSID.length() > 0) {
      Serial.println("Conectando a Wi-Fi: " + currentSSID);
      WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
      WiFi.setSleep(false);
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      Serial.println();
      
      if (WiFi.status() == WL_CONNECTED) {
          wifiConnected = true;
          esp32IP = WiFi.localIP().toString();
          Serial.println("WiFi connected");
          Serial.print("Camera Ready! Use 'http://");
          Serial.print(esp32IP);
          Serial.println("' to connect");
      } else {
          Serial.println("Fallo al conectar a Wi-Fi.");
          WiFi.disconnect(true);
      }
  }

  if (!wifiConnected) {
      Serial.println("Iniciando modo de configuración BLE...");
      initBLE();
      // No inicializamos la cámara si entramos a modo BLE para ahorrar memoria.
      return; 
  }

  // Si llegamos aquí, Wi-Fi conectó correctamente, procedemos a iniciar la cámara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  startCameraServer();

  if (currentBackendUrl.length() > 0) {
      String macAddress = getBluetoothMac();
      String wsPath = "/ws?deviceKey=" + urlEncode(macAddress);
      if (currentToken.length() > 0) {
          wsPath += "&token=" + urlEncode(currentToken);
      }
      wsPath += "&tipoAparato=" + urlEncode("Cámara");
      
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
      
      Serial.println("Conectando a WS Host: " + host + " Port: " + String(port) + " Path: " + wsPath);
      if (isWss) {
          webSocket.beginSSL(host, port, wsPath);
      } else {
          webSocket.begin(host, port, wsPath);
      }
      webSocket.onEvent(webSocketEvent);
      webSocket.setReconnectInterval(5000);
      webSocket.enableHeartbeat(10000, 3000, 2);
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
      webSocket.loop();
  }
  delay(10);
}
