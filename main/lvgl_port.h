#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "lvgl.h"
#include "esp_err.h"

/**
 * @brief  初始化LVGL显示端口：显示驱动、1ms节拍定时器、处理任务。
 *         需在lcd_init()之后调用。
 * @return 成功返回ESP_OK
 */
esp_err_t lvgl_port_init(void);

/**
 * @brief  获取LVGL互斥锁。在LVGL处理任务之外的任务中
 *         调用LVGL API前必须获取。
 */
void lvgl_port_lock(void);

/**
 * @brief  释放LVGL互斥锁。
 */
void lvgl_port_unlock(void);

#endif
