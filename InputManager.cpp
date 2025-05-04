#include "InputManager.h"

RotaryEncoder encoder(PIN_ENCODE_A, PIN_ENCODE_B, RotaryEncoder::LatchMode::TWO03);
OneButton button(PIN_ENCODE_BTN, true);
      
InputManager::InputManager(DisplayManager &displayManager) : displayManager(displayManager) {
}

extern void clickThunk();
extern void doubleclickThunk();


void InputManager::Setup() {
  attachInterrupt(
  digitalPinToInterrupt(PIN_ENCODE_A), []() {
    encoder.tick();
  }, CHANGE);
  attachInterrupt(
  digitalPinToInterrupt(PIN_ENCODE_B), []() {
    encoder.tick();
  }, CHANGE);

  button.attachClick([]() {
    clickThunk();
  });
  
  button.attachDoubleClick([]() {
      doubleclickThunk();
  });
}

void InputManager::Loop() {
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
