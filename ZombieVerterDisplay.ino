#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "DataRetriever.h"

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


void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);

  displayManager.Setup();
  inputManager.Setup();
  canSdo.Setup();

  timer = timerBegin(0, 240, true); // Timer 0, clock divisor 80
  timerAttachInterrupt(timer, &timerInterrupt, true); // Attach the interrupt handling function
  timerAlarmWrite(timer, 50000, true); // Interrupt every 50ms
  timerAlarmEnable(timer); // Enable the alarm

}

void loop() {

  displayManager.Loop();
  inputManager.Loop();

  if (requestNextData) {
     requestNextData = false;
     dataRetriever.GetNextValue();
  }

}
