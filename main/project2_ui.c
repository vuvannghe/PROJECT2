
#include "stdio.h"
#include "lvgl.h"

#define IMG_ALREADY_EXSISTS 1
#define IMG_FAILED_TO_WRITE 2
#define IMG_FILE_FAILED_TO_OPEN_TO_WRITE 3
#define IMG_FILE_DOWNLOADED_SUCCESSFULLY 4
#define FAILED_TO_CONNECT_TO_SERVER 5
#define IMG_DOWNLOADING 6
lv_timer_t *image_download_state_timer;
lv_obj_t *scr;
lv_obj_t *img_display;
lv_obj_t *wifi_icon;
lv_obj_t *sdcard_icon;
lv_obj_t *line_above;
lv_obj_t *line_bellow;
lv_obj_t *download_state_label;
lv_obj_t *img_container;
// lv_obj_t *loading_arc; // for the loading animation

void lvgl_timer_handle_task(void *param)
{
    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay 1 giây
    }
}

/**
 * @brief Change the color of the wifi icon based on the state of the wifi
 * @param wifi_state true if the wifi is connected, false otherwise
 */
void wifi_state_display(bool wifi_state)
{
    if (wifi_state == true)
    {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x00cc00), LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0xd92626), LV_PART_MAIN);
    }
    // lv_timer_handler();
}

/**
 * @brief Set the color of the sdcard icon based on the sdcard state.
 *
 *        If sdcard_state is true, set the color to green, else set the color to red.
 *        Call lv_timer_handler() at the end to draw the updated icon.
 *
 * @param sdcard_state The state of the sdcard, true for available, false for unavailable.
 */

void sdcard_state_display(bool sdcard_state)
{
    if (sdcard_state == true)
    {
        lv_obj_set_style_text_color(sdcard_icon, lv_color_hex(0x00cc00), LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_color(sdcard_icon, lv_color_hex(0xd92626), LV_PART_MAIN);
    }
    // lv_timer_handler();
}

/**
 * @brief Hides the download_state_label and stops the timer.
 *
 *        Called by the timer set in display_image_dowload_state() to hide the label after 3 seconds.
 */
void hide_image_download_state_cb()
{
    lv_timer_del(image_download_state_timer);
    lv_obj_add_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Set the text of the download_state_label and set its color based on the download_state_code.
 *
 *        If download_state_code is IMG_ALREADY_EXSISTS, IMG_FAILED_TO_WRITE or IMG_FILE_FAILED_TO_OPEN_TO_WRITE, set the text to an error message and set the color to red.
 *        If download_state_code is IMG_FILE_DOWNLOADED_SUCCESSFULLY, set the text to a success message and set the color to green.
 *        Call lv_timer_handler() at the end to draw the updated label.
 *        Set a timer to call hide_image_download_state_cb in 3 seconds to hide the label.
 *
 * @param download_state_code The code of the download state.
 */
void display_image_dowload_state(uint8_t download_state_code)
{
    switch (download_state_code)
    {
    case IMG_ALREADY_EXSISTS:
        lv_label_set_text(download_state_label, "Image already exists");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0xff0000), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        image_download_state_timer = lv_timer_create(hide_image_download_state_cb, 3000, NULL);
        break;
    case IMG_FAILED_TO_WRITE:
        lv_label_set_text(download_state_label, "Failed to write image to SD card");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0xff0000), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        image_download_state_timer = lv_timer_create(hide_image_download_state_cb, 3000, NULL);
        break;
    case IMG_FILE_FAILED_TO_OPEN_TO_WRITE:
        lv_label_set_text(download_state_label, "Failed to open image file to write");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0xff0000), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        image_download_state_timer = lv_timer_create(hide_image_download_state_cb, 3000, NULL);
        break;
    case IMG_FILE_DOWNLOADED_SUCCESSFULLY:
        lv_label_set_text(download_state_label, "Image downloaded successfully");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0x00cc00), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        image_download_state_timer = lv_timer_create(hide_image_download_state_cb, 3000, NULL);
        break;
    case FAILED_TO_CONNECT_TO_SERVER:
        lv_label_set_text(download_state_label, "Failed to connect to server");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0xff0000), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        image_download_state_timer = lv_timer_create(hide_image_download_state_cb, 3000, NULL);
        break;
    case IMG_DOWNLOADING:
        lv_label_set_text(download_state_label, "Downloading...");
        lv_obj_set_style_text_color(download_state_label, lv_color_hex(0xEFB036), LV_PART_MAIN);
        lv_obj_clear_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
}

/**
 * @brief Display an image from a file on the screen.
 *
 *        Create an img object with the given image file path and delete the
 *        previous img object.
 *
 * @param image_path The path of the image file to be displayed.
 */
void image_file_display(char *image_path)
{
    lv_obj_del(img_display);
    img_display = lv_img_create(img_container);
    lv_img_set_src(img_display, image_path);
    lv_obj_align(img_display, LV_ALIGN_CENTER, 0, 0);
    // lv_timer_handler();
}

/**
 * @brief Create the UI for the project 2.
 *
 *        This function creates the UI for the project 2. It creates a black background, a wifi icon, an sdcard icon, a line above and bellow the icons, and a label to display the image download state.
 *
 * @param disp A pointer to the lv_disp_t structure.
 */
void ui_project2_init(lv_disp_t *disp)
{

    static lv_style_t style_base;
    lv_style_init(&style_base);

    scr = lv_disp_get_scr_act(disp);
    lv_style_set_border_width(&style_base, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x032221), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(scr);
    lv_obj_add_style(scr, &style_base, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Create the wifi and sdcard icons
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

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 2);
    lv_style_set_line_color(&style_line, lv_color_hex(0x1BCCFF));
    lv_style_set_line_rounded(&style_line, true);

    static lv_point_t line_points_above[] = {{0, 25}, {240, 25}};
    line_above = lv_line_create(scr);
    lv_line_set_points(line_above, line_points_above, 2);
    lv_obj_add_style(line_above, &style_line, LV_PART_MAIN);

    static lv_point_t line_points_bellow[] = {{0, 300}, {240, 300}};
    line_bellow = lv_line_create(scr);
    lv_line_set_points(line_bellow, line_points_bellow, 2);   /*Set the points*/
    lv_obj_add_style(line_bellow, &style_line, LV_PART_MAIN); /*Apply the new style*/

    download_state_label = lv_label_create(scr);
    lv_obj_align(download_state_label, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_label_set_text(download_state_label, "");
    lv_obj_set_style_text_font(download_state_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(download_state_label, lv_color_hex(0x1BCCFF), LV_PART_MAIN);
    lv_obj_add_flag(download_state_label, LV_OBJ_FLAG_HIDDEN);

    /*     loading_arc = lv_arc_create(scr);
        lv_obj_set_size(loading_arc, 2, 2);
        lv_obj_align(loading_arc, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_arc_width(loading_arc, 2, LV_PART_INDICATOR);
        lv_arc_set_angles(loading_arc, 0, 270);
        lv_arc_set_bg_angles(loading_arc, 0, 360);
        lv_obj_set_style_arc_color(loading_arc, lv_color_hex(0xEFB036), LV_PART_MAIN); */

    img_container = lv_obj_create(scr);
    lv_obj_set_size(img_container, 240, 273);
    lv_obj_set_pos(img_container, 0, 26);
    lv_obj_clear_flag(img_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(img_container, 0, 0);
    lv_obj_set_style_bg_color(img_container, lv_color_hex(0x2e3192), 0);
    lv_obj_set_style_bg_grad_color(img_container, lv_color_hex(0x00ffe9), 0); // Màu kết thúc (xanh dương)
    lv_obj_set_style_bg_grad_dir(img_container, LV_GRAD_DIR_VER, 0);          // Hướng gradient (dọc)
    lv_obj_set_style_border_width(img_container, 1, 0);                       // Độ rộng viền 1 pixel
    lv_obj_set_style_border_opa(img_container, LV_OPA_50, 0);                 // Độ mờ viền (opaque)
    lv_obj_set_style_clip_corner(img_container, true, 0);

    img_display = lv_img_create(lv_scr_act());

    lv_timer_handler();
}
