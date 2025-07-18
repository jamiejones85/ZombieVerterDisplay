// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.5.1
// LVGL version: 8.3.11
// Project name: SquareLine_Project

#include "ui.h"

void ui_Screen5_screen_init(void)
{
ui_Screen5 = lv_obj_create(NULL);
lv_obj_clear_flag( ui_Screen5, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_bg_color(ui_Screen5, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(ui_Screen5, 255, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_Label13 = lv_label_create(ui_Screen5);
lv_obj_set_width( ui_Label13, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_Label13, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_Label13, 97 );
lv_obj_set_y( ui_Label13, -74 );
lv_obj_set_align( ui_Label13, LV_ALIGN_CENTER );
lv_label_set_text(ui_Label13,"Zombieverter UI");
lv_obj_set_style_text_color(ui_Label13, lv_color_hex(0x928C8C), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(ui_Label13, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(ui_Label13, &lv_font_montserrat_12, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_Panel17 = lv_obj_create(ui_Screen5);
lv_obj_set_width( ui_Panel17, 288);
lv_obj_set_height( ui_Panel17, 1);
lv_obj_set_x( ui_Panel17, 0 );
lv_obj_set_y( ui_Panel17, 78 );
lv_obj_set_align( ui_Panel17, LV_ALIGN_CENTER );
lv_obj_clear_flag( ui_Panel17, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(ui_Panel17, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(ui_Panel17, lv_color_hex(0x3C3B3B), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(ui_Panel17, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_border_width(ui_Panel17, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_Panel18 = lv_obj_create(ui_Screen5);
lv_obj_set_width( ui_Panel18, 288);
lv_obj_set_height( ui_Panel18, 1);
lv_obj_set_x( ui_Panel18, 0 );
lv_obj_set_y( ui_Panel18, -83 );
lv_obj_set_align( ui_Panel18, LV_ALIGN_CENTER );
lv_obj_clear_flag( ui_Panel18, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(ui_Panel18, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(ui_Panel18, lv_color_hex(0x3C3B3B), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(ui_Panel18, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_border_width(ui_Panel18, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_Label14 = lv_label_create(ui_Screen5);
lv_obj_set_width( ui_Label14, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_Label14, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_Label14, -79 );
lv_obj_set_y( ui_Label14, -44 );
lv_obj_set_align( ui_Label14, LV_ALIGN_CENTER );
lv_label_set_text(ui_Label14,"Regen");
lv_obj_set_style_text_color(ui_Label14, lv_color_hex(0xAEA02D), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(ui_Label14, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(ui_Label14, &lv_font_montserrat_40, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_Label16 = lv_label_create(ui_Screen5);
lv_obj_set_width( ui_Label16, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_Label16, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_Label16, -20 );
lv_obj_set_y( ui_Label16, 20 );
lv_obj_set_align( ui_Label16, LV_ALIGN_CENTER );
lv_label_set_text(ui_Label16,"-15");
lv_obj_set_style_text_font(ui_Label16, &lv_font_montserrat_44, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_regenEditing = lv_label_create(ui_Screen5);
lv_obj_set_width( ui_regenEditing, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_regenEditing, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_regenEditing, 2 );
lv_obj_set_y( ui_regenEditing, -40 );
lv_obj_set_align( ui_regenEditing, LV_ALIGN_CENTER );
lv_label_set_text(ui_regenEditing,"");
lv_obj_set_style_text_font(ui_regenEditing, &lv_font_montserrat_24, LV_PART_MAIN| LV_STATE_DEFAULT);

}
