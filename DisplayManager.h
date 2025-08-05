#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <lvgl.h>
#include "CanSDO.h"
#include <ArduinoJson.h>

#define MAX_PARAMETERS 150

struct Parameter {
    char name[32];
    char unit[96];  // Increased to handle longer unit strings
    float value;
    float minimum;
    float maximum;
    float defaultValue;
    int id;
    bool isparam;
    bool isFavorite;
};

#define BATTERYINFOSCREEN        0
#define TEMPERATUREINFOSCREEN    1
#define GEARSETTINGSCREEN        2
#define MOTORSETTINGSCREEN       3
#define REGENSETTINGSCREEN       4
#define SETTINGSMAINSCREEN       5
#define SPOTPARAMSMAINSCREEN     6

#define PARAMETERSCREEN          7
#define SPOTPARAMSCREEN          8

#define LASTSCREEN               6

// Forward declaration
class DataRetriever;

class DisplayManager
{
   public:
      /** Default constructor */
      DisplayManager(CanSDO &canSDO);
      void Setup();
      void Loop();
      void Flusher(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
      void IncrementIndex();
      void DecrementIndex();
      void ProcessClockwiseInput();
      void ProcessAnticlockwiseInput();
      void ProcessClickInput();
      void ProcessDoubleClickInput();
      int GetScreenIndex();
      void UpdateData(int id, int value);
      void UpdateSpotParameterData(int id, int value);
      void UpdateParameterData(int id, int value);
      int GetCurrentParameterId();
      void LoadParameters();
      void EnterSettingsMode();
      void ExitSettingsMode();
      void NextParameter();
      void PreviousParameter();
      void NextSpotParameter();
      void PreviousSpotParameter();
      void RequestSpotParameterUpdate();
      int GetCurrentSpotParameterId();
      void SetDataRetriever(DataRetriever* retriever);


   private:
      CanSDO &canSDO;
      int screenIndex = 0;
      int gearSetting = 0;
      int motorSetting = 0;
      int16_t regenSetting = 0;
      int stateOfCharge = 0;
      int kwh = 0;
      int amps = 0;
      int dir = 0;
      int batteryMaxTemp = 0;
      int motorTemp = 0;
      int inverterTemp = 0;

      bool isEditing = false;
      bool inSettingsMode = false;
      bool inSpotParams = false;
      bool isEditingParam = false;
      float tempParamValue = 0.0f;
      
      // Parameters from JSON
      Parameter parameters[MAX_PARAMETERS];
      int parameterCount = 0;
      int currentParameterIndex = 0;
      
      // Spot Parameters (non-parameters)
      Parameter spotParameters[MAX_PARAMETERS];
      int spotParameterCount = 0;
      int currentSpotParameterIndex = 0;
      
      // Reference to data retriever for immediate updates
      DataRetriever* dataRetriever;

      void Screen1Refresh();
      void Screen2Refresh();
      void Screen3Refresh();
      void Screen4Refresh();
      void Screen5Refresh();
      void SettingsMainRefresh();
      void SpotParameterMainRefresh();
      void ParameterScreenRefresh();
      void SpotParameterScreenRefresh();
      
      // Utility functions for parameter editing
      String parseUnitValue(const char* unit, int value);
      bool isValidParameterValue(float value, float min, float max);




};

#endif
