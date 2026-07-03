/*
 * Referencia del circuito medidor ACS712 + relé.
 * La lógica activa está integrada en SocketWebServer/SocketWebServer.ino
 *
 * Pines:
 *   ACS712 -> GPIO 34
 *   Relé   -> GPIO 26
 */

#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "DONALAP";
const char* password = "12345678";

#define ACS712_PIN 34
#define RELE_PIN 26

WebServer server(80);

bool estadoFoco = false;

String paginaHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>Smart Meter ESP32</title>

<style>
body{
  font-family: Arial;
  text-align: center;
  background:#111;
  color:#00ff00;
}

.card{
  border:1px solid #00ff00;
  margin:10px auto;
  width:80%;
  padding:10px;
  border-radius:10px;
}

button{
  width:140px;
  height:45px;
  margin:10px;
}
</style>

</head>

<body>

<h2>⚡ Medidor de Consumo IoT</h2>

<div class="card">
  🔌 Estado: <span id="estado">---</span>
</div>

<div class="card">
  ⚡ Corriente: <span id="corriente">0</span> A
</div>

<div class="card">
  🔥 Potencia: <span id="potencia">0</span> W
</div>

<div class="card">
  📊 Energía: <span id="energia">0</span> Wh
</div>

<button onclick="encender()">ENCENDER</button>
<button onclick="apagar()">APAGAR</button>

<script>

function actualizar(){

 fetch('/datos')
 .then(r => r.json())
 .then(d => {

    document.getElementById("corriente").innerHTML = d.corriente.toFixed(3);
    document.getElementById("potencia").innerHTML = d.potencia.toFixed(2);
    document.getElementById("energia").innerHTML = d.energia.toFixed(3);

    document.getElementById("estado").innerHTML =
      d.estado ? "ENCENDIDO" : "APAGADO";
 });
}

function encender(){ fetch('/on'); }
function apagar(){ fetch('/off'); }

setInterval(actualizar, 1000);

</script>

</body>
</html>
)rawliteral";
}

float baseADC = 0;
float energiaWh = 0;
unsigned long lastTime = 0;

void calibrarACS() {
  long suma = 0;

  for (int i = 0; i < 1000; i++) {
    suma += analogRead(ACS712_PIN);
    delay(2);
  }

  baseADC = suma / 1000.0;
}

float getCorriente() {

  int adc = analogRead(ACS712_PIN);

  float voltaje = (adc / 4095.0) * 3.3;
  float voltajeBase = (baseADC / 4095.0) * 3.3;

  float corriente = (voltajeBase - voltaje) / 0.185;

  return abs(corriente);
}

float getPotencia(float corriente) {
  return 127.0 * corriente;
}

void actualizarEnergia(float potencia) {

  unsigned long now = millis();

  if (lastTime == 0) {
    lastTime = now;
    return;
  }

  float horas = (now - lastTime) / 3600000.0;

  if (estadoFoco == true) {
    energiaWh += potencia * horas;
  }

  lastTime = now;
}

void handleRoot() {
  server.send(200, "text/html", paginaHTML());
}

void handleDatos() {

  float corriente = getCorriente();

  if (corriente < 0.05 || estadoFoco == false) {
    corriente = 0;
  }

  float potencia = getPotencia(corriente);

  String json = "{";

  json += "\"adc\":" + String(analogRead(ACS712_PIN)) + ",";
  json += "\"corriente\":" + String(corriente, 3) + ",";
  json += "\"potencia\":" + String(potencia, 2) + ",";
  json += "\"energia\":" + String(energiaWh, 3) + ",";
  json += "\"estado\":" + String(estadoFoco ? "true" : "false");

  json += "}";

  server.send(200, "application/json", json);
}

void handleOn() {
  estadoFoco = true;

  digitalWrite(RELE_PIN, HIGH);

  server.send(200, "text/plain", "ON");
}

void handleOff() {
  estadoFoco = false;

  digitalWrite(RELE_PIN, LOW);

  server.send(200, "text/plain", "OFF");
}

void setup() {

  Serial.begin(115200);

  pinMode(RELE_PIN, OUTPUT);

  digitalWrite(RELE_PIN, LOW);

  WiFi.begin(ssid, password);

  Serial.print("Conectando");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/datos", handleDatos);
  server.on("/on", handleOn);
  server.on("/off", handleOff);

  server.begin();
}

void loop() {

  server.handleClient();

  static unsigned long last = 0;

  if (millis() - last >= 1000) {

    last = millis();

    float corriente = getCorriente();

    if (corriente < 0.05 || estadoFoco == false) {
      corriente = 0;
    }

    float potencia = getPotencia(corriente);

    actualizarEnergia(potencia);
  }
}
