/*******************************************************************************
* @filename:  temp_sensor.c
* @brief:     PT1000 测温模块：比值法双通道采样 + 查表换算（2026-09-15 实装）
* @note        电路（原理图核实 2026-09-15）：VCC_5V —R16(1k)— ADC_PT1000+（CN3.1）
*              — 探头 — GND；AIN0 测探头节点，AIN1 测 R19/R18(1k:1k) 基准中点。
*              比值法（免疫电源波动，无需知道 VCC）：R_t = 1000 × A0/(2×A1 − A0)，
*              A0/A1 = 两通道原始码；拔/插由 2×A1−A0 判定（悬空≈0，插 25℃≈12106）。
*              温度：PT1000 分度表（0~300℃ 每 5℃，A=3.862e-3 老分度）+ 线性插值 → ℃×10。
*              采样：阻塞单次转换 2 路 ≈20ms / 500ms 周期，占用可接受。
*              滤波：8 点滑动平均（500ms×8=4s 窗，TEMP_AVG_WINDOW，2026-10-01 平滑优先），
*              抑制 ADS 采样噪声防显示跳字；插/拔切换清窗，插入首拍满窗装载快速响应。
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

/* 均值滤波（2026-10-01）：环形窗 + 求和，平均用移位（TEMP_AVG_WINDOW=8=2^3） */
#define TEMP_AVG_SHIFT   3U
static int16_t  tAvgBuf[TEMP_AVG_WINDOW];  /* 滑动窗（℃×10） */
static uint8_t  tAvgIdx  = 0U;             /* 下一写入位置 */
static uint8_t  tAvgFill = 0U;             /* 已填充计数（0=空：刚插入/拔出） */
static int32_t  tAvgSum  = 0;              /* 窗内求和（免每拍重算） */

/* PT1000 分度表：R×10（Ω），0~300℃ 每 5℃；
   由 R(t)=1000×(1+3.862e-3·t−5.775e-7·t²) 生成（A=3.862e-3 老分度，
   2026-10-01 按参考项目 magnetic_stir_heat_v5.0 系数统一；原 IEC 60751
   A=3.9083e-3 已弃用——两者 100℃ 差 ≈1.2℃、300℃ ≈3.5℃；
   最终口径以探头标定单点验证为准） */
static const uint16_t RTableX10[61] = {
  10000U, 10193U, 10386U, 10578U, 10770U, 10962U, 11153U, 11345U,
  11536U, 11726U, 11917U, 12107U, 12296U, 12486U, 12675U, 12864U,
  13053U, 13241U, 13429U, 13617U, 13804U, 13991U, 14178U, 14365U,
  14551U, 14737U, 14923U, 15108U, 15294U, 15478U, 15663U, 15847U,
  16031U, 16215U, 16399U, 16582U, 16764U, 16947U, 17129U, 17311U,
  17493U, 17674U, 17856U, 18036U, 18217U, 18397U, 18577U, 18757U,
  18936U, 19115U, 19294U, 19473U, 19651U, 19829U, 20006U, 20184U,
  20361U, 20538U, 20714U, 20890U, 21066U,
};

/* static 前置原型 */
static int16_t  temp_r_to_t10(uint32_t rOhm);
static uint16_t temp_raw_to_mv(uint16_t raw);
static int16_t  temp_calibrate(int16_t t10);
static void     temp_avg_reset(void);
static void     temp_avg_push(int16_t t10);

void temp_sensor_init(void)
{
  tRawA0 = 0xFFFFU;
  tRawA1 = 0xFFFFU;
  tValid = 0U;
  tDeciC = 0;
  tLogEn = 0U;
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
  if (rOhm > 2106U)
  {
    return 3000;       /* >300℃：clamp 300.0（A=3.862e-3 分度） */
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
  * @brief  线性标定修正：T_cal = T_raw × TEMP_CAL_GAIN_PPT / 1000 + TEMP_CAL_OFFSET_T10
  * @note   标定参数集中在 app_cfg.h（2026-10-01 新增，默认 1000/0=无修正）：
  *         增益千分比（实测系统性偏高 a% → 1000−10a；偏低 → 1000+10a）；
  *         偏移 0.1℃ 可负（整体低 b℃ → +10b；高 → −10b）。
  *         无需再重新生成分度表；输出 clamp 0~3000 与分度表范围一致。
  */
static int16_t temp_calibrate(int16_t t10)
{
  int32_t v = ((int32_t)t10 * (int32_t)TEMP_CAL_GAIN_PPT) / 1000L
              + (int32_t)TEMP_CAL_OFFSET_T10;

  if (v < 0)
  {
    v = 0;
  }
  else if (v > 3000)
  {
    v = 3000;
  }
  return (int16_t)v;
}

/**
  * @brief  原始码 → 等效电压 mV（单端、PGA=±6.144V：1LSB=187.5µV，mV=raw×1875/10000）
  */
static uint16_t temp_raw_to_mv(uint16_t raw)
{
  return (uint16_t)(((uint32_t)raw * 1875UL) / 10000UL);
}

/**
  * @brief  均值滤波窗重置（插/拔切换时调用：防旧探头值污染新状态）
  */
static void temp_avg_reset(void)
{
  tAvgIdx  = 0U;
  tAvgFill = 0U;
  tAvgSum  = 0;
}

/**
  * @brief  推入一个采样并维护窗平均（首次满窗装载：插入后立即出值，不等窗满）
  * @note   TEMP_AVG_WINDOW 必须为 2 的幂，平均用移位（M0 无除法器）；
  *         改 WINDOW 需同步 TEMP_AVG_SHIFT
  */
static void temp_avg_push(int16_t t10)
{
  if (tAvgFill < TEMP_AVG_WINDOW)
  {
    uint8_t i;
    for (i = 0U; i < TEMP_AVG_WINDOW; i++)
    {
      tAvgBuf[i] = t10;
    }
    tAvgSum  = (int32_t)t10 * (int32_t)TEMP_AVG_WINDOW;
    tAvgFill = TEMP_AVG_WINDOW;
  }
  else
  {
    tAvgSum -= tAvgBuf[tAvgIdx];
    tAvgBuf[tAvgIdx] = t10;
    tAvgSum += tAvgBuf[tAvgIdx];
    tAvgIdx++;
    if (tAvgIdx >= TEMP_AVG_WINDOW)
    {
      tAvgIdx = 0U;
    }
  }
}

void temp_sensor_ops(void)
{
  uint16_t a0 = ads1115_read(ADS_CH_TEMP);
  uint16_t a1;
  int32_t  den;
  uint32_t rOhm = 0U;
  int16_t  t10Raw = 0;

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

  /* 在位判定（直接切换：拔出/插入 den 差异巨大（−8 vs 12106）无噪声误判风险；
     拔插联动 ≤ 采样周期+显示刷新 ≈ 550ms，满足 REQ-TEMP-03 ≤1s） */
  {
    uint8_t nv = (den >= (int32_t)TEMP_DEN_MIN) ? 1U : 0U;

    if (nv != tValid)
    {
      tValid = nv;
      temp_avg_reset();   /* 插/拔切换：清滤波窗（防旧探头值污染，2026-10-01） */
      (void)evt_bus_post((tValid != 0U) ? EVT_TEMP_VALID : EVT_TEMP_INVALID, 0U);
    }
  }

  /* 阻值（比值法，免疫电源波动） + 查表温度 + 线性标定；den 未过阈值时不得计算（防负值/除零） */
  if ((tValid != 0U) && (den >= (int32_t)TEMP_DEN_MIN))
  {
    rOhm   = (1000UL * (uint32_t)a0) / (uint32_t)den;
    t10Raw = temp_calibrate(temp_r_to_t10(rOhm));
  }
  else
  {
    t10Raw = 0;
  }

  /* 均值滤波输出（滑动平均，500ms×TEMP_AVG_WINDOW 窗；抑制 ADS 采样噪声防显示
     跳字；仅有效时更新，无效直接 0；插入首拍满窗装载快速响应，2026-10-01） */
  if (tValid != 0U)
  {
    temp_avg_push(t10Raw);
    tDeciC = (int16_t)(tAvgSum >> TEMP_AVG_SHIFT);
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
      printf("Rt=%u ohm Traw=%d.%d Tavg=%d.%d valid=1\r\n",
             (unsigned)rOhm,
             (int)(t10Raw / 10), (int)(t10Raw % 10),
             (int)(tDeciC / 10), (int)(tDeciC % 10));
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
