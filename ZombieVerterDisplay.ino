#include <Arduino.h>

#include "pin_config.h"
#include "CanSDO.h"
#include "DisplayManager.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "ui.h"
#include "pin_config.h"

#include <OneButton.h>     // https://github.com/mathertel/OneButton
#include <RotaryEncoder.h> // https://github.com/mathertel/RotaryEncoder

#define LV_BUTTON                _BV(0)
#define LV_ENCODER_CW            _BV(1)
#define LV_ENCODER_CCW           _BV(2)

CanSDO canSdo;
DisplayManager displayManager;

RotaryEncoder encoder(PIN_ENCODE_A, PIN_ENCODE_B, RotaryEncoder::LatchMode::TWO03);
OneButton button(PIN_ENCODE_BTN, true);


void my_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p ) {
  displayManager.Flusher(disp, area, color_p);
}

int timer = 0;


void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);

  displayManager.Setup();
  attachInterrupt(
  digitalPinToInterrupt(PIN_ENCODE_A), []() {
    encoder.tick();
  }, CHANGE);
  attachInterrupt(
  digitalPinToInterrupt(PIN_ENCODE_B), []() {
    encoder.tick();
  }, CHANGE);

  button.attachClick([]() {
    displayManager.ProcessClickInput();
  });
  
  button.attachDoubleClick([]() {
      displayManager.ProcessDoubleClickInput();
  });
  Serial.println("Setting up Can");
  canSdo.Setup();
  Serial.println("Setup Done");

}

void handleInput() {
  RotaryEncoder::Direction dir = encoder.getDirection();
  if (dir != RotaryEncoder::Direction::NOROTATION) {
    if (dir != RotaryEncoder::Direction::CLOCKWISE) {
        displayManager.ProcessClockwiseInput();


    } else {
        displayManager.ProcessAnticlockwiseInput();
    }
  }

  button.tick();
}

twai_message_t message;
void loop() {

  displayManager.Loop();
  handleInput();
  delay(5);

  if (timer > 50) {
    
//    message.identifier = 0x0F6;
//    message.data_length_code = 4;
//    for (int i = 0; i < 4; i++) {
//      message.data[i] = 0;
//    }
//
//    // Queue message for transmission
//    if (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
//      printf("Message queued for transmission\n");
//    } else {
//      printf("Failed to queue message for transmission\n");
//    }
//
//    timer = 0;
  }

  timer++;

}
