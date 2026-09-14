/*******************************************************************************
* @filename:  speed_sensor.h
* @brief:     测速模块：SPEED_SW 周期法测速 / RPM（架构第 3.2 节）
* @note       测量 = PB4/EXTI4 时间戳 + TIM6 µs 时基，周期平均 + 低通滤波（20ms 定频）；
*              无脉冲超时归零；链路已上板验证通（2026-09-14），PPR=18 已标定（2026-09-15）；
*              PB4 与 USART1_RX 同网络，编码器与 USB-TTL 不可同时接
*******************************************************************************/
#ifndef SPEED_SENSOR_H
#define SPEED_SENSOR_H

#include <stdint.h>

void     speed_sensor_init(void);
void     speed_sensor_ops(void);          /* 20ms：周期法算 RPM + 滤波 + 停转检测 */
uint16_t speed_sensor_get_rpm(void);      /* 滤波后实测转速（显示/到达判定/PID 共用） */
uint32_t speed_sensor_get_pulses(void);   /* 累计脉冲数（标定/调试；上电清零） */
void     speed_sensor_set_ppr(uint16_t ppr);

#endif /* SPEED_SENSOR_H */
