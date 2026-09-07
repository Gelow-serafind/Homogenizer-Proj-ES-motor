/*******************************************************************************
* @filename:  main.h
* @brief:     
* @version:   V1.0/2026.04.19
*******************************************************************************/


/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "hk32f0301mxxc.h"

#ifdef USE_HK32F0301MF4P7C_EVAL
#include "hk32f0301mf4p7c_eval.h"
#endif /* USE_HK32F0301MF4P7C_EVAL */

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
void SysTick_Handler(void);

#ifdef __cplusplus
}
#endif  /* End of __cplusplus */
#endif /*__MAIN_H */

