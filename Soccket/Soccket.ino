#include <WiFi.h>
#include <WebSocketsClient.h>

const char* ssid = "AngelLap";
const char* password = "12345678";

WebSocketsClient webSocket;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length)
{
    switch(type)
    {
        case WStype_DISCONNECTED:
            Serial.println("Desconectado del servidor de Render");
            break;

        case WStype_CONNECTED:
            Serial.println("¡Conectado exitosamente a Render!");
            break;

        case WStype_TEXT:
            Serial.print("Mensaje recibido: ");
            Serial.println((char*)payload);

            // Aquí puedes agregar tu lógica para encender el LED
            // String msg = String((char*)payload);
            // if(msg == "LED_ON") { digitalWrite(LED_BUILTIN, HIGH); }
            break;
    }
}

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);

    Serial.print("Conectando WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi conectado");

    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());

    // --- CONFIGURACIÓN PARA RENDER ---
    // 1. Usamos 'beginSSL' en lugar de 'begin' para habilitar wss://
    // 2. Pasamos el dominio sin el "https://"
    // 3. El puerto seguro por defecto es el 443
    // 4. La ruta de tu endpoint sigue siendo "/ws"
    webSocket.beginSSL("pruebasocket.onrender.com", 443, "/ws");

    webSocket.onEvent(webSocketEvent);

    // Intenta reconectarse automáticamente cada 5 segundos si Render se "duerme"
    webSocket.setReconnectInterval(5000);
}

void loop()
{
    webSocket.loop();
}