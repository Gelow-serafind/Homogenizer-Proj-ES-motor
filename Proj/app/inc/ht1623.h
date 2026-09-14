/*******************************************************************************
* @filename:  ht1623.h
* @brief:     HT1623 4 线串行 LCD 驱动：命令/写 RAM 原语，无业务语义
* @note       帧格式：CS0 + ID(3位) + 数据 + CS1；帧间 CS 高电平 ≥700ns（tCS）
*              RAM 组织：7 位地址 × 4 位数据（nibble），D0 先发，地址自动递增
*******************************************************************************/
#ifndef HT1623_H
#define HT1623_H

#include <stdint.h>

void ht1623_init(void);   /* 端口初始化 + 清 RAM + 选 RC32K + 开系统/显示 */
void ht1623_write_ram(uint8_t startAddr, const uint8_t *buf, uint8_t len);

#endif /* HT1623_H */
