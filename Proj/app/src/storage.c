/*******************************************************************************
* @filename:  storage.c
* @brief:     参数存储模块实现（槽格式与原实现一致，兼容既有已存数据）
* @note       槽格式：w0 = rpm | (h<<16)；w1 = m | (s<<8)；rsv(高16)=0
*******************************************************************************/
#include "main.h"
#include <string.h>
#include "app_cfg.h"
#include "flash_wear.h"
#include "storage.h"

static user_param_t storageCurrent;              /* 当前已加载/已保存参数 */
static user_param_t storageWriting;              /* 当前两 word 共用的固定快照 */
static user_param_t storagePending;              /* 待写入参数 */

/* 分步写状态机：0=空闲 1=写 w0 2=写 w1 */
static uint8_t storageSaveState  = 0U;
static uint8_t storageSaveSlot   = 0U;
static uint8_t storageNeedAgain  = 0U;           /* 写 w0 期间又有新请求 → 完成后重写一轮 */

static uint32_t storage_pack_w0(const user_param_t *p)
{
  return (uint32_t)p->rpm | ((uint32_t)p->h << 16);
}

static uint32_t storage_pack_w1(const user_param_t *p)
{
  return (uint32_t)p->m | ((uint32_t)p->s << 8);
}

/**
  * @brief  上电加载：取最后一条结构有效记录并做范围校验（同原实现）
  */
void storage_init(void)
{
  uint32_t w0 = 0U;
  uint32_t w1 = 0U;

  memset(&storageCurrent, 0, sizeof(storageCurrent));
  memset(&storagePending, 0, sizeof(storagePending));
  memset(&storageWriting, 0, sizeof(storageWriting));
  storageCurrent.rpm = RPM_MIN;
  storageSaveState = 0U;
  storageNeedAgain = 0U;

  if (flash_wear_load(&w0, &w1) != 0U)
  {
    uint16_t rpm = (uint16_t)(w0 & 0xFFFFU);
    uint16_t h   = (uint16_t)(w0 >> 16);
    uint8_t  m   = (uint8_t)(w1 & 0xFFU);
    uint8_t  s   = (uint8_t)((w1 >> 8) & 0xFFU);

    if ((rpm <= RPM_MAX) && (h <= TIME_HOUR_MAX) && (m < 60U) && (s < 60U))
    {
      storageCurrent.rpm = (rpm < RPM_MIN) ? RPM_MIN : rpm;
      storageCurrent.h   = h;
      storageCurrent.m   = m;
      storageCurrent.s   = s;
    }
  }
}

/**
  * @brief  请求保存（快速入队，实际写 Flash 在 storage_ops 分步完成）
  */
uint8_t storage_request_save(const user_param_t *p)
{
  if (p == 0)
  {
    return 0U;
  }

  if ((storageSaveState == 0U) && (p->rpm == storageCurrent.rpm) &&
      (p->h == storageCurrent.h) && (p->m == storageCurrent.m) && (p->s == storageCurrent.s))
  {
    return 1U;   /* 无变化的单击确认不消耗 Flash 槽 */
  }
  storagePending = *p;
  if (storageSaveState == 0U)
  {
    storageSaveState = 1U;
  }
  else
  {
    storageNeedAgain = 1U;   /* 正在写：完成后用最新 pending 重写一轮 */
  }
  return 1U;
}

/**
  * @brief  分步写任务（10ms）：每周期 ≤1 word，避免长阻塞
  */
void storage_ops(void)
{
  switch (storageSaveState)
  {
    case 1U:
      storageWriting = storagePending;
      storageNeedAgain = 0U;
      storageSaveSlot = flash_wear_find_empty_slot();
      if (storageSaveSlot == 0xFFU)
      {
        flash_wear_erase_page();          /* 写满 16 槽：擦页重来（单次 <50ms 停顿） */
        storageSaveSlot = 0U;
      }
      flash_wear_write_word(storageSaveSlot, 0U, storage_pack_w0(&storageWriting));
      storageSaveState = 2U;
      break;

    case 2U:
      flash_wear_write_word(storageSaveSlot, 1U, storage_pack_w1(&storageWriting));
      storageCurrent = storageWriting;
      storageSaveState = 0U;
      if (storageNeedAgain != 0U)
      {
        storageNeedAgain = 0U;
        storageSaveState = 1U;
      }
      break;

    default:
      break;
  }
}

/**
  * @brief  读取当前参数（app_state 上电初始化用）
  */
void storage_get_params(user_param_t *p)
{
  if (p == 0)
  {
    return;
  }
  *p = storageCurrent;
}
