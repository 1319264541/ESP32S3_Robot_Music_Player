#ifndef RADAR_VIEW_H
#define RADAR_VIEW_H

#include "lvgl.h"
#include "radar.h"

/**
 * @brief  在活动LVGL屏幕上创建雷达可视化界面。
 *         绘制120°扇区网格（距离环、角度线、标签），
 *         并准备目标圆点对象。在lvgl_port_init()之后调用一次。
 */
void radar_view_init(void);

/**
 * @brief  根据雷达帧更新目标位置。
 *         需在radar_view_init()之后调用。线程安全（内部互斥锁）。
 * @param  target  解析后的雷达帧；为NULL或未检测到目标时隐藏所有圆点。
 */
void radar_view_update(radar_target_t *target);

/**
 * @brief  为显示任务保存最新的雷达帧。
 *         线程安全，可在雷达任务中调用。
 * @param  target  解析后的雷达帧（深拷贝），NULL表示无目标。
 */
void radar_view_set_data(radar_target_t *target);

/**
 * @brief  FreeRTOS任务，周期性读取共享雷达数据并更新LVGL显示
 *         （约20Hz）。绑定CPU0，优先级2。
 */
void radar_display_task(void *pvParameters);

#endif
