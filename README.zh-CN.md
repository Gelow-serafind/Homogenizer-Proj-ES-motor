# 匀浆机控制器工程（HK32F0301MF4P7C）

> 本 README 基于 `Proj/src/main.c` 的实际逻辑编写，已剔除与当前实现不符的遗留描述。

## 工程简介

本工程实现了基于 HK32F0301MF4P7C 的匀浆机控制原型。核心逻辑采用：
- `SysTick` 1ms 定时 + 任务数组轮询
- EC11 编码器 A/B 4x 解码与按键短/长按事件
- PWM 控制电机速度
- TM1729 LCD 数码管显示
- UART 串口周期性状态输出

## 主要功能

- 旋钮旋转：调整当前设定值
- 短按：进入设置模式 / 切换设置字段
- 长按（3 秒）：启动/暂停电机
- 运行中：倒计时递减，归零即暂停
- 设置中：速度、分钟、秒钟字段可编辑，5 秒无操作自动退出并提交
- 运行中设置速度：仅在编辑速度字段时，PWM 输出会根据编辑值实时预览

## 运行现象

1. 上电后，MCU 初始化 UART、TIM1 PWM、EC11 GPIO、TM1729，并在 LCD 上显示 `1234` 的初始化测试内容。
2. 主循环按任务周期轮询执行：编码器、应用逻辑、PWM、串口打印、LCD 刷新、空闲任务。
3. 串口每 100ms 输出当前状态行，包括模式、速度、时间、按键、编码器位置。
4. 进入 `SETTING` 模式后，当前字段闪烁；5 秒无操作后自动退出并提交设置。
5. 长按 3 秒：若当前是 `RUNNING`，则切换到 `PAUSED`；否则启动电机进入 `RUNNING`。
6. 运行状态下，`TIM1` PWM 输出按当前运行速度；暂停/待机状态下 PWM 输出为 0。

## 业务逻辑说明

### 输入处理

- `Task_Encoder()` 每 2ms 执行：
  - 读取 `PC3`/`PC4` 编码器 A/B，按 4x 轨迹解码。
  - 读取 `PC5` 按键，做 3tick 硬件消抖。
  - 按键按下保持 3 秒后产生 `KeyLongPressEvent`，松开时产生 `KeyShortPressEvent`。

### 状态机

- `IDLE`：待机状态；PWM 输出为 0。
- `SETTING`：参数设置状态；可编辑速度、分钟、秒钟字段，且编辑时显示闪烁。
- `RUNNING`：电机运行，倒计时每秒递减。
- `PAUSED`：暂停状态，电机停止，保持当前配置。

### 按键行为

- 短按：
  - 若已在 `SETTING`，切换下一个字段：`SPEED -> MIN -> SEC -> SPEED`。
  - 否则进入 `SETTING` 模式。
- 长按：
  - 若当前为 `RUNNING`，执行暂停。
  - 否则执行启动。

### 启动与暂停

- 启动逻辑：
  - 若倒计时为 0，则重置为当前配置的 `ConfigMin:ConfigSec`。
  - `MotorSpeedPercent` 设为当前配置速度。
  - PWM 输出更新。
- 暂停逻辑：
  - `MotorSpeedPercent` = 0，PWM 输出变 0。
  - 倒计时暂停，不再递减。

### 设置模式行为

- 进入设置时，保存当前倒计时备份，避免设置时倒计时继续流失。
- `SETTING` 模式退出后：
  - 若来自 `RUNNING`，保留运行状态；速度编辑时即刻提交速度，时间编辑后退出时更新倒计时。
  - 若来自 `PAUSED` 或 `IDLE`，退出后保持原状态或回到待机。
- 5 秒无操作自动退出并提交设置。

### 倒计时与空闲任务

- `Task_Idle()` 仅在 `RUNNING` 时每秒递减 `CountdownSec`。
- `CountdownSec` 归零时自动调用暂停动作。

## PWM 与显示逻辑

### TIM1 PWM

- PWM 输出使用 `TIM1 CH2`，引脚 `PD2`。
- 目标频率由 `PWM_TARGET_FREQ_HZ = 17570UL` 计算得到。
- `Task_PWM()` 仅在 `DutyChanged` 为真时更新 CCR2，并触发 `Update` 事件。
- PWM 输出规则：
  - `RUNNING`：根据 `MotorSpeedPercent` 输出。
  - `SETTING` 且来自 `RUNNING`：
    - 若当前字段是速度编辑，输出编辑速度预览。
    - 否则保持当前运行速度。
  - 其它状态：输出 0。

### TM1729 显示

- `Task_TM1729()` 每 50ms 刷新一次 LCD。
- 显示规则：
  - `SETTING` + 编辑速度：显示速度并闪烁；转速指示灯点亮。
  - `SETTING` + 编辑时间：显示分钟和秒钟，当前字段闪烁；转速指示灯关闭。
  - 其它状态：显示当前运行速度或 0，并点亮转速指示灯。

## 串口状态输出

- `Task_Print()` 每 100ms 输出一行状态：
  - `MODE`、`SPEED`、`TIME`、`KEY`、`POS`
- 设定模式下会根据闪烁状态隐藏当前字段值。

## Keil 工程构建信息

- 工程文件：`Proj/MDK-ARM/Project.uvprojx`
- 目标芯片：HK32F0301MF4P7C
- 编译器：ARMCLANG V6.19
- 主要分组：CMSIS、User、Driver、Board
- 包含路径：
  - `inc/`
  - `src/`
  - `Libraries/CMSIS/...`
  - `Libraries/HK32F0301MxxC_StdPeriph_Driver/...`
  - `Boards/Board_HK32F0301MF4P7C_EVAL/...`
- 兼容性说明：`NOP()` 宏的兼容判断在 `Libraries/HK32F0301MxxC_StdPeriph_Driver/inc/hk32f0301mxxc_def.h` 中处理，通过检测 `__ARMCC_VERSION` 避免 ARM Compiler 6 不支持的 `asm("nop")` 写法。
- 输出文件：`Objects/Project.axf`、`Objects/Project.hex`
- 支持下载器：ULINK、STLink 等

## 主要硬件连接

- EC11 旋钮：`PC3` / `PC4` / `PC5`
- PWM 输出：`PD2`（`TIM1_CH2`）
- LCD（TM1729）：`PC6` / `PC7`
- 串口调试：`USART1`（`EVAL_COM1`，115200 8N1）

## 运行与调试

1. 使用 Keil 打开 `Proj/MDK-ARM/Project.uvprojx`。
2. 选择目标设备并编译。
3. 下载程序到开发板。
4. 串口连接 `EVAL_COM1`，设置 115200 8N1。
5. 上电后检查 LCD 与串口输出。

## 贡献与开源

本项目为商用闭源项目，归杭州云渡海跨境电商有限公司所有。未经授权不得复制、修改、分发、再许可或销售。

## License

本项目为商业专有软件，不开放源代码。未经授权不得复制、修改、分发、再许可或销售。详情见 `LICENSE` 文件。