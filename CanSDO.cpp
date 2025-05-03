#include "CanSDO.h"

CanSDO::CanSDO() {
  
}

void CanSDO::Setup() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);  // TWAI_MODE_NORMAL, TWAI_MODE_NO_ACK or TWAI_MODE_LISTEN_ONLY
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();
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

  if (twai_receive(&inMessage, pdMS_TO_TICKS(10)) == ESP_OK) {
    if (inMessage.data[0] == SDO_RESPONSE_DOWNLOAD)
      return Ok;
    else if (*(uint32_t*)&inMessage.data[4] == SDO_ERR_RANGE)
      return ValueOutOfRange;
    else
      return UnknownIndex;
  }
  else {
    return CommError;
  }
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

  requestSdoElement(SDO_INDEX_PARAM_UID | (id >> 8), id & 0xFF);

  if (twai_receive(&inMessage, pdMS_TO_TICKS(10)) == ESP_OK) {
    if (inMessage.data[0] == 0x80)
      return 0;
    else
      return ((double)*(uint32_t*)&inMessage.data[4]) / 32;
  }
  else {
    return 0;
  }
}
