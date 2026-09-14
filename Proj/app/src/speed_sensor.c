/*******************************************************************************
* @filename:  speed_sensor.c
* @brief:     光电测速模块实现（STEP3.5 采样能力模块化；硬件未通，业务暂不采用）
* @note        非阻塞两阶段采样：本次 ops 触发转换，下次 ops 读结果（间隔 20ms ≫ 1.2ms）；
*              窗口满换算 RPM 并发布 EVT_SPEED_UPDATED（供 app_state 到达判定/显示）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ads1115.h"
#include "event_bus.h"
#include "speed_sensor.h"

#define SPEED_WINDOW_MS      200U      /* 计数窗口时长 */
#define SPEED_EDGE_RAW_HIGH  13000U    /* raw > 13000（≈2.5V）判高（STEP3.5 验证值） */

static uint16_t sPpr        = 1U;      /* A10：每转脉冲数（标定后写入） */
static uint16_t sRpm        = 0U;
static uint16_t sPulses     = 0U;      /* 窗口内边沿数 */
static uint16_t sWinMs      = 0U;
static uint8_t  sPrevLevel  = 0xFFU;   /* 0xFF = 首帧（不产生边沿） */
static uint8_t  sPhase      = 0U;      /* 0=触发转换 1=读结果 */

void speed_sensor_init(void)
{
  sPpr       = 1U;
  sRpm       = 0U;
  sPulses    = 0U;
  sWinMs     = 0U;
  sPrevLevel = 0xFFU;
  sPhase     = 0U;
}

void speed_sensor_ops(void)
{
  /* 两阶段采样（非阻塞） */
  if (sPhase == 0U)
  {
    if (ads1115_start(ADS_CH_SPEED) != 0U)
    {
      sPhase = 1U;
    }
  }
  else
  {
    uint16_t v = ads1115_fetch();

    sPhase = 0U;
    if (v != 0xFFFFU)
    {
      uint8_t lvl = (v > SPEED_EDGE_RAW_HIGH) ? 1U : 0U;

      if ((sPrevLevel != 0xFFU) && (lvl != sPrevLevel))
      {
        sPulses++;
      }
      sPrevLevel = lvl;
    }
  }

  /* 计数窗口 */
  sWinMs += TASK_PERIOD_SPEED_MS;
  if (sWinMs >= SPEED_WINDOW_MS)
  {
    uint32_t rpm;

    sWinMs = 0U;
    rpm = ((uint32_t)sPulses * 60000U) / ((uint32_t)sPpr * SPEED_WINDOW_MS);
    if (rpm > 65535U)
    {
      rpm = 65535U;
    }
    sRpm = (uint16_t)rpm;
    sPulses = 0U;

    evt_bus_post(EVT_SPEED_UPDATED, sRpm);
  }
}

uint16_t speed_sensor_get_rpm(void)
{
  return sRpm;
}

void speed_sensor_set_ppr(uint16_t ppr)
{
  if (ppr > 0U)
  {
    sPpr = ppr;
  }
}
