/*******************************************************************************
* @filename:  ads1115.h
* @brief:     ADS1115 16 位 ADC 驱动（I2C 0x48，PA1=SCL/PA3=SDA AF0）
* @note       仅实现单次转换轮询（无 ALERT/RDY 引脚连接）
*              阻塞版（调试/慢速）与非阻塞两阶段版（周期任务）并存
*******************************************************************************/
#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

void     ads1115_init(void);

/* 阻塞单次转换：内部等待 ~10ms；返回 16 位原始值，0xFFFF = I2C 错误 */
uint16_t ads1115_read(uint8_t ch);

/* 非阻塞两阶段：start 触发转换，≥2ms 后 fetch 读取（避免周期任务阻塞） */
uint8_t  ads1115_start(uint8_t ch);   /* 1=已触发 0=失败 */
uint16_t ads1115_fetch(void);         /* 0xFFFF = I2C 错误 */

void     ads1115_delay_ms(uint32_t ms);   /* 粗延时（调试命令用，不精确） */

#endif /* ADS1115_H */
