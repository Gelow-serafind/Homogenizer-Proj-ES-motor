/*******************************************************************************
* @filename:  app.h
* @brief:     应用框架入口：外设/模块初始化 + 超级循环（唯一编排点）
* @note        main.c 只调用 App_Init() 与 App_Run()
*******************************************************************************/
#ifndef APP_H
#define APP_H

void App_Init(void);   /* 事件总线 + 各模块 _init + SysTick 配置 */
void App_Run(void);    /* 超级循环：evt_bus_dispatch + 任务调度表轮询（不返回） */

#endif /* APP_H */
