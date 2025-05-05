#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <lvgl.h>

#define BATTERYINFOSCREEN        0
#define TEMPERATUREINFOSCREEN    1
#define GEARSETTINGSCREEN        2
#define MOTORSETTINGSCREEN       3
#define REGENSETTINGSCREEN       4

#define LASTSCREEN               4

class DisplayManager
{
   public:
      /** Default constructor */
      DisplayManager();
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


   private:
      int screenIndex = 0;
      int gearSetting = 0;
      int motorSetting = 0;
      int regenSetting = 0;
      int stateOfCharge = 0;
      int kwh = 0;
      int amps = 0;
      int dir = 0;
      int batteryAveTemp = 0;
      int motorTemp = 0;
      int inverterTemp = 0;

      bool isEditing = false;

      void Screen1Refresh();
      void Screen2Refresh();
      void Screen3Refresh();
      void Screen4Refresh();
      void Screen5Refresh();




};

#endif
