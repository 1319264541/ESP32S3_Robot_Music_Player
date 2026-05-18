/**
 ****************************************************************************************************
 * @file        es8311.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       ES8311驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __ES8311_H_
#define __ES8311_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_types.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "myiic.h"
#include "math.h"
#include "string.h"
#include "myi2s.h"
 
 
#define ES8311_ADDR                     0x18	/* ES8311的器件地址,芯片地址必须是001100x，其中x等于CE（输入引脚：1表示数字输入高电平，0表示数字输入低电平） */

/* 定义MCLK的时钟源 */
#define FROM_MCLK_PIN                   0
#define FROM_SCLK_PIN                   1
#define MCLK_SOURCE                     1

/* MCLK_DIV_FRE是LRCLK的分频系数 */
#define MCLK_DIV_FRE                    32

/* 定义是否翻转时钟 */
#define INVERT_MCLK                     0       /* 设置为0时：默认状态，MCLK 信号正常，即按照芯片内部默认的电平逻辑进行输出/设置为1时：将 MCLK 信号进行反转，原本的高电平变为低电平，低电平变为高电平。在某些硬件连接或特定通信协议要求下，可能需要反转时钟信号电平来保证数据传输的正确性 */
#define INVERT_SCLK                     0		/* 设置为0时：默认状态，BCLK 信号正常，即按照芯片内部默认的电平逻辑进行输出/设置为1时：将 BCLK 信号进行反转，原本的高电平变为低电平，低电平变为高电平。在某些硬件连接或特定通信协议要求下，可能需要反转时钟信号电平来保证数据传输的正确性 */

#define IS_DMIC                         0		/* 设置为0时：禁用 DMIC，MIC1P 引脚作为模拟麦克风输入/设置为1时：启用 DMIC，MIC1P 引脚用作 DMIC 的 SDA 信号线 */

/* ES8311寄存器 寄存器名称 寄存器地址 */
#define ES8311_RESET_REG00              0x00    /* 复位数字电路、芯片状态机、时钟管理器等 */

/* 时钟方案寄存器定义 */
#define ES8311_CLK_MANAGER_REG01        0x01    /* 为MCLK选择时钟源，为编解码器启用时钟 */
#define ES8311_CLK_MANAGER_REG02        0x02    /* 时钟分频器和时钟倍频器 */
#define ES8311_CLK_MANAGER_REG03        0x03    /* ADC帧同步模式和过采样率 */
#define ES8311_CLK_MANAGER_REG04        0x04    /* DAC过采样率 */
#define ES8311_CLK_MANAGER_REG05        0x05    /* ADC和DAC的时钟分频器 */
#define ES8311_CLK_MANAGER_REG06        0x06    /* BCLK反相器和分频器 */
#define ES8311_CLK_MANAGER_REG07        0x07    /* 三态控制，LRCK分频器 */
#define ES8311_CLK_MANAGER_REG08        0x08    /* LRCK分频器 */

/* 串行数字端口（SDP） */
#define ES8311_SDPIN_REG09              0x09    /* DAC串行数字端口配置 */
#define ES8311_SDPOUT_REG0A             0x0A    /* ADC串行数字端口配置 */

/* 系统控制 */
#define ES8311_SYSTEM_REG0B             0x0B    /* 系统配置 */
#define ES8311_SYSTEM_REG0C             0x0C    /* 系统配置 */
#define ES8311_SYSTEM_REG0D             0x0D    /* 系统上电/掉电控制 */
#define ES8311_SYSTEM_REG0E             0x0E    /* 系统电源管理 */
#define ES8311_SYSTEM_REG0F             0x0F    /* 系统低功耗模式配置 */
#define ES8311_SYSTEM_REG10             0x10    /* 系统配置 */
#define ES8311_SYSTEM_REG11             0x11    /* 系统配置 */
#define ES8311_SYSTEM_REG12             0x12    /* 启用DAC输出 */
#define ES8311_SYSTEM_REG13             0x13    /* 系统配置 */
#define ES8311_SYSTEM_REG14             0x14    /* 选择数字麦克风，配置模拟PGA增益 */

/* ADC配置 */
#define ES8311_ADC_REG15                0x15    /* ADC斜坡速率设置，数字麦克风信号检测 */
#define ES8311_ADC_REG16                0x16    /* ADC配置 */
#define ES8311_ADC_REG17                0x17    /* ADC音量控制 */
#define ES8311_ADC_REG18                0x18    /* 自动电平控制（ALC）使能及窗口大小 */
#define ES8311_ADC_REG19                0x19    /* ALC最大电平设置 */
#define ES8311_ADC_REG1A                0x1A    /* ALC自动静音功能 */
#define ES8311_ADC_REG1B                0x1B    /* ALC自动静音，ADC高通滤波器设置1 */
#define ES8311_ADC_REG1C                0x1C    /* ADC均衡器，高通滤波器设置2 */

/* DAC配置 */
#define ES8311_DAC_REG31                0x31    /* DAC静音控制 */
#define ES8311_DAC_REG32                0x32    /* DAC音量控制 */ 
#define ES8311_DAC_REG33                0x33    /* DAC输出偏移校准*/
#define ES8311_DAC_REG34                0x34    /* 动态范围压缩（DRC）使能及窗口大小 */
#define ES8311_DAC_REG35                0x35    /* DRC最大/最小电平设置 */
#define ES8311_DAC_REG37                0x37    /* DAC斜坡速率控制 */

/* GPIO配置 */
#define ES8311_GPIO_REG44               0x44    /* GPIO功能配置，用于测试DAC转ADC路径 */
#define ES8311_GP_REG45                 0x45    /* 通用控制寄存器 */

/* 芯片信息 */
#define ES8311_CHD1_REGFD               0xFD    /* 芯片ID1（0x83）*/
#define ES8311_CHD2_REGFE               0xFE    /* 芯片ID2（0x11）*/
#define ES8311_CHVER_REGFF              0xFF    /* 芯片版本号 */

typedef enum {
    ES8311_MIC_GAIN_MIN = -1,
    ES8311_MIC_GAIN_0DB,
    ES8311_MIC_GAIN_6DB,
    ES8311_MIC_GAIN_12DB,
    ES8311_MIC_GAIN_18DB,
    ES8311_MIC_GAIN_24DB,
    ES8311_MIC_GAIN_30DB,
    ES8311_MIC_GAIN_36DB,
    ES8311_MIC_GAIN_42DB,
    ES8311_MIC_GAIN_MAX
} es8311_mic_gain_t;

typedef enum {
    ES_MODULE_MIN = -1,
    ES_MODULE_ADC = 0x01,
    ES_MODULE_DAC = 0x02,
    ES_MODULE_ADC_DAC = 0x03,
    ES_MODULE_LINE = 0x04,
    ES_MODULE_MAX
} es_module_t;

typedef enum {
    ES_I2S_MIN = -1,
    ES_I2S_NORMAL = 0,
    ES_I2S_LEFT = 1,
    ES_I2S_RIGHT = 2,
    ES_I2S_DSP = 3,
    ES_I2S_MAX
} es_i2s_fmt_t;

/* 声明函数 */
esp_err_t es8311_init(int sample_fre);
esp_err_t es8311_deinit(void);
esp_err_t es8311_set_voice_volume(int volume);
esp_err_t es8311_get_voice_volume(int *volume);
void es8311_read_all();
esp_err_t es8311_read_reg(uint8_t reg_addr);
esp_err_t es8311_write_reg(uint8_t reg_addr, uint8_t data);
int es8311_set_voice_mute(int enable);
int es8311_set_mic_gain(es8311_mic_gain_t gain_db);

#endif
