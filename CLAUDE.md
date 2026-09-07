# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此仓库中工作提供指导。

## 项目概述

匀浆机电机控制器固件，基于 HK32F0301MF4P7C (ARM Cortex-M0)。商用闭源项目。所有应用逻辑集中在单文件 `Proj/src/main.c` 中。

## 构建

- **IDE**：Keil MDK-ARM，打开 `Proj/MDK-ARM/Project.uvprojx`
- **编译器**：ARMCLANG V6.19
- **输出**：`Proj/MDK-ARM/Objects/Project.axf`（调试）、`Project.hex`（烧录）
- **目标芯片**：HK32F0301MF4P7C

无命令行构建方式，只能通过 Keil IDE 编译。

## 架构

### 任务调度器

基于 `Task_t` 数组的协作式多任务调度（`main.c:352-358`）。每个任务有周期（ms）和处理函数。`SysTick_Handler`（1ms）递减各任务的倒计时；主循环轮询，倒计时归零时执行对应处理函数：

| 任务 | 周期 | 职责 |
|------|------|------|
| `Task_Encoder` | 2ms | EC11 A/B 4倍频解码 + 按键消抖/长按检测 |
| `Task_UartCmd` | 2ms | UART 接收轮询、行编辑、命令分发 |
| `Task_App` | 10ms | 状态机：旋钮增量处理、启停、电机转速斜坡 |
| `Task_PWM` | 5ms | `DutyChanged` 置位时将新占空比应用到 TIM1 CH2 |
| `Task_TM1729` | 50ms | LCD 显示缓冲刷新 |

### 状态机

双模式系统（`AppMode_t`）：`APP_MODE_STOPPED` / `APP_MODE_RUNNING`。长按 3 秒切换模式。`ConfigRpm` 为目标转速，`MotorRpm` 为实际输出转速（每 10ms 以 50 RPM 步进逼近目标）。

### 关键数据流

1. EC11 旋钮旋转 → `EncoderPosition` 增量 → `Task_App` 中调整 `ConfigRpm`
2. `ConfigRpm` 变更 → 写入 Flash 持久化（磨损均衡，末页 128 字节，32 个 slot）
3. `ConfigRpm` → `MotorRpm` 斜坡 → `RpmToDutyPercent()` 查表线性插值 → PWM 占空比（反相：100% - duty%）
4. `MotorRpm` 或 `ConfigRpm` → TM1729 5 位数码管显示

### UART 命令系统

环形缓冲区接收（`uartRxRing`，64 字节），支持行编辑和命令表（`cmdTable`）。命令：`help`/`h`、`speed <rpm>`、`start`、`pause`、`status`。提示符：`homogenizer > `。

### 硬件引脚映射

- EC11：PC3 (A)、PC4 (B)、PC5 (KEY，低电平有效)
- PWM：PD2 (TIM1_CH2, 17.57kHz, AF3)
- LCD：PC6 (SCL)、PC7 (SDA) — 软件模拟 TM1729 协议
- UART：USART1 (`EVAL_COM1`)，115200 8N1

### RPM-占空比查表

静态数组 `RpmDutyLUT`（`main.c:110-148`）映射 RPM → 占空比（存储值 ×10，线性插值）。范围：0–25000 RPM。11000 RPM 以下为实测数据，以上为线性外推。

### Flash 磨损均衡

末页 128 字节地址 `0x08003F80`，32 个 slot 各 4 字节。上电时从末向前扫描最后有效记录；变更时写入下一个空闲 slot；写满后整页擦除重新开始。

## SDK 兼容性注意事项

HK32 SDK 中 `Libraries/HK32F0301MxxC_StdPeriph_Driver/inc/hk32f0301mxxc_def.h` 的 `NOP()` 宏必须识别 `__ARMCC_VERSION` 以兼容 ARM Compiler 6——原始代码仅检测 `__CC_ARM`。详见 `Proj/MDK-ARM/README_ARMCOMPILER6_FIX.md`。

## 关键约束

- PWM **反相**输出：占空比存储为 `(100% - 占空比百分比) × 10` 千分比，使用 `TIM_OCMode_PWM2` + `TIM_OCPolarity_Low`。0% 占空比意味着满输出。
- 编码器 4 倍频解码（每个边沿都计数），增量缓冲区有 ±120 饱和限制。
- 按键消抖：3 次稳定判定；长按阈值：3000ms。
- 旋钮步进：1000 RPM（常规），最小 5000 RPM；0 ↔ 5000 之间跳变。
