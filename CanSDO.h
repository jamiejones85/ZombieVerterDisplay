#ifndef CANSDO_H
#define CANSDO_H

#include "driver/twai.h"


#define ZOMBIE_NODE_ID        3
#define CAN_BAUD              500000
#define RX_PIN                16
#define TX_PIN                17

#define SDO_REQ_ID_BASE       0x600U
#define SDO_REP_ID_BASE       0x580U

#define SDO_INDEX_PARAM_UID   0x2100

#define SDO_REQUEST_DOWNLOAD  (1 << 5)
#define SDO_EXPEDITED         (1 << 1)
#define SDO_SIZE_SPECIFIED    (1)
#define SDO_WRITE             (SDO_REQUEST_DOWNLOAD | SDO_EXPEDITED | SDO_SIZE_SPECIFIED)
#define SDO_ERR_RANGE         0x06090030
#define SDO_RESPONSE_DOWNLOAD (3 << 5)
#define SDO_READ              (2 << 5)
#define SDO_REQUEST_SEGMENT   (3 << 5)
#define SDO_ABORT             0x80

class CanSDO
{
   public:
      enum State { IDLE, ERROR, OBTAINVALUE };
      enum SetResult { Ok, UnknownIndex, ValueOutOfRange, CommError };
      /** Default constructor */
      CanSDO();
      void Setup();
      void Loop();
      SetResult SetValue(int id, double value);
      double GetValue(int id);


   private:
      twai_message_t outMessage;
      twai_message_t inMessage;
      State state;

      void setValueSdo(uint16_t index, uint8_t subIndex, uint32_t value);
      void requestSdoElement(uint16_t index, uint8_t subIndex);


};

#endif // CANSDO_H
