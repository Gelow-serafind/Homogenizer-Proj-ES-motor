/*******************************************************************************
* @filename:  uart_cmd.h
* @brief:     UART 调试 shell：USART1 (PD4/PB4, 115200 8N1) 接收轮询与命令行
* @note       调试支撑模块（非产品需求）；fputc 串口重定向也在本模块
*******************************************************************************/
#ifndef UART_CMD_H
#define UART_CMD_H

#include "app_cfg.h"   /* BOOT_TACH_LOOP 开关 */

void uart_cmd_init(void);   /* UART 外设配置 + 欢迎信息 + 提示符 */
void uart_cmd_ops(void);    /* 2ms：接收轮询 + 行编辑 + 命令分发 */

#if (BOOT_TACH_LOOP != 0U)
void uart_cmd_boot_tach_loop(void);   /* 上电测速调试循环（不返回） */
#endif

#endif /* UART_CMD_H */
