/**
 ****************************************************************************************************
 * @file        radar.h
 * @brief       Rd-03D V2 雷达模块驱动 (24GHz 轨迹跟踪)
 *              支持: 使能配置/结束配置/版本查询/波特率设置/单多目标模式切换
 ****************************************************************************************************
 */

#ifndef _RADAR_H
#define _RADAR_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/* 引脚和串口定义 */
#define RADAR_UART              UART_NUM_2
#define RADAR_TX_GPIO_PIN       GPIO_NUM_18
#define RADAR_RX_GPIO_PIN       GPIO_NUM_8

/* 接收缓冲区大小 */
#define RADAR_RX_BUF_SIZE       1024

/* ===== 命令帧格式 ===== */
#define RADAR_CMD_HEADER_0      0xFD
#define RADAR_CMD_HEADER_1      0xFC
#define RADAR_CMD_HEADER_2      0xFB
#define RADAR_CMD_HEADER_3      0xFA

#define RADAR_CMD_FOOTER_0      0x04
#define RADAR_CMD_FOOTER_1      0x03
#define RADAR_CMD_FOOTER_2      0x02
#define RADAR_CMD_FOOTER_3      0x01

/* ===== 命令字 ===== */
#define RADAR_CMD_ENABLE        0xFF  /* 使能配置 */
#define RADAR_CMD_END_CFG       0xFE  /* 结束配置 */
#define RADAR_CMD_VERSION       0x00  /* 版本号查询 */
#define RADAR_CMD_BAUDRATE      0x31  /* 波特率设置 */
#define RADAR_CMD_SINGLE        0x80  /* 单目标模式 */
#define RADAR_CMD_MULTI         0x90  /* 多目标模式 */

/* ===== 波特率代码 ===== */
#define RADAR_BAUD_9600         0x01
#define RADAR_BAUD_19200        0x02
#define RADAR_BAUD_38400        0x03
#define RADAR_BAUD_57600        0x04
#define RADAR_BAUD_115200       0x05
#define RADAR_BAUD_230400       0x06
#define RADAR_BAUD_256000       0x07
#define RADAR_BAUD_460800       0x08

/* ===== 单目标信息 ===== */
typedef struct {
    int16_t  x;                 /* X坐标 (cm) */
    int16_t  y;                 /* Y坐标 (cm), 正=远离雷达 */
    int16_t  speed;             /* 速度 (cm/s), 负数=靠近雷达 */
    int16_t  angle;             /* 目标角度 (deg), 0=正前方, 正=右侧, 负=左侧 */
    uint16_t range_resolution;  /* 像素距离值 (mm) */
    uint16_t distance;          /* 计算距离 sqrt(x^2+y^2) cm */
} radar_single_target_t;

/* ===== 雷达目标数据结构体 ===== */
typedef struct {
    uint8_t  detected;          /* 0=无人, 1=有人 */
    uint8_t  target_count;      /* 非零目标槽位数 (0~3) */
    uint8_t  data_valid;        /* 数据有效标志 */
    uint8_t  raw_slots;         /* 帧内原始槽位数 (1=单目标, 3=多目标) */
    radar_single_target_t targets[3];  /* 最多3个目标 */
} radar_target_t;

/* ===== 滤波状态 / 输出 ===== */
typedef struct {
    uint8_t  active;            /* 0=无目标, 1=跟踪中 */
    int16_t  x;                 /* 平滑后X坐标 (cm) */
    int16_t  y;                 /* 平滑后Y坐标 (cm) */
    int16_t  angle;             /* 平滑后角度 (度) */
    uint16_t dist;              /* 平滑后距离 (cm) */
    uint8_t  confidence;        /* 置信度 0~100, 值越大越可靠 */
    uint8_t  consecutive_fail;  /* 连续丢帧计数 */
    uint8_t  tgt_idx;           /* 选中的目标索引 (0/1/2) */
} radar_filtered_t;

/* ===== 人体目标跟踪器 ===== */
typedef struct {
    uint8_t  locked;            /* 0=未锁定, 1=已锁定目标 */
    uint8_t  confidence;        /* 锁定置信度 0~100 */
    uint8_t  lost_frames;       /* 连续丢帧计数 */

    int16_t  est_x, est_y;      /* 位置EMA估计 (cm) */
    int16_t  est_angle;         /* 角度估计 (度) */
    uint16_t est_dist;          /* 距离估计 (cm) */

    int16_t  prev_x, prev_y;    /* 上一帧位置, 用于速度预测 */
    uint8_t  hist_valid;        /* 历史位置是否有效 */

    uint8_t  max_lost_frames;   /* 连续丢帧上限, 超过则解锁 (默认10) */
    uint16_t max_match_dist;    /* 匹配半径(cm), 预测位置±此范围才算同一目标 (默认80) */
    int16_t  init_angle_range;  /* 初始锁定角度范围(°), 正前方±此范围 (默认30) */
    int16_t  init_min_dist;     /* 初始锁定最小距离(cm) (默认30) */
    int16_t  init_max_dist;     /* 初始锁定最大距离(cm) (默认600) */
} radar_tracker_t;

/**
 * @brief       初始化跟踪器为默认参数
 */
void radar_tracker_init(radar_tracker_t *trk);

/**
 * @brief       人体目标跟踪 (锁定-预测-匹配-更新)
 * @param       trk : 跟踪器状态 (需先用 radar_tracker_init 初始化)
 * @param       raw : 雷达原始目标帧
 * @param       out : 输出: 跟踪到的目标 (角度/距离等)
 *
 * 策略:
 *   未锁定时: 在正前方±init_angle_range内、距离最近的目标作为初始锁定
 *   锁定后:   线性外推预测位置 → 在max_match_dist内找最近匹配 → EMA更新
 *            连续丢帧超过max_lost_frames则解锁, 不会因旁侧闯入者而切走
 */
void radar_tracker_update(radar_tracker_t *trk, radar_target_t *raw,
                          radar_filtered_t *out);

/* ===== 函数声明 ===== */
void radar_init(uint32_t baudrate);                                     /* 初始化雷达串口 */
int  radar_read_target(radar_target_t *target, uint32_t timeout_ms);    /* 读取目标数据 */

/* 数据处理 */
void radar_process_target(radar_target_t *raw, radar_filtered_t *out);  /* 质量过滤+平滑，每帧调用 */

/* 配置命令 */
void radar_enable_config(void);                                         /* 使能配置命令 */
void radar_end_config(void);                                            /* 结束配置命令 */
int  radar_query_version(uint8_t *ver_buf, uint8_t *ver_len);           /* 版本号查询 */
void radar_set_baudrate(uint8_t baud_code);                             /* 波特率设置 */
void radar_set_mode(uint8_t multi_target);                              /* 模式切换: 0=单目标, 1=多目标 */

#endif
