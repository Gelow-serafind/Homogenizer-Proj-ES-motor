/*******************************************************************************
* @filename:  speed_sensor.c
* @brief:     测速模块实现：SPEED_SW 脉冲计数 / RPM 换算（PB4 + EXTI4）
* @note        1. 链路已验证：SPEED_SW（U15 速度接口）与 USART1_RX 同网络 → PB4
*                  （图纸核实 2026-09-14；上板验证通）；计数标定 18 计数/圈（2026-09-15）。
*               2. 编码器与 USB-TTL 不可同时接（双驱动打架）；编码器接入时串口 RX
*                  受扰属正常。
*               3. 计数约定：下降沿，1 脉冲 = 1 计数（ISR 仅自增，不发事件）。
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "speed_sensor.h"

static uint16_t sPpr   = SPEED_PPR;        /* 每转脉冲数（A10 实测 18；可经 set_ppr 调整） */
static uint16_t sRpm   = 0U;
static uint16_t sWinMs = 0U;

static volatile uint32_t sPulseTotal = 0U; /* 累计脉冲数（ISR 写；标定/调试可读） */
static uint32_t sLastSnap = 0U;            /* 上次窗口快照（仅主循环用） */

void speed_sensor_init(void)
{
  EXTI_InitTypeDef EXTI_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  sRpm        = 0U;
  sWinMs      = 0U;
  sPulseTotal = 0U;
  sLastSnap   = 0U;

  /* PB4 已由 COM1 初始化为 UART1_RX——不重配引脚，EXTI 直接挂同一引脚 */
  RCC_APBPeriph2ClockCmd(RCC_APBPeriph2_SYSCFG, ENABLE);
  SYSCFG_EXTILineConfig(SPEED_EXTI_PORT_SOURCE, SPEED_EXTI_PIN_SOURCE);

  EXTI_InitStructure.EXTI_Line    = SPEED_EXTI_LINE;
  EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;   /* 单边沿=脉冲计数；信号极性相反则改 Rising */
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel         = SPEED_EXTI_IRQN;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0U;
  NVIC_InitStructure.NVIC_IRQChannelCmd      = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  EXTI_ClearITPendingBit(SPEED_EXTI_LINE);
}

void speed_sensor_ops(void)
{
  sWinMs = (uint16_t)(sWinMs + TASK_PERIOD_SPEED_MS);

  if (sWinMs >= SPEED_WINDOW_MS)
  {
    uint32_t now;
    uint32_t n;
    uint32_t rpm;

    sWinMs = 0U;

    now       = sPulseTotal;     /* 32 位对齐读原子；快照差值法，无需清计数 */
    n         = now - sLastSnap;
    sLastSnap = now;

    rpm = (n * 60000U) / ((uint32_t)sPpr * SPEED_WINDOW_MS);
    if (rpm > 65535U)
    {
      rpm = 65535U;
    }
    sRpm = (uint16_t)rpm;

    (void)evt_bus_post(EVT_SPEED_UPDATED, sRpm);
  }
}

/**
  * @brief  EXTI4（PB4：SPEED_SW/USART1_RX 同网络）边沿中断：只计数
  * @note   仅自增 + 清标志，不投递事件（架构 ISR 纪律）
  */
void EXTI4_IRQHandler(void)
{
  if (EXTI_GetITStatus(SPEED_EXTI_LINE) == SET)
  {
    sPulseTotal++;
    EXTI_ClearITPendingBit(SPEED_EXTI_LINE);
  }
}

uint16_t speed_sensor_get_rpm(void)
{
  return sRpm;
}

/**
  * @brief  读取累计脉冲数（标定/调试；上电清零）
  */
uint32_t speed_sensor_get_pulses(void)
{
  return sPulseTotal;
}

void speed_sensor_set_ppr(uint16_t ppr)
{
  if (ppr > 0U)
  {
    sPpr = ppr;
  }
}
