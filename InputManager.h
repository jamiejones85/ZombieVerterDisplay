#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include "DisplayManager.h"
#include <OneButton.h>     // https://github.com/mathertel/OneButton
#include <RotaryEncoder.h> // https://github.com/mathertel/RotaryEncoder
#include "pin_config.h"


#define LV_BUTTON                _BV(0)
#define LV_ENCODER_CW            _BV(1)
#define LV_ENCODER_CCW           _BV(2)

class InputManager
{
   public:
      InputManager(DisplayManager &displayManager);
      void Setup();
      void Loop();

   private:
      DisplayManager &displayManager;

};

#endif
