/*******************************************************************************
* @filename:  ads1115.c
* @brief:     ADS1115 驱动实现（原 I2C_ADS1115_* 迁移，I2C 时序沿用 EVAL sEE 值）
* @note       配置：OS=单次触发 + PGA=±6.144V(000，容纳 0~5V) + DR=860SPS + 比较器关
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ads1115.h"

#define ADS_I2C          I2C
#define ADS_I2C_TIMING   0x00201D2BU   /* 借 EVAL sEE 时序（48MHz SYSCLK） */

#define ADS_REG_CONV     0x00U
#define ADS_REG_CONFIG   0x01U

#define ADS_CFG_SINGLE   0x0100U       /* 单次转换模式 */
#define ADS_CFG_DR_860   0x00E0U       /* 860 SPS */

/* static 前置原型 */
static uint8_t  ads_wait_flag(uint32_t flag);
static uint8_t  ads_write_reg(uint8_t reg, uint16_t val);
static uint8_t  ads_set_pointer(uint8_t reg);
static uint16_t ads_read_conv(void);

/**
  * @brief  I2C + ADS1115 初始化（PA1/PA3，AF0 开漏）
  */
void ads1115_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  I2C_InitTypeDef  I2C_InitStructure;

  RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
  RCC_APBPeriph1ClockCmd(RCC_APBPeriph1_I2C, ENABLE);

  GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_0);
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_0);
  GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1 | GPIO_Pin_3;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
  I2C_InitStructure.I2C_AnalogFilter        = I2C_AnalogFilter_Enable;
  I2C_InitStructure.I2C_DigitalFilter       = 0x00;
  I2C_InitStructure.I2C_OwnAddress1         = 0x00;
  I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
  I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_InitStructure.I2C_Timing              = ADS_I2C_TIMING;
  I2C_Init(ADS_I2C, &I2C_InitStructure);
  I2C_Cmd(ADS_I2C, ENABLE);
}

/**
  * @brief  等待 I2C 标志（带超时）
  * @return 0=OK 1=超时
  */
static uint8_t ads_wait_flag(uint32_t flag)
{
  uint32_t t = 0xFFFFFU;

  while (I2C_GetFlagStatus(ADS_I2C, flag) == RESET)
  {
    if (--t == 0U)
    {
      return 1U;
    }
  }
  return 0U;
}

/**
  * @brief  写寄存器（pointer + 2 字节数据）
  * @return 0=OK 1=失败
  */
static uint8_t ads_write_reg(uint8_t reg, uint16_t val)
{
  uint8_t buf[3];

  buf[0] = reg;
  buf[1] = (uint8_t)(val >> 8);
  buf[2] = (uint8_t)(val & 0xFFU);

  I2C_TransferHandling(ADS_I2C, ADS1115_ADDR_W, 3U, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
  for (uint8_t i = 0U; i < 3U; i++)
  {
    if (ads_wait_flag(I2C_ISR_TXIS))
    {
      return 1U;
    }
    I2C_SendData(ADS_I2C, buf[i]);
  }
  if (ads_wait_flag(I2C_ISR_STOPF))
  {
    return 1U;
  }
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return 0U;
}

/**
  * @brief  写指针寄存器（后续读转换结果用）
  * @return 0=OK 1=失败
  */
static uint8_t ads_set_pointer(uint8_t reg)
{
  I2C_TransferHandling(ADS_I2C, ADS1115_ADDR_W, 1U, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
  if (ads_wait_flag(I2C_ISR_TXIS))
  {
    return 1U;
  }
  I2C_SendData(ADS_I2C, reg);
  if (ads_wait_flag(I2C_ISR_STOPF))
  {
    return 1U;
  }
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return 0U;
}

/**
  * @brief  读 2 字节转换结果（需先 ads_set_pointer(CONV)）
  * @return 16 位转换值；失败返回 0xFFFF
  */
static uint16_t ads_read_conv(void)
{
  uint16_t v = 0xFFFFU;

  I2C_TransferHandling(ADS_I2C, ADS1115_ADDR_R, 2U, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);
  for (uint8_t i = 0U; i < 2U; i++)
  {
    if (ads_wait_flag(I2C_ISR_RXNE))
    {
      return v;
    }
    if (i == 0U)
    {
      v = (uint16_t)((uint16_t)I2C_ReceiveData(ADS_I2C) << 8);
    }
    else
    {
      v |= I2C_ReceiveData(ADS_I2C);
    }
  }
  (void)ads_wait_flag(I2C_ISR_STOPF);
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return v;
}

/**
  * @brief  粗延时（约 ms 级，ADS1115 转换等待/I2C 保护用）
  */
void ads1115_delay_ms(uint32_t ms)
{
  volatile uint32_t i;
  volatile uint32_t n = ms;

  while (n > 0U)
  {
    for (i = 0U; i < 9000U; i++)
    {
      (void)i;
    }
    n--;
  }
}

/**
  * @brief  触发单次转换（非阻塞）
  * @param  ch: 0=AIN0 1=AIN1 2=AIN2 3=AIN3（vs GND）
  * @return 1=已触发 0=失败
  */
uint8_t ads1115_start(uint8_t ch)
{
  static const uint16_t mux[4] = { 0x4000U, 0x5000U, 0x6000U, 0x7000U };
  uint16_t cfg;

  if (ch > 3U)
  {
    return 0U;
  }
  cfg = (uint16_t)(0x8000U | ADS_CFG_SINGLE | ADS_CFG_DR_860 | mux[ch]);  /* OS=1 启动单次 */

  if (ads_write_reg(ADS_REG_CONFIG, cfg) != 0U)
  {
    return 0U;
  }
  return 1U;
}

/**
  * @brief  读取上次触发的转换结果（非阻塞；调用前需等待 ≥2ms）
  * @return 16 位转换值；失败返回 0xFFFF
  */
uint16_t ads1115_fetch(void)
{
  if (ads_set_pointer(ADS_REG_CONV) != 0U)
  {
    return 0xFFFFU;
  }
  return ads_read_conv();
}

/**
  * @brief  阻塞单次转换（内部等待 ~10ms）
  * @param  ch: 0=AIN0 1=AIN1 2=AIN2 3=AIN3
  * @return 16 位转换值；失败返回 0xFFFF
  */
uint16_t ads1115_read(uint8_t ch)
{
  if (ads1115_start(ch) == 0U)
  {
    return 0xFFFFU;
  }
  ads1115_delay_ms(10U);   /* 860SPS≈1.16ms，余量给足 */
  return ads1115_fetch();
}
