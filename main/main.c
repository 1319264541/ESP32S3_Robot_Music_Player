/**
 ****************************************************************************************************
 * @file        main.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2024-06-25
 * @brief       ���ֲ����� ʵ��
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ESP32S3 BOX ������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
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
// #include "audioplay.h"  // ���ֲ�����ע��
#include "myi2s.h"
#include "es8311.h"
#include "driver/uart.h"
#include "usart.h"
#include "radar.h"
#include "lvgl_port.h"
#include "radar_view.h"
#include "kaomoji_view.h"
#include "esp_log.h"


#define RX_BUF_SIZE1 100


// ���ڿ��Ʊ�־λ����������+�߼���
uint8_t uart_config1 = 0;        // ����ָ���ܴ�����־��1=��ָ���������
uint8_t pause_config1 = 0;       // ��ͣ/���Ŵ�����־��1=��Ҫ�л�״̬��
uint8_t music_key1 = 0;          // �и�ָ���־��KEY0_PRES=��һ�ף�KEY1_PRES=��һ�ף�
uint8_t play_trigger = 0;        // ���Ŵ�����־��1=��Ҫ��ʼ���ţ�

TaskHandle_t UART_Task_Handler;

static int16_t  g_track_ang  = 0;
static uint16_t g_track_dist = 0;
static uint8_t  g_track_conf = 0;
static uint8_t  g_track_has  = 0;

/* ��ʾģʽ: 0=�״�(L), 1=������(E) */
static uint8_t  g_display_mode = 1;  /* Ĭ��������ģʽ */
static uint8_t  g_kaomoji_sel  = 0;    /* ��ǰ���������� 0/1/2 */


/**
 * @brief ����ָ��������񣨶������У��ϵ������������
 * @param pvParameters�����������δʹ�ã�
 */
void uart_cmd_task(void *pvParameters)
{
    unsigned char rx_buf[RX_BUF_SIZE];
    int len;
    uint32_t last_send_ms = 0;

    pvParameters = pvParameters; // ����δʹ�þ���
    static const char *UTAG = "uart_cmd";
    while (1)
    {
        // ��������ȡ��������
        len = uart_read_bytes(UART_NUM_1, rx_buf, RX_BUF_SIZE1, pdMS_TO_TICKS(10));
        
        if (len > 0) 
        {
            
            // ��������ָ��
            switch (rx_buf[0])
            {
                /* ---- ���ֲ���ָ����ע�� ----
                case 'P':
                    play_trigger = 1;
                    pause_config1 = 2;
                    break;
                case 'O':
                    pause_config1 = 1;
                    break;
                case '+':
                    music_key1 = 2;
                    break;
                case '-':
                    music_key1 = 3;
                    break;
                ---- */

                case 'L':
                case 'l': // �״�ģʽ�������״�+LVGL��ʾ
                    ESP_LOGI(UTAG, "-> Radar mode");
                    g_display_mode = 0;
                    radar_view_show();
                    kaomoji_view_hide();
                    break;

                case 'E':
                case 'e': // ������ģʽ���ر��״��ʾ������
                    ESP_LOGI(UTAG, "-> Kaomoji mode");
                    g_display_mode = 1;
                     g_kaomoji_sel = 0;
                    kaomoji_view_set(0);
                    radar_view_hide();
                    kaomoji_view_show();
                    break;

                case '1': // ������1
                    ESP_LOGI(UTAG, "-> Kaomoji 1");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 0;
                        kaomoji_view_set(0);
                    }
                    break;

                case '2': // ������2
                    ESP_LOGI(UTAG, "-> Kaomoji 2");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 1;
                        kaomoji_view_set(1);
                    }
                    break;

                case '3': // ������3
                    ESP_LOGI(UTAG, "-> Kaomoji 3");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 2;
                        kaomoji_view_set(2);
                    }
                    break;

                case '4':
                    ESP_LOGI(UTAG, "-> Kaomoji 4");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 3;
                        kaomoji_view_set(3);
                    }
                    break;

                case '5':
                    ESP_LOGI(UTAG, "-> Kaomoji 5");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 4;
                        kaomoji_view_set(4);
                    }
                    break;

                case '6':
                    ESP_LOGI(UTAG, "-> Kaomoji 6");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 5;
                        kaomoji_view_set(5);
                    }
                    break;

                case '7':
                    ESP_LOGI(UTAG, "-> Kaomoji 7");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 6;
                        kaomoji_view_set(6);
                    }
                    break;

                case '8':
                    ESP_LOGI(UTAG, "-> Kaomoji 8");
                    if (g_display_mode == 1) {
                        g_kaomoji_sel = 7;
                        kaomoji_view_set(7);
                    }
                    break;

                default:
                    break;
            }
            
            // ��Ǵ���ָ�������
            uart_config1 = 1;
            
            // ��մ��ڻ�����
            memset(rx_buf, 0, RX_BUF_SIZE1);
        }
        
        // ������ʱ������CPUռ�ã�
        vTaskDelay(pdMS_TO_TICKS(10));

        /* 5Hz radar tracker upload (���״�ģʽ) */
        uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
        if (g_display_mode == 0 && g_track_has && (now - last_send_ms >= 100)) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf),
                "T,%d,%u\r\n",
                g_track_ang, g_track_dist);
            uart_write_bytes(UART_NUM_1, buf, n);
            last_send_ms = now;
        }
    }
}
/**
 * @brief       �������
 * @param       ��
 * @retval      ��
 */
/**
 * @brief       �״����ݶ�ȡ����
 * @param       pvParameters��δʹ��
 */
void radar_task(void *pvParameters)
{
    radar_target_t target = {0};
    radar_tracker_t tracker;
    radar_filtered_t trk_out;
    radar_tracker_init(&tracker);
    while (1)
    {
        /* ������ģʽ�������״��ȡ�������״�ģ�鷢�� */
        if (g_display_mode != 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (radar_read_target(&target, 50))
        {
            radar_tracker_update(&tracker, &target, &trk_out);
            static uint8_t _lc = 0;
            if (trk_out.active) {
                g_track_ang  = trk_out.angle;
                g_track_dist = trk_out.dist;
                g_track_conf = trk_out.confidence;
                g_track_has  = 1;
                if (++_lc >= 5) {
                    ESP_LOGI("TRACKER", "ang=%d dist=%u conf=%u%%",
                             trk_out.angle, trk_out.dist, trk_out.confidence);
                    _lc = 0;
                }
            } else {
                if (++_lc >= 5) {
                    ESP_LOGI("TRACKER", "no target (locked=%u conf=%u%%)",
                             tracker.locked, tracker.confidence);
                    _lc = 0;
                }
            }
            radar_view_set_data(&target);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
   uint8_t key = 0;

    esp_err_t res;

    res = nvs_flash_init();                             /* ��ʼ��NVS */

    lcd_cfg_t lcd_config_info = {0};
    lcd_config_info.notify_flush_ready = NULL;

    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    led_init();                                         /* ��ʼ��LED */
    key_init();                                         /* ��ʼ������ */
    my_spi_init();                                      /* ��ʼ��SPI */
    myiic_init();                                       /* ��ʼ��IIC */
    xl9555_init();                                      /* ��ʼ������ */ 
    lcd_init(lcd_config_info);                          /* ��ʼ��LCD */
    es8311_init(I2S_SAMPLE_RATE);                       /* ES8311��ʼ�� */
	usart_init(230400);
	radar_init(256000);
radar_set_mode(1);
	// radar_end_config();  /* Ĭ��emojiģʽ�������״����ݣ���Lָ���ٿ��� */
	
	myi2s_init();


    while (sd_spi_init())                               /* ��ⲻ��SD�� */
    {
        lcd_show_string(30, 120, 200, 16, 16, "SD Card Error!", RED);
        vTaskDelay(500);
        lcd_show_string(30, 140, 200, 16, 16, "Please Check! ", RED);
        vTaskDelay(500);
    }

    while (fonts_init())                                /* ����ֿ� */
    {
        lcd_clear(WHITE);                               /* ���� */
        lcd_show_string(30, 30, 200, 16, 16, "ESP32-S3", RED);
        
        key = fonts_update_font(30, 50, 16, (uint8_t *)"0:", RED);  /* �����ֿ� */

        while (key)                                     /* ����ʧ�� */
        {
            lcd_show_string(30, 50, 200, 16, 16, "Font Update Failed!", RED);
            vTaskDelay(200);
            lcd_fill(20, 50, 200 + 20, 90 + 16, WHITE);
            vTaskDelay(200);
        }

        lcd_show_string(30, 50, 200, 16, 16, "Font Update Success!   ", RED);
        vTaskDelay(1500);
        lcd_clear(WHITE);                               /* ���� */
    }
    
    res = exfuns_init();                                /* Ϊfatfs��ر��������ڴ� */

    /* LVGL��ʼ�� */
    lvgl_port_init();
    //vTaskDelay(500);                                    /* ʵ����Ϣ��ʾ��ʱ */

    //text_show_string(30, 50, 200, 16, "����ԭ��ESP32S3 BOX", 16, 0, RED);
    //text_show_string(30, 70, 200, 16, "���ֲ���", 16, 0, RED);
    //text_show_string(30, 90, 200, 16, "ATOM@ALIENTEK", 16, 0, RED);

 if (UART_Task_Handler == NULL)
    {
        xTaskCreatePinnedToCore(
            uart_cmd_task,        // ����������
            "uart_cmd",      // ��������
            8192,      // ��ջ��С
            NULL,                 // �������
            3,       // ���ȼ������ڲ��������4��
            &UART_Task_Handler,   // ������
            0                     // �󶨵�CPU0�����������CPU1�������ͻ��
    );
	}

	TaskHandle_t radar_task_handle = NULL;
	xTaskCreatePinnedToCore(
		radar_task, "radar_task", 6144, NULL, 5, &radar_task_handle, 0);
	
	TaskHandle_t radar_display_task_handle = NULL;
	xTaskCreatePinnedToCore(
		radar_display_task, "radar_disp", 4096, NULL, 2, &radar_display_task_handle, 0);

    /* ��������ʾ�������״���ʾ����ģʽ���⣩ */
    TaskHandle_t kaomoji_task_handle = NULL;
    xTaskCreatePinnedToCore(
        kaomoji_display_task, "kaomoji_disp", 8192, NULL, 2,
        &kaomoji_task_handle, 0);

    while (!radar_view_is_ready()) vTaskDelay(pdMS_TO_TICKS(10));
    kaomoji_view_show();
    radar_view_hide();

    while (1)
    {
        /* ---- ���ֲ�����ע�� ----
        if (play_trigger == 1) {
            play_trigger = 0;
            audio_play();
        }
        ---- */

        vTaskDelay(pdMS_TO_TICKS(1000)); /* ��ѭ������ */
    }
}
 