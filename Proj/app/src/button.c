/*******************************************************************************
* @filename:  button.c
* @brief:     按键/编码器扫描实现（原 Task_Encoder 迁移，行为等价）
* @note       事件产生：EVT_KNOB_CW/CCW（每格）、EVT_KNOB_PRESS（旋钮释放沿）、
*              EVT_KEY_START / EVT_KEY_STOP（按下沿，单次不重复）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ec11.h"
#include "event_bus.h"
#include "button.h"

void button_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);

  ec11_init();   /* A/B 相（PC5/PC3） */

  /* 旋钮按键 PC4：高有效（外部 10k 下拉），下拉输入 */
  GPIO_InitStructure.GPIO_Pin   = EC11_PIN_KEY;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);

  /* START PA2 / STOP PD7：低有效（外部 10k 上拉），上拉输入 */
  GPIO_InitStructure.GPIO_Pin  = KEY_START_PIN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(KEY_START_GPIO_PORT, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = KEY_STOP_PIN;
  GPIO_Init(KEY_STOP_GPIO_PORT, &GPIO_InitStructure);
}

void button_ops(void)
{
  /* ---- 旋钮旋转：解码增量 → 每格一个事件（原 EncDeltaPending 消费语义等价） ---- */
  ec11_ops();
  {
    int32_t d = ec11_take_delta();

    while (d > 0)
    {
      (void)evt_bus_post(EVT_KNOB_CW, 0U);
      d--;
    }
    while (d < 0)
    {
      (void)evt_bus_post(EVT_KNOB_CCW, 0U);
      d++;
    }
  }

  /* ---- 旋钮按键 EC11 KEY（PC4，高有效：按下=高）----
     内部语义：raw=0 表示按下；在释放沿产生一次单击事件 */
  {
    static uint8_t st = 1U;   /* 1=未按（对应未按 raw=1），0=按下 */
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_KEY) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 释放沿 → 单击 */
        {
          (void)evt_bus_post(EVT_KNOB_PRESS, 0U);
        }
      }
    }
    else
    {
      db = 0U;
    }
  }

  /* ---- 开始键（PA2，低有效：按下=低，外部上拉 R13）---- */
  {
    static uint8_t st = 1U;   /* 稳态 0=未按（对应未按 raw=0），1=按下 */
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(KEY_START_GPIO_PORT, KEY_START_PIN) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 按下沿 → 开始事件（单次，不重复） */
        {
          (void)evt_bus_post(EVT_KEY_START, 0U);
        }
      }
    }
    else
    {
      db = 0U;
    }
  }

  /* ---- 停止键（PD7，低有效：按下=低，外部上拉 R11）---- */
  {
    static uint8_t st = 1U;
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(KEY_STOP_GPIO_PORT, KEY_STOP_PIN) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 按下沿 → 停止事件 */
        {
          (void)evt_bus_post(EVT_KEY_STOP, 0U);
        }
      }
    }
    else
    {
      db = 0U;
    }
  }
}
