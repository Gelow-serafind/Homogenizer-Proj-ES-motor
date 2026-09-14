/*******************************************************************************
* @filename:  timer.c
* @brief:     倒计时模块实现（原 Task_App 内倒计时逻辑迁移，行为等价）
* @note        秒节拍由 housekeeping 任务 1000ms 发布 EVT_1S；
*              回调只置标志，递减在 timer_ops 中执行（回调只读纪律）
*******************************************************************************/
#include "main.h"
#include "event_bus.h"
#include "timer.h"

static uint16_t remH = 0U;
static uint8_t  remM = 0U;
static uint8_t  remS = 0U;
static uint8_t  active = 0U;          /* 递减进行中 */

static uint8_t  tickPending   = 0U;   /* EVT_1S 接收标志 */
static uint8_t  resumePending = 0U;   /* EVT_REACH_TARGET 接收标志 */

void timer_init(void)
{
  remH = 0U;
  remM = 0U;
  remS = 0U;
  active = 0U;
  tickPending = 0U;
  resumePending = 0U;

  evt_bus_subscribe(EVT_MOD_TIMER, EVT_1S);
  evt_bus_subscribe(EVT_MOD_TIMER, EVT_REACH_TARGET);
}

/**
  * @brief  事件回调（只置标志；递减/恢复在 timer_ops 执行）
  */
void timer_on_event(evt_type_t type, uint16_t val)
{
  (void)val;

  if (type == EVT_1S)
  {
    tickPending = 1U;
  }
  else if (type == EVT_REACH_TARGET)
  {
    resumePending = 1U;
  }
  else
  {
    /* 其他事件不订阅 */
  }
}

void timer_ops(void)
{
  if (resumePending != 0U)
  {
    resumePending = 0U;
    timer_resume();                    /* 到达目标：开始/继续递减（A12） */
  }

  if (tickPending != 0U)
  {
    tickPending = 0U;
    if (active != 0U)
    {
      if (remS > 0U)
      {
        remS--;
      }
      else if (remM > 0U)
      {
        remM--;
        remS = 59U;
      }
      else if (remH > 0U)
      {
        remH--;
        remM = 59U;
        remS = 59U;
      }
      else
      {
        /* 归零：停表并发布事件（app_state → STANDBY、motor_halt） */
        active = 0U;
        evt_bus_post(EVT_CNTDOWN_ZERO, 0U);
      }
    }
  }
}

void timer_load(uint16_t h, uint8_t m, uint8_t s)
{
  remH = h;
  remM = m;
  remS = s;
  active = 0U;          /* 等到达目标转速后再开始递减（A12） */
  tickPending = 0U;
  resumePending = 0U;
}

void timer_pause(void)
{
  active = 0U;          /* 冻结：保留剩余值 */
}

void timer_resume(void)
{
  active = 1U;
}

void timer_get_rem(uint16_t *h, uint8_t *m, uint8_t *s)
{
  if (h != 0)
  {
    *h = remH;
  }
  if (m != 0)
  {
    *m = remM;
  }
  if (s != 0)
  {
    *s = remS;
  }
}

uint8_t timer_is_active(void)
{
  return active;
}
