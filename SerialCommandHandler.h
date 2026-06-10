#ifndef SERIALCOMMANDHANDLER_H
#define SERIALCOMMANDHANDLER_H

#include <Arduino.h>
#include "CanSDO.h"

#define SERIAL_CMD_RX_PIN 40
#define SERIAL_CMD_TX_PIN 38
#define SERIAL_CMD_BAUD   115200
#define TIMEOUT_MS        5000  // 5 seconds

class SerialCommandHandler
{
   public:
      SerialCommandHandler(CanSDO &canSDO);
      void Setup();
      void Loop();
      void SetEnabled(bool enabled);
      bool IsEnabled();
      unsigned long GetLastTimeoutTime();
      int GetLastCommandId();
      unsigned long GetLastCommandIdTime();

   private:
      CanSDO &canSDO;
      HardwareSerial serialPort;
      String buffer;
      unsigned long lastCommandTime;
      bool timeoutCommandsSent;
      bool enabled;
      unsigned long lastTimeoutTime;
      int lastCommandId;
      unsigned long lastCommandIdTime;

      void processCommand(String command);
      void sendTimeoutCommands();
};

#endif // SERIALCOMMANDHANDLER_H
