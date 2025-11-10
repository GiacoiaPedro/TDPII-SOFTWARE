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

// Estructura para figuras mejorada
typedef struct {
    double hu[7];
    const char* nombre;
    bool calibrada;
    int muestras;  // Número de muestras para esta figura
} FiguraCalibrada;

FiguraCalibrada figuras_calibradas[5] = {
    {{0}, "Cuadrado", false, 0},
    {{0}, "Circulo", false, 0},
    {{0}, "L", false, 0},
    {{0}, "Estrella", false, 0},
    {{0}, "C", false, 0}
};

const int MIN_AREA = 500;         // área mínima para considerar un objeto
const float HU_SIM_SCALE = 0.3f;  // escala más estricta para mejor precisión

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

    if (t == 0) {
        // Umbral automático usando método simplificado
        uint32_t sum = 0;
        for (int i = 0; i < len; i++) sum += src[i];
        uint8_t mean = sum / len;
        int computed = (int)mean - 15; 
        if (computed < 25) computed = 25;
        if (computed > 200) computed = 200;
        t = (uint8_t)computed;
    }

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

// ===== MOMENTOS DE HU CORREGIDOS =====
void calcular_momentos_hu_corregido(uint8_t* imagen_binaria, int width, int height, double hu[7]) {
    double m00 = 0, m10 = 0, m01 = 0;
    
    // Calcular momentos espaciales
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (imagen_binaria[y * width + x] == 0) { // Objeto = 0
                m00 += 1;
                m10 += x;
                m01 += y;
            }
        }
    }
    
    if (m00 < MIN_AREA) {
        for (int i = 0; i < 7; i++) hu[i] = 0.0;
        return;
    }
    
    // Centroide
    double cx = m10 / m00;
    double cy = m01 / m00;
    
    // Momentos centrales
    double mu20 = 0, mu02 = 0, mu11 = 0;
    double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
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
    hu[4] = (nu30 - 3.0 * nu12) * (nu30 + nu12) * 
            ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) +
            (3.0 * nu21 - nu03) * (nu21 + nu03) * 
            (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));
    hu[5] = (nu20 - nu02) * ((nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03)) +
            4.0 * nu11 * (nu30 + nu12) * (nu21 + nu03);
    hu[6] = (3.0 * nu21 - nu03) * (nu30 + nu12) * 
            ((nu30 + nu12) * (nu30 + nu12) - 3.0 * (nu21 + nu03) * (nu21 + nu03)) -
            (nu30 - 3.0 * nu12) * (nu21 + nu03) * 
            (3.0 * (nu30 + nu12) * (nu30 + nu12) - (nu21 + nu03) * (nu21 + nu03));
    
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
    double pesos[7] = {1.0, 0.8, 0.3, 0.2, 0.1, 0.1, 0.05};
    
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
    calcular_momentos_hu_corregido(imagen_binaria, width, height, hu_actual);
    
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
        Serial.printf("✅ Detectado: %s (%.1f%%) - Muestras: %d\n", 
                     figura, mejor_sim * 100, figuras_calibradas[mejor_idx].muestras);
    } else {
        strcpy(figura, "Ninguna");
        *confianza = 0.0;
    }
}

// ===== CALIBRACIÓN CON MÚLTIPLES MUESTRAS =====
bool calibrar_con_promedio(int figure_id) {
    const int MUESTRAS_CALIBRACION = 3; // Reducido para mejor rendimiento
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
        delay(200); // Pausa entre muestras
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

        if (fb->format == PIXFORMAT_JPEG && buffers_initialized) {
            size_t rgb_len = fb->width * fb->height * 2;
            uint8_t* rgb_buffer = (uint8_t*)ps_malloc(rgb_len);
            
            if (rgb_buffer && jpg2rgb565(fb->buf, fb->len, rgb_buffer, JPG_SCALE_NONE)) {
                // Conversión RGB565 a Grayscale
                uint16_t* pixels = (uint16_t*)rgb_buffer;
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
                static int frame_count = 0;
                if (frame_count++ % 3 == 0) {
                    detectar_figura_mejorada(grayscale, fb->width, fb->height, ultima_figura, &ultima_confianza);
                }

                // Convertir a JPEG para streaming
                size_t jpg_len = 0;
                uint8_t* jpg_buffer = NULL;
                
                if (fmt2jpg(grayscale, grayscale_len, fb->width, fb->height, 
                           PIXFORMAT_GRAYSCALE, 70, &jpg_buffer, &jpg_len)) {
                    
                    // Enviar frame
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "--frame\r\n", 10);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
                    if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpg_buffer, jpg_len);
                    
                    // Metadata
                    char metadata[100];
                    snprintf(metadata, sizeof(metadata), 
                            "\r\nX-Detection-Data: {\"figura\":\"%s\",\"confianza\":%.2f}\r\n", 
                            ultima_figura, ultima_confianza);
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
    
    Serial.printf("Heap libre después: %d bytes\n", esp_get_free_heap_size());
    Serial.println("====== FIN CALIBRACIÓN ======\n");
    return httpd_resp_send(req, response.c_str(), response.length());
}

// Endpoint: /detect (GET)
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

    // Buffer para imagen procesada
    imagen_procesada = (uint8_t*)ps_malloc(320 * 240);
    if (!imagen_procesada) {
        Serial.println("⚠ Error buffer procesado - Usando malloc");
        imagen_procesada = (uint8_t*)malloc(320 * 240);
    }
    
    if (imagen_procesada) {
        Serial.println("✅ Buffer de procesamiento listo");
    } else {
        Serial.println("❌ Error crítico buffer - Reiniciando...");
        ESP.restart();
    }
    
    startCameraServer();
    
    Serial.println("\n✅ SISTEMA LISTO");
    Serial.println("════════════════════════════════════");
    Serial.println("Endpoints disponibles:");
    Serial.println("  GET  /status           - Estado del sistema");
    Serial.println("  POST /calibrate        - Calibrar figura");
    Serial.println("  GET  /detect           - Última detección");
    Serial.println("  GET  /data             - Alias de /detect");
    Serial.println("  GET  /processed_stream - Video procesado");
    Serial.println("════════════════════════════════════");
    Serial.printf("Memoria libre: %d bytes\n", esp_get_free_heap_size());
    Serial.println("════════════════════════════════════\n");
}

// ===== LOOP PRINCIPAL =====
void loop() {
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
  
}