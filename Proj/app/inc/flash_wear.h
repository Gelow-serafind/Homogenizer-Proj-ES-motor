/*******************************************************************************
* @filename:  flash_wear.h
* @brief:     Flash 末页磨损均衡原语（无业务语义）
* @note       区域：末页 128B（0x08003F80），16 槽 × 8B
*              槽格式：{w0 u32, w1 u32}，空槽 = 两个 u32 全 0xFFFFFFFF
*              结构有效判据：w0/w1 均非全 FF，且 w1 高 16 位（rsv）=0（半写槽自然作废）
*******************************************************************************/
#ifndef FLASH_WEAR_H
#define FLASH_WEAR_H

#include <stdint.h>

uint8_t flash_wear_load(uint32_t *w0, uint32_t *w1);   /* 1=找到最后一条结构有效记录 */
uint8_t flash_wear_find_empty_slot(void);              /* 空槽号；0xFF=无空槽（需擦页） */
void    flash_wear_erase_page(void);                   /* 擦除整页 */
void    flash_wear_write_word(uint8_t slot, uint8_t wordIdx, uint32_t val);  /* 写 slot 的第 idx 个 32 位字 */

#endif /* FLASH_WEAR_H */
