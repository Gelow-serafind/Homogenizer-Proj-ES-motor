/*******************************************************************************
* @filename:  motor.c
* @brief:     电机控制模块实现（原 TIM1_PWM_Config/Task_PWM/ramp/LUT 迁移，行为等价）
* @note        1. 0% 占空比 = 电机停；LUT 值 = duty% × 10，线性插值
*              2. 斜坡每 10ms 步进 RPM_RAMP_STEP（5ms 任务内部 2 分频，保持原时序）
*              3. MOTOR_DUTY_CLAMP=1 强制 0%（防护）；当前=0 且 LUT 为验证映射（2026-09-15）
*              4. 速度环（2026-09-15）：FF(LUT 前馈) + PI 反馈，20ms 节拍，Q8 定点；
*                 反馈经订阅 EVT_SPEED_UPDATED 缓存（motor_on_event），无模块间直调；
*                 参考轨迹=原斜坡 outRpm（启动无阶跃；STOP 后重启动时按实测转速复位，
*                 2026-09-17 修复大电流冲击）；
*                 输出限幅 SPEED_LOOP_DUTY_MAX（兼堵转保护）；SPEED_LOOP_EN=0 回退开环
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "motor.h"

/* 【验证映射 2026-09-15】简单线性表（非标定值）：先行验证电机转动/方向/驱动链；
   真实标定后回填实测数据（P8/R7）。LUT 值 = duty% × 10，线性插值 */
static const uint16_t RpmDutyLUT[][2] = {
  { 0,         0  },   /* 0% */
  { 100,     150  },   /* 15%：最低档 */
  { 1000,    220  },   /* 22% */
  { 2000,    300  }    /* 30%：最高档 */
};
#define RPM_DUTY_LUT_ENTRIES (sizeof(RpmDutyLUT) / sizeof(RpmDutyLUT[0]))

static uint16_t pwmPeriod   = 0U;      /* ARR 值 */
static uint16_t targetRpm   = 0U;      /* 闭环目标（RUNNING 时 = cfg_rpm） */
static uint16_t outRpm      = 0U;      /* 参考轨迹（ramp，1000 RPM/s） */
static uint8_t  enabled     = 0U;      /* 1=RUNNING 使能输出 */
static uint8_t  dutyDirty   = 1U;      /* 占空比待刷新（开环回退路径） */
static uint16_t dutyPermille = 0U;     /* 当前已应用占空比 */
static uint8_t  manualMode  = 0U;      /* 手动标定模式（不覆盖 CCR） */
static uint8_t  rampDiv     = 0U;      /* 5ms → 10ms 分频 */

/* 速度环状态（SPEED_LOOP_EN=1；主循环独占，无 ISR 共享） */
static uint8_t  pidDiv      = 0U;      /* 5ms → 20ms PID 节拍分频 */
static uint16_t measRpm     = 0U;      /* 反馈缓存（motor_on_event 刷新） */
static int32_t  iAcc        = 0L;      /* 积分累加器（Q8；±I_MAX<<8 限幅） */
static uint16_t kpQ8        = SPEED_LOOP_KP_Q8;   /* P 系数（Q8，可经 shell 整定） */
static uint16_t kiQ8        = SPEED_LOOP_KI_Q8;   /* I 系数（Q8） */
static int32_t  lastFF      = 0L;      /* 最近 FF/P/I 分量（‰，shell pid 诊断） */
static int32_t  lastP       = 0L;
static int32_t  lastI       = 0L;

/* static 前置原型 */
static void     motor_tim1_config(void);
static void     motor_set_duty(uint16_t permille);
static uint8_t  motor_rpm_to_duty_percent(uint16_t rpm);
static void     motor_ramp_step(void);
static void     motor_speed_loop_tick(void);
#if (SPEED_LOOP_EN == 0U)
static uint16_t motor_calc_duty(void);
#endif

/**
  * @brief  TIME 基与 PWM 输出配置（PD6 = TIM1_CH2 AF3，10kHz）
  * @note   严禁恢复旧板 IOMUX 重映射（会毁掉 SWCLK，见 CLAUDE.md 第 2 节）
  */
static void motor_tim1_config(void)
{
  GPIO_InitTypeDef        GPIO_InitStructure;
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_OCInitTypeDef       TIM_OCInitStructure;

  RCC_APBPeriph2ClockCmd(RCC_APBPeriph2_TIM1, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);

  /* PD6 复用推挽（TIM1_CH2 原生引脚，无需 IOMUX） */
  GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  GPIO_PinAFConfig(GPIOD, GPIO_PinSource6, GPIO_AF_3);

  /* 10kHz 时基 */
  pwmPeriod = (uint16_t)((SystemCoreClock / PWM_TARGET_FREQ_HZ) - 1U);
  TIM_TimeBaseStructure.TIM_Period            = pwmPeriod;
  TIM_TimeBaseStructure.TIM_Prescaler         = 0;
  TIM_TimeBaseStructure.TIM_ClockDivision     = 0;
  TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

  /* PWM 直出模式：PWM1 + PolarityHigh，CNT<CCR→HIGH=通电，占空比=CCR/ARR */
  TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;
  TIM_OCInitStructure.TIM_Pulse       = 0;   /* 初始 0% */
  TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
  TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;
  TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
  TIM_OC2Init(TIM1, &TIM_OCInitStructure);

  TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
  TIM_ARRPreloadConfig(TIM1, ENABLE);

  TIM_Cmd(TIM1, ENABLE);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

/**
  * @brief  按千分比设置 TIM1 CH2 占空比（0..1000）
  */
static void motor_set_duty(uint16_t permille)
{
  uint32_t compare;

  if (permille >= 1000U)
  {
    compare = (uint32_t)pwmPeriod + 1U;   /* CCR > ARR：恒高 */
  }
  else
  {
    compare = ((uint32_t)(pwmPeriod + 1U) * permille) / 1000U;
  }

  TIM_SetCompare2(TIM1, (uint16_t)compare);
  TIM_GenerateEvent(TIM1, TIM_EventSource_Update);   /* 预装载下立即生效 */
}

/**
  * @brief  RPM → 占空比(%) 查表线性插值（表值 ×10，插值后四舍五入）
  */
static uint8_t motor_rpm_to_duty_percent(uint16_t rpm)
{
  uint8_t i;

  if (rpm <= RpmDutyLUT[0][0])
  {
    return (uint8_t)((RpmDutyLUT[0][1] + 5U) / 10U);
  }

  for (i = 1; i < RPM_DUTY_LUT_ENTRIES; i++)
  {
    if (rpm <= RpmDutyLUT[i][0])
    {
      uint16_t rLo = RpmDutyLUT[i - 1][0];
      uint16_t rHi = RpmDutyLUT[i][0];
      uint16_t dLo = RpmDutyLUT[i - 1][1];   /* ×10 */
      uint16_t dHi = RpmDutyLUT[i][1];       /* ×10 */

      if (rHi == rLo)
      {
        return (uint8_t)((dLo + 5U) / 10U);
      }
      {
        uint16_t dutyX10 = dLo + (uint16_t)(((uint32_t)(dHi - dLo) * (uint32_t)(rpm - rLo)) / (rHi - rLo));
        return (uint8_t)((dutyX10 + 5U) / 10U);
      }
    }
  }

  return (uint8_t)((RpmDutyLUT[RPM_DUTY_LUT_ENTRIES - 1][1] + 5U) / 10U);
}

/**
  * @brief  计算当前应输出的占空比（千分比）——开环回退路径（SPEED_LOOP_EN=0）
  * @note   MOTOR_DUTY_CLAMP=1 时恒 0（标定前电机不动作）
  */
#if (SPEED_LOOP_EN == 0U)
static uint16_t motor_calc_duty(void)
{
  if (MOTOR_DUTY_CLAMP != 0U)
  {
    return PWM_INACTIVE_PERMILLE;
  }

  if (enabled != 0U)
  {
    return (uint16_t)motor_rpm_to_duty_percent(outRpm) * 10U;
  }
  return PWM_INACTIVE_PERMILLE;
}
#endif

/**
  * @brief  斜坡推进：10ms 步进 RPM_RAMP_STEP（原 Task_App 行为）
  */
static void motor_ramp_step(void)
{
  uint16_t tgt = (enabled != 0U) ? targetRpm : 0U;

  if (outRpm == tgt)
  {
    return;
  }

  if (outRpm < tgt)
  {
    outRpm += RPM_RAMP_STEP;
    if (outRpm > tgt)
    {
      outRpm = tgt;
    }
  }
  else
  {
    if (outRpm > RPM_RAMP_STEP)
    {
      outRpm -= RPM_RAMP_STEP;
    }
    else
    {
      outRpm = 0U;
    }
    if (outRpm < tgt)
    {
      outRpm = tgt;
    }
  }
  dutyDirty = 1U;
}

/**
  * @brief  速度环单拍（20ms）：FF(LUT) + PI，限幅输出
  * @note   err = 参考轨迹 − 实测；Q8 定点无浮点（Cortex-M0）；
  *         低速反馈断续时缓存值即最新可观测值（EMA 已平滑）；
  *         积分限幅抗饱和；输出限幅兼堵转保护
  */
static void motor_speed_loop_tick(void)
{
  int32_t err;
  int32_t p;
  int32_t i;
  int32_t duty;
  int32_t ff;
  int32_t iMaxQ8 = (int32_t)SPEED_LOOP_I_MAX << 8;

  err = (int32_t)outRpm - (int32_t)measRpm;

  /* P 分量：Q8 系数 × 误差 → ‰ */
  p = ((int32_t)kpQ8 * err) >> 8;

  /* I 分量：Q8 累加 + 限幅（抗饱和） */
  iAcc += (int32_t)kiQ8 * err;
  if (iAcc > iMaxQ8)
  {
    iAcc = iMaxQ8;
  }
  if (iAcc < -iMaxQ8)
  {
    iAcc = -iMaxQ8;
  }
  i = iAcc >> 8;

  /* FF（LUT 前馈，‰）+ P + I → 限幅输出（0~100% 物理边界，无额外人为限制） */
  ff   = (int32_t)motor_rpm_to_duty_percent(outRpm) * 10;
  duty = ff + p + i;
  if (duty < 0)
  {
    duty = 0;
  }
  if (duty > (int32_t)SPEED_LOOP_DUTY_MAX)
  {
    duty = (int32_t)SPEED_LOOP_DUTY_MAX;
  }

  lastFF = ff;
  lastP  = p;
  lastI  = i;

  if ((uint16_t)duty != dutyPermille)
  {
    dutyPermille = (uint16_t)duty;
    motor_set_duty(dutyPermille);
  }
}

void motor_init(void)
{
  targetRpm    = 0U;
  outRpm       = 0U;
  enabled      = 0U;
  dutyDirty    = 1U;
  dutyPermille = 0U;
  manualMode   = 0U;
  rampDiv      = 0U;
  pidDiv       = 0U;
  measRpm      = 0U;
  iAcc         = 0L;
  kpQ8         = SPEED_LOOP_KP_Q8;
  kiQ8         = SPEED_LOOP_KI_Q8;
  lastFF       = 0L;
  lastP        = 0L;
  lastI        = 0L;

  motor_tim1_config();
  motor_set_duty(PWM_INACTIVE_PERMILLE);

  /* 订阅实测转速：motor_on_event 只缓存（回调只读纪律） */
  (void)evt_bus_subscribe(EVT_MOD_MOTOR, EVT_SPEED_UPDATED);
}

void motor_ops(void)
{
  /* 5ms 任务内部 2 分频：参考轨迹保持原 10ms 步进节奏 */
  rampDiv++;
  if (rampDiv >= 2U)
  {
    rampDiv = 0U;
    motor_ramp_step();
  }

  if (manualMode != 0U)
  {
    return;   /* 手动标定：不覆盖 CCR（motor_manual_off 恢复自动） */
  }

#if (SPEED_LOOP_EN != 0U)
  if (enabled == 0U)
  {
    /* 停机：输出立即归零 + 积分清零（不依赖 20ms 节拍） */
    if (dutyPermille != PWM_INACTIVE_PERMILLE)
    {
      dutyPermille = PWM_INACTIVE_PERMILLE;
      motor_set_duty(dutyPermille);
    }
    iAcc   = 0L;
    lastFF = 0L;
    lastP  = 0L;
    lastI  = 0L;
    pidDiv = 0U;
    return;
  }

  /* 5ms → 20ms 分频：PID 与测速反馈同频 */
  pidDiv++;
  if (pidDiv >= SPEED_LOOP_PERIOD_DIV)
  {
    pidDiv = 0U;
    motor_speed_loop_tick();
  }
#else
  /* 开环回退：LUT 直接映射（标定/对比验证） */
  if (dutyDirty != 0U)
  {
    dutyPermille = motor_calc_duty();
    motor_set_duty(dutyPermille);
    dutyDirty = 0U;
  }
#endif
}

void motor_set_target(uint16_t rpm)
{
  /* 2026-09-17 修复（上板 bug：STOP 后立即再 START 大电流猛启动）：
     从停机状态（enabled=0）重新起飞时，参考轨迹改为从实测转速起飞（无阶跃）——
     原实现沿用残留 outRpm（STOP 后由斜坡向 0 衰减，2s 内仍在高位），
     电机已停而参考高位 → FF(LUT)+PI 误差巨大 → 启动冲击。 */
  if (enabled == 0U)
  {
    outRpm = measRpm;
  }

  targetRpm = rpm;
  enabled = 1U;
  pidDiv  = 0U;
  /* 注意：不清积分！A14 运行中 ±10 RPM 实时步进时积分承担着稳态负载补偿，
     清零会导致输出阶跃→转速明显掉速→再慢慢积回（表现为调速时大幅抖动）。
     积分有 SPEED_LOOP_I_MAX 限幅自校正，目标大幅变化时会被误差自然拉回；
     停机（motor_stop/halt）才清零。 */
}

void motor_stop(void)
{
  enabled = 0U;   /* 输出由 ops 立即归零；参考轨迹自然衰减（下次 set_target 按实测复位） */
  iAcc    = 0L;
}

void motor_halt(void)
{
  enabled  = 0U;
  targetRpm = 0U;
  outRpm   = 0U;
  iAcc     = 0L;
  measRpm  = 0U;
  dutyDirty = 1U;
}

uint16_t motor_get_out_rpm(void)
{
  return outRpm;   /* 参考轨迹（ramp）值 */
}

uint16_t motor_get_duty_permille(void)
{
  return dutyPermille;
}

uint16_t motor_get_meas_rpm(void)
{
  return measRpm;
}

/**
  * @brief  订阅回调（EVT_SPEED_UPDATED）：只刷新反馈缓存
  * @note   回调只读纪律：不 post、不长操作；动作由 motor_ops 20ms 节拍消费
  */
void motor_on_event(evt_type_t type, uint16_t val)
{
  (void)type;
  measRpm = val;
}

uint16_t motor_get_kp_q8(void)
{
  return kpQ8;
}

uint16_t motor_get_ki_q8(void)
{
  return kiQ8;
}

void motor_set_kp_q8(uint16_t v)
{
  if (v <= 4095U)
  {
    kpQ8 = v;
  }
}

void motor_set_ki_q8(uint16_t v)
{
  if (v <= 4095U)
  {
    kiQ8 = v;
  }
}

/**
  * @brief  速度环诊断快照（shell pid 命令；纯读）
  */
void motor_pid_get_info(motor_pid_info_t *info)
{
  if (info == 0)
  {
    return;
  }

  info->target_rpm      = targetRpm;
  info->ref_rpm         = outRpm;
  info->meas_rpm        = measRpm;
  info->ff_permille     = (int16_t)lastFF;
  info->p_permille      = (int16_t)lastP;
  info->i_permille      = (int16_t)lastI;
  info->i_acc_permille  = (int16_t)(iAcc >> 8);
  info->duty_permille   = dutyPermille;
}

void motor_manual_duty(uint16_t permille)
{
  if (permille > 1000U)
  {
    permille = 1000U;
  }
  manualMode = 1U;
  dutyPermille = permille;
  motor_set_duty(permille);
}

void motor_manual_off(void)
{
  manualMode = 0U;
  dutyDirty = 1U;   /* 下一周期恢复自动占空比 */
}
