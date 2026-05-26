/**
 ****************************************************************************************************
 * @file        radar.c
 * @brief       Rd-03D V2 雷达模块驱动
 *              数据帧(30B): AA FF | N 00 | T1(8B) | T2(8B) | T3(8B) | 55 CC
 *              单目标8B: x(2B LE) y(2B LE) speed(2B LE) range_res(2B LE)
 *              坐标编码: bit15=符号(1正0负), bit14-0=绝对值(mm) → 转为cm
 *              速度编码: bit15=符号(1正0负), bit14-0=绝对值(cm/s), 负=靠近雷达
 *              命令帧: FD FC FB FA | len(2B LE) | cmd(2B LE) | [data] | 04 03 02 01
 ****************************************************************************************************
 */

#include "radar.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "RADAR";

#define FRAME_HEADER_0  0xAA
#define FRAME_HEADER_1  0xFF
#define FRAME_FOOTER_0  0x55
#define FRAME_FOOTER_1  0xCC

/* 解码有符号值: bit15=符号(1正0负), bit14-0=绝对值 */
static int16_t decode_s16(uint16_t raw)
{
    int16_t abs_val = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? abs_val : -abs_val;
}

/* ====================================================================
 * 初始化
 * ==================================================================== */

/**
 * @brief       初始化雷达串口 (UART2)
 * @param       baudrate: 波特率 (Rd-03D V2 默认 256000)
 */
void radar_init(uint32_t baudrate)
{
    uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_param_config(RADAR_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART,
        RADAR_TX_GPIO_PIN, RADAR_RX_GPIO_PIN,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART,
        RADAR_RX_BUF_SIZE * 2, RADAR_RX_BUF_SIZE * 2,
        10, NULL, 0));

    ESP_LOGI(TAG, "Radar UART2 init OK, baud=%lu, TX=IO%d, RX=IO%d",
        baudrate, RADAR_TX_GPIO_PIN, RADAR_RX_GPIO_PIN);
}

/* ====================================================================
 * 命令发送 / 响应读取
 * ==================================================================== */

/**
 * @brief       发送命令帧
 * @param       cmd      : 命令字
 * @param       data     : 附加数据(可为NULL)
 * @param       data_len : 附加数据长度(字节)
 * @return      0=成功, -1=发送失败
 */
static int radar_send_cmd(uint16_t cmd, const uint8_t *data, uint16_t data_len)
{
    uint8_t frame[128];
    uint16_t total_data_len = 2 + data_len;  /* cmd(2B) + extra data */
    int pos = 0;

    /* 帧头 */
    frame[pos++] = RADAR_CMD_HEADER_0;
    frame[pos++] = RADAR_CMD_HEADER_1;
    frame[pos++] = RADAR_CMD_HEADER_2;
    frame[pos++] = RADAR_CMD_HEADER_3;

    /* 帧内数据长度 (小端) */
    frame[pos++] = (uint8_t)(total_data_len & 0xFF);
    frame[pos++] = (uint8_t)((total_data_len >> 8) & 0xFF);

    /* 命令字 (小端) */
    frame[pos++] = (uint8_t)(cmd & 0xFF);
    frame[pos++] = (uint8_t)((cmd >> 8) & 0xFF);

    /* 附加数据 */
    if (data && data_len > 0) {
        memcpy(&frame[pos], data, data_len);
        pos += data_len;
    }

    /* 帧尾 */
    frame[pos++] = RADAR_CMD_FOOTER_0;
    frame[pos++] = RADAR_CMD_FOOTER_1;
    frame[pos++] = RADAR_CMD_FOOTER_2;
    frame[pos++] = RADAR_CMD_FOOTER_3;

    int sent = uart_write_bytes(RADAR_UART, frame, pos);
    if (sent != pos) {
        ESP_LOGE(TAG, "cmd 0x%02X: send failed (%d/%d)", cmd, sent, pos);
        return -1;
    }
    return 0;
}

/**
 * @brief       读取命令响应 (同步 FD FC FB FA 帧头)
 * @param       buf      : 输出缓冲区(存放数据+帧尾)
 * @param       max_len  : 缓冲区最大长度
 * @param       timeout_ms: 超时时间
 * @return      >=0 实际读取字节数, -1=超时
 */
static int radar_read_response(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    uint8_t  sync = 0;   /* 已匹配的帧头字节数 */
    uint8_t  hdr[6];     /* 帧头(4B) + 数据长度(2B) */
    int      idx = 0;

    /* 同步帧头 FD FC FB FA + 读取2字节长度 */
    while (idx < 6) {
        uint8_t b;
        if (uart_read_bytes(RADAR_UART, &b, 1, pdMS_TO_TICKS(5)) != 1) {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - start) > timeout_ms)
                return -1;
            continue;
        }

        if (sync < 4) {
            const uint8_t hdr_seq[4] = {0xFD, 0xFC, 0xFB, 0xFA};
            if (b == hdr_seq[sync]) {
                sync++;
            } else {
                sync = (b == 0xFD) ? 1 : 0;
            }
        } else {
            /* sync == 4, 读取2字节长度 */
            hdr[4 + (idx - 4)] = b;
            idx++;
        }

        if (sync == 4 && idx < 4) idx = 4;
    }

    uint16_t data_len = hdr[4] | (hdr[5] << 8);
    uint16_t remaining = data_len + 4;  /* 数据 + 帧尾(4B) */
    if (remaining > max_len) remaining = max_len;

    int total = 0;
    TickType_t t0 = xTaskGetTickCount();
    while (total < remaining) {
        int n = uart_read_bytes(RADAR_UART, buf + total, remaining - total,
                                pdMS_TO_TICKS(50));
        if (n > 0) total += n;
        if (pdTICKS_TO_MS(xTaskGetTickCount() - t0) > 300) break;
    }

    return total;
}

/* ====================================================================
 * 配置命令
 * ==================================================================== */

/**
 * @brief       使能配置 — 任何其他命令前必须先发送此命令
 */
void radar_enable_config(void)
{
    uint8_t data[] = {0x01, 0x00};  /* 命令值: 0x0001=使能 */
    uart_flush_input(RADAR_UART);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(RADAR_UART);
    radar_send_cmd(RADAR_CMD_ENABLE, data, 2);
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(RADAR_UART);
    ESP_LOGI(TAG, "enable config sent");
}

/**
 * @brief       结束配置 — 雷达恢复工作模式
 */
void radar_end_config(void)
{
    uart_flush_input(RADAR_UART);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(RADAR_UART);
    radar_send_cmd(RADAR_CMD_END_CFG, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(RADAR_UART);
    ESP_LOGI(TAG, "end config sent");
}

/**
 * @brief       查询版本号
 * @param       ver_buf : 版本号输出缓冲区 (至少16字节)
 * @param       ver_len : 输出版本号有效长度
 * @return      0=成功
 */
int radar_query_version(uint8_t *ver_buf, uint8_t *ver_len)
{
    uart_flush_input(RADAR_UART);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(RADAR_UART);
    if (radar_send_cmd(RADAR_CMD_VERSION, NULL, 0) != 0) return -1;

    uint8_t rsp[64];
    int len = radar_read_response(rsp, sizeof(rsp), 1000);
    if (len < 6) return -1;

    /* rsp[0..1]=命令字+ACK, rsp[2..3]=状态, rsp[4..5]=版本号长度(LE) */
    uint16_t vlen = rsp[4] | (rsp[5] << 8);
    if (vlen > 16) vlen = 16;
    memcpy(ver_buf, &rsp[6], vlen);
    *ver_len = (uint8_t)vlen;

    ESP_LOGI(TAG, "version query OK, len=%d", vlen);
    return 0;
}

/**
 * @brief       设置波特率 (设置后需用新波特率重新初始化串口)
 * @param       baud_code: RADAR_BAUD_9600 ~ RADAR_BAUD_460800
 */
void radar_set_baudrate(uint8_t baud_code)
{
    uint8_t data[] = {baud_code, 0x00};  /* 命令值 LE: 低字节=波特率代码, 高字节=0 */
    uart_flush_input(RADAR_UART);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(RADAR_UART);
    radar_send_cmd(RADAR_CMD_BAUDRATE, data, 2);
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(RADAR_UART);
    ESP_LOGI(TAG, "set baudrate code=%d sent", baud_code);
}

/**
 * @brief       单/多目标模式切换
 * @param       multi_target: 0=单目标模式, 1=多目标模式
 */
void radar_set_mode(uint8_t multi_target)
{
    uint16_t cmd = multi_target ? RADAR_CMD_MULTI : RADAR_CMD_SINGLE;
    uint8_t rsp[16];
    int ack_ok = 0;

    uart_flush_input(RADAR_UART);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(RADAR_UART);

    if (radar_send_cmd(cmd, NULL, 0) == 0) {
        int len = radar_read_response(rsp, sizeof(rsp), 500);
        if (len >= 4) {
            /* rsp[0..1]=命令字+ACK, 期待 rsp[0]=cmd_low, rsp[1]=0x01(成功) */
            if (rsp[0] == (uint8_t)(cmd & 0xFF) && rsp[1] == 0x01) {
                ack_ok = 1;
            }
        }
    }

    uart_flush_input(RADAR_UART);
    ESP_LOGI(TAG, "set mode %s -> %s",
             multi_target ? "multi" : "single",
             ack_ok ? "ACK OK" : "ACK FAIL");
}

/* ====================================================================
 * 数据帧解析
 * ==================================================================== */

/**
 * @brief       解析单个目标槽位 (8字节)
 * @param       buf : 指向目标的8字节起始位置
 * @param       tgt : 输出目标数据
 * @return      1=该槽位有数据, 0=全零(无目标)
 */
static int radar_parse_slot(const uint8_t *buf, radar_single_target_t *tgt)
{
    uint16_t x_raw = buf[0] | (buf[1] << 8);
    uint16_t y_raw = buf[2] | (buf[3] << 8);
    uint16_t s_raw = buf[4] | (buf[5] << 8);
    uint16_t r_raw = buf[6] | (buf[7] << 8);

    /* 全零槽位 = 无目标 */
    if (x_raw == 0 && y_raw == 0 && s_raw == 0 && r_raw == 0) return 0;

    int16_t x_mm  = decode_s16(x_raw);
    int16_t y_mm  = decode_s16(y_raw);
    int16_t s_cms = decode_s16(s_raw);

    tgt->x                = x_mm / 10;                /* mm → cm */
    tgt->y                = y_mm / 10;
    tgt->speed            = s_cms;                    /* 已是 cm/s */
    tgt->range_resolution = r_raw;                    /* 像素距离值, 单位mm */
    tgt->distance         = (uint16_t)sqrtf((float)(x_mm * x_mm + y_mm * y_mm)) / 10;

    /* 目标角度: -atan2f(x,y), 0=正前方, 正=右侧, 负=左侧, 范围 -180~+180 */
    tgt->angle            = (int16_t)(-atan2f((float)x_mm, (float)y_mm) * 57.29578f);

    return 1;
}

/**
 * @brief       解析Rd-03D V2 完整数据帧(变长)
 * @param       buf    : payload数据(不含帧头帧尾)
 * @param       len    : payload长度(字节), 必须为8的倍数
 * @param       target : 输出目标数据
 * @return      1=成功
 */
static int radar_parse_frame(const uint8_t *buf, uint16_t len, radar_target_t *target)
{
    if (len < 8 || (len % 8) != 0) return 0;

    target->target_count = 0;
    uint8_t slots = len / 8;
    if (slots > 3) slots = 3;

    for (int i = 0; i < slots; i++) {
        if (radar_parse_slot(&buf[i * 8], &target->targets[i])) {
            target->target_count++;
        }
    }

    target->detected   = (target->target_count > 0) ? 1 : 0;
    target->data_valid = 1;
    return 1;
}

/* ====================================================================
 * 质量过滤 + 平滑处理
 * ==================================================================== */

#define RNG_RES_ABS_OK      500         /* 像素距离值 <= 500mm 直接放行 */
#define QUALITY_RATIO_MAX   0.80f       /* rng_res > 500 时, 比值 > 0.8 视为不可靠 */
#define DIST_MIN_CM         15          /* 最小有效距离(cm) */
#define DIST_MAX_CM         800         /* 最大有效距离(cm) */
#define SMOOTH_ALPHA        0.25f       /* EMA平滑系数 */
#define MAX_CONSEC_FAIL     5           /* 连续丢帧超过此值, active置0 */
#define TRACK_MATCH_MAX_CM  50          /* 预测位置匹配最大距离 */

/**
 * @brief       质量过滤 + 轨迹匹配 + EMA平滑
 * @param       raw : 雷达原始数据帧
 * @param       out : 滤波输出
 *
 * 处理管线:
 *   1. 逐个目标质量校验
 *   2. 轨迹预测 + 位置匹配选目标
 *   3. EMA轻平滑
 */
void radar_process_target(radar_target_t *raw, radar_filtered_t *out)
{
    static uint8_t  initialized = 0;
    static uint8_t  was_active  = 0;
    static int16_t  trk_x[2] = {0};     /* 轨迹历史 [n-2, n-1] */
    static int16_t  trk_y[2] = {0};
    static uint8_t  trk_full = 0;

    if (!raw->data_valid || raw->target_count == 0)
    {
        out->consecutive_fail++;
        if (out->consecutive_fail >= MAX_CONSEC_FAIL && out->active) {
            out->active = 0;
            out->confidence = 0;
            trk_full = 0;
            ESP_LOGW(TAG, "lost: no target (fail=%d)", out->consecutive_fail);
        }
        return;
    }

    /* === 第1步: 逐个目标质量校验 === */
    radar_single_target_t *candidates[3] = {NULL};
    uint8_t cand_count = 0;
    for (int i = 0; i < raw->target_count && i < 3; i++)
    {
        radar_single_target_t *t = &raw->targets[i];
        if (t->distance == 0) continue;
        if (t->distance < DIST_MIN_CM || t->distance > DIST_MAX_CM) {
            ESP_LOGD(TAG, "reject T%d: dist=%u cm out of range", i, t->distance);
            continue;
        }
        uint16_t dist_mm = (uint16_t)t->distance * 10;
        uint16_t rng_res = t->range_resolution;
        float ratio = (dist_mm > 0) ? (float)rng_res / (float)dist_mm : 1.0f;
        if (rng_res > RNG_RES_ABS_OK && ratio > QUALITY_RATIO_MAX) {
            ESP_LOGD(TAG, "reject T%d: rng=%u mm / dist=%u mm = %.2f", i, rng_res, dist_mm, ratio);
            continue;
        }
        candidates[cand_count++] = t;
    }

    if (cand_count == 0)
    {
        out->consecutive_fail++;
        if (out->consecutive_fail >= MAX_CONSEC_FAIL && out->active) {
            out->active = 0;
            out->confidence = 0;
            trk_full = 0;
            ESP_LOGW(TAG, "lost: all rejected (fail=%d)", out->consecutive_fail);
        }
        return;
    }

    /* === 第2步: 轨迹预测 + 匹配 === */
    radar_single_target_t *t = NULL;
    uint8_t best_idx = 0;

    if (trk_full)
    {
        /* 线性外推预测下一帧位置 */
        int16_t pred_x = trk_x[1] + (trk_x[1] - trk_x[0]);  /* 2*x1 - x0 */
        int16_t pred_y = trk_y[1] + (trk_y[1] - trk_y[0]);
        uint16_t best_err = TRACK_MATCH_MAX_CM * 10;  /* cm*10 for precision */
        radar_single_target_t *best_match = NULL;

        for (int i = 0; i < cand_count; i++) {
            int16_t dx = (int16_t)(candidates[i]->x - pred_x);
            int16_t dy = (int16_t)(candidates[i]->y - pred_y);
            uint16_t err = (uint16_t)sqrtf((float)(dx * dx + dy * dy));
            if (err < best_err) {
                best_err = err;
                best_match = candidates[i];
            }
        }

        if (best_match) {
            t = best_match;
            ESP_LOGD(TAG, "track: pred(%d,%d) -> T? err=%u cm/10", pred_x, pred_y, best_err);
        }
        else {
            /* 无匹配, 跌回选最近 */
            t = candidates[0];
            for (int i = 1; i < cand_count; i++)
                if (candidates[i]->distance < t->distance) t = candidates[i];
            ESP_LOGD(TAG, "track: no match, fallback to closest dist=%u", t->distance);
        }
    }
    else
    {
        /* 轨迹未满, 选最近 */
        t = candidates[0];
        for (int i = 1; i < cand_count; i++)
            if (candidates[i]->distance < t->distance) t = candidates[i];
    }

    for (int i = 0; i < raw->target_count && i < 3; i++) {
        if (&raw->targets[i] == t) { best_idx = i; break; }
    }
    out->tgt_idx = best_idx;

    /* 更新轨迹历史 */
    if (!trk_full) {
        trk_x[0] = trk_x[1] = t->x;
        trk_y[0] = trk_y[1] = t->y;
        trk_full = 1;
    } else {
        trk_x[0] = trk_x[1]; trk_x[1] = t->x;
        trk_y[0] = trk_y[1]; trk_y[1] = t->y;
    }

    /* === 第3步: EMA轻平滑 === */
    float alpha = SMOOTH_ALPHA;
    if (initialized)
    {
        out->x     = (int16_t)((float)out->x     * (1.0f - alpha) + (float)t->x        * alpha);
        out->y     = (int16_t)((float)out->y     * (1.0f - alpha) + (float)t->y        * alpha);
        out->angle = (int16_t)((float)out->angle * (1.0f - alpha) + (float)t->angle    * alpha);
        out->dist  = (uint16_t)((float)out->dist  * (1.0f - alpha) + (float)t->distance * alpha);
    }
    else
    {
        out->x     = t->x;
        out->y     = t->y;
        out->angle = t->angle;
        out->dist  = t->distance;
        initialized = 1;
    }

    /* === 第4步: 置信度 === */
    uint16_t dist_mm = (uint16_t)t->distance * 10;
    float ratio = (dist_mm > 0) ? (float)t->range_resolution / (float)dist_mm : 1.0f;
    uint8_t conf = (uint8_t)(100.0f * (1.0f - ratio / QUALITY_RATIO_MAX));
    if (conf > 100) conf = 100;

    if (!was_active) {
        ESP_LOGI(TAG, "acquired: T%d x=%d y=%d ang=%d dist=%u conf=%u%%",
                 best_idx, t->x, t->y, t->angle, t->distance, conf);
    }
    was_active = 1;

    out->active           = 1;
    out->confidence       = conf;
    out->consecutive_fail = 0;
}


/* ====================================================================
 * 人体目标跟踪器 (锁定-预测-匹配-更新)
 * ==================================================================== */

#define TRK_MAX_LOST    10      /* 连续丢帧上限 */
#define TRK_MATCH_CM    60      /* 匹配半径(cm), 距离加权优先选近的 */
#define TRK_DIST_WEIGHT 0.25f   /* 距离权重, 锁定时优先选近的目标 */
#define TRK_INIT_ANGLE  60      /* 初始锁定角度范围(°), 与雷达120°FOV一致 */
#define TRK_INIT_MIN_D  30      /* 初始锁定最小距离(cm) */
#define TRK_INIT_MAX_D  600     /* 初始锁定最大距离(cm) */
#define TRK_EMA_ALPHA   0.30f   /* EMA平滑系数 */

void radar_tracker_init(radar_tracker_t *trk)
{
    memset(trk, 0, sizeof(*trk));
    trk->max_lost_frames  = TRK_MAX_LOST;
    trk->max_match_dist   = TRK_MATCH_CM;
    trk->init_angle_range = TRK_INIT_ANGLE;
    trk->init_min_dist    = TRK_INIT_MIN_D;
    trk->init_max_dist    = TRK_INIT_MAX_D;
}

void radar_tracker_update(radar_tracker_t *trk, radar_target_t *raw,
                          radar_filtered_t *out)
{
    out->active = 0;

    if (!raw || !raw->data_valid || raw->target_count == 0) {
        if (trk->locked) {
            trk->lost_frames++;
            if (trk->lost_frames >= trk->max_lost_frames) {
                ESP_LOGW(TAG, "tracker: unlocked (lost %d frames)", trk->lost_frames);
                trk->locked = 0;
                trk->confidence = 0;
                trk->hist_valid = 0;
            }
        }
        return;
    }

    /* 先做一次质量预筛选, 和 radar_process_target 保持一致 */
    radar_single_target_t *candidates[3] = {NULL};
    uint8_t cand_count = 0;
    for (int i = 0; i < raw->target_count && i < 3; i++) {
        radar_single_target_t *t = &raw->targets[i];
        if (t->distance < DIST_MIN_CM || t->distance > DIST_MAX_CM) continue;
        if (t->speed == 0) continue;  /* 静止目标雷达不可靠, 跳过 */
        uint16_t dist_mm = (uint16_t)t->distance * 10;
        float ratio = (dist_mm > 0) ? (float)t->range_resolution / (float)dist_mm : 1.0f;
        if (t->range_resolution > RNG_RES_ABS_OK && ratio > QUALITY_RATIO_MAX) continue;
        candidates[cand_count++] = t;
    }

    if (cand_count == 0) {
        if (trk->locked) {
            trk->lost_frames++;
            if (trk->lost_frames >= trk->max_lost_frames) {
                ESP_LOGW(TAG, "tracker: unlocked (all rejected, lost=%d)", trk->lost_frames);
                trk->locked = 0;
                trk->confidence = 0;
                trk->hist_valid = 0;
            }
        }
        return;
    }

    radar_single_target_t *best = NULL;

    if (trk->locked) {
        /* === 已锁定: 预测位置 + 距离加权评分, 优先选近的目标 === */
        int16_t pred_x = trk->est_x;
        int16_t pred_y = trk->est_y;
        if (trk->hist_valid) {
            /* 线性外推: p = est + (est - prev) = 2*est - prev */
            pred_x = trk->est_x * 2 - trk->prev_x;
            pred_y = trk->est_y * 2 - trk->prev_y;
        }

        /* 评分 = 预测误差(cm) + 距离权重 × 实际距离(cm)
           同时预测误差必须在匹配半径内才算候选 */
        float  best_score = 9999.0f;
        uint16_t best_err  = 0;
        for (int i = 0; i < cand_count; i++) {
            int16_t dx = (int16_t)(candidates[i]->x - pred_x);
            int16_t dy = (int16_t)(candidates[i]->y - pred_y);
            uint16_t err = (uint16_t)sqrtf((float)(dx * dx + dy * dy));
            if (err > trk->max_match_dist) continue;  /* 超出匹配半径, 跳过 */
            float score = (float)err + TRK_DIST_WEIGHT * (float)candidates[i]->distance;
            if (score < best_score) {
                best_score = score;
                best_err  = err;
                best = candidates[i];
            }
        }

        if (best) {
            trk->lost_frames = 0;
            if (trk->confidence < 100) trk->confidence += 5;
            ESP_LOGD(TAG, "tracker: match err=%u score=%.0f conf=%u%%",
                     best_err, best_score, trk->confidence);
        } else {
            trk->lost_frames++;
            ESP_LOGD(TAG, "tracker: no match (lost=%d), pred(%d,%d)",
                     trk->lost_frames, pred_x, pred_y);
            if (trk->lost_frames >= trk->max_lost_frames) {
                ESP_LOGW(TAG, "tracker: unlocked after %d lost frames", trk->lost_frames);
                trk->locked = 0;
                trk->confidence = 0;
                trk->hist_valid = 0;
            }
            return;
        }
    } else {
        /* === 未锁定: 在正前方±init_angle_range、距离范围内选最近目标 === */
        uint16_t min_dist = 0xFFFF;
        for (int i = 0; i < cand_count; i++) {
            int16_t ang = candidates[i]->angle;
            uint16_t d  = candidates[i]->distance;
            if (ang >= -trk->init_angle_range && ang <= trk->init_angle_range
                && d >= (uint16_t)trk->init_min_dist
                && d <= (uint16_t)trk->init_max_dist
                && d < min_dist) {
                min_dist = d;
                best = candidates[i];
            }
        }

        if (best) {
            trk->locked     = 1;
            trk->confidence = 50;
            trk->lost_frames = 0;
            trk->hist_valid  = 0;
            ESP_LOGI(TAG, "tracker: LOCKED ang=%d dist=%u cm", best->angle, best->distance);
        } else {
            return;  /* 没有符合条件的目标, 不输出 */
        }
    }

    /* === EMA 平滑更新 === */
    float alpha = TRK_EMA_ALPHA;
    if (trk->hist_valid) {
        trk->est_x     = (int16_t)((float)trk->est_x     * (1.0f - alpha) + (float)best->x        * alpha);
        trk->est_y     = (int16_t)((float)trk->est_y     * (1.0f - alpha) + (float)best->y        * alpha);
        trk->est_angle = (int16_t)((float)trk->est_angle * (1.0f - alpha) + (float)best->angle    * alpha);
        trk->est_dist  = (uint16_t)((float)trk->est_dist  * (1.0f - alpha) + (float)best->distance * alpha);
    } else {
        trk->est_x     = best->x;
        trk->est_y     = best->y;
        trk->est_angle = best->angle;
        trk->est_dist  = best->distance;
    }

    /* 保存历史 */
    trk->prev_x = trk->est_x;
    trk->prev_y = trk->est_y;
    trk->hist_valid = 1;

    /* === 输出 === */
    out->active           = 1;
    out->x                = trk->est_x;
    out->y                = trk->est_y;
    out->angle            = trk->est_angle;
    out->dist             = trk->est_dist;
    out->confidence       = trk->confidence;
    out->consecutive_fail = trk->lost_frames;
}

/* ====================================================================
 * 数据读取 (状态机 + 变长帧)
 * ==================================================================== */

/**
 * @brief       从雷达读取一帧目标数据 (状态机方式, 支持变长帧)
 * @param       target    : 输出目标数据结构体
 * @param       timeout_ms: 读取超时 (ms), 0=非阻塞
 * @return      1=读取到有效数据, 0=无新数据
 */
int radar_read_target(radar_target_t *target, uint32_t timeout_ms)
{
    typedef enum { S_WAIT_AA, S_WAIT_FF, S_WAIT_03, S_WAIT_00, S_ACCUM } state_t;
    static state_t  state = S_WAIT_AA;
    static uint8_t  payload[32];
    static uint16_t pkt_len = 0;

    uint8_t raw[128];
    int len;

    target->data_valid = 0;

    len = uart_read_bytes(RADAR_UART, raw, sizeof(raw), pdMS_TO_TICKS(timeout_ms));
    if (len <= 0) return 0;

    for (int i = 0; i < len; i++) {
        uint8_t b = raw[i];

        switch (state) {
        case S_WAIT_AA:
            if (b == 0xAA) state = S_WAIT_FF;
            break;

        case S_WAIT_FF:
            state = (b == 0xFF) ? S_WAIT_03 : (b == 0xAA) ? S_WAIT_FF : S_WAIT_AA;
            break;

        case S_WAIT_03:
            state = (b == 0x03) ? S_WAIT_00 : (b == 0xAA) ? S_WAIT_FF : S_WAIT_AA;
            break;

        case S_WAIT_00:
            if (b == 0x00) {
                pkt_len = 0;
                state = S_ACCUM;
            } else {
                state = (b == 0xAA) ? S_WAIT_FF : S_WAIT_AA;
            }
            break;

        case S_ACCUM:
            if (pkt_len < sizeof(payload)) {
                payload[pkt_len++] = b;
            }
            /* 检测帧尾 55 CC */
            if (pkt_len >= 2 && payload[pkt_len - 2] == 0x55 && payload[pkt_len - 1] == 0xCC) {
                uint16_t data_len = pkt_len - 2;  /* 去掉帧尾 */
                if (radar_parse_frame(payload, data_len, target)) {
                    state = S_WAIT_AA;
                    pkt_len = 0;
                    return 1;
                }
                /* 解析失败, 重新同步 */
                state = S_WAIT_AA;
                pkt_len = 0;
            }
            break;
        }
    }

    return 0;
}
