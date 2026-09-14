/*******************************************************************************
* @filename:  speed_sensor.h
* @brief:     光电测速模块：AIN 脉冲计数窗口、RPM 换算、滤波（架构第 3.2 节）
* @note       R1：SPEED_SW 硬件链路未通，当前为骨架（两阶段采样 + 边沿计数）；
*              通道归属见 app_cfg.h（ADS_CH_SPEED，飞线后实测为准）；
*              PPR（每转脉冲数，A10）标定后经 speed_sensor_set_ppr 配置
*******************************************************************************/
#ifndef SPEED_SENSOR_H
#define SPEED_SENSOR_H

#include <stdint.h>

void     speed_sensor_init(void);
void     speed_sensor_ops(void);          /* 20ms：两阶段采样 + 200ms 计数窗口 */
uint16_t speed_sensor_get_rpm(void);
void     speed_sensor_set_ppr(uint16_t ppr);

#endif /* SPEED_SENSOR_H */
