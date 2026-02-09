#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//  DATOS (HOTSPOT) 
const char *ssid = "S23 FE de Ari";
const char *password = "deadspace1";

//IP LOCAL
const char *host = "10.61.132.17";
const int httpPort = 8000;

// Endpoints
const char *api_predict = "/predict";
const char *api_message = "/esp32/message";
const char *api_handshake = "/handshake";

// HARDWARE 
const int SDA_PIN = 1;
const int SCL_PIN = 2;
const int BUTTON_PIN = 21;

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// PINES CAMARA (Freenove S3 Config)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

WiFiClient client;
unsigned long lastCheckTime = 0;
const long checkInterval = 2000;

// UI
void showText(String title, String subtitle) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(title);
    display.setTextSize(2);
    display.setCursor(0, 20);
    if (subtitle.length() > 10) display.setTextSize(1);
    display.println(subtitle);
    display.display();
}

//  LOGICA 
void connectWiFi() {
    WiFi.begin(ssid, password);
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED) {
        String status = "Espere";
        for (int i = 0; i < dots; i++) status += ".";
        showText("WIFI...", status);
        dots = (dots + 1) % 4;
        delay(500);
    }
    showText("WIFI", "CONECTADO");
    Serial.println("WiFi OK");
    Serial.print("MI IP ES: ");
    Serial.println(WiFi.localIP());
    delay(1000);
}

void waitForAPI() {
    bool connected = false;
    while (!connected) {
        HTTPClient http;
        String url = String("http://") + host + ":" + httpPort + api_handshake;

        
        Serial.print("Intentando conectar a: ");  // mensaje intento de conexión esp32-api
        Serial.println(url); 

        
        if (http.begin(client, url)) {
            int httpCode = http.GET();
            if (httpCode == 200) {
                connected = true;
                showText("API", "ONLINE");
                delay(1000);
            } else {
                showText("ERROR API", String(httpCode));
                delay(2000);
            }
            http.end();
        } else {
            showText("BUSCANDO", "SERVIDOR...");
            delay(1500);
        }
    }
}

void checkMessages() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = String("http://") + host + ":" + httpPort + api_message;
    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            StaticJsonDocument<512> doc;
            if (!deserializeJson(doc, payload) && doc["has_message"]) {
                showText("MENSAJE", doc["text"].as<String>());
                delay(4000);
                showText("LISTO", "Presiona Btn");
            }
        }
        http.end();
    }
}

void captureAndSend() {
    showText("FOTO", "Tomando...");
    
    // Tomar foto
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        showText("ERROR", "Camara Fail");
        return;
    }

    showText("ENVIANDO", "Analizando...");
    
    if (client.connect(host, httpPort)) {
        String boundary = "---ESP32Boundary";
        String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
        String tail = "\r\n--" + boundary + "--\r\n";
        uint32_t totalLen = head.length() + fb->len + tail.length();

        client.println("POST " + String(api_predict) + " HTTP/1.1");
        client.println("Host: " + String(host));
        client.println("Content-Length: " + String(totalLen));
        client.println("Content-Type: multipart/form-data; boundary=" + boundary);
        client.println("Connection: close");
        client.println();
        client.print(head);
        client.write(fb->buf, fb->len);
        client.print(tail);

        esp_camera_fb_return(fb); // Liberar memoria rápido

        // Esperar respuesta
        long timeout = millis();
        while (client.connected() && !client.available()) {
            if (millis() - timeout > 10000) {
                client.stop();
                showText("ERROR", "TIMEOUT");
                return;
            }
        }

        // Leer respuesta JSON
        String response = "";
        bool jsonStarted = false;
        int brackets = 0;
        while (client.available()) {
            char c = client.read();
            if (c == '{') { jsonStarted = true; brackets++; }
            if (jsonStarted) {
                response += c;
                if (c == '}') brackets--;
                if (brackets == 0) break;
            }
        }
        client.stop();

        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, response)) {
            if (doc.containsKey("oled_text") && doc["oled_text"].as<String>().length() > 0) {
                showText("RESULTADO", doc["oled_text"].as<String>());
            } else {
                String p = doc["prediction"];
                float c = doc["confidence"];
                showText("IA DICE:", p + " " + String(c, 0) + "%");
            }
            delay(4000);
        }
    } else {
        showText("ERROR", "No Server");
        esp_camera_fb_return(fb);
        delay(2000);
    }
    showText("LISTO", "Presiona Btn");
}

// SETUP 
void setup() {
    //  Inicia Serial y espera
    delay(3000);
    Serial.begin(115200);
    Serial.println("--- ARRANQUE ---");

    // Inicia OLED
    Wire.begin(SDA_PIN, SCL_PIN);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("Fallo OLED");
    } else {
        display.clearDisplay();
        display.display();
    }

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    //  Configurar CAMARA 
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
    config.pixel_format = PIXFORMAT_JPEG;


    // configuración específica para limitar uso de RAM
    // Usar VGA (640x480) si hay RAM, sino CIF (400x296)
    config.frame_size = FRAMESIZE_VGA; 
    
    //Calidad media-baja para ahorrar RAM
    // 10 = Mejor calidad (Pesado)
    // 20 = Calidad media (Ligero)
    config.jpeg_quality = 20; 
    
    config.fb_count = 1; 
    config.fb_location = CAMERA_FB_IN_DRAM;

    // Inicializar
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Fallo VGA... Bajando a CIF");
        
        // Si VGA falla, se cambia a CIF (400x296)
        config.frame_size = FRAMESIZE_CIF;
        config.jpeg_quality = 12;
        esp_camera_init(&config); // Reintentar con CIF
    }
    
    // Verificacion final
    camera_fb_t *fb = esp_camera_fb_get();
    if(!fb) {
        showText("ERROR", "CAMARA RAM");
        Serial.println("Error: No hay RAM para la cámara");
        return;
    }
    esp_camera_fb_return(fb); // Liberar prueba
    Serial.println("Camara OK");

    //Conectar
    connectWiFi();
    waitForAPI();
    
    showText("LISTO", "Presiona Btn");
}

void loop() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50);
        if (digitalRead(BUTTON_PIN) == LOW) {
            captureAndSend();
            // Esperar a que suelte el boton
            while(digitalRead(BUTTON_PIN) == LOW) delay(10);
        }
    }
    if (millis() - lastCheckTime > checkInterval) {
        checkMessages();
        lastCheckTime = millis();
    }
}