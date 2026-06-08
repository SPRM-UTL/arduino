#include <WiFi.h>
#include <WebSocketsClient.h>

// =====================
// Configuracion WiFi
// =====================
const char* WIFI_SSID = "DONALAP";
const char* WIFI_PASSWORD = "12345678";

// =====================
// Configuracion servidor
// =====================
// Para Render usa WSS:
const char* WS_HOST = "192.168.100.60";
const uint16_t WS_PORT = 5295;
const bool USE_SSL = false;

// Para local seria algo asi:
// const char* WS_HOST = "192.168.1.50";
// const uint16_t WS_PORT = 8080;
// const bool USE_SSL = false;

// =====================
// Configuracion dispositivo
// =====================
const char* DEVICE_KEY = "esp32-b";
const char* TARGET_DEVICE_KEY = "esp32-a";

// Nombre solo informativo para tu firmware
const char* DEVICE_NAME = "ESP32 Sala";

WebSocketsClient webSocket;

const int FAN_PIN = 4;
const int LED_PIN = 2;

int velocidad = 0;

String buildWsPath() {
  String path = "/ws?deviceKey=";
  path += DEVICE_KEY;

  if (String(TARGET_DEVICE_KEY).length() > 0) {
    path += "&targetDeviceKey=";
    path += TARGET_DEVICE_KEY;
  }

  return path;
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Desconectado");
      break;

    case WStype_CONNECTED:
      Serial.println("[WS] Conectado al servidor");
      Serial.print("[DEVICE] ");
      Serial.println(DEVICE_KEY);
      break;

    case WStype_TEXT:
    {
        String msg = (char*)payload;

        Serial.print("[WS] Mensaje recibido: ");
        Serial.println(msg);

        if (msg == "LED_ON")
        {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("LED encendido");
        }
        else if (msg == "LED_OFF")
        {
            digitalWrite(LED_PIN, LOW);
            Serial.println("LED apagado");
        }
        else if (msg == "FAN_ON")
        {
          encenderVentilador()
        }
        else if (msg == "FAN_OFF")
        {
          apagarVentilador()
        }
        else if (msg == "FAN_UP")
        {
          aumentarVelocidad()
        }
        else if (msg == "FAN_DOWN")
        {
          disminuirVelocidad()
        }

        break;
    }

    case WStype_ERROR:
      Serial.println("[WS] Error");
      break;

    default:
      break;
  }
}

void connectWiFi() {
  Serial.print("Conectando a WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
}

void setupWebSocket() {
  String path = buildWsPath();

  Serial.print("Conectando WebSocket a: ");
  Serial.println(path);

  if (USE_SSL) {
    webSocket.beginSSL(WS_HOST, WS_PORT, path);
  } else {
    webSocket.begin(WS_HOST, WS_PORT, path);
  }

  webSocket.onEvent(onWebSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  ledcAttach(FAN_PIN, 5000, 8);
  ledcWrite(FAN_PIN, velocidad);

  Serial.println("***************");
  Serial.println("SOY EL FIRMWARE B NUEVO");
  Serial.println("DEVICE_KEY DEBE SER esp32-b");
  Serial.println("***************");

  Serial.println();
  Serial.println("Iniciando ESP32 WebSocket Client");
  Serial.print("Dispositivo: ");
  Serial.println(DEVICE_NAME);

  connectWiFi();
  setupWebSocket();

  Serial.println();
  Serial.println("Escribe un mensaje en la terminal serial y presiona Enter.");
}

void loop() {
  webSocket.loop();

  if (Serial.available() > 0) {
    String message = Serial.readStringUntil('\n');
    message.trim();

    if (message.length() > 0) {
      webSocket.sendTXT(message);

      Serial.print("[TX] ");
      Serial.println(message);
    }
  }
}

void encenderVentilador() {
    velocidad = 255;
    ledcWrite(FAN_PIN, velocidad);

    Serial.println("Ventilador encendido al 100%");
}

void apagarVentilador() {
    velocidad = 0;
    ledcWrite(FAN_PIN, velocidad);

    Serial.println("Ventilador apagado");
}

void aumentarVelocidad() {
    velocidad += 51; // 20% de 255 ≈ 51

    if (velocidad > 255)
        velocidad = 255;

    ledcWrite(FAN_PIN, velocidad);

    Serial.print("Velocidad: ");
    Serial.print((velocidad * 100) / 255);
    Serial.println("%");
}

void disminuirVelocidad() {
    velocidad -= 51;

    if (velocidad < 0)
        velocidad = 0;

    ledcWrite(FAN_PIN, velocidad);

    Serial.print("Velocidad: ");
    Serial.print((velocidad * 100) / 255);
    Serial.println("%");
}