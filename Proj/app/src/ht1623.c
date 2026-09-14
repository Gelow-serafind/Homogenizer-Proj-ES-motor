/*******************************************************************************
* @filename:  ht1623.c
* @brief:     HT1623 4 线串行驱动实现（原 HT1623_* 迁移，协议已验证）
* @note       命令：9 位命令码，末位 X=0；SYS_EN=0x01 / LCD_ON=0x03 / RC32K=0x18
*              固定 1/4 偏压、1/8 占空比（无 BIAS 命令）
*******************************************************************************/
#include "main.h"
#include "app_cfg.h"
#include "ht1623.h"

/* 命令码（9 位） */
#define HT1623_CMD_SYS_EN   0x01U   /* 开系统振荡器 */
#define HT1623_CMD_LCD_ON   0x03U   /* 开 LCD 显示 */
#define HT1623_CMD_RC32K    0x18U   /* 内部 RC 32kHz（当前使用：U46 晶振不起振） */
#define HT1623_CMD_XTAL32K  0x1CU   /* 外接 32.768kHz 晶振（U46 补焊后切换） */

/* static 前置原型（ARMCLANG C99 禁止使用未声明函数） */
static void ht1623_delay(void);
static void ht1623_port_init(void);
static void ht1623_send_bits(uint16_t bits, uint8_t nbits);
static void ht1623_send_nibble(uint8_t nib);
static void ht1623_send_cmd(uint8_t cmd);

/**
  * @brief  位操作延时（@48MHz 约 8µs；WR ~60kHz，满足 4~300kHz 与脉宽 ≥1.67µs）
  */
static void ht1623_delay(void)
{
  for (volatile uint32_t i = 0; i < 96U; ++i)
  {
  }
}

/**
  * @brief  GPIO 初始化：CS=PD3、RD=PD2、WR=PD1、DATA=PC6 推挽输出
  *         CS 空闲高（低有效片选），RD 常高（只写模式），WR 空闲高
  */
static void ht1623_port_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

  GPIO_InitStructure.GPIO_Pin   = HT1623_PIN_CS | HT1623_PIN_RD | HT1623_PIN_WR;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
  GPIO_Init(HT1623_GPIO_PORT, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = HT1623_PIN_DATA;
  GPIO_Init(HT1623_PORT_DATA, &GPIO_InitStructure);

  /* 初始空闲电平 */
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_RD, Bit_SET);
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
  GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
}

/**
  * @brief  按 MSB 优先发送 nbits 位（数据在 WR 上升沿被锁存）
  */
static void ht1623_send_bits(uint16_t bits, uint8_t nbits)
{
  uint16_t mask = (uint16_t)(1U << (nbits - 1U));

  while (nbits-- != 0U)
  {
    if ((bits & mask) != 0U)
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_SET);
    }
    else
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
    }
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_RESET);
    ht1623_delay();
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
    ht1623_delay();
    mask >>= 1;
  }
}

/**
  * @brief  发送一个 nibble（4 位 RAM 数据，D0 先发）
  */
static void ht1623_send_nibble(uint8_t nib)
{
  for (uint8_t i = 0U; i < 4U; i++)
  {
    if (((nib >> i) & 1U) != 0U)
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_SET);
    }
    else
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
    }
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_RESET);
    ht1623_delay();
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
    ht1623_delay();
  }
}

/**
  * @brief  发送命令：CS=0 → ID(100) → 9 位命令码（末位 X=0） → CS=1
  */
static void ht1623_send_cmd(uint8_t cmd)
{
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_RESET);
  ht1623_send_bits(0x04U, 3U);                  /* ID = 100（命令模式） */
  ht1623_send_bits((uint16_t)(cmd << 1), 9U);   /* 命令码 9 位，末位 X=0 */
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  ht1623_delay();                               /* CS 高电平 ≥tCS(700ns)，复位串行接口 */
  ht1623_delay();
  ht1623_delay();
}

/**
  * @brief  写 RAM：CS=0 → ID(101) → 7 位起始地址 → 连续 nibble（地址自动递增） → CS=1
  */
void ht1623_write_ram(uint8_t startAddr, const uint8_t *buf, uint8_t len)
{
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_RESET);
  ht1623_send_bits(0x05U, 3U);                  /* ID = 101（写模式） */
  ht1623_send_bits((uint16_t)startAddr, 7U);    /* 7 位地址 A6..A0 */
  while (len-- != 0U)
  {
    ht1623_send_nibble(*buf++);
  }
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  ht1623_delay();                               /* CS 高电平 ≥tCS(700ns)，复位串行接口 */
  ht1623_delay();
  ht1623_delay();
}

/**
  * @brief  初始化：清 RAM → 选时钟源 → 开系统 → 开显示
  */
void ht1623_init(void)
{
  uint8_t zeros[HT1623_RAM_NIBBLES] = {0};

  ht1623_port_init();

  ht1623_write_ram(0U, zeros, HT1623_RAM_NIBBLES);

  ht1623_send_cmd(HT1623_CMD_RC32K);   /* 内部 RC 32kHz（U46 晶振不起振；补焊后切 XTAL32K） */
  ht1623_send_cmd(HT1623_CMD_SYS_EN);  /* 开系统振荡器 */
  ht1623_send_cmd(HT1623_CMD_LCD_ON);  /* 开显示 */
}
