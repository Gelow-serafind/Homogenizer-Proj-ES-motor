# AGENTS.md

本文件为 AI 助手（Codex / Claude / Copilot 等）提供仓库导航。**强制约束以 `CLAUDE.md` 为准**（架构守则、编码规范、开发流程）；需求/架构/测试以 `doc/` 文档体系为唯一权威。

## 项目概述

匀浆机电机控制器固件（商用闭源），芯片 HK32F0301MF4P7C（Cortex-M0，48MHz，16KB Flash / 4KB SRAM，TSSOP20，5V 供电）。
2026-09-14 已完成模块化重构：业务不再集中在 `main.c`，改为 `Proj/app/` 应用框架（见 commit f436df7）。

## 构建

- IDE：Keil MDK-ARM 打开 `Proj/MDK-ARM/Project.uvprojx`（ARMCLANG V6.22，保持 `-Oz`，Flash 余量紧张）
- 产物：`Proj/MDK-ARM/Objects/Project.axf`（调试）/ `Project.hex`（烧录）；无命令行构建
- 无 Keil 环境时可用 clang `-fsyntax-only` 做语法级校验（命令模板见仓库提交历史）

## 目录与架构

- `Proj/src/main.c`：仅 `App_Init() + App_Run()`；`hk32f0301mxxc_it.c`：仅异常向量
- `Proj/app/inc`、`Proj/app/src`：9 业务模块 + 4 芯片驱动 + shell 框架
  - 业务：event_bus / app_state / display / button / motor / speed_sensor / temp_sensor / timer / storage
  - 驱动：ht1623 / ads1115 / ec11 / flash_wear
  - 框架：`app.c`（10 任务调度表 + SysTick 1ms）、`uart_cmd.c`（shell）、`app_cfg.h`（周期/常量/引脚集中）
- 通信：事件总线（16 条环形队列）+ 白名单直调（仅 app_state → 模块 get/set）；ISR 禁止投递事件
- Keil 工程新增 .c 文件需同步 `Project.uvprojx`

## 硬件速查（2026-09-15；完整网络表见 `doc/原理图网络连接说明_V1.0.md`）

- PWM：PD6 = TIM1_CH2（10kHz，高电平=通电）；电机 24V 低边斩波；TIM2 空闲（TIM6=测速 µs 时基）
- 旋钮 EC11：A=PC5、B=PC3、KEY=PC4（高有效，外部下拉）；EXTI5/EXTI3 双边沿硬件捕获
- START=PA2（高有效）、STOP=PD7（低有效）；轮询消抖
- 屏 HT1623：CS=PD3、RD=PD2、WR=PD1、DATA=PC6、IRQ=PC7（不用）
- I2C AF0：SCL=PA1、SDA=PA3；ADS1115 0x48；AIN0/AIN1 = PT1000
- UART1：TX=PD4、RX=PB4（115200）；**PB4 与 SPEED_SW 同网络**
- 测速：U15(3P) SPEED_SW → PB4/EXTI4 周期法（TIM6 时基）；**18 计数/圈（已标定）；屏显实测 RPM 已接入**；编码器与 USB-TTL 不可同时接
- SWD：PD5=SWDIO / PB5=SWCLK（勿动；禁止 IOMUX 重映射）
- Flash 存储：末页 128B 磨损均衡（`0x08003F80`，16 槽 × 8B）

## 当前状态与待办（2026-09-15）

- 已上板验证：shell / 显示 / 旋钮编辑 / 启停 / 倒计时归零 / Flash 记忆 / 旋钮 EXTI / 测速链路 + PPR
- 主线待办：速度环已落地（FF+PI 闭环 + A12 到达判定切实测，2026-09-15，待 Keil 编译 + 上板整定 Kp/Ki）→ **PT1000 温度已实装（比值法+查表）+ 数码位已核对（图标 S1/S2/S3 的旧校准记录于 2026-10-08 纠正，见文末）；探头已接入（2026-10-01），切实值显示（TEMP_DISPLAY_PLACEHOLDER=0），待上板精度核对**；倒计时已改"**START 即开始、STOP 即冻结**"（2026-09-17 用户实测决策，取代到达后启动；到达判定保留为信息事件） → 新电机 LUT 实测标定回填（当前为验证映射，`MOTOR_DUTY_CLAMP` 已置 0 2026-09-15）
- 文档：`doc/` 六份体系 + `doc/原理图网络连接说明_V1.0.md`（新增网络表）

## 2026-10-08 UI 修正

- S1=Set Speed 标签常显，S2=时钟（RUNNING 且倒计时 active），S3=感叹号恒灭，S4=圆（RUNNING 且实测>0，暂停即灭），S30=PT1000 在位；无独立运行箭头。
- 旧 S1/S2/S3 “全匹配”记录错误，已按屏规格书第 3 页纠正。详细问题/测试/上板待验见 `doc/审核报告.md` 第 8 节。
- 主机回归：`python3 tests/test_ui.py`；语法检查：`python3 tests/check_syntax.py`；仍需 Keil `-Oz` 编译检查内存余量与烧录复核。
