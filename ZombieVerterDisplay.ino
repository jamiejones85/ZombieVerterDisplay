#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"

CanSDO canSdo;
DisplayManager displayManager;
DataRetriever dataRetriever(canSdo, displayManager);
InputManager inputManager(displayManager);

void flushThunk( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p ) {
  displayManager.Flusher(disp, area, color_p);
}

void clickThunk() {
   displayManager.ProcessClickInput();
}

void doubleclickThunk() {
   displayManager.ProcessDoubleClickInput();
}


void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);

  displayManager.Setup();
  inputManager.Setup();
  canSdo.Setup();

}

void loop() {

  displayManager.Loop();
  inputManager.Loop();
  delay(5);

}
