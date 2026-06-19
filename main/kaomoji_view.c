#include "kaomoji_view.h"
#include "lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "piclib.h"
#include "gif.h"
#include "jpeg.h"
#include "lcd.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "emoji";
static const char *FILE_NAMES[] = { "1.gif", "2.gif", "3.gif", "4.jpeg", "5.jpeg", "6.jpeg", "7.jpeg", "8.jpeg" };
#define NUM_FILES (sizeof(FILE_NAMES)/sizeof(FILE_NAMES[0]))

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
    if (!img_obj || !img_buf) return;
    lv_img_set_src(img_obj, &img_dsc);  /* LVGL timer context, lock already held */
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
    if (d < 15) d = 15;
    if (d > 1000) d = 1000;
    lv_timer_set_period(t, d);
}

/* ---- 关闭GIF ---- */
static void gif_close(void)
{
    if (gif_timer) { lv_timer_del(gif_timer); gif_timer = NULL; }
    if (gif_open) { f_close(&gif_file); gif_open = false; }
    /* 清图像源+释放缓冲（调用者已持lvgl_port_lock） */
    if (img_buf && img_obj) {
        lv_img_set_src(img_obj, NULL);
    }
    if (img_buf) {
        free(img_buf);
        img_buf = NULL;
    }
    memcpy(&pic_phy, &orig_phy, sizeof(pic_phy));
}

/* ---- 加载JPEG ---- */
static void load_jpeg(const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "0:/emoji/%s", name);
    ESP_LOGI(TAG, "jpeg load %s", path);

    lvgl_port_lock();
    gif_close();  /* 停止之前的GIF动画 */

    pixel_jpeg **pixels = NULL;
    int jpg_w = 0, jpg_h = 0;
    esp_err_t err = decode_jpeg(&pixels, path, 320, 240, &jpg_w, &jpg_h);
    if (err != ESP_OK || !pixels) {
        ESP_LOGE(TAG, "jpeg decode fail: %d", err);
        /* decode_jpeg already freed pixels on failure, no need to release again */
        goto unlock_out;
    }
    ESP_LOGI(TAG, "jpeg %dx%d", jpg_w, jpg_h);

    buf_w = (uint16_t)jpg_w;
    buf_h = (uint16_t)jpg_h;
    if (img_buf) { free(img_buf); img_buf = NULL; }
    size_t need = buf_w * buf_h * 2;
    img_buf = malloc(need);
    if (!img_buf) {
        ESP_LOGE(TAG, "jpeg buf fail %u", (unsigned)need);
        release_image(&pixels, 320, 240);
        goto unlock_out;
    }

    /* 从2D数组拷贝到平铺缓冲 */
    for (int y = 0; y < jpg_h; y++) {
        memcpy(&img_buf[y * buf_w], pixels[y], (size_t)jpg_w * 2);
    }
    release_image(&pixels, 320, 240);

    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.header.w  = buf_w;
    img_dsc.header.h  = buf_h;
    img_dsc.data_size = need;
    img_dsc.data      = (const uint8_t *)img_buf;
    lv_img_set_src(img_obj, &img_dsc);

    uint16_t zw = (uint16_t)((uint32_t)320 * 256 / buf_w);
    uint16_t zh = (uint16_t)((uint32_t)240 * 256 / buf_h);
    uint16_t zoom = (zw > zh) ? zw : zh;
    if (zoom < 256) zoom = 256;
    lv_img_set_zoom(img_obj, zoom);
    lv_obj_center(img_obj);
    lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "jpeg display OK");
    return;

unlock_out:
    lvgl_port_unlock();
}

/* ---- 加载GIF ---- */
static void load_gif(uint8_t idx)
{
    if (idx >= NUM_FILES) idx = 0;
    const char *name = FILE_NAMES[idx];
    const char *ext = strrchr(name, '.');
    int is_jpg = ext && (ext[1]=='j'||ext[1]=='J') && (ext[2]=='p'||ext[2]=='P');
    if (is_jpg) { load_jpeg(name); return; }
    char path[64];
    snprintf(path, sizeof(path), "0:/emoji/%s", name);
    ESP_LOGI(TAG, "load %s", path);

    lvgl_port_lock();
    gif_close();

    /* 打开文件 */
    if (f_open(&gif_file, path, FA_READ)) { ESP_LOGE(TAG, "open fail"); goto unlock_out; }
    gif_open = true;

    /* 获取信息 */
    /* 校验GIF头 */
    {
        uint8_t hdr[6]; UINT br;
        f_lseek(&gif_file, 0);
        f_read(&gif_file, hdr, 6, &br);
        if (hdr[0]!='G'||hdr[1]!='I'||hdr[2]!='F'||hdr[3]!='8'
            ||(hdr[4]!='7'&&hdr[4]!='9')||hdr[5]!='a') {
            ESP_LOGE(TAG, "not a GIF: %02X%02X%02X%02X%02X%02X",
                     hdr[0],hdr[1],hdr[2],hdr[3],hdr[4],hdr[5]);
            gif_close();
            goto unlock_out;
        }
    }
    memset(&gif_state, 0, sizeof(gif_state));
    memset(&gif_lzw, 0, sizeof(gif_lzw));
    gif_state.lzw = &gif_lzw;
    if (gif_getinfo(&gif_file, &gif_state)) { ESP_LOGE(TAG, "getinfo fail"); gif_close(); goto unlock_out; }
    buf_w = gif_state.gifLSD.width;
    buf_h = gif_state.gifLSD.height;
    ESP_LOGI(TAG, "gif %ux%u", buf_w, buf_h);

    /* 记录帧数据起始位置（用于循环） */
    gif_frame0_pos = f_tell(&gif_file);

    /* 分配缓冲 */
    if (img_buf) { free(img_buf); img_buf = NULL; }
    size_t need = buf_w * buf_h * 2;
    img_buf = malloc(need);
    if (!img_buf) { ESP_LOGE(TAG, "buf fail %u", (unsigned)need); gif_close(); goto unlock_out; }
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

    /* 启动帧刷新定时器 */
    uint32_t d = gif_state.delay * 10;
    if (d < 15) d = 15;
    if (d > 1000) d = 1000;
    gif_timer = lv_timer_create(gif_next_frame, d, NULL);

    ESP_LOGI(TAG, "anim start delay=%lu", d);
    return;

unlock_out:
    lvgl_port_unlock();
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
    lvgl_port_lock();
    gif_close();
    /* 隐藏+清图像源，让雷达面板完整覆盖（不删对象，避免跨核渲染冲突） */
    if (img_obj) {
        lv_img_set_src(img_obj, NULL);
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(lv_scr_act());
    lvgl_port_unlock();
}

void kaomoji_view_set(uint8_t idx)
{
    if (idx >= NUM_FILES) idx = 0;
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
    /* 同步当前版本号，避免启动时自动加载 */
    uint32_t last_ver;
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20))) {
        last_ver = g_version;
        xSemaphoreGive(g_mutex);
    } else {
        last_ver = 0;
    }
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
