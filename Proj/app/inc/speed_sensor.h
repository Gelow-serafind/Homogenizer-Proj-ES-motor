/*******************************************************************************
* @filename:  speed_sensor.h
* @brief:     测速模块：SPEED_SW 脉冲计数 / RPM 换算（架构第 3.2 节）
* @note       PB4 + EXTI4 单边沿计数；链路已上板验证通（2026-09-14），
*              18 计数/圈已标定（2026-09-15）；PB4 与 USART1_RX 同网络，
*              编码器与 USB-TTL 不可同时接
*******************************************************************************/
#ifndef SPEED_SENSOR_H
#define SPEED_SENSOR_H

#include <stdint.h>

void     speed_sensor_init(void);
void     speed_sensor_ops(void);          /* 20ms：窗口维护（累计计数在 EXTI ISR 中） */
uint16_t speed_sensor_get_rpm(void);      /* 窗口换算 RPM */
uint32_t speed_sensor_get_pulses(void);   /* 累计脉冲数（标定/调试；上电清零） */
void     speed_sensor_set_ppr(uint16_t ppr);

#endif /* SPEED_SENSOR_H */
