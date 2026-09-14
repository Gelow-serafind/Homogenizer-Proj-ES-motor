/*******************************************************************************
* @filename:  display.h
* @brief:     显示模块：字段渲染（4 数据区+图标区）、闪烁相位、差分写屏（架构第 8 节）
* @note       渲染采用"周期拉取式"：display_ops 每 50ms 经各模块 getter 重建 RAM 镜像，
*              与上一帧 memcmp 有差异才经 ht1623 整片写屏；
*              字段布局/图标编号为推断初值，实测校准只改本文件两张表（R6）
*******************************************************************************/
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "event_bus.h"

void display_init(void);      /* ht1623 初始化 + 开机全显（清屏） */
void display_ops(void);       /* 50ms：渲染 + 差分写屏（手动调试模式仅送屏） */
void display_on_event(evt_type_t type, uint16_t val);
void display_request_refresh(void);

/* 调试接口（shell lcd 命令；手动模式暂停业务自动刷新） */
void display_debug_all(uint8_t on);     /* 全亮/全灭（点亮测试） */
void display_debug_digit(uint8_t n);    /* 第 n 位数码管显示 8（1..28） */
void display_debug_icon(uint8_t s);     /* 点亮图标 S 编号（1..30） */
void display_debug_auto(void);          /* 恢复业务自动刷新 */

#endif /* DISPLAY_H */
