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
- 模块清单（`Proj/src/modules/`）：`event_bus`（信号流转核心）、`app_state`、`display`、`button`、`motor`、`speed_sensor`、`temp_sensor`、`timer`、`storage`。
- 芯片级协议放 `Proj/src/drivers/`（ht1623、ads1115、ec11、flash_wear），驱动层不得含业务语义。

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
- **ISR 最短**：SysTick 只做任务计数器递减与标志置位；禁止在 ISR 调用业务接口。
- **volatile 纪律**：仅跨 ISR↔主循环的标志用 `volatile`；纯主循环变量不加。
- **极性集中**：按键/编码器极性（旋钮高有效、START/STOP 低有效）只在 `app_cfg.h` 宏集中定义，禁止散落魔法值。
- **假设常量集中**：A1~A17 相关常量（`RPM_MIN`、`RPM_STEP`、`TIME_HOUR_MAX`、`SETTING_TIMEOUT_MS`、`SPEED_PPR`、到达容差等）集中在 `Proj/inc/app_cfg.h`，确认后单点修改。
- **内存与 Flash 预算**：16KB Flash，编译保持 `-Oz`；新增功能必须验证剩余空间（历史教训：L6406E 溢出）。
- **删除 IOMUX 重映射**：严禁恢复 `GPIO_IOMUX_ChangePin(IOMUX_PIN11, IOMUX_PB5_SEL_PD2)`（会毁掉 SWCLK 烧录口）。

## 3. 目录结构约定

```
Proj/src/main.c                 # 仅：系统初始化 + 任务调度表 + evt_bus_dispatch 编排
Proj/src/hk32f0301mxxc_it.c     # 仅 SysTick
Proj/inc/app_cfg.h              # 周期/假设常量/极性宏
Proj/src/drivers/*.c            # 芯片级驱动（无业务）
Proj/src/modules/*.c            # 9 个业务模块（含 event_bus）
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
- **引脚**：PWM=PD6(TIM1_CH2,AF3,**10kHz**,ARR=4799,高电平=通电)；EC11 A/B/键=PC5/PC3/PC4（**高有效**，下拉）；START=PA2、STOP=PD7（**低有效**，上拉）；屏 HT1623：CS=PD3、RD=PD2、WR=PD1、DATA=PC6、IRQ=PC7；I2C：SCL=PA1、SDA=PA3（AF0），ADS1115 地址 0x48；UART1：TX=PD4、RX=PB4（115200 8N1）。
- **屏**：LVD13083AVTP1 + HT1623（48SEG×8COM，96×4 位 RAM）；命令：SYS_EN=0x01、LCD_ON=0x03、RC32K=0x18（量产切 XTAL32K=0x1C）；RAM nibble 映射 `addr=SEG*2+com/4`、`bit=com%4`，D0 先发；CS 帧间高电平 ≥700ns。
- **ADC 通道**：PT1000/光耦测速分别接 ADS1115 两路模拟输入，**通道归属以硬件飞线修复后实测为准**（R2）；测速硬件链路当前未通（R1）。
- **Flash**：末页 128B（0x08003F80）磨损均衡；16 槽 × 8B `{magic,rpm,h,m,s,rsv}`；空槽=全 0xFF。
- **编译**：无命令行构建，只能 Keil MDK 打开 `Proj/MDK-ARM/Project.uvprojx` 编译（ARMCLANG V6.22）；产物 `Objects/Project.axf` / `Project.hex`。
- **已知硬件遗留**：VLCD 对比度（R5 0Ω→22k~47k）、U46 晶振不起振、个别段位疑虚焊——显示类问题先核对这三项再怀疑软件。

## 6. 关键业务约束（与需求一致）

- 状态机：`STANDBY/RUNNING/PAUSED` + 编辑叠加层（`SPEED→TIME_H→TIME_M→TIME_S` 循环）；5s 无操作自动确认；任意闪烁状态按 START 立即启动。
- 倒计时：**实际转速到达目标后才开始**（|实际−目标|≤50 RPM 持续 1s，A12）；暂停冻结；改过时间重载、未改续用（A11）；归零→停机回待机（A4）。
- 转速：100~2000 RPM（A1），步进 10；转速/H 钳位，M/S 循环（A6）。
- 图标：十字架=PT1000 在位（A5）、时钟=倒计时中、圆=转速>0、▲=运行常亮（A7）、感叹号恒灭（A8）、隔断线常显。
- 参数确认即写 Flash（单击/5s/START，A15）；`MOTOR_DUTY_CLAMP=1` 表示电机强制不转（标定期间），正式交付前必须解除并标定 LUT。
