/*******************************************************************************
* @filename:  ec11.c
* @brief:     EC11 正交解码实现（原 Task_Encoder 解码部分迁移，行为等价）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ec11.h"

/* 状态转移表：idx = (prevAB<<2)|currAB，值 = 单个边沿的方向（+1/-1/0 无效） */
static const int8_t ec11Transition[16] =
{
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

static uint8_t ec11PrevAB  = 0x03U;
static int8_t  ec11StepAcc = 0;    /* 单格内 4 边沿累计 */
static int32_t ec11Delta   = 0;    /* 待取增量（格），±120 饱和 */

void ec11_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

  /* A/B 相：PC5/PC3，高有效（外部 10k 下拉），下拉输入 */
  GPIO_InitStructure.GPIO_Pin   = EC11_PIN_A | EC11_PIN_B;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);

  ec11PrevAB  = 0x03U;
  ec11StepAcc = 0;
  ec11Delta   = 0;
}

void ec11_ops(void)
{
  uint8_t a = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_A) != Bit_RESET) ? 1U : 0U;
  uint8_t b = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_B) != Bit_RESET) ? 1U : 0U;
  uint8_t currAB = (uint8_t)((a << 1) | b);
  int8_t  delta  = ec11Transition[(uint8_t)((ec11PrevAB << 2) | currAB)];

  ec11PrevAB = currAB;

  if (delta != 0)
  {
    ec11StepAcc += delta;

    if (ec11StepAcc >= 4)
    {
      ec11StepAcc = 0;
      if (ec11Delta < 120)
      {
        ec11Delta++;        /* 顺时针 1 格 */
      }
    }
    else if (ec11StepAcc <= -4)
    {
      ec11StepAcc = 0;
      if (ec11Delta > -120)
      {
        ec11Delta--;        /* 逆时针 1 格 */
      }
    }
  }
}

int32_t ec11_take_delta(void)
{
  int32_t d = ec11Delta;
  ec11Delta = 0;
  return d;
}
