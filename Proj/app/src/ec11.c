/*******************************************************************************
* @filename:  ec11.c
* @brief:     EC11 正交解码实现：EXTI 双边沿硬件捕获（默认）/ 2ms 轮询回退
* @note        1. EC11_USE_EXTI=1：PC5=EXTI5、PC3=EXTI3 任一相位双边沿触发；
*                  ISR 只做"读 A/B → 查表 → 累加 1/4 格"，不发事件（架构 ISR 纪律）；
*              2. 主循环经 ec11_take_delta() 以 ÷4 换取整格，余量保留（反转可抵消）；
*                  边沿零丢失，与主循环卡顿解耦；
*              3. EC11_USE_EXTI=0：回退旧版 2ms 采样解码（行为等价）；
*              4. ISR 与主循环共享量用 volatile；取值处关中断保证原子。
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ec11.h"

#define EC11_QUARTER_SAT  480   /* 1/4 格累计饱和：±480 = ±120 格（4 边沿/格） */

/* 状态转移表：idx = (prevAB<<2)|currAB，值 = 单个边沿的方向（+1/-1/0 无效） */
static const int8_t ec11Transition[16] =
{
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

static volatile uint8_t  ec11PrevAB    = 0x03U;
static volatile int32_t  ec11Quarters  = 0;     /* 1/4 格累计（ISR 写，主循环取整格） */
static volatile uint32_t ec11IsrCount  = 0U;    /* 诊断：EXTI 触发次数 */
static volatile uint32_t ec11EdgeCount = 0U;    /* 诊断：有效边沿数 */
static int32_t           ec11DetentNet = 0;     /* 诊断：净格数（主循环写） */

/* static 前置原型 */
static void ec11_capture(void);

/**
  * @brief  单次状态捕获：读 A/B → 查表 → 累加 1/4 格
  * @note   ISR 上下文调用：仅 GPIO 读 + 查表 + 加法，不投递事件
  */
static void ec11_capture(void)
{
  uint8_t a = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_A) != Bit_RESET) ? 1U : 0U;
  uint8_t b = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_B) != Bit_RESET) ? 1U : 0U;
  uint8_t currAB = (uint8_t)((a << 1) | b);
  int8_t  delta  = ec11Transition[(uint8_t)((ec11PrevAB << 2) | currAB)];

  ec11PrevAB = currAB;

  if (delta != 0)
  {
    int32_t q = ec11Quarters + delta;

    if (q > EC11_QUARTER_SAT)
    {
      q = EC11_QUARTER_SAT;
    }
    else if (q < -EC11_QUARTER_SAT)
    {
      q = -EC11_QUARTER_SAT;
    }
    ec11Quarters = q;
    ec11EdgeCount++;
  }
}

#if (EC11_USE_EXTI != 0U)
/* ====================== EXTI 硬件捕获配置 ====================== */

static void ec11_exti_init(void)
{
  EXTI_InitTypeDef EXTI_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APBPeriph2ClockCmd(RCC_APBPeriph2_SYSCFG, ENABLE);          /* SYSCFG：EXTI 端口选路 */
  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource5);   /* PC5 → EXTI5（A 相） */
  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource3);   /* PC3 → EXTI3（B 相） */

  EXTI_InitStructure.EXTI_Line    = EXTI_Line5 | EXTI_Line3;
  EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel         = EXTI3_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0U;
  NVIC_InitStructure.NVIC_IRQChannelCmd      = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI5_IRQn;
  NVIC_Init(&NVIC_InitStructure);

  EXTI_ClearITPendingBit(EXTI_Line3);   /* 清上电残留标志 */
  EXTI_ClearITPendingBit(EXTI_Line5);
}
#endif /* EC11_USE_EXTI */

void ec11_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  uint8_t a;
  uint8_t b;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

  /* A/B 相：PC5/PC3，高有效（外部 10k 下拉），下拉输入 */
  GPIO_InitStructure.GPIO_Pin   = EC11_PIN_A | EC11_PIN_B;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);

  /* 解码初态取实际引脚（静止时外部下拉 = 00，避免首边沿虚假跳变） */
  a = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_A) != Bit_RESET) ? 1U : 0U;
  b = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_B) != Bit_RESET) ? 1U : 0U;
  ec11PrevAB    = (uint8_t)((a << 1) | b);
  ec11Quarters  = 0;
  ec11IsrCount  = 0U;
  ec11EdgeCount = 0U;
  ec11DetentNet = 0;

#if (EC11_USE_EXTI != 0U)
  ec11_exti_init();
#endif
}

void ec11_ops(void)
{
#if (EC11_USE_EXTI == 0U)
  ec11_capture();     /* 轮询回退：2ms 采样一次 */
#endif
}

int32_t ec11_take_delta(void)
{
  int32_t q;
  int32_t d;

  __disable_irq();
  q = ec11Quarters;
  d = q / 4;                       /* C99 截断除法：余量（±3 个 1/4 格）保留 */
  ec11Quarters = q - (d * 4);
  __enable_irq();

  ec11DetentNet += d;              /* 诊断统计（主循环上下文） */
  return d;
}

void ec11_take_stats(ec11_stats_t* st)
{
  __disable_irq();
  st->isrCount  = ec11IsrCount;
  st->edgeCount = ec11EdgeCount;
  st->detentNet = ec11DetentNet;
  ec11IsrCount  = 0U;
  ec11EdgeCount = 0U;
  ec11DetentNet = 0;
  __enable_irq();
}

#if (EC11_USE_EXTI != 0U)
/* ====================== EXTI ISR（覆盖 startup 弱符号） ====================== */

/**
  * @brief  EXTI3（PC3/B 相）边沿中断：捕获一次状态跳变
  * @note   仅 GPIO 读 + 查表 + 计数；禁止投递事件/长操作（架构 ISR 纪律）
  */
void EXTI3_IRQHandler(void)
{
  if (EXTI_GetITStatus(EXTI_Line3) == SET)
  {
    ec11IsrCount++;
    ec11_capture();
    EXTI_ClearITPendingBit(EXTI_Line3);
  }
}

/**
  * @brief  EXTI5（PC5/A 相）边沿中断：捕获一次状态跳变
  * @note   与 EXTI3 同优先级，不会互相嵌套（Cortex-M0 同优先级不抢占）
  */
void EXTI5_IRQHandler(void)
{
  if (EXTI_GetITStatus(EXTI_Line5) == SET)
  {
    ec11IsrCount++;
    ec11_capture();
    EXTI_ClearITPendingBit(EXTI_Line5);
  }
}
#endif /* EC11_USE_EXTI */
