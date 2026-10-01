/*******************************************************************************
* @filename:  app_cfg.h
* @brief:     应用层集中配置：任务周期、业务常量（A1~A17 假设）、引脚/极性宏
* @note        1. 假设常量集中于此，客户确认后单点修改（见 doc/产品认知摘要.md A1~A17）
*              2. 按键/编码器/引脚极性只在此定义，禁止散落魔法值
*              3. 本文件只含宏，不依赖任何 SDK 头
*******************************************************************************/
#ifndef APP_CFG_H
#define APP_CFG_H

/* ============ 任务周期（ms，架构第 6 节调度表） ============ */
#define TASK_PERIOD_BUTTON_MS     2U    /* EC11 解码 + 三键消抖 */
#define TASK_PERIOD_UART_MS       2U    /* shell 接收轮询 */
#define TASK_PERIOD_APP_MS       10U    /* 主状态机 */
#define TASK_PERIOD_TIMER_MS     10U    /* 倒计时节拍消费 */
#define TASK_PERIOD_MOTOR_MS      5U    /* 斜坡 + 占空比应用（内部 2 分频 → 10ms 步进） */
#define TASK_PERIOD_SPEED_MS     20U    /* 测速任务周期：周期法平均窗口 + EMA 滤波更新 */
#define TASK_PERIOD_TEMP_MS     500U    /* 温度采样 */
#define TASK_PERIOD_DISPLAY_MS   50U    /* 显示渲染 + 差分写屏 */
#define TASK_PERIOD_STORAGE_MS   10U    /* Flash 分步写（每周期 ≤1 word） */
#define TASK_PERIOD_HOUSEKEEP_MS 1000U  /* 秒节拍（EVT_1S） */

/* ============ 业务常量（A 系列假设；确认后单点修改） ============ */
#define RPM_MIN               100U      /* A1：转速下限（需求"100/200"未定，暂 100） */
#define RPM_MAX              2000U      /* 转速上限（MOT-01） */
#define RPM_STEP               10U      /* A6：旋钮分度 10 RPM */
#define RPM_RAMP_STEP          10U      /* 每 10ms 平滑步进（1000 RPM/s） */
#define TIME_HOUR_MAX        9999U      /* 倒计时小时上限 9999:59:59 */
#define SETTING_TIMEOUT_MS   5000U      /* A15：编辑无操作 5s 自动确认 */
#define BLINK_PERIOD_MS       500U      /* 编辑字段闪烁相位周期 */
#define KEY_DEBOUNCE_TICKS      3U      /* 按键消抖：3 次稳定判定（2ms×3=6ms） */

/* ============ PWM / 电机 ============ */
#define PWM_TARGET_FREQ_HZ 10000UL      /* 电机控制频率 10kHz（硬件确认） */
#define PWM_INACTIVE_PERMILLE   0U      /* 停机占空比 0% */
#define MOTOR_DUTY_CLAMP        0U      /* 1=强制不转（防护）；0=按 LUT 输出（2026-09-15 起验证阶段，LUT=验证映射） */

/* ============ 速度环（REQ-MOT-01：FF(LUT 前馈) + PI 反馈，Q8 定点，2026-09-15） ============ */
#define SPEED_LOOP_EN            1U      /* 1=闭环（FF+PI）；0=回退开环 LUT（对比/标定用） */
#define SPEED_LOOP_PERIOD_DIV    4U      /* motor_ops 5ms × 4 = 20ms PID 节拍（与测速反馈同频） */
#define SPEED_LOOP_KP_Q8        64U      /* P 系数 Q8（=0.25‰/RPM）；上板整定后可改（shell pidkp） */
#define SPEED_LOOP_KI_Q8         6U      /* I 系数 Q8（≈0.023‰/RPM/拍）；上板整定后可改（shell pidki） */
#define SPEED_LOOP_I_MAX       700U      /* 积分限幅（‰，抗饱和；放至可覆盖全输出范围，避免稳态缺口补不满） */
#define SPEED_LOOP_DUTY_MAX   1000U      /* 输出限幅（‰）：物理边界 0~100%，不设人为限制（2026-09-15 用户决策，
                                             原 300‰→500‰ 均撞顶，彻底放开） */

/* ============ A12 到达判定（实测转速数据源，2026-09-15） ============ */
#define REACH_TOLERANCE_RPM      50U      /* |实际−目标| ≤ 50 RPM（A12） */
#define REACH_HOLD_MS          1000U      /* 持续 ≥ 1s（A12） */

/* ============ 屏显转速二次平滑（display 层；不影响 speed_sensor 原始反馈→PID/A12） ============ */
#define DISP_SPEED_JUMP_RPM     30U      /* |原始−显示| > 该值 → 快速跟随（每拍 /2，调速不迟钝） */
#define DISP_SPEED_SS_DIV        8U      /* 稳态平滑：每拍仅跟随 1/8 偏差（50ms 拍，τ≈0.4s；防跳字） */

/* ============ 屏显 Reach 吸附 / 偏离回退（2026-10-01 用户调试决策；判定用 speed_sensor 原始值） ============ */
#define DISP_REACH_RPM            5U     /* RUNNING 下实测连续 N 拍进入目标 ±2 RPM → 吸附：显示锁定目标值（reach） */
#define DISP_DEVIATE_RPM         15U     /* 吸附态中连续 N 拍偏离超过 ±15 RPM 才回退实时显示（偏离）；回到 ±2 内重新吸附 */
#define DISP_REACH_HOLD_PATS      4U     /* 吸附判定去抖：连续 N 拍（50ms/拍）在 ±2 内才锁定显示（防毛刺误锁） */
#define DISP_DEVIATE_HOLD_PATS    4U     /* 回退判定去抖：连续 N 拍（50ms/拍）超 ±15 才回退实时显示（防毛刺误退） */

/* ============ 功能开关（标定/验证阶段） ============ */
#define BOOT_TACH_LOOP          0U      /* 1=上电进入 AIN 通道测速调试循环（不启动 shell） */
#define EC11_USE_EXTI           1U      /* 1=旋钮 PC5/PC3 双边沿 EXTI 捕获（推荐）；0=2ms 轮询回退 */
#define TEMP_DISPLAY_PLACEHOLDER 0U     /* 0=显示真实测量值（探头已接入 2026-10-01；拔出整块隐藏 REQ-DISP-06）；
                                           1=回退固定占位值（段位调试/探头未到时的演示备份） */
#define TEMP_PLACEHOLDER_T10    258U    /* 占位值：25.8℃（仅 TEMP_DISPLAY_PLACEHOLDER=1 时生效） */

/* ============ 引脚定义（新板 V1.0；禁止散落魔法值） ============ */
/* EC11：A=PC5、B=PC3、KEY=PC4；公共端 5V + 外部 10k 下拉 → 高有效 */
#define EC11_GPIO_PORT   GPIOC
#define EC11_PIN_A       GPIO_Pin_5
#define EC11_PIN_B       GPIO_Pin_3
#define EC11_PIN_KEY     GPIO_Pin_4
#define EC11_KEY_ACTIVE_HIGH   1U
/* START=PA2 / STOP=PD7；外部 10k 上拉 → 低有效 */
#define KEY_START_GPIO_PORT GPIOA
#define KEY_START_PIN       GPIO_Pin_2
#define KEY_STOP_GPIO_PORT  GPIOD
#define KEY_STOP_PIN        GPIO_Pin_7
#define KEY_ACTIVE_HIGH     0U

/* HT1623 4 线串行：CS=PD3、RD=PD2、WR=PD1、DATA=PC6、IRQ=PC7（本版不用） */
#define HT1623_GPIO_PORT GPIOD
#define HT1623_PIN_CS    GPIO_Pin_3
#define HT1623_PIN_RD    GPIO_Pin_2
#define HT1623_PIN_WR    GPIO_Pin_1
#define HT1623_PORT_DATA GPIOC
#define HT1623_PIN_DATA  GPIO_Pin_6
#define HT1623_PORT_IRQ  GPIOC
#define HT1623_PIN_IRQ   GPIO_Pin_7

/* 玻璃屏 SEG0..SEG28；HT1623 RAM = 96×4 位（nibble 地址 = SEG*2 + COM组） */
#define HT1623_SEG_COUNT    29U
#define HT1623_RAM_NIBBLES  (HT1623_SEG_COUNT * 2U)   /* 58 个 nibble */

/* ============ ADS1115（I2C 0x48；PT1000 比率法双通道，原理图核实 2026-09-15） ============ */
#define ADS1115_ADDR_W  0x90U      /* 0x48<<1 写 */
#define ADS1115_ADDR_R  0x91U      /* 0x48<<1|1 读 */
#define ADS_CH_TEMP     0U         /* AIN0 = ADC_PT1000+ 节点（R16 1k 上拉 + 探头对地） */
#define ADS_CH_TEMP_REF 1U         /* AIN1 = R19/R18(1k:1k) 基准中点（比率法参考，免疫电源波动） */

/* ============ PT1000 测温（比率法 + 查表，2026-09-15 实装） ============ */
#define TEMP_DEN_MIN     4000U     /* 2×AIN1−AIN0 < 此值 → 探头拔出/断路（悬空≈0；插 25℃≈12106） */

/* ============ 测速 SPEED_SW（U15 速度检测 3P 座；与 USART1_RX 同网络 → PB4，图纸核实 2026-09-14） ============ */
#define SPEED_PPR              18U      /* A10 实测：18 计数/圈（2026-09-15 标定：1圈18、2圈36、7圈126） */
#define SPEED_TICK_US           4U      /* 脉冲时间戳分辨率（TIM6 时基，µs/计数） */
#define SPEED_RPM_EMA_SHIFT     2U      /* RPM 一阶低通系数 α=1/(1<<shift)，20ms 更新一次 */
#define SPEED_STALL_TIMEOUT_MS 200U     /* 无脉冲超时 → 停转归零（100 RPM 周期 33ms 的 6 倍余量） */
#define SPEED_EXTI_LINE        EXTI_Line4
#define SPEED_EXTI_IRQN        EXTI4_IRQn
#define SPEED_EXTI_PORT_SOURCE EXTI_PortSourceGPIOB
#define SPEED_EXTI_PIN_SOURCE  EXTI_PinSource4   /* PB4：已由 COM1 初始化为 UART1_RX，EXTI 挂同脚不重配 */

/* ============ Flash 存储（末页 128B 磨损均衡；格式见 flash_wear.h） ============ */
#define FLASH_SAVE_PAGE_ADDR ((uint32_t)0x08003F80)
#define FLASH_SAVE_SLOTS     16U
#define FLASH_SAVE_SLOT_SIZE 8U

#endif /* APP_CFG_H */
