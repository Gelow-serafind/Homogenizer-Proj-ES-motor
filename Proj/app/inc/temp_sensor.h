/*******************************************************************************
* @filename:  temp_sensor.h
* @brief:     PT1000 测温模块：AIN 采样、在位检测、温度换算（架构第 3.2 节）
* @note       R2/ P4：通道归属与分压网络待实测；TEMP_VALID_DETECT_EN=0 期间
*              保持"未检测"（与原行为一致），标定后启用骨架判定并实现温度换算
*******************************************************************************/
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>

void     temp_sensor_init(void);
void     temp_sensor_ops(void);          /* 500ms：单次采样 + 在位判定 + 事件 */
int16_t  temp_sensor_get_deci_c(void);   /* 温度 ×10（0.1℃），P4 后实现换算 */
uint8_t  temp_sensor_is_valid(void);     /* 探头在位（十字架图标） */

#endif /* TEMP_SENSOR_H */
