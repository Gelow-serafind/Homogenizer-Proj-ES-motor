/*******************************************************************************
* @filename:  event_bus.c
* @brief:     事件总线实现：16 条环形队列 + 订阅位图 + 广播分发
* @note        免锁根因：所有 post 均发生在主循环上下文（含各 _ops 与 shell），
*              SysTick 仅递减任务计数，不触碰事件数据。
*******************************************************************************/
#include "main.h"
#include <string.h>
#include "event_bus.h"
#include "app_state.h"
#include "display.h"
#include "timer.h"

#define EVT_QUEUE_SIZE 16U

typedef struct {
  evt_type_t type;
  uint16_t   val;
} evt_item_t;

static evt_item_t evtQueue[EVT_QUEUE_SIZE];
static uint8_t    evtHead = 0U;   /* 生产者下标 */
static uint8_t    evtTail = 0U;   /* 消费者下标 */

/* 订阅位图：subMask[type] 的 bit m 表示模块 m 订阅了该事件 */
static uint16_t   subMask[EVT_TYPE_COUNT];

/* 各模块事件入口（回调只读：置标志/刷新缓存） */
typedef void (*evt_handler_t)(evt_type_t type, uint16_t val);

static const evt_handler_t modHandlers[EVT_MOD_COUNT] = {
  app_state_on_event,
  display_on_event,
  timer_on_event,
};

void evt_bus_init(void)
{
  evtHead = 0U;
  evtTail = 0U;
  memset(subMask, 0, sizeof(subMask));
  memset(evtQueue, 0, sizeof(evtQueue));
}

uint8_t evt_bus_subscribe(evt_module_t mod, evt_type_t type)
{
  if ((mod >= EVT_MOD_COUNT) || (type >= EVT_TYPE_COUNT))
  {
    return 0U;
  }
  subMask[type] |= (uint16_t)(1U << (uint8_t)mod);
  return 1U;
}

uint8_t evt_bus_post(evt_type_t type, uint16_t val)
{
  uint8_t next;

  if (type >= EVT_TYPE_COUNT)
  {
    return 0U;
  }

  next = (uint8_t)((evtHead + 1U) % EVT_QUEUE_SIZE);
  if (next == evtTail)
  {
    return 0U;   /* 队列满：丢弃（主循环每轮 dispatch，正常不会积压） */
  }

  evtQueue[evtHead].type = type;
  evtQueue[evtHead].val  = val;
  evtHead = next;
  return 1U;
}

uint8_t evt_bus_dispatch(void)
{
  uint8_t count = 0U;

  while (evtTail != evtHead)
  {
    evt_item_t it   = evtQueue[evtTail];
    uint16_t   mask;
    uint8_t    m;

    evtTail = (uint8_t)((evtTail + 1U) % EVT_QUEUE_SIZE);

    mask = subMask[it.type];
    for (m = 0U; m < (uint8_t)EVT_MOD_COUNT; m++)
    {
      if ((mask & (uint16_t)(1U << m)) != 0U)
      {
        modHandlers[m](it.type, it.val);
      }
    }
    count++;
  }

  return count;
}
