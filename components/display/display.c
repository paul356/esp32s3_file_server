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

#define LCD_HOST    SPI2_HOST
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10
#define PIN_NUM_DC   13
#define PIN_NUM_RST  9
#define PIN_NUM_BCKL 14

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 280

#define DRAW_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color16_t) / 10)

static void lvgl_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(10);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, unsigned char* px_map)
{
    // Implement the flush callback to send the buffer to the display
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // because SPI LCD is big-endian, we need to swap the RGB bytes order
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    // copy a buffer's content to a specific area of the display
    offsety1 += 20;
    offsety2 += 20;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    uint32_t time_threshold_ms = 1000 / CONFIG_FREERTOS_HZ;
    while (1) {
        time_till_next_ms = lv_timer_handler();
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, time_threshold_ms);
        vTaskDelay(time_till_next_ms / portTICK_PERIOD_MS);
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display");

    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BCKL
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUFFER_SIZE
    };
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

    esp_lcd_panel_io_handle_t io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    esp_lcd_panel_handle_t panel_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(PIN_NUM_BCKL, 1);

    // Initialize LVGL
    lv_init();

    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(buf2);

    // Create display
    lv_disp_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t trans_done_cb = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &trans_done_cb, disp));


    // Create a timer to handle LVGL ticks
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_task,
        .name = "lvgl_tick"
    };

    esp_timer_handle_t lvgl_tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 10 * 1000));

    const char* message = "Hello Dude";
    // Create a simple label
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, message);
    lv_obj_set_pos(label, 0, 0);

    const char* message2 = "Hello Would";
    lv_obj_t *label2 = lv_label_create(lv_scr_act());
    lv_label_set_text(label2, message2);
    lv_obj_set_pos(label2, 0, 260);

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", 8192, NULL, 10, NULL);

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

esp_err_t display_show_message(const char* msg)
{
    ESP_LOGI(TAG, "Displaying message: %s", msg);
    // Code to display the message on the screen

    return ESP_OK;
}
