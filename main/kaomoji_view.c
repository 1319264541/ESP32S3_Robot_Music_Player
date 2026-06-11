#include "kaomoji_view.h"
#include "lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "piclib.h"
#include "gif.h"
#include "lcd.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "emoji";
static const char *FILE_NAMES[] = { "1.gif", "2.gif", "3.gif" };

static lv_obj_t    *img_obj = NULL;
static lv_img_dsc_t img_dsc;
static uint16_t    *img_buf = NULL;
static uint16_t     buf_w = 0, buf_h = 0;
static bool         g_ready = false;

static uint8_t  g_idx     = 0;
static uint32_t g_version = 0;
static SemaphoreHandle_t g_mutex = NULL;
static _pic_phy orig_phy;

/* ---- GIF状态（持久化，供逐帧动画） ---- */
static FIL     gif_file;
static gif89a  gif_state;
static LZW_INFO gif_lzw;
static FSIZE_t gif_frame0_pos;  /* 第一帧起始位置（循环用） */
static bool    gif_open = false;
static lv_timer_t *gif_timer = NULL;

/* ---- 缓冲回调 ---- */
static void buf_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t c)
{
    if (!img_buf) return;
    if (sx >= buf_w) sx = buf_w - 1;
    if (ex >= buf_w) ex = buf_w - 1;
    if (sy >= buf_h) sy = buf_h - 1;
    if (ey >= buf_h) ey = buf_h - 1;
    if (sx > ex || sy > ey) return;
    for (int y = sy; y <= ey; y++)
        for (int x = sx; x <= ex; x++)
            img_buf[y * buf_w + x] = c;
}
static void buf_point(uint16_t x, uint16_t y, uint16_t c)
{ if (img_buf && x < buf_w && y < buf_h) img_buf[y * buf_w + x] = c; }
static void buf_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t c)
{
    if (!img_buf || y >= buf_h || x >= buf_w) return;
    if (x + len > buf_w) len = buf_w - x;
    for (int i = 0; i < len; i++) img_buf[y * buf_w + x + i] = c;
}

/* ---- 显示缓冲到LVGL ---- */
static void show_frame(void)
{
    /* 仅更新图像源，不重新设置尺寸/位置/缩放（首次由load_gif设置） */
    lvgl_port_lock();
    lv_img_set_src(img_obj, &img_dsc);
    lvgl_port_unlock();
}

/* ---- GIF帧刷新 ---- */
static void gif_next_frame(lv_timer_t *t)
{
    if (!gif_open || !img_buf) return;

    /* 不清空白底，让GIF解码器自己处理背景（避免闪烁） */
    uint8_t r = gif_decode_one(&gif_file, &gif_state, 0, 0);

    /* 恢复全局颜色表（帧可能有局部颜色表，不恢复会导致偏色） */
    if (gif_state.gifISD.flag & 0x80)
        gif_restore_ctbl(&gif_state);

    if (r == 2) {
        f_lseek(&gif_file, gif_frame0_pos);
        gif_state.delay = 10;
        gif_decode_one(&gif_file, &gif_state, 0, 0);
        if (gif_state.gifISD.flag & 0x80)
            gif_restore_ctbl(&gif_state);
    }
    show_frame();

    uint32_t d = gif_state.delay * 10;
    if (d < 30) d = 30;
    if (d > 1000) d = 1000;
    lv_timer_set_period(t, d);
}

/* ---- 关闭GIF ---- */
static void gif_close(void)
{
    if (gif_timer) { lv_timer_del(gif_timer); gif_timer = NULL; }
    if (gif_open) { f_close(&gif_file); gif_open = false; }
    if (img_buf) { free(img_buf); img_buf = NULL; }
    /* 恢复pic_phy为LCD默认 */
    memcpy(&pic_phy, &orig_phy, sizeof(pic_phy));
}

/* ---- 加载GIF ---- */
static void load_gif(uint8_t idx)
{
    if (idx > 2) idx = 0;
    char path[64];
    snprintf(path, sizeof(path), "0:/emoji/%s", FILE_NAMES[idx]);
    ESP_LOGI(TAG, "load %s", path);

    gif_close();

    /* 打开文件 */
    if (f_open(&gif_file, path, FA_READ)) { ESP_LOGE(TAG, "open fail"); return; }
    gif_open = true;

    /* 获取信息 */
    f_lseek(&gif_file, 6);
    memset(&gif_state, 0, sizeof(gif_state));
    memset(&gif_lzw, 0, sizeof(gif_lzw));
    gif_state.lzw = &gif_lzw;
    if (gif_getinfo(&gif_file, &gif_state)) { ESP_LOGE(TAG, "getinfo fail"); gif_close(); return; }
    buf_w = gif_state.gifLSD.width;
    buf_h = gif_state.gifLSD.height;
    ESP_LOGI(TAG, "gif %ux%u", buf_w, buf_h);

    /* 记录帧数据起始位置（用于循环） */
    gif_frame0_pos = f_tell(&gif_file);

    /* 分配缓冲 */
    if (img_buf) { free(img_buf); img_buf = NULL; }
    size_t need = buf_w * buf_h * 2;
    img_buf = malloc(need);
    if (!img_buf) { ESP_LOGE(TAG, "buf fail %u", (unsigned)need); gif_close(); return; }
    ESP_LOGI(TAG, "buf OK");

    /* 拦截回调 */
    piclib_init();
    memcpy(&orig_phy, &pic_phy, sizeof(pic_phy));
    pic_phy.fill = buf_fill; pic_phy.draw_point = buf_point; pic_phy.draw_hline = buf_hline;

    /* 首帧 */
    memset(img_buf, 0xFF, buf_w * buf_h * 2);
    gif_decode_one(&gif_file, &gif_state, 0, 0);
    if (gif_state.gifISD.flag & 0x80) gif_restore_ctbl(&gif_state);

    /* 设置图像描述符 + 显示属性（只设一次，后续帧只换src） */
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.header.w  = buf_w;
    img_dsc.header.h  = buf_h;
    img_dsc.data_size = buf_w * buf_h * 2;
    img_dsc.data      = (const uint8_t *)img_buf;
    lvgl_port_lock();
    lv_img_set_src(img_obj, &img_dsc);
    uint16_t zw = (uint16_t)((uint32_t)320 * 256 / buf_w);
    uint16_t zh = (uint16_t)((uint32_t)240 * 256 / buf_h);
    uint16_t zoom = (zw > zh) ? zw : zh;
    if (zoom < 256) zoom = 256;
    lv_img_set_zoom(img_obj, zoom);
    lv_obj_center(img_obj);
    lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    /* 不恢复回调——后续帧刷新需要继续走缓冲 */
    /* memcpy(&pic_phy, &orig_phy, sizeof(pic_phy)); */

    /* 启动帧刷新定时器 */
    uint32_t d = gif_state.delay * 10;
    if (d < 30) d = 30;
    if (d > 1000) d = 1000;
    gif_timer = lv_timer_create(gif_next_frame, d, NULL);

    ESP_LOGI(TAG, "anim start delay=%lu", d);
}

/* ================================================================== */

void kaomoji_view_init(void)
{
    g_mutex = xSemaphoreCreateMutex();
    piclib_init();
    lvgl_port_lock();
    img_obj = lv_img_create(lv_scr_act());
    lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    g_ready = true;
    g_version = 1;
    ESP_LOGI(TAG, "init done");
}

void kaomoji_view_show(void)
{
    if (!g_ready) return;
    uint8_t idx = 0;
    if (g_mutex) { xSemaphoreTake(g_mutex, portMAX_DELAY); idx = g_idx; xSemaphoreGive(g_mutex); }
    load_gif(idx);
}

void kaomoji_view_hide(void)
{
    gif_close();
    if (!img_obj) return;
    lvgl_port_lock();
    lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

void kaomoji_view_set(uint8_t idx)
{
    if (idx > 2) idx = 0;
    if (!g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_idx = idx;
    g_version++;
    xSemaphoreGive(g_mutex);
}

void kaomoji_display_task(void *pvParameters)
{
    while (!g_ready) vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "task start");
    uint32_t last_ver = 0;
    while (1) {
        uint8_t idx; uint32_t ver;
        if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20))) {
            ver = g_version; idx = g_idx;
            xSemaphoreGive(g_mutex);
            if (ver != last_ver) { load_gif(idx); last_ver = ver; }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
