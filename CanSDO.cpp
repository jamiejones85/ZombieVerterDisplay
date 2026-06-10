#include "CanSDO.h"
#include "Arduino.h"
#include "SPIFFS.h"

CanSDO::CanSDO() : state(IDLE), toggleBit(false), jsonFetchTimeout(0), segmentCount(0),
                    expectedDataSize(0), receivedDataSize(0), jsonFile(NULL),
                    currentSerialSubIndex(0), waitingForJsonInitResponse(false),
                    vcuUpdateState(UPD_IDLE), vcuUpdateFile(NULL), vcuFileSize(0),
                    currentPage(0), totalPages(0), pageCrc(0xFFFFFFFF), pagePosition(0),
                    updateStateTimeout(0), waitingForUpdateResponse(false), bootloaderVersion(0),
                    vcuUpdateProgressPercent(0) {
  serial[0] = serial[1] = serial[2] = serial[3] = 0;
  jsonFileName[0] = '\0';
  vcuUpdateProgressMessage[0] = '\0';
}

void CanSDO::Setup() {
  // Increase RX queue to 100 messages to prevent dropping SDO responses
  twai_general_config_t g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = (gpio_num_t)TX_PIN,
    .rx_io = (gpio_num_t)RX_PIN,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 20,
    .rx_queue_len = 100,  // Increased from default ~20 to handle message bursts
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_LEVEL1
  };
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_500KBITS();

  // Temporarily back to ACCEPT_ALL for debugging
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();

  Serial.println("CAN SDO initialized with RX queue size: 100, filter: ACCEPT_ALL (debug)");
}

void CanSDO::setValueSdo(uint16_t index, uint8_t subIndex, uint32_t value) {
  outMessage.extd = false;
  outMessage.identifier = 0x600 | ZOMBIE_NODE_ID;
  outMessage.data_length_code = 8;
  outMessage.data[0] = SDO_WRITE;
  outMessage.data[1] = index & 0xFF;
  outMessage.data[2] = index >> 8;
  outMessage.data[3] = subIndex;
  *(uint32_t*)&outMessage.data[4] = value;

  twai_transmit(&outMessage, pdMS_TO_TICKS(10));
}

CanSDO::SetResult CanSDO::SetValue(int id, double value) {
  if (state != IDLE) return CommError;

  setValueSdo(SDO_INDEX_PARAM_UID | (id >> 8), id & 0xFF, (uint32_t)(value * 32));

  // Wait for response, checking multiple messages like GetValue does
  int messageCount = 0;
  bool responseReceived = false;
  uint16_t expectedIndex = SDO_INDEX_PARAM_UID | (id >> 8);
  uint8_t expectedSubIndex = id & 0xFF;

  while (!responseReceived && messageCount < 10) {
    if (twai_receive(&inMessage, pdMS_TO_TICKS(10)) == ESP_OK) {
      messageCount++;

      // Check if this is an SDO response for our node
      if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
        // Check if this response is for the parameter we just set
        uint16_t responseIndex = inMessage.data[1] | (inMessage.data[2] << 8);
        uint8_t responseSubIndex = inMessage.data[3];

        if (responseIndex == expectedIndex && responseSubIndex == expectedSubIndex) {
          responseReceived = true;

          if (inMessage.data[0] == SDO_RESPONSE_DOWNLOAD) {
            Serial.println("SetValue: Success");
            return Ok;
          } else if (inMessage.data[0] == SDO_ABORT) {
            uint32_t abortCode = *(uint32_t*)&inMessage.data[4];
            Serial.print("SetValue: Abort code 0x");
            Serial.println(abortCode, HEX);

            if (abortCode == SDO_ERR_RANGE) {
              return ValueOutOfRange;
            } else {
              return UnknownIndex;
            }
          }
        } else {
          Serial.print("SetValue: Ignoring response for different param (index=0x");
          Serial.print(responseIndex, HEX);
          Serial.print(", subindex=");
          Serial.print(responseSubIndex);
          Serial.println(")");
        }
      }
    }
  }

  Serial.print("SetValue: Timeout after checking ");
  Serial.print(messageCount);
  Serial.println(" messages");
  return CommError;
}

void CanSDO::requestSdoElement(uint16_t index, uint8_t subIndex) {
  outMessage.extd = false;
  outMessage.identifier = 0x600 | ZOMBIE_NODE_ID;
  outMessage.data_length_code = 8;
  outMessage.data[0] = SDO_READ;
  outMessage.data[1] = index & 0xFF;
  outMessage.data[2] = index >> 8;
  outMessage.data[3] = subIndex;
  outMessage.data[4] = 0;
  outMessage.data[5] = 0;
  outMessage.data[6] = 0;
  outMessage.data[7] = 0;

  twai_transmit(&outMessage, pdMS_TO_TICKS(10));
}

double CanSDO::GetValue(int id) {
  if (state != IDLE) return 0;
  int messageCount = 0;
  bool responseRecieved = false;
  requestSdoElement(SDO_INDEX_PARAM_UID | (id >> 8), id & 0xFF);

  while(!responseRecieved && messageCount < 10) {
    if (twai_receive(&inMessage, pdMS_TO_TICKS(10)) == ESP_OK) {
      messageCount++;

      //if is SDO response
      if(inMessage.identifier ==  SDO_REP_ID_BASE | ZOMBIE_NODE_ID) {

        uint16_t index = SDO_INDEX_PARAM_UID | (id >> 8);
        uint8_t data1 = index & 0xFF;
        uint8_t data2 = index >> 8;
        uint8_t data3 = id & 0xFF;
              
        if (inMessage.data_length_code == 8 && inMessage.data[0] != 0x80 
          && inMessage.data[1] == data1 && inMessage.data[2] == data2 
          && inMessage.data[3] == data3) {  
              responseRecieved = true;
              return ((double)*(int32_t*)&inMessage.data[4]) / 32;
        }
      }
      
    }
  }

  return 0;
}

void CanSDO::Loop() {
  if (state == IDLE || state == ERROR || state == OBTAINVALUE) {
    return;
  }

  // Handle VCU firmware update state machine
  if (state == VCUUPDATE) {
    handleVcuUpdate();
    return;
  }

  // Handle JSON fetching state machine
  // Keep reading messages until we find an SDO response or queue is empty
  bool foundSdoResponse = false;
  int messagesChecked = 0;

  while (twai_receive(&inMessage, pdMS_TO_TICKS(0)) == ESP_OK) {
    messagesChecked++;
    jsonFetchTimeout = 0;

    // Log all received CAN IDs for debugging
    static unsigned long lastLog = 0;
    static uint32_t lastLoggedId = 0;
    if (inMessage.identifier != lastLoggedId || (millis() - lastLog > 5000)) {
      Serial.print("CAN RX ID: 0x");
      Serial.println(inMessage.identifier, HEX);
      lastLoggedId = inMessage.identifier;
      lastLog = millis();
    }

    // Check if this is the SDO response we're waiting for
    if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
      foundSdoResponse = true;
      Serial.print("*** SDO RESPONSE found after checking ");
      Serial.print(messagesChecked);
      Serial.println(" messages ***");
      Serial.print("    Data: ");
      for (int i = 0; i < 8; i++) {
        Serial.print("0x");
        if (inMessage.data[i] < 16) Serial.print("0");
        Serial.print(inMessage.data[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
      break;  // Found it, exit the loop
    }
  }

  if (!foundSdoResponse) {
    jsonFetchTimeout++;
    if (jsonFetchTimeout % 10 == 0) {
      Serial.print("No SDO response, timeout counter: ");
      Serial.print(jsonFetchTimeout);
      Serial.println(" (will abort at 100)");
    }
    if (jsonFetchTimeout > 100) {
      Serial.println("JSON fetch timeout - no response from VCU");
      if (jsonFile != NULL) {
        ((File*)jsonFile)->close();
        delete (File*)jsonFile;
        jsonFile = NULL;
      }
      state = ERROR;
    }
    return;
  }

  // Process the SDO response
  if (state == OBTAINSERIAL) {
      if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
        uint16_t responseIndex = inMessage.data[1] | (inMessage.data[2] << 8);
        uint8_t responseSubIndex = inMessage.data[3];

        Serial.print("Checking response - Index: 0x");
        Serial.print(responseIndex, HEX);
        Serial.print(", SubIndex: ");
        Serial.print(responseSubIndex);
        Serial.print(" (expecting 0x");
        Serial.print(SDO_INDEX_SERIAL, HEX);
        Serial.print(", ");
        Serial.print(currentSerialSubIndex);
        Serial.println(")");

        // Check if this is an abort for OUR request
        if (inMessage.data[0] == SDO_ABORT) {
          if (responseIndex == SDO_INDEX_SERIAL && responseSubIndex == currentSerialSubIndex) {
            Serial.println("Error: VCU aborted serial number request");
            Serial.print("Abort code: 0x");
            Serial.println(*(uint32_t*)&inMessage.data[4], HEX);
            state = ERROR;
            return;
          } else {
            Serial.println("Ignoring abort for different index");
            return;  // Ignore aborts for other requests, try again next loop
          }
        }

        if (responseIndex == SDO_INDEX_SERIAL && responseSubIndex == currentSerialSubIndex) {
            serial[responseSubIndex] = *(uint32_t*)&inMessage.data[4];

            Serial.print("Received serial segment ");
            Serial.print(responseSubIndex);
            Serial.print(": 0x");
            Serial.println(serial[responseSubIndex], HEX);

            // Update progress based on serial segment
            fetchProgressPercent = 5 + (responseSubIndex * 5);
            sprintf(fetchProgressMessage, "Reading serial %d/4...", responseSubIndex + 1);

            if (responseSubIndex < 3) {
              currentSerialSubIndex++;
              requestSdoElement(SDO_INDEX_SERIAL, currentSerialSubIndex);
            } else {
              sprintf(jsonFileName, "/params.json");
              Serial.print("Serial received, downloading to: ");
              Serial.println(jsonFileName);

              state = OBTAINJSON;
              toggleBit = false;
              waitingForJsonInitResponse = true;

              // Update progress
              fetchProgressPercent = 25;
              strcpy(fetchProgressMessage, "Starting JSON download...");

              if (SPIFFS.exists(jsonFileName)) {
                SPIFFS.remove(jsonFileName);
                Serial.println("Deleted existing params.json");
              }

              File* filePtr = new File(SPIFFS.open(jsonFileName, "w"));
              if (!(*filePtr)) {
                Serial.println("Failed to open file for JSON download");
                delete filePtr;
                state = ERROR;
                return;
              }
              jsonFile = (void*)filePtr;
              segmentCount = 0;
              Serial.println("File opened for JSON download");

              requestSdoElement(SDO_INDEX_JSON, 0);
            }
          } else {
            Serial.print("Ignoring serial response with mismatched index/subindex: 0x");
            Serial.print(responseIndex, HEX);
            Serial.print("/");
            Serial.println(responseSubIndex);
          }
        }
      }
    else if (state == OBTAINJSON) {
      if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
        if (inMessage.data[0] == SDO_ABORT) {
          Serial.println("JSON download aborted by device");
          if (jsonFile != NULL) {
            ((File*)jsonFile)->close();
            delete (File*)jsonFile;
            jsonFile = NULL;
          }
          state = ERROR;
          return;
        }

        uint8_t cmd = inMessage.data[0];

        if ((cmd & 0xE0) == 0x40) {
          if (cmd & 0x01) {
            expectedDataSize = *(uint32_t*)&inMessage.data[4];
            Serial.print("Received initial upload response with size: ");
            Serial.print(expectedDataSize);
            Serial.println(" bytes");
          } else {
            Serial.println("Received initial upload response (size not indicated)");
            expectedDataSize = 0;
          }
          receivedDataSize = 0;
          toggleBit = false;
          requestSegment();
          return;
        }

        if ((cmd & 0xE0) != 0x00) {
          Serial.print("Unexpected command byte: 0x");
          Serial.println(cmd, HEX);
          return;
        }

        bool responseToggle = (cmd & 0x10) != 0;
        if (responseToggle != toggleBit) {
          Serial.print("Toggle bit mismatch! Expected: ");
          Serial.print(toggleBit);
          Serial.print(", Got: ");
          Serial.println(responseToggle);
          return;
        }

        bool isLastSegment = (cmd & 1);
        int dataSize = 7;

        if (isLastSegment) {
          int sizeCode = (cmd >> 1) & 0x07;
          dataSize = 7 - sizeCode;
          Serial.print("Last segment, size code: ");
          Serial.print(sizeCode);
          Serial.print(", data size: ");
          Serial.println(dataSize);
        }

        if (jsonFile != NULL) {
          File* filePtr = (File*)jsonFile;
          size_t written = filePtr->write(&inMessage.data[1], dataSize);
          segmentCount++;
          receivedDataSize += written;

          // Update progress
          if (expectedDataSize > 0) {
            int jsonProgress = (receivedDataSize * 75) / expectedDataSize;  // 75% of total progress
            fetchProgressPercent = 25 + jsonProgress;  // 25% for serial + up to 75% for JSON
            sprintf(fetchProgressMessage, "Downloading: %d%%", jsonProgress);
          } else {
            sprintf(fetchProgressMessage, "Downloaded %d bytes", receivedDataSize);
          }

          if (segmentCount % 100 == 0 || isLastSegment) {
            Serial.print("Segment ");
            Serial.print(segmentCount);
            Serial.print(": Received ");
            Serial.print(receivedDataSize);
            if (expectedDataSize > 0) {
              Serial.print("/");
              Serial.print(expectedDataSize);
            }
            Serial.println(" bytes");
          }
        } else {
          Serial.println("ERROR: File handle is NULL!");
          state = ERROR;
          return;
        }

        if (expectedDataSize > 0 && receivedDataSize >= expectedDataSize) {
          Serial.println("Received all expected data!");
          if (jsonFile != NULL) {
            ((File*)jsonFile)->close();
            delete (File*)jsonFile;
            jsonFile = NULL;
          }
          Serial.print("Downloaded ");
          Serial.print(receivedDataSize);
          Serial.println(" bytes");
          state = IDLE;
          return;
        }

        if (segmentCount >= 5000 && !isLastSegment) {
          Serial.print("Reached segment limit (");
          Serial.print(segmentCount);
          Serial.println(" segments). Force closing file.");
          if (jsonFile != NULL) {
            ((File*)jsonFile)->close();
            delete (File*)jsonFile;
            jsonFile = NULL;
          }
          Serial.print("Downloaded ");
          Serial.print(receivedDataSize);
          Serial.println(" bytes");
          state = IDLE;
          return;
        }

        if (isLastSegment) {
          if (jsonFile != NULL) {
            ((File*)jsonFile)->close();
            delete (File*)jsonFile;
            jsonFile = NULL;
          }
          Serial.println("JSON download complete!");
          state = IDLE;
        } else {
          toggleBit = !toggleBit;
          requestSegment();
        }
      }
    }
}

void CanSDO::requestSegment() {
  outMessage.extd = false;
  outMessage.identifier = 0x600 | ZOMBIE_NODE_ID;
  outMessage.data_length_code = 8;
  outMessage.data[0] = SDO_REQUEST_SEGMENT | (toggleBit ? 0x10 : 0x00);
  outMessage.data[1] = 0;
  outMessage.data[2] = 0;
  outMessage.data[3] = 0;
  outMessage.data[4] = 0;
  outMessage.data[5] = 0;
  outMessage.data[6] = 0;
  outMessage.data[7] = 0;

  twai_transmit(&outMessage, pdMS_TO_TICKS(10));
}

void CanSDO::StartJsonFetch() {
  Serial.print("StartJsonFetch called, current state: ");
  Serial.println(state);

  if (state != IDLE && state != ERROR) {
    Serial.println("Cannot start JSON fetch - not in IDLE state");
    return;
  }

  if (state == ERROR) {
    Serial.println("Resetting error state...");
    ResetError();
  }

  Serial.println("=== Starting JSON fetch from VCU ===");
  Serial.print("Using Node ID: ");
  Serial.println(ZOMBIE_NODE_ID);
  Serial.print("Sending SDO request to: 0x");
  Serial.println(0x600 | ZOMBIE_NODE_ID, HEX);
  Serial.print("Expecting SDO response from: 0x");
  Serial.println(0x580 | ZOMBIE_NODE_ID, HEX);

  state = OBTAINSERIAL;
  jsonFetchTimeout = 0;
  toggleBit = false;
  currentSerialSubIndex = 0;
  fetchProgressPercent = 0;
  strcpy(fetchProgressMessage, "Initializing...");

  // Flush the CAN queue before requesting to clear old messages
  Serial.println("Flushing old CAN messages...");
  twai_message_t dummy;
  int flushed = 0;
  while (twai_receive(&dummy, pdMS_TO_TICKS(0)) == ESP_OK) {
    flushed++;
  }
  Serial.print("Flushed ");
  Serial.print(flushed);
  Serial.println(" messages");

  Serial.println("Step 1: Requesting serial number...");
  requestSdoElement(SDO_INDEX_SERIAL, 0);

  // Use blocking receive like GetValue() does
  int messageCount = 0;
  bool foundResponse = false;

  while (!foundResponse && messageCount < 50) {
    if (twai_receive(&inMessage, pdMS_TO_TICKS(10)) == ESP_OK) {
      messageCount++;

      if (inMessage.identifier == (SDO_REP_ID_BASE | ZOMBIE_NODE_ID)) {
        uint16_t responseIndex = inMessage.data[1] | (inMessage.data[2] << 8);
        uint8_t responseSubIndex = inMessage.data[3];

        Serial.print("Message ");
        Serial.print(messageCount);
        Serial.print(" - Index: 0x");
        Serial.print(responseIndex, HEX);
        Serial.print(", SubIndex: ");
        Serial.println(responseSubIndex);

        if (responseIndex == SDO_INDEX_SERIAL && responseSubIndex == 0) {
          if (inMessage.data[0] == SDO_ABORT) {
            Serial.println("ERROR: VCU aborted serial number request!");
            Serial.print("Abort code: 0x");
            Serial.println(*(uint32_t*)&inMessage.data[4], HEX);
            state = ERROR;
            return;
          }

          Serial.println("*** Found serial number response! ***");

          // Process the first serial segment directly
          serial[0] = *(uint32_t*)&inMessage.data[4];
          Serial.print("Received serial segment 0: 0x");
          Serial.println(serial[0], HEX);

          // Update progress
          fetchProgressPercent = 5;
          strcpy(fetchProgressMessage, "Reading serial 1/4...");

          // Request the next segment
          currentSerialSubIndex = 1;
          requestSdoElement(SDO_INDEX_SERIAL, currentSerialSubIndex);
          Serial.println("Requested serial segment 1");

          foundResponse = true;
          break;
        }
      }
    }
  }

  if (!foundResponse) {
    Serial.println("ERROR: No response from VCU for serial number request");
    Serial.print("Checked ");
    Serial.print(messageCount);
    Serial.println(" messages");
    state = ERROR;
    return;
  }

  // Now Loop() will continue fetching the remaining serial segments
  Serial.println("First segment received, Loop() will handle the rest");
}

CanSDO::FetchResult CanSDO::GetJsonFetchStatus() {
  if (state == OBTAINSERIAL || state == OBTAINJSON) {
    return InProgress;
  } else if (state == ERROR) {
    return Failed;
  } else {
    return Success;
  }
}

void CanSDO::ResetError() {
  if (jsonFile != NULL) {
    ((File*)jsonFile)->close();
    delete (File*)jsonFile;
    jsonFile = NULL;
  }
  state = IDLE;
  fetchProgressPercent = 0;
  strcpy(fetchProgressMessage, "");
  Serial.println("Error state reset");
}

const char* CanSDO::GetFetchProgressMessage() {
  return fetchProgressMessage;
}

int CanSDO::GetFetchProgressPercent() {
  return fetchProgressPercent;
}

// ========== VCU FIRMWARE UPDATE IMPLEMENTATION ==========

uint32_t CanSDO::crc32_word(uint32_t crc, uint32_t data) {
  crc = crc ^ data;
  for (int i = 0; i < 32; i++) {
    if (crc & 0x80000000)
      crc = (crc << 1) ^ 0x04C11DB7;  // STM32 polynomial
    else
      crc = (crc << 1);
  }
  return crc;
}

void CanSDO::sendVcuUpdateMessage(const uint8_t* data, uint8_t len) {
  twai_message_t msg;
  msg.extd = false;
  msg.identifier = VCU_UPDATE_CAN_ID;
  msg.data_length_code = len;
  memcpy(msg.data, data, len);
  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

int CanSDO::StartVcuUpdate(const char* fileName) {
  Serial.println("=== StartVcuUpdate called ===");
  Serial.print("Current state: ");
  Serial.println(state);
  Serial.print("Current vcuUpdateState: ");
  Serial.println(vcuUpdateState);

  if (state != IDLE && state != ERROR) {
    Serial.print("Cannot start VCU update - not in IDLE state. State = ");
    Serial.println(state);
    return -1;
  }

  Serial.println("=== Starting VCU firmware update ===");
  Serial.print("Opening firmware file: ");
  Serial.println(fileName);

  // Check if file exists first
  if (!SPIFFS.exists(fileName)) {
    Serial.print("ERROR: File does not exist: ");
    Serial.println(fileName);
    return -1;
  }

  File* filePtr = new File(SPIFFS.open(fileName, "r"));
  if (!(*filePtr)) {
    Serial.println("ERROR: Failed to open firmware file");
    delete filePtr;
    return -1;
  }

  if (!filePtr->available()) {
    Serial.println("ERROR: File is empty or not readable");
    filePtr->close();
    delete filePtr;
    return -1;
  }

  vcuUpdateFile = filePtr;
  vcuFileSize = filePtr->size();
  totalPages = (vcuFileSize + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
  currentPage = 0;
  pageCrc = 0xFFFFFFFF;
  pagePosition = 0;
  bootloaderVersion = 0;
  vcuUpdateProgressPercent = 0;
  strcpy(vcuUpdateProgressMessage, "Resetting VCU...");

  Serial.print("File size: ");
  Serial.print(vcuFileSize);
  Serial.print(" bytes, ");
  Serial.print(totalPages);
  Serial.println(" pages");

  // Clear CAN queue before starting
  twai_message_t dummy;
  int flushed = 0;
  while (twai_receive(&dummy, pdMS_TO_TICKS(0)) == ESP_OK) {
    flushed++;
  }
  Serial.print("Flushed ");
  Serial.print(flushed);
  Serial.println(" old CAN messages");

  // Send reset command
  Serial.println("Sending reset command to VCU...");
  setValueSdo(SDO_INDEX_COMMANDS, SDO_CMD_RESET, 1);

  // Give VCU time to reset and enter bootloader
  Serial.println("Waiting for VCU to reset and enter bootloader...");
  delay(1000);  // Increased to 1 second

  // Flush CAN queue again after delay to clear any startup messages
  Serial.println("Flushing CAN queue after reset...");
  int flushed2 = 0;
  while (twai_receive(&dummy, pdMS_TO_TICKS(0)) == ESP_OK) {
    flushed2++;
  }
  Serial.print("Flushed ");
  Serial.print(flushed2);
  Serial.println(" messages after reset");

  vcuUpdateState = SEND_MAGIC;
  state = VCUUPDATE;
  waitingForUpdateResponse = true;
  updateStateTimeout = millis();

  Serial.println("VCU update started - waiting for bootloader magic on CAN ID 0x7DD...");
  Serial.println("If update times out, check:");
  Serial.println("  1. VCU is powered and connected");
  Serial.println("  2. CAN bus termination is correct");
  Serial.println("  3. VCU bootloader is working");

  return totalPages;
}

void CanSDO::handleVcuUpdate() {
  twai_message_t msg;
  static unsigned long lastDebugPrint = 0;

  // Check for timeout (10 seconds for initial magic, 5 seconds for others)
  unsigned long timeout = (vcuUpdateState == SEND_MAGIC) ? 10000 : 5000;
  if (millis() - updateStateTimeout > timeout) {
    Serial.print("VCU update timeout in state: ");
    Serial.println(vcuUpdateState);
    vcuUpdateState = UPDATE_ERROR;
    strcpy(vcuUpdateProgressMessage, "Update timeout");
  }

  // Print debug message every second while waiting
  if (waitingForUpdateResponse && millis() - lastDebugPrint > 1000) {
    Serial.print("Waiting for VCU response, state: ");
    Serial.print(vcuUpdateState);
    Serial.print(", elapsed: ");
    Serial.print((millis() - updateStateTimeout) / 1000);
    Serial.println("s");
    lastDebugPrint = millis();
  }

  // Check for incoming messages
  bool gotMessage = false;
  int messagesChecked = 0;
  while (twai_receive(&msg, pdMS_TO_TICKS(0)) == ESP_OK) {
    messagesChecked++;

    // Debug: print all CAN messages when waiting for bootloader
    if (vcuUpdateState == SEND_MAGIC && messagesChecked <= 10) {
      Serial.print("CAN msg #");
      Serial.print(messagesChecked);
      Serial.print(": ID=0x");
      Serial.print(msg.identifier, HEX);
      Serial.print(" DLC=");
      Serial.print(msg.data_length_code);
      Serial.print(" data=");
      for (int i = 0; i < msg.data_length_code; i++) {
        Serial.print("0x");
        if (msg.data[i] < 16) Serial.print("0");
        Serial.print(msg.data[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }

    if (msg.identifier == VCU_UPDATE_CAN_ID) {
      Serial.print("*** Got VCU update message on ID 0x");
      Serial.print(VCU_UPDATE_CAN_ID, HEX);
      Serial.print("! Data[0]=0x");
      Serial.print(msg.data[0], HEX);
      Serial.print(" State=");
      Serial.println(vcuUpdateState);
      gotMessage = true;
      waitingForUpdateResponse = false;
      updateStateTimeout = millis();  // Reset timeout
      break;
    }
  }

  // Log if we checked messages but didn't find what we're looking for
  if (messagesChecked > 0 && !gotMessage && vcuUpdateState == SEND_MAGIC) {
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 2000) {
      Serial.print("Checked ");
      Serial.print(messagesChecked);
      Serial.print(" CAN messages, waiting for ID 0x");
      Serial.println(VCU_UPDATE_CAN_ID, HEX);
      lastLogTime = millis();
    }
  }

  if (!gotMessage && waitingForUpdateResponse) {
    return;  // Still waiting for response
  }

  switch (vcuUpdateState) {
    case SEND_MAGIC: {
      if (gotMessage && msg.data[0] == 0x33) {
        // Got magic byte, echo back the device ID
        bootloaderVersion = msg.data[3];
        Serial.print("Bootloader version: ");
        Serial.println(bootloaderVersion);

        uint8_t response[8] = {msg.data[4], msg.data[5], msg.data[6], msg.data[7], 0, 0, 0, 0};
        sendVcuUpdateMessage(response, 8);

        // Legacy bootloader needs delay
        if (bootloaderVersion == 1) {
          delay(100);
        }

        vcuUpdateState = SEND_SIZE;
        waitingForUpdateResponse = true;
        vcuUpdateProgressPercent = 5;
        strcpy(vcuUpdateProgressMessage, "Sending file size...");
        Serial.println("Magic received, sending size...");
      }
      break;
    }

    case SEND_SIZE: {
      if (gotMessage && msg.data[0] == 'S') {
        // Send number of pages
        uint8_t response[8];
        response[0] = 'S';
        *(uint32_t*)&response[4] = totalPages;
        sendVcuUpdateMessage(response, 8);

        vcuUpdateState = SEND_PAGE;
        pageCrc = 0xFFFFFFFF;
        pagePosition = 0;
        waitingForUpdateResponse = true;
        vcuUpdateProgressPercent = 10;
        sprintf(vcuUpdateProgressMessage, "Sending page 1/%lu...", totalPages);
        Serial.print("Size sent, total pages: ");
        Serial.println(totalPages);
      }
      break;
    }

    case SEND_PAGE: {
      if (gotMessage) {
        if (msg.data[0] == 'P') {
          // Send next 8-byte chunk of current page
          uint8_t response[8];
          response[0] = 'P';

          File* file = (File*)vcuUpdateFile;
          size_t bytesToRead = min((size_t)7, (size_t)(PAGE_SIZE_BYTES - pagePosition));
          size_t bytesRead = file->read(&response[1], bytesToRead);

          // Pad with 0xFF if needed
          for (size_t i = bytesRead; i < 7; i++) {
            response[1 + i] = 0xFF;
          }

          // Update CRC with 32-bit words (little endian)
          if (pagePosition % 4 == 0 && pagePosition < PAGE_SIZE_BYTES) {
            uint32_t word = response[1] | (response[2] << 8) | (response[3] << 16) | (response[4] << 24);
            pageCrc = crc32_word(pageCrc, word);
            if (pagePosition + 4 < PAGE_SIZE_BYTES) {
              word = response[5] | (response[6] << 8) | (response[7] << 16);
              // Need to read one more byte for the complete word
              uint8_t nextByte = 0xFF;
              if (file->available()) {
                file->read(&nextByte, 1);
                file->seek(file->position() - 1);  // Go back one byte
              }
              word |= (nextByte << 24);
              pageCrc = crc32_word(pageCrc, word);
            }
          }

          sendVcuUpdateMessage(response, 8);
          pagePosition += 7;

          // Update progress
          vcuUpdateProgressPercent = 10 + ((currentPage * 85) / totalPages);
          sprintf(vcuUpdateProgressMessage, "Sending page %lu/%lu...", currentPage + 1, totalPages);

          waitingForUpdateResponse = true;
        }
        else if (msg.data[0] == 'C') {
          // Bootloader wants CRC
          vcuUpdateState = CHECK_CRC;
          sprintf(vcuUpdateProgressMessage, "Verifying page %lu/%lu...", currentPage + 1, totalPages);
          Serial.print("Page ");
          Serial.print(currentPage);
          Serial.print(" CRC request, sending: 0x");
          Serial.println(pageCrc, HEX);

          uint8_t response[8];
          response[0] = 'C';
          *(uint32_t*)&response[4] = pageCrc;
          sendVcuUpdateMessage(response, 8);
          waitingForUpdateResponse = true;
        }
      }
      break;
    }

    case CHECK_CRC: {
      if (gotMessage) {
        if (msg.data[0] == 'P') {
          // CRC OK, move to next page
          currentPage++;
          Serial.print("Page ");
          Serial.print(currentPage - 1);
          Serial.println(" verified OK");

          if (currentPage >= totalPages) {
            vcuUpdateState = UPDATE_COMPLETE;
            vcuUpdateProgressPercent = 100;
            strcpy(vcuUpdateProgressMessage, "Update complete!");
            Serial.println("=== VCU update complete! ===");

            File* file = (File*)vcuUpdateFile;
            file->close();
            delete file;
            vcuUpdateFile = NULL;
            state = IDLE;
          } else {
            vcuUpdateState = SEND_PAGE;
            pageCrc = 0xFFFFFFFF;
            pagePosition = 0;
            vcuUpdateProgressPercent = 10 + ((currentPage * 85) / totalPages);
            sprintf(vcuUpdateProgressMessage, "Sending page %lu/%lu...", currentPage + 1, totalPages);
          }
          waitingForUpdateResponse = true;
        }
        else if (msg.data[0] == 'E') {
          // CRC error, retry page
          Serial.print("Page ");
          Serial.print(currentPage);
          Serial.println(" CRC error, retrying...");

          File* file = (File*)vcuUpdateFile;
          file->seek(currentPage * PAGE_SIZE_BYTES);
          vcuUpdateState = SEND_PAGE;
          pageCrc = 0xFFFFFFFF;
          pagePosition = 0;
          sprintf(vcuUpdateProgressMessage, "Retrying page %lu/%lu...", currentPage + 1, totalPages);
          waitingForUpdateResponse = true;
        }
        else if (msg.data[0] == 'D') {
          // Update done
          vcuUpdateState = UPDATE_COMPLETE;
          vcuUpdateProgressPercent = 100;
          strcpy(vcuUpdateProgressMessage, "Update complete!");
          Serial.println("=== VCU update complete! ===");

          File* file = (File*)vcuUpdateFile;
          file->close();
          delete file;
          vcuUpdateFile = NULL;
          state = IDLE;
        }
      }
      break;
    }

    case UPDATE_COMPLETE:
    case UPDATE_ERROR:
      // Do nothing, waiting for user to acknowledge
      break;

    default:
      break;
  }
}

CanSDO::UpdateState CanSDO::GetVcuUpdateStatus() {
  return vcuUpdateState;
}

const char* CanSDO::GetVcuUpdateProgressMessage() {
  return vcuUpdateProgressMessage;
}

int CanSDO::GetVcuUpdateProgressPercent() {
  return vcuUpdateProgressPercent;
}

void CanSDO::AbortVcuUpdate() {
  Serial.println("Aborting VCU firmware update");
  vcuUpdateState = UPDATE_ERROR;
  strcpy(vcuUpdateProgressMessage, "Update aborted");

  if (vcuUpdateFile != NULL) {
    File* file = (File*)vcuUpdateFile;
    file->close();
    delete file;
    vcuUpdateFile = NULL;
  }

  state = IDLE;
}
