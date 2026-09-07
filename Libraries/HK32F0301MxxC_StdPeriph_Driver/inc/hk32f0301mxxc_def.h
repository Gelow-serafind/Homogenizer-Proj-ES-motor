/********************************************************************************
  * @file    hk32f0301mxxc_def.h
  * @author  AE Team
  * @version V1.0.0/2022-04-14
  *     1.初始版本
  * @log  V1.0.0
  *******************************************************************************/

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __HK32F0301MXXC_DEF_H
#define __HK32F0301MXXC_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "hk32f0301mxxc.h"
#include <stdio.h>
#define UNUSED(X) (void)X      /* To avoid gcc/g++ warnings */


#if defined ( __GNUC__ ) && !defined (__CC_ARM) /* GNU Compiler */
#ifndef __weak
#define __weak   __attribute__((weak))
#endif /* __weak */
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif /* __packed */
#endif /* __GNUC__ */


/* Macro to get variable aligned on 4-bytes, for __ICCARM__ the directive "#pragma data_alignment=4" must be used instead */
#if defined ( __GNUC__ ) && !defined (__CC_ARM) /* GNU Compiler */
#ifndef __ALIGN_END
#define __ALIGN_END    __attribute__ ((aligned (4)))
#endif /* __ALIGN_END */
#ifndef __ALIGN_BEGIN
#define __ALIGN_BEGIN
#endif /* __ALIGN_BEGIN */
#else
#ifndef __ALIGN_END
#define __ALIGN_END
#endif /* __ALIGN_END */
#ifndef __ALIGN_BEGIN
#if defined   (__CC_ARM)      /* ARM Compiler */
#define __ALIGN_BEGIN    __align(4)
#elif defined (__ICCARM__)    /* IAR Compiler */
#define __ALIGN_BEGIN
#endif /* __CC_ARM */
#endif /* __ALIGN_BEGIN */
#endif /* __GNUC__ */

#if defined __GNUC__
  /* With GCC/RAISONANCE, small printf (option LD Linker->Libraries->Small printf
     set to 'Yes') calls __io_putchar() */
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#elif defined __ICCARM__
  #define PUTCHAR_PROTOTYPE int putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */
  
/**
  * @brief  __RAM_FUNC definition
  */
#if defined ( __CC_ARM   )
/* ARM Compiler
   ------------
   RAM functions are defined using the toolchain options.
   Functions that are executed in RAM should reside in a separate source module.
   Using the 'Options for File' dialog you can simply change the 'Code / Const'
   area of a module to a memory space in physical RAM.
   Available memory areas are declared in the 'Target' tab of the 'Options for Target'
   dialog.
*/
#define __RAM_FUNC

#elif defined ( __ICCARM__ )
/* ICCARM Compiler
   ---------------
   RAM functions are defined using a specific toolchain keyword "__ramfunc".
*/
#define __RAM_FUNC __ramfunc

#elif defined   (  __GNUC__  )
/* GNU Compiler
   ------------
  RAM functions are defined using a specific toolchain attribute
   "__attribute__((section(".RamFunc")))".
*/
#define __RAM_FUNC __attribute__((section(".RamFunc")))

#endif

/**
  * @brief  __NOINLINE definition
  */
#if defined ( __CC_ARM   ) || defined   (  __GNUC__  ) || defined(__ARMCC_VERSION)
/* ARM Compiler 5/6 & GNU Compiler
   ----------------
*/
#define __NOINLINE __attribute__((noinline))

#elif defined ( __ICCARM__ )
/* ICCARM Compiler
   ---------------
*/
#define __NOINLINE _Pragma("optimize = no_inline")

#endif

/* NOP 宏定义 - 支持各种编译器 */
#if defined ( __CC_ARM )   /* ARM Compiler 5 */
    #define NOP()   __nop()
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6000000)  /* ARM Compiler 6 */
    /* 使用内联汇编，避免头文件依赖 */
    #define NOP()   __asm volatile ("nop")
#elif defined ( __GNUC__ )  /* GCC Compiler */
    #define NOP()   asm("nop")
#elif defined ( __ICCARM__ )  /* IAR Compiler */
    #define NOP()   __no_operation()
#else
    #error "Unsupported compiler"
#endif

#ifdef __cplusplus
}
#endif

#endif /* ___HK32F0301MXXC_DEF_H */


