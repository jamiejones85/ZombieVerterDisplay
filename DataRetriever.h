#ifndef DATARETRIEVER_H
#define DATARETRIEVER_H

#include "CanSDO.h"
#include "Globals.h"
#include "DisplayManager.h"

class DataRetriever
{
   public:
      DataRetriever(CanSDO &canSDO, DisplayManager &displayManager);
      void GetNextValue();


   private:
      CanSDO &canSDO;
      DisplayManager &displayManager;
      int dataSetIndex;
      int dataIndex;

      int dataSet[5][5] = {
        { KWH_VALUE_ID, DIR_VALUE_ID, IDC_VALUE_ID, BMS_T_AVG_VALUE_ID, SOC_VALUE_ID}, //screen 1 data ids
        { MOTOR_TEMP_VALUE_ID, INVERTER_TEMP_VALUE_ID }, //screen 2 data ids
        { GEAR_PARAM_ID }, //screen 3 data ids
        { MOTORS_ACTIVE_PARAM_ID }, //screen 4 data ids
        { REGEN_MAX_PARAM_ID }, //screen 5 data ids 
      };

};

#endif
