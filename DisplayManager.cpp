#include "DisplayManager.h"
#include "DataRetriever.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "ui.h"
#include "pin_config.h"
#include "Globals.h"
#include "FS.h"
#include "SPIFFS.h"

TFT_eSPI tft = TFT_eSPI();
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 170;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[ screenWidth * screenHeight / 13 ];
extern void flushThunk( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p );

// External reference to global paramsDoc from main ino file
extern DynamicJsonDocument paramsDoc;

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
      dir = (int16_t)value;
      break;
    case BMS_T_MAX_VALUE_ID:
      batteryMaxTemp = value;
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

void DisplayManager::UpdateSpotParameterData(int id, int value) {
  // Find the spot parameter with matching ID and update its value
  for (int i = 0; i < spotParameterCount; i++) {
    if (spotParameters[i].id == id) {
      spotParameters[i].value = (float)value;
      break;
    }
  }
}

void DisplayManager::UpdateParameterData(int id, int value) {
  // Find the parameter with matching ID and update its value (only if not editing)
  if (!isEditingParam) {
    for (int i = 0; i < parameterCount; i++) {
      if (parameters[i].id == id) {
        parameters[i].value = (float)value;
        break;
      }
    }
  }
}

int DisplayManager::GetCurrentParameterId() {
  if (parameterCount > 0 && currentParameterIndex < parameterCount) {
    return parameters[currentParameterIndex].id;
  }
  return 0;
}

int DisplayManager::GetScreenIndex() {
  return screenIndex;
}

void DisplayManager::ProcessClockwiseInput() {
    if (screenIndex == PARAMETERSCREEN && !isEditingParam) {
      NextParameter();
    } else if (screenIndex == PARAMETERSCREEN && isEditingParam && parameterCount > 0) {
      // Increment parameter value while editing
      Parameter &param = parameters[currentParameterIndex];
      float newValue = tempParamValue + 1.0f;
      if (newValue <= param.maximum) {
        tempParamValue = newValue;
      }
    } else if (screenIndex == SPOTPARAMSCREEN) {
      NextSpotParameter();
    } else if (!isEditing && screenIndex < LASTSCREEN) {
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
      if (screenIndex == PARAMETERSCREEN && !isEditingParam) {
        PreviousParameter();
      } else if (screenIndex == PARAMETERSCREEN && isEditingParam && parameterCount > 0) {
        // Decrement parameter value while editing
        Parameter &param = parameters[currentParameterIndex];
        float newValue = tempParamValue - 1.0f;
        if (newValue >= param.minimum) {
          tempParamValue = newValue;
        }
      } else if (screenIndex == SPOTPARAMSCREEN) {
        PreviousSpotParameter();  
      } else if (!isEditing && screenIndex > 0) {
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
    if (screenIndex == SETTINGSMAINSCREEN) {
      // Enter parameter navigation mode
      EnterSettingsMode();
    } else if (screenIndex == SPOTPARAMSMAINSCREEN) {
      // Switch to spot parameters
      screenIndex = SPOTPARAMSCREEN;
      inSpotParams = true;
    } else if (screenIndex == PARAMETERSCREEN && parameterCount > 0 && !isEditingParam) {
      // Start editing current parameter
      isEditingParam = true;
      tempParamValue = parameters[currentParameterIndex].value;
    } else if (screenIndex == GEARSETTINGSCREEN) {
      lv_label_set_text(ui_gearEdittingLabel, LV_SYMBOL_EDIT);
      isEditing = true;
    } else if (screenIndex == MOTORSETTINGSCREEN) {
      lv_label_set_text(ui_motorEdittingLabel, LV_SYMBOL_EDIT);
      isEditing = true;
    } else if (screenIndex == REGENSETTINGSCREEN) {
      lv_label_set_text(ui_regenEditing, LV_SYMBOL_EDIT);
      isEditing = true;
    } else if (screenIndex == SETTINGSMAINSCREEN) {
    }
}

void DisplayManager::ProcessDoubleClickInput() {
    if (inSettingsMode && isEditingParam) {
      // Save parameter changes
      Parameter &param = parameters[currentParameterIndex];
      if (isValidParameterValue(tempParamValue, param.minimum, param.maximum)) {
        canSDO.SetValue(param.id, tempParamValue);
        param.value = tempParamValue;
        isEditingParam = false;
      }
    } else if (inSettingsMode) {
      // Exit settings mode
      ExitSettingsMode();
    } else if (inSpotParams) {
      inSpotParams = false;
      screenIndex = SPOTPARAMSMAINSCREEN;
    } else if (screenIndex == GEARSETTINGSCREEN) {
        canSDO.SetValue(GEAR_PARAM_ID, gearSetting);
        isEditing = false;
        lv_label_set_text(ui_gearEdittingLabel, "");
    } else if (screenIndex == MOTORSETTINGSCREEN) {
        canSDO.SetValue(MOTORS_ACTIVE_PARAM_ID, motorSetting);
        isEditing = false;
        lv_label_set_text(ui_motorEdittingLabel, "");
    } else if (screenIndex == REGENSETTINGSCREEN) {
        canSDO.SetValue(REGEN_MAX_PARAM_ID, regenSetting);
        isEditing = false;
        lv_label_set_text(ui_regenEditing, "");
    }
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
  
  // Load parameters from JSON for settings screens
  LoadParameters();

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
    sprintf(str, format, batteryMaxTemp);
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
  } else if (screenIndex == SETTINGSMAINSCREEN) {
    SettingsMainRefresh();
  } else if (screenIndex == SPOTPARAMSMAINSCREEN) {
    SpotParameterMainRefresh();
  }else if (screenIndex == PARAMETERSCREEN) {
    ParameterScreenRefresh();
  } else if (screenIndex == SPOTPARAMSCREEN) {
    SpotParameterScreenRefresh();
  }
}

void DisplayManager::LoadParameters() {
  parameterCount = 0;
  spotParameterCount = 0;
  
  // Parse parameters from JSON document
  for (JsonPair param : paramsDoc.as<JsonObject>()) {
    JsonObject paramObj = param.value().as<JsonObject>();
    
    // Load parameters where isparam is true
    if (paramObj.containsKey("isparam") && paramObj["isparam"].as<bool>() && parameterCount < MAX_PARAMETERS) {
      strncpy(parameters[parameterCount].name, param.key().c_str(), sizeof(parameters[parameterCount].name) - 1);
      parameters[parameterCount].name[sizeof(parameters[parameterCount].name) - 1] = '\0';
      
      
      if (paramObj.containsKey("unit")) {
        strncpy(parameters[parameterCount].unit, paramObj["unit"].as<String>().c_str(), sizeof(parameters[parameterCount].unit) - 1);
        parameters[parameterCount].unit[sizeof(parameters[parameterCount].unit) - 1] = '\0';
      } else {
        strcpy(parameters[parameterCount].unit, "");
      }
      
      parameters[parameterCount].value = paramObj.containsKey("value") ? paramObj["value"].as<float>() : 0.0f;
      parameters[parameterCount].minimum = paramObj.containsKey("minimum") ? paramObj["minimum"].as<float>() : 0.0f;
      parameters[parameterCount].maximum = paramObj.containsKey("maximum") ? paramObj["maximum"].as<float>() : 100.0f;
      parameters[parameterCount].defaultValue = paramObj.containsKey("default") ? paramObj["default"].as<float>() : 0.0f;
      parameters[parameterCount].id = paramObj.containsKey("id") ? paramObj["id"].as<int>() : 0;
      parameters[parameterCount].isparam = paramObj["isparam"].as<bool>();
      parameters[parameterCount].isFavorite = paramObj.containsKey("isFavorite") ? paramObj["isFavorite"].as<bool>() : false;
      
      parameterCount++;
    }
    // Load spot parameters where isparam is false
    else if (paramObj.containsKey("isparam") && !paramObj["isparam"].as<bool>() && spotParameterCount < MAX_PARAMETERS) {
      strncpy(spotParameters[spotParameterCount].name, param.key().c_str(), sizeof(spotParameters[spotParameterCount].name) - 1);
      spotParameters[spotParameterCount].name[sizeof(spotParameters[spotParameterCount].name) - 1] = '\0';
      
      
      if (paramObj.containsKey("unit")) {
        strncpy(spotParameters[spotParameterCount].unit, paramObj["unit"].as<String>().c_str(), sizeof(spotParameters[spotParameterCount].unit) - 1);
        spotParameters[spotParameterCount].unit[sizeof(spotParameters[spotParameterCount].unit) - 1] = '\0';
      } else {
        strcpy(spotParameters[spotParameterCount].unit, "");
      }
      
      spotParameters[spotParameterCount].value = paramObj.containsKey("value") ? paramObj["value"].as<float>() : 0.0f;
      spotParameters[spotParameterCount].minimum = paramObj.containsKey("minimum") ? paramObj["minimum"].as<float>() : 0.0f;
      spotParameters[spotParameterCount].maximum = paramObj.containsKey("maximum") ? paramObj["maximum"].as<float>() : 100.0f;
      spotParameters[spotParameterCount].defaultValue = paramObj.containsKey("default") ? paramObj["default"].as<float>() : 0.0f;
      spotParameters[spotParameterCount].id = paramObj.containsKey("id") ? paramObj["id"].as<int>() : 0;
      spotParameters[spotParameterCount].isparam = paramObj["isparam"].as<bool>();
      spotParameters[spotParameterCount].isFavorite = false;
      
      spotParameterCount++;
    }
  }
  
  Serial.print("Loaded ");
  Serial.print(parameterCount);
  Serial.println(" parameters for settings screen");
  Serial.print("Loaded ");
  Serial.print(spotParameterCount);
  Serial.println(" spot parameters for spot params screen");
}


void DisplayManager::EnterSettingsMode() {
  inSettingsMode = true;
  currentParameterIndex = 0;
  screenIndex = PARAMETERSCREEN;
}

void DisplayManager::ExitSettingsMode() {
  inSettingsMode = false;
  screenIndex = SETTINGSMAINSCREEN;
}

void DisplayManager::NextParameter() {
  if (inSettingsMode && parameterCount > 0) {
    currentParameterIndex = (currentParameterIndex + 1) % parameterCount;
  }
}

void DisplayManager::PreviousParameter() {
  if (inSettingsMode && parameterCount > 0) {
    currentParameterIndex = (currentParameterIndex - 1 + parameterCount) % parameterCount;
  }
}

void DisplayManager::NextSpotParameter() {
  if (spotParameterCount > 0) {
    currentSpotParameterIndex = (currentSpotParameterIndex + 1) % spotParameterCount;
    //RequestSpotParameterUpdate();
  }
}

void DisplayManager::PreviousSpotParameter() {
  if (spotParameterCount > 0) {
    currentSpotParameterIndex = (currentSpotParameterIndex - 1 + spotParameterCount) % spotParameterCount;
    //RequestSpotParameterUpdate();
  }
}

int DisplayManager::GetCurrentSpotParameterId() {
  Serial.println("GetCurrentSpotParameterId");
  if (spotParameterCount > 0 && currentSpotParameterIndex < spotParameterCount) {
    Serial.println(spotParameters[currentSpotParameterIndex].id);
    return spotParameters[currentSpotParameterIndex].id;
  }
  return -1;
}

//void DisplayManager::SetDataRetriever(DataRetriever* retriever) {
//  dataRetriever = retriever;
//}
//
//void DisplayManager::RequestSpotParameterUpdate() {
//  if (dataRetriever != nullptr) {
//    dataRetriever->GetSpotParameterValue();
//  }
//}

void DisplayManager::SettingsMainRefresh() {
  static lv_obj_t * settingsScreen = NULL;
  static lv_obj_t * titleLabel = NULL;
  static lv_obj_t * instructionLabel = NULL;
  
  // Create screen if it doesn't exist
  if (settingsScreen == NULL) {
    settingsScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settingsScreen, lv_color_black(), 0);
    
    // Title
    titleLabel = lv_label_create(settingsScreen);
    lv_label_set_text(titleLabel, "SETTINGS");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 20);
    
    // Instructions
    instructionLabel = lv_label_create(settingsScreen);
    lv_label_set_text(instructionLabel, "Rotate: Navigate\nClick: Enter\nDouble-Click: Exit");
    lv_obj_set_style_text_font(instructionLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(instructionLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(instructionLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instructionLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_y( instructionLabel, 40 );
  }
  
  lv_disp_load_scr(settingsScreen);
}


void DisplayManager::SpotParameterMainRefresh() {
  static lv_obj_t * settingsScreen = NULL;
  static lv_obj_t * titleLabel = NULL;
  static lv_obj_t * instructionLabel = NULL;
  
  // Create screen if it doesn't exist
  if (settingsScreen == NULL) {
    settingsScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settingsScreen, lv_color_black(), 0);
    
    // Title
    titleLabel = lv_label_create(settingsScreen);
    lv_label_set_text(titleLabel, "Spot Params");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 20);
    
    // Instructions
    instructionLabel = lv_label_create(settingsScreen);
    lv_label_set_text(instructionLabel, "Rotate: Navigate\nClick: Enter\nDouble-Click: Exit");
    lv_obj_set_style_text_font(instructionLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(instructionLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(instructionLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instructionLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_y( instructionLabel, 40 );
  }
  
  lv_disp_load_scr(settingsScreen);
}

void DisplayManager::ParameterScreenRefresh() {
  static lv_obj_t * paramScreen = NULL;
  static lv_obj_t * nameLabel = NULL;
  static lv_obj_t * valueLabel = NULL;
  static lv_obj_t * infoLabel = NULL;
  static lv_obj_t * navigationLabel = NULL;
  static int lastParameterIndex = -1;
  
  // Create screen if it doesn't exist
  if (paramScreen == NULL) {
    paramScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(paramScreen, lv_color_black(), 0);
    
    // Parameter name
    nameLabel = lv_label_create(paramScreen);
    lv_label_set_text(nameLabel, "Loading...");
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(nameLabel, lv_color_white(), 0);
    lv_obj_align(nameLabel, LV_ALIGN_TOP_MID, 0, 10);
    
    
    // Value
    valueLabel = lv_label_create(paramScreen);
    lv_label_set_text(valueLabel, "0.00");
    lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_make(0, 255, 0), 0);
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, 0);
    
    // Info (range, default, etc.)
    infoLabel = lv_label_create(paramScreen);
    lv_label_set_text(infoLabel, "");
//    lv_obj_set_style_text_font(infoLabel, &lv_font_montserrat_14, 0);
//    lv_obj_set_style_text_color(infoLabel, lv_color_make(150, 150, 150), 0);
//    lv_obj_align(infoLabel, LV_ALIGN_CENTER, 0, 30);
    
    // Navigation info
    navigationLabel = lv_label_create(paramScreen);
    lv_label_set_text(navigationLabel, "Rotate: Next/Prev | Click: Edit | Double-Click: Exit");
    lv_obj_set_style_text_font(navigationLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(navigationLabel, lv_color_make(100, 100, 100), 0);
    lv_obj_align(navigationLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
  }
  
  // Update content if we have parameters or if parameter index changed
  if (parameterCount > 0 && currentParameterIndex < parameterCount) {
    Parameter &param = parameters[currentParameterIndex];
    
    // Update parameter name
    lv_label_set_text(nameLabel, param.name);
    
    
    // Update value with unit - use temporary value if editing
    float displayValue = isEditingParam ? tempParamValue : param.value;
    char valueStr[64];
    
    // Handle comma-separated unit values (like "0=None, 1=BMW")
    if (strchr(param.unit, '=') != NULL) {
      String parsedText = parseUnitValue(param.unit, (int)displayValue);
      strncpy(valueStr, parsedText.c_str(), sizeof(valueStr) - 1);
      valueStr[sizeof(valueStr) - 1] = '\0';
    } else {
      // Regular numeric display
      if (strlen(param.unit) > 0) {
        snprintf(valueStr, sizeof(valueStr), "%.2f %s", displayValue, param.unit);
      } else {
        snprintf(valueStr, sizeof(valueStr), "%.2f", displayValue);
      }
    }
    
    lv_label_set_text(valueLabel, valueStr);
    
    // Change color to indicate edit mode
    if (isEditingParam) {
      lv_obj_set_style_text_color(valueLabel, lv_color_make(255, 255, 0), 0); // Yellow when editing
    } else {
      lv_obj_set_style_text_color(valueLabel, lv_color_make(0, 255, 0), 0); // Green when not editing
    }

    // Update info (range and default)
//    char infoStr[128];
//    snprintf(infoStr, sizeof(infoStr), "Range: %.1f - %.1f\nDefault: %.1f | ID: %d", 
//             param.minimum, param.maximum, param.defaultValue, param.id);
//    lv_label_set_text(infoLabel, infoStr);
    
    // Show current position and editing instructions
    char navStr[128];
    if (isEditingParam) {
      snprintf(navStr, sizeof(navStr), "EDITING | Rotate: ±1 | Double-Click: Save");
    } else {
      snprintf(navStr, sizeof(navStr), "Parameter %d/%d | Click: Edit | Double-Click: Exit", 
               currentParameterIndex + 1, parameterCount);
    }
    lv_label_set_text(navigationLabel, navStr);
    
    // Remember the last parameter index
    lastParameterIndex = currentParameterIndex;
  } else if (parameterCount == 0) {
    // No parameters loaded
    lv_label_set_text(nameLabel, "No Parameters");
    lv_label_set_text(valueLabel, "Check JSON file");
    lv_label_set_text(infoLabel, "");
    lv_label_set_text(navigationLabel, "Double-Click: Exit");
  }
  
  lv_disp_load_scr(paramScreen);
}

void DisplayManager::SpotParameterScreenRefresh() {
  static lv_obj_t * spotParamScreen = NULL;
  static lv_obj_t * titleLabel = NULL;
  static lv_obj_t * nameLabel = NULL;
  static lv_obj_t * valueLabel = NULL;
  static lv_obj_t * unitLabel = NULL;
  static lv_obj_t * idLabel = NULL;
  static lv_obj_t * navigationLabel = NULL;
  static int lastSpotParameterIndex = -1;
  
  // Create screen if it doesn't exist
  if (spotParamScreen == NULL) {
    spotParamScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(spotParamScreen, lv_color_black(), 0);
    
    // Title - smaller and moved up
    titleLabel = lv_label_create(spotParamScreen);
    lv_label_set_text(titleLabel, "SPOT PARAMS");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 2);
    
    // Parameter name - smaller font, positioned higher
    nameLabel = lv_label_create(spotParamScreen);
    lv_label_set_text(nameLabel, "Loading...");
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(nameLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nameLabel, LV_ALIGN_TOP_MID, 0, 20);
    
    // Value - much larger font, centered
    valueLabel = lv_label_create(spotParamScreen);
    lv_label_set_text(valueLabel, "0.00");
    lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_46, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_make(0, 255, 0), 0);
    lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, -5);
    
    // Unit - positioned closer to value
    unitLabel = lv_label_create(spotParamScreen);
    lv_label_set_text(unitLabel, "");
    lv_obj_set_style_text_font(unitLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(unitLabel, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_align(unitLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 25);
    
  }
  
  // Update content if we have spot parameters
  if (spotParameterCount > 0 && currentSpotParameterIndex < spotParameterCount) {
    Parameter &param = spotParameters[currentSpotParameterIndex];
    
    // Update parameter name
    lv_label_set_text(nameLabel, param.name);
    
    // Update value and unit - parse if contains = sign
    char valueStr[64];
    char displayUnit[64];
    
    float displayValue = param.value;
    
    if (strchr(param.unit, '=') != NULL) {
      // Parse the unit field to find the matching value
      char unitCopy[64];
      strncpy(unitCopy, param.unit, sizeof(unitCopy) - 1);
      unitCopy[sizeof(unitCopy) - 1] = '\0';
      
      // Look for the integer value in the unit string
      int intValue = (int)displayValue;
      char searchValue[16];
      snprintf(searchValue, sizeof(searchValue), "%d=", intValue);
      
      char* found = strstr(unitCopy, searchValue);
      if (found != NULL) {
        // Move past the "X=" part
        found += strlen(searchValue);
        
        // Find the end of this option (next comma or end of string)
        char* end = strchr(found, ',');
        if (end != NULL) {
          *end = '\0';  // Null terminate at comma
        }
        
        // Remove leading/trailing spaces
        while (*found == ' ') found++;
        char* tail = found + strlen(found) - 1;
        while (tail > found && *tail == ' ') {
          *tail = '\0';
          tail--;
        }
        
        // Display the text instead of the numeric value
        strncpy(valueStr, found, sizeof(valueStr) - 1);
        valueStr[sizeof(valueStr) - 1] = '\0';
        strcpy(displayUnit, "");  // Clear unit since text is now in value
      } else {
        // Value not found in unit string, show numeric value and raw unit
        snprintf(valueStr, sizeof(valueStr), "%.2f", displayValue);
        strncpy(displayUnit, param.unit, sizeof(displayUnit) - 1);
        displayUnit[sizeof(displayUnit) - 1] = '\0';
      }
    } else {
      // No = sign, show numeric value and unit as is
      snprintf(valueStr, sizeof(valueStr), "%.2f", displayValue);
      strncpy(displayUnit, param.unit, sizeof(displayUnit) - 1);
      displayUnit[sizeof(displayUnit) - 1] = '\0';
    }
    
    lv_label_set_text(valueLabel, valueStr);
    lv_label_set_text(unitLabel, displayUnit);
    
    
    // Remember the last parameter index
    lastSpotParameterIndex = currentSpotParameterIndex;
  } else if (spotParameterCount == 0) {
    // No spot parameters loaded
    lv_label_set_text(nameLabel, "No Spot Parameters");
    lv_label_set_text(valueLabel, "Check JSON file");
    lv_label_set_text(unitLabel, "");
    lv_label_set_text(idLabel, "");
    lv_label_set_text(navigationLabel, "Double-Click: Exit");
  }
  
  lv_disp_load_scr(spotParamScreen);
}

// Utility function to parse unit values (e.g., "0=None, 1=BMW" -> for value 1, returns "BMW")
String DisplayManager::parseUnitValue(const char* unit, int value) {
  if (unit == NULL || strlen(unit) == 0) {
    return String(value);
  }
  
  // Split the unit string by commas and search for our value
  String unitStr = String(unit);
  int startIdx = 0;
  
  while (startIdx < unitStr.length()) {
    // Find the next comma or end of string
    int commaIdx = unitStr.indexOf(',', startIdx);
    if (commaIdx == -1) commaIdx = unitStr.length();
    
    // Extract this option
    String option = unitStr.substring(startIdx, commaIdx);
    option.trim();
    
    // Check if this option starts with our value
    String prefix = String(value) + "=";
    if (option.startsWith(prefix)) {
      // Extract the text after the "X="
      String result = option.substring(prefix.length());
      result.trim();
      if (result.length() > 0) {
        return result;
      }
    }
    
    // Move to next option
    startIdx = commaIdx + 1;
    while (startIdx < unitStr.length() && unitStr.charAt(startIdx) == ' ') {
      startIdx++; // Skip spaces after comma
    }
  }
  
  return String(value);  // Return numeric value if no match found
}

// Utility function to validate parameter values against min/max bounds
bool DisplayManager::isValidParameterValue(float value, float min, float max) {
  return value >= min && value <= max;
}
