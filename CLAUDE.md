# CLAUDE.md

本文件为 Claude Code 在 ES 电机驱动板固件仓库中工作提供**强制约束**。任何开发动作（新增需求、修改代码、重构）都必须遵守本文档与下列文档体系，违例视为不合格交付。

## 文档体系（权威顺序）

1. `doc/产品需求简述.md` — 需求原始来源（含 docx 原文）
2. `doc/产品认知摘要.md` — 统一认知 + 假设清单 A1~A17（歧义处理依据）
3. `doc/需求细分拆解.md` — **开发唯一指导**（REQ-XXX，47 条）
4. `doc/详细测试用例.md` — **测试唯一依据**（TC-XXX，57 条，双向跟踪矩阵）
5. `doc/软件架构设计.md` — **架构唯一蓝本**（后续开发不得绕过）
6. `doc/审核报告.md` — 覆盖与一致性审核闭环

冲突裁决：需求文档 > 架构文档 > 既有代码；有歧义必须引用 A 系列假设或新增假设并回填三份文档，**禁止擅自定死或臆造行为**。

## 1. 架构守则（不可违反）

### 1.1 模块归属

- 新需求先落 `REQ`，再按《软件架构设计》第 12 节落点表映射到模块；**禁止**出现无归属模块的业务逻辑（尤其禁止堆在 `main.c`）。
- 模块清单（业务模块在 `Proj/app/src/`，头文件在 `Proj/app/inc/`）：`event_bus`（信号流转核心）、`app_state`、`display`、`button`、`motor`、`speed_sensor`、`temp_sensor`、`timer`、`storage`；框架入口/调度表在 `app.c`，调试 shell 在 `uart_cmd.c`。
- 芯片级协议驱动（ht1623、ads1115、ec11、flash_wear）同样落在 `Proj/app/src/`，驱动层不得含业务语义。

### 1.2 信号流转机制（强制）

- 模块间通信只有两条通路：
  1. **事件总线**：`evt_bus_post()` → 订阅表广播（按钮事件必须同时分发给 app_state 等订阅者）；
  2. **白名单直调**：仅 `app_state` 可调用各模块 get/set 接口（如 `motor_set_target`）。
- 硬性规则：模块之间禁止互相直调；禁止跨模块访问私有变量；订阅回调只读（置标志/刷新缓存），**回调内禁止 post 与长操作**；派生事件在模块自己下一次 `_ops()` 中发布。
- `evt_bus_post` 仅限主循环上下文；**ISR 禁止投递事件**。

### 1.3 模块接口规范

- 每个模块强制实现：`void <mod>_init(void);`（上电初始化，含初始状态/初始画面）与 `void <mod>_ops(void);`（超级循环周期调用）。
- 模块状态封装为模块私有 static 实例（对象），对外只暴露 `_init/_ops` + 文档化 get/set；私有函数一律 `static`。
- 模块自身**不得**出现阻塞延时（`delay` 等待外设），慢速外设用状态推进 + 超时。
- 新加接口必须同步更新《软件架构设计》第 3 节接口签名清单。

## 2. 编码规范

- **语言**：C99（ARMCLANG V6.19+）；Keil 工程加入新 `.c` 文件后必须同步 `Project.uvprojx`。
- **命名**：模块前缀 + 蛇形（`display_set_icon`）；枚举 `xxx_t`；宏全大写。公共符号必须有模块前缀。
- **static 前置声明**：被后置定义调用的 static 函数必须加前置原型（ARMCLANG 会报 implicit declaration，已踩过）。
- **无阻塞**：禁止 `while(flag)` 死等、长 `for` 延时；等待类逻辑交给任务周期状态机。
- **ISR 最短**：SysTick 只做任务计数器递减与标志置位；驱动级 EXTI ISR（先例：ec11 旋钮边沿捕获）只做 GPIO 采样/查表/计数；禁止在 ISR 调用业务接口或投递事件。
- **volatile 纪律**：仅跨 ISR↔主循环的标志用 `volatile`；纯主循环变量不加。
- **极性集中**：按键/编码器极性（旋钮高有效、START/STOP 低有效）只在 `app_cfg.h` 宏集中定义，禁止散落魔法值。
- **假设常量集中**：A1~A17 相关常量（`RPM_MIN`、`RPM_STEP`、`TIME_HOUR_MAX`、`SETTING_TIMEOUT_MS`、`SPEED_PPR`、到达容差等）集中在 `Proj/app/inc/app_cfg.h`，确认后单点修改。
- **内存与 Flash 预算**：16KB Flash，编译保持 `-Oz`；新增功能必须验证剩余空间（历史教训：L6406E 溢出）。
- **删除 IOMUX 重映射**：严禁恢复 `GPIO_IOMUX_ChangePin(IOMUX_PIN11, IOMUX_PB5_SEL_PD2)`（会毁掉 SWCLK 烧录口）。

## 3. 目录结构约定

```
Proj/src/main.c                 # 仅 App_Init() + App_Run()（极简入口）
Proj/src/hk32f0301mxxc_it.c     # 仅异常向量（业务 SysTick 在 app.c）
Proj/app/inc/app_cfg.h          # 周期/假设常量/极性/引脚宏
Proj/app/inc/*.h                # 全部头文件（模块/驱动/shell/app.h）
Proj/app/src/app.c              # 任务调度表 + evt_bus_dispatch 编排（唯一编排点）
Proj/app/src/*.c                # 9 个业务模块 + 4 个芯片驱动 + 调试 shell
Proj/app/src/ht1623.c 等        # 芯片级驱动（无业务）
doc/*.md                        # 六份文档体系（见上）
```

新增文件必须落在对应目录；**禁止**把新逻辑塞进 `main.c` 或 `hk32f0301mxxc_it.c`。

## 4. 新增一条需求的标准开发流程（从 REQ 到模块到 TC）

1. **需求落盘**：在《需求细分拆解.md》新增 `REQ-XXX`（描述/来源/验收标准/优先级/ASSUMPTION 标注）；若引入新歧义，先在《产品认知摘要》登记 `An` 假设。
2. **测试先行**：在《详细测试用例.md》为每条新 REQ 至少新增 1 条 `TC-XXX`（前置/步骤/可判定预期），并同步更新双向跟踪矩阵。
3. **架构落点**：按《软件架构设计》第 12 节确定归属模块；若需要新模块/新接口/新事件，先更新架构文档（模块表、接口签名、订阅表、调度表），后写代码。
4. **实现**：遵守第 1~3 节全部守则；每步保持可编译、行为等价；新事件类型在 `evt_type_t` 注册并登记订阅表。
5. **自测与闭环**：对照 TC 自测（优先 shell/上板）；更新《审核报告》覆盖矩阵；确认无"裸需求"、无"无主用例"、REQ↔模块落点完整。
6. **提交**：变更说明引用 REQ/TC 编号；文档与代码同 commit。

## 5. 硬件事实速查（勿凭记忆修改）

- **MCU**：HK32F0301MF4P7C（Cortex-M0，TSSOP20，5V 供电）；SWD：PD5=SWDIO、PB5=SWCLK（勿动）。
- **引脚**：PWM=PD6(TIM1_CH2,AF3,**10kHz**,ARR=4799,高电平=通电)；EC11 A/B/键=PC5/PC3/PC4（**高有效**，下拉）；START=PA2、STOP=PD7（**低有效**，上拉）；屏 HT1623：CS=PD3、RD=PD2、WR=PD1、DATA=PC6、IRQ=PC7；I2C：SCL=PA1、SDA=PA3（AF0），ADS1115 地址 0x48；UART1：TX=PD4、RX=PB4（115200 8N1；**PB4 与 SPEED_SW 同网络，见下**）。
- **屏**：LVD13083AVTP1 + HT1623（48SEG×8COM，96×4 位 RAM）；命令：SYS_EN=0x01、LCD_ON=0x03、RC32K=0x18（量产切 XTAL32K=0x1C）；RAM nibble 映射 `addr=SEG*2+com/4`、`bit=com%4`，D0 先发；CS 帧间高电平 ≥700ns。
- **ADC/测速**：PT1000 测温实装（2026-09-15，原理图核实）：**VCC_5V—R16(1k)—ADC_PT1000+（CN3 探头）—GND；AIN0=探头节点、AIN1=R19/R18(1k:1k) 基准中点**；**比值法** `R_t=1000×A0/(2×A1−A0)`（免疫电源波动）+ PT1000 分度表查表（0~300℃/5℃步进/线性插值，零浮点；**A=3.862e-3 老分度 2026-10-01 按参考项目统一，原 IEC 60751 3.9083e-3 弃用**）；在位：`2×A1−A0<TEMP_DEN_MIN`；ADS1115 地址 0x48；**SPEED_SW（U15）与 USART1_RX 同一网络 → PB4**（图纸核实 2026-09-14）——编码器与 USB-TTL 不可同时接（双驱动打架），编码器接入时串口 RX 受扰属正常；测速=PB4/EXTI4 周期法（TIM6 4µs 时基，20ms 滤波）——**链路验证通 + 18 计数/圈标定 + 屏显实测转速已接入（2026-09-15）**；**到达判定（A12）已切实测（2026-09-15；2026-09-17 起仅信息事件，倒计时改 START 直启）**：|实际−目标|≤`REACH_TOLERANCE_RPM`(50) 持续 `REACH_HOLD_MS`(1s)。
- **温度显示已收尾**：小数点 S28、℃ S29、标签 S26/27 已实测绑定（`lcd sweep`，2026-09-16）；0.1℃ 全格式生效。**2026-10-01 探头已接入：`TEMP_DISPLAY_PLACEHOLDER=0` 切实值显示（在位 `xxx.x`；拔出整块连标签隐藏，REQ-DISP-06）；置 1 可回退占位 25.8℃（调试/演示备份）。**2026-10-01 温度均值滤波**：8 点滑动平均（`TEMP_AVG_WINDOW=8`，500ms×8=4s 窗，平滑优先，`temp_sensor` 移位平均无除法），抑制 ADS 采样噪声防显示跳字；插/拔清窗、插入首拍满窗装载；`templog` 打印 `Traw/Tavg` 双值。**线性标定（2026-10-01）**：`T_cal=T_raw×TEMP_CAL_GAIN_PPT/1000+TEMP_CAL_OFFSET_T10`（app_cfg.h，默认无修正）。待上板精度核对（REQ-TEMP-01 偏差 ≤±1℃）。**
- **速度环（2026-09-15 落地，上板整定中）**：`motor` = FF(LUT 前馈) + PI 反馈，20ms 节拍（5ms 任务内部 4 分频，与测速同频），Q8 定点；反馈经事件总线订阅 `EVT_SPEED_UPDATED` 缓存（`motor_on_event` 只读，无模块间直调）；参考轨迹=原 ramp（启动无阶跃；**STOP 后重启动按实测转速复位——2026-09-17 修复大电流猛启动**）；积分限幅抗饱和；输出仅限物理边界 0~100%（`SPEED_LOOP_DUTY_MAX`=1000‰，2026-09-15 用户决策：不设人为限制，此前 300‰ 下实测只到 ≈720 RPM 撞顶）；`SPEED_LOOP_EN=0` 回退开环；Kp=64/Ki=6（Q8）为初值，shell `pid/pidkp/pidki` 上板整定后回填 `app_cfg.h`（编码器占用 PB4 时无 shell，面板测 + 改宏重烧）。**屏显转速吸附（2026-10-01）**：Instant Speed 显示 = 二次平滑 + Reach 吸附——RUNNING 下实测连续 4 拍（≈200ms）进入目标 ±5 RPM 即锁定显示目标值，吸附态连续 4 拍偏离超 ±15 RPM 才回实时（切换当拍对齐，无跳变无滞后）；判定用 `speed_sensor` 原始值，PID/A12 不受影响；常量 `DISP_REACH_RPM/DEVIATE_RPM/HOLD_PATS` 在 `app_cfg.h`。
- **Flash**：末页 128B（0x08003F80）磨损均衡；16 槽 × 8B `{magic,rpm,h,m,s,rsv}`；空槽=全 0xFF。
- **编译**：无命令行构建，只能 Keil MDK 打开 `Proj/MDK-ARM/Project.uvprojx` 编译（ARMCLANG V6.22）；产物 `Objects/Project.axf` / `Project.hex`。
- **已知硬件遗留**：VLCD 对比度（R5 0Ω→22k~47k）、U46 晶振不起振、个别段位疑虚焊——显示类问题先核对这三项再怀疑软件。

## 6. 关键业务约束（与需求一致）

- 状态机：`STANDBY/RUNNING/PAUSED` + 编辑叠加层（`SPEED→TIME_H→TIME_M→TIME_S` 循环）；5s 无操作自动确认；任意闪烁状态按 START 立即启动。
- 倒计时：**按 START 立即开始（2026-09-17 用户实测决策变更）、按 STOP 立即冻结**（原为"实际转速到达目标后才开始"）；运行中改时间确认后按新值重载继续；时间设 0 时启动不倒数（不触发归零）；归零→停机回待机（A4）。到达判定（|实际−目标|≤50 RPM 持续 1s）保留为信息事件（不再触发倒计时）。
- 圆图标：RUNNING 且实测转速>0 时亮；暂停/待机立即灭（2026-10-08 用户决策）。Instant Speed 数字仍按 A9 实测衰减。
- 转速：100~2000 RPM（A1），步进 10；转速/H 钳位，M/S 循环（A6）。
- 图标（2026-10-08 按规格书第 3 页纠正）：Set Speed 标签 S1 全状态常显；时钟 S2=RUNNING 且 timer active（零时间运行灭）；圆 S4=RUNNING 且实测转速>0，暂停/待机立即灭；十字架 S30=PT1000 在位；感叹号 S3 本版恒灭（用户再次确认，不新增故障判断）。无独立运行箭头。隔断线 S10~19 常显。display 只渲染 app_state 白名单推送的 display_view_t 快照。
- 参数确认即写 Flash（单击/5s/START，A15）；`MOTOR_DUTY_CLAMP` 已于 2026-09-15 置 0——当前 `RpmDutyLUT` 是**验证用简单映射**（15%~30%，非标定值），P8 需实测回填后再交付。
