/*******************************************************************************
* @filename:  display.h
* @brief:     显示模块：字段渲染（4 数据区+图标区）、闪烁相位、差分写屏（架构第 8 节）
* @note       app_state 经白名单推送完整快照；50ms 重建 RAM，有差异才写屏。
*******************************************************************************/
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "event_bus.h"
#include "app_state.h"

typedef struct {
  app_mode_t mode;
  edit_field_t edit;
  uint16_t cfg_rpm, cfg_h, rem_h, raw_rpm;
  int16_t temp_deci_c;
  uint8_t cfg_m, cfg_s, rem_m, rem_s;
  uint8_t temp_valid, timer_active;
} display_view_t;

/* app_state 白名单推送；仅复制，不写屏 */
void display_set_view(const display_view_t *view);

void display_init(void);      /* ht1623 初始化 + 开机全显（清屏） */
void display_ops(void);       /* 50ms：渲染 + 差分写屏（手动调试模式仅送屏） */
void display_on_event(evt_type_t type, uint16_t val);
void display_request_refresh(void);

/* 调试接口（shell lcd 命令；手动模式暂停业务自动刷新） */
void display_debug_all(uint8_t on);     /* 全亮/全灭（点亮测试） */
void display_debug_digit(uint8_t n);    /* 第 n 位数码管显示 8（1..28） */
void display_debug_icon(uint8_t s);     /* 点亮图标 S 编号（1..30） */
void display_debug_sweep_start(void);   /* S1..S30 自动轮扫（1.5s/个，串口同步打编号；扫完回自动） */
void display_debug_sweep_stop(void);    /* 提前结束轮扫（保持当前段亮） */
void display_debug_auto(void);          /* 恢复业务自动刷新 */

#endif /* DISPLAY_H */
