#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= DATOS WIFI =================
const char *ssid = "TU_WIFI";
const char *password = "TU_PASSWORD";

// ================= CONFIGURACIÓN SERVIDOR =================
// Host sin 'https://' para la conexión segura manual
const char *host = "usually-stable-lacewing.ngrok-free.app";
const int httpsPort = 443;

// Endpoints
const char *api_predict = "/predict";
const char *api_message = "/esp32/message";

// ================= HARDWARE FREENOVE S3 =================
const int BUTTON_PIN = 0; // Botón BOOT
const int SDA_PIN = 1;    // Pin I2C SDA S3
const int SCL_PIN = 2;    // Pin I2C SCL S3

// Configuración OLED (SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Configuración Cámara (Freenove S3 Standard)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 40
#define SIOD_GPIO_NUM 17
#define SIOC_GPIO_NUM 18
#define Y9_GPIO_NUM 39
#define Y8_GPIO_NUM 41
#define Y7_GPIO_NUM 42
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 3
#define Y4_GPIO_NUM 14
#define Y3_GPIO_NUM 5
#define Y2_GPIO_NUM 13
#define VSYNC_GPIO_NUM 21
#define HREF_GPIO_NUM 38
#define PCLK_GPIO_NUM 11

WiFiClientSecure client;

// Variables para polling (revisar mensajes)
unsigned long lastCheckTime = 0;
const long checkInterval = 2000; // Revisar cada 2 segundos

// -----------------------------------------------------------------
// ANIMACIONES Y PANTALLA
// -----------------------------------------------------------------

void showText(String title, String subtitle)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(title);

    display.setTextSize(2);
    display.setCursor(0, 20);
    // Ajuste de tamaño si el texto es muy largo
    if (subtitle.length() > 10)
        display.setTextSize(1);
    display.println(subtitle);
    display.display();
}

// Animación estilo "Jarvis/Alexa"
void playAIAnimation(int duration_ms)
{
    long startTime = millis();
    int numBars = 8;
    int barWidth = 10;
    int spacing = 4;
    int startX = (SCREEN_WIDTH - (numBars * (barWidth + spacing))) / 2;

    while (millis() - startTime < duration_ms)
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(30, 0);
        display.print("ANALIZANDO...");

        for (int i = 0; i < numBars; i++)
        {
            int height = random(5, 40);
            int x = startX + i * (barWidth + spacing);
            int y = SCREEN_HEIGHT - height;

            display.fillRect(x, y, barWidth, height, WHITE);
            display.fillRect(x, y - 4, barWidth, 2, WHITE);
        }
        display.display();
        delay(50);
    }
}

// -----------------------------------------------------------------
// FUNCIÓN: REVISAR MENSAJES WEB (POLLING)
// -----------------------------------------------------------------
void checkMessages()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    http.setInsecure(); // Ngrok Free SSL

    // Construir URL completa: https://host/esp32/message
    String url = String("https://") + host + api_message;

    if (http.begin(url))
    {
        int httpCode = http.GET();

        if (httpCode > 0)
        {
            String payload = http.getString();

            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error)
            {
                bool hasMessage = doc["has_message"];
                if (hasMessage)
                {
                    String text = doc["text"].as<String>();

                    Serial.println("Mensaje Web: " + text);

                    // Efecto visual de notificación
                    display.invertDisplay(true);
                    delay(200);
                    display.invertDisplay(false);

                    showText("NUEVO MSJ:", text);

                    // Dejar el mensaje en pantalla unos segundos
                    delay(4000);
                    showText("Listo", "Btn -> Foto");
                }
            }
        }
        http.end();
    }
}

// -----------------------------------------------------------------
// FUNCIÓN PRINCIPAL: CAPTURA Y ENVÍO
// -----------------------------------------------------------------
void captureAndSend()
{
    display.fillScreen(WHITE); // Efecto Flash
    display.display();
    delay(100);
    display.clearDisplay();

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        showText("Error", "Camara Fail");
        delay(2000);
        return;
    }

    client.setInsecure();

    // Animación mientras conecta
    playAIAnimation(1000);

    if (client.connect(host, httpsPort))
    {

        String boundary = "---FreenoveS3Boundary";
        String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"esp32.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
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

        esp_camera_fb_return(fb);

        // Animación mientras espera respuesta
        long timeout = millis();
        while (client.connected() && !client.available())
        {
            playAIAnimation(50);
            if (millis() - timeout > 15000)
            {
                showText("Error", "Timeout");
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
        DeserializationError error = deserializeJson(doc, response);

        if (!error)
        {
            if (doc.containsKey("oled_text") && doc["oled_text"].as<String>().length() > 0)
            {
                String autoMsg = doc["oled_text"].as<String>();
                // Mostrar resultado final
                display.clearDisplay();
                display.setTextSize(1);
                display.setCursor(0, 0);
                display.println("RESULTADO:");
                display.setTextSize(2);
                display.setCursor(0, 25);
                if (autoMsg.length() > 10)
                    display.setTextSize(1);
                display.println(autoMsg);
                display.display();
            }
            else
            {
                // Modo Manual o predicción estándar
                String pred = doc["prediction"].as<String>();
                float conf = doc["confidence"];
                showText("Manual:", pred + " " + String(conf, 0) + "%");
            }
        }
        else
        {
            showText("Error JSON", "Formato");
        }
    }
    else
    {
        showText("Error Conex", "Ngrok Down?");
        esp_camera_fb_return(fb);
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Init OLED S3 (Pines 1 y 2)
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println(F("Fallo OLED"));
        while (true)
            ;
    }
    display.clearDisplay();
    display.setTextColor(WHITE);

    // Pantalla de inicio
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("KALEB AI");
    display.display();
    delay(1000);

    // Init Cámara
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
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;

    if (esp_camera_init(&config) != ESP_OK)
    {
        showText("Error", "Cam Init Fail");
        while (true)
            ;
    }

    WiFi.begin(ssid, password);
    showText("Conectando", "WiFi...");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }

    showText("Listo", "Btn -> Foto");
}

void loop()
{
    // 1. Revisar Botón (Prioridad Alta - Foto)
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        delay(50);
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            captureAndSend();
            delay(3000);
            showText("Listo", "Btn -> Foto");
        }
    }

    // 2. Revisar Mensajes Web (Segundo plano - Cada 2 seg)
    if (millis() - lastCheckTime > checkInterval)
    {
        checkMessages();
        lastCheckTime = millis();
    }
}