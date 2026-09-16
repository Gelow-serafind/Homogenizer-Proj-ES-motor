/*******************************************************************************
* @filename:  temp_sensor.c
* @brief:     PT1000 测温模块：比值法双通道采样 + 查表换算（2026-09-15 实装）
* @note        电路（原理图核实 2026-09-15）：VCC_5V —R16(1k)— ADC_PT1000+（CN3.1）
*              — 探头 — GND；AIN0 测探头节点，AIN1 测 R19/R18(1k:1k) 基准中点。
*              比值法（免疫电源波动，无需知道 VCC）：R_t = 1000 × A0/(2×A1 − A0)，
*              A0/A1 = 两通道原始码；拔/插由 2×A1−A0 判定（悬空≈0，插 25℃≈12106）。
*              温度：IEC 60751 公式生成表（0~300℃ 每 5℃）+ 线性插值 → ℃×10。
*              采样：阻塞单次转换 2 路 ≈20ms / 500ms 周期，占用可接受。
*******************************************************************************/
#include "main.h"
#include <stdio.h>
#include "app_cfg.h"
#include "ads1115.h"
#include "event_bus.h"
#include "temp_sensor.h"

static uint16_t tRawA0 = 0xFFFFU;   /* AIN0 最近原始码（诊断） */
static uint16_t tRawA1 = 0xFFFFU;   /* AIN1 最近原始码（诊断） */
static uint8_t  tValid = 0U;
static int16_t  tDeciC = 0;
static uint8_t  tLogEn = 0U;        /* 调试：持续打印开关（shell templog） */

/* 在位判定去抖（连续 2 次一致才切换，防拔插瞬态抖动） */
static uint8_t  tValidPend = 0U;
static uint8_t  tValidCnt  = 0U;

/* IEC 60751 PT1000：R×10（Ω），0~300℃ 每 5℃；
   由 R(t)=1000×(1+3.9083e-3·t−5.775e-7·t²) 生成 */
static const uint16_t RTableX10[61] = {
  10000U, 10195U, 10390U, 10585U, 10779U, 10973U, 11167U, 11361U,
  11554U, 11747U, 11940U, 12132U, 12324U, 12516U, 12708U, 12899U,
  13090U, 13280U, 13471U, 13661U, 13851U, 14040U, 14229U, 14418U,
  14607U, 14795U, 14983U, 15171U, 15358U, 15546U, 15733U, 15919U,
  16105U, 16291U, 16477U, 16663U, 16848U, 17033U, 17217U, 17402U,
  17586U, 17769U, 17953U, 18136U, 18319U, 18501U, 18684U, 18866U,
  19047U, 19229U, 19410U, 19591U, 19771U, 19951U, 20131U, 20311U,
  20490U, 20670U, 20848U, 21027U, 21205U,
};

/* static 前置原型 */
static int16_t  temp_r_to_t10(uint32_t rOhm);
static uint16_t temp_raw_to_mv(uint16_t raw);

void temp_sensor_init(void)
{
  tRawA0     = 0xFFFFU;
  tRawA1     = 0xFFFFU;
  tValid     = 0U;
  tDeciC     = 0;
  tLogEn     = 0U;
  tValidPend = 0U;
  tValidCnt  = 0U;
}

void temp_sensor_log_enable(uint8_t enable)
{
  tLogEn = (enable != 0U) ? 1U : 0U;
}

uint8_t temp_sensor_log_is_enabled(void)
{
  return tLogEn;
}

/**
  * @brief  阻值 → 温度（℃×10）：查表 + 线性插值（0~300℃；超范围 clamp）
  * @note   表步进 5℃（50=5℃×10）；R 与 T 单调，区间插值误差 <0.02℃
  */
static int16_t temp_r_to_t10(uint32_t rOhm)
{
  uint32_t r10;
  uint8_t  i;

  if (rOhm < 1000U)
  {
    return 0;          /* <0℃：本应用不涉及，clamp 0.0 */
  }
  if (rOhm > 2120U)
  {
    return 3000;       /* >300℃：clamp 300.0 */
  }

  r10 = rOhm * 10U;
  for (i = 0U; i < 60U; i++)
  {
    if (r10 <= (uint32_t)RTableX10[i + 1U])
    {
      break;
    }
  }

  return (int16_t)((uint16_t)((uint16_t)i * 50U) +
                   (uint16_t)(((r10 - (uint32_t)RTableX10[i]) * 50UL) /
                              (uint32_t)(RTableX10[i + 1U] - RTableX10[i])));
}

/**
  * @brief  原始码 → 等效电压 mV（单端、PGA=±6.144V：1LSB=187.5µV，mV=raw×1875/10000）
  */
static uint16_t temp_raw_to_mv(uint16_t raw)
{
  return (uint16_t)(((uint32_t)raw * 1875UL) / 10000UL);
}

void temp_sensor_ops(void)
{
  uint16_t a0 = ads1115_read(ADS_CH_TEMP);
  uint16_t a1;
  int32_t  den;
  uint32_t rOhm = 0U;

  if (a0 == 0xFFFFU)
  {
    return;   /* I2C 错误：保持上次状态 */
  }

  a1 = ads1115_read(ADS_CH_TEMP_REF);
  if (a1 == 0xFFFFU)
  {
    return;
  }

  tRawA0 = a0;
  tRawA1 = a1;

  den = (2L * (int32_t)a1) - (int32_t)a0;   /* 悬空≈0；插 25℃≈12106 */

  /* 在位判定（去抖：连续 2 次一致才切换，500ms×2=1s 内完成） */
  {
    uint8_t nv = (den >= (int32_t)TEMP_DEN_MIN) ? 1U : 0U;

    if (nv == tValidPend)
    {
      if (tValidCnt < 2U)
      {
        tValidCnt++;
      }
    }
    else
    {
      tValidPend = nv;
      tValidCnt  = 1U;
    }

    if ((tValidCnt >= 2U) && (tValid != tValidPend))
    {
      tValid = tValidPend;
      (void)evt_bus_post((tValid != 0U) ? EVT_TEMP_VALID : EVT_TEMP_INVALID, 0U);
    }
  }

  /* 阻值（比值法，免疫电源波动） + 查表温度；den 未过阈值时不得计算（防负值/除零） */
  if ((tValid != 0U) && (den >= (int32_t)TEMP_DEN_MIN))
  {
    rOhm   = (1000UL * (uint32_t)a0) / (uint32_t)den;
    tDeciC = temp_r_to_t10(rOhm);
  }
  else
  {
    tDeciC = 0;
  }

  /* 调试：持续打印（标定用；拔插/加热观察） */
  if (tLogEn != 0U)
  {
    printf("TEMP: A0=%u (%umV) A1=%u (%umV) den=%d ",
           (unsigned)a0, (unsigned)temp_raw_to_mv(a0),
           (unsigned)a1, (unsigned)temp_raw_to_mv(a1), (int)den);
    if (tValid != 0U)
    {
      printf("Rt=%u ohm T=%d.%dC valid=1\r\n",
             (unsigned)rOhm, (int)(tDeciC / 10), (int)(tDeciC % 10));
    }
    else
    {
      printf("Rt=-- T=-- valid=0\r\n");
    }
  }
}

int16_t temp_sensor_get_deci_c(void)
{
  return tDeciC;
}

uint8_t temp_sensor_is_valid(void)
{
  return tValid;
}
