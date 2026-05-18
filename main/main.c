/**
 ****************************************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-06-25
 * @brief       音乐播放器 实验
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

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "led.h"
#include "key.h"
#include "my_spi.h"
#include "myiic.h"
#include "lcd.h"
#include "xl9555.h"
#include "fonts.h"
#include "spi_sd.h"
#include "text.h"
#include "exfuns.h"
#include "audioplay.h"
#include "myi2s.h"
#include "es8311.h"
#include "driver/uart.h"
#include "usart.h"


#define RX_BUF_SIZE1 100


// 串口控制标志位（修正命名+逻辑）
uint8_t uart_config1 = 0;        // 串口指令总触发标志（1=有指令待处理）
uint8_t pause_config1 = 0;       // 暂停/播放触发标志（1=需要切换状态）
uint8_t music_key1 = 0;          // 切歌指令标志（KEY0_PRES=下一首，KEY1_PRES=上一首）
uint8_t play_trigger = 0;        // 播放触发标志（1=需要开始播放）

TaskHandle_t UART_Task_Handler;    // 串口任务句柄

/**
 * @brief 串口指令解析任务（独立运行，上电仅启动此任务）
 * @param pvParameters：任务参数（未使用）
 */
void uart_cmd_task(void *pvParameters)
{
    unsigned char rx_buf[RX_BUF_SIZE];
    int len;
    pvParameters = pvParameters; // 消除未使用警告
    while (1)
    {
        // 非阻塞读取串口数据
        len = uart_read_bytes(UART_NUM_1, rx_buf, RX_BUF_SIZE1, pdMS_TO_TICKS(10));
        
        if (len > 0) 
        {
            
            // 解析串口指令
            switch (rx_buf[0])
            {
                case 'P': // 触发播放（仅设置标志，不直接调用播放函数）
                    play_trigger = 1;
					pause_config1 = 2;
                    break;

                case 'O': // 暂停/播放切换
					// play_trigger = 1;
                    pause_config1 = 1;
                    break;

                case '+': // 下一首
                    music_key1 = 2; // 对应原有KEY0_PRES（下一首）
                    break;			
                
                case '-': // 上一首
                    music_key1 = 3; // 对应原有KEY1_PRES（上一首）
                    break;
                
                default:
                    break;
            }
            
            // 标记串口指令待处理
            uart_config1 = 1;
            
            // 清空串口缓冲区
            memset(rx_buf, 0, RX_BUF_SIZE1);
        }
        
        // 任务延时（降低CPU占用）
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
   uint8_t key = 0;

    esp_err_t res;

    res = nvs_flash_init();                             /* 初始化NVS */

    lcd_cfg_t lcd_config_info = {0};
    lcd_config_info.notify_flush_ready = NULL;

    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    led_init();                                         /* 初始化LED */
    key_init();                                         /* 初始化按键 */
    my_spi_init();                                      /* 初始化SPI */
    myiic_init();                                       /* 初始化IIC */
    xl9555_init();                                      /* 初始化按键 */ 
    lcd_init(lcd_config_info);                          /* 初始化LCD */
    es8311_init(I2S_SAMPLE_RATE);                       /* ES8311初始化 */
	usart_init(115200);                                      /* 初始化串口 */
	
	myi2s_init();


    while (sd_spi_init())                               /* 检测不到SD卡 */
    {
        lcd_show_string(30, 120, 200, 16, 16, "SD Card Error!", RED);
        vTaskDelay(500);
        lcd_show_string(30, 140, 200, 16, 16, "Please Check! ", RED);
        vTaskDelay(500);
    }

    while (fonts_init())                                /* 检查字库 */
    {
        lcd_clear(WHITE);                               /* 清屏 */
        lcd_show_string(30, 30, 200, 16, 16, "ESP32-S3", RED);
        
        key = fonts_update_font(30, 50, 16, (uint8_t *)"0:", RED);  /* 更新字库 */

        while (key)                                     /* 更新失败 */
        {
            lcd_show_string(30, 50, 200, 16, 16, "Font Update Failed!", RED);
            vTaskDelay(200);
            lcd_fill(20, 50, 200 + 20, 90 + 16, WHITE);
            vTaskDelay(200);
        }

        lcd_show_string(30, 50, 200, 16, 16, "Font Update Success!   ", RED);
        vTaskDelay(1500);
        lcd_clear(WHITE);                               /* 清屏 */
    }
    
    res = exfuns_init();                                /* 为fatfs相关变量申请内存 */
    //vTaskDelay(500);                                    /* 实验信息显示延时 */

    //text_show_string(30, 50, 200, 16, "正点原子ESP32S3 BOX", 16, 0, RED);
    text_show_string(30, 70, 200, 16, "音乐播放", 16, 0, RED);
    //text_show_string(30, 90, 200, 16, "ATOM@ALIENTEK", 16, 0, RED);

 if (UART_Task_Handler == NULL)
    {
        xTaskCreatePinnedToCore(
            uart_cmd_task,        // 串口任务函数
            "uart_cmd",      // 任务名称
            8192,      // 堆栈大小
            NULL,                 // 任务参数
            3,       // 优先级（低于播放任务的4）
            &UART_Task_Handler,   // 任务句柄
            0                     // 绑定到CPU0（播放任务绑CPU1，避免冲突）
    );
	}

    while (1)
    {
		if(play_trigger == 1) // 检测到播放触发
		{
			play_trigger = 0; // 重置触发标志
			audio_play();      // 调用播放函数（阻塞式）
		}
		
		vTaskDelay(pdMS_TO_TICKS(100)); // 主循环延时，降低CPU占用
    }
}
 