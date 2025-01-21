
#include "stdio.h"
#include "lvgl.h"

lv_obj_t *scr;
lv_obj_t *img_display;
lv_obj_t *wifi_icon;
lv_obj_t *sdcard_icon;

void image_file_display(char *image_path)
{
    lv_obj_del(img_display);
    img_display = lv_img_create(lv_scr_act());
    lv_img_set_src(img_display, image_path);
    lv_obj_align(img_display, LV_ALIGN_CENTER, 0, 0);
    lv_timer_handler();
}

// Hàm tạo giao diện
void ui_project2_init(lv_disp_t *disp)
{

    static lv_style_t style_base;
    lv_style_init(&style_base);
    lv_style_set_border_width(&style_base, LV_PART_MAIN); // đặt độ rộng viền
    scr = lv_disp_get_scr_act(disp);

    wifi_icon = lv_label_create(scr);
    lv_obj_set_width(wifi_icon, disp->driver->hor_res);
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_LEFT, 3, 4);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0xd92626), LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_16, LV_PART_MAIN);

    sdcard_icon = lv_label_create(scr);
    lv_obj_set_width(sdcard_icon, disp->driver->hor_res);
    lv_obj_align(sdcard_icon, LV_ALIGN_TOP_LEFT, 35, 4);
    lv_label_set_text(sdcard_icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_color(sdcard_icon, lv_color_hex(0xd92626), LV_PART_MAIN);
    lv_obj_set_style_text_font(sdcard_icon, &lv_font_montserrat_16, LV_PART_MAIN);

    img_display = lv_img_create(lv_scr_act());
}
