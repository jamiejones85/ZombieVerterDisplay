#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"
#include "Globals.h"
#include "GVRETServer.h"
#include "SerialCommandHandler.h"

#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ArduinoJson.h>
#include "driver/twai.h"

#define AP_SSID "ZombieDisplay"
#define AP_PWD  "DisplayZombie"

#define FORMAT_SPIFFS_IF_FAILED true

CanSDO canSdo;
DisplayManager displayManager(canSdo);
DataRetriever dataRetriever(canSdo, displayManager);
InputManager inputManager(displayManager);
SerialCommandHandler serialCommandHandler(canSdo);
hw_timer_t * timer = NULL;
AsyncWebServer server(80);

// Global variable to store parsed parameters
DynamicJsonDocument paramsDoc(40960);  // 40KB to handle large params.json file

volatile bool requestNextData = false;
bool gvretRunning = false;
CanSDO::FetchResult lastFetchResult = CanSDO::Failed;
CanSDO::UpdateState lastVcuUpdateState = CanSDO::UPD_IDLE;

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

// handles VCU firmware uploads
void handleVcuFirmwareUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    Serial.println("=== Starting VCU firmware upload ===");
    Serial.print("Filename: ");
    Serial.println(filename);

    // Delete old file if it exists
    if (SPIFFS.exists("/vcu_firmware.bin")) {
      SPIFFS.remove("/vcu_firmware.bin");
      Serial.println("Deleted old VCU firmware file");
    }

    request->_tempFile = SPIFFS.open("/vcu_firmware.bin", "w");
    if (!request->_tempFile) {
      Serial.println("ERROR: Failed to open VCU firmware file for writing");
    } else {
      Serial.println("VCU firmware file opened for writing");
    }
  }

  if (len) {
    size_t written = request->_tempFile.write(data, len);
    if (written != len) {
      Serial.print("WARNING: Write mismatch - requested: ");
      Serial.print(len);
      Serial.print(", written: ");
      Serial.println(written);
    }
  }

  if (final) {
    request->_tempFile.close();
    size_t totalSize = index + len;
    Serial.print("VCU firmware upload complete: ");
    Serial.print(totalSize);
    Serial.println(" bytes");

    // Verify file was written
    if (SPIFFS.exists("/vcu_firmware.bin")) {
      File f = SPIFFS.open("/vcu_firmware.bin", "r");
      if (f) {
        Serial.print("Verified file size: ");
        Serial.print(f.size());
        Serial.println(" bytes");
        f.close();
      }
    } else {
      Serial.println("ERROR: VCU firmware file was not saved!");
    }

    request->send(200, "application/json", "{\"success\":true,\"message\":\"VCU firmware uploaded\"}");
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


  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
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
  serialCommandHandler.Setup();

  // Give DisplayManager access to SerialCommandHandler for on-screen settings
  displayManager.SetSerialCommandHandler(&serialCommandHandler);

  // Load serial relay setting from settings.json
  if (SPIFFS.exists("/settings.json")) {
    File settingsFile = SPIFFS.open("/settings.json", "r");
    if (settingsFile) {
      DynamicJsonDocument settingsDoc(1024);
      DeserializationError error = deserializeJson(settingsDoc, settingsFile);
      settingsFile.close();

      if (!error && settingsDoc.containsKey("serialRelayEnabled")) {
        bool relayEnabled = settingsDoc["serialRelayEnabled"].as<bool>();
        serialCommandHandler.SetEnabled(relayEnabled);
        Serial.print("Loaded serial relay setting: ");
        Serial.println(relayEnabled ? "enabled" : "disabled");
      }
    }
  }

  timer = timerBegin(0, 240, true); // Timer 0, clock divisor 80
  timerAttachInterrupt(timer, &timerInterrupt, true); // Attach the interrupt handling function
  timerAlarmWrite(timer, 50000, true); // Interrupt every 50ms
  timerAlarmEnable(timer); // Enable the alarm

  //wifi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PWD);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/index.html")) {
      request->send(SPIFFS, "/index.html", "text/html");
    } else {
      // Serve embedded HTML if index.html doesn't exist
      request->send(200, "text/html", "No SPIFFS Files");
    }
  });

  // Serve the params page
  server.on("/params", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/params.html")) {
      request->send(SPIFFS, "/params.html", "text/html");
    } else {
      request->send(404, "text/plain", "Params page not found");
    }
  });

  // Serve the settings page
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/settings.html")) {
      request->send(SPIFFS, "/settings.html", "text/html");
    } else {
      request->send(404, "text/plain", "Settings page not found");
    }
  });

  // Serve the spot params page
  server.on("/spotparams", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/spotparams.html")) {
      request->send(SPIFFS, "/spotparams.html", "text/html");
    } else {
      request->send(404, "text/plain", "Spot params page not found");
    }
  });

  // API endpoint to get all parameters (structure and metadata)
  server.on("/api/params", HTTP_GET, [](AsyncWebServerRequest * request) {
    String response;
    serializeJson(paramsDoc, response);
    request->send(200, "application/json", response);
  });

  // API endpoint to get current parameter values from VCU
  server.on("/api/params/values", HTTP_GET, [](AsyncWebServerRequest * request) {
    DynamicJsonDocument valuesDoc(8192);

    // Iterate through all parameters and get current values
    for (JsonPair param : paramsDoc.as<JsonObject>()) {
      JsonObject paramObj = param.value().as<JsonObject>();
      if (paramObj.containsKey("id") && paramObj.containsKey("isparam")) {
        int id = paramObj["id"].as<int>();

        // Get current value from VCU
        double currentValue = canSdo.GetValue(id);

        // Add to response
        valuesDoc[param.key().c_str()] = currentValue;
      }
    }

    String response;
    serializeJson(valuesDoc, response);
    request->send(200, "application/json", response);
  });

  // API endpoint to set a parameter value
  server.on("/api/params/set", HTTP_POST,
  [](AsyncWebServerRequest * request) {
    // This will be called when the request is complete
  },
  NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
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
      Serial.println("=== Parameter Set Request ===");
      Serial.print("JSON: ");
      Serial.println(jsonBuffer);

      // Parse the JSON data
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, jsonBuffer);

      if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        jsonBuffer = "";
        return;
      }

      if (!doc.containsKey("id") || !doc.containsKey("value")) {
        Serial.println("Missing id or value in request");
        request->send(400, "application/json", "{\"error\":\"Missing id or value\"}");
        jsonBuffer = "";
        return;
      }

      int id = doc["id"].as<int>();
      double value = doc["value"].as<double>();

      Serial.print("Setting parameter ID ");
      Serial.print(id);
      Serial.print(" to value ");
      Serial.println(value);

      // Use CanSDO to set the value
      CanSDO::SetResult result = canSdo.SetValue(id, value);

      Serial.print("SetValue result: ");
      Serial.println(result);

      switch (result) {
        case CanSDO::Ok:
          request->send(200, "application/json", "{\"success\":true}");
          Serial.println("Parameter updated successfully");
          break;
        case CanSDO::ValueOutOfRange:
          Serial.println("Error: Value out of range");
          request->send(400, "application/json", "{\"error\":\"Value out of range\"}");
          break;
        case CanSDO::UnknownIndex:
          Serial.println("Error: Unknown parameter ID");
          request->send(400, "application/json", "{\"error\":\"Unknown parameter ID\"}");
          break;
        case CanSDO::CommError:
          Serial.println("Error: Communication error");
          request->send(500, "application/json", "{\"error\":\"Communication error\"}");
          break;
        default:
          Serial.print("Error: Unknown result code ");
          Serial.println(result);
          request->send(500, "application/json", "{\"error\":\"Unknown error\"}");
          break;
      }

      // Clear buffer for next request
      jsonBuffer = "";
    }
  });

  // API endpoint to save favorites
  server.on("/api/favorites", HTTP_POST,
  [](AsyncWebServerRequest * request) {
    // This will be called when the request is complete
  },
  NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
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
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
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
    request->send(200, "application/json", "{\"rotation\":0,\"serialRelayEnabled\":true}");
  });

  // API endpoint to save settings
  server.on("/api/settings", HTTP_POST,
  [](AsyncWebServerRequest * request) {
    // This will be called when the request is complete
  },
  NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
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

      // Apply serial relay setting if present
      if (settingsDoc.containsKey("serialRelayEnabled")) {
        bool relayEnabled = settingsDoc["serialRelayEnabled"].as<bool>();
        serialCommandHandler.SetEnabled(relayEnabled);
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

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200);
  }, handleUpload);

  // Serve the setup page
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/setup.html")) {
      request->send(SPIFFS, "/setup.html", "text/html");
    } else {
      request->send(404, "text/plain", "Setup page not found");
    }
  });

  // GVRET server status API
  server.on("/api/gvret/status", HTTP_GET, [](AsyncWebServerRequest * request) {
    String response = "{\"running\":";
    response += gvretRunning ? "true" : "false";
    response += ",\"clientCount\":";
    response += gvretRunning ? (GVRETServer::getInstance().hasClients() ? "1" : "0") : "0";
    response += "}";
    request->send(200, "application/json", response);
  });

  // GVRET server start API
  server.on("/api/gvret/start", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (!gvretRunning) {
      GVRETServer::getInstance().begin();
      gvretRunning = true;
      Serial.println("GVRET server started via web UI");
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Server already running\"}");
    }
  });

  // GVRET server stop API
  server.on("/api/gvret/stop", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (gvretRunning) {
      GVRETServer::getInstance().stop();
      gvretRunning = false;
      Serial.println("GVRET server stopped via web UI");
      request->send(200, "application/json", "{\"success\":true}");
    } else {
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Server not running\"}");
    }
  });

  // Fetch params from VCU API
  server.on("/api/fetch-params", HTTP_POST, [](AsyncWebServerRequest * request) {
    Serial.println("Fetch params from VCU requested via web UI");

    // Check if already fetching
    CanSDO::FetchResult status = canSdo.GetJsonFetchStatus();
    if (status == CanSDO::InProgress) {
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Fetch already in progress\"}");
      return;
    }

    // Start the fetch
    canSdo.StartJsonFetch();
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Parameter fetch started\"}");
  });

  // VCU firmware upload API
  server.on("/api/vcu-firmware/upload", HTTP_POST,
  [](AsyncWebServerRequest * request) {
    // This will be called when the request is complete
  },
  handleVcuFirmwareUpload);

  // Start VCU firmware update API
  server.on("/api/vcu-firmware/update", HTTP_POST, [](AsyncWebServerRequest * request) {
    Serial.println("=== VCU firmware update requested via web UI ===");

    // Stop GVRET if running to avoid CAN message conflicts
    if (gvretRunning) {
      Serial.println("Stopping GVRET server for VCU update...");
      GVRETServer::getInstance().stop();
      gvretRunning = false;
    }

    // Check if firmware file exists
    if (!SPIFFS.exists("/vcu_firmware.bin")) {
      Serial.println("ERROR: /vcu_firmware.bin does not exist");
      request->send(200, "application/json", "{\"success\":false,\"error\":\"No firmware file uploaded\"}");
      return;
    }

    // Get file info
    File f = SPIFFS.open("/vcu_firmware.bin", "r");
    if (f) {
      Serial.print("VCU firmware file size: ");
      Serial.print(f.size());
      Serial.println(" bytes");
      f.close();
    } else {
      Serial.println("ERROR: Could not open /vcu_firmware.bin for reading");
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Cannot read firmware file\"}");
      return;
    }

    // Check if already updating
    CanSDO::UpdateState updateState = canSdo.GetVcuUpdateStatus();
    Serial.print("Current VCU update state: ");
    Serial.println(updateState);
    if (updateState != CanSDO::UPD_IDLE && updateState != CanSDO::UPDATE_COMPLETE && updateState != CanSDO::UPDATE_ERROR) {
      Serial.println("ERROR: Update already in progress");
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Update already in progress\"}");
      return;
    }

    // Start the update
    Serial.println("Calling canSdo.StartVcuUpdate()...");
    int totalPages = canSdo.StartVcuUpdate("/vcu_firmware.bin");
    Serial.print("StartVcuUpdate returned: ");
    Serial.println(totalPages);

    if (totalPages < 0) {
      Serial.println("ERROR: Failed to start VCU update");
      request->send(200, "application/json", "{\"success\":false,\"error\":\"Failed to start update - check serial console\"}");
    } else {
      Serial.print("VCU update started successfully, total pages: ");
      Serial.println(totalPages);
      String response = "{\"success\":true,\"message\":\"VCU firmware update started\",\"totalPages\":";
      response += totalPages;
      response += "}";
      request->send(200, "application/json", response);
    }
  });

  // VCU firmware update status API
  server.on("/api/vcu-firmware/status", HTTP_GET, [](AsyncWebServerRequest * request) {
    CanSDO::UpdateState updateState = canSdo.GetVcuUpdateStatus();
    const char* message = canSdo.GetVcuUpdateProgressMessage();
    int progress = canSdo.GetVcuUpdateProgressPercent();

    String response = "{\"state\":";
    response += (int)updateState;
    response += ",\"message\":\"";
    response += message;
    response += "\",\"progress\":";
    response += progress;
    response += "}";

    request->send(200, "application/json", response);
  });

  // Serve our styled firmware update page wrapper
  server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (SPIFFS.exists("/update.html")) {
      request->send(SPIFFS, "/update.html", "text/html");
    } else {
      request->send(404, "text/plain", "Update page not found");
    }
  });

  AsyncElegantOTA.begin(&server);    // Start AsyncElegantOTA at /update
  server.begin();


}

void loop() {

  displayManager.Loop();
  inputManager.Loop();
  canSdo.Loop();
  serialCommandHandler.Loop();

  // Check if JSON fetch completed and update loading screen
  CanSDO::FetchResult currentFetchResult = canSdo.GetJsonFetchStatus();

  // Show loading screen when fetch is in progress
  if (currentFetchResult == CanSDO::InProgress) {
    const char* message = canSdo.GetFetchProgressMessage();
    int progress = canSdo.GetFetchProgressPercent();
    displayManager.ShowLoadingScreen(message, progress);
  }

  // Hide loading screen when fetch completes or fails
  if (lastFetchResult == CanSDO::InProgress && currentFetchResult != CanSDO::InProgress) {
    displayManager.HideLoadingScreen();

    if (currentFetchResult == CanSDO::Success) {
      Serial.println("JSON fetch completed! Reloading parameters...");

      if (SPIFFS.exists("/params.json")) {
        if (parseParamsFile()) {
          Serial.println("Parameters reloaded successfully!");
          displayManager.LoadParameters();
          displayManager.ShowErrorMessage("Success!", "Parameters loaded\nfrom VCU", 2000);
        } else {
          Serial.println("Failed to parse parameters");
          displayManager.ShowErrorMessage("Parse Error", "Failed to parse\nparams.json", 3000);
        }
      } else {
        Serial.println("params.json not found");
        displayManager.ShowErrorMessage("File Error", "params.json not found", 3000);
      }
    } else if (currentFetchResult == CanSDO::Failed) {
      Serial.println("JSON fetch failed!");
      displayManager.ShowErrorMessage("Fetch Failed", "Could not download\nparameters from VCU", 3000);
    }
  }

  lastFetchResult = currentFetchResult;

  // Check VCU firmware update status and update loading screen
  CanSDO::UpdateState currentVcuUpdateState = canSdo.GetVcuUpdateStatus();

  // Show loading screen when VCU update is in progress
  if (currentVcuUpdateState != CanSDO::UPD_IDLE &&
      currentVcuUpdateState != CanSDO::UPDATE_COMPLETE &&
      currentVcuUpdateState != CanSDO::UPDATE_ERROR) {
    const char* message = canSdo.GetVcuUpdateProgressMessage();
    int progress = canSdo.GetVcuUpdateProgressPercent();
    displayManager.ShowLoadingScreen(message, progress);
  }

  // Hide loading screen and show result when VCU update completes or fails
  if ((lastVcuUpdateState != CanSDO::UPD_IDLE &&
       lastVcuUpdateState != CanSDO::UPDATE_COMPLETE &&
       lastVcuUpdateState != CanSDO::UPDATE_ERROR) &&
      (currentVcuUpdateState == CanSDO::UPDATE_COMPLETE ||
       currentVcuUpdateState == CanSDO::UPDATE_ERROR)) {
    displayManager.HideLoadingScreen();

    if (currentVcuUpdateState == CanSDO::UPDATE_COMPLETE) {
      Serial.println("VCU firmware update completed!");
      displayManager.ShowErrorMessage("Success!", "VCU firmware\nupdated successfully", 3000);

      // Delete the firmware file
      if (SPIFFS.exists("/vcu_firmware.bin")) {
        SPIFFS.remove("/vcu_firmware.bin");
        Serial.println("Deleted VCU firmware file");
      }
    } else if (currentVcuUpdateState == CanSDO::UPDATE_ERROR) {
      Serial.println("VCU firmware update failed!");
      displayManager.ShowErrorMessage("Update Failed", "VCU firmware update\nfailed", 3000);
    }
  }

  lastVcuUpdateState = currentVcuUpdateState;

  // Update GVRET server and forward CAN frames if running
  // Pause forwarding when CanSDO is fetching/updating to avoid consuming SDO response messages
  if (gvretRunning && currentFetchResult != CanSDO::InProgress &&
      currentVcuUpdateState == CanSDO::UPD_IDLE) {
    GVRETServer::getInstance().update();

    // Forward all CAN frames to GVRET clients
    twai_message_t message;
    while (twai_receive(&message, pdMS_TO_TICKS(0)) == ESP_OK) {
      // Push frame to SavvyCAN clients
      GVRETServer::getInstance().pushFrame(
        message.identifier,
        message.extd,
        message.data,
        message.data_length_code
      );
    }
  }

  // Pause data retrieval when fetching params.json or updating VCU firmware to avoid CAN message conflicts
  if (requestNextData) {
    requestNextData = false;

    CanSDO::FetchResult fetchStatus = canSdo.GetJsonFetchStatus();
    CanSDO::UpdateState vcuUpdateStatus = canSdo.GetVcuUpdateStatus();
    if (fetchStatus != CanSDO::InProgress && vcuUpdateStatus == CanSDO::UPD_IDLE) {
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

}
