#include "DisplayManager.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "ui.h"
#include "pin_config.h"
#include "Globals.h"

TFT_eSPI tft = TFT_eSPI();
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 170;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[ screenWidth * screenHeight / 13 ];
extern void flushThunk( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p );

DisplayManager::DisplayManager(CanSDO &canSDO) : canSDO(canSDO) {
  
}

void DisplayManager::UpdateData(int id, int value) {
  if (isEditing) {
    //dont update if in edit mode
    return;
  }
  
  switch(id) {
    case KWH_VALUE_ID:
      kwh = value;
      break;
    case DIR_VALUE_ID:
      dir = value;
      break;
    case BMS_T_AVG_VALUE_ID:
      batteryAveTemp = value;
      break;
    case SOC_VALUE_ID:
      stateOfCharge = value;
      break;
    case MOTOR_TEMP_VALUE_ID:
      motorTemp = value;
      break;
    case INVERTER_TEMP_VALUE_ID:
      inverterTemp = value;
      break;
    case GEAR_PARAM_ID:
      gearSetting = value;
      break;
    case MOTORS_ACTIVE_PARAM_ID:
      motorSetting = value;
      break;
    case REGEN_MAX_PARAM_ID:
      regenSetting = (int16_t)value;
      break;
    case IDC_VALUE_ID:
      amps = (int16_t)value;
      break;
  }
}

int DisplayManager::GetScreenIndex() {
  return screenIndex;
}

void DisplayManager::ProcessClockwiseInput() {
    if (!isEditing && screenIndex < LASTSCREEN) {
      screenIndex++;
    } else if (isEditing && screenIndex == GEARSETTINGSCREEN && gearSetting < 2) {
      gearSetting++;
    }else if (isEditing && screenIndex == MOTORSETTINGSCREEN && motorSetting < 3) {
      motorSetting++;
    } else if (isEditing && screenIndex == REGENSETTINGSCREEN && regenSetting < 0) {
      regenSetting = regenSetting + 5;
    }
}

void DisplayManager::ProcessAnticlockwiseInput() {
      if (!isEditing && screenIndex > 0) {
        screenIndex--;
      } else if (isEditing && screenIndex == GEARSETTINGSCREEN && gearSetting > 0) {
        gearSetting--;
      } else if (isEditing && screenIndex == MOTORSETTINGSCREEN && motorSetting > 0) {
        motorSetting--;
      }else if (isEditing && screenIndex == REGENSETTINGSCREEN && regenSetting > -50) {
        regenSetting = regenSetting - 5;
      }

}

void DisplayManager::ProcessClickInput() {
    if (screenIndex == GEARSETTINGSCREEN) {
      lv_label_set_text(ui_gearEdittingLabel, LV_SYMBOL_EDIT);
      isEditing = true;
    }
    if (screenIndex == MOTORSETTINGSCREEN) {
      lv_label_set_text(ui_motorEdittingLabel, LV_SYMBOL_EDIT);
      isEditing = true;
    }
    if (screenIndex == REGENSETTINGSCREEN) {
      lv_label_set_text(ui_regenEditing, LV_SYMBOL_EDIT);
      isEditing = true;
    }
}

void DisplayManager::ProcessDoubleClickInput() {
    if (screenIndex == GEARSETTINGSCREEN) {
        canSDO.SetValue(GEAR_PARAM_ID, gearSetting);
    } else if (screenIndex == MOTORSETTINGSCREEN) {
        canSDO.SetValue(MOTORS_ACTIVE_PARAM_ID, motorSetting);
    } else if (screenIndex == REGENSETTINGSCREEN) {
        canSDO.SetValue(REGEN_MAX_PARAM_ID, regenSetting);
    }
  
    isEditing = false;
    lv_label_set_text(ui_gearEdittingLabel, "");
    lv_label_set_text(ui_motorEdittingLabel, "");
    lv_label_set_text(ui_regenEditing, "");
}

void DisplayManager::IncrementIndex() {
    if (screenIndex < LASTSCREEN) {
      screenIndex++;
    }
}

void DisplayManager::DecrementIndex() {
  if (screenIndex > 0) {
    screenIndex--;
  }
}

void DisplayManager::Flusher( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p ) {
  uint32_t w = ( area->x2 - area->x1 + 1 );
  uint32_t h = ( area->y2 - area->y1 + 1 );

  tft.startWrite();
  tft.setAddrWindow( area->x1, area->y1, w, h );
  tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
  tft.endWrite();

  lv_disp_flush_ready( disp );
}


void DisplayManager::Setup() {
  
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  tft.begin();

  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  //lvgl init
  lv_init();

  lv_disp_draw_buf_init( &draw_buf, buf1, NULL, screenWidth * screenHeight / 13 );

  /* Initialize the display */
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init( &disp_drv );
  /* Change the following line to your display resolution */
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.flush_cb = flushThunk;
  lv_disp_drv_register( &disp_drv );

  tft.fillScreen(TFT_BLACK);

  ui_init();

}

void DisplayManager::Screen1Refresh() {
    lv_disp_load_scr(ui_Screen1);

    char str[10];
    itoa( kwh, str, 10 );
    lv_label_set_text(ui_kwhValue, str);

    itoa(stateOfCharge, str, 10 );
    lv_label_set_text(ui_socValue, str);

    lv_arc_set_value(ui_socArc, stateOfCharge);

    itoa(amps * -1 , str, 10 );
    lv_label_set_text(ui_ampsValue, str);
    lv_arc_set_value(ui_ampsArc, amps * -1);

//    Serial.print("dir: ");
//    Serial.println(dir);
    if (dir == 1) {
      lv_label_set_text(ui_Label18, "D");
    } else if (dir == -1) {
      lv_label_set_text(ui_Label18, "R");
    } else {
      lv_label_set_text(ui_Label18, "N");
    }

    char format[] = "%d C";
//    Serial.print("BAt: ");
//    Serial.println(batteryAveTemp);
    sprintf(str, format, batteryAveTemp);
    lv_label_set_text(ui_batteryTempValue, str);

}

void DisplayManager::Screen2Refresh() {
    lv_disp_load_scr(ui_Screen2);

    char str[8];
    itoa(motorTemp, str, 10 );
    lv_label_set_text(ui_motorTempValue, str);
    lv_arc_set_value(ui_motorTempArc, motorTemp);

    itoa(inverterTemp, str, 10 );
    lv_label_set_text(ui_inverterTempValue, str);
    lv_arc_set_value(ui_inverterTempArc, inverterTemp);

}

void DisplayManager::Screen3Refresh() {
    lv_disp_load_scr( ui_Screen3);
    if (gearSetting == 0) {
      lv_label_set_text(ui_Label8, "LOW");
    } else if (gearSetting == 1) {
      lv_label_set_text(ui_Label8, "HIGH");
    } else if (gearSetting == 2) {
      lv_label_set_text(ui_Label8, "AUTO");
    }
}

void DisplayManager::Screen4Refresh() {
    lv_disp_load_scr(ui_Screen4);
    if (motorSetting == 0) {
      lv_label_set_text(ui_Label12, "MG1+MG2");
    } else if (motorSetting == 1) {
      lv_label_set_text(ui_Label12, "MG1");
    } else if (motorSetting == 2) {
      lv_label_set_text(ui_Label12, "MG2");
    } else if (motorSetting == 3) {
      lv_label_set_text(ui_Label12, "BLEND");
    }
}

void DisplayManager::Screen5Refresh() {
    lv_disp_load_scr(ui_Screen5);
    char str[4];
    itoa( regenSetting, str, 10 );
    lv_label_set_text(ui_Label16, str);
}

void DisplayManager::Loop() {
  lv_timer_handler();
  if (screenIndex == BATTERYINFOSCREEN) {
    Screen1Refresh();
  } else if (screenIndex == TEMPERATUREINFOSCREEN) {
    Screen2Refresh();
  } else if (screenIndex == GEARSETTINGSCREEN) {
    Screen3Refresh();
  } else if (screenIndex == MOTORSETTINGSCREEN) {
    Screen4Refresh();
  } else if (screenIndex == REGENSETTINGSCREEN) {
    Screen5Refresh();
  }
}
