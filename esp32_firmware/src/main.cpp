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
const char *host = "usually-stable-lacewing.ngrok-free.app";
const int httpsPort = 443;
const char *api_predict = "/predict";
const char *api_message = "/esp32/message";

// ================= HARDWARE FREENOVE S3 WROOM (CORREGIDO) =================
// PINES PANTALLA OLED (Mantén tu cableado en 1 y 2)
const int SDA_PIN = 1;
const int SCL_PIN = 2;

// Botón BOOT (GPIO 0)
const int BUTTON_PIN = 0;

// Configuración OLED (SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PINES DE CÁMARA OFICIALES FREENOVE ESP32-S3 WROOM ---
// (Estos son los correctos para la placa Freenove negra/azul estándar)
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

WiFiClientSecure client;
unsigned long lastCheckTime = 0;
const long checkInterval = 2000;
bool isSystemStarted = false;

// -----------------------------------------------------------------
// ANIMACIONES
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
    if (subtitle.length() > 10)
        display.setTextSize(1);
    display.println(subtitle);
    display.display();
}

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
        display.print("PROCESANDO...");

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
// POLLING MENSAJES
// -----------------------------------------------------------------
void checkMessages()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    String url = String("https://") + host + api_message;

    if (http.begin(secureClient, url))
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

                    display.invertDisplay(true);
                    delay(200);
                    display.invertDisplay(false);

                    showText("NUEVO MSJ:", text);
                    delay(4000);
                    showText("Listo", "Btn -> Foto");
                }
            }
        }
        http.end();
    }
}

// -----------------------------------------------------------------
// CAPTURA Y ENVIO
// -----------------------------------------------------------------
void captureAndSend()
{
    display.fillScreen(WHITE);
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
    playAIAnimation(1000);

    if (client.connect(host, httpsPort))
    {
        String boundary = "---FreenoveBoundary";
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

    // PINES I2C: 1 y 2
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println(F("Fallo OLED"));
        for (;;)
            ;
    }

    display.clearDisplay();
    display.display();

    // CONFIG CÁMARA FREENOVE
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
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;

    if (esp_camera_init(&config) != ESP_OK)
    {
        showText("Error", "Cam Init Fail");
        while (true)
            ;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Conectando WiFi...");
    display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(15, 20);
    display.println("INICIAR");
    display.setTextSize(1);
    display.setCursor(25, 45);
    display.println("Click ->");
    display.display();
}

void loop()
{
    if (digitalRead(BUTTON_PIN) == LOW)
    {
        delay(50);
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            if (!isSystemStarted)
            {
                isSystemStarted = true;
                playAIAnimation(2000);
                showText("SISTEMA", "LISTO");
                delay(1000);
                showText("Listo", "Btn -> Foto");
            }
            else
            {
                captureAndSend();
                delay(2000);
                showText("Listo", "Btn -> Foto");
            }
        }
    }

    if (isSystemStarted)
    {
        if (millis() - lastCheckTime > checkInterval)
        {
            checkMessages();
            lastCheckTime = millis();
        }
    }
}