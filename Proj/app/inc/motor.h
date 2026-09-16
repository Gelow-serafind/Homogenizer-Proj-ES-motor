/*******************************************************************************
* @filename:  motor.h
* @brief:     电机控制模块：TIM1_CH2 PWM 输出、RPM-占空比 LUT、转速斜坡（架构第 3.2 节）
* @note       PWM=PD6(TIM1_CH2, AF3)，10kHz，ARR=4799，高电平=通电；
*              速度环（2026-09-15）：FF(LUT 前馈) + PI 反馈，20ms 节拍，Q8 定点；
*              反馈经事件总线订阅 EVT_SPEED_UPDATED（motor_on_event 缓存实测转速）
*******************************************************************************/
#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>
#include "event_bus.h"

/* 速度环诊断快照（shell pid 命令；字段为最近一次 PID 计算值） */
typedef struct {
  uint16_t target_rpm;       /* 闭环目标 */
  uint16_t ref_rpm;          /* 参考轨迹（ramp，1000RPM/s） */
  uint16_t meas_rpm;         /* 实测转速缓存（EVT_SPEED_UPDATED） */
  int16_t  ff_permille;      /* FF 分量（‰，LUT） */
  int16_t  p_permille;       /* P 分量（‰） */
  int16_t  i_permille;       /* I 分量（‰） */
  int16_t  i_acc_permille;   /* 积分累计（‰，±SPEED_LOOP_I_MAX） */
  uint16_t duty_permille;    /* 当前应用占空比（‰） */
} motor_pid_info_t;

void     motor_init(void);                      /* PD6/TIM1 配置，初始占空比 0 */
void     motor_ops(void);                       /* 5ms：斜坡推进 + 闭环（内部 4 分频 20ms） */
void     motor_on_event(evt_type_t type, uint16_t val);   /* 订阅回调：只缓存实测转速 */

void     motor_set_target(uint16_t rpm);        /* 白名单接口（仅 app_state 调用；不清积分，A14 步进平滑防抖动） */
void     motor_stop(void);                      /* 受控停转：输出归零、参考轨迹斜坡衰减到 0（A9 显示） */
void     motor_halt(void);                      /* 立即停转并清零参考轨迹（倒计时归零路径） */
uint16_t motor_get_out_rpm(void);               /* 参考轨迹转速（shell 诊断） */
uint16_t motor_get_duty_permille(void);         /* 当前应用占空比（千分比） */
uint16_t motor_get_meas_rpm(void);              /* 实测转速缓存（诊断） */

/* 速度环调试/整定接口（shell pid/pidkp/pidki；上板整定不改固件） */
uint16_t motor_get_kp_q8(void);
uint16_t motor_get_ki_q8(void);
void     motor_set_kp_q8(uint16_t v);           /* 0..4095 */
void     motor_set_ki_q8(uint16_t v);           /* 0..4095 */
void     motor_pid_get_info(motor_pid_info_t *info);

/* 调试接口（shell pwm 命令；标定/驱动链验证用，绕过 LUT 与钳位） */
void     motor_manual_duty(uint16_t permille);  /* 0..1000 直接写 CCR */
void     motor_manual_off(void);                /* 恢复自动占空比 */

#endif /* MOTOR_H */
