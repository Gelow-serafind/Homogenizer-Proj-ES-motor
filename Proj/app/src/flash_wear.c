/*******************************************************************************
* @filename:  flash_wear.c
* @brief:     Flash 末页槽轮转原语实现（原 LoadParams/SaveParams 拆分，行为等价）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "hk32f0301mxxc_flash.h"
#include "flash_wear.h"

/**
  * @brief  槽起始地址
  */
static uint32_t flash_wear_slot_addr(uint8_t slot)
{
  return FLASH_SAVE_PAGE_ADDR + ((uint32_t)slot * FLASH_SAVE_SLOT_SIZE);
}

/**
  * @brief  从末槽向前扫描，返回最后一条结构有效记录
  * @param  w0/w1: 输出原始两个 32 位字
  * @return 1=找到 0=无有效记录
  */
uint8_t flash_wear_load(uint32_t *w0, uint32_t *w1)
{
  int16_t i;

  for (i = (int16_t)(FLASH_SAVE_SLOTS - 1U); i >= 0; i--)
  {
    uint32_t addr = flash_wear_slot_addr((uint8_t)i);
    uint32_t v0 = *(volatile uint32_t*)(uintptr_t)addr;
    uint32_t v1 = *(volatile uint32_t*)(uintptr_t)(addr + 4U);

    if ((v0 != 0xFFFFFFFFU) && (v1 != 0xFFFFFFFFU) && ((v1 >> 16) == 0U))
    {
      *w0 = v0;
      *w1 = v1;
      return 1U;
    }
  }
  return 0U;
}

/**
  * @brief  查找第一个空槽（判据：w0 == 0xFFFFFFFF）
  * @return 槽号；0xFF = 无空槽
  */
uint8_t flash_wear_find_empty_slot(void)
{
  uint8_t i;

  for (i = 0U; i < FLASH_SAVE_SLOTS; i++)
  {
    if (*(volatile uint32_t*)(uintptr_t)flash_wear_slot_addr(i) == 0xFFFFFFFFU)
    {
      return i;
    }
  }
  return 0xFFU;
}

/**
  * @brief  擦除整页（写满 16 槽后调用）
  */
void flash_wear_erase_page(void)
{
  FLASH_Unlock();
  FLASH_ErasePage(FLASH_SAVE_PAGE_ADDR);
  FLASH_Lock();
}

/**
  * @brief  写 slot 的第 wordIdx 个 32 位字
  * @param  slot: 0..15
  * @param  wordIdx: 0=w0 1=w1
  * @param  val: 32 位数据
  */
void flash_wear_write_word(uint8_t slot, uint8_t wordIdx, uint32_t val)
{
  if ((slot >= FLASH_SAVE_SLOTS) || (wordIdx > 1U))
  {
    return;
  }

  FLASH_Unlock();
  FLASH_ProgramWord(flash_wear_slot_addr(slot) + ((uint32_t)wordIdx * 4U), val);
  FLASH_Lock();
}
