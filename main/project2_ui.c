
#include "stdio.h"
#include "lvgl.h"

lv_obj_t *scr;
lv_obj_t *img_display;

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

    img_display = lv_img_create(lv_scr_act());
}
