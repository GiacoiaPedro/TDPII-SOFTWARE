/*
 * ESP32-CAM CON DETECCIÓN DE FIGURAS USANDO HU MOMENTS
 * Compatible con interfaz Svelte Kit - CORREGIDO
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
const char* ssid = "PedroWiFi 2.4Ghz";
const char* password = "negonego";

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
int photo_count = 0;
uint8_t threshold_value = 70;
char ultima_figura[20] = "Ninguna";
double ultima_confianza = 0.0;

// Buffer para imagen procesada
uint8_t* imagen_procesada = nullptr;

// Estructura para figuras
typedef struct {
    double hu[7];
    const char* nombre;
    bool calibrada;
} FiguraCalibrada;

FiguraCalibrada figuras_calibradas[5] = {
    {{0}, "Cuadrado", false},
    {{0}, "Circulo", false},
    {{0}, "L", false},
    {{0}, "Estrella", false},
    {{0}, "C", false}
};

const int MIN_AREA = 500;         // área mínima para considerar un objeto
const float HU_SIM_SCALE = 0.6f;  // escala para convertir distancia en similitud (ajustar si hace falta)

// ===== FUNCIONES DE PROCESAMIENTO DE IMAGEN =====
// ... (Tus funciones de procesamiento de imagen se mantienen igual)
// ---- Filtro Gaussiano 3x3 ----
void aplicar_blur(uint8_t* src, uint8_t* dst, int width, int height) {
    // Kernel Gaussiano 3x3 normalizado (suma = 16)
    const int k[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    const int ksum = 16;

    // Copiar bordes sin modificar (para no acceder fuera)
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

// ---- Binarización: soporte para threshold dinámico (si threshold == 0 calcula global) ----
void aplicar_binarizacion(uint8_t* src, uint8_t* dst, int width, int height, uint8_t threshold) {
    int len = width * height;
    uint8_t t = threshold;

    if (t == 0) {
        // Umbral dinámico: media global menos pequeño offset
        uint32_t sum = 0;
        for (int i = 0; i < len; i++) sum += src[i];
        uint8_t mean = sum / len;
        // margem/offset para evitar umbrales demasiado bajos
        int computed = (int)mean - 12; 
        if (computed < 20) computed = 20;
        t = (uint8_t)computed;
    }

    for (int i = 0; i < len; i++) {
        dst[i] = (src[i] > t) ? 255 : 0;
    }
}

// ---- Erosión: objeto = 0, fondo = 255 ----
void aplicar_erosion(uint8_t* src, uint8_t* dst, int width, int height) {
    int len = width * height;
    // copiar inicialmente todo a fondo
    for (int i = 0; i < len; i++) dst[i] = 255;

    // procesar zonas internas
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            bool allObject = true; // solo si todos los vecinos (3x3) son objeto (0) -> permanece 0
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
    // bordes se quedan en 255 por seguridad (evita artefactos en el borde del frame)
}

// ---- Dilatación: objeto = 0, fondo = 255 ----
void aplicar_dilatacion(uint8_t* src, uint8_t* dst, int width, int height) {
    int len = width * height;
    for (int i = 0; i < len; i++) dst[i] = 255;

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            bool anyObject = false; // si cualquier vecino es objeto -> se propaga
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

// ===== FUNCIONES HU MOMENTS MEJORADAS =====
void calcular_momentos_hu_mejorado(uint8_t* imagen_binaria, int width, int height, double hu[7]) {
    double area = 0;
    double sum_x = 0, sum_y = 0;
    int len = width * height;

    // Centroide y área (objeto = 0)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (imagen_binaria[y * width + x] == 0) {
                area++;
                sum_x += x;
                sum_y += y;
            }
        }
    }

    if (area >= MIN_AREA) {
        double cx = sum_x / area;
        double cy = sum_y / area;

        // Momentos centrales hasta orden 3
        double mu20 = 0, mu02 = 0, mu11 = 0;
        double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (imagen_binaria[y * width + x] == 0) {
                    double dx = x - cx;
                    double dy = y - cy;
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

        // Normalización robusta (invariante a escala aproximada)
        double a_gamma = pow(area, 2.0); // normalizamos con un exponente estable
        if (a_gamma == 0) a_gamma = 1;
        mu20 /= a_gamma;
        mu02 /= a_gamma;
        mu11 /= a_gamma;
        mu30 /= a_gamma;
        mu03 /= a_gamma;
        mu21 /= a_gamma;
        mu12 /= a_gamma;

        // Momentos Hu (estándar)
        hu[0] = mu20 + mu02;
        hu[1] = (mu20 - mu02) * (mu20 - mu02) + 4.0 * mu11 * mu11;
        hu[2] = (mu30 - 3.0 * mu12) * (mu30 - 3.0 * mu12) + (3.0 * mu21 - mu03) * (3.0 * mu21 - mu03);
        hu[3] = (mu30 + mu12) * (mu30 + mu12) + (mu21 + mu03) * (mu21 + mu03);
        hu[4] = (mu30 - 3.0 * mu12) * (mu30 + mu12) * ((mu30 + mu12) * (mu30 + mu12) - 3.0 * (mu21 + mu03) * (mu21 + mu03)) +
                (3.0 * mu21 - mu03) * (mu21 + mu03) * (3.0 * (mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03));
        hu[5] = (mu20 - mu02) * ((mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03)) +
                4.0 * mu11 * (mu30 + mu12) * (mu21 + mu03);
        hu[6] = (3.0 * mu21 - mu03) * (mu30 + mu12) * ((mu30 + mu12) * (mu30 + mu12) - 3.0 * (mu21 + mu03) * (mu21 + mu03)) -
                (mu30 - 3.0 * mu12) * (mu21 + mu03) * (3.0 * (mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03));

        // Log-escala estable
        for (int i = 0; i < 7; i++) {
            if (fabs(hu[i]) > 1e-12) hu[i] = log(fabs(hu[i]));
            else hu[i] = 0.0;
        }
    } else {
        // Objeto muy pequeño -> ceros
        for (int i = 0; i < 7; i++) hu[i] = 0.0;
    }
}

// ---- Similitud Hu: convierte distancia a similitud [0..1] con exponencial ----
double calcular_similitud_hu_mejorado(double hu1[7], double hu2[7]) {
    double distancia = 0.0;
    // pesos para priorizar primeros momentos
    double pesos[7] = {1.0, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2};
    for (int i = 0; i < 7; i++) {
        double d = hu1[i] - hu2[i];
        distancia += pesos[i] * d * d;
    }
    double dist_root = sqrt(distancia);
    // convertir a similitud: similitud = exp(-dist/k) -> k controla sensibilidad
    double k = HU_SIM_SCALE; // 0.6 por defecto, disminuye para hacer la similitud más estricta
    double simil = exp(-dist_root / k);
    if (simil < 0) simil = 0;
    if (simil > 1) simil = 1;
    return simil;
}

double calcular_similitud_hu(double hu1[7], double hu2[7]) {
    double distancia = 0;
    for (int i = 0; i < 2; i++) {
        distancia += fabs(hu1[i] - hu2[i]);
    }
    return 1.0 / (1.0 + distancia);
}

// ---- Detección usando la nueva similitud y umbral razonable ----
void detectar_figura(uint8_t* imagen_binaria, int width, int height, char* figura, double* confianza) {
    double hu_actual[7];
    calcular_momentos_hu_mejorado(imagen_binaria, width, height, hu_actual);

    double mejor_sim = 0.0;
    int mejor_idx = -1;
    for (int i = 0; i < 5; i++) {
        if (figuras_calibradas[i].calibrada) {
            double sim = calcular_similitud_hu_mejorado(hu_actual, figuras_calibradas[i].hu);
            if (sim > mejor_sim) {
                mejor_sim = sim;
                mejor_idx = i;
            }
        }
    }

    // Umbral de similitud más razonable
    const double UMbral_SIM = 0.25; // ajustar si quieres más estricto (ej: 0.35)
    if (mejor_idx != -1 && mejor_sim >= UMbral_SIM) {
        strncpy(figura, figuras_calibradas[mejor_idx].nombre, 19);
        figura[19] = '\0';
        *confianza = mejor_sim;
    } else {
        strcpy(figura, "Ninguna");
        *confianza = 0.0;
    }

    if (strcmp(figura, "Ninguna") != 0) {
        Serial.printf("✅ Detectado: %s (%.1f%% confianza)\n", figura, *confianza * 100);
    }
}

// ===== FUNCIÓN PARA CONFIGURAR CORS =====
static void set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ===== ENDPOINTS COMPATIBLES CON SVELTE =====

// Endpoint: /status (GET) - FALTABA ESTA DEFINICIÓN
static esp_err_t status_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    StaticJsonDocument<512> doc;
    doc["conectado"] = true;
    doc["fotos_capturadas"] = photo_count;
    doc["calibrando"] = calibrating;
    
    JsonArray figuras = doc.createNestedArray("figuras_calibradas");
    for (int i = 0; i < 5; i++) {
        if (figuras_calibradas[i].calibrada) {
            figuras.add(figuras_calibradas[i].nombre);
        }
    }
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

static esp_err_t processed_stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char figura[20] = "Ninguna";
    double confianza = 0.0;
    
    Serial.println("Processed stream started - Optimized real version");
    
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if(res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Buffers que sí existen
    uint8_t* grayscale = NULL;
    size_t grayscale_len = 0;
    bool buffers_initialized = false;

    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            delay(1);
            continue;
        }

        // Inicializar buffers una sola vez
        if (!buffers_initialized) {
            grayscale_len = fb->width * fb->height;
            grayscale = (uint8_t*)ps_malloc(grayscale_len);
            if (grayscale) {
                buffers_initialized = true;
            }
        }

        if (fb->format == PIXFORMAT_JPEG && buffers_initialized && imagen_procesada) {
            size_t rgb_len = fb->width * fb->height * 2;
            uint8_t* rgb_buffer = (uint8_t*)ps_malloc(rgb_len);
            
            if (rgb_buffer && jpg2rgb565(fb->buf, fb->len, rgb_buffer, JPG_SCALE_NONE)) {
                // Conversión RGB565 a Grayscale (esta parte SÍ existe)
                uint16_t* pixels = (uint16_t*)rgb_buffer;
                for (int i = 0; i < grayscale_len; i++) {
                    uint16_t pixel = pixels[i];
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    grayscale[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
                }

                // 🔥 OPTIMIZACIÓN: Reducir procesamiento
                // Aplicar solo binarización (la más importante)
                aplicar_binarizacion(grayscale, grayscale, fb->width, fb->height, threshold_value);
                
                // 🔥 Detección cada 5 frames (no cada frame)
                static int frame_count = 0;
                if (frame_count++ % 5 == 0) {
                    detectar_figura(grayscale, fb->width, fb->height, figura, &confianza);
                    strncpy(ultima_figura, figura, 19);
                    ultima_confianza = confianza;
                }

                // Convertir a JPEG
                size_t jpg_len = 0;
                uint8_t* jpg_buffer = NULL;
                
                if (fmt2jpg(grayscale, grayscale_len, fb->width, fb->height, 
                           PIXFORMAT_GRAYSCALE, 60, &jpg_buffer, &jpg_len)) {  // 🔥 Calidad más baja
                    
                    // Enviar frame
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "--frame\r\n", 10);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpg_buffer, jpg_len);
                    
                    // Metadata
                    char metadata[100];
                    snprintf(metadata, sizeof(metadata), 
                            "\r\nX-Detection-Data: {\"figura\":\"%s\",\"confianza\":%.2f}\r\n", 
                            figura, confianza);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, metadata, strlen(metadata));
                    
                    free(jpg_buffer);
                } else {
                    send_fallback_frame(req, fb);
                }
                
                free(rgb_buffer);
            } else {
                if (rgb_buffer) free(rgb_buffer);
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
    Serial.println("Processed stream ended");
    return res;
}

// 🔥 Función auxiliar REAL para enviar frames de fallback
static void send_fallback_frame(httpd_req_t *req, camera_fb_t *fb) {
    httpd_resp_send_chunk(req, "--frame\r\n", 10);
    httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
    httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    httpd_resp_send_chunk(req, "\r\n", 2);
}

// Endpoint: /calibrate (POST) - COMPATIBLE CON SVELTE
static esp_err_t calibrate_handler(httpd_req_t *req) {
    Serial.printf("\n====== Nueva calibración ======\n");
    Serial.printf("Heap libre antes: %d bytes\n", esp_get_free_heap_size());
    // ✅ CONFIGURAR CORS AL INICIO (para todos los casos)
    set_cors_headers(req);

    // ✅ MANEJAR PREFLIGHT OPTIONS EXPLÍCITAMENTE
    if (req->method == HTTP_OPTIONS) {
        Serial.println("🔄 Preflight OPTIONS recibido");
        return httpd_resp_send(req, NULL, 0);
    }
    
    // Solo procesar POST requests
    if (req->method != HTTP_POST) {
        Serial.println("❌ Método no permitido");
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, NULL, 0);
    }
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    
    if (ret <= 0) {
        Serial.println("❌ Error recibiendo datos de calibración");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Error recibiendo datos", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    Serial.printf("📦 Datos recibidos: %s\n", buf);
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, buf);
    
    if (error) {
        Serial.printf("❌ Error parseando JSON: %s\n", error.c_str());
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Error parseando JSON", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    if (!doc.containsKey("figure")) {
        Serial.println("❌ JSON no contiene campo 'figure'");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Falta campo 'figure'", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    int figure_id = doc["figure"];
    Serial.printf("🎯 Calibrando figura ID: %d\n", figure_id);
    
    if (figure_id < 0 || figure_id >= 5) {
        Serial.println("❌ ID de figura inválido");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "ID de figura inválido", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    calibrating = true;
    current_calibration_figure = figure_id;
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Error obteniendo frame de cámara");
        calibrating = false;
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Error de cámara", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    uint8_t* grayscale = nullptr;
    uint8_t* rgb565 = nullptr;
    bool calibracion_exitosa = false;
    
    do {
        size_t grayscale_len = fb->width * fb->height;
        
        if (fb->format != PIXFORMAT_JPEG || !imagen_procesada) {
            Serial.println("❌ Formato de cámara no compatible");
            break;
        }
        
        size_t rgb565_len = grayscale_len * 2; 
        rgb565 = (uint8_t*)ps_malloc(rgb565_len);
        if (!rgb565) {
            Serial.println("❌ Error asignando memoria RGB565");
            break;
        }
        
        bool ok = jpg2rgb565(fb->buf, fb->len, rgb565, JPG_SCALE_NONE);
        if (!ok) {
            Serial.println("❌ Error convirtiendo JPEG a RGB565");
            break;
        }
        
        grayscale = (uint8_t*)ps_malloc(grayscale_len);
        if (!grayscale) {
            Serial.println("❌ Error asignando memoria escala grises");
            break;
        }
        
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
        aplicar_blur(grayscale, imagen_procesada, fb->width, fb->height);
        aplicar_binarizacion(imagen_procesada, grayscale, fb->width, fb->height, threshold_value);
        aplicar_erosion(grayscale, imagen_procesada, fb->width, fb->height);
        aplicar_dilatacion(imagen_procesada, grayscale, fb->width, fb->height);
        
        // Calibrar momentos Hu
        calcular_momentos_hu_mejorado(grayscale, fb->width, fb->height, figuras_calibradas[figure_id].hu);
        figuras_calibradas[figure_id].calibrada = true;
        calibracion_exitosa = true;
        
        Serial.printf("✅ Figura %d calibrada: %s - Momentos: [%.3f, %.3f, %.3f]\n", 
            figure_id, figuras_calibradas[figure_id].nombre,
            figuras_calibradas[figure_id].hu[0],
            figuras_calibradas[figure_id].hu[1],
            figuras_calibradas[figure_id].hu[2]);
            
    } while(0);

    // Limpiar memoria
    if (rgb565) free(rgb565);
    if (grayscale && grayscale != fb->buf) free(grayscale);
    
    esp_camera_fb_return(fb);
    calibrating = false;
    
    // ✅ RESPUESTA MÁS DETALLADA
    StaticJsonDocument<256> response_doc;
    if (calibracion_exitosa) {
        response_doc["status"] = "ok";
        response_doc["figure_id"] = figure_id;
        response_doc["figure_name"] = figuras_calibradas[figure_id].nombre;
        Serial.println("Figuras calibradas:");
        for (int i = 0; i < 5; i++) {
            if (figuras_calibradas[i].calibrada) {
                Serial.printf(" - %s\n", figuras_calibradas[i].nombre);
            }
        }
        response_doc["message"] = "Calibración exitosa";
        Serial.printf("✅ Calibración exitosa para: %s\n", figuras_calibradas[figure_id].nombre);
    } else {
        response_doc["status"] = "error";
        response_doc["message"] = "Error en calibración";
        Serial.println("❌ Calibración falló");
    }
    
    String response;
    serializeJson(response_doc, response);
    
    Serial.printf("📤 Enviando respuesta: %s\n", response.c_str());
    
    httpd_resp_set_type(req, "application/json");

    
    Serial.printf("Heap libre despues: %d bytes\n", esp_get_free_heap_size());
    Serial.println("====== Fin calibración ======\n");
    return httpd_resp_send(req, response.c_str(), response.length());
}

static esp_err_t detect_handler(httpd_req_t *req) {
    set_cors_headers(req);
    


    StaticJsonDocument<200> doc;
    doc["figura"] = ultima_figura;
    doc["confianza"] = ultima_confianza;
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

// Endpoint: /data (alias de /detect para compatibilidad)
static esp_err_t data_handler(httpd_req_t *req) {
    return detect_handler(req);
}

// Handler para requests OPTIONS global
static esp_err_t options_handler(httpd_req_t *req) {
    set_cors_headers(req);
    return httpd_resp_send(req, NULL, 0);
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    // Servidor principal
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
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔═══════════════════════════════════╗");
    Serial.println("║  ESP32-CAM DETECTOR DE FIGURAS   ║");
    Serial.println("║   COMPATIBLE CON SVELTE KIT      ║");
    Serial.println("╚═══════════════════════════════════╝\n");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // WiFi
    WiFi.begin(ssid, password);
    Serial.print("Conectando WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✓ WiFi conectado");
    Serial.print("📱 IP: http://");
    Serial.println(WiFi.localIP());
    
    // SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("✗ Error SPIFFS");
        ESP.restart();
    }
    Serial.println("✓ SPIFFS OK");
    
    // Verificar PSRAM
    if (!psramFound()) {
        Serial.println("⚠ PSRAM no encontrada");
    } else {
        Serial.printf("✓ PSRAM: %d KB libres\n", ESP.getFreePsram() / 1024);
    }
    
    // Cámara
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
    config.xclk_freq_hz = 20000000;       // 20MHz - máximo
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;   // 320x240
    config.jpeg_quality = 8;              // Calidad baja
    config.fb_count = 2;                  // 2 buffers
    
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("✗ Error cámara");
        ESP.restart();
    }
    Serial.println("✓ Cámara OK - Low power mode");

    // Configuración mínima del sensor
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);                      // Voltear vertical
    s->set_hmirror(s, 1);                    // Voltear horizontal

    // Buffer para imagen procesada
    imagen_procesada = (uint8_t*)ps_malloc(320 * 240);
    if (!imagen_procesada) {
        Serial.println("✗ Error asignando buffer");
        ESP.restart();
    }
    Serial.println("✓ Buffer asignado");
    
    startCameraServer();
    Serial.println("✓ Servidor web OK");
    Serial.println("✓ Endpoints disponibles:");
    Serial.println("  - GET  /status");
    Serial.println("  - POST /calibrate");
    Serial.println("  - GET  /detect");
    Serial.println("  - GET  /data");
    Serial.println("  - GET  /processed_stream"); 
    Serial.println("════════════════════════════════════");
    Serial.println("¡Listo! La interfaz Svelte puede conectarse");
    Serial.println("════════════════════════════════════\n");
}

void loop() {
}