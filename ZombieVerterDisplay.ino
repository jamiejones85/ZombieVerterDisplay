#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"

#include "FS.h"
#include "SPIFFS.h"

#define FORMAT_SPIFFS_IF_FAILED true

CanSDO canSdo;
DisplayManager displayManager(canSdo);
DataRetriever dataRetriever(canSdo, displayManager);
InputManager inputManager(displayManager);
hw_timer_t * timer = NULL;

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

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
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

  //test set Soc
  canSdo.SetValue(SOC_VALUE_ID, 25);

//  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
//      Serial.println("SPIFFS Mount Failed");
//      return;
//  }

//  listDir(SPIFFS, "/", 0);
//  Serial.println("Getting Zombie Version");
//  canSdo.GetValue(2000);

}

void loop() {

  displayManager.Loop();
  inputManager.Loop();

  if (requestNextData) {
     requestNextData = false;
     dataRetriever.GetNextValue();
  }

}
