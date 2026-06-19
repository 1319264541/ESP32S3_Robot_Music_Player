#include "lvgl_port.h"
#include "lcd.h"
#include "radar_view.h"
#include "kaomoji_view.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "lvgl_port";

#define DISP_W  320
#define DISP_H  240
#define BUF_LINES  80  /* 单缓冲80行 = 51.2KB，双缓冲共102.4KB */

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
    kaomoji_view_init();
    ESP_LOGI(TAG, "radar_view_init + kaomoji_view_init done, entering loop");

    while (1) {
        lvgl_port_lock();
        uint32_t ms = lv_timer_handler();
        lvgl_port_unlock();
        if (ms > 10) ms = 10;
        if (ms < 1)  ms = 1;
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/* ---- LVGL文件系统驱动（FatFS→LVGL 'S'盘） ---- */

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    FIL *fp = malloc(sizeof(FIL));
    if (!fp) return NULL;
    char fatfs_path[256];
    snprintf(fatfs_path, sizeof(fatfs_path), "0:%s", path);
    BYTE fm = (mode & LV_FS_MODE_WR) ? FA_READ | FA_WRITE | FA_CREATE_ALWAYS : FA_READ;
    FRESULT fr = f_open(fp, fatfs_path, fm);
    ESP_LOGI("lv_fs", "open %s -> %d", fatfs_path, fr);
    if (fr == FR_OK) return fp;
    free(fp);
    return NULL;
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *fp)
{
    ESP_LOGI("lv_fs", "close");
    if (fp) { f_close((FIL *)fp); free(fp); }
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *fp, void *buf, uint32_t btr, uint32_t *br)
{
    UINT r = 0;
    FRESULT res = f_read((FIL *)fp, buf, btr, &r);
    if (br) *br = r;
    ESP_LOGI("lv_fs", "read %lu -> %d (got %u)", btr, res, r);
    return (res == FR_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *fp, uint32_t pos, lv_fs_whence_t whence)
{
    FRESULT res;
    if (whence == LV_FS_SEEK_SET) res = f_lseek((FIL *)fp, pos);
    else if (whence == LV_FS_SEEK_CUR) res = f_lseek((FIL *)fp, f_tell((FIL *)fp) + pos);
    else if (whence == LV_FS_SEEK_END) res = f_lseek((FIL *)fp, f_size((FIL *)fp) + pos);
    else return LV_FS_RES_FS_ERR;
    return (res == FR_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *fp, uint32_t *pos)
{
    *pos = f_tell((FIL *)fp);
    return LV_FS_RES_OK;
}

static void fs_drv_register_sd(void)
{
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = 'S';
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = NULL;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;
    lv_fs_drv_register(&fs_drv);
}

/* ---- 公共接口 ---- */
esp_err_t lvgl_port_init(void)
{
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
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

    /* 注册SD卡文件系统驱动（供lv_gif等使用 'S:' 路径） */
    fs_drv_register_sd();

    /* 处理任务绑定CPU1，优先级2，独立于雷达/串口任务 */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "初始化完成");
    return ESP_OK;
}

void lvgl_port_lock(void)
{
    if (lvgl_mutex) xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY);
}

void lvgl_port_unlock(void)
{
    if (lvgl_mutex) xSemaphoreGiveRecursive(lvgl_mutex);
}
