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
#define SDO_INDEX_SERIAL      0x5000
#define SDO_INDEX_JSON        0x5001

#define SDO_REQUEST_DOWNLOAD  (1 << 5)
#define SDO_EXPEDITED         (1 << 1)
#define SDO_SIZE_SPECIFIED    (1)
#define SDO_WRITE             (SDO_REQUEST_DOWNLOAD | SDO_EXPEDITED | SDO_SIZE_SPECIFIED)
#define SDO_ERR_RANGE         0x06090030
#define SDO_RESPONSE_DOWNLOAD (3 << 5)
#define SDO_READ              (2 << 5)
#define SDO_REQUEST_SEGMENT   (3 << 5)
#define SDO_ABORT             0x80
#define SDO_TOGGLE_BIT        (1 << 4)

#define SDO_INDEX_COMMANDS    0x5003
#define SDO_CMD_RESET         0

#define PAGE_SIZE_BYTES       1024
#define VCU_UPDATE_CAN_ID     0x7DD

// Include CanSDOQueue after constants are defined
#include "CanSDOQueue.h"

class CanSDO
{
   public:
      enum State { IDLE, ERROR, OBTAINVALUE, OBTAINSERIAL, OBTAINJSON, VCUUPDATE };
      enum SetResult { Ok, UnknownIndex, ValueOutOfRange, CommError };
      enum FetchResult { InProgress, Success, Failed };
      enum UpdateState { UPD_IDLE, SEND_MAGIC, SEND_SIZE, SEND_PAGE, CHECK_CRC, UPDATE_COMPLETE, UPDATE_ERROR };

      /** Default constructor */
      CanSDO();
      void Setup();
      void Loop();
      SetResult SetValue(int id, double value);
      double GetValue(int id);
      void StartJsonFetch();
      FetchResult GetJsonFetchStatus();
      void ResetError();
      const char* GetFetchProgressMessage();
      int GetFetchProgressPercent();

      // VCU Firmware Update
      int StartVcuUpdate(const char* fileName);
      UpdateState GetVcuUpdateStatus();
      const char* GetVcuUpdateProgressMessage();
      int GetVcuUpdateProgressPercent();
      void AbortVcuUpdate();

      // SDO Queue access
      CanSDOQueue& GetQueue();

   private:
      CanSDOQueue sdoQueue;  // Centralized request queue
      twai_message_t outMessage;
      twai_message_t inMessage;
      State state;
      uint32_t serial[4];
      char jsonFileName[32];
      bool toggleBit;
      int jsonFetchTimeout;
      int segmentCount;
      uint32_t expectedDataSize;
      uint32_t receivedDataSize;
      void* jsonFile;
      uint8_t currentSerialSubIndex;
      bool waitingForJsonInitResponse;
      char fetchProgressMessage[64];
      int fetchProgressPercent;

      // VCU Update variables
      UpdateState vcuUpdateState;
      void* vcuUpdateFile;
      uint32_t vcuFileSize;
      uint32_t currentPage;
      uint32_t totalPages;
      uint32_t pageCrc;
      uint32_t pagePosition;
      unsigned long updateStateTimeout;
      bool waitingForUpdateResponse;
      uint8_t bootloaderVersion;
      char vcuUpdateProgressMessage[64];
      int vcuUpdateProgressPercent;

      void setValueSdo(uint16_t index, uint8_t subIndex, uint32_t value);
      void requestSdoElement(uint16_t index, uint8_t subIndex);
      void requestSegment();

      // VCU Update helpers
      void handleVcuUpdate();
      uint32_t crc32_word(uint32_t crc, uint32_t data);
      void sendVcuUpdateMessage(const uint8_t* data, uint8_t len);

};

#endif // CANSDO_H
