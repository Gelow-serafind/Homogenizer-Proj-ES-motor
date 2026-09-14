/*******************************************************************************
* @filename:  main.c
* @brief:     程序入口（极简）：外设/模块初始化 + 超级循环
* @note        全部业务逻辑见 Proj/app/：
*                app/src/app.c    —— 任务调度表 + evt_bus_dispatch 编排（唯一编排点）
*                app/src 其余 .c  —— 9 个业务模块 + 4 个芯片驱动 + 调试 shell
*              本文件不得新增业务代码（CLAUDE.md 第 3 节）。
*******************************************************************************/
#include "main.h"
#include "app.h"

int main(void)
{
  /* 外设与模块初始化（UART/TIM1/EC11/I2C/HT1623/Flash + 事件总线 + SysTick） */
  App_Init();

  /* 超级循环：事件分发 + 任务调度（不返回） */
  App_Run();

  return 0;
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif
