#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"

#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#define AP_SSID "ZombieDisplay"
#define AP_PWD  "DisplayZombie"

#define FORMAT_SPIFFS_IF_FAILED true

CanSDO canSdo;
DisplayManager displayManager(canSdo);
DataRetriever dataRetriever(canSdo, displayManager);
InputManager inputManager(displayManager);
hw_timer_t * timer = NULL;
AsyncWebServer server(80);


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


void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);
  delay(500);
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

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200);
    }, handleUpload);

  AsyncElegantOTA.begin(&server);    // Start AsyncElegantOTA
  server.begin();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      Serial.println("SPIFFS Mount Failed");
      return;
  }


}

void loop() {

  displayManager.Loop();
  inputManager.Loop();

  if (requestNextData) {
     requestNextData = false;
     dataRetriever.GetNextValue();
  }

}
