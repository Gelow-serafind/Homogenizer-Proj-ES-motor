/*******************************************************************************
* @filename:  app_state.h
* @brief:     主状态机模块：主模式×编辑层正交状态机、参数变更、到达判定、
*             倒计时协调、派生事件发布（架构第 3.2/5 节）
* @note       主模式：STANDBY / RUNNING / PAUSED；
*              编辑叠加层：SPEED → TIME_H → TIME_M → TIME_S 循环（可在任一主模式上激活）
*******************************************************************************/
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include "event_bus.h"

typedef enum {
  APP_STANDBY = 0,   /* 待机：显示上次参数，可直接启动 */
  APP_RUNNING,       /* 运行：转速斜坡→目标；倒计时 START 即开始（2026-09-17） */
  APP_PAUSED         /* 暂停：电机停、倒计时冻结、可恢复 */
} app_mode_t;

typedef enum {
  EDIT_NONE = 0,     /* 非编辑态 */
  EDIT_SPEED,
  EDIT_TIME_H,
  EDIT_TIME_M,
  EDIT_TIME_S
} edit_field_t;

void app_state_init(void);
void app_state_ops(void);                       /* 10ms：事件消费、状态迁移、5s 计时、到达判定 */
void app_state_on_event(evt_type_t type, uint16_t val);

/* 权威参数 getter（display/timer/storage 只读） */
app_mode_t   app_state_get_mode(void);
edit_field_t app_state_get_edit(void);
uint16_t     app_state_get_cfg_rpm(void);
void         app_state_get_cfg_time(uint16_t *h, uint8_t *m, uint8_t *s);

/* 调试 shell 接口（uart_cmd 调用；与 UI 路径语义一致） */
void app_state_set_speed(uint16_t rpm);                  /* speed 命令：设置并确认写 Flash */
void app_state_set_time(uint16_t h, uint8_t m, uint8_t s);
void app_state_start(void);                              /* start 命令 */
void app_state_stop(void);                               /* stop/pause 命令 */

#endif /* APP_STATE_H */
