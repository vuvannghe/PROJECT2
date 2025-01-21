/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_http_client.h"
#include "esp_tls.h"

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"

#include "esp_lcd_ili9341.h"
#include "extended_ili9341.h"
#include "lvgl.h"
#include "sdcard.h"

static const char *TAG = "Main";

// DEFINE SECTION
#define WIFI_connect_max_attemp_number CONFIG_MAX_RECONNECT_ATTEMP_NUMBER
#define SW_BUILT_IN 0           // Boot button (NEGATIVE EDGE)
#define SC_SW 32                // Switch to turn into smart config mode  (NEGATIVE EDGE)
#define IMG_DOWNLOAD_SW 35      // Switch to connect to server, send request, receive response from server and save the image to SD card (NEGATIVE EDGE)
#define IMG_FORWARD_SHOW_SW 21  // Switch to display images from SD card one by one (NEGATIVE EDGE)
#define IMG_BACKWARD_SHOW_SW 34 // Switch to display images from SD card one by one (NEGATIVE EDGE)

// Semaphore
SemaphoreHandle_t xSmartConfigSemaphore = NULL;
SemaphoreHandle_t xImageDownloadSemaphore = NULL;
SemaphoreHandle_t xImageShowSemaphore = NULL;
SemaphoreHandle_t SyncSemaphore = NULL;

// Task handle
TaskHandle_t image_download_task_handle = NULL;
static const char *SD_TAG = "SD CARD";
static const char *SC_TAG = "SMART CONFIG";
static void smartconfig_itr_handler(void *parm);
static smartconfig_start_config_t smart_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
static uint8_t attemp_count = 0;
bool change_AP = false;
bool press_to_change_AP = false;
bool is_connected = false;
static uint8_t attemp_count_1 = 0;
static wifi_config_t wifi_config;
uint8_t old_ssid[32] = CONFIG_DEFAULT_AP_SSID;
uint8_t old_password[64] = CONFIG_DEFAULT_AP_PASSWORD;
uint8_t old_bssid[6] = {0x34, 0xE8, 0x94, 0xF6, 0xAA, 0xD7};

lv_disp_t *lcd_mainscreen;
char list_img[100][64];
int16_t image_index = -1;
uint16_t image_count = 0;
bool forward = false;
bool backward = false;
extern void ui_project2_init(lv_disp_t *disp);
extern void image_file_display(char *image_path);

#define MAX_HTTP_OUTPUT_BUFFER 2048
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
static const char *IMG_TAG = "HTTP_CLIENT";

#define IMG_ALREADY_EXSISTS 1
#define IMG_FAILED_TO_WRITE 2
#define IMG_FILE_FAILED_TO_OPEN_TO_WRITE 3
#define IMG_FILE_DOWNLOADED_SUCCESSFULLY 4
#define FAILED_TO_CONNECT_TO_SERVER 5
#define IMG_DOWNLOADING 6
extern void wifi_state_display(bool wifi_state);
extern void sdcard_state_display(bool sdcard_state);
extern void display_image_dowload_state(uint8_t download_state_code);
extern void lvgl_timer_handle_task(void *param);
/*-----------------------------------BUTTON INTERRUPT HANDLER----------------------------------------- */

/**
 * @brief This function is a button interrupt handler, when forward button is pressed
 *        it will trigger the image show task to show the next image.
 *
 * @param arg Not used in this function.
 */
void IRAM_ATTR img_forward_display_button_handler(void *arg)
{
    if (forward == false)
    {
        forward = true;
        BaseType_t xHigherPriorityTaskWoken;
        xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xImageShowSemaphore, xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief This function is a button interrupt handler, when backward button is pressed
 *        it will trigger the image show task to show the previous image.
 *
 * @param arg Not used in this function.
 */
void IRAM_ATTR img_backward_display_button_handler(void *arg)
{
    if (backward == false)
    {
        backward = true;
        BaseType_t xHigherPriorityTaskWoken;
        xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xImageShowSemaphore, xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

bool is_downloading = false;
/**
 * @brief Interrupt handler for the image download button.
 *
 * This function is triggered when the image download button is pressed. It checks
 * if the device is connected to Wi-Fi and not currently downloading. If both conditions are
 * met, it gives the semaphore to trigger the image download task from an ISR context.
 *
 * @param arg Unused parameter.
 */

void IRAM_ATTR img_download_button_handler(void *arg)
{
    if ((is_connected == true))
    {
        if (is_downloading == false)
        {
            BaseType_t xHigherPriorityTaskWoken;
            xHigherPriorityTaskWoken = pdFALSE;

            xSemaphoreGiveFromISR(xImageDownloadSemaphore, xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}
/*-----------------------------------IMAGE DOWNLOAD (HTTP and SD Card)----------------------------------------- */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data)
            {
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len)
                {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                }
            }
            else
            {
                const int buffer_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL)
                {
                    output_buffer = (char *)malloc(buffer_len);
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (buffer_len - output_len));
                if (copy_len)
                {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
            }
            output_len += copy_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

/**
 * @brief Take file name of the image in the header of the http packet sent by server.
 *
 * @param[in] response_header Header of http packet.
 * @param[out] filename Reeceived file name.
 * @return esp_err_t
 *
 * @retval  - ESP_OK on success.
 * @retval  - ESP_FAIL on fail.
 */
esp_err_t get_filename_from_header(char *response_header, char *filename)
{

    char *content_disposition = strstr(response_header, "Content-Disposition"); // Find Content Disposition field in header
    if (content_disposition)
    {
        char *start = strstr(content_disposition, "filename=");
        if (start)
        {
            if ((strstr(start, ".bmp") == NULL) && (strstr(start, ".BMP") == NULL))
            {
                ESP_LOGE(TAG, "The new file is not an BMP image");
                return ESP_FAIL;
            }
            start += 9; // Skip "filename="
            char *end = strchr(start, '\r');
            if (end)
            {
                *end = '\0'; // Kết thúc chuỗi
            }
            strcpy(filename, start);
        }
        ESP_LOGI(TAG, "Get filename successfully, filename = %s", filename);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Get filename unsuccessfully");
    return ESP_FAIL;
}

bool sdcard_check_exsisting_file(const char *filepath)
{
    struct stat buffer;
    struct dirent *entry;
    return (stat(filepath, &buffer) == 0);
}

/**
 * @brief Download a new image from the server and save to SD card.
 *
 *        If the image is already existed in SD card, it will stop downloading.
 *
 *        If failed to download, it will remove the incomplete image file from SD card.
 *
 * @note  This function is non-block because it is called in a task.
 */
static void download_image(void)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    esp_http_client_config_t config = {
        .host = CONFIG_HTTP_ENDPOINT,
        .path = "/api/get/dataFile",
        //  .query = "esp",
        .port = 5000,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer, // Pass address of local buffer to get response
        .disable_auto_redirect = true,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    // GET
    display_image_dowload_state(IMG_DOWNLOADING);
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_err_t err = esp_http_client_open(client, 0);
    char *response_header = (char *)malloc(369 * sizeof(char));
    if (err == ESP_OK)
    {
        if (esp_http_client_fetch_headers_revised(client, response_header))
        {
            ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRIu64,
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));

            char *image_name = (char *)malloc(64 * sizeof(char));

            if (get_filename_from_header(response_header, image_name) == ESP_OK)
            {
                char pathFile[128];
                sprintf(pathFile, "%s/%s", mount_point, image_name);

                if (sdcard_check_exsisting_file(pathFile) == true)
                {
                    ESP_LOGI("FILE_CHECK", "File '%s' exsists, stop downloading the image", pathFile);
                    display_image_dowload_state(IMG_ALREADY_EXSISTS);
                    goto end;
                }
                FILE *image_file = fopen(pathFile, "wb");
                if (image_file == NULL)
                {
                    ESP_LOGE(TAG, "Failed to open file %s for writing", pathFile);
                    display_image_dowload_state(IMG_FILE_FAILED_TO_OPEN_TO_WRITE);
                    goto end;
                }

                char buffer[1024]; // Buffer dùng để nhận dữ liệu
                int total_bytes_read = 0;
                int data_read;

                while ((data_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
                {
                    total_bytes_read += data_read;
                    if (fwrite(buffer, 1, data_read, image_file) < data_read)
                    {
                        ESP_LOGI("FILE_CHECK", "Downloading the new image encounters an error. Stop downloading");
                        display_image_dowload_state(IMG_FAILED_TO_WRITE);
                        if (remove(pathFile) != 0)
                        {
                            ESP_LOGE(__func__, "Remove %s failed", pathFile);
                        }
                        else
                        {
                            ESP_LOGW(__func__, "Remove %s successfully", pathFile);
                        }
                        goto end;
                    }
                }
                fclose(image_file);
                ESP_LOGI("BMP", "Download the new image '%s' successfully. Total bytes read: %d", image_name, total_bytes_read);
                display_image_dowload_state(IMG_FILE_DOWNLOADED_SUCCESSFULLY);
                char *ext = strchr(image_name, '.');
                *ext = '\0';
                image_count++;
                image_index += 1;
                printf("%s", image_name);
                strcpy(list_img[image_index], image_name);
            }
            else
            {
                ESP_LOGI("BMP", "Failed to get file name. Stop downloading");
            }
            free(image_name);
        }
        else
        {
            ESP_LOGE(TAG, "HTTP GET response header failed");
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        display_image_dowload_state(FAILED_TO_CONNECT_TO_SERVER);
    }

end:
    free(response_header);
    esp_http_client_cleanup(client);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
}

/**
 * @brief Task to handle downloading images from the server and saving them to the SD card.
 *
 * This task waits for the xImageDownloadSemaphore to be available, indicating a request to download an image.
 * Once the semaphore is taken, it also takes the SyncSemaphore to ensure synchronization during the download process.
 * The task sets the is_downloading flag to true, invokes the download_image function to perform the download,
 * and then sets the is_downloading flag back to false after completion. It releases the SyncSemaphore afterwards.
 *
 * @param pvParameters Pointer to the task parameters (not used in this task).
 */

static void image_download_task(void *pvParameters)
{
    for (;;)
    {
        if (xSemaphoreTake(xImageDownloadSemaphore, portMAX_DELAY) == pdPASS)
        {
            if (xSemaphoreTake(SyncSemaphore, portMAX_DELAY) == pdPASS)
            {
                is_downloading = true;
                download_image();
                is_downloading = false;
                xSemaphoreGive(SyncSemaphore);
            }
        }
    }
}

/*------------------------------------ WIFI ------------------------------------ */

/**
 * @brief Interrupt handler for the smart config button.
 *
 * This function is triggered when the smart config button is pressed.
 * It checks if the system is connected to a network, and if so, it
 * gives the xSmartConfigSemaphore from an interrupt service routine
 * to signal the smart config process, yielding from the ISR if a
 * higher priority task was woken.
 *
 * @param arg Not used in this function.
 */

void IRAM_ATTR smart_cfg_button_handler(void *arg)
{

    if ((is_connected == true))
    {
        BaseType_t xHigherPriorityTaskWoken;
        xHigherPriorityTaskWoken = pdFALSE;

        xSemaphoreGiveFromISR(xSmartConfigSemaphore, xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief Smart config task
 *
 * This task is an infinite loop that waits for the xSmartConfigSemaphore to be given.
 * When it is given, it changes the AP by disconnecting the current connection, stopping
 * the previous smart config process, and starting a new smart config process.
 *
 * @param parm Not used in this function.
 */
static void smartconfig_itr_handler(void *parm)
{
    for (;;)
    {
        if (xSemaphoreTake(xSmartConfigSemaphore, portMAX_DELAY) == pdPASS)
        {
            press_to_change_AP = true;
            ESP_LOGI(SC_TAG, "Change AP");
            esp_wifi_disconnect();
            esp_smartconfig_stop();
            if (esp_smartconfig_start(&smart_cfg) == ESP_OK)
            {
                press_to_change_AP = false;
            }
        }
    }
}

static void Wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
        {
            if (xSmartConfigSemaphore == NULL)
            {
                xSmartConfigSemaphore = xSemaphoreCreateBinary();
                xTaskCreate(smartconfig_itr_handler, "smartconfig_itr_handler", 4096, NULL, 3, NULL);
            }
            ESP_ERROR_CHECK(esp_smartconfig_start(&smart_cfg));
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_state_display(false);
            if (is_connected == true)
            {
                is_connected = false;
            }
            if (press_to_change_AP == false)
            {
                if ((change_AP == true) && ((wifi_config.sta.ssid != old_ssid) || (wifi_config.sta.password != old_password) || (wifi_config.sta.bssid != old_bssid)))
                {
                    if (attemp_count < WIFI_connect_max_attemp_number)
                    {
                        esp_wifi_connect();
                        attemp_count += 1;
                        ESP_LOGE(__func__, "Wi-Fi disconnected: Retrying connect to AP SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                    }
                    else
                    {
                        memcpy(wifi_config.sta.ssid, old_ssid, sizeof(old_ssid));
                        memcpy(wifi_config.sta.password, old_password, sizeof(old_password));
                        ESP_LOGI(TAG, "Reconfigurating the previous AP: SSID: %s PASSWORDS: %s", wifi_config.sta.ssid, wifi_config.sta.password);
                        memcpy(wifi_config.sta.bssid, old_bssid, sizeof(old_bssid));
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                        attemp_count += 1;
                    }
                    ESP_LOGI(TAG, "Connect to the new AP unsuccessfully");
                }
                else
                {
                    if (change_AP == true)
                    {
                        ESP_LOGI(TAG, "The new AP is same to the previous AP");
                        esp_wifi_disconnect();
                        change_AP = false;
                        memcpy(wifi_config.sta.ssid, old_ssid, sizeof(old_ssid));
                        memcpy(wifi_config.sta.password, old_password, sizeof(old_password));
                        memcpy(wifi_config.sta.bssid, old_bssid, sizeof(old_bssid));
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                    }
                }
                if ((change_AP == false) || (attemp_count > WIFI_connect_max_attemp_number))
                {
                    if (change_AP == true)
                    {
                        ESP_LOGI(TAG, "Trying to reconnect to the previous AP");
                        change_AP = false;
                    }

                    if (attemp_count_1 < WIFI_connect_max_attemp_number)
                    {
                        esp_wifi_connect();
                        attemp_count_1 += 1;
                        ESP_LOGE(TAG, "Wi-Fi disconnected: Retrying connect to AP SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Connect to the AP unsuccessfully. Smart Config again");
                        if (esp_smartconfig_stop() == ESP_OK)
                        {
                            esp_smartconfig_start(&smart_cfg);
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
    else
    {
        if (event_base == IP_EVENT)
        {
            if (event_id == IP_EVENT_STA_GOT_IP)
            {
                wifi_state_display(true);
                ESP_LOGI(TAG, "The Device got an IP successfully");
                attemp_count = 0;
                attemp_count_1 = 0;
                is_connected = true;
                memcpy(old_ssid, wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid));
                memcpy(old_password, wifi_config.sta.password, sizeof(wifi_config.sta.password));
                memcpy(old_bssid, wifi_config.sta.bssid, sizeof(wifi_config.sta.bssid));
                wifi_ap_record_t ap_info;
                esp_wifi_sta_get_ap_info(&ap_info);
                ESP_LOGI(TAG, "AP MAC ADDRESS: %02X:%02X:%02X:%02X:%02X:%02X", ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2], ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
                if (image_download_task_handle == NULL)
                {
                    xTaskCreate(image_download_task, "Image Download task", (10 * 1024), NULL, 15, &image_download_task_handle);
                }
            }
        }
        else
        {
            if (event_base == SC_EVENT)
            {
                switch (event_id)
                {
                case SC_EVENT_SCAN_DONE:
                {
                    ESP_LOGI(SC_TAG, "Scan done");
                    break;
                }
                case SC_EVENT_FOUND_CHANNEL:
                {
                    attemp_count = 0;
                    attemp_count_1 = 0;
                    change_AP = true;
                    ESP_LOGI(SC_TAG, "Found channel");
                    break;
                }
                case SC_EVENT_GOT_SSID_PSWD:
                {
                    ESP_LOGI(SC_TAG, "Got SSID and password");

                    smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

                    uint8_t ssid[33] = {0};
                    uint8_t password[65] = {0};
                    uint8_t bssid[6] = {0};
                    uint8_t rvd_data[33] = {0};

                    bzero(&wifi_config, sizeof(wifi_config_t));
                    memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
                    memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
                    wifi_config.sta.bssid_set = evt->bssid_set;
                    if (wifi_config.sta.bssid_set == true)
                    {
                        memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
                        memcpy(bssid, evt->bssid, sizeof(evt->bssid));
                        ESP_LOGI(SC_TAG, "AP MAC ADDRESS: %02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                    }

                    memcpy(ssid, evt->ssid, sizeof(evt->ssid));
                    memcpy(password, evt->password, sizeof(evt->password));

                    ESP_LOGI(SC_TAG, "SSID:%s", ssid);
                    ESP_LOGI(SC_TAG, "PASSWORD:%s", password);

                    if (evt->type == SC_TYPE_ESPTOUCH_V2)
                    {
                        ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
                        ESP_LOGI(SC_TAG, "RVD_DATA:");
                        for (int i = 0; i < 33; i++)
                        {
                            printf("%02x ", rvd_data[i]);
                        }
                        printf("\n");
                    }

                    ESP_ERROR_CHECK(esp_wifi_disconnect());
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                    esp_wifi_connect();
                    break;
                }
                case SC_EVENT_SEND_ACK_DONE:
                {
                    esp_smartconfig_stop();
                    change_AP = false;

                    break;
                }
                default:
                    break;
                }
            }
        }
    }
}

static void WIFI_init(void)
{
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi_event_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi_event_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &Wifi_event_handler, NULL));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
}
/*---------------------------------------------------------------------------- */
/*-------------------------------------SD CARD--------------------------------------- */

/**
 * @brief Scanning base directory mounted to File system to take all .bmp image names.
 *
 * @param[in] base_path Base directory.
 * @return esp_err_t
 *
 * @retval  - ESP_OK on success.
 * @retval  - ESP_FAIL on fail.
 */
esp_err_t scan_bmp_images(const char *base_path)
{
    DIR *dir = opendir(base_path);
    if (!dir)
    {
        ESP_LOGE(SD_TAG, "Failed to open directory: %s\n to scan", base_path);
        return ESP_FAIL;
    }
    struct dirent *entry;
    char *full_path = (char *)malloc(320 * sizeof(char));
    while ((entry = readdir(dir)) != NULL)
    {
        sprintf(full_path, "%s/%s", base_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode))
        {
            char *ext = strrchr(entry->d_name, '.');
            if ((ext != NULL) && (strcmp(ext, ".BMP") == 0))
            {
                *ext = '\0';
                image_index += 1;
                image_count++;
                strcpy(list_img[image_index], entry->d_name);
            }
        }
        memset(full_path, 0, strlen(full_path));
    }
    free(full_path);
    ESP_LOGI(SD_TAG, "Scaning file system to find .bmp images successfully. %" PRIu16 " image found", image_count);
    closedir(dir);
    return ESP_OK;
}
/*-------------------------------------LVGL--------------------------------------- */

/**
 * @brief Task to display .bmp images stored in SD card.
 *
 * This task waits for the xImageShowSemaphore to be given, and then it displays the
 * next or previous image in the image list based on the forward and backward
 * flags. If the forward flag is true, it shows the next image, and if the backward
 * flag is true, it shows the previous image. The image is displayed in the
 * lv_scr_act() screen.
 *
 * @param parm Not used in this function.
 */
static void image_show_handler(void *parm)
{
    int img_index = -1;
    char image_path[128];
    for (;;)
    {
        if (xSemaphoreTake(xImageShowSemaphore, portMAX_DELAY) == pdPASS)
        {
            if (xSemaphoreTake(SyncSemaphore, portMAX_DELAY) == pdPASS)
            {
                if (forward == true)
                {
                    img_index += 1;
                    if (img_index >= image_count)
                    {
                        img_index = 0;
                    }

                    sprintf(image_path, "A:%s.bmp", list_img[img_index]);
                    ESP_LOGI(TAG, "%s", image_path);
                    image_file_display(image_path);
                    forward = false;
                }
                else
                {
                    if (backward == true)
                    {
                        img_index -= 1;
                        if (img_index < 0)
                        {
                            img_index = image_count - 1;
                        }

                        sprintf(image_path, "A:%s.bmp", list_img[img_index]);
                        ESP_LOGI(TAG, "%s", image_path);
                        image_file_display(image_path);
                        backward = false;
                    }
                }
                xSemaphoreGive(SyncSemaphore);
            }
        }
    }
}

// nvs flash init
static void initialize_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
}

void app_main(void)
{

    initialize_nvs(); // nvs flash init
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());

    // Initialize SPI bus and mount SD card to file system
    ESP_LOGI(__func__, "Initialize SD card with SPI interface.");
    esp_vfs_fat_mount_config_t mount_config_t = MOUNT_CONFIG_DEFAULT();
    spi_bus_config_t spi_bus_config_t_1 = SPI_BUS_CONFIG_DEFAULT();
    sdmmc_host_t host_t = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_PIN_NUM_CS;
    slot_config.host_id = host_t.slot;

    sdmmc_card_t SDCARD;
    esp_err_t sd_err = sdcard_initialize(&mount_config_t, &SDCARD, &host_t, &spi_bus_config_t_1, &slot_config);

    ESP_ERROR_CHECK_WITHOUT_ABORT(sd_err);
    scan_bmp_images(mount_point);

    // Initialize LCD TFT (LVGL drivers)
    ili9341_init(&lcd_mainscreen);
    ui_project2_init(lcd_mainscreen);

    if (sd_err != ESP_OK)
    {
        sdcard_state_display(false);
    }
    else
    {
        sdcard_state_display(true);
    }

    //-----------------------------//

    // SmartConfig button
    gpio_config_t smart_cfg_button =
        {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .pin_bit_mask = (1ULL << SC_SW)};
    gpio_config(&smart_cfg_button);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SC_SW, smart_cfg_button_handler, (void *)SC_SW);
    //-----------------------------//

    // Image download button
    gpio_config_t image_download_button =
        {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .pin_bit_mask = (1ULL << IMG_DOWNLOAD_SW)};
    gpio_config(&image_download_button);

    gpio_isr_handler_add(IMG_DOWNLOAD_SW, img_download_button_handler, (void *)IMG_DOWNLOAD_SW);
    //-----------------------------//

    // Image display button
    gpio_config_t image_forward_display_button =
        {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .pin_bit_mask = (1ULL << IMG_FORWARD_SHOW_SW)};
    gpio_config(&image_forward_display_button);

    gpio_isr_handler_add(IMG_FORWARD_SHOW_SW, img_forward_display_button_handler, (void *)IMG_FORWARD_SHOW_SW);

    gpio_config_t image_backward_display_button =
        {
            .intr_type = GPIO_INTR_POSEDGE,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .pin_bit_mask = (1ULL << IMG_BACKWARD_SHOW_SW)};
    gpio_config(&image_backward_display_button);

    gpio_isr_handler_add(IMG_BACKWARD_SHOW_SW, img_backward_display_button_handler, (void *)IMG_BACKWARD_SHOW_SW);
    //-----------------------------//

    xImageDownloadSemaphore = xSemaphoreCreateBinary();
    xImageShowSemaphore = xSemaphoreCreateBinary();
    SyncSemaphore = xSemaphoreCreateMutex();

    xTaskCreate(&image_show_handler, "Image display task", 10 * 1024, NULL, 10, NULL);
    xTaskCreate(&lvgl_timer_handle_task, "LVGL timer handle task", 10 * 1024, NULL, 5, NULL);
    WIFI_init(); // Initialize Wifi in station mode with Smart Config
}
