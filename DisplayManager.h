#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <lvgl.h>
#include "CanSDO.h"
#include <ArduinoJson.h>
#include <APA102.h>
#include "pin_config.h"

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
#define PARAMSMAINSCREEN         5
#define SPOTPARAMSMAINSCREEN     6
#define SETTINGSMAINSCREEN       7

#define PARAMETERSCREEN          8
#define SPOTPARAMSCREEN          9

#define LASTSCREEN               7

// Forward declarations
class DataRetriever;
class SerialCommandHandler;

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
      void SetSerialCommandHandler(SerialCommandHandler* handler);
      void ShowLoadingScreen(const char* message, int progress);
      void HideLoadingScreen();
      void ShowErrorMessage(const char* title, const char* message, int displayTimeMs = 3000);
      void UpdateLEDFuelGauge();


   private:
      CanSDO &canSDO;
      SerialCommandHandler* serialCommandHandler;
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
      bool inSettingsMenu = false;
      bool isEditingParam = false;
      float tempParamValue = 0.0f;
      int settingsMenuOption = 0;  // 0 = Rotate, 1 = Fetch
      int currentRotation = 0;  // 0-3 for 0°, 90°, 180°, 270°
      
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

      // Loading screen objects
      lv_obj_t* loadingScreen = nullptr;
      lv_obj_t* loadingLabel = nullptr;
      lv_obj_t* loadingBar = nullptr;

      // Error message tracking
      unsigned long errorDisplayStart = 0;
      int errorDisplayDuration = 0;
      bool errorMessageShown = false;

      // Debug label for HeatReq
      lv_obj_t* debugLabel = nullptr;

      // LED strip for debugging
      static APA102<PIN_APA102_DI, PIN_APA102_CLK> ledStrip;

      void Screen1Refresh();
      void Screen2Refresh();
      void Screen3Refresh();
      void Screen4Refresh();
      void Screen5Refresh();
      void ParamsMainRefresh();
      void SpotParameterMainRefresh();
      void SettingsMainRefresh();
      void ParameterScreenRefresh();
      void SpotParameterScreenRefresh();
      
      // Utility functions for parameter editing
      String parseUnitValue(const char* unit, int value);
      bool isValidParameterValue(float value, float min, float max);




};

#endif
