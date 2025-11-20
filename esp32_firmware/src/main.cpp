#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================== TUS DATOS ====================
const char *ssid = "Pan de plátano";
const char *password = "12345678";

// TU URL DE NGROK (Sin https://)
const char *host = "usually-stable-lacewing.ngrok-free.app";
const int httpsPort = 443;

// Endpoints
const char *api_predict = "/predict";
const char *api_message = "/esp32/message";
const char *api_handshake = "/handshake";

// ==================== HARDWARE ====================
const int SDA_PIN = 1;
const int SCL_PIN = 2;
const int BUTTON_PIN = 21;

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// PINES CAMARA FREENOVE S3 (OPI)
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

// GLOBALES
WiFiClientSecure client;
unsigned long lastCheckTime = 0;
const long checkInterval = 2000;
bool isSystemStarted = false;

// ==================== HERRAMIENTAS VISUALES ====================
void showText(String title, String subtitle)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(title);
    display.setTextSize(2);
    display.setCursor(0, 20);
    if (subtitle.length() > 10)
        display.setTextSize(1);
    display.println(subtitle);
    display.display();
}

void playTransition(int duration_ms)
{
    long start = millis();
    while (millis() - start < duration_ms)
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(35, 0);
        display.print("CARGANDO...");
        for (int i = 0; i < 8; i++)
        {
            int h = random(5, 35);
            display.fillRect(16 + (i * 12), 64 - h, 8, h, WHITE);
        }
        display.display();
        delay(50);
    }
}

void playMiniSpectrum(int frames)
{
    for (int f = 0; f < frames; f++)
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("BUSCANDO API");
        display.setCursor(0, 15);
        display.println("Conectando...");
        int startX = 40;
        for (int i = 0; i < 4; i++)
        {
            int h = random(5, 25);
            int x = startX + (i * 12);
            int y_base = 63;
            display.fillRect(x, y_base - h, 8, h, WHITE);
        }
        display.display();
        delay(60);
    }
}

// ==================== CONEXIONES ====================
void connectWiFi()
{
    WiFi.begin(ssid, password);
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        String status = "Espere";
        for (int i = 0; i < dots; i++)
            status += ".";
        showText("WIFI...", status);
        dots = (dots + 1) % 4;
        delay(500);
    }
    showText("WIFI", "CONECTADO");
    delay(1000);
}

void waitForAPI()
{
    bool connected = false;
    while (!connected)
    {
        playMiniSpectrum(5);

        // --- CONFIGURACION SSL PESADA (LA SOLUCIÓN) ---
        client.setInsecure(); // Ignora certificado

        // 🔥 AUMENTAMOS EL BUFFER PARA COMERSE EL CERTIFICADO ENTERO 🔥
        // RX: 16KB (Exagerado para seguridad), TX: 4KB
        // El ESP32-S3 tiene RAM de sobra, usémosla.
        client.setBufferSizes(16384, 4096);

        client.setTimeout(15); // Timeout de socket

        HTTPClient http;
        http.setConnectTimeout(15000); // 15s conexion
        http.setTimeout(15000);        // 15s lectura

        String url = String("https://") + host + api_handshake;

        if (http.begin(client, url))
        {
            http.addHeader("ngrok-skip-browser-warning", "true");

            int httpCode = http.GET();

            if (httpCode == 200)
            {
                connected = true;
                showText("API", "ONLINE");
                delay(1000);
                playTransition(1000);
            }
            else
            {
                showText("ERROR API", String(httpCode));
                Serial.println(http.getString()); // Debug en monitor
                delay(2000);
            }
            http.end();
        }
        else
        {
            showText("FALLO RED", "Reintentar");
            delay(1500);
        }
    }
}

// ==================== FUNCIONES PRINCIPALES ====================
void checkMessages()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    // Cliente fresco para polling con buffer aumentado
    WiFiClientSecure secureMsg;
    secureMsg.setInsecure();
    secureMsg.setBufferSizes(4096, 1024); // Buffer decente

    HTTPClient http;
    http.setConnectTimeout(5000);
    String url = String("https://") + host + api_message;

    if (http.begin(secureMsg, url))
    {
        http.addHeader("ngrok-skip-browser-warning", "true");
        int httpCode = http.GET();

        if (httpCode > 0)
        {
            String payload = http.getString();
            StaticJsonDocument<512> doc;
            if (!deserializeJson(doc, payload) && doc["has_message"])
            {
                String text = doc["text"].as<String>();
                display.invertDisplay(true);
                delay(100);
                display.invertDisplay(false);
                showText("MENSAJE", text);
                delay(4000);
                showText("SISTEMA", "LISTO");
            }
        }
        http.end();
    }
}

void captureAndSend()
{
    display.fillScreen(WHITE);
    display.display();
    delay(50);
    display.clearDisplay();
    display.display();

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        showText("ERROR", "Camara Fail");
        delay(2000);
        return;
    }

    long startTime = millis();

    // Reconfigurar cliente principal por si acaso
    client.setInsecure();
    client.setBufferSizes(16384, 4096); // Buffer gigante para imagen
    client.setTimeout(20);

    if (client.connect(host, httpsPort))
    {
        String boundary = "---FreenoveBoundary";
        String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"esp32.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
        String tail = "\r\n--" + boundary + "--\r\n";
        uint32_t totalLen = head.length() + fb->len + tail.length();

        client.println("POST " + String(api_predict) + " HTTP/1.1");
        client.println("Host: " + String(host));
        client.println("ngrok-skip-browser-warning: true");
        client.println("Content-Length: " + String(totalLen));
        client.println("Content-Type: multipart/form-data; boundary=" + boundary);
        client.println("Connection: close");
        client.println();
        client.print(head);
        client.write(fb->buf, fb->len);
        client.print(tail);

        esp_camera_fb_return(fb);

        while (client.connected() && !client.available())
        {
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(30, 0);
            display.print("ANALIZANDO");
            for (int i = 0; i < 8; i++)
            {
                int h = random(5, 30);
                int x = 16 + (i * 12);
                display.fillRect(x, 64 - h, 8, h, WHITE);
            }
            display.display();
            delay(50);

            if (millis() - startTime > 25000)
            {
                showText("ERROR", "Timeout");
                client.stop();
                return;
            }
        }

        // Leer respuesta
        String response = "";
        bool jsonStarted = false;
        int brackets = 0;
        while (client.available())
        {
            char c = client.read();
            if (c == '{')
            {
                jsonStarted = true;
                brackets++;
            }
            if (jsonStarted)
            {
                response += c;
                if (c == '}')
                    brackets--;
                if (brackets == 0)
                    break;
            }
        }
        client.stop();

        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, response))
        {
            if (doc.containsKey("oled_text") && doc["oled_text"].as<String>().length() > 0)
            {
                showText("RESULTADO", doc["oled_text"].as<String>());
            }
            else
            {
                String p = doc["prediction"];
                float c = doc["confidence"];
                showText("MANUAL", p + " " + String(c, 0) + "%");
            }
            delay(4000);
        }
        else
        {
            showText("ERROR", "JSON Malo");
        }
    }
    else
    {
        showText("ERROR RED", "Ngrok Off");
        esp_camera_fb_return(fb);
        delay(2000);
    }
    showText("SISTEMA", "LISTO");
}

// ==================== SETUP ====================
void setup()
{
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
        Serial.println(F("Fallo OLED"));
    else
    {
        display.clearDisplay();
        display.display();
    }

    pinMode(BUTTON_PIN, INPUT_PULLUP);

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

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    if (esp_camera_init(&config) != ESP_OK)
    {
        showText("ERROR FATAL", "Fallo Camara");
        while (true)
            ;
    }

    showText("INICIANDO...", "Version Internet");
    delay(2000);
    playTransition(1000);

    // 1. WIFI
    connectWiFi();

    // 2. BOTON
    showText("WIFI LISTO", "Click Iniciar");
    while (digitalRead(BUTTON_PIN) == HIGH)
        delay(10);
    display.invertDisplay(true);
    delay(100);
    display.invertDisplay(false);
    while (digitalRead(BUTTON_PIN) == LOW)
        delay(10);

    playTransition(1000);

    // 3. API
    waitForAPI();

    // 4. LISTO
    showText("SISTEMA", "LISTO");
    isSystemStarted = true;
}

// ==================== LOOP ====================
void loop()
{
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        delay(50);
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            captureAndSend();
            while (digitalRead(BUTTON_PIN) == LOW)
                delay(10);
        }
    }
    if (millis() - lastCheckTime > checkInterval)
    {
        checkMessages();
        lastCheckTime = millis();
    }
}