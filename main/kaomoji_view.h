#ifndef KAOMOJI_VIEW_H
#define KAOMOJI_VIEW_H

#include <stdint.h>

/**
 * @brief  初始化GIF表情控件（LVGL lv_gif）。
 *         需在lvgl_port_init()之后、LVGL上下文中调用。
 */
void kaomoji_view_init(void);

/**
 * @brief  显示当前表情GIF。
 */
void kaomoji_view_show(void);

/**
 * @brief  隐藏表情GIF。
 */
void kaomoji_view_hide(void);

/**
 * @brief  切换表情文件（线程安全）。
 * @param  idx  0=1.gif, 1=2.gif, 2=3.gif
 */
void kaomoji_view_set(uint8_t idx);

/**
 * @brief  FreeRTOS任务，监听切换并刷新。
 */
void kaomoji_display_task(void *pvParameters);

#endif
