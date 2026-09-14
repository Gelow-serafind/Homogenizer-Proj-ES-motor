/*******************************************************************************
* @filename:  temp_sensor.c
* @brief:     PT1000 测温模块实现（骨架版；P4 标定后完善换算与判定）
* @note        采样：阻塞单次转换（~10ms / 500ms 周期，占用可接受）；
*              在位判定骨架（实测参考 2026-09-13）：
*                AIN0 悬空/断路 ≈5V（raw≈26578）、PT1000 分压断开 ≈2.5V（raw≈13286）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ads1115.h"
#include "event_bus.h"
#include "temp_sensor.h"

/* 在位判定阈值（骨架值，P4 标定后修正）：raw ≥ 25000 视为未接入 */
#define TEMP_RAW_INVALID_MIN 25000U

static uint16_t tRaw   = 0xFFFFU;   /* 最近原始值（调试） */
static uint8_t  tValid = 0U;
static int16_t  tDeciC = 0;

void temp_sensor_init(void)
{
  tRaw   = 0xFFFFU;
  tValid = 0U;
  tDeciC = 0;
}

void temp_sensor_ops(void)
{
  uint16_t v = ads1115_read(ADS_CH_TEMP);

  if (v == 0xFFFFU)
  {
    return;   /* I2C 错误：保持上次状态 */
  }
  tRaw = v;

  /* 在位判定（TEMP_VALID_DETECT_EN=0 时保持恒"未接入"，与原行为一致） */
  if (TEMP_VALID_DETECT_EN != 0U)
  {
    uint8_t nv = (v < TEMP_RAW_INVALID_MIN) ? 1U : 0U;

    if (nv != tValid)
    {
      tValid = nv;
      evt_bus_post((nv != 0U) ? EVT_TEMP_VALID : EVT_TEMP_INVALID, 0U);
    }
  }

  /* TODO(P4)：PT1000 阻值 → 温度（×10）换算（待通道归属与分压网络实测后实现） */
  tDeciC = 0;
}

int16_t temp_sensor_get_deci_c(void)
{
  return tDeciC;
}

uint8_t temp_sensor_is_valid(void)
{
  return tValid;
}
