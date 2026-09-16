/*******************************************************************************
* @filename:  timer.h
* @brief:     倒计时模块：剩余时间模型、启停/重载/每秒递减、归零事件（架构第 3.2 节）
* @note       递减由 app_state 在 START 时显式 timer_resume() 启动（2026-09-17 用户决策：
*              点击 START 即开始倒数，取代 A12"到达目标后才递减"旧语义）；
*              暂停冻结（timer_pause）、改过时间重载（timer_load）；
*              归零 → 发布 EVT_CNTDOWN_ZERO（app_state 收到后停机回待机，A4）
*******************************************************************************/
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "event_bus.h"

void    timer_init(void);
void    timer_ops(void);               /* 10ms：消费 EVT_1S 秒节拍递减 */
void    timer_on_event(evt_type_t type, uint16_t val);

void    timer_load(uint16_t h, uint8_t m, uint8_t s);   /* 装载新值（不启动递减，需 timer_resume） */
void    timer_pause(void);                              /* 冻结（保留剩余值；STOP 时调用） */
void    timer_resume(void);                             /* 开始/继续递减（START 时调用） */
void    timer_get_rem(uint16_t *h, uint8_t *m, uint8_t *s);
uint8_t timer_is_active(void);                          /* 递减进行中（shell 状态显示用） */

#endif /* TIMER_H */
