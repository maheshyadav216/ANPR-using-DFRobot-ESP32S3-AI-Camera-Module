//=============================================================================//
// Project/Tutorial       - ANPR using DFRobot ESP32S3 AI Camera Module
// Author                 - https://www.hackster.io/maheshyadav216
// Hardware               - DFRobot ESP32S3 AI Camera Module    
// Sensors                - NA
// Software               - Arduino IDE, Gemini 2.0 Flash API
// GitHub Repo of Project - https://github.com/maheshyadav216/ANPR-using-DFRobot-ESP32S3-AI-Camera-Module 
// Code last Modified on  - 15/06/2025
// Code/Content license   - (CC BY-NC-SA 4.0) https://creativecommons.org/licenses/by-nc-sa/4.0/
//============================================================================//

// Include Necessary Libraries 
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "time.h"

// Pin definitions for DFR1154 ESP32-S3 AI CAM (OV3660)
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     5
#define Y9_GPIO_NUM       4
#define Y8_GPIO_NUM       6
#define Y7_GPIO_NUM       7
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      17
#define Y4_GPIO_NUM      21
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM      16
#define VSYNC_GPIO_NUM    1
#define HREF_GPIO_NUM     2
#define PCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     8
#define SIOC_GPIO_NUM     9

// ======= CONFIGURATION =======

// WiFi Credentials
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";

// Gemini AI API Key
const char* GEMINI_API_KEY = "";

// Firebase URL (Set if using Firebase)
const char* FIREBASE_URL = "";

// Enable Firebase integration
#define ENABLE_FIREBASE true  // Set to false to disable Firebase

// NTP Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// ======= BASE64 ENCODING =======
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t length) {
    String encoded = "";
    int i = 0;
    uint8_t array_3[3], array_4[4];

    while (length--) {
        array_3[i++] = *(data++);
        if (i == 3) {
            array_4[0] = (array_3[0] & 0xfc) >> 2;
            array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
            array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
            array_4[3] = array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_table[array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            array_3[j] = '\0';

        array_4[0] = (array_3[0] & 0xfc) >> 2;
        array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
        array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
        array_4[3] = array_3[2] & 0x3f;

        for (int j = 0; j < i + 1; j++)
            encoded += base64_table[array_4[j]];

        while ((i++ < 3))
            encoded += '=';
    }

    return encoded;
}

// ======= GET DATE AND TIME =======
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time Error";
    }
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// ======= SEND TO FIREBASE =======
void sendDataToFirebase(const String& numberPlate, const String& dateTime, const String& imageBase64) {
    if (!ENABLE_FIREBASE) return;

    HTTPClient http;
    http.begin(FIREBASE_URL);
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"number_plate\":\"" + numberPlate + "\",";
    payload += "\"date_time\":\"" + dateTime + "\",";
    payload += "\"image\":\"" + imageBase64 + "\"";
    payload += "}";

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        Serial.println("[Firebase] Response: " + http.getString());
    } else {
        Serial.println("[Firebase] Error: " + String(httpCode));
    }
    http.end();
}

// ======= DETECT LICENSE PLATE =======
void detectNumberPlate() {
    Serial.println("\n[+] Capturing Image...");
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[-] Capture failed");
        return;
    }

    String base64Image = base64_encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    Serial.println("[+] Image captured. Sending to Gemini...");

    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + String(GEMINI_API_KEY);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"contents\":[{\"parts\":[";
    payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}},";
    payload += "{\"text\":\"Detect and extract the vehicle number plate text from this image. If a number plate is present, return only the plate number in plain text. If no plate is found, return 'No Plate'.\"}";
    payload += "]}]}";

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        String response = http.getString();
        Serial.println("[Gemini] Response: " + response);

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            return;
        }

        const char* aiText = doc["candidates"][0]["content"]["parts"][0]["text"];
        String plateNumber = String(aiText);

        // === Validate Number Plate ===
        String lower = plateNumber;
        lower.toLowerCase();
        plateNumber.trim();
        plateNumber.replace("\"", "");

        bool isValidPlate = (
            aiText &&
            plateNumber.length() >= 3 &&
            plateNumber.length() <= 15 &&
            lower.indexOf("no plate") == -1 &&
            lower.indexOf("i'm afraid") == -1 &&
            lower.indexOf("sorry") == -1 &&
            lower.indexOf("cannot") == -1
        );

        if (!isValidPlate) {
            Serial.println("[!] No valid number plate detected");
            return;
        }
        String dateTime = getCurrentTime();
        Serial.println("\n======= License Plate Detected =======");
        Serial.println("🕒 Date and Time     : " + dateTime);
        Serial.println("🔢 Licence Plate No. : " + plateNumber);
        Serial.println("======================================");

        sendDataToFirebase(plateNumber, dateTime, base64Image);
    } else {
        Serial.println("[-] HTTP Request Failed: " + String(httpCode));
    }

    http.end();
}

// ======= SETUP =======
void setup() {
    Serial.begin(115200);
    Serial.println("\n[+] Starting...");

    Serial.println("Initializing camera...");

      camera_config_t config;
      config.ledc_channel = LEDC_CHANNEL_0;
      config.ledc_timer   = LEDC_TIMER_0;
      config.pin_d0       = Y2_GPIO_NUM;
      config.pin_d1       = Y3_GPIO_NUM;
      config.pin_d2       = Y4_GPIO_NUM;
      config.pin_d3       = Y5_GPIO_NUM;
      config.pin_d4       = Y6_GPIO_NUM;
      config.pin_d5       = Y7_GPIO_NUM;
      config.pin_d6       = Y8_GPIO_NUM;
      config.pin_d7       = Y9_GPIO_NUM;
      config.pin_xclk     = XCLK_GPIO_NUM;
      config.pin_pclk     = PCLK_GPIO_NUM;
      config.pin_vsync    = VSYNC_GPIO_NUM;
      config.pin_href     = HREF_GPIO_NUM;
      config.pin_sccb_sda = SIOD_GPIO_NUM;
      config.pin_sccb_scl = SIOC_GPIO_NUM;
      config.pin_pwdn     = PWDN_GPIO_NUM;
      config.pin_reset    = RESET_GPIO_NUM;
      config.xclk_freq_hz = 20000000;
      config.pixel_format = PIXFORMAT_JPEG;  // Only for testing capture
      config.frame_size   = FRAMESIZE_QVGA;  // 320x240
      config.jpeg_quality = 10;
      config.fb_count     = psramFound() ? 2 : 1;
      config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
      config.grab_mode    = CAMERA_GRAB_LATEST;

      esp_err_t err = esp_camera_init(&config);
      if (err != ESP_OK) {
        Serial.printf("Camera init failed! Error: 0x%x\n", err);
        return;
      }

      Serial.println("Camera initialized successfully!");

  // Optional sensor tweaks
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[+] Connecting WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[+] WiFi Connected: " + WiFi.localIP().toString());

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("[+] Time sync done");

    // Run detection repeatedly
    xTaskCreate([](void*) {
        while (1) {
            detectNumberPlate();
            delay(20000);  // every 20 sec
        }
    }, "ANPRTask", 8192, NULL, 1, NULL);
}

// ======= LOOP =======
void loop() {
    delay(1000); // do nothing, task runs detection
}
