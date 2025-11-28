#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "PedroWiFi 2.4Ghz";
const char* password = "negonego";

WebServer server(80);

// Estructura para objetos
struct Objeto {
  int id;
  int area;
  int perimetro;
  float circularidad;
  int centro_x, centro_y;
  String forma;
  String color;
  bool detectado;  // Nuevo campo para indicar si fue detectado
};

// Generar objeto aleatorio
Objeto generarObjetoAleatorio(int id) {
  Objeto obj;
  obj.id = id;
  obj.area = random(500, 5000);
  obj.perimetro = random(100, 500);
  obj.circularidad = random(30, 95) / 100.0;
  obj.centro_x = random(0, 640);
  obj.centro_y = random(0, 480);
  
  // Formas aleatorias
  const char* formas[] = {"Circulo", "Cuadrado", "Triangulo", "L"};
  obj.forma = formas[random(0, 4)];
  
  // Colores aleatorios
  const char* colores[] = {"Rojo", "Azul", "Verde", "Amarillo", "Naranja", "Morado", "Rosa", "Cyan"};
  obj.color = colores[random(0, 8)];
  
  obj.detectado = true;
  return obj;
}

// Generar objeto no detectado
Objeto generarObjetoNoDetectado(int id) {
  Objeto obj;
  obj.id = id;
  obj.area = -1;
  obj.perimetro = -1;
  obj.circularidad = -1.0;
  obj.centro_x = -1;
  obj.centro_y = -1;
  obj.forma = "";
  obj.color = "";
  obj.detectado = false;
  return obj;
}

void handleStatus() {
  StaticJsonDocument<2048> doc;
  
  // Siempre generar 4 objetos
  JsonArray objetos = doc.createNestedArray("objetos");
  
  // Decidir cuántos objetos estarán detectados (entre 0 y 4)
  int objetosDetectados = random(0, 5); // 0, 1, 2, 3, o 4
  
  // Array para controlar qué objetos están detectados
  bool detectados[4] = {false, false, false, false};
  for (int i = 0; i < objetosDetectados; i++) {
    int posicion;
    do {
      posicion = random(0, 4);
    } while (detectados[posicion]);
    detectados[posicion] = true;
  }
  
  // Generar los 4 objetos
  for (int i = 0; i < 4; i++) {
    Objeto obj;
    
    if (detectados[i]) {
      obj = generarObjetoAleatorio(i + 1);
    } else {
      obj = generarObjetoNoDetectado(i + 1);
    }
    
    JsonObject objetoJson = objetos.createNestedObject();
    objetoJson["id"] = obj.id;
    objetoJson["area"] = obj.area;
    objetoJson["perimetro"] = obj.perimetro;
    objetoJson["circularidad"] = obj.circularidad;
    objetoJson["centro_x"] = obj.centro_x;
    objetoJson["centro_y"] = obj.centro_y;
    objetoJson["forma"] = obj.forma;
    objetoJson["color"] = obj.color;
    objetoJson["detectado"] = obj.detectado;
  }
  
  // Datos adicionales del sistema
  doc["timestamp"] = millis();
  doc["total_objetos"] = objetosDetectados;
  doc["max_posibles"] = 4;
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);
  
  // Inicializar random
  randomSeed(analogRead(0));
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  server.on("/status", handleStatus);
  server.begin();
}

void loop() {
  server.handleClient();
}