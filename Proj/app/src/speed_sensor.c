/*******************************************************************************
* @filename:  speed_sensor.c
* @brief:     测速模块实现：SPEED_SW 周期法测速（EXTI4 时间戳 + TIM6 µs 时基）
* @note        1. 测量原理（周期法）：EXTI4 记录相邻脉冲间隔（单位 SPEED_TICK_US），
*                  20ms 任务取平均周期换算 RPM = 60e6 / (PPR × 平均周期µs)；
*                  无脉冲超过 SPEED_STALL_TIMEOUT_MS → 转速归零（停转）。
*                  周期法在 100~2000 RPM（PPR=18）全量程精度优于计数法，且输出
*                  定频（20ms）滤波值，供显示/到达判定/未来速度环 PID 共用。
*               2. TIM6 自由运行作时间戳时基（48MHz/SPEED_TICK_US 分频，不开中断）；
*                  周期量程 ≈ SPEED_TICK_US × 65535 ≈ 262ms（≈13 RPM）。
*               3. 硬件注意：PB4 与 USART1_RX 同网络（U15 速度接口）——编码器与
*                  USB-TTL 不可同时接；编码器接入时串口 RX 受扰属正常。
*               4. ISR 仅读时间戳/累加，不发事件（架构 ISR 纪律）。
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "speed_sensor.h"

#define SPEED_PERIOD_MIN_TICKS   5U        /* <20µs（≈16.6 万 RPM）：噪声，不参与平均 */
#define SPEED_PERIOD_MAX_TICKS   50000U    /* >200ms（≈16.7 RPM）：无效（超时逻辑接管） */

static uint16_t sPpr       = SPEED_PPR;    /* 每转脉冲数（A10 实测 18；可经 set_ppr 调整） */
static uint16_t sRpm       = 0U;           /* 滤波后实测转速（对外反馈值） */
static uint16_t sNoPulseMs = 0U;           /* 无脉冲计时（停转判定） */

/* ISR 与主循环共享量（主循环取用时关中断） */
static volatile uint32_t sPulseTotal   = 0U;   /* 累计脉冲数（诊断/标定） */
static volatile uint16_t sLastEdgeTick = 0U;   /* 上一脉冲时间戳（TIM6 tick） */
static volatile uint32_t sPeriodSum    = 0U;   /* 窗口内相邻周期累加（tick） */
static volatile uint16_t sPeriodCnt    = 0U;   /* 窗口内周期个数 */

/* static 前置原型 */
static void speed_sensor_tim6_config(void);
static void speed_sensor_exti_config(void);

/**
  * @brief  TIM6 自由运行时基（每 SPEED_TICK_US µs 一个 tick，不使用中断）
  */
static void speed_sensor_tim6_config(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

  RCC_APBPeriph1ClockCmd(RCC_APBPeriph1_TIM6, ENABLE);

  TIM_TimeBaseStructure.TIM_Period            = 0xFFFFU;
  TIM_TimeBaseStructure.TIM_Prescaler         = (uint16_t)((SystemCoreClock / 1000000U) * (uint32_t)SPEED_TICK_US - 1U);
  TIM_TimeBaseStructure.TIM_ClockDivision     = 0;
  TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);

  TIM_Cmd(TIM6, ENABLE);
}

/**
  * @brief  PB4/EXTI4 边沿捕获配置（PB4 已由 COM1 初始化为 UART1_RX，不重配引脚）
  */
static void speed_sensor_exti_config(void)
{
  EXTI_InitTypeDef EXTI_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

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

void speed_sensor_init(void)
{
  sRpm        = 0U;
  sNoPulseMs  = 0U;
  sPulseTotal = 0U;
  sPeriodSum  = 0U;
  sPeriodCnt  = 0U;

  speed_sensor_tim6_config();
  sLastEdgeTick = (uint16_t)TIM_GetCounter(TIM6);
  speed_sensor_exti_config();
}

void speed_sensor_ops(void)
{
  uint32_t periodSum;
  uint16_t periodCnt;

  /* 原子取走窗口数据 */
  __disable_irq();
  periodSum  = sPeriodSum;
  periodCnt  = sPeriodCnt;
  sPeriodSum = 0U;
  sPeriodCnt = 0U;
  __enable_irq();

  if (periodCnt > 0U)
  {
    uint32_t avgTick = periodSum / periodCnt;               /* 平均脉冲周期（tick） */
    uint32_t rpmRaw  = 60000000UL / ((uint32_t)sPpr * avgTick * (uint32_t)SPEED_TICK_US);

    if (rpmRaw > 65535U)
    {
      rpmRaw = 65535U;
    }

    if (sRpm == 0U)
    {
      sRpm = (uint16_t)rpmRaw;                              /* 起始直接装载，加快响应 */
    }
    else
    {
      /* 一阶低通：α = 1/(1<<SPEED_RPM_EMA_SHIFT)，每次（20ms）更新一次 */
      sRpm = (uint16_t)((int32_t)sRpm + ((int32_t)rpmRaw - (int32_t)sRpm) / (int32_t)(1U << SPEED_RPM_EMA_SHIFT));
    }

    sNoPulseMs = 0U;
    (void)evt_bus_post(EVT_SPEED_UPDATED, sRpm);
  }
  else
  {
    sNoPulseMs = (uint16_t)(sNoPulseMs + TASK_PERIOD_SPEED_MS);
    if ((sNoPulseMs >= SPEED_STALL_TIMEOUT_MS) && (sRpm != 0U))
    {
      sRpm = 0U;                                            /* 停转：超时归零 */
      (void)evt_bus_post(EVT_SPEED_UPDATED, sRpm);
    }
  }
}

/**
  * @brief  EXTI4（PB4：SPEED_SW/USART1_RX 同网络）边沿中断：时间戳 + 累加
  * @note   仅读 TIM6 计数、算术与累加，不投递事件（架构 ISR 纪律）；
  *         无符号回绕安全；异常周期（噪声/过长）不参与平均。
  */
void EXTI4_IRQHandler(void)
{
  if (EXTI_GetITStatus(SPEED_EXTI_LINE) == SET)
  {
    uint16_t now    = (uint16_t)TIM_GetCounter(TIM6);
    uint16_t period = (uint16_t)(now - sLastEdgeTick);

    sLastEdgeTick = now;
    sPulseTotal++;

    if ((period >= SPEED_PERIOD_MIN_TICKS) && (period <= SPEED_PERIOD_MAX_TICKS))
    {
      sPeriodSum += period;
      sPeriodCnt++;
    }

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
