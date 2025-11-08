#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"
#include "Globals.h"

#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ArduinoJson.h>

#define AP_SSID "ZombieDisplay"
#define AP_PWD  "DisplayZombie"

#define FORMAT_SPIFFS_IF_FAILED true

CanSDO canSdo;
DisplayManager displayManager(canSdo);
DataRetriever dataRetriever(canSdo, displayManager);
InputManager inputManager(displayManager);
hw_timer_t * timer = NULL;
AsyncWebServer server(80);

// Global variable to store parsed parameters
DynamicJsonDocument paramsDoc(40960);  // 40KB to handle large params.json file

volatile bool requestNextData = false;

void IRAM_ATTR timerInterrupt() {
    requestNextData = true;
}

void flushThunk( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p ) {
  displayManager.Flusher(disp, area, color_p);
}

void clickThunk() {
   displayManager.ProcessClickInput();
}

void doubleclickThunk() {
   displayManager.ProcessDoubleClickInput();
}

// handles uploads
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

  if (!index) {
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SPIFFS.open("/" + filename, "w");
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
  }

  if (final) {
    // close the file handle as the upload is now done
    request->_tempFile.close();
    request->redirect("/");
  }
}

bool parseParamsFile() {
  // Open file for reading
  File file = SPIFFS.open("/params.json", "r");
  if (!file) {
    Serial.println("Failed to open params.json file");
    return false;
  }

  // Parse JSON from file
  DeserializationError error = deserializeJson(paramsDoc, file);
  file.close();
  
  if (error) {
    Serial.print("Failed to parse params.json: ");
    Serial.println(error.c_str());
    return false;
  }
  
  Serial.print("JSON parsed successfully. Parameter count: ");
  Serial.println(paramsDoc.size());
  
  // Add isFavorite and isHidden fields to all parameters if they don't exist
  for (JsonPair param : paramsDoc.as<JsonObject>()) {
    JsonObject paramObj = param.value().as<JsonObject>();
    if (paramObj.containsKey("isparam") && paramObj["isparam"].as<bool>()) {
      if (!paramObj.containsKey("isFavorite")) {
        paramObj["isFavorite"] = false;
      }
      if (!paramObj.containsKey("isHidden")) {
        paramObj["isHidden"] = false;
      }
    }
  }

  return true;
}

// Save parameters back to SPIFFS
bool saveParamsFile() {
  File file = SPIFFS.open("/params.json", "w");
  if (!file) {
    Serial.println("Failed to open params.json for writing");
    return false;
  }
  
  if (serializeJson(paramsDoc, file) == 0) {
    Serial.println("Failed to write params.json");
    file.close();
    return false;
  }
  
  file.close();
  return true;
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);
  delay(500);

  
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      Serial.println("SPIFFS Mount Failed");
      return;
  }

  if (parseParamsFile()) {
    Serial.println("Parameters loaded successfully!");
////    printParams();
  } else {
    Serial.println("Failed to load parameters");
  }
  
  displayManager.Setup();
  inputManager.Setup();
  canSdo.Setup();

  timer = timerBegin(0, 240, true); // Timer 0, clock divisor 80
  timerAttachInterrupt(timer, &timerInterrupt, true); // Attach the interrupt handling function
  timerAlarmWrite(timer, 50000, true); // Interrupt every 50ms
  timerAlarmEnable(timer); // Enable the alarm

  //wifi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PWD);

   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SPIFFS.exists("/index.html")) {
            request->send(SPIFFS, "/index.html", "text/html");
        } else {
            // Serve embedded HTML if index.html doesn't exist
            request->send(200, "text/html", "No SPIFFS Files");
        }
  });
  
  // Serve the params page
  server.on("/params", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SPIFFS.exists("/params.html")) {
            request->send(SPIFFS, "/params.html", "text/html");
        } else {
            request->send(404, "text/plain", "Params page not found");
        }
  });

  // Serve the settings page
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SPIFFS.exists("/settings.html")) {
            request->send(SPIFFS, "/settings.html", "text/html");
        } else {
            request->send(404, "text/plain", "Settings page not found");
        }
  });

  // Serve the spot params page
  server.on("/spotparams", HTTP_GET, [](AsyncWebServerRequest *request){
        if (SPIFFS.exists("/spotparams.html")) {
            request->send(SPIFFS, "/spotparams.html", "text/html");
        } else {
            request->send(404, "text/plain", "Spot params page not found");
        }
  });
  
  // API endpoint to get all parameters
  server.on("/api/params", HTTP_GET, [](AsyncWebServerRequest *request){
        String response;
        serializeJson(paramsDoc, response);
        request->send(200, "application/json", response);
  });
  
  // API endpoint to save favorites
  server.on("/api/favorites", HTTP_POST, 
    [](AsyncWebServerRequest *request){
        // This will be called when the request is complete
    }, 
    NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        static String jsonBuffer = "";
        
        // Accumulate data chunks
        if (index == 0) {
            jsonBuffer = "";
        }
        
        // Add current chunk to buffer
        for (size_t i = 0; i < len; i++) {
            jsonBuffer += (char)data[i];
        }
        
        // Process when all data is received
        if (index + len == total) {
            Serial.print("Received JSON data: ");
            Serial.println(jsonBuffer.length());
            
            // Parse the complete JSON data
            DynamicJsonDocument tempDoc(40960);
            DeserializationError error = deserializeJson(tempDoc, jsonBuffer);
            
            if (error) {
                Serial.print("JSON parse error: ");
                Serial.println(error.c_str());
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            
            // Copy the data to our global document
            paramsDoc = tempDoc;
            
            // Save to SPIFFS
            if (saveParamsFile()) {
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Favorites saved successfully\"}");
                Serial.println("Favorites saved successfully");
            } else {
                request->send(500, "application/json", "{\"error\":\"Failed to save to SPIFFS\"}");
                Serial.println("Failed to save favorites to SPIFFS");
            }
            
            // Clear buffer for next request
            jsonBuffer = "";
        }
    });

  // API endpoint to get settings
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        // Open settings file if it exists
        if (SPIFFS.exists("/settings.json")) {
            File file = SPIFFS.open("/settings.json", "r");
            if (file) {
                String settings = file.readString();
                file.close();
                request->send(200, "application/json", settings);
                return;
            }
        }
        // Return default settings if file doesn't exist
        request->send(200, "application/json", "{\"rotation\":0}");
  });

  // API endpoint to save settings
  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *request){
        // This will be called when the request is complete
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        static String jsonBuffer = "";

        // Accumulate data chunks
        if (index == 0) {
            jsonBuffer = "";
        }

        // Add current chunk to buffer
        for (size_t i = 0; i < len; i++) {
            jsonBuffer += (char)data[i];
        }

        // Process when all data is received
        if (index + len == total) {
            Serial.print("Received settings data: ");
            Serial.println(jsonBuffer);

            // Parse the JSON data
            DynamicJsonDocument settingsDoc(1024);
            DeserializationError error = deserializeJson(settingsDoc, jsonBuffer);

            if (error) {
                Serial.print("Settings JSON parse error: ");
                Serial.println(error.c_str());
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            // Save to SPIFFS
            File file = SPIFFS.open("/settings.json", "w");
            if (!file) {
                request->send(500, "application/json", "{\"error\":\"Failed to save settings\"}");
                Serial.println("Failed to open settings.json for writing");
                return;
            }

            if (serializeJson(settingsDoc, file) == 0) {
                file.close();
                request->send(500, "application/json", "{\"error\":\"Failed to write settings\"}");
                Serial.println("Failed to write settings.json");
                return;
            }

            file.close();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Settings saved successfully\"}");
            Serial.println("Settings saved successfully");

            // Clear buffer for next request
            jsonBuffer = "";
        }
    });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200);
    }, handleUpload);

  AsyncElegantOTA.begin(&server);    // Start AsyncElegantOTA
  server.begin();


}

void loop() {

  displayManager.Loop();
  inputManager.Loop();

  if (requestNextData) {
     requestNextData = false;
     if (displayManager.GetScreenIndex() == SPOTPARAMSCREEN) {
       dataRetriever.GetSpotParameterValue();
     } else if (displayManager.GetScreenIndex() == PARAMETERSCREEN) {
       dataRetriever.GetParameterValue();
     }
     else {
       dataRetriever.GetNextValue();
     }
  }

}
