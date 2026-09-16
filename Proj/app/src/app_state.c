/*******************************************************************************
* @filename:  app_state.c
* @brief:     主状态机实现（原 Task_App/AppStart/AppStop/ApplyKnobDelta 迁移，行为等价）
* @note        1. 事件回调只置标志，动作在 app_state_ops 消费（回调只读纪律）；
*              2. 白名单直调：motor_set_target/motor_stop/timer_* /storage_request_save；
*              3. 到达判定（2026-09-15 实装）：实测转速 |实际−目标| ≤ REACH_TOLERANCE_RPM
*                 持续 REACH_HOLD_MS → post(EVT_REACH_TARGET)（数据源 = speed_sensor）。
*                 2026-09-17 起该事件仅作信息用途（无消费方）：倒计时改为 START 立即开始
*                 （app_do_start 内 timer_resume），不再等待到达（用户实测决策）。
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "storage.h"
#include "motor.h"
#include "speed_sensor.h"
#include "timer.h"
#include "app_state.h"

static app_mode_t   appMode = APP_STANDBY;
static edit_field_t appEdit = EDIT_NONE;

/* 权威参数（cfg_*） */
static uint16_t cfgRpm = 0U;
static uint16_t cfgH   = 0U;
static uint8_t  cfgM   = 0U;
static uint8_t  cfgS   = 0U;

/* 编辑层状态 */
static uint16_t idleMs     = 0U;   /* 编辑无操作计时 */
static uint8_t  timeEdited = 0U;   /* 本次编辑会话是否改过时间（A11 重载依据） */

/* 运行辅助 */
static uint8_t  reached    = 0U;   /* 已发布到达事件（对应原 RemActive 启动） */
static uint16_t reachHoldMs = 0U;  /* A12：误差入容差累计时间（持续 ≥1s 判定到达） */
static uint16_t blinkMs    = 0U;   /* 500ms 闪烁相位计数 */
static uint8_t  blinkPhase = 0U;

/* 事件接收标志（回调置位，ops 消费） */
static uint8_t evStart = 0U;
static uint8_t evStop  = 0U;
static uint8_t evPress = 0U;
static uint8_t evCw    = 0U;
static uint8_t evCcw   = 0U;
static uint8_t evZero  = 0U;

/* static 前置原型 */
static void app_commit(void);
static void app_enter_edit(void);
static void app_do_start(void);
static void app_do_stop(void);
static void app_apply_knob(int8_t dir);

/* ====================== 内部动作 ====================== */

/**
  * @brief  将当前设定参数写入 Flash（设置确认 / 启动 / 停止 / shell 命令时调用）
  */
static void app_commit(void)
{
  user_param_t p;

  p.rpm = cfgRpm;
  p.h   = cfgH;
  p.m   = cfgM;
  p.s   = cfgS;

  (void)storage_request_save(&p);      /* 入队，storage_ops 10ms 内分步落盘 */
  (void)evt_bus_post(EVT_PARAM_CONFIRMED, cfgRpm);

  /* 运行中改过时间 → 倒计时按新值重载并从新值立即继续递减（REQ-TIMER-03，2026-09-17）；
     时间为 0:00:00 时仅装载不递减（无计时作业）。timeEdited 用掉即清，避免后续
     确认（如改速度的 5s 超时）误触发重复重载 */
  if ((appMode == APP_RUNNING) && (timeEdited != 0U))
  {
    timer_load(cfgH, cfgM, cfgS);
    if (((uint16_t)(cfgH | cfgM | cfgS)) != 0U)
    {
      timer_resume();
    }
    timeEdited = 0U;
  }
}

/**
  * @brief  进入设置叠加层（任一非设置状态单击旋钮触发）
  */
static void app_enter_edit(void)
{
  if (appEdit != EDIT_NONE)
  {
    return;
  }
  appEdit    = EDIT_SPEED;
  idleMs     = 0U;
  timeEdited = 0U;
}

/**
  * @brief  开始/继续运行：编辑确认并退出 → RUNNING，装载或续用倒计时
  */
static void app_do_start(void)
{
  if (appEdit != EDIT_NONE)      /* 设置中按开始：先确认写入并退出设置 */
  {
    app_commit();
    appEdit = EDIT_NONE;
    idleMs  = 0U;
  }
  if (appMode == APP_RUNNING)
  {
    return;
  }

  if (cfgRpm == 0U)
  {
    cfgRpm = RPM_MIN;            /* 无设定速度时给最小转速 */
  }

  if (appMode != APP_PAUSED)
  {
    /* 全新启动：倒计时从设定时间装载 */
    timer_load(cfgH, cfgM, cfgS);
  }
  else if (timeEdited != 0U)
  {
    /* 暂停后改过时间：按新设定重载倒计时（A11） */
    timer_load(cfgH, cfgM, cfgS);
  }
  /* 否则：PAUSED 未改时间 → 保留冻结的倒计时继续（timer 侧不动） */
  timeEdited = 0U;               /* 编辑会话已消费（防运行中确认误触发重载） */

  /* 2026-09-17 用户决策：按 START 立即开始/继续递减（取代"到达目标后才倒数"的
     A12 旧语义）；时间为 0:00:00 时不倒数（无计时作业，也不触发归零） */
  if (((uint16_t)(cfgH | cfgM | cfgS)) != 0U)
  {
    timer_resume();
  }

  reached = 0U;                  /* 到达判定保留：仅发布 EVT_REACH_TARGET（当前无消费方） */
  reachHoldMs = 0U;
  appMode = APP_RUNNING;
  motor_set_target(cfgRpm);      /* 白名单直调 */
  (void)evt_bus_post(EVT_MODE_CHANGED, (uint16_t)appMode);
}

/**
  * @brief  停止/暂停：RUNNING→PAUSED（倒计时冻结）；设置中则确认并退出
  */
static void app_do_stop(void)
{
  if (appEdit != EDIT_NONE)
  {
    app_commit();
    appEdit = EDIT_NONE;
    idleMs  = 0U;
  }
  if (appMode == APP_RUNNING)
  {
    appMode = APP_PAUSED;
    reached = 0U;                /* 到达判定复位（信息事件用） */
    reachHoldMs = 0U;
    timer_pause();               /* 倒计时冻结保持（START 后从冻结值继续/立即倒数） */
    motor_stop();
    (void)evt_bus_post(EVT_MODE_CHANGED, (uint16_t)appMode);
  }
}

/**
  * @brief  按当前编辑字段应用一次旋钮增量（单位步进）
  * @param  dir: +1 增加 / -1 减少
  */
static void app_apply_knob(int8_t dir)
{
  switch (appEdit)
  {
    case EDIT_SPEED:
      if (dir > 0)
      {
        cfgRpm += RPM_STEP;
        if (cfgRpm > RPM_MAX)
        {
          cfgRpm = RPM_MAX;
        }
      }
      else
      {
        if (cfgRpm > RPM_MIN + RPM_STEP)
        {
          cfgRpm -= RPM_STEP;
        }
        else
        {
          cfgRpm = RPM_MIN;      /* 下限钳位（需求未定义 0 档） */
        }
      }
      if (appMode == APP_RUNNING)
      {
        motor_set_target(cfgRpm);   /* A14：RUNNING 下实时生效 */
      }
      break;

    case EDIT_TIME_H:
      if (dir > 0)
      {
        if (cfgH < TIME_HOUR_MAX)
        {
          cfgH++;
        }
      }
      else if (cfgH > 0U)
      {
        cfgH--;
      }
      timeEdited = 1U;
      break;

    case EDIT_TIME_M:
      cfgM = (dir > 0) ? (uint8_t)((cfgM + 1U) % 60U)
                       : (uint8_t)((cfgM + 59U) % 60U);
      timeEdited = 1U;
      break;

    case EDIT_TIME_S:
      cfgS = (dir > 0) ? (uint8_t)((cfgS + 1U) % 60U)
                       : (uint8_t)((cfgS + 59U) % 60U);
      timeEdited = 1U;
      break;

    default:
      break;
  }
}

/* ====================== 模块接口 ====================== */

void app_state_init(void)
{
  user_param_t p;

  appMode    = APP_STANDBY;
  appEdit    = EDIT_NONE;
  idleMs     = 0U;
  timeEdited = 0U;
  reached    = 0U;
  reachHoldMs = 0U;
  blinkMs    = 0U;
  blinkPhase = 0U;
  evStart    = 0U;
  evStop     = 0U;
  evPress    = 0U;
  evCw       = 0U;
  evCcw      = 0U;
  evZero     = 0U;

  /* 从 Flash 恢复上次设置（无记录 → 默认 0，A13） */
  storage_get_params(&p);
  cfgRpm = p.rpm;
  cfgH   = p.h;
  cfgM   = p.m;
  cfgS   = p.s;

  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_KEY_START);
  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_KEY_STOP);
  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_KNOB_PRESS);
  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_KNOB_CW);
  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_KNOB_CCW);
  evt_bus_subscribe(EVT_MOD_APP_STATE, EVT_CNTDOWN_ZERO);
}

/**
  * @brief  事件回调：只置标志（动作在 app_state_ops 执行）
  */
void app_state_on_event(evt_type_t type, uint16_t val)
{
  (void)val;

  switch (type)
  {
    case EVT_KEY_START:
      evStart = 1U;
      break;

    case EVT_KEY_STOP:
      evStop = 1U;
      break;

    case EVT_KNOB_PRESS:
      evPress = 1U;
      break;

    case EVT_KNOB_CW:
      if (evCw < 200U)
      {
        evCw++;
      }
      break;

    case EVT_KNOB_CCW:
      if (evCcw < 200U)
      {
        evCcw++;
      }
      break;

    case EVT_CNTDOWN_ZERO:
      evZero = 1U;
      break;

    default:
      break;
  }
}

void app_state_ops(void)
{
  uint8_t start;
  uint8_t stop;
  uint8_t press;
  uint8_t cw;
  uint8_t ccw;
  uint8_t zero;

  /* 取事件标志（始终主循环上下文：button_ops 与 dispatch 均在主循环，无并发） */
  start = evStart; evStart = 0U;
  stop  = evStop;  evStop  = 0U;
  press = evPress; evPress = 0U;
  cw    = evCw;    evCw    = 0U;
  ccw   = evCcw;   evCcw   = 0U;
  zero  = evZero;  evZero  = 0U;

  /* ---- 倒计时归零：停机回待机（A4：参数保留） ---- */
  if (zero != 0U)
  {
    appMode = APP_STANDBY;
    reached = 0U;
    reachHoldMs = 0U;
    motor_halt();                /* 立即清零输出模型（同原 CountdownTick 行为） */
    (void)evt_bus_post(EVT_MODE_CHANGED, (uint16_t)appMode);
  }

  /* ---- 按键事件（优先级：开始 > 停止 > 单击/旋钮） ---- */
  if (start != 0U)
  {
    app_do_start();
  }
  else if (stop != 0U)
  {
    app_do_stop();
  }
  else if (appEdit != EDIT_NONE)
  {
    if (press != 0U)   /* 单击：SPEED → TIME_H → TIME_M → TIME_S → SPEED 循环 */
    {
      appEdit = (edit_field_t)(((uint8_t)appEdit % 4U) + 1U);
      idleMs  = 0U;
    }
    if ((cw != 0U) || (ccw != 0U))   /* 旋钮：调整当前编辑字段 */
    {
      while (cw > 0U)
      {
        app_apply_knob(1);
        cw--;
      }
      while (ccw > 0U)
      {
        app_apply_knob(-1);
        ccw--;
      }
      idleMs = 0U;
      (void)evt_bus_post(EVT_PARAM_CHANGED, cfgRpm);
    }
  }
  else if (press != 0U)
  {
    app_enter_edit();      /* 任一非设置状态单击 → 进入设置 */
  }

  /* ---- 闪烁相位：500ms 翻转并广播（display 同步） ---- */
  blinkMs += TASK_PERIOD_APP_MS;
  if (blinkMs >= BLINK_PERIOD_MS)
  {
    blinkMs = 0U;
    blinkPhase ^= 1U;
    (void)evt_bus_post(EVT_BLINK_TOGGLE, (uint16_t)blinkPhase);
  }

  /* ---- 编辑无操作 5s 自动确认（A15） ---- */
  if (appEdit != EDIT_NONE)
  {
    idleMs += TASK_PERIOD_APP_MS;
    if (idleMs >= SETTING_TIMEOUT_MS)
    {
      app_commit();
      appEdit = EDIT_NONE;
      idleMs  = 0U;
    }
  }

  /* ---- 到达判定（2026-09-15 实装；2026-09-17 起仅发布信息事件，不再驱动倒计时——
         倒计时已由 app_do_start 的 timer_resume 直启）：实测转速 |实际−目标| ≤ 容差且
         持续 ≥ 保持时间 → post(EVT_REACH_TARGET)；数据源 = speed_sensor（白名单 get） ---- */
  if ((appMode == APP_RUNNING) && (reached == 0U))
  {
    if ((cfgRpm > 0U) && (((uint16_t)(cfgH | cfgM | cfgS)) != 0U))
    {
      int32_t err = (int32_t)cfgRpm - (int32_t)speed_sensor_get_rpm();

      if ((err >= -(int32_t)REACH_TOLERANCE_RPM) && (err <= (int32_t)REACH_TOLERANCE_RPM))
      {
        reachHoldMs += TASK_PERIOD_APP_MS;
        if (reachHoldMs >= REACH_HOLD_MS)
        {
          reached = 1U;
          reachHoldMs = 0U;
          (void)evt_bus_post(EVT_REACH_TARGET, cfgRpm);
        }
      }
      else
      {
        reachHoldMs = 0U;
      }
    }
  }
}

/* ====================== getter / shell 接口 ====================== */

app_mode_t app_state_get_mode(void)
{
  return appMode;
}

edit_field_t app_state_get_edit(void)
{
  return appEdit;
}

uint16_t app_state_get_cfg_rpm(void)
{
  return cfgRpm;
}

void app_state_get_cfg_time(uint16_t *h, uint8_t *m, uint8_t *s)
{
  if (h != 0)
  {
    *h = cfgH;
  }
  if (m != 0)
  {
    *m = cfgM;
  }
  if (s != 0)
  {
    *s = cfgS;
  }
}

void app_state_set_speed(uint16_t rpm)
{
  if (rpm > RPM_MAX)
  {
    rpm = RPM_MAX;
  }
  cfgRpm = rpm;
  app_commit();
  if (appMode == APP_RUNNING)
  {
    motor_set_target(cfgRpm);    /* 运行中修改：实时生效（A14） */
  }
}

void app_state_set_time(uint16_t h, uint8_t m, uint8_t s)
{
  if (h > TIME_HOUR_MAX)
  {
    h = TIME_HOUR_MAX;
  }
  if (m > 59U)
  {
    m = 59U;
  }
  if (s > 59U)
  {
    s = 59U;
  }
  cfgH = h;
  cfgM = m;
  cfgS = s;
  timeEdited = 1U;               /* 与编辑路径一致：下次启动重载（A11） */
  app_commit();
}

void app_state_start(void)
{
  app_do_start();
}

void app_state_stop(void)
{
  app_do_stop();
}
