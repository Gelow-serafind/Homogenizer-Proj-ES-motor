/**
  ******************************************************************************
  * @file    hk32f0301mf4p7c_eval_i2c_ee.h
  * @author  Alexander
  * @version V1.0.0
  * @date    4-Jan-2023
  * @brief   This file contains all the functions prototypes for 
  *          the hk32f0301mf4p7c_eval_i2c_ee.c firmware driver.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __HK32F0301MF4P7C_EVAL_I2C_EE_H
#define __HK32F0301MF4P7C_EVAL_I2C_EE_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "hk32f0301mf4p7c_eval.h"

/** @addtogroup Boards
  * @{
  */
  
/** @addtogroup HK32_EVAL
  * @{
  */ 

/** @addtogroup HK32F0301MF4P7C_EVAL
  * @{
  */
  
/** @addtogroup HK32F0301MF4P7C_EVAL_I2C_EE
  * @{
  */  

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Uncomment the following line to use the default sEE_TIMEOUT_UserCallback() 
   function implemented in hk32l084rb_eval_i2c_ee.c file.
   sEE_TIMEOUT_UserCallback() function is called whenever a timeout condition 
   occurs during communication (waiting on an event that doesn't occur, bus 
   errors, busy devices ...). */   
/* #define USE_DEFAULT_TIMEOUT_CALLBACK */
   

#if !defined (sEE_HK24C64)
/* Use the defines below the choose the EEPROM type */
 #define sEE_HK24C02  /* Support the device: HK24C02. */
/* #define sEE_HK24C64 */ /*Support the devices: sEE_HK24C64 */ 
#endif

#if defined (sEE_HK24C02)
  #define sEE_HW_ADDRESS         0xA0
#elif defined (sEE_HK24C64)
  #define sEE_HW_ADDRESS         0xA0
#endif /* sEE_HK24C02 */

#define sEE_I2C_TIMING          0x00201D2B

#if defined (sEE_HK24C02)
#define sEE_PAGESIZE           8
#elif defined (sEE_HK24C64)
#define sEE_PAGESIZE           16
#endif

   
/* Maximum Timeout values for flags and events waiting loops. These timeouts are
   not based on accurate values, they just guarantee that the application will 
   not remain stuck if the I2C communication is corrupted.
   You may modify these timeout values depending on CPU frequency and application
   conditions (interrupts routines ...). */   
#define sEE_FLAG_TIMEOUT         ((uint32_t)0x1000)
#define sEE_LONG_TIMEOUT         ((uint32_t)(10 * sEE_FLAG_TIMEOUT))

/* Maximum number of trials for sEE_WaitEepromStandbyState() function */
#define sEE_MAX_TRIALS_NUMBER     300
      
#define sEE_OK                    0
#define sEE_FAIL                  1   

/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */ 
void     sEE_DeInit(void);
void     sEE_Init(void);
uint32_t sEE_ReadBuffer(uint8_t* pBuffer, uint16_t ReadAddr, uint16_t* NumByteToRead);
uint32_t sEE_WritePage(uint8_t* pBuffer, uint16_t WriteAddr, uint8_t* NumByteToWrite);
void     sEE_WriteBuffer(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite);
uint32_t sEE_WaitEepromStandbyState(void);

/* USER Callbacks: These are functions for which prototypes only are declared in
   EEPROM driver and that should be implemented into user application. */  
/* sEE_TIMEOUT_UserCallback() function is called whenever a timeout condition 
   occurs during communication (waiting on an event that doesn't occur, bus 
   errors, busy devices ...).
   You can use the default timeout callback implementation by uncommenting the 
   define USE_DEFAULT_TIMEOUT_CALLBACK in hk32f0301mf4p7c_eval_i2c_ee.h file.
   Typically the user implementation of this callback should reset I2C peripheral
   and re-initialize communication or in worst case reset all the application. */
uint32_t sEE_TIMEOUT_UserCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __HK32F0301MF4P7C_EVAL_I2C_EE_H */

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

