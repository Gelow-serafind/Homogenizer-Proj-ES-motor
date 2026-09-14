/*******************************************************************************
* @filename:  motor.h
* @brief:     电机控制模块：TIM1_CH2 PWM 输出、RPM-占空比 LUT、转速斜坡（架构第 3.2 节）
* @note       PWM=PD6(TIM1_CH2, AF3)，10kHz，ARR=4799，高电平=通电；
*              转速用输出模型 ramp（1000 RPM/s）；P4 接入真实测速后闭环此处扩展
*******************************************************************************/
#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

void     motor_init(void);                      /* PD6/TIM1 配置，初始占空比 0 */
void     motor_ops(void);                       /* 5ms：斜坡推进 + 占空比应用（内部 2 分频） */

void     motor_set_target(uint16_t rpm);        /* 白名单接口（仅 app_state 调用） */
void     motor_stop(void);                      /* 受控停转：输出模型斜坡衰减到 0（A9 显示） */
void     motor_halt(void);                      /* 立即停转并清零输出模型（倒计时归零路径） */
uint16_t motor_get_out_rpm(void);               /* 输出模型转速（显示/到达判定用） */
uint16_t motor_get_duty_permille(void);         /* 当前应用占空比（千分比） */

/* 调试接口（shell pwm 命令；标定/驱动链验证用，绕过 LUT 与钳位） */
void     motor_manual_duty(uint16_t permille);  /* 0..1000 直接写 CCR */
void     motor_manual_off(void);                /* 恢复自动占空比 */

#endif /* MOTOR_H */
