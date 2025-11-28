/*
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

// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "Sandra Wifi 2.4";// "iPhone de Tomás";//"Sandra Wifi 2.4";
const char* password =  "0142265207";// "tomas2002";//"0142265207";

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

httpd_handle_t camera_httpd = NULL; // Handle del servidor HTTP de la cámara. Se usa para iniciar y manejar
// las rutas HTTP que permiten transmitir la imagen en tiempo real.

// Variables globales
bool calibrating = false; // Indica si el sistema está actualmente en proceso de calibración
int current_calibration_figure = -1; // Almacena el ID de la figura que se está calibrando. -1 significa que no hay una figura seleccionada para calibración.
int photo_count = 70; // Cantidad de fotos a capturar durante la calibración de una figura.
uint8_t threshold_value = 75; // Umbral de binarización. Se usa para separar el fondo del objeto. 0 <-- Más blanco  255 <-- Más negro
char ultima_figura[20] = "Ninguna"; // Guarda el nombre de la última figura detectada.
char ultimo_color[20] = "N/A"; // Guarda el color detectado de la última figura.
double ultima_confianza = 0.0; // Nivel de confianza en la clasificación de la última figura detectada.

// Buffer para imagen procesada
uint8_t* imagen_procesada = nullptr; // Imagen binarizada espués de aplicar threshold.
int* labels = nullptr;  // Array para etiquetar componentes conectados. Cada píxel almacena un ID para saber a qué figura pertenece.

// --- Máquina de Estados ---
enum ModoOperacion {
  MODO_DETECCION,  // Modo deteccion: analizar imagen, detectar figuras y colores
  MODO_CALIBRACION, // Modo Calibracion: recolectar datos para calibrar una figura
  MODO_IDLE        // Sistema en reposo: no procesa imágenes
};

volatile ModoOperacion modo_actual = MODO_IDLE; // Estado actual del sistema. Inicia en modo reposo.
volatile int figura_a_calibrar = -1; // Qué figura vamos a calibrar (0-4)


// Coordenadas (x, y) del centroide de la última figura detectada.
double ultimo_cx = -1;
double ultimo_cy = -1;

// Estructura para almacebar los datos de las figuras
typedef struct {
    double hu[7]; // Vector con los 7 Momentos de Hu modelo para esta figura
    const char* nombre; // Nombre de la figura 
    bool calibrada; // Indica si ya tiene valores de Hu válidos
    int muestras;  // Cantidad de muestras usadas en la calibración
} FiguraCalibrada;

// ===========================================
//  Base de datos de 5 figuras posibles
// ===========================================
// Cada entrada contiene los Hu característicos y su nombre.
// Si calibrada = false => se deben obtener nuevos Hu.
// Si calibrada = true  => ya está lista para comparar.

FiguraCalibrada figuras_calibradas[5] = {
    {{-1.791, -13.305, -14.275, -12.958, -26.598, -20.774, 0.00}, "Cuadrado", true, 10},
    {{-1.836, -10.910, -13.538, -11.661, -25.362, -17.392, -24.350}, "Circulo", true, 10},
    {{0}, "L", false, 0},
    {{0}, "Estrella", false, 0},
    {{-1.640, -7.158, -5.372, -8.911, -16.059, -12.502, -18.305}, "C", true, 10}
};

// Estructura para objetos detectados
struct Objeto {
  int id;                    // Identificador del objeto
  int area;                  // Cantidad de píxeles que forman el objeto
  int perimetro;             // Perímetro aproximado (se calcula al recorrer el contorno)
  float circularidad;        // Métrica de circularidad = (4πA) / P²
  int centro_x, centro_y;    // Centroide del objeto
  int bbox_x, bbox_y;        // Esquina superior izquierda del bounding box
  int bbox_w, bbox_h;        // Ancho y alto del bounding box
  String forma;              // Nombre de la figura detectada 
  String color;              // Color detectado del objeto 
};


const int MIN_AREA = 500;         // área mínima para considerar un objeto. Esto elimina ruido, manchas pequeñas o píxeles aislados.
const float HU_SIM_SCALE = 0.3f;  // Escala de sensibilidad para comparar HU moments. Valores más bajos → comparación más estricta → mejor precisión en figuras similares.


const int MAX_OBJETOS = 3; // Máximo de figuras que se guardan por frame.
Objeto objetos_detectados[MAX_OBJETOS]; // Arreglo donde se guardarán los objetos detectados en un frame.
int num_objetos = 0; // Cantidad actual de objetos detectados en este frame.

// ===== ESTRUCTURA PARA FLOOD FILL =====
struct Punto { int x, y; };

// ====================================================
//   Flood Fill ITERATIVO (sin recursión)
// ====================================================
// Etiqueta una región conectada y asigna un ID a todos los píxeles que la forman.
// img     → imagen binaria (0 = fondo, 255 = figura)
// labels  → matriz paralela donde se guardan las etiquetas de componentes
// w,h     → ancho y alto de la imagen
// sx,sy   → punto inicial (semilla)
// label   → etiqueta que se asignará al objeto detectado

void floodFill(uint8_t* img, int* labels, int w, int h, int sx, int sy, int label) {
 
  const int MAX_Q = 100000;  // Tamaño máximo de la cola.
  Punto* q = (Punto*)malloc(sizeof(Punto) * MAX_Q); // Cola (FIFO) de puntos a procesar.
  if (!q) return; // Si falla el malloc, aborta el flood fill.
  
  int qs = 0, qe = 0;
  // qs = índice de lectura (inicio de la cola)
  // qe = índice de escritura (final de la cola)
  
  q[qe++] = {sx, sy};
  
  while (qs < qe) {
     Punto p = q[qs++];
      // Sacamos el siguiente punto de la cola (pop FIFO).

      // Si el punto está fuera de los límites de la imagen, se ignora.
      if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h) continue;

      int idx = p.y * w + p.x;
      // Índice lineal del píxel dentro del buffer img[] y labels[].

      // Verificación del píxel:
      // - Si labels[idx] != 0, este píxel ya fue etiquetado.
      // - Si img[idx] != 0, el píxel no es blanco (0), por tanto, es fondo y no es parte del objeto.
      if (labels[idx] != 0 || img[idx] != 0) continue;

      labels[idx] = label;
      // Se asigna la etiqueta al píxel, marcándolo como parte del objeto actual.

      // Encolar vecinos (4-conectividad) si hay espacio en la cola.
      if (qe < MAX_Q - 4) {
        q[qe++] = {p.x + 1, p.y};   // derecha
        q[qe++] = {p.x - 1, p.y};   // izquierda
        q[qe++] = {p.x, p.y + 1};   // abajo
        q[qe++] = {p.x, p.y - 1};   // arriba
      }
  }

  free(q);
  // Se libera la memoria de la cola una vez completado el flood fill.
}
// ===== ANALIZAR OBJETO =====
Objeto analizar_objeto(int* labels, int label, int w, int h, uint16_t* rgb_buf) {
  Objeto obj;
  obj.id = label;               // Identificador del objeto (coincide con flood fill)
  obj.area = 0;                 // Contador de pixeles del objeto
  obj.perimetro = 0;            // (Se calcula luego)
  obj.centro_x = 0;             // Se acumulan para centroides
  obj.centro_y = 0;
  obj.bbox_x = w;               // Inicializados al máximo para encontrar mínimos reales
  obj.bbox_y = h;
  obj.bbox_w = 0;               // Inicializados a 0 para encontrar máximos reales
  obj.bbox_h = 0;
  //obj.color = "N/A";          // Si no se calcula el color, queda N/A

  long sum_x = 0, sum_y = 0; // Suma acumulada de los píxeles del objeto para obtener el centroide.

  // PRIMERA PASADA: calcular área, centroide y bounding box

  const int CROP_MARGIN = 10;  // Margen para ignorar bordes de la imagen donde suele haber ruido o sombras.

  for (int y = CROP_MARGIN; y < h - CROP_MARGIN; y++) {
    for (int x = CROP_MARGIN; x < w - CROP_MARGIN; x++) {

      if (labels[y * w + x] == label) {
      // Si el pixel pertenece a este objeto...

        obj.area++;          // Aumentamos el área del objeto
        sum_x += x;          // Acumulamos X para el centroide
        sum_y += y;          // Acumulamos Y

        // Actualizamos bounding box
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
  // Si el objeto tiene muy pocos píxeles, lo consideramos ruido y salimos.

  // Calcular centroide a partir de las sumas acumuladas
  obj.centro_x = sum_x / obj.area;
  obj.centro_y = sum_y / obj.area;
  
  // centro_x/centro_y los convertimos a double oara los cálculos de momentos que requieren precisión.
  double cx = (double)obj.centro_x;
  double cy = (double)obj.centro_y;

  // Convertir las coordenadas máximas/minimas a ancho/alto del bbox
  obj.bbox_w = obj.bbox_w - obj.bbox_x + 1;
  obj.bbox_h = obj.bbox_h - obj.bbox_y + 1;
  
 // --- ¡NUEVA LÓGICA! ---
  // 1. Calcular Momentos Centrales (muXX)
  // Inicializo acumuladores para momentos centrales hasta orden 3
  double mu20 = 0, mu02 = 0, mu11 = 0;
  double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;

  // Recorro la región entera y solo sumo para los píxeles que pertenecen a ESTE objeto (labels == label)
  for (int y = CROP_MARGIN; y < h - CROP_MARGIN; y++) {
    for (int x = CROP_MARGIN; x < w - CROP_MARGIN; x++) {
      // Solo analizamos los píxeles de ESTE objeto
      if (labels[y * w + x] == label) {
        // desplazamientos respecto al centroide
        double dx = (x - cx);
        double dy = (y - cy);
        
        // Acumular momentos centrales: mu_pq = Σ (dx^p * dy^q)
        mu20 += dx * dx;             // p=2,q=0
        mu02 += dy * dy;             // p=0,q=2
        mu11 += dx * dy;             // p=1,q=1
        mu30 += dx * dx * dx;        // p=3,q=0
        mu03 += dy * dy * dy;        // p=0,q=3
        mu21 += dx * dx * dy;        // p=2,q=1
        mu12 += dx * dy * dy;        // p=1,q=2
      }
    }
  }

  // Normalización y Cálculo de Hu Moments
  // m00 = área total (equivalente al momento de orden 0)
  double m00 = (double)obj.area;
  // area_sq = m00^2  (usado para normalizar momentos de orden 2)
  double area_sq = m00 * m00;
  // area_5_2 = m00^(5/2) (usado para normalizar momentos de orden 3)
  double area_5_2 = pow(m00, 2.5);
  
  // Normalizar los momentos centrales para obtener momentos "nu" invariantes a escala
  double nu20 = mu20 / area_sq;
  double nu02 = mu02 / area_sq;
  double nu11 = mu11 / area_sq;
  double nu30 = mu30 / area_5_2;
  double nu03 = mu03 / area_5_2;
  double nu21 = mu21 / area_5_2;
  double nu12 = mu12 / area_5_2;

  // Calcular los 7 Momentos de Hu a partir de los nuXX
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

  // Log-transform para estabilizar la magnitud y comparar hu moments
  for (int i = 0; i < 7; i++) {
    if (fabs(hu_actual[i]) > 1e-12) {
      hu_actual[i] = log(fabs(hu_actual[i]));
    } else {
      hu_actual[i] = 0.0;
    }
  }

  // Imprimir por Serial los Hu calculados para debugging/registro
 Serial.printf("Hu[0]=%.3f, Hu[1]=%.3f, Hu[2]=%.3f, Hu[3]=%.3f, Hu[4]=%.3f, Hu[5]=%.3f, Hu[6]=%.3f\n", hu_actual[0], hu_actual[1], hu_actual[2], hu_actual[3], hu_actual[4], hu_actual[5], hu_actual[6]);

 // 3. Clasificar
double mejor_sim = 0.00000;   // Mejor similitud encontrada hasta ahora (inicializada a 0)
int mejor_idx = -1;           // Índice de la figura calibrada que mejor coincide

// Recorremos la "base" de figuras calibradas para encontrar la mejor coincidencia
for (int i = 0; i < 5; i++) {
  if (figuras_calibradas[i].calibrada) {
    // calcular_similitud_hu_optimizada compara hu_actual con la hu modelo
    // y devuelve una medida de similitud (mayor = más parecido).
    double sim = calcular_similitud_hu_optimizada(hu_actual, figuras_calibradas[i].hu);

    // Imprime por serial la similitud encontrada y los Hu de la figura calibrada
    /*Serial.printf("  vs %s: sim=%.3f (cal: %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)\n",
                  figuras_calibradas[i].nombre, sim,
                  figuras_calibradas[i].hu[0],
                  figuras_calibradas[i].hu[1],
                  figuras_calibradas[i].hu[2],
                  figuras_calibradas[i].hu[3],
                  figuras_calibradas[i].hu[4],
                  figuras_calibradas[i].hu[5],
                  figuras_calibradas[i].hu[6]);
    */

    // Si la similitud actual es mejor que la mejor hasta ahora, la guardamos
    if (sim > mejor_sim) {
      mejor_sim = sim;
      mejor_idx = i;
    }
  }
}

// -------------------------------
// Umbral adaptativo 
// -------------------------------
double umbral_adaptativo = 0.35; 
// Valor base para decidir si la similitud es suficientemente buena.

if (mejor_idx != -1 && figuras_calibradas[mejor_idx].muestras >= 3) { //Si la figura mejor match tiene al menos 3 muestras en su calibración, elevamos el umbral: necesitamos mayor confianza para considerarlo válido.
  umbral_adaptativo = 0.4;
}

// Decisión final de clasificación

if (mejor_idx != -1 && mejor_sim >= 0.10) { 
  obj.forma = figuras_calibradas[mejor_idx].nombre;
} else {
  // Si no hay coincidencias buenas, marcamos como desconocido
  obj.forma = "DESCONOCIDO";
}


 // ----------------- 4) Detección de color -----------------
  // Tomamos el píxel en el centroide para decidir color.
  if (rgb_buf) {
    // rgb_buf en formato RGB565: 5 bits R, 6 bits G, 5 bits B
    uint16_t pixel_rgb565 = rgb_buf[obj.centro_y * w + obj.centro_x];

    // Extraer canales y convertir a 8 bits (shifts simples)
    uint8_t r = ((pixel_rgb565 >> 11) & 0x1F) << 3; // 5->8 bits
    uint8_t g = ((pixel_rgb565 >> 5) & 0x3F) << 2;  // 6->8 bits
    uint8_t b = (pixel_rgb565 & 0x1F) << 3;         // 5->8 bits

    float h, s, v;
    // Convertir RGB888 a HSV. Asegurate que rgb888_to_hsv use rangos h:[0..360), s,v:[0..1]
    rgb888_to_hsv(r, g, b, h, s, v);

    // Determinar nombre de color
    obj.color = get_color_name(h, s, v);
  }

  return obj;
} // end analizar_objeto


//================== FUNCIONES PARA EL COLOR ======================//
/**
 * Convierte un píxel RGB888 a HSV (Hue, Saturation, Value)
 * H: 0-360 (Color)
 * S: 0-1 (Saturación)
 * V: 0-1 (Brillo)
 */

void rgb888_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
  // Normalizamos los valores
  float r_f = r / 255.0;
  float g_f = g / 255.0;
  float b_f = b / 255.0;
  // Calculamos el máximo  y minimo entre R,G,B
  float cmax = max(r_f, max(g_f, b_f));
  float cmin = min(r_f, min(g_f, b_f));
  float delta = cmax - cmin; // Diferencia para calcular saturación y color
  
  if (delta == 0) h = 0;                    // Si no hay diferencia, el color es gris → Hue 0
  else if (cmax == r_f) h = 60 * fmod(((g_f - b_f) / delta), 6);  // Hue en sector rojo
  else if (cmax == g_f) h = 60 * (((b_f - r_f) / delta) + 2);     // Hue en sector verde
  else h = 60 * (((r_f - g_f) / delta) + 4);                      // Hue en sector azul

  if (h < 0) h += 360.0;                    // Asegurar hue dentro de [0..360]
  s = (cmax == 0) ? 0 : (delta / cmax);     // Saturación normalizada
  v = cmax;                                 // Valor = máximo de los RGB normalizados
}

/**
 * Devuelve el nombre de un color basado en sus valores HSV
 */
  String get_color_name(float h, float s, float v) {
    // SOLO negro si es muy oscuro Y sin saturación
    if (v < 0.10 && s < 0.20) {
      return "Negro";
    }

    // Blanco/gris si tiene poca saturación pero no está tan oscuro
    if (s < 0.20) {
      return "Blanco/Gris";
    }

    // A partir de acá, siempre es color, aunque el valor sea bajo
    if (h < 15 || h >= 345) return "Rojo";
    if (h >= 15 && h < 45) return "Naranja";
    if (h >= 45 && h < 75) return "Amarillo";
    if (h >= 75 && h < 150) return "Verde";
    if (h >= 150 && h < 190) return "Cyan";
    if (h >= 190 && h < 270) return "Azul";
    if (h >= 270 && h < 315) return "Magenta";
    if (h >= 315 && h < 345) return "Rosa";

    return "Desconocido";
  }

// Detecta el color promedio en un área (ej. 5x5) alrededor del centroide en la imagen original a color (RGB565).

String detectar_color_promedio(uint16_t* rgb565_buf, int w, int h, int cx, int cy, int sample_size = 5) {
  if (cx < 0 || cy < 0) return "N/A";        // Si el centroide está fuera, no se puede medir color

  long total_r = 0, total_g = 0, total_b = 0; // Acumuladores para promediar colores
  int count = 0;                              // Cantidad de píxeles sumados

  // Recorrer un área cuadrada alrededor del centroide
  for (int y = cy - sample_size; y <= cy + sample_size; y++) {
    for (int x = cx - sample_size; x <= cx + sample_size; x++) {
      
      // Ignorar píxeles fuera de la imagen
      if (x < 0 || x >= w || y < 0 || y >= h) continue;

      // 1. Leer píxel RGB565
      uint16_t pixel_rgb565 = rgb565_buf[y * w + x];
      
      // 2. Convertir a RGB888
      uint8_t r = ((pixel_rgb565 >> 11) & 0x1F) << 3;  // Rojo expandido
      uint8_t g = ((pixel_rgb565 >> 5) & 0x3F) << 2;   // Verde expandido
      uint8_t b = (pixel_rgb565 & 0x1F) << 3;          // Azul expandido

      // 3. Sumar componentes para el promedio
      total_r += r;
      total_g += g;
      total_b += b;
      count++;
    }
  }

  if (count == 0) return "N/A";              // No se pudieron promediar píxeles

  // 4. Promedio RGB
  uint8_t avg_r = total_r / count;
  uint8_t avg_g = total_g / count;
  uint8_t avg_b = total_b / count;

  // 5. Convertir RGB promedio a HSV
  float h_avg, s_avg, v_avg;
  rgb888_to_hsv(avg_r, avg_g, avg_b, h_avg, s_avg, v_avg);

  // 6. Devolver color interpretado
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
// ===== FUNCIÓN PARA PINTAR CENTROIDE (coloreado según obj.color) =====
void pintar_centroide_en_imagen(uint8_t* imagen_color, int width, int height, double cx, double cy, const String &obj_color) {
    int centroide_x = (int)round(cx);                        // convertir a entero
    int centroide_y = (int)round(cy);                        // convertir a entero

    // Verificar que las coordenadas estén dentro de los límites
    if (centroide_x < 0 || centroide_x >= width || centroide_y < 0 || centroide_y >= height) {
        return;                                              // salir si está fuera
    }

    // Determinar color a usar según la cadena obj_color
    uint8_t r_col = 255, g_col = 0, b_col = 0;               // color por defecto = rojo
    if (obj_color.startsWith("Azul")) {                      // si la etiqueta comienza con "Azul"
        r_col = 0; g_col = 0; b_col = 255;                   // azul
    } else if (obj_color.startsWith("Rojo")) {               // si comienza con "Rojo"
        r_col = 255; g_col = 0; b_col = 0;                   // rojo
    } else if (obj_color.startsWith("Negro")) {              // si comienza con "Negro"
        r_col = 255; g_col = 255; b_col = 255;               // blanco para objetos negros
    }
    
    // Pintar pixel central del centroide en imagen RGB888 (3 bytes/pixel)
    int pixel_index = (centroide_y * width + centroide_x) * 3; // índice base del píxel
    imagen_color[pixel_index]     = r_col;                    // R
    imagen_color[pixel_index + 1] = g_col;                    // G
    imagen_color[pixel_index + 2] = b_col;                    // B

    // Pintar una cruz pequeña coloreada (mejora visibilidad)
    int cross_size = 6;                                       // tamaño mitad de la cruz
    for (int i = -cross_size; i <= cross_size; i++) {
        // Línea horizontal
        int x_h = centroide_x + i;
        int y_h = centroide_y;
        if (x_h >= 0 && x_h < width && y_h >= 0 && y_h < height) {
            int idx_h = (y_h * width + x_h) * 3;
            imagen_color[idx_h]     = r_col;                 // R
            imagen_color[idx_h + 1] = g_col;                 // G
            imagen_color[idx_h + 2] = b_col;                 // B
        }
        
        // Línea vertical
        int x_v = centroide_x;
        int y_v = centroide_y + i;
        if (x_v >= 0 && x_v < width && y_v >= 0 && y_v < height) {
            int idx_v = (y_v * width + x_v) * 3;
            imagen_color[idx_v]     = r_col;                 // R
            imagen_color[idx_v + 1] = g_col;                 // G
            imagen_color[idx_v + 2] = b_col;                 // B
        }
    }
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

// Endpoint: /status (GET)
static esp_err_t status_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    StaticJsonDocument<512> doc;
    doc["conectado"] = true;
    doc["fotos_capturadas"] = photo_count;
    doc["calibrando"] = calibrating;
    doc["threshold"] = threshold_value;
    doc["ultimo_centroide_x"] = ultimo_cx;
    doc["ultimo_centroide_y"] = ultimo_cy;
    
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


                delay(1000);

                // --- Actualizar datos para el stream ---
                if (num_objetos > 0) {
                    strncpy(ultima_figura, objetos_detectados[0].forma.c_str(), 19);
                    ultima_confianza = 0.0; // si no tenés confianza por objeto, dejar 0
                    ultimo_cx = objetos_detectados[0].centro_x;
                    ultimo_cy = objetos_detectados[0].centro_y;

                    // <<< Línea crítica: copiar el color calculado del primer objeto >>>
                    if (objetos_detectados[0].color.length() > 0) {
                        strncpy(ultimo_color, objetos_detectados[0].color.c_str(), 19);
                    } else {
                        strncpy(ultimo_color, "N/A", 19);
                    }

                    Serial.printf("\n📊 Total detectados : %d objetos\n", num_objetos);
                } else {
                    strncpy(ultima_figura, "Ninguna", 19);
                    strncpy(ultimo_color, "N/A", 19); // asegurarse de limpiar el color también
                    ultima_confianza = 0.0;
                    ultimo_cx = -1;
                    ultimo_cy = -1;
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
                for (int i = 0; i < num_objetos; i++) {
                  if (objetos_detectados[i].centro_x >= 0 && objetos_detectados[i].centro_y >= 0) {
                    pintar_centroide_en_imagen(rgb_buffer, fb->width, fb->height, 
                                               objetos_detectados[i].centro_x, 
                                               objetos_detectados[i].centro_y,
                                               objetos_detectados[i].color);
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

// Endpoint: /detect (GET)
static esp_err_t detect_handler(httpd_req_t *req) {
    set_cors_headers(req);

    StaticJsonDocument<256> doc;
    doc["figura"] = ultima_figura;
    doc["confianza"] = ultima_confianza;
    doc["centroide_x"] = ultimo_cx;
    doc["centroide_y"] = ultimo_cy;
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
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

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_options_uri);
        httpd_register_uri_handler(camera_httpd, &processed_stream_uri);
        httpd_register_uri_handler(camera_httpd, &detect_uri);
        httpd_register_uri_handler(camera_httpd, &data_uri);
        
        Serial.println("✅ Servidor web iniciado correctamente");
    } else {
        Serial.println("❌ Error iniciando servidor web");
    }
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  ESP32-CAM DETECTOR OPTIMIZADO      ║");
    Serial.println("║   MOMENTOS HU MEJORADOS             ║");
    Serial.println("║   FLOOD FILL      MAX     COLOR     ║");
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
  
   labels = (int*)ps_malloc(buf_len * sizeof(int));

  if (!imagen_procesada || !labels) { // Chequeo mejorado
    Serial.println("❌ Error crítico asignando buffers - Reiniciando...");
    ESP.restart();
  }
  Serial.println("✅ Buffers de procesamiento listos");
    
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