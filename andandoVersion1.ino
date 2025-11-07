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
uint8_t threshold_value = 127;

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

// ===== FUNCIONES DE PROCESAMIENTO DE IMAGEN =====
void aplicar_blur(uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int i = y * width + x;
            int sum = src[i - width - 1] + src[i - width] + src[i - width + 1] +
                      src[i - 1] + src[i] + src[i + 1] +
                      src[i + width - 1] + src[i + width] + src[i + width + 1];
            dst[i] = sum / 9;
        }
    }
}

void aplicar_binarizacion(uint8_t* src, uint8_t* dst, int width, int height, uint8_t threshold) {
    for (int i = 0; i < width * height; i++) {
        dst[i] = (src[i] > threshold) ? 255 : 0;
    }
}

void aplicar_erosion(uint8_t* src, uint8_t* dst, int width, int height) {
    memcpy(dst, src, width * height);
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int i = y * width + x;
            if (dst[i] == 0) {
                if (dst[i - 1] == 255 || dst[i + 1] == 255 || 
                    dst[i - width] == 255 || dst[i + width] == 255) {
                    src[i] = 255;
                } else {
                    src[i] = 0;
                }
            } else {
                src[i] = 255;
            }
        }
    }
}

void aplicar_dilatacion(uint8_t* src, uint8_t* dst, int width, int height) {
    memcpy(dst, src, width * height);
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int i = y * width + x;
            if (dst[i] == 255) {
                if (dst[i - 1] == 0 || dst[i + 1] == 0 || 
                    dst[i - width] == 0 || dst[i + width] == 0) {
                    src[i] = 0;
                } else {
                    src[i] = 255;
                }
            } else {
                src[i] = 0;
            }
        }
    }
}


// ===== FUNCIONES HU MOMENTS MEJORADAS =====
void calcular_momentos_hu_mejorado(uint8_t* imagen_binaria, int width, int height, double hu[7]) {
    double area = 0;
    double sum_x = 0, sum_y = 0;
    
    // Calcular centroide y área
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (imagen_binaria[y * width + x] == 0) { // Objeto
                area++;
                sum_x += x;
                sum_y += y;
            }
        }
    }
    
    // Umbral de área más bajo para mayor sensibilidad
    if (area > 50) { // Reducido de 100 a 50
        double centro_x = sum_x / area;
        double centro_y = sum_y / area;
        
        // Momentos centrales
        double mu20 = 0, mu02 = 0, mu11 = 0;
        double mu30 = 0, mu03 = 0, mu21 = 0, mu12 = 0;
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (imagen_binaria[y * width + x] == 0) {
                    double dx = x - centro_x;
                    double dy = y - centro_y;
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
        
        // Normalizar
        double gamma = 2.0; // Para normalización
        mu20 /= pow(area, gamma);
        mu02 /= pow(area, gamma);
        mu11 /= pow(area, gamma);
        mu30 /= pow(area, gamma + 0.5);
        mu03 /= pow(area, gamma + 0.5);
        mu21 /= pow(area, gamma + 0.5);
        mu12 /= pow(area, gamma + 0.5);
        
        // Momentos Hu mejorados
        hu[0] = mu20 + mu02;
        hu[1] = (mu20 - mu02) * (mu20 - mu02) + 4 * mu11 * mu11;
        hu[2] = (mu30 - 3 * mu12) * (mu30 - 3 * mu12) + (3 * mu21 - mu03) * (3 * mu21 - mu03);
        hu[3] = (mu30 + mu12) * (mu30 + mu12) + (mu21 + mu03) * (mu21 + mu03);
        hu[4] = (mu30 - 3 * mu12) * (mu30 + mu12) * ((mu30 + mu12) * (mu30 + mu12) - 3 * (mu21 + mu03) * (mu21 + mu03)) +
                (3 * mu21 - mu03) * (mu21 + mu03) * (3 * (mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03));
        hu[5] = (mu20 - mu02) * ((mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03)) +
                4 * mu11 * (mu30 + mu12) * (mu21 + mu03);
        hu[6] = (3 * mu21 - mu03) * (mu30 + mu12) * ((mu30 + mu12) * (mu30 + mu12) - 3 * (mu21 + mu03) * (mu21 + mu03)) -
                (mu30 - 3 * mu12) * (mu21 + mu03) * (3 * (mu30 + mu12) * (mu30 + mu12) - (mu21 + mu03) * (mu21 + mu03));
        
        // Aplicar logaritmo para mayor estabilidad numérica
        for (int i = 0; i < 7; i++) {
            if (fabs(hu[i]) > 1e-10) {
                hu[i] = log(fabs(hu[i]));
            } else {
                hu[i] = 0;
            }
        }
    } else {
        for (int i = 0; i < 7; i++) {
            hu[i] = 0;
        }
    }
}

double calcular_similitud_hu_mejorado(double hu1[7], double hu2[7]) {
    double distancia = 0;
    double pesos[7] = {1.0, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2}; // Pesos para cada momento
    
    for (int i = 0; i < 7; i++) {
        // Usar distancia euclidiana ponderada
        double diff = hu1[i] - hu2[i];
        distancia += pesos[i] * diff * diff;
    }
    
    // Convertir a similitud (0-1), con umbral más bajo
    double similitud = 1.0 / (1.0 + sqrt(distancia));
    
    // Escalar para mayor sensibilidad
    return similitud * 1.5; // Aumentar sensibilidad
}
// ===== FUNCIONES HU MOMENTS (SIMPLIFICADAS) =====
void calcular_momentos_hu_simplificado(uint8_t* imagen_binaria, int width, int height, double hu[7]) {
    double area = 0;
    double sum_x = 0, sum_y = 0;
    
    // Calcular centroide y área
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (imagen_binaria[y * width + x] == 0) {
                area++;
                sum_x += x;
                sum_y += y;
            }
        }
    }
    
    if (area > 0) {
        double centro_x = sum_x / area;
        double centro_y = sum_y / area;
        
        // Momentos centrales simplificados
        double mu20 = 0, mu02 = 0, mu11 = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (imagen_binaria[y * width + x] == 0) {
                    double dx = x - centro_x;
                    double dy = y - centro_y;
                    mu20 += dx * dx;
                    mu02 += dy * dy;
                    mu11 += dx * dy;
                }
            }
        }
        
        // Normalizar
        mu20 /= area;
        mu02 /= area;
        mu11 /= area;
        
        // Momentos Hu simplificados
        hu[0] = mu20 + mu02;
        hu[1] = (mu20 - mu02) * (mu20 - mu02) + 4 * mu11 * mu11;
        for (int i = 2; i < 7; i++) {
            hu[i] = 0;
        }
    } else {
        for (int i = 0; i < 7; i++) {
            hu[i] = 0;
        }
    }
}

double calcular_similitud_hu(double hu1[7], double hu2[7]) {
    double distancia = 0;
    for (int i = 0; i < 2; i++) {
        distancia += fabs(hu1[i] - hu2[i]);
    }
    return 1.0 / (1.0 + distancia);
}

// ===== DETECCIÓN DE FIGURAS =====
// ===== DETECCIÓN DE FIGURAS MEJORADA =====
void detectar_figura(uint8_t* imagen_binaria, int width, int height, char* figura, double* confianza) {
    double hu_actual[7];
    calcular_momentos_hu_mejorado(imagen_binaria, width, height, hu_actual);
    
    double mejor_similitud = 0;
    int mejor_figura = -1;
    
    for (int i = 0; i < 5; i++) {
        if (figuras_calibradas[i].calibrada) {
            double similitud = calcular_similitud_hu_mejorado(hu_actual, figuras_calibradas[i].hu);
            if (similitud > mejor_similitud) {
                mejor_similitud = similitud;
                mejor_figura = i;
            }
        }
    }
    
    // Umbral mucho más bajo para mayor sensibilidad
    if (mejor_figura != -1 && mejor_similitud > 0.05) { // Reducido de 0.3 a 0.05
        strcpy(figura, figuras_calibradas[mejor_figura].nombre);
        *confianza = mejor_similitud;
        if (*confianza > 1.0) *confianza = 1.0; // Limitar a 100%
    } else {
        strcpy(figura, "Ninguna");
        *confianza = 0;
    }
}

// ===== FUNCIÓN PARA CONFIGURAR CORS =====
static void set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ===== ENDPOINTS COMPATIBLES CON SVELTE =====

// Endpoint: /status (GET)
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

// Endpoint: /calibrate (POST) - CORREGIDO
static esp_err_t calibrate_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    Serial.println("pedido de calibracion");

    // Manejar preflight OPTIONS - CORREGIDO
    if (req->method == HTTP_OPTIONS) {
        return httpd_resp_send(req, NULL, 0);
    }
    
    // Solo procesar POST requests
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, NULL, 0);
    }
    
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    
    if (ret > 0) {
        buf[ret] = '\0';
        StaticJsonDocument<100> doc;
        DeserializationError error = deserializeJson(doc, buf);
        
        if (!error) {
            int figure_id = doc["figure"];
            if (figure_id >= 0 && figure_id < 5) {
                calibrating = true;
                current_calibration_figure = figure_id;
                
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) {
                    uint8_t* grayscale = nullptr;
                    size_t grayscale_len = fb->width * fb->height;
                    
                    if (fb->format == PIXFORMAT_JPEG && imagen_procesada) {
                        size_t rgb565_len = grayscale_len * 2; 
                        uint8_t* rgb565 = (uint8_t*)ps_malloc(rgb565_len);
                        if (rgb565) {
                            bool ok = jpg2rgb565(fb->buf, fb->len, rgb565, JPG_SCALE_NONE);
                            if (ok) {
                                grayscale = (uint8_t*)ps_malloc(grayscale_len);
                                if (grayscale) {
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
                                    
                                    Serial.printf("✓ Figura %d calibrada: %s - Momentos: [%.3f, %.3f, %.3f, ...]\n", 
                                        figure_id, figuras_calibradas[figure_id].nombre,
                                        figuras_calibradas[figure_id].hu[0],
                                        figuras_calibradas[figure_id].hu[1],
                                        figuras_calibradas[figure_id].hu[2]);
                                }
                            }
                            free(rgb565);
                        }
                    }
                    if (grayscale != nullptr && grayscale != fb->buf) {
                        free(grayscale);
                    }
                    esp_camera_fb_return(fb);
                }
                
                calibrating = false;
                
                StaticJsonDocument<100> response_doc;
                response_doc["status"] = "ok";
                String response;
                serializeJson(response_doc, response);
                
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_send(req, response.c_str(), response.length());
            }
        }
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// Endpoint: /detect (GET) - Compatible con Svelte
static esp_err_t detect_handler(httpd_req_t *req) {
    set_cors_headers(req);
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    StaticJsonDocument<200> doc;
    char figura[20];
    double confianza = 0;
    
    uint8_t* grayscale = nullptr;
    size_t grayscale_len = fb->width * fb->height;
    
    if (fb->format == PIXFORMAT_JPEG && imagen_procesada) {
        size_t rgb565_len = grayscale_len * 2; 
        uint8_t* rgb565 = (uint8_t*)ps_malloc(rgb565_len);
        if (rgb565) {
            bool ok = jpg2rgb565(fb->buf, fb->len, rgb565, JPG_SCALE_NONE);
            if (ok) {
                grayscale = (uint8_t*)ps_malloc(grayscale_len);
                if (grayscale) {
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
                    
                    // Detectar figura
                    detectar_figura(grayscale, fb->width, fb->height, figura, &confianza);
                    
                    doc["figura"] = figura;
                    doc["confianza"] = confianza;
                }
            }
            free(rgb565);
        }
    }
    
    if (grayscale != nullptr && grayscale != fb->buf) {
        free(grayscale);
    }
    esp_camera_fb_return(fb);
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

/// Endpoint: /stream - MÁS FPS
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    
    Serial.println("Stream started - High FPS mode");
    
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    if(res != ESP_OK) return res;
    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            delay(10);
            continue;
        }

        // Enviar frame rápidamente
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "--frame\r\n", 10);
        }
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 28);
        }
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        }
        if(res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }

        esp_camera_fb_return(fb);
        
        if(res != ESP_OK) {
            break;
        }
        
        // Delay mínimo para ~10-15 FPS
        delay(70);
    }
    
    Serial.println("Stream ended");
    return res;
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

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_uri);
        httpd_register_uri_handler(camera_httpd, &calibrate_options_uri);
        httpd_register_uri_handler(camera_httpd, &detect_uri);
        httpd_register_uri_handler(camera_httpd, &data_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
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
    config.xclk_freq_hz = 10000000;       // 10MHz - buen balance
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;   // 320x240 - mejor para procesamiento
    config.jpeg_quality = 15;             // Calidad media-baja
    config.fb_count = 2;                  // 2 buffers para mejor FPS
    
    if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("✗ Error cámara");
    ESP.restart();
}
    Serial.println("✓ Cámara OK - Low power mode");

    // Configuración mínima del sensor
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);                      // Voltear vertical
    s->set_hmirror(s, 1);                    // Voltear horizontal

    // Buffer más pequeño para procesamiento
    // Buffer para imagen procesada - usar tamaño consistente con QVGA
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
    Serial.println("  - GET  /stream");
    Serial.println("════════════════════════════════════");
    Serial.println("¡Listo! La interfaz Svelte puede conectarse");
    Serial.println("════════════════════════════════════\n");
}

void loop() {
}
