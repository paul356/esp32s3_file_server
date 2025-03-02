#include <string.h>
#include "display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"

static const char *TAG = "display";

#define LCD_HOST SPI2_HOST
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK 12
#define PIN_NUM_CS 10
#define PIN_NUM_DC 13
#define PIN_NUM_RST 9
#define PIN_NUM_BCKL 14

#define SCREEN_HEIGHT 240
#define SCREEN_WIDTH 280
#define SCREEN_OFFSET_X 20
#define LVGL_MAX_WAIT_MS 3000

#define DRAW_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color16_t) / 10)

#define FILE_SERVER_IDLE_STR "Waiting for connections"

typedef enum {
    SERVER_URI,
    FILE_SERVER_STATUS,
} update_part_e;

typedef struct
{
    update_part_e update_part;
    union {
        struct {
            const char *file_path;
            file_operation_e op;
        };
        struct {
            esp_ip4_addr_t new_addr;
        };
    };
} display_update_msg_t;

static lv_obj_t *uri_label;
static lv_obj_t *server_status_label;
static QueueHandle_t display_update_que;

static void lvgl_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(20);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, unsigned char *px_map)
{
    // Implement the flush callback to send the buffer to the display
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, true);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // because SPI LCD is big-endian, we need o swap the RGB bytes order
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    // copy a buffer's content to a specific area of the display
    offsetx1 += SCREEN_OFFSET_X;
    offsetx2 += SCREEN_OFFSET_X;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static const char *fop_to_str(file_operation_e op)
{
    switch (op)
    {
    case FILE_OPERATION_DOWNLOAD:
        return "Downloading";
    case FILE_OPERATION_UPLOAD:
        return "Uploading";
    case FILE_OPERATION_DELETE:
        return "Deleting";
    default:
        return "Unknown operation on";
    }
}

static void update_uri_label(esp_ip4_addr_t new_addr)
{
    lv_label_set_text_fmt(uri_label, "Server Address: http://" IPSTR "/", IP2STR(&new_addr));
}

static void update_status_label(file_operation_e op, const char* file_path)
{
    if (file_path != NULL && op != FILE_OPERATION_IDLE)
    {
        lv_label_set_text_fmt(server_status_label, "%s %s", fop_to_str(op), file_path);
    }
    else
    {
        lv_label_set_text(server_status_label, FILE_SERVER_IDLE_STR);
    }
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    display_update_msg_t msg;
    while (1) {
        lv_timer_handler();

        uint32_t wait_time = LVGL_MAX_WAIT_MS;
        // in case of triggering a task watch dog time out
        while (xQueueReceive(display_update_que, &msg, pdMS_TO_TICKS(wait_time)) == pdTRUE) {
            switch (msg.update_part) {
            case SERVER_URI:
                update_uri_label(msg.new_addr);
                break;
            case FILE_SERVER_STATUS:
                update_status_label(msg.op, msg.file_path);
                break;
            default:
                continue;
            }

            wait_time = 0;
        }
    }
}

static esp_err_t init_lcd_panel(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle)
{
    ESP_LOGI(TAG, "initialize display panel");

    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BCKL};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUFFER_SIZE};
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(*io_handle, &panel_config, panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(PIN_NUM_BCKL, 1);

    return ESP_OK;
}

static esp_err_t init_lvgl(esp_lcd_panel_io_handle_t io_handle, void *user_data)
{
    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();

    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(buf2);

    // Create display
    lv_disp_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, user_data);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    const esp_lcd_panel_io_callbacks_t trans_done_cb = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &trans_done_cb, disp));

    // Create a timer to handle LVGL ticks
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_task,
        .name = "lvgl_tick"};

    esp_timer_handle_t lvgl_tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 20 * 1000));

    return ESP_OK;
}

static esp_err_t create_widgets(void)
{
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act());
    lv_win_add_title(win, "Mini File Server");

    // Get the content area of the window
    lv_obj_t *win_content = lv_win_get_content(win);
    lv_obj_set_flex_flow(win_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win_content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create the first label
    uri_label = lv_label_create(win_content);
    lv_label_set_text(uri_label, "No IP is assigned yet.");

    // Create the second label
    server_status_label = lv_label_create(win_content);
    lv_label_set_long_mode(server_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(server_status_label, FILE_SERVER_IDLE_STR);
    lv_obj_set_width(server_status_label, SCREEN_WIDTH);
    lv_obj_set_style_text_align(server_status_label, LV_TEXT_ALIGN_CENTER, 0);

    return ESP_OK;
}

static esp_err_t create_lvgl_task(void)
{
    ESP_LOGI(TAG, "Create LVGL task");

    // Create the queue
    display_update_que = xQueueCreate(5, sizeof(display_update_msg_t));
    if (display_update_que == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }

    if (xTaskCreate(example_lvgl_port_task, "LVGL", 8192, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        vQueueDelete(display_update_que);
        display_update_que = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}


esp_err_t turn_off_backlight(void)
{
    gpio_config_t bk_gpio_config = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_NUM_BCKL};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(PIN_NUM_BCKL, 0);

    return ESP_OK;
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display");

    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_io_handle_t panel_io_handle;
    ESP_ERROR_CHECK(init_lcd_panel(&panel_io_handle, &panel_handle));

    ESP_ERROR_CHECK(init_lvgl(panel_io_handle, panel_handle));

    ESP_ERROR_CHECK(create_widgets());

    ESP_ERROR_CHECK(create_lvgl_task());

    ESP_LOGI(TAG, "Display all initialized successfully");
    return ESP_OK;
}

void update_service_uri(esp_ip4_addr_t new_addr)
{
    if (display_update_que == NULL) {
        return;
    }

    display_update_msg_t msg = {
        .update_part = SERVER_URI,
        .new_addr = new_addr,
    };
    if (xQueueSend(display_update_que, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue URI updating msg");
    }
}

void update_file_server_status(const char *file_path, file_operation_e op)
{
    if (display_update_que == NULL) {
        return;
    }

    display_update_msg_t msg = {
        .update_part = FILE_SERVER_STATUS,
        .file_path = file_path,
        .op = op,
    };
    if (xQueueSend(display_update_que, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue server status updating msg");
    }
}
