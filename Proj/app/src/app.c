/*******************************************************************************
* @filename:  app.c
* @brief:     应用框架：任务调度表（SysTick 1ms 递减）、超级循环、秒节拍
* @note        1. 调度器沿用现有 Task_t 协作式模式；
*              2. SysTick_Handler 只做任务计数器递减（ISR 最短纪律）；
*              3. 秒节拍 EVT_1S 由 housekeeping（1000ms）发布。
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "app_state.h"
#include "storage.h"
#include "timer.h"
#include "motor.h"
#include "button.h"
#include "display.h"
#include "speed_sensor.h"
#include "temp_sensor.h"
#include "ads1115.h"
#include "uart_cmd.h"
#include "app.h"

/* ====================== 任务调度表 ====================== */

typedef void (*TaskHandler_t)(void);

typedef struct {
  uint16_t period_ms;    /* 周期 */
  volatile uint16_t cnt; /* 倒计时（SysTick 递减） */
  TaskHandler_t handler;
} Task_t;

static void housekeeping_ops(void);   /* 前置原型（tasks[] 初始化引用） */

static Task_t tasks[] = {
  { TASK_PERIOD_BUTTON_MS,    0U, button_ops       },
  { TASK_PERIOD_UART_MS,      0U, uart_cmd_ops     },
  { TASK_PERIOD_APP_MS,       0U, app_state_ops    },
  { TASK_PERIOD_TIMER_MS,     0U, timer_ops        },
  { TASK_PERIOD_MOTOR_MS,     0U, motor_ops        },
  { TASK_PERIOD_SPEED_MS,     0U, speed_sensor_ops },
  { TASK_PERIOD_TEMP_MS,      0U, temp_sensor_ops  },
  { TASK_PERIOD_DISPLAY_MS,   0U, display_ops      },
  { TASK_PERIOD_STORAGE_MS,   0U, storage_ops      },
  { TASK_PERIOD_HOUSEKEEP_MS, 0U, housekeeping_ops },
};

#define TASKS_COUNT (sizeof(tasks) / sizeof(tasks[0]))

/**
  * @brief  1000ms 后台任务：发布秒节拍
  * @note   TODO(SYS-02)：IWDG 喂狗（看门狗启用后在此加入）
  */
static void housekeeping_ops(void)
{
  (void)evt_bus_post(EVT_1S, 0U);
}

/* ====================== SysTick（仅任务计数器） ====================== */

void SysTick_Handler(void)
{
  for (uint32_t i = 0; i < TASKS_COUNT; ++i)
  {
    if (tasks[i].cnt > 0U)
    {
      tasks[i].cnt--;
    }
  }
}

/* ====================== 框架入口 ====================== */

void App_Init(void)
{
  evt_bus_init();      /* 事件总线最先（订阅表在各自 init 建立） */

  uart_cmd_init();     /* USART1 + shell 提示（最先可用便于诊断） */
  motor_init();        /* TIM1_CH2 PD6 10kHz，初始 0% */
  button_init();       /* EC11 A/B/KEY + START/STOP GPIO */
  ads1115_init();      /* I2C PA1/PA3 AF0 */
  display_init();      /* HT1623 4 线串行 */
  storage_init();      /* 末页槽扫描 → 参数 */
  app_state_init();    /* 状态机复位 + 读取 storage 参数 + 订阅事件 */
  timer_init();
  speed_sensor_init();
  temp_sensor_init();

  /* SysTick 1ms */
  if (SysTick_Config(SystemCoreClock / 1000U))
  {
    while (1)
    {
    }
  }

#if (BOOT_TACH_LOOP != 0U)
  uart_cmd_boot_tach_loop();   /* 调试：不返回；验证完将 BOOT_TACH_LOOP 置 0 */
#endif
}

void App_Run(void)
{
  while (1)
  {
    /* 事件零积压：先消费全部事件，再轮询任务 */
    (void)evt_bus_dispatch();

    for (uint32_t i = 0; i < TASKS_COUNT; ++i)
    {
      if (tasks[i].cnt == 0U)
      {
        /* 重置倒计时（关中断保证原子性），随后调用任务处理函数 */
        __disable_irq();
        tasks[i].cnt = tasks[i].period_ms;
        __enable_irq();

        tasks[i].handler();
      }
    }
  }
}
