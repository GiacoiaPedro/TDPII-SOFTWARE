/*18:27 4/12/2025
 * ESP32-CAM CON DETECCIÓN DE FIGURAS USANDO HU MOMENTS - VERSIÓN OPTIMIZADA
 * Compatible con interfaz Svelte Kit - MEJOR PRECISIÓN
 */

#include "WiFi.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "SPIFFS.h"
#include "img_converters.h"
#include <ArduinoJson.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include <stdbool.h>

// ---------- INICIO: Declaraciones únicas y consistentes ----------

// ----- Tipo Objeto (USAR ESTA VERSIÓN) -----
// Esta definición contiene los campos que tu código utiliza más abajo.
// Usa String para 'forma' y 'color' porque tu código hace .c_str() y asignaciones.
typedef struct {
    int id;
    int area;
    int perimetro;
    int centro_x;
    int centro_y;
    int bbox_x;
    int bbox_y;
    int bbox_w;
    int bbox_h;
    double momento_m00; // si usás momentos (opcional)
    String forma;       // nombre de la forma (ej. "CUADRADO")
    String color;       // nombre color (ej. "NEGRO")
} Objeto;

// ----- SSE: estructura cliente y variables globales (UNA SOLA VEZ) -----
#define MAX_SSE_CLIENTS 4

typedef struct {
    httpd_req_t *req;
    bool in_use;
} sse_client_t;

static sse_client_t sse_clients[MAX_SSE_CLIENTS];
static SemaphoreHandle_t sse_mutex = NULL;

// NOTA: no declarar prototipos 'extern' y luego definir 'static'.  
// Aquí NO ponemos prototipos separados; definiremos las funciones abajo sin 'static' duplicado.

// ---------- FIN: Declaraciones únicas ---------- 

// Prototipos (opcional pero ordena)
void sse_init_clients();
bool sse_add_client(httpd_req_t *req);
void sse_remove_client(httpd_req_t *req);
void sse_broadcast_json(const char *json_str);


// Inicializa estructura de clientes SSE
void sse_init_clients() {
    if (sse_mutex == NULL) {
        sse_mutex = xSemaphoreCreateMutex();
    }
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sse_clients[i].req = NULL;
        sse_clients[i].in_use = false;
    }
}

// Añadir cliente
bool sse_add_client(httpd_req_t *req) {
    bool ok = false;
    if (xSemaphoreTake(sse_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
            if (!sse_clients[i].in_use) {
                sse_clients[i].req = req;
                sse_clients[i].in_use = true;
                ok = true;
                break;
            }
        }
        xSemaphoreGive(sse_mutex);
    }
    return ok;
}

// Quitar cliente
void sse_remove_client(httpd_req_t *req) {
    if (xSemaphoreTake(sse_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
            if (sse_clients[i].in_use && sse_clients[i].req == req) {
                sse_clients[i].req = NULL;
                sse_clients[i].in_use = false;
                break;
            }
        }
        xSemaphoreGive(sse_mutex);
    }
}

// Enviar JSON a todos los clientes conectados
void sse_broadcast_json(const char *json_str) {
    if (sse_mutex == NULL) return;
    if (xSemaphoreTake(sse_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_clients[i].in_use && sse_clients[i].req) {
            httpd_req_t *client_req = sse_clients[i].req;
            char chunk[512];
            int len = snprintf(chunk, sizeof(chunk), "data: %s\n\n", json_str);
            esp_err_t err = httpd_resp_send_chunk(client_req, chunk, len);
            if (err != ESP_OK) {
                // desconectado o error => limpiar
                sse_clients[i].in_use = false;
                sse_clients[i].req = NULL;
            }
        }
    }

    xSemaphoreGive(sse_mutex);
}


static const char *TAG_SSE = "sse";
static httpd_handle_t server = NULL; // si ya la tenés definida, ignorá esta línea


// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "PedroWiFi 2.4Ghz"; //"iPhone de Tomás";//"Sandra Wifi 2.4";
const char* password =  "negonego"; //"tomas2002";//"0142265207";

// ===== PINES ESP32-CAM =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23

#define PCLK_GPIO_NUM     22

httpd_handle_t camera_httpd = NULL;

// Variables globales
bool calibrating = false;
int current_calibration_figure = -1;
int photo_count = 70;
uint8_t threshold_value = 75; // 0 <-- Más blanco  255 <-- Más negro
char ultima_figura[20] = "Ninguna";
char ultimo_color[20] = "N/A";
double ultima_confianza = 0.0;

// HTML embebido para la interfaz web
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Detector</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        
        .container {
            width: 100%;
            max-width: 1000px;
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 25px;
            text-align: center;
        }
        
        .header h1 {
            font-size: 2em;
            margin-bottom: 10px;
        }
        
        .header p {
            font-size: 1em;
            opacity: 0.9;
        }
        
        .main-content {
            display: flex;
            flex-wrap: wrap;
            padding: 30px;
            gap: 30px;
        }
        
        .video-section {
            flex: 1;
            min-width: 300px;
            display: flex;
            flex-direction: column;
            gap: 20px;
        }
        
        .video-container {
            position: relative;
            background: #000;
            border-radius: 10px;
            overflow: hidden;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }
        
        #stream {
            width: 100%;
            height: auto;
            display: block;
        }
        
        .controls {
            display: flex;
            gap: 10px;
            justify-content: center;
            margin-top: 5px;
        }
        
        .btn {
            padding: 12px 30px;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s;
            font-size: 1em;
        }
        
        .btn-detect {
            background: #28a745;
            color: white;
        }
        
        .btn-detect:hover {
            background: #218838;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(40, 167, 69, 0.3);
        }
        
        .btn-stream {
            background: #007bff;
            color: white;
        }
        
        .btn-stream:hover {
            background: #0056b3;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(0, 123, 255, 0.3);
        }
        
        .compass-section {
            flex: 0 0 300px;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }
        
        .compass-title {
            color: #667eea;
            font-size: 1.3em;
            margin-bottom: 25px;
            text-align: center;
            font-weight: 600;
        }
        
        .compass-container {
            width: 280px;
            height: 280px;
            position: relative;
            border-radius: 50%;
            background: linear-gradient(145deg, #f0f0f0, #ffffff);
            box-shadow: 15px 15px 30px #d9d9d9, 
                       -15px -15px 30px #ffffff;
            display: flex;
            align-items: center;
            justify-content: center;
            border: 2px solid #e0e0e0;
        }
        
        .compass-inner {
            width: 230px;
            height: 230px;
            position: relative;
            border-radius: 50%;
            background: white;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        
        .compass-axis {
            position: absolute;
            background: #333;
        }
        
        .axis-vertical {
            width: 3px;
            height: 100%;
            top: 0;
            left: 50%;
            transform: translateX(-50%);
        }
        
        .axis-horizontal {
            width: 100%;
            height: 3px;
            top: 50%;
            left: 0;
            transform: translateY(-50%);
        }
        
        .compass-label {
            position: absolute;
            font-size: 1.4em;
            font-weight: bold;
            color: #333;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.1);
        }
        
        .compass-top {
            top: 10px;
            left: 50%;
            transform: translateX(-50%);
            text-align: center;
        }
        
        .compass-top .degree {
            font-size: 0.8em;
            color: #667eea;
        }
        
        .compass-right {
            top: 50%;
            right: 10px;
            transform: translateY(-50%);
            text-align: center;
        }
        
        .compass-right .degree {
            font-size: 0.8em;
            color: #667eea;
        }
        
        .compass-bottom {
            bottom: 10px;
            left: 50%;
            transform: translateX(-50%);
            text-align: center;
        }
        
        .compass-bottom .degree {
            font-size: 0.8em;
            color: #667eea;
        }
        
        .compass-left {
            top: 50%;
            left: 10px;
            transform: translateY(-50%);
            text-align: center;
        }
        
        .compass-left .degree {
            font-size: 0.8em;
            color: #667eea;
        }
        
        .center-point {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            width: 15px;
            height: 15px;
            background: #667eea;
            border-radius: 50%;
            z-index: 10;
            box-shadow: 0 0 10px rgba(102, 126, 234, 0.5);
        }
        
        .coord-system {
            margin-top: 30px;
            text-align: center;
            color: #666;
            font-size: 0.9em;
            line-height: 1.5;
        }
        
        .coord-title {
            font-weight: 600;
            margin-bottom: 8px;
            color: #555;
        }
        
        .coord-description {
            color: #777;
            max-width: 250px;
        }
        
        @media (max-width: 768px) {
            .main-content {
                flex-direction: column;
                align-items: center;
            }
            
            .video-section {
                width: 100%;
            }
            
            .compass-section {
                width: 100%;
                align-items: center;
            }
            
            .compass-container {
                width: 250px;
                height: 250px;
            }
            
            .compass-inner {
                width: 200px;
                height: 200px;
            }
            
            .compass-label {
                font-size: 1.2em;
            }
        }
        
        @media (max-width: 480px) {
            .header {
                padding: 20px;
            }
            
            .header h1 {
                font-size: 1.7em;
            }
            
            .main-content {
                padding: 20px;
            }
            
            .compass-container {
                width: 220px;
                height: 220px;
            }
            
            .compass-inner {
                width: 180px;
                height: 180px;
            }
            
            .btn {
                padding: 10px 20px;
                font-size: 0.9em;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🎯 ESP32-CAM Detector</h1>
            <p>Sistema de detección de formas geométricas</p>
        </div>
        
        <div class="main-content">
            <div class="video-section">
                <div class="video-container">
                    <img id="stream" src="/processed_stream" alt="Stream en vivo">
                </div>
                <div class="controls">
                    <button id="btn-detect" class="btn btn-detect">⏸ Detener Stream</button>
                    <button id="btn-stream" class="btn btn-stream" style="display:none;">▶ Reanudar Stream</button>
                </div>
            </div>
            
            <div class="compass-section">
                <div class="compass-title">🧭 Guía de Vientos</div>
                <div class="compass-container">
                    <div class="compass-inner">
                        <!-- Ejes -->
                        <div class="compass-axis axis-vertical"></div>
                        <div class="compass-axis axis-horizontal"></div>
                        
                        <!-- Punto central -->
                        <div class="center-point"></div>
                        
                        <!-- Marcas de dirección -->
                        <div class="compass-label compass-top">
                            <div class="degree">270°</div>
                            <div class="direction">|</div>
                        </div>
                        
                        <div class="compass-label compass-right">
                            <div class="degree">0°</div>
                            <div class="direction">—</div>
                        </div>
                        
                        <div class="compass-label compass-bottom">
                            <div class="degree">90°</div>
                            <div class="direction">|</div>
                        </div>
                        
                        <div class="compass-label compass-left">
                            <div class="degree">180°</div>
                            <div class="direction">—</div>
                        </div>
                    </div>
                </div>
                
                <div class="coord-system">
                    <div class="coord-title">Sistema de Coordenadas</div>
                    <div class="coord-description">
                        0° (este) - 90° (sur)<br>
                        180° (oeste) - 270° (norte)<br>
                        Sentido horario desde el este
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <script>
        // Variables globales
        let isStreaming = true;
        
        // Elementos DOM
        const streamImg = document.getElementById('stream');
        const btnDetect = document.getElementById('btn-detect');
        const btnStream = document.getElementById('btn-stream');
        
        // Función para pausar stream
        async function pauseStream() {
            isStreaming = false;
            btnDetect.style.display = 'none';
            btnStream.style.display = 'block';
            
            // Detener el stream (la imagen quedará congelada)
            streamImg.src = ''; // Limpiar src
            
            // Obtener una captura
            try {
                const response = await fetch('/capture');
                const blob = await response.blob();
                const url = URL.createObjectURL(blob);
                streamImg.src = url;
                
                // Reanudar automáticamente después de 3 segundos
                setTimeout(() => {
                    if (!isStreaming) {
                        resumeStream();
                    }
                }, 3000);
                
            } catch (error) {
                console.error('Error en captura:', error);
                // Si hay error, reanudar de todos modos
                resumeStream();
            }
        }
        
        // Función para reanudar stream
        function resumeStream() {
            isStreaming = true;
            btnDetect.style.display = 'block';
            btnStream.style.display = 'none';
            
            // Restaurar stream en vivo
            streamImg.src = '/processed_stream';
        }
        
        // Event listeners
        btnDetect.addEventListener('click', pauseStream);
        btnStream.addEventListener('click', resumeStream);
        
        // Manejo de errores en la imagen
        streamImg.onerror = function() {
            console.log('Error cargando stream, intentando reconectar...');
            setTimeout(() => {
                streamImg.src = '/processed_stream';
            }, 1000);
        };
    </script>
</body>
</html>
)rawliteral";

// Buffer para imagen procesada
uint8_t* imagen_procesada = nullptr;
int* labels = nullptr;  // Para etiquetar componentes

// --- Máquina de Estados ---
enum ModoOperacion {
  MODO_DETECCION,  // Estado normal, busca figuras
  MODO_CALIBRACION, // Esperando para calibrar
  MODO_IDLE        // No hacer nada
};
volatile ModoOperacion modo_actual = MODO_DETECCION; // Inicia en modo "Detenido"
volatile int figura_a_calibrar = -1; // Qué figura vamos a calibrar (0-4)


// Variables para centroide
double ultimo_cx = -1;
double ultimo_cy = -1;

// Estructura para figuras mejorada
typedef struct {
    double hu[7];
    const char* nombre;
    bool calibrada;
    int muestras;  // Número de muestras para esta figura
} FiguraCalibrada;

FiguraCalibrada figuras_calibradas[5] = {
    {{-1.791, -13.305, -14.275, -12.958, -26.598, -20.774, 0.00}, "Cuadrado", true, 10},
    {{-1.836, -10.910, -13.538, -11.661, -25.362, -17.392, -24.350}, "Circulo", true, 10},
    {{-1.004, -2.588, -3.930, -5.233, -9.932, -6.726, -10.594}, "L", true, 0},
    {{-1.640, -7.158, -5.372, -8.911, -16.059, -12.502, -18.305}, "Triangulo", true, 10}
};



const int MIN_AREA = 500;         // área mínima para considerar un objeto
const float HU_SIM_SCALE = 0.3f;  // escala más estricta para mejor precisión



const int MAX_OBJETOS = 4;
Objeto objetos_detectados[MAX_OBJETOS];
int num_objetos = 0;

// ===== ESTRUCTURA PARA FLOOD FILL =====
struct Punto { int x, y; };

// ===== FLOOD FILL ITERATIVO =====
void floodFill(uint8_t* img, int* labels, int w, int h, int sx, int sy, int label) {
  const int MAX_Q = 100000;
  Punto* q = (Punto*)malloc(sizeof(Punto) * MAX_Q);
  if (!q) return;
  
  int qs = 0, qe = 0;
  q[qe++] = {sx, sy};
  
  while (qs < qe) {
    Punto p = q[qs++];
    if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) continue;
    
    int idx = p.y * w + p.x;
    
    // Detectar píxeles BLANCOS (255) como objetos
    if (labels[idx] != 0 || img[idx] != 0) continue;
    
    labels[idx] = label;
    
    if (qe < MAX_Q - 4) {
      q[qe++] = {p.x + 1, p.y};
      q[qe++] = {p.x - 1, p.y};
      q[qe++] = {p.x, p.y + 1};
      q[qe++] = {p.x, p.y - 1};
    }
  }
  free(q);
}

// ===== ANALIZAR OBJETO =====
Objeto analizar_objeto(int* labels, int label, int w, int h, uint16_t* rgb_buf) {
  Objeto obj;
  obj.id = label;
  obj.area = 0;
  obj.perimetro = 0;
  obj.centro_x = 0;
  obj.centro_y = 0;
  obj.bbox_x = w;
  obj.bbox_y = h;
  obj.bbox_w = 0;
  obj.bbox_h = 0;
  //obj.color = "N/A";
  
  long sum_x = 0, sum_y = 0;
  
  // Primera pasada: área, centroide, bounding box
  const int CROP_MARGIN = 10; 
  for (int y = CROP_MARGIN; y < h - CROP_MARGIN; y++) {
    for (int x = CROP_MARGIN; x < w - CROP_MARGIN; x++) {
      if (labels[y * w + x] == label) {
        obj.area++;
        sum_x += x;
        sum_y += y;
        
        if (x < obj.bbox_x) obj.bbox_x = x;
        if (y < obj.bbox_y) obj.bbox_y = y;
        if (x > obj.bbox_w) obj.bbox_w = x;
        if (y > obj.bbox_h) obj.bbox_h = y;
      }
    }
  }
  
  if (obj.area < 100) {  // Ignorar ruido
    obj.forma = "RUIDO";
    return obj;
  }
  
  obj.centro_x = sum_x / obj.area;
  obj.centro_y = sum_y / obj.area;
  double cx = (double)obj.centro_x;
  double cy = (double)obj.centro_y;

  obj.bbox_w = obj.bbox_w - obj.bbox_x + 1;
  obj.bbox_h = obj.bbox_h - obj.bbox_y + 1;
  
 // --- ¡NUEVA LÓGICA! ---
  // 1. Calcular Momentos Centrales (muXX)
  double mu20 = 0, mu02 = 0, mu11 = 0;
  double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;

  for (int y = CROP_MARGIN; y < h - CROP_MARGIN; y++) {
    for (int x = CROP_MARGIN; x < w - CROP_MARGIN; x++) {
      // ¡LA CLAVE! Solo analizar píxeles de ESTE objeto
      if (labels[y * w + x] == label) {
        double dx = (x - cx);
        double dy = (y - cy);
        
        mu20 += dx * dx;
        mu02 += dy * dy;
        mu11 += dx * dy;
        mu30 += dx * dx * dx;
        mu03 += dy * dy * dy;
        mu21 += dx * dx * dy;
        mu12 += dx * dy * dy;
      }
    }
  }

  // Normalización y Cálculo de Hu Moments
  // (Copiado 1:1 de version10.txt [cite: 944-957])
  double m00 = (double)obj.area;
  double area_sq = m00 * m00;
  double area_5_2 = pow(m00, 2.5);
  
  double nu20 = mu20 / area_sq;
  double nu02 = mu02 / area_sq;
  double nu11 = mu11 / area_sq;
  double nu30 = mu30 / area_5_2;
  double nu03 = mu03 / area_5_2;
  double nu21 = mu21 / area_5_2;
  double nu12 = mu12 / area_5_2;

  double hu_actual[7];
  hu_actual[0] = nu20 + nu02;
  hu_actual[1] = (nu20 - nu02) * (nu20 - nu02) + 4.0 * nu11 * nu11;
  hu_actual[2] = (nu30 - 3.0 * nu12) * (nu30 - 3.0 * nu12) + (3.0 * nu21 - nu03) * (3.0 * nu21 - nu03);
  hu_actual[3] = (nu30 + nu12) * (nu30 + nu12) + (nu21 + nu03) * (nu21 + nu03);
  hu_actual[4] = (nu30 - 3.0 * nu12) * (nu30 + nu12) * ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) +
                 (3.0 * nu21 - nu03) * (nu21 + nu03) * (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));
  hu_actual[5] = (nu20 - nu02) * ((nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03)) +
                 4.0 * nu11 * (nu30 + nu12) * (nu21 + nu03);
  hu_actual[6] = (3.0 * nu21 - nu03) * (nu30 + nu12) * ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) -
                 (nu30 - 3.0 * nu12) * (nu21 + nu03) * (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));

  for (int i = 0; i < 7; i++) {
    if (fabs(hu_actual[i]) > 1e-12) {
      hu_actual[i] = log(fabs(hu_actual[i]));
    } else {
      hu_actual[i] = 0.0;
    }
  }



  // 3. Clasificar 
  double mejor_sim = 0.00000;
  int mejor_idx = -1;
  for (int i = 0; i < 5; i++) {
    if (figuras_calibradas[i].calibrada) {
      double sim = calcular_similitud_hu_optimizada(hu_actual, figuras_calibradas[i].hu);
      if (sim > mejor_sim) {
        mejor_sim = sim;
        mejor_idx = i;
      }
    }
  }
  
  // Usamos el umbral adaptativo 
  double umbral_adaptativo = 0.35; 
  if (mejor_idx != -1 && figuras_calibradas[mejor_idx].muestras >= 3) {
      umbral_adaptativo = 0.4;
  }

  if (mejor_idx != -1 && mejor_sim >= 0.10) { // 0.35 es tu umbral
     obj.forma = figuras_calibradas[mejor_idx].nombre;
  } else {
     obj.forma = "DESCONOCIDO";
  }

  // 4. Detección de Color (¡Ya la tienes!)
  // (Copia la lógica de "vistazo" de 'prueba2.txt' [cite: 522-542])
  // ... (usa obj.centro_x y obj.centro_y para mirar en rgb_buf) ...
  // obj.color = get_color_name(...);
  if (rgb_buf) {
    uint16_t pixel_rgb565 = rgb_buf[obj.centro_y * w + obj.centro_x];
    uint8_t r = ((pixel_rgb565 >> 11) & 0x1F) << 3;
    uint8_t g = ((pixel_rgb565 >> 5) & 0x3F) << 2;
    uint8_t b = (pixel_rgb565 & 0x1F) << 3;
    float h, s, v;
    rgb888_to_hsv(r, g, b, h, s, v); // Asegúrate de tener esta función en tu sketch
    obj.color = get_color_name(h, s, v); // Asegúrate de tener esta función
  }
  return obj;
}


//================== FUNCIONES PARA EL COLOR ======================//
/**
 * Convierte un píxel RGB888 a HSV (Hue, Saturation, Value)
 * H: 0-360 (Matiz/Color)
 * S: 0-1 (Saturación/Pureza)
 * V: 0-1 (Valor/Brillo)
 */
void rgb888_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
  float r_f = r / 255.0;
  float g_f = g / 255.0;
  float b_f = b / 255.0;
  float cmax = max(r_f, max(g_f, b_f));
  float cmin = min(r_f, min(g_f, b_f));
  float delta = cmax - cmin;

  if (delta == 0) h = 0;
  else if (cmax == r_f) h = 60 * fmod(((g_f - b_f) / delta), 6);
  else if (cmax == g_f) h = 60 * (((b_f - r_f) / delta) + 2);
  else h = 60 * (((r_f - g_f) / delta) + 4);
  if (h < 0) h += 360.0;
  s = (cmax == 0) ? 0 : (delta / cmax);
  v = cmax;
}

/**
 * Devuelve el nombre de un color basado en sus valores HSV
 */
String get_color_name(float h, float s, float v) {
  // si está muy oscuro → Negro/grafito independiente del hue
  if (v < 0.15) {
    return "Negro";
  }
  // si no tiene mucha saturación → lo tomamos como Negro o “gris”
  if (s < 0.20) {
    return "Negro";
  }

  // Ahora sí, cases de color según hue
  if (h < 75 || h >= 315) {
    return "Rojo";
  }
  if (h >= 190 && h < 270) {
    return "Azul";
  }
  return "N/A";
}


/**
 * Detecta el color promedio en un área (ej. 5x5) alrededor del centroide
 * en la imagen original a color (RGB565).
 */
String detectar_color_promedio(uint16_t* rgb565_buf, int w, int h, int cx, int cy, int sample_size = 5) {
  if (cx < 0 || cy < 0) return "N/A";

  long total_r = 0, total_g = 0, total_b = 0;
  int count = 0;

  // Tomar un promedio de un cuadrado de (sample_size*2+1) x (sample_size*2+1)
  for (int y = cy - sample_size; y <= cy + sample_size; y++) {
    for (int x = cx - sample_size; x <= cx + sample_size; x++) {
      
      // Asegurarse de no salirse de los bordes de la imagen
      if (x < 0 || x >= w || y < 0 || y >= h) {
        continue;
      }

      // 1. Obtener el píxel de color original
      uint16_t pixel_rgb565 = rgb565_buf[y * w + x];
      
      // 2. Convertir RGB565 a RGB888
      uint8_t r = ((pixel_rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((pixel_rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (pixel_rgb565 & 0x1F) << 3;

      // 3. Sumar para el promedio
      total_r += r;
      total_g += g;
      total_b += b;
      count++;
    }
  }

  if (count == 0) return "N/A";

  // 4. Calcular el promedio RGB
  uint8_t avg_r = total_r / count;
  uint8_t avg_g = total_g / count;
  uint8_t avg_b = total_b / count;

  // 5. Convertir el promedio RGB a HSV
  float h_avg, s_avg, v_avg;
  rgb888_to_hsv(avg_r, avg_g, avg_b, h_avg, s_avg, v_avg);

  //Serial.printf("    > [Debug Color] H: %.1f, S: %.2f, V: %.2f\n", h_avg, s_avg, v_avg);
  // 6. Obtener el nombre del color
  return get_color_name(h_avg, s_avg, v_avg);
}


// ===== FUNCIONES DE PROCESAMIENTO DE IMAGEN OPTIMIZADAS =====

// ---- Filtro Gaussiano 3x3 ----
void aplicar_blur(uint8_t* src, uint8_t* dst, int width, int height) {
    // Kernel Gaussiano 3x3 normalizado (suma = 16)
    const int k[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    const int ksum = 16;

    // Copiar bordes sin modificar
    for (int x = 0; x < width; x++) {
        dst[x] = src[x];
        dst[(height - 1) * width + x] = src[(height - 1) * width + x];
    }
    for (int y = 0; y < height; y++) {
        dst[y * width + 0] = src[y * width + 0];
        dst[y * width + (width - 1)] = src[y * width + (width - 1)];
    }

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int acc = 0;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    acc += src[(y + ky) * width + (x + kx)] * k[ky + 1][kx + 1];
                }
            }
            dst[y * width + x] = acc / ksum;
        }
    }
}

// ---- Binarización optimizada ----
void aplicar_binarizacion(uint8_t* src, uint8_t* dst, int width, int height, uint8_t threshold) {
    int len = width * height;
    uint8_t t = threshold;

    for (int i = 0; i < len; i++) {
        dst[i] = (src[i] > t) ? 255 : 0;
    }
}

// ---- Erosión optimizada ----
void aplicar_erosion(uint8_t* src, uint8_t* dst, int width, int height) {
    int len = width * height;
    for (int i = 0; i < len; i++) dst[i] = 255;

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            bool allObject = true;
            for (int ky = -1; ky <= 1 && allObject; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    if (src[(y + ky) * width + (x + kx)] != 0) {
                        allObject = false;
                        break;
                    }
                }
            }
            dst[y * width + x] = allObject ? 0 : 255;
        }
    }
}

// ---- Dilatación optimizada ----
void aplicar_dilatacion(uint8_t* src, uint8_t* dst, int width, int height) {
    int len = width * height;
    for (int i = 0; i < len; i++) dst[i] = 255;

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            bool anyObject = false;
            for (int ky = -1; ky <= 1 && !anyObject; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    if (src[(y + ky) * width + (x + kx)] == 0) {
                        anyObject = true;
                        break;
                    }
                }
            }
            dst[y * width + x] = anyObject ? 0 : 255;
        }
    }
}

// ===== FUNCIÓN UNIFICADA DE FILTRADO =====
void aplicar_filtros_completos(uint8_t* entrada, uint8_t* salida, int width, int height, uint8_t threshold) {
    uint8_t* temp1 = (uint8_t*)malloc(width * height);
    uint8_t* temp2 = (uint8_t*)malloc(width * height);
    
    if (!temp1 || !temp2) {
        // Fallback: solo binarización
        aplicar_binarizacion(entrada, salida, width, height, threshold);
        if (temp1) free(temp1);
        if (temp2) free(temp2);
        return;
    }
    
    // Secuencia correcta de filtros
    aplicar_blur(entrada, temp1, width, height);           // 1. Suavizado
    aplicar_binarizacion(temp1, temp2, width, height, threshold); // 2. Binarización
    aplicar_erosion(temp2, temp1, width, height);          // 3. Erosión (elimina ruido)  
    aplicar_dilatacion(temp1, salida, width, height);      // 4. Dilatación (rellena huecos)
    

    free(temp1);
    free(temp2);
}

// ===== FUNCIÓN PARA PINTAR CENTROIDE =====
void pintar_centroide_en_imagen(uint8_t* imagen_color, int width, int height, double cx, double cy) {
    int centroide_x = (int)round(cx);
    int centroide_y = (int)round(cy);
    
    // Verificar que las coordenadas estén dentro de los límites
    if (centroide_x < 0 || centroide_x >= width || centroide_y < 0 || centroide_y >= height) {
        return;
    }
    
    // Pintar pixel del centroide de rojo en imagen RGB
    // Asumiendo que imagen_color es RGB888 (3 bytes por pixel)
    int pixel_index = (centroide_y * width + centroide_x) * 3;
    
    // Rojo: R=255, G=0, B=0
    imagen_color[pixel_index] = 255;     // Canal Rojo
    imagen_color[pixel_index + 1] = 0;   // Canal Verde  
    imagen_color[pixel_index + 2] = 0;   // Canal Azul
    
    // Opcional: pintar una cruz pequeña para mejor visibilidad
    int cross_size = 6;
    for (int i = -cross_size; i <= cross_size; i++) {
        // Línea horizontal
        int x_h = centroide_x + i;
        int y_h = centroide_y;
        if (x_h >= 0 && x_h < width && y_h >= 0 && y_h < height) {
            int idx_h = (y_h * width + x_h) * 3;
            imagen_color[idx_h] = 0;
            imagen_color[idx_h + 1] = 0;
            imagen_color[idx_h + 2] = 255;
        }
        
        // Línea vertical
        int x_v = centroide_x;
        int y_v = centroide_y + i;
        if (x_v >= 0 && x_v < width && y_v >= 0 && y_v < height) {
            int idx_v = (y_v * width + x_v) * 3;
            imagen_color[idx_v] = 0;
            imagen_color[idx_v + 1] = 0;
            imagen_color[idx_v + 2] = 255;
        }
    }
}

// ===== SISTEMA DE FUENTE BITMAP SIMPLE (5x7 píxeles) =====

// Función para obtener el patrón de un carácter específico
void obtener_patron_caracter(char c, uint8_t patron[7]) {
    // Inicializar con ceros
    for (int i = 0; i < 7; i++) patron[i] = 0x00;
    
    switch(c) {
        case 'C':
            patron[0] = 0x0E; patron[1] = 0x11; patron[2] = 0x10;
            patron[3] = 0x10; patron[4] = 0x10; patron[5] = 0x11;
            patron[6] = 0x0E;
            break;
        case 'u':
            patron[2] = 0x11; patron[3] = 0x11; patron[4] = 0x11;
            patron[5] = 0x13; patron[6] = 0x0D;
            break;
        case 'a':
            patron[2] = 0x0E; patron[3] = 0x01; patron[4] = 0x0F;
            patron[5] = 0x11; patron[6] = 0x0F;
            break;
        case 'i':
            patron[0] = 0x04; patron[2] = 0x0C; patron[3] = 0x04;
            patron[4] = 0x04; patron[5] = 0x04; patron[6] = 0x0E;
            break;
        case 'r':
            patron[2] = 0x16; patron[3] = 0x19; patron[4] = 0x10;
            patron[5] = 0x10; patron[6] = 0x10;
            break;
        case 'T':
            patron[0] = 0x1F; patron[1] = 0x04; patron[2] = 0x04;
            patron[3] = 0x04; patron[4] = 0x04; patron[5] = 0x04;
            patron[6] = 0x04;
            break;
        case 'L':
            patron[0] = 0x10; patron[1] = 0x10; patron[2] = 0x10;
            patron[3] = 0x10; patron[4] = 0x10; patron[5] = 0x10;
            patron[6] = 0x1F;
            break;
    }
}

// Función para obtener abreviatura de la forma
const char* obtener_abreviatura(const char* forma) {
    if (strcmp(forma, "Cuadrado") == 0) return "Cua";
    if (strcmp(forma, "Circulo") == 0) return "Cir";
    if (strcmp(forma, "Triangulo") == 0) return "Tri";
    if (strcmp(forma, "L") == 0) return "L";
    return "?"; // Desconocido
}

    
   // Función para obtener color RGB según el nombre del color
void obtener_color_rgb(const char* color_nombre, uint8_t &r, uint8_t &g, uint8_t &b) {
    // Si es negro, usar blanco para que sea visible
    if (strcmp(color_nombre, "Negro") == 0) {
        r = 255; g = 255; b = 255;
        return;
    }
    
    // ⭐ IMPORTANTE: El ESP32 usa BGR, no RGB
    // Por eso invertimos r y b en las asignaciones
    
    if (strcmp(color_nombre, "Rojo") == 0) {
        r = 0; g = 0; b = 255;  // BGR: Azul en r, Rojo en b
    } else if (strcmp(color_nombre, "Azul") == 0) {
        r = 255; g = 50; b = 0;  // BGR: Rojo en r, Azul en b
    } else if (strncmp(color_nombre, "Blanco", 6) == 0 || strncmp(color_nombre, "Gris", 4) == 0) {
        r = 200; g = 200; b = 200;  // Gris queda igual
    } else {
        // Color por defecto (blanco)
        r = 255; g = 255; b = 255;
    }
}

// Función para dibujar un carácter en la imagen
void dibujar_caracter(uint8_t* imagen_color, int width, int height, 
                     int x_start, int y_start, char c, 
                     uint8_t r, uint8_t g, uint8_t b) {
    
    uint8_t char_data[7];
    obtener_patron_caracter(c, char_data);
    
    for (int row = 0; row < 7; row++) {
        uint8_t row_data = char_data[row];
        for (int col = 0; col < 5; col++) {
            if (row_data & (1 << (4 - col))) { // Bit encendido = píxel activo
                int px = x_start + col;
                int py = y_start + row;
                
                // Verificar límites
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    int idx = (py * width + px) * 3;
                    imagen_color[idx] = r;
                    imagen_color[idx + 1] = g;
                    imagen_color[idx + 2] = b;
                }
            }
        }
    }
}

// Función para dibujar texto
void dibujar_texto(uint8_t* imagen_color, int width, int height,
                   int x, int y, const char* texto,
                   uint8_t r, uint8_t g, uint8_t b) {
    
    int x_offset = 0;
    for (int i = 0; texto[i] != '\0'; i++) {
        dibujar_caracter(imagen_color, width, height, 
                        x + x_offset, y, texto[i], r, g, b);
        x_offset += 6; // 5 píxeles de ancho + 1 de espacio
    }
}

// ===== FUNCIÓN MEJORADA PARA PINTAR CENTROIDE CON ETIQUETA =====
void pintar_centroide_con_etiqueta(uint8_t* imagen_color, int width, int height, 
                                   double cx, double cy, 
                                   const char* forma, const char* color_nombre) {
    int centroide_x = (int)round(cx);
    int centroide_y = (int)round(cy);
    
    // Verificar que las coordenadas estén dentro de los límites
    if (centroide_x < 0 || centroide_x >= width || centroide_y < 0 || centroide_y >= height) {
        return;
    }
    
    // Obtener color para el texto basado en el color del objeto
    uint8_t text_r, text_g, text_b;
    obtener_color_rgb(color_nombre, text_r, text_g, text_b);
    
    // Pintar cruz del centroide (en el color del objeto)
    int cross_size = 6;
    for (int i = -cross_size; i <= cross_size; i++) {
        // Línea horizontal
        int x_h = centroide_x + i;
        int y_h = centroide_y;
        if (x_h >= 0 && x_h < width && y_h >= 0 && y_h < height) {
            int idx_h = (y_h * width + x_h) * 3;
            imagen_color[idx_h] = text_r;
            imagen_color[idx_h + 1] = text_g;
            imagen_color[idx_h + 2] = text_b;
        }
        
        // Línea vertical
        int x_v = centroide_x;
        int y_v = centroide_y + i;
        if (x_v >= 0 && x_v < width && y_v >= 0 && y_v < height) {
            int idx_v = (y_v * width + x_v) * 3;
            imagen_color[idx_v] = text_r;
            imagen_color[idx_v + 1] = text_g;
            imagen_color[idx_v + 2] = text_b;
        }
    }
    
    // Obtener abreviatura de la forma
    const char* abreviatura = obtener_abreviatura(forma);
    
    // Calcular posición del texto (al lado derecho del centroide)
    int text_x = centroide_x + cross_size + 3; // 3 píxeles de separación
    int text_y = centroide_y - 3; // Centrado verticalmente con la cruz
    
    // Ajustar si se sale de los límites
    if (text_x + (strlen(abreviatura) * 6) >= width) {
        text_x = centroide_x - (strlen(abreviatura) * 6) - cross_size - 3; // Poner a la izquierda
    }
    if (text_y < 0) text_y = 0;
    if (text_y + 7 >= height) text_y = height - 8;
    
    // Dibujar el texto
    dibujar_texto(imagen_color, width, height, text_x, text_y, 
                 abreviatura, text_r, text_g, text_b);
}
// ===== MOMENTOS DE HU CORREGIDOS (CON RECORTE) =====
void calcular_momentos_hu_corregido(uint8_t* imagen_binaria, int width, int height, double hu[7], double* cx_out = nullptr, double* cy_out = nullptr) {
    double m00 = 0, m10 = 0, m01 = 0;
    
    // --- ¡NUEVO! Definir un margen para ignorar los bordes ---
    // Ajusta este valor (en píxeles) según sea necesario
    const int CROP_MARGIN = 10; 

    // --- CAMBIO AQUÍ ---
    // Calcular momentos espaciales (ignorando los márgenes)
    for (int y = CROP_MARGIN; y < height - CROP_MARGIN; y++) {
        for (int x = CROP_MARGIN; x < width - CROP_MARGIN; x++) {
    // --- FIN DEL CAMBIO ---
            if (imagen_binaria[y * width + x] == 0) { // Objeto = 0
                m00 += 1;
                m10 += x;
                m01 += y;
            }
        }
    }
    
    if (m00 < MIN_AREA) {
        for (int i = 0; i < 7; i++) hu[i] = 0.0;
        if (cx_out) *cx_out = -1;
        if (cy_out) *cy_out = -1;
        return;
    }
    
    // Centroide (esto se calcula bien porque m00, m10, m01 son correctos)
    double cx = m10 / m00;
    double cy = m01 / m00;
    
    if (cx_out) *cx_out = cx;
    if (cy_out) *cy_out = cy;
    
    // Momentos centrales
    double mu20 = 0, mu02 = 0, mu11 = 0;
    double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;

    // --- CAMBIO AQUÍ ---
    // Calcular momentos centrales (ignorando los márgenes)
    for (int y = CROP_MARGIN; y < height - CROP_MARGIN; y++) {
        for (int x = CROP_MARGIN; x < width - CROP_MARGIN; x++) {
    // --- FIN DEL CAMBIO ---
            if (imagen_binaria[y * width + x] == 0) {
                double dx = (x - cx);
                double dy = (y - cy);
                
                mu20 += dx * dx;
                mu02 += dy * dy;
                mu11 += dx * dy;
                mu30 += dx * dx * dx;
                mu03 += dy * dy * dy;
                mu21 += dx * dx * dy;
                mu12 += dx * dy * dy;
            }
        }
    }
    
    // ... (El resto de la función para calcular Hu[0] a Hu[6] no cambia) ...
    // ✅ NORMALIZACIÓN CORRECTA para invarianza a escala
    double area_sq = m00 * m00;
    double area_5_2 = pow(m00, 2.5);
    
    // Momentos normalizados
    double nu20 = mu20 / area_sq;
    double nu02 = mu02 / area_sq;
    double nu11 = mu11 / area_sq;
    double nu30 = mu30 / area_5_2;
    double nu03 = mu03 / area_5_2;
    double nu21 = mu21 / area_5_2;
    double nu12 = mu12 / area_5_2;

    // Momentos de Hu originales
    hu[0] = nu20 + nu02;
    hu[1] = (nu20 - nu02) * (nu20 - nu02) + 4.0 * nu11 * nu11;
    hu[2] = (nu30 - 3.0 * nu12) * (nu30 - 3.0 * nu12) + 
            (3.0 * nu21 - nu03) * (3.0 * nu21 - nu03);
    hu[3] = (nu30 + nu12) * (nu30 + nu12) + (nu21 + nu03) * (nu21 + nu03);
    hu[4] = (nu30 - 3.0 * nu12) * (nu30 + nu12) * ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) +
            (3.0 * nu21 - nu03) * (nu21 + nu03) * (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));
    hu[5] = (nu20 - nu02) * ((nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03)) +
            4.0 * nu11 * (nu30 + nu12) * (nu21 + nu03);
    hu[6] = (3.0 * nu21 - nu03) * (nu30 + nu12) * ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) -
            (nu30 - 3.0 * nu12) * (nu21 + nu03) * (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));

    // Aplicar logaritmo para comprimir rango y mejorar comparación
    for (int i = 0; i < 7; i++) {
        if (fabs(hu[i]) > 1e-12) {
            hu[i] = log(fabs(hu[i]));
        } else {
            hu[i] = 0.0;
        }
    }
}


// ===== SIMILITUD MEJORADA CON PESOS INTELIGENTES =====
double calcular_similitud_hu_optimizada(double hu1[7], double hu2[7]) {
    // Pesos basados en estabilidad de los momentos
    // Hu1 y Hu2 son más estables, Hu3-Hu7 más sensibles
    double pesos[7] = {1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    
    double distancia_ponderada = 0.0;
    double suma_pesos = 0.0;
    
    for (int i = 0; i < 7; i++) {
        double diff = fabs(hu1[i] - hu2[i]);
        
        // Para momentos significativamente diferentes de cero, usar diferencia relativa
        if (fabs(hu1[i]) > 0.1 && fabs(hu2[i]) > 0.1) {
            diff = diff / ((fabs(hu1[i]) + fabs(hu2[i])) / 2.0);
        }
        
        distancia_ponderada += pesos[i] * diff;
        suma_pesos += pesos[i];
    }
    
    double distancia_normalizada = distancia_ponderada / suma_pesos;
    
    // Convertir a similitud [0-1] con curva exponencial
    double simil = exp(-distancia_normalizada / HU_SIM_SCALE);
    return fmax(0.0, fmin(1.0, simil));
}

/*// ===== SIMILITUD MEJORADA (VERSIÓN SIMPLIFICADA) =====
double calcular_similitud_hu_optimizada(double hu1[7], double hu2[7]) {
    // hu1 = Objeto en vivo (calculado)
    // hu2 = Objeto calibrado (guardado)

    double distancia_total = 0.0;

    // Pesos (igual que antes)
    double pesos[7] = {1.0, 0.8, 0.3, 0.2, 0.0, 0.1, 0.00};
    double suma_pesos = 2.55; // Suma de los pesos de arriba

    for (int i = 0; i < 7; i++) {
        // Calcular la diferencia absoluta (simple)
        double diff = fabs(hu1[i] - hu2[i]);

        // Ponderar la diferencia
        distancia_total += diff * pesos[i];
    }

    // Normalizar la distancia por los pesos
    double distancia_normalizada = distancia_total / suma_pesos;

    // Convertir a similitud [0-1] con curva exponencial
    // (HU_SIM_SCALE = 0.3f, definido al inicio)
    double simil = exp(-distancia_normalizada / HU_SIM_SCALE);
    return fmax(0.0, fmin(1.0, simil));
}

*/
// ===== DETECCIÓN MEJORADA =====
void detectar_figura_mejorada(uint8_t* imagen_binaria, int width, int height, char* figura, double* confianza) {
    double hu_actual[7];
    double cx, cy;
    calcular_momentos_hu_corregido(imagen_binaria, width, height, hu_actual, &cx, &cy);
    
    // Guardar centroide global
    ultimo_cx = cx;
    ultimo_cy = cy;
    
    double mejor_sim = 0.0;
    int mejor_idx = -1;
    
    for (int i = 0; i < 5; i++) {
        if (figuras_calibradas[i].calibrada && figuras_calibradas[i].muestras > 0) {
            double sim = calcular_similitud_hu_optimizada(hu_actual, figuras_calibradas[i].hu);
            
            if (sim > mejor_sim) {
                mejor_sim = sim;
                mejor_idx = i;
            }
        }
    }
    
    // Umbral adaptativo basado en número de muestras
    double umbral_adaptativo = 0.35;
    if (mejor_idx != -1 && figuras_calibradas[mejor_idx].muestras >= 3) {
        umbral_adaptativo = 0.4; // Más estricto con más muestras
    }
    
    if (mejor_idx != -1 && mejor_sim >= umbral_adaptativo) {
        strncpy(figura, figuras_calibradas[mejor_idx].nombre, 19);
        *confianza = mejor_sim;
        
        // Debug serial
        Serial.printf("✅ Detectado: %s %s (%.1f%%) - Centroide: (%.1f, %.1f) - Muestras: %d\n", 
             ultimo_color, figura, mejor_sim * 100, cx, cy, figuras_calibradas[mejor_idx].muestras);
    } else {
        strcpy(figura, "Ninguna");
        *confianza = 0.0;
    }
}

// ===== CALIBRACIÓN CON MÚLTIPLES MUESTRAS =====
bool calibrar_con_promedio(int figure_id) {
    const int MUESTRAS_CALIBRACION = 10; // Reducido para mejor rendimiento
    double hu_acum[7] = {0};
    int muestras_exitosas = 0;
    
    Serial.printf("🎯 Calibrando %s con %d muestras...\n", 
                 figuras_calibradas[figure_id].nombre, MUESTRAS_CALIBRACION);
    
    for (int muestra = 0; muestra < MUESTRAS_CALIBRACION; muestra++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("❌ Error obteniendo frame");
            continue;
        }
        
        if (fb->format != PIXFORMAT_JPEG) {
            Serial.println("❌ Formato no JPEG");
            esp_camera_fb_return(fb);
            continue;
        }
        
        size_t grayscale_len = fb->width * fb->height;
        uint8_t* grayscale = (uint8_t*)malloc(grayscale_len);
        uint8_t* rgb565 = (uint8_t*)malloc(grayscale_len * 2);
        
        if (!grayscale || !rgb565) {
            Serial.println("❌ Error memoria en calibración");
            if (grayscale) free(grayscale);
            if (rgb565) free(rgb565);
            esp_camera_fb_return(fb);
            continue;
        }
        
        // Convertir JPEG a RGB565
        if (jpg2rgb565(fb->buf, fb->len, rgb565, JPG_SCALE_NONE)) {
            // Conversión a escala de grises
            uint16_t* pixels = (uint16_t*)rgb565;
            for (int i = 0; i < grayscale_len; i++) {
                uint16_t pixel = pixels[i];
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                grayscale[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
            }
            
            // Aplicar filtros
            aplicar_filtros_completos(grayscale, grayscale, fb->width, fb->height, threshold_value);
            
            // Calcular momentos Hu
            double hu_temp[7];
            calcular_momentos_hu_corregido(grayscale, fb->width, fb->height, hu_temp);
            
            // Verificar que no sean todos cero (objeto detectado)
            bool hu_valido = false;
            for (int i = 0; i < 7; i++) {
                if (fabs(hu_temp[i]) > 0.001) {
                    hu_valido = true;
                    break;
                }
            }
            
            if (hu_valido) {
                for (int i = 0; i < 7; i++) {
                    hu_acum[i] += hu_temp[i];
                }
                muestras_exitosas++;
                Serial.printf("  Muestra %d: [%.3f, %.3f, %.3f]\n", 
                             muestra + 1, hu_temp[0], hu_temp[1], hu_temp[2]);
            } else {
                Serial.printf("  Muestra %d: Objeto no detectado\n", muestra + 1);
            }
        }
        
        free(grayscale);
        free(rgb565);
        esp_camera_fb_return(fb);
        //delay(200); // Pausa entre muestras
        // --- ¡NUEVO! PAUSA PARA MOVER EL OBJETO ---
        if (muestra < MUESTRAS_CALIBRACION - 1) { // No pausar después de la última muestra
          Serial.printf("  >> MUEVA EL OBJETO. Próxima muestra en 5 segundos...\n");
          delay(5000); // 5 segundos de pausa
        }
    }
    
    if (muestras_exitosas > 0) {
        // Promedio de momentos
        for (int i = 0; i < 7; i++) {
            figuras_calibradas[figure_id].hu[i] = hu_acum[i] / muestras_exitosas;
        }
        
        figuras_calibradas[figure_id].calibrada = true;
        figuras_calibradas[figure_id].muestras = muestras_exitosas;
        
        Serial.printf("✅ %s calibrado: %d muestras - Hu: [%.3f, %.3f, %.3f]\n",
                     figuras_calibradas[figure_id].nombre, muestras_exitosas,
                     figuras_calibradas[figure_id].hu[0],
                     figuras_calibradas[figure_id].hu[1],
                     figuras_calibradas[figure_id].hu[2]);
        return true;
    } else {
        Serial.printf("❌ Error: No se pudieron obtener muestras válidas para %s\n",
                     figuras_calibradas[figure_id].nombre);
        return false;
    }
}

// ===== FUNCIÓN PARA CONFIGURAR CORS =====
static void set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ===== ENDPOINTS =====

// Endpoint: /status (GET) - Modificado para compatibilidad con app Svelte
static esp_err_t status_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    StaticJsonDocument<2048> doc;
    doc["conectado"] = true;
    doc["fotos_capturadas"] = photo_count;
    doc["calibrando"] = calibrating;
    doc["threshold"] = threshold_value;
    doc["ultimo_centroide_x"] = ultimo_cx;
    doc["ultimo_centroide_y"] = ultimo_cy;
    doc["max_posibles"] = 4;
    
    // Crear array de 4 objetos (posiciones 1-4)
    JsonArray objetos = doc.createNestedArray("objetos");
    
    // Inicializar las 4 posiciones con -1 o valores por defecto
    for (int i = 1; i <= 4; i++) {
        JsonObject obj = objetos.createNestedObject();
        obj["id"] = i;
        
        // Buscar si este objeto existe en objetos_detectados
        bool detectado = false;
        for (int j = 0; j < num_objetos; j++) {
            if (objetos_detectados[j].id == i) {
                detectado = true;
                obj["detectado"] = true;
                obj["forma"] = objetos_detectados[j].forma;
                obj["color"] = objetos_detectados[j].color;
                obj["area"] = objetos_detectados[j].area;
                obj["centro_x"] = objetos_detectados[j].centro_x;
                obj["centro_y"] = objetos_detectados[j].centro_y;
                obj["bbox_x"] = objetos_detectados[j].bbox_x;
                obj["bbox_y"] = objetos_detectados[j].bbox_y;
                obj["bbox_w"] = objetos_detectados[j].bbox_w;
                obj["bbox_h"] = objetos_detectados[j].bbox_h;
                obj["perimetro"] = objetos_detectados[j].perimetro;
                break;
            }
        }
        
        // Si no se encontró, poner valores por defecto
        if (!detectado) {
            obj["detectado"] = false;
            obj["forma"] = "";
            obj["color"] = "";
            obj["area"] = -1;
            obj["centro_x"] = -1;
            obj["centro_y"] = -1;
            obj["bbox_x"] = -1;
            obj["bbox_y"] = -1;
            obj["bbox_w"] = -1;
            obj["bbox_h"] = -1;
            obj["perimetro"] = -1;
        }
    }
    
    // Agregar información de figuras calibradas
    JsonArray figuras = doc.createNestedArray("figuras_calibradas");
    for (int i = 0; i < 5; i++) {
        if (figuras_calibradas[i].calibrada) {
            JsonObject figura = figuras.createNestedObject();
            figura["nombre"] = figuras_calibradas[i].nombre;
            figura["muestras"] = figuras_calibradas[i].muestras;
        }
    }
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

// Función auxiliar para enviar frames de fallback
static void send_fallback_frame(httpd_req_t *req, camera_fb_t *fb) {
    httpd_resp_send_chunk(req, "--frame\r\n", 10);
    httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
    httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    
    // Metadata
    char metadata[100];
    snprintf(metadata, sizeof(metadata), 
            "\r\nX-Detection-Data: {\"figura\":\"%s\",\"confianza\":%.2f}\r\n", 
            ultima_figura, ultima_confianza);
    httpd_resp_send_chunk(req, metadata, strlen(metadata));
}

// Endpoint: /processed_stream (GET)
static esp_err_t processed_stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    
    Serial.println("🚀 Iniciando processed stream optimizado");
    
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if(res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Buffers reutilizables
    uint8_t* grayscale = NULL;
    uint8_t* rgb_buffer = NULL;
    size_t grayscale_len = 0;
    bool buffers_initialized = false;
    uint8_t* rgb565_buffer = NULL;

    while(true) {

        switch(modo_actual) {
          
          case MODO_DETECCION:
            // Continúa con la lógica normal de detección
            break; // Sigue
            
          case MODO_CALIBRACION:
            // Estamos calibrando. Solo muestra el video original.
            fb = esp_camera_fb_get();
            if (!fb) { delay(1); continue; }
            send_fallback_frame(req, fb); // Envía el JPEG original
            esp_camera_fb_return(fb);
            if(res != ESP_OK) break;
            continue; // Vuelve al inicio del while
            
          case MODO_IDLE:
          default:
            // Estamos detenidos.
            delay(100); // No hacer nada, esperar cambio de modo
            continue; // Vuelve al inicio del while
        }
        
        fb = esp_camera_fb_get();
        if (!fb) {
            delay(1);
            continue;
        }

        // Inicializar buffers una sola vez
        if (!buffers_initialized) {
            grayscale_len = fb->width * fb->height;
            grayscale = (uint8_t*)ps_malloc(grayscale_len);
            rgb_buffer = (uint8_t*)ps_malloc(grayscale_len * 3); // RGB888
            rgb565_buffer = (uint8_t*)ps_malloc(grayscale_len * 2); // <-- MEMORIA ASIGNADA AQUÍ
            if (grayscale && rgb_buffer) {
                buffers_initialized = true;
                Serial.println("✅ Buffers de Stream listos.");
            }else {
                Serial.println("❌ Error crítico: No se pudo alocar memoria para los buffers del stream.");
                if(grayscale) free(grayscale);
                if(rgb_buffer) free(rgb_buffer);
                if(rgb565_buffer) free(rgb565_buffer);
                buffers_initialized = false;
                esp_camera_fb_return(fb);
                break; // Salir del bucle
            }
        }

        if (fb->format == PIXFORMAT_JPEG && buffers_initialized) {
            //size_t rgb565_len = fb->width * fb->height * 2;
            //uint8_t* rgb565_buffer = (uint8_t*)ps_malloc(rgb565_len);
            
            if (rgb565_buffer && jpg2rgb565(fb->buf, fb->len, rgb565_buffer, JPG_SCALE_NONE)) {
                // Conversión RGB565 a Grayscale
                uint16_t* pixels = (uint16_t*)rgb565_buffer;
                for (int i = 0; i < grayscale_len; i++) {
                    uint16_t pixel = pixels[i];
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    grayscale[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
                }

                // 🔥 PROCESAMIENTO OPTIMIZADO: Solo filtros esenciales
                aplicar_filtros_completos(grayscale, grayscale, fb->width, fb->height, threshold_value);
                
                // 🔥 Detección cada 3 frames para mejor rendimiento
                //static int frame_count = 0;
                //if (frame_count++ % 3 == 0) {
                 //   detectar_figura_mejorada(grayscale, fb->width, fb->height, ultima_figura, &ultima_confianza);
                //}

                // === DETECCIÓN DE MÚLTIPLES OBJETOS ===
                memset(labels, 0, sizeof(int) * fb->width * fb->height);
                int label_actual = 1;
                num_objetos = 0; // Resetea el contador global
                
                const int CROP_MARGIN = 10; // Definimos el margen
                for (int y = CROP_MARGIN; y < fb->height - CROP_MARGIN; y++) { // Añadido margen
                /*for (int y = 0; y < fb->height; y++) {*/
                  //for (int x = 0; x < fb->width; x++) {
                  for (int x = CROP_MARGIN; x < fb->width - CROP_MARGIN; x++) { // Añadido margen
                    // Busca un píxel de objeto (NEGRO) que no haya sido visitado
                    if (grayscale[y * fb->width + x] == 0 && labels[y * fb->width + x] == 0) {

                      // 1. EnconFtramos un objeto, lo "pintamos"
                      floodFill(grayscale, labels, fb->width, fb->height, x, y, label_actual);

                      // 2. Analizamos el objeto que acabamos de pintar
                      if (num_objetos < MAX_OBJETOS) {
                        // Llama a tu 'analizar_objeto' (modificado con Hu Moments)
                        Objeto obj = analizar_objeto(labels, label_actual, fb->width, fb->height,(uint16_t*)rgb565_buffer); 

                        if (obj.forma != "RUIDO") {
                          objetos_detectados[num_objetos++] = obj;
                        }
                      }
                      label_actual++;
                    }
                  }
                }


                delay(50);

                                // --- Actualizar datos para el stream ---
                // ESTO DEBE ESTAR JUSTO DESPUÉS DE LA DETECCIÓN DE OBJETOS
                if (num_objetos > 0) {
                    // Tomar el primer objeto detectado (o el más grande si quieres)
                    strncpy(ultima_figura, objetos_detectados[0].forma.c_str(), 19);
                    ultima_figura[19] = '\0'; // Asegurar terminación
                    
                    strncpy(ultimo_color, objetos_detectados[0].color.c_str(), 19);
                    ultimo_color[19] = '\0'; // Asegurar terminación
                    
                    ultima_confianza = 0.85; // Ajustar según tu lógica
                    ultimo_cx = objetos_detectados[0].centro_x;
                    ultimo_cy = objetos_detectados[0].centro_y;
                    
                    // Debug para verificar
                    Serial.printf("🔄 Variables actualizadas: %s %s (%.1f, %.1f)\n", 
                                  ultima_figura, ultimo_color, ultimo_cx, ultimo_cy);
                } else {
                    strncpy(ultima_figura, "Ninguna", 19);
                    strncpy(ultimo_color, "N/A", 19);
                    ultima_confianza = 0.0;
                    ultimo_cx = -1;
                    ultimo_cy = -1;
                }
                // --- Enviar evento SSE con la detección para actualizar la UI en tiempo real ---
{
    // Preparar campos de bbox y frame (si hay objetos detectados)
    int x_min = -1, y_min = -1, x_max = -1, y_max = -1;
    int frame_w = fb->width;
    int frame_h = fb->height;

    if (num_objetos > 0) {
        x_min = objetos_detectados[0].bbox_x;
        y_min = objetos_detectados[0].bbox_y;
        x_max = objetos_detectados[0].bbox_x + objetos_detectados[0].bbox_w - 1;
        y_max = objetos_detectados[0].bbox_y + objetos_detectados[0].bbox_h - 1;
    }

    char sse_json[256];
    // Asegúrate de no exceder el tamaño del buffer (ajusta 256 si necesitás más)
    int n = snprintf(sse_json, sizeof(sse_json),
        "{\"figura\":\"%s\",\"color\":\"%s\",\"confianza\":%.2f,\"cx\":%d,\"cy\":%d,"
        "\"frame_width\":%d,\"frame_height\":%d,\"x_min\":%d,\"y_min\":%d,\"x_max\":%d,\"y_max\":%d}",
        ultima_figura, ultimo_color, ultima_confianza,
        (int)round(ultimo_cx), (int)round(ultimo_cy),
        frame_w, frame_h, x_min, y_min, x_max, y_max);

    if (n > 0 && n < (int)sizeof(sse_json)) {
        sse_broadcast_json(sse_json);
    } else {
        Serial.println("⚠ SSE JSON truncated or error");
    }
}
    
                // Convertir imagen binaria a RGB para pintar centroide
                for (int i = 0; i < grayscale_len; i++) {
                    int rgb_index = i * 3;
                    uint8_t pixel_val = grayscale[i];
                    // Convertir binario a RGB (blanco y negro)
                    rgb_buffer[rgb_index] = pixel_val;     // R
                    rgb_buffer[rgb_index + 1] = pixel_val; // G
                    rgb_buffer[rgb_index + 2] = pixel_val; // B
                }
                

                // AGREGADO PARA MULTIPLES OBJETOS
                // AGREGADO PARA MULTIPLES OBJETOS CON ETIQUETAS
                for (int i = 0; i < num_objetos; i++) {
                  if (objetos_detectados[i].centro_x >= 0 && objetos_detectados[i].centro_y >= 0) {
                    pintar_centroide_con_etiqueta(rgb_buffer, fb->width, fb->height, 
                                                objetos_detectados[i].centro_x, 
                                                objetos_detectados[i].centro_y,
                                                objetos_detectados[i].forma.c_str(),
                                                objetos_detectados[i].color.c_str());
                  }
                }
                                
                Serial.println("\n=== OBJETOS DETECTADOS ===");
                for (int i = 0; i < num_objetos; i++) {
                   Serial.printf("Objeto %d: Forma=%s  Color=%s  Centro=(%d,%d)\n",
                   i+1,
                   objetos_detectados[i].forma.c_str(),
                   objetos_detectados[i].color.c_str(),
                   objetos_detectados[i].centro_x,
                   objetos_detectados[i].centro_y);
                }

                // Convertir RGB a JPEG para streaming
                size_t jpg_len = 0;
                uint8_t* jpg_buffer = NULL;
                
                if (fmt2jpg(rgb_buffer, grayscale_len * 3, fb->width, fb->height, 
                           PIXFORMAT_RGB888, 70, &jpg_buffer, &jpg_len)) {
                    
                    // Enviar frame
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "--frame\r\n", 10);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpg_buffer, jpg_len);
                    
                    // Metadata
                    char metadata[150];
                    snprintf(metadata, sizeof(metadata), 
                            "\r\nX-Detection-Data: {\"figura\":\"%s\",\"color\":\"%s\",\"confianza\":%.2f,\"centroide_x\":%.1f,\"centroide_y\":%.1f}\r\n", 
                            ultima_figura, ultimo_color, ultima_confianza, ultimo_cx, ultimo_cy);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, metadata, strlen(metadata));
                    
                    free(jpg_buffer);
                } else {
                    send_fallback_frame(req, fb);
                }
                
               // free(rgb565_buffer);
            } else {
               // if (rgb565_buffer) free(rgb565_buffer);
                send_fallback_frame(req, fb);
            }
        } else {
            // Frame original sin procesar
            send_fallback_frame(req, fb);
        }
        esp_camera_fb_return(fb);
        if(res != ESP_OK) break;
    }
    
    // Limpieza
    if (grayscale) free(grayscale);
    if (rgb_buffer) free(rgb_buffer);
    if (rgb565_buffer) free(rgb565_buffer);

    Serial.println("🛑 Processed stream terminado");
    return res;
}

// Endpoint: /calibrate (POST)
static esp_err_t calibrate_handler(httpd_req_t *req) {
    Serial.printf("\n====== INICIANDO CALIBRACIÓN ======\n");
    Serial.printf("Heap libre: %d bytes\n", esp_get_free_heap_size());
    
    set_cors_headers(req);

    // Manejar preflight OPTIONS
    if (req->method == HTTP_OPTIONS) {
        Serial.println("🔄 Preflight OPTIONS recibido");
        return httpd_resp_send(req, NULL, 0);
    }
    
    if (req->method != HTTP_POST) {
        Serial.println("❌ Método no permitido");
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, NULL, 0);
    }
    
    
    if (modo_actual != MODO_CALIBRACION || figura_a_calibrar == -1) {
        Serial.println("❌ Error: Se recibió /calibrate pero no se estaba en MODO_CALIBRACION.");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Error: Iniciar calibracion desde Serial primero", HTTPD_RESP_USE_STRLEN);
    }

    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    
    if (ret <= 0) {
        Serial.println("❌ Error recibiendo datos");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Error recibiendo datos", HTTPD_RESP_USE_STRLEN);
    }
    

    buf[ret] = '\0';
    Serial.printf("📦 Datos recibidos: %s\n", buf);
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, buf);
    
    if (error) {
        Serial.printf("❌ Error JSON: %s\n", error.c_str());
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Error JSON", HTTPD_RESP_USE_STRLEN);
    }
    
    if (!doc.containsKey("figure")) {
        Serial.println("❌ Falta campo 'figure'");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "Falta campo 'figure'", HTTPD_RESP_USE_STRLEN);
    }
    
    int figure_id = doc["figure"];
    Serial.printf("🎯 Calibrando figura ID: %d\n", figure_id);
    
    if (figure_id < 0 || figure_id >= 5) {
        Serial.println("❌ ID de figura inválido");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "ID inválido", HTTPD_RESP_USE_STRLEN);
    }

    calibrating = true;
    bool calibracion_exitosa = calibrar_con_promedio(figure_id);
    calibrating = false;

    // Respuesta
    StaticJsonDocument<256> response_doc;
    if (calibracion_exitosa) {
        response_doc["status"] = "ok";
        response_doc["figure_id"] = figure_id;
        response_doc["figure_name"] = figuras_calibradas[figure_id].nombre;
        response_doc["muestras"] = figuras_calibradas[figure_id].muestras;
        response_doc["message"] = "Calibración exitosa";
        
        Serial.println("✅ Calibración exitosa");
        Serial.println("Figuras calibradas:");
        for (int i = 0; i < 5; i++) {
            if (figuras_calibradas[i].calibrada) {
                Serial.printf(" - %s (%d muestras)\n", 
                            figuras_calibradas[i].nombre, 
                            figuras_calibradas[i].muestras);
            }
        }
    } else {
        response_doc["status"] = "error";
        response_doc["message"] = "Error en calibración - objeto no detectado";
        Serial.println("❌ Calibración falló");
    }
    
    String response;
    serializeJson(response_doc, response);
    
    httpd_resp_set_type(req, "application/json");

    modo_actual = MODO_IDLE;
    figura_a_calibrar = -1;    
    Serial.printf("Heap libre después: %d bytes\n", esp_get_free_heap_size());
    Serial.println("====== FIN CALIBRACIÓN (Volviendo a MODO IDLE) ======\n");
    return httpd_resp_send(req, response.c_str(), response.length());
}


/*
// Endpoint: /detect (GET)
static esp_err_t detect_handler(httpd_req_t *req) {
    set_cors_headers(req);

    StaticJsonDocument<256> doc;
    doc["figura"] = ultima_figura;
    doc["color"] = ultimo_color;  // <-- AGREGADO
    doc["confianza"] = ultima_confianza;
    doc["centroide_x"] = ultimo_cx;
    doc["centroide_y"] = ultimo_cy;
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}
*/

// Endpoint: /detect (GET) - Devuelve todos los objetos detectados
static esp_err_t detect_handler(httpd_req_t *req) {
    set_cors_headers(req);

    StaticJsonDocument<1024> doc;
    JsonArray objetos_array = doc.createNestedArray("objetos");
    
    for (int i = 0; i < num_objetos; i++) {
        JsonObject obj = objetos_array.createNestedObject();
        obj["id"] = objetos_detectados[i].id;
        obj["area"] = objetos_detectados[i].area;
        obj["centro_x"] = objetos_detectados[i].centro_x;
        obj["centro_y"] = objetos_detectados[i].centro_y;
        obj["bbox_x"] = objetos_detectados[i].bbox_x;
        obj["bbox_y"] = objetos_detectados[i].bbox_y;
        obj["bbox_w"] = objetos_detectados[i].bbox_w;
        obj["bbox_h"] = objetos_detectados[i].bbox_h;
        obj["forma"] = objetos_detectados[i].forma;
        obj["color"] = objetos_detectados[i].color;
    }
    
    doc["total_objetos"] = num_objetos;
    doc["timestamp"] = millis();
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

static esp_err_t sse_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (!sse_add_client(req)) {
        const char *too_many = "data: {\"error\":\"too_many_clients\"}\n\n";
        httpd_resp_send(req, too_many, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_SSE, "SSE client connected");
    const char *init_msg = "data: {\"event\":\"connected\"}\n\n";
    httpd_resp_send_chunk(req, init_msg, strlen(init_msg));

    // Mantener la conexión abierta enviando un "ping" periódico para detectar desconexión
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        esp_err_t err = httpd_resp_send_chunk(req, ": ping\n\n", strlen(": ping\n\n"));
        if (err != ESP_OK) {
            ESP_LOGI(TAG_SSE, "SSE client disconnected");
            break;
        }
    }

    // limpiar y terminar
    sse_remove_client(req);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG_SSE, "SSE client handler finished");
    return ESP_OK;
}

// Endpoint: /data (alias de /detect)
static esp_err_t data_handler(httpd_req_t *req) {
    return detect_handler(req);
}

// Handler para requests OPTIONS global
static esp_err_t options_handler(httpd_req_t *req) {
    set_cors_headers(req);
    return httpd_resp_send(req, NULL, 0);
}

// Endpoint: / (GET) - Página principal
static esp_err_t index_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));
}

// ===== CONFIGURACIÓN SERVIDOR =====
void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };

    httpd_uri_t processed_stream_uri = {
        .uri = "/processed_stream",
        .method = HTTP_GET,
        .handler = processed_stream_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t calibrate_uri = {
        .uri = "/calibrate",
        .method = HTTP_POST,
        .handler = calibrate_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t calibrate_options_uri = {
        .uri = "/calibrate",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t detect_uri = {
        .uri = "/detect",
        .method = HTTP_GET,
        .handler = detect_handler,
        .user_ctx = NULL
    };
    
    httpd_uri_t data_uri = {
        .uri = "/data",
        .method = HTTP_GET,
        .handler = data_handler,
        .user_ctx = NULL
    };

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
    };
    


    // 🚀 INICIO DEL SERVIDOR
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {

        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_options_uri);
        httpd_register_uri_handler(camera_httpd, &processed_stream_uri);
        httpd_register_uri_handler(camera_httpd, &detect_uri);
        httpd_register_uri_handler(camera_httpd, &data_uri);
        httpd_register_uri_handler(camera_httpd, &index_uri);

        // 🔥🔥🔥 PEGÁ ESTO ACÁ 🔥🔥🔥
        sse_init_clients();

        httpd_uri_t sse_uri = {
            .uri       = "/events",
            .method    = HTTP_GET,
            .handler   = sse_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &sse_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        // 🔥🔥🔥 FIN DEL CÓDIGO A PEGAR 🔥🔥🔥

        Serial.println("✅ Servidor web iniciado correctamente");
    } else {
        Serial.println("❌ Error iniciando servidor web");
    }
}


// Endpoint: /capture (GET) - Captura un frame y devuelve JSON con detecciones
static esp_err_t capture_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Procesar el frame (similar a processed_stream_handler pero sin loop)
    size_t grayscale_len = fb->width * fb->height;
    uint8_t* grayscale = (uint8_t*)ps_malloc(grayscale_len);
    uint8_t* rgb_buffer = (uint8_t*)ps_malloc(grayscale_len * 3);
    uint8_t* rgb565_buffer = (uint8_t*)ps_malloc(grayscale_len * 2);
    
    StaticJsonDocument<1024> json_doc;
    JsonArray objetos_array = json_doc.createNestedArray("objetos");
    
    if (grayscale && rgb_buffer && rgb565_buffer && 
        jpg2rgb565(fb->buf, fb->len, rgb565_buffer, JPG_SCALE_NONE)) {
        
        // Conversión a escala de grises
        uint16_t* pixels = (uint16_t*)rgb565_buffer;
        for (int i = 0; i < grayscale_len; i++) {
            uint16_t pixel = pixels[i];
            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            uint8_t b = (pixel & 0x1F) << 3;
            grayscale[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
        }
        
        // Aplicar filtros
        aplicar_filtros_completos(grayscale, grayscale, fb->width, fb->height, threshold_value);
        
        // Detectar objetos
        memset(labels, 0, sizeof(int) * fb->width * fb->height);
        int label_actual = 1;
        num_objetos = 0;
        
        const int CROP_MARGIN = 10;
        for (int y = CROP_MARGIN; y < fb->height - CROP_MARGIN; y++) {
            for (int x = CROP_MARGIN; x < fb->width - CROP_MARGIN; x++) {
                if (grayscale[y * fb->width + x] == 0 && labels[y * fb->width + x] == 0) {
                    floodFill(grayscale, labels, fb->width, fb->height, x, y, label_actual);
                    
                    if (num_objetos < MAX_OBJETOS) {
                        Objeto obj = analizar_objeto(labels, label_actual, fb->width, fb->height, (uint16_t*)rgb565_buffer);
                        
                        if (obj.forma != "RUIDO") {
                            objetos_detectados[num_objetos++] = obj;
                            
                            // Agregar al JSON
                            JsonObject obj_json = objetos_array.createNestedObject();
                            obj_json["id"] = obj.id;
                            obj_json["area"] = obj.area;
                            obj_json["centro_x"] = obj.centro_x;
                            obj_json["centro_y"] = obj.centro_y;
                            obj_json["bbox_x"] = obj.bbox_x;
                            obj_json["bbox_y"] = obj.bbox_y;
                            obj_json["bbox_w"] = obj.bbox_w;
                            obj_json["bbox_h"] = obj.bbox_h;
                            obj_json["forma"] = obj.forma;
                            obj_json["color"] = obj.color;
                        }
                    }
                    label_actual++;
                }
            }
        }
    }
    
    // Convertir imagen a JPEG para enviar
    size_t jpg_len = 0;
    uint8_t* jpg_buffer = NULL;
    
    // Primero intentar con la imagen procesada
    if (fmt2jpg(rgb_buffer, grayscale_len * 3, fb->width, fb->height, 
                PIXFORMAT_RGB888, 70, &jpg_buffer, &jpg_len)) {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_send(req, (const char*)jpg_buffer, jpg_len);
        free(jpg_buffer);
    } else {
        // Fallback: enviar el frame original
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_send(req, (const char*)fb->buf, fb->len);
    }
    
    // Liberar memoria
    if (grayscale) free(grayscale);
    if (rgb_buffer) free(rgb_buffer);
    if (rgb565_buffer) free(rgb565_buffer);
    esp_camera_fb_return(fb);
    
    return ESP_OK;
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  ESP32-CAM DETECTOR OPTIMIZADO      ║");
    Serial.println("║   MOMENTOS HU MEJORADOS             ║");
    Serial.println("║   FLOOD FILL       MAX     CROP Tomas      ║");
    Serial.println("╚══════════════════════════════════════╝\n");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // WiFi
    WiFi.begin(ssid, password);
    Serial.print("📶 Conectando WiFi");
    int wifi_timeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20) {
        delay(500);
        Serial.print(".");
        wifi_timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi conectado");
        Serial.print("📱 IP: http://");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ Error WiFi - Continuando sin conexión");
    }
    
    // SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("⚠ Error SPIFFS - Continuando sin filesystem");
    } else {
        Serial.println("✅ SPIFFS OK");
    }
    
    // Verificar PSRAM
    if (!psramFound()) {
        Serial.println("⚠ PSRAM no encontrada - Usando RAM interna");
    } else {
        Serial.printf("✅ PSRAM: %d KB libres\n", ESP.getFreePsram() / 1024);
    }
    
    // Configuración cámara
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;   // 320x240
    config.jpeg_quality = 10;              // Calidad media-baja
    config.fb_count = 2;
    
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("❌ Error cámara - Reiniciando...");
        delay(1000);
        ESP.restart();
    }
    Serial.println("✅ Cámara inicializada");

    // Configuración sensor
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    s->set_brightness(s, 1);     // Aumentar brillo ligeramente
    s->set_contrast(s, 1);       // Aumentar contraste ligeramente

    // Asignar buffers (del tamaño de la captura QVGA)
   size_t buf_len = 320 * 240;
   imagen_procesada = (uint8_t*)ps_malloc(buf_len);
  
   // --- ¡ESTA ES LA LÍNEA CRÍTICA QUE FALTABA! ---
   labels = (int*)ps_malloc(buf_len * sizeof(int));
    // ---------------------------------------------

  if (!imagen_procesada || !labels) { // Chequeo mejorado
    Serial.println("❌ Error crítico asignando buffers - Reiniciando...");
    ESP.restart();
  }
  Serial.println("✅ Buffers de procesamiento listos");
    
  /*  if (imagen_procesada) {
        Serial.println("✅ Buffer de procesamiento listo");
    } else {
        Serial.println("❌ Error crítico buffer - Reiniciando...");
        ESP.restart();
    }*/
    
    startCameraServer();
    
    Serial.println("\n✅ SISTEMA LISTO");
    Serial.println("════════════════════════════════════");
    Serial.println("Endpoints disponibles:");
    Serial.println("  GET  /status           - Estado del sistema");
    Serial.println("  POST /calibrate        - Calibrar figura");
    Serial.println("  GET  /detect           - Última detección");
    Serial.println("  GET  /data             - Alias de /detect");
    Serial.println("  GET  /processed_stream - Video procesado con centroide");
    Serial.println("════════════════════════════════════");
    Serial.printf("Memoria libre: %d bytes\n", esp_get_free_heap_size());
    Serial.println("════════════════════════════════════\n");
}

// ===== LOOP PRINCIPAL =====
void loop() {
    if (Serial.available() > 0) {
    char cmd = Serial.read();

    if (cmd >= '1' && cmd <= '5') {
      // --- Iniciar Calibración ---
      int fig_id = cmd - '1'; // Convierte '1' a 0, '2' a 1, etc.
      figura_a_calibrar = fig_id;
      modo_actual = MODO_CALIBRACION;
      Serial.printf("\n=== MODO CALIBRACION: %s ===\n", figuras_calibradas[fig_id].nombre);
      Serial.println("Ponga el objeto SOLO y abra la app para calibrar.");

    } else if (cmd == 'd' || cmd == 'D') {
      // --- Iniciar Detección ---
      modo_actual = MODO_DETECCION;
      Serial.println("\n=== MODO DETECCION ACTIVADO ===");

    } else if (cmd == '0') {
      // --- Modo IDLE (Detener) ---
      modo_actual = MODO_IDLE;
      Serial.println("\n=== MODO IDLE (DETENIDO) ===");
    }
    }


    // Mantener el sistema estable
    static unsigned long last_mem_check = 0;
    if (millis() - last_mem_check > 30000) { // Cada 30 segundos
        last_mem_check = millis();
        Serial.printf("💾 Memoria libre: %d bytes\n", esp_get_free_heap_size());
        // Verificar estado WiFi
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("🔁 Reconectando WiFi...");
            WiFi.reconnect();
        }
    }
    delay(500);
  
}