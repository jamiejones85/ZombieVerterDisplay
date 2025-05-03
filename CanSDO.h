#ifndef CANSDO_H
#define CANSDO_H

#include "driver/twai.h"


#define ZOMBIE_NODE_ID        3
#define CAN_BAUD                 500000
#define RX_PIN                   16
#define TX_PIN                   17

class CanSDO
{
   public:
      /** Default constructor */
      CanSDO();
      void Setup();


   private:
      twai_message_t outMessage;
      twai_message_t inMessage;



};

#endif // CANSDO_H
