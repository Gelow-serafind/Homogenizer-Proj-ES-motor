/*******************************************************************************
* @filename:  ec11.h
* @brief:     EC11 旋转编码器驱动：A/B 相正交解码（4 倍频），无业务语义
* @note       高有效（公共端 5V + 外部 10k 下拉）；每格 = 4 个状态边沿；
*              EC11_USE_EXTI=1（默认）：PC5=EXTI5 / PC3=EXTI3 双边沿硬件捕获；
*              EC11_USE_EXTI=0：回退 2ms 轮询采样（旧行为，便于对比排查）。
*******************************************************************************/
#ifndef EC11_H
#define EC11_H

#include <stdint.h>

typedef struct {
  uint32_t isrCount;    /* EXTI 触发次数（含无变化的毛刺触发） */
  uint32_t edgeCount;   /* 有效状态跳变（4 倍频边沿） */
  int32_t  detentNet;   /* 净格数（消费侧统计，带符号） */
} ec11_stats_t;

void    ec11_init(void);        /* A/B 相 GPIO（PC5/PC3）+ 捕获配置 + 解码复位 */
void    ec11_ops(void);         /* EXTI 模式空操作；轮询模式执行一次采样解码 */
int32_t ec11_take_delta(void);  /* 取累计整格增量（±120 饱和，余量保留），取后清零 */
void    ec11_take_stats(ec11_stats_t* st);   /* 诊断：取统计并清零（st 非空） */

#endif /* EC11_H */
