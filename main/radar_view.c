#include "radar_view.h"
#include "lvgl_port.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

/* ---- 雷达坐标系 ---- */
#define ORIGIN_X     160       /* 原点屏幕X坐标（底部居中） */
#define ORIGIN_Y     225       /* 原点屏幕Y坐标 */
#define MAX_DIST_CM  300       /* 满量程3米 */
#define MAX_RADIUS   155       /* 3米对应像素半径（0°方向） */
#define SCALE        (MAX_RADIUS / (float)MAX_DIST_CM)

/* ---- 目标圆点外观 ---- */
#define DOT_R        4         /* 圆点半径（像素） */
#define NUM_DOTS     3

/* ---- 预创建的LVGL对象 ---- */
static lv_obj_t *dots[NUM_DOTS];
static lv_obj_t *label_title;
static lv_obj_t *label_info;
static bool g_view_ready = false;

/* ---- 辅助函数 ---- */
static inline lv_color_t grey(void)   { return lv_color_make(128, 128, 128); }
static inline lv_color_t dkgrey(void) { return lv_color_make(64, 64, 64); }

/* 雷达极坐标（角度°, 距离cm）→ 屏幕像素坐标 */
static void radar_to_screen(int16_t ang_deg, uint16_t dist_cm,
                            lv_coord_t *px, lv_coord_t *py)
{
    float r   = dist_cm * SCALE;
    float rad = ang_deg * (float)M_PI / 180.0f;
    *px = (lv_coord_t)(ORIGIN_X + r * sinf(rad));
    *py = (lv_coord_t)(ORIGIN_Y - r * cosf(rad));
}

/* ---- 自定义绘制事件：网格、弧线、标签 ---- */
static void panel_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_point_t o = {ORIGIN_X, ORIGIN_Y};

    /* ---- 同心距离环（1m、2m、3m），用线段逼近圆弧避免分块裁剪问题 ---- */
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = grey();
    line_dsc.width = 1;

    /* 三个距离环半径：3m=155, 2m=103, 1m=52 */
    int radii[] = {MAX_RADIUS, MAX_RADIUS * 2/3, MAX_RADIUS / 3};
    for (int ring = 0; ring < 3; ring++) {
        int R = radii[ring];
        lv_point_t prev;
        prev.x = o.x + (lv_coord_t)(R * sinf(-60.0f * (float)M_PI / 180.0f));
        prev.y = o.y - (lv_coord_t)(R * cosf(-60.0f * (float)M_PI / 180.0f));
        for (int deg = -58; deg <= 60; deg += 2) {
            float rad = deg * (float)M_PI / 180.0f;
            lv_point_t cur;
            cur.x = o.x + (lv_coord_t)(R * sinf(rad));
            cur.y = o.y - (lv_coord_t)(R * cosf(rad));
            lv_draw_line(ctx, &line_dsc, &prev, &cur);
            prev = cur;
        }
    }

    /* ---- 径向角度线：-60°、-30°、0°、+30°、+60° ---- */
    static const int ang_deg[] = {-60, -30, 0, 30, 60};
    for (int i = 0; i < 5; i++) {
        float rad = ang_deg[i] * (float)M_PI / 180.0f;
        lv_point_t ep;
        ep.x = o.x + (lv_coord_t)(MAX_RADIUS * sinf(rad));
        ep.y = o.y - (lv_coord_t)(MAX_RADIUS * cosf(rad));
        lv_draw_line(ctx, &line_dsc, &o, &ep);
    }

    /* ---- 0°线上的距离标签 ---- */
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.color = dkgrey();
    lbl_dsc.font  = LV_FONT_DEFAULT;

    lv_area_t la;
    la.x1 = o.x + 4;
    la.x2 = la.x1 + 60;
    la.y1 = o.y - MAX_RADIUS / 3 - 10;
    la.y2 = la.y1 + 24;
    lv_draw_label(ctx, &lbl_dsc, &la, "1m", NULL);
    la.y1 = o.y - MAX_RADIUS * 2/3 - 10;
    la.y2 = la.y1 + 24;
    lv_draw_label(ctx, &lbl_dsc, &la, "2m", NULL);
    la.y1 = o.y - MAX_RADIUS - 10;
    la.y2 = la.y1 + 24;
    lv_draw_label(ctx, &lbl_dsc, &la, "3m", NULL);

    /* ---- 外弧附近的角度标签 ---- */
    for (int i = 0; i < 5; i++) {
        float rad = ang_deg[i] * (float)M_PI / 180.0f;
        int16_t r = MAX_RADIUS + 14;
        la.x1 = o.x + (lv_coord_t)(r * sinf(rad)) - 12;
        la.x2 = la.x1 + 60;
        la.y1 = o.y - (lv_coord_t)(r * cosf(rad)) - 10;
        la.y2 = la.y1 + 24;

        char buf[12];
        if (ang_deg[i] >= 0) snprintf(buf, sizeof(buf), "+%d", ang_deg[i]);
        else                 snprintf(buf, sizeof(buf), "%d",  ang_deg[i]);
        lv_draw_label(ctx, &lbl_dsc, &la, buf, NULL);
    }
}

/* ---- 创建小圆点对象 ---- */
static lv_obj_t *create_dot(lv_obj_t *parent)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, DOT_R * 2, DOT_R * 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

/* ---- 定位圆点（居中对齐） ---- */
static void place_dot(lv_obj_t *dot, lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_set_pos(dot, cx - DOT_R, cy - DOT_R);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

/* ================================================================== */
/*  公共接口                                                            */
/* ================================================================== */

void radar_view_init(void)
{
    ESP_LOGI("radar_view", "init begin");
    lvgl_port_lock();
    ESP_LOGI("radar_view", "mutex taken");

    /* ---- 活动屏幕（白色背景） ---- */
    lv_obj_t *scr = lv_scr_act();
    ESP_LOGI("radar_view", "scr_act done");
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- 面板：全屏，在DRAW_POST中绘制雷达网格 ---- */
    lv_obj_t *panel = lv_obj_create(scr);
    ESP_LOGI("radar_view", "panel created");
    lv_obj_set_size(panel, 320, 240);
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_add_event_cb(panel, panel_draw_cb, LV_EVENT_DRAW_POST, NULL);

    /* ---- 标题 ---- */
    label_title = lv_label_create(scr);
    lv_label_set_text(label_title, "Rd-03D");
    lv_obj_set_style_text_color(label_title, dkgrey(), 0);
    lv_obj_set_pos(label_title, 8, 4);
    lv_obj_set_width(label_title, LV_SIZE_CONTENT);
    ESP_LOGI("radar_view", "title created");

    /* ---- 信息（目标数量） ---- */
    label_info = lv_label_create(scr);
    lv_label_set_text(label_info, "No target");
    lv_obj_set_style_text_color(label_info, dkgrey(), 0);
    lv_obj_set_pos(label_info, 240, 4);
    lv_obj_set_width(label_info, LV_SIZE_CONTENT);

    /* ---- 3个目标圆点（初始隐藏） ---- */
    for (int i = 0; i < NUM_DOTS; i++) {
        dots[i] = create_dot(scr);
    }
    ESP_LOGI("radar_view", "dots created");

    lvgl_port_unlock();
    g_view_ready = true;
    ESP_LOGI("radar_view", "init done");
}

void radar_view_update(radar_target_t *target)
{
    if (!g_view_ready) return;
    static uint8_t last_count = 255;
    lvgl_port_lock();

    if (!target || !target->detected) {
        for (int i = 0; i < NUM_DOTS; i++)
            lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
        if (last_count != 0) {
            lv_label_set_text(label_info, "No target");
            last_count = 0;
        }
        lvgl_port_unlock();
        return;
    }

    uint8_t n = target->target_count;
    if (n > NUM_DOTS) n = NUM_DOTS;

    if (n != last_count) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Targets: %u", n);
        lv_label_set_text(label_info, buf);
        last_count = n;
    }

    for (int i = 0; i < NUM_DOTS; i++) {
        if (i < n) {
            lv_coord_t px, py;
            radar_to_screen(target->targets[i].angle,
                           target->targets[i].distance, &px, &py);
            place_dot(dots[i], px, py);
        } else {
            lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_port_unlock();
}

/* ================================================================== */
/*  共享数据 + 显示任务                                                 */
/* ================================================================== */

static radar_target_t g_radar_data;
static SemaphoreHandle_t g_radar_mutex;

void radar_view_set_data(radar_target_t *target)
{
    if (!g_radar_mutex) return;
    xSemaphoreTake(g_radar_mutex, portMAX_DELAY);
    if (target) memcpy(&g_radar_data, target, sizeof(radar_target_t));
    else        g_radar_data.detected = 0;
    xSemaphoreGive(g_radar_mutex);
}

void radar_display_task(void *pvParameters)
{
    g_radar_mutex = xSemaphoreCreateMutex();

    /* 等CPU1上的radar_view_init完成，否则dots[]还是NULL会崩溃 */
    while (!g_view_ready) vTaskDelay(pdMS_TO_TICKS(10));

    while (1) {
        radar_target_t local;
        if (g_radar_mutex
            && xSemaphoreTake(g_radar_mutex, pdMS_TO_TICKS(100))) {
            memcpy(&local, &g_radar_data, sizeof(radar_target_t));
            xSemaphoreGive(g_radar_mutex);
            radar_view_update(&local);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
