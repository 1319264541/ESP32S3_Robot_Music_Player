#include "lvgl_port.h"
#include "lcd.h"
#include "radar_view.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "lvgl_port";

#define DISP_W  320
#define DISP_H  240
#define BUF_LINES  40  /* 单缓冲40行 = 25.6KB，双缓冲共51.2KB */

static lv_color_t *disp_buf1;
static lv_color_t *disp_buf2;
static lv_disp_draw_buf_t draw_buf;
static SemaphoreHandle_t lvgl_mutex;

/* ---- 1ms节拍（硬件定时器中断） ---- */
static void tick_cb(void *arg) { lv_tick_inc(1); }

/* ---- 显示刷新回调 ---- */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_map)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x1 + w, area->y1 + h,
                              (uint16_t *)color_map);
    lv_disp_flush_ready(drv);
}

/* ---- LVGL处理任务（CPU1，初始化雷达视图 + 渲染循环） ---- */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "lvgl_task started on CPU%d", xPortGetCoreID());

    /* 在LVGL上下文里创建雷达界面，避免跨任务竞态 */
    ESP_LOGI(TAG, "calling radar_view_init...");
    radar_view_init();
    ESP_LOGI(TAG, "radar_view_init done, entering loop");

    const TickType_t period = pdMS_TO_TICKS(5);
    while (1) {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10))) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
        vTaskDelay(period);
    }
}

/* ---- 公共接口 ---- */
esp_err_t lvgl_port_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();
    if (!lvgl_mutex) {
        ESP_LOGE(TAG, "LVGL互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    /* 从DMA安全内存分配双缓冲，避免i80 DMA对齐问题 */
    size_t buf_bytes = DISP_W * BUF_LINES * sizeof(lv_color_t);
    disp_buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    disp_buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!disp_buf1 || !disp_buf2) {
        ESP_LOGE(TAG, "DMA缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&draw_buf, disp_buf1, disp_buf2, DISP_W * BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISP_W;
    disp_drv.ver_res  = DISP_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* 1ms节拍 */
    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&tick_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000);

    /* 处理任务绑定CPU1，优先级2，独立于雷达/串口任务 */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 4096, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "初始化完成");
    return ESP_OK;
}

void lvgl_port_lock(void)
{
    if (lvgl_mutex) xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
}

void lvgl_port_unlock(void)
{
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}
