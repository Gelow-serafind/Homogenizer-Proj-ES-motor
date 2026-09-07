/**
  ******************************************************************************
  * @file    hk32f0301mf4p7c_eval.h
  * @author  Alexander
  * @version V1.0.0
  * @date    30-Jan-2023
  * @brief   This file contains definitions for HK32F0301MF4P7C_EVAL's Leds, push-buttons
  *          and COM ports hardware resources.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __HK32F0301MF4P7C_EVAL_H
#define __HK32F0301MF4P7C_EVAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "hk32f0301mxxc.h"

/** @addtogroup Boards
  * @{
  */

/** @addtogroup HK32_EVAL
  * @{
  */

/** @addtogroup HK32F0301MF4P7C_EVAL
  * @{
  */

/** @addtogroup HK32F0301MF4P7C_EVAL_LOW_LEVEL
  * @{
  */

/* Exported types ------------------------------------------------------------*/
typedef enum
{
  LED1 = 0,
  LED2 = 1,
  LED3 = 2,
} Led_TypeDef;

typedef enum
{
  BUTTON_1 = 0,
  BUTTON_2 = 1,
} Button_TypeDef;

typedef enum
{
  BUTTON_MODE_GPIO = 0,
  BUTTON_MODE_EXTI = 1
} ButtonMode_TypeDef;

typedef enum
{
  BEEP1 = 0,
} Beep_TypeDef;

typedef enum
{
  COM1 = 0,
} COM_TypeDef;

/* Exported constants --------------------------------------------------------*/
/** @defgroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_Exported_Constants
  * @{
  */

/**
  * @brief  Define for HK32F0301MF4P7C_EVAL board
  */
#if !defined (USE_HK32F0301MF4P7C_EVAL)
#define USE_HK32F0301MF4P7C_EVAL
#endif

/** @addtogroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_LED
  * @{
  */
#define LEDn                             3
#define LED1_PIN                         GPIO_Pin_1
#define LED1_GPIO_PORT                   GPIOD
#define LED1_GPIO_CLK                    RCC_AHBPeriph_GPIOD

#define LED2_PIN                         GPIO_Pin_7
#define LED2_GPIO_PORT                   GPIOC
#define LED2_GPIO_CLK                    RCC_AHBPeriph_GPIOC

#define LED3_PIN                         GPIO_Pin_2
#define LED3_GPIO_PORT                   GPIOA
#define LED3_GPIO_CLK                    RCC_AHBPeriph_GPIOA

/**
  * @}
  */

/** @addtogroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_BUTTON
  * @{
  */
#define BUTTONn                          2

/**
 * @brief button 1
 */
#define BUTTON_1_PIN                GPIO_Pin_4
#define BUTTON_1_GPIO_PORT          GPIOC
#define BUTTON_1_GPIO_CLK           RCC_AHBPeriph_GPIOC
#define BUTTON_1_EXTI_LINE          EXTI_Line4
#define BUTTON_1_EXTI_PORT_SOURCE   EXTI_PortSourceGPIOC
#define BUTTON_1_EXTI_PIN_SOURCE    EXTI_PinSource4
#define BUTTON_1_EXTI_IRQn          EXTI4_IRQn

/**
 * @brief button 2
 */
#define BUTTON_2_PIN                GPIO_Pin_1
#define BUTTON_2_GPIO_PORT          GPIOA
#define BUTTON_2_GPIO_CLK           RCC_AHBPeriph_GPIOA
#define BUTTON_2_EXTI_LINE          EXTI_Line1
#define BUTTON_2_EXTI_PORT_SOURCE   EXTI_PortSourceGPIOA
#define BUTTON_2_EXTI_PIN_SOURCE    EXTI_PinSource1
#define BUTTON_2_EXTI_IRQn          EXTI1_IRQn

/**
  * @}
  */


/** @addtogroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_COM
  * @{
  */
#define COMn                             1

/**
 * @brief Definition for COM port1, connected to USART1
 */
#define EVAL_COM1                        UART1
#define EVAL_COM1_CLK                    RCC_APBPeriph2_UART1

#define EVAL_COM1_TX_PIN                 GPIO_Pin_3
#define EVAL_COM1_TX_GPIO_PORT           GPIOA
#define EVAL_COM1_TX_GPIO_CLK            RCC_AHBPeriph_GPIOA
#define EVAL_COM1_TX_SOURCE              GPIO_PinSource3
#define EVAL_COM1_TX_AF                  GPIO_AF_1

#define EVAL_COM1_RX_PIN                 GPIO_Pin_6
#define EVAL_COM1_RX_GPIO_PORT           GPIOD
#define EVAL_COM1_RX_GPIO_CLK            RCC_AHBPeriph_GPIOD
#define EVAL_COM1_RX_SOURCE              GPIO_PinSource6
#define EVAL_COM1_RX_AF                  GPIO_AF_1

#define EVAL_COM1_IRQn                   UART1_IRQn

/**
  * @}
  */

/**
  * @brief  HK25Q FLASH SPI Interface pins
  */
#define sFLASH_SPI                       SPI
#define sFLASH_SPI_CLK                   RCC_APBPeriph2_SPI

#define sFLASH_SPI_SCK_PIN               GPIO_Pin_3                  /* PD.03 */
#define sFLASH_SPI_SCK_GPIO_PORT         GPIOD                       /* GPIOD */
#define sFLASH_SPI_SCK_GPIO_CLK          RCC_AHBPeriph_GPIOD
#define sFLASH_SPI_SCK_GPIO_PinSource    GPIO_PinSource3
#define sFLASH_SPI_SCK_GPIO_AF           GPIO_AF_2

#define sFLASH_SPI_MISO_PIN              GPIO_Pin_4                  /* PB.04 */
#define sFLASH_SPI_MISO_GPIO_PORT        GPIOB                       /* GPIOB */
#define sFLASH_SPI_MISO_GPIO_CLK         RCC_AHBPeriph_GPIOB
#define sFLASH_SPI_MISO_GPIO_PinSource   GPIO_PinSource4
#define sFLASH_SPI_MISO_GPIO_AF          GPIO_AF_2

#define sFLASH_SPI_MOSI_PIN              GPIO_Pin_4                  /* PD.04 */
#define sFLASH_SPI_MOSI_GPIO_PORT        GPIOD                       /* GPIOD */
#define sFLASH_SPI_MOSI_GPIO_CLK         RCC_AHBPeriph_GPIOA
#define sFLASH_SPI_MOSI_GPIO_PinSource   GPIO_PinSource4
#define sFLASH_SPI_MOSI_GPIO_AF          GPIO_AF_2

#define sFLASH_CS_PIN                    GPIO_Pin_7                  /* PD.07 */
#define sFLASH_CS_GPIO_PORT              GPIOD                       /* GPIOD */
#define sFLASH_CS_GPIO_CLK               RCC_AHBPeriph_GPIOD
#define sFLASH_CS_GPIO_PinSource         GPIO_PinSource7
#define sFLASH_CS_GPIO_AF                GPIO_AF_2

/**
  * @}
  */


/**
  * @}
  */

/** @addtogroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_I2C_EE
  * @{
  */
/**
  * @brief  I2C EEPROM Interface pins
  */
#define sEE_I2C                          I2C
#define sEE_I2C_CLK                      RCC_APBPeriph1_I2C

#define sEE_I2C_SCL_PIN                  GPIO_Pin_6                  /* PC.06 */
#define sEE_I2C_SCL_GPIO_PORT            GPIOC                       /* GPIOC */
#define sEE_I2C_SCL_GPIO_CLK             RCC_AHBPeriph_GPIOC
#define sEE_I2C_SCL_SOURCE               GPIO_PinSource6
#define sEE_I2C_SCL_AF                   GPIO_AF_0

#define sEE_I2C_SDA_PIN                  GPIO_Pin_5                  /* PC.05 */
#define sEE_I2C_SDA_GPIO_PORT            GPIOC                       /* GPIOC */
#define sEE_I2C_SDA_GPIO_CLK             RCC_AHBPeriph_GPIOC
#define sEE_I2C_SDA_SOURCE               GPIO_PinSource5
#define sEE_I2C_SDA_AF                   GPIO_AF_0

/**
  * @}
  */

/** @defgroup HK32F0301MF4P7C_EVAL_LOW_LEVEL_Exported_Functions
  * @{
  */
void HK_EVAL_LEDInit(Led_TypeDef Led);
void HK_EVAL_LEDOn(Led_TypeDef Led);
void HK_EVAL_LEDOff(Led_TypeDef Led);
void HK_EVAL_LEDToggle(Led_TypeDef Led);
void HK_EVAL_PBInit(Button_TypeDef Button, ButtonMode_TypeDef Button_Mode);
uint32_t HK_EVAL_PBGetState(Button_TypeDef Button);
void HK_EVAL_COMInit(COM_TypeDef COM, UART_InitTypeDef* UART_InitStruct);
void sEE_LowLevel_DeInit(void);
void sEE_LowLevel_Init(void);
void sFLASH_LowLevel_DeInit(void);
void sFLASH_LowLevel_Init(void);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __HK32F0301MF4P7C_EVAL_H */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
