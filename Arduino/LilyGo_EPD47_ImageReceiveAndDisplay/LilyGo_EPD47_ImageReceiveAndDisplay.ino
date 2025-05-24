#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "Arduino.h"
#include <WiFiClient.h>
#include <ArduinoJson.h>

#include "epd_driver.h"

#include "base64.hpp"

#include <HTTPClient.h>
#include <JPEGDecoder.h>

#include "settings.h"

#include "opensans8b.h"
#include "opensans12b.h"
#include "opensans18b.h"
#include "opensans26b.h"

String appVersion="1.0";

GFXfont  currentFont;
uint8_t *framebuffer;

String ipaddr="";
enum alignment {LEFT, RIGHT, CENTER};

WebServer server(80);


void getAppVersion() {
  String postBody = server.arg("plain");
  Serial.setTimeout(10000);
  DynamicJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str()); 
        String msg = error.c_str();
        server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
    } else {
        JsonObject postObj = doc.as<JsonObject>(); 
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method()); 
        if (server.method() == HTTP_POST) {
                DynamicJsonDocument doc1(512);
                doc1["status"] = "OK";
                doc1["version"] = appVersion;
                doc1["message"] = F("Fine!");
 
                Serial.print(F("Stream..."));
                String buf1;
                serializeJson(doc1, buf1);
 
                server.send(201, F("application/json"), buf1);
        }
    }
}

void geturi() {
  // Puffer für Bilddaten
  #define MAX_IMAGE_SIZE  (200 * 1024)
  uint8_t *imgBuffer = nullptr;

  String postBody = server.arg("plain");
  Serial.setTimeout(10000);
  DynamicJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str()); 
        String msg = error.c_str();
        server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
    } else {
        JsonObject postObj = doc.as<JsonObject>(); 
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method()); 

        if (server.method() == HTTP_POST) {
            if (postObj.containsKey("name") && postObj.containsKey("uri")) {

              const char* uri = postObj["uri"];

// TODO
              HTTPClient http;
              http.begin(uri);
              int status = http.GET();
              if (status != HTTP_CODE_OK) {
                Serial.printf("HTTP-Fehler: %d\n", status);
                http.end();
                return;
              }

              // Länge ermitteln und Puffer anlegen
              int len = http.getSize();
              if (len > MAX_IMAGE_SIZE) len = MAX_IMAGE_SIZE;
              imgBuffer = (uint8_t*)malloc(len);
              http.getStream().readBytes(imgBuffer, len);
              http.end();

              // JPEG decodieren
              JpegDec.decodeArray(imgBuffer, len);
              uint16_t w = JpegDec.width;
              uint16_t h = JpegDec.height;

              // Graustufen-Puffer anlegen
              uint8_t *gray = (uint8_t*)malloc(w * h);
              if (!gray) {
                Serial.println("Kein RAM für gray");
                free(imgBuffer);
                return;
              }

              // Pixel auslesen und in Graustufen umwandeln
              uint16_t *pImg;
              int count = 0;
              while (JpegDec.read()) {
                pImg = JpegDec.pImage;              // 16-bit RGB565
                for (int i = 0; i < (w * h); i++) {
                  uint16_t px = pImg[i];
                  uint8_t r = (px >> 11) & 0x1F;
                  uint8_t g = (px >> 5)  & 0x3F;
                  uint8_t b = px         & 0x1F;
                  // einfache Graustufen‐Berechnung
                  gray[count++] = (r*8*30 + g*4*59 + b*8*11) / 100;
                }
              }

              Rect_t area = {
                .x = (EPD_WIDTH - w) / 2,
                .y = (EPD_HEIGHT - h) / 2,
                .width = w,
                .height = h
              };

              epd_poweron();
              epd_clear();
              epd_draw_grayscale_image(area, gray);
              // epd_display(); // notwendig?
              epd_poweroff();

              free(gray);
              free(imgBuffer);

              Serial.println(F("done."));

              DynamicJsonDocument doc1(512);
              doc1["status"] = "OK";
              doc1["message"] = F("Fine!");

              Serial.print(F("Stream..."));
              String buf1;
              serializeJson(doc1, buf1);

              server.send(201, F("application/json"), buf1);
              Serial.print(F("done."));
            }else {
                DynamicJsonDocument doc(512);
                doc["status"] = "KO";
                doc["message"] = F("No data found, or incorrect!");
 
                Serial.print(F("Stream..."));
                String buf;
                serializeJson(doc, buf);
 
                server.send(400, F("application/json"), buf);
                Serial.print(F("done."));
            }
        }
    }
}

void turnOff() {
  String postBody = server.arg("plain");
  Serial.setTimeout(10000);
  DynamicJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str()); 
        String msg = error.c_str();
        server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
    } else {
        JsonObject postObj = doc.as<JsonObject>(); 
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method()); 
        if (server.method() == HTTP_POST) {
            if (postObj.containsKey("name")) {
                epd_poweroff_all();
                esp_deep_sleep_start();
            }
        }
                DynamicJsonDocument doc1(512);
                doc1["status"] = "OK";
                doc1["message"] = F("Fine!");
 
                Serial.print(F("Stream..."));
                String buf1;
                serializeJson(doc1, buf1);
 
                server.send(201, F("application/json"), buf1);
    }
}

void infoScreen() {
  clearScreen();
  setFont(OpenSans26B);
  drawString(EPD_WIDTH/2,  50, "Welcome", CENTER);
  setFont(OpenSans12B);
  drawString(EPD_WIDTH/2, 150, "Connected to " + String(ssid), CENTER);
  drawString(EPD_WIDTH/2, 250, "IP address: " + ipaddr, CENTER);
  drawString(EPD_WIDTH/2, 350, "Ready, send image file...", CENTER);
}

void clearScreen() {
  String postBody = server.arg("plain");
  Serial.setTimeout(10000);
  DynamicJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str()); 
        String msg = error.c_str();
        server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
    } else {
        JsonObject postObj = doc.as<JsonObject>(); 
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method()); 
        if (server.method() == HTTP_POST) {
            if (postObj.containsKey("name")) {
              //epd_poweron();
              epd_clear();
// war textausgabe
              epd_poweroff();
            }
        }
                DynamicJsonDocument doc1(512);
                doc1["status"] = "OK";
                doc1["message"] = F("Fine!");
 
                Serial.print(F("Stream..."));
                String buf1;
                serializeJson(doc1, buf1);
 
                server.send(201, F("application/json"), buf1);
    }
}

void setImage() {
    String postBody = server.arg("plain");
    //Serial.println(postBody);
               epd_poweron();
              //epd_clear();
              Serial.setTimeout(10000);
    DynamicJsonDocument doc(300000);
    DeserializationError error = deserializeJson(doc, postBody);
    if (error) {
        // if the file didn't open, print an error:
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str());
 
        String msg = error.c_str();
 
        server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
 
    } else {
        JsonObject postObj = doc.as<JsonObject>();
 
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method());
 
        if (server.method() == HTTP_POST) {
            if (postObj.containsKey("name") && postObj.containsKey("type")) {

              const char* encoded = postObj["type"];
              const int x = postObj["startx"];
              const int y = postObj["starty"];
              const int x2 = postObj["width"];
              const int y2 = postObj["height"];

              Serial.printf("decodedLength: %d\n",BASE64::decodeLength(encoded));
              uint8_t raw[BASE64::decodeLength(encoded)];
              BASE64::decode(encoded, raw);

               Rect_t area = {
                .x = x,
                .y = y,
                .width  = x2,
                .height = y2
              };

              epd_poweron();
              if(x==0 && y==0) {
                epd_clear();
              }
              epd_draw_grayscale_image(area, (uint8_t *) raw);
              epd_poweroff();
 
              Serial.println(F("done."));

              DynamicJsonDocument doc1(512);
              doc1["status"] = "OK";
              doc1["message"] = F("Fine!");

              Serial.print(F("Stream..."));
              String buf1;
              serializeJson(doc1, buf1);

              server.send(201, F("application/json"), buf1);
              Serial.print(F("done."));
 
             } else {
                DynamicJsonDocument doc(512);
                doc["status"] = "KO";
                doc["message"] = F("No data found, or incorrect!");

                Serial.print(F("Stream..."));
                String buf;
                serializeJson(doc, buf);

                server.send(400, F("application/json"), buf);
                Serial.print(F("done."));
            }
        }
    }
}

void restServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"),
            F("Welcome to the EPD47 Display Web Server"));
    });
    server.on(F("/setImage"), HTTP_POST, setImage);
    server.on(F("/clearScreen"), HTTP_POST, clearScreen);
    server.on(F("/turnOff"), HTTP_POST, turnOff);
    server.on(F("/appVersion"), HTTP_POST, getAppVersion);
    server.on(F("/url"), HTTP_POST, geturi);
}

// Manage not found URL
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
 
void setup(void) {
  Serial.begin(115200);
  
  epd_init();

  framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  epd_poweron();
  epd_clear();
  

  delay(3000);

  Serial.print("Verbinde mit: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
 
  // Wait for connection
  Serial.print("Hole IP ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Eigene IP: ");
  Serial.println(WiFi.localIP());
  ipaddr=WiFi.localIP().toString();;

  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin(mdnsname)) {
    Serial.println("MDNS responder started");
  }

  // Set server routing
  restServerRouting();
  // Set not found response
  server.onNotFound(handleNotFound);
  // Start server
  server.begin();
  Serial.println("HTTP server started");

  infoScreen();
}
 
void loop(void) {
 server.handleClient();
}

void setFont(GFXfont const &font) {
  currentFont = font;
}

void drawString(int x, int y, String text, alignment align) {
  char * data  = const_cast<char*>(text.c_str());
  int  x1, y1; 
  int w, h;
  int xx = x, yy = y;
  get_text_bounds(&currentFont, data, &xx, &yy, &x1, &y1, &w, &h, NULL);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  int cursor_y = y + h;
  write_string(&currentFont, data, &x, &cursor_y, framebuffer);
  epd_draw_grayscale_image(epd_full_screen(), framebuffer); 
}
