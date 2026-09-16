/*******************************************************************************
* @filename:  temp_sensor.h
* @brief:     PT1000 测温模块：比值法双通道采样、查表换算、在位检测（架构第 3.2 节）
* @note       电路原理图核实 2026-09-15：VCC_5V—R16(1k)—ADC_PT1000+（探头）—GND；
*              R_t = 1000×A0/(2×A1−A0)（比值法）；温度表 IEC 60751（0~300℃）
*******************************************************************************/
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>

void     temp_sensor_init(void);
void     temp_sensor_ops(void);          /* 500ms：单次采样 + 在位判定 + 事件 */
int16_t  temp_sensor_get_deci_c(void);   /* 温度 ×10（0.1℃），P4 后实现换算 */
uint8_t  temp_sensor_is_valid(void);     /* 探头在位（十字架图标） */

/* 调试接口（shell templog 命令）：周期打印 AIN0/AIN1 原始值与等效电压（标定用） */
void     temp_sensor_log_enable(uint8_t enable);
uint8_t  temp_sensor_log_is_enabled(void);

#endif /* TEMP_SENSOR_H */
