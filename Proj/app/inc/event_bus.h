/*******************************************************************************
* @filename:  event_bus.h
* @brief:     事件总线：事件定义、投递、订阅与广播分发（架构第 3.2/4 节）
* @note        硬性约定：仅主循环上下文 evt_bus_post（ISR 禁止投递）；
*              订阅回调只读（置标志/刷新缓存），回调内禁止 post 与长操作
*******************************************************************************/
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>

/* 事件类型（架构第 3.2 节 evt_type_t） */
typedef enum {
  EVT_NONE = 0,
  EVT_KEY_START,        /* START 按下沿        */
  EVT_KEY_STOP,         /* STOP 按下沿         */
  EVT_KNOB_PRESS,       /* 旋钮单击（释放沿）  */
  EVT_KNOB_CW,          /* 旋钮顺时针 1 格     */
  EVT_KNOB_CCW,         /* 旋钮逆时针 1 格     */
  EVT_PARAM_CHANGED,    /* 参数预览变化        */
  EVT_PARAM_CONFIRMED,  /* 参数确认（已请求落盘） */
  EVT_MODE_CHANGED,     /* 主模式迁移（val=新模式） */
  EVT_SPEED_UPDATED,    /* 实时转速刷新（val=RPM） */
  EVT_REACH_TARGET,     /* 实际转速到达目标    */
  EVT_CNTDOWN_ZERO,     /* 倒计时归零          */
  EVT_TEMP_VALID,       /* 探头插入            */
  EVT_TEMP_INVALID,     /* 探头拔出            */
  EVT_1S,               /* 秒节拍（housekeeping 产生） */
  EVT_BLINK_TOGGLE,     /* 500ms 闪烁相位翻转（val=新相位） */
  EVT_TYPE_COUNT
} evt_type_t;

/* 订阅者（模块）标识：位图下标，扩展时在 evt_module_t 与 modHandlers 同步增加 */
typedef enum {
  EVT_MOD_APP_STATE = 0,
  EVT_MOD_DISPLAY,
  EVT_MOD_TIMER,
  EVT_MOD_MOTOR,        /* 速度环：订阅 EVT_SPEED_UPDATED（回调缓存实测转速） */
  EVT_MOD_COUNT
} evt_module_t;

void    evt_bus_init(void);                                    /* 清空队列与订阅表 */
uint8_t evt_bus_post(evt_type_t type, uint16_t val);           /* 投递（仅主循环上下文） */
uint8_t evt_bus_subscribe(evt_module_t mod, evt_type_t type);  /* 订阅（init 期完成） */
uint8_t evt_bus_dispatch(void);                                /* 主循环每轮：取出并广播全部事件 */

#endif /* EVENT_BUS_H */
