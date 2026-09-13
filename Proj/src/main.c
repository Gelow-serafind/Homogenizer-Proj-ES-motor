/*******************************************************************************
* @filename:  main.c
*/

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "hk32f0301mxxc_flash.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define PWM_TARGET_FREQ_HZ 10000UL   /* 电机控制频率 10kHz（新板确认值） */

/* 分步实施开关：电机占空比钳位（电机/新 LUT 未标定前保持 0，防止误动作） */
#define MOTOR_DUTY_CLAMP 1U

/* 上电即循环打印 AIN1/AIN0..3（电机光耦反馈 SPEED_SW）调试模式：置 1 后无需 PC 输入，上电直接观察通道值。
   硬件信号问题定位期间使用；正常开发保持 0（shell 交互） */
#define BOOT_TACH_LOOP 0U

/* 任务周期（ms），可通过宏调整每个任务响应时间 */
#define TASK_PERIOD_ENCODER_MS 2U
#define TASK_PERIOD_APP_MS     10U
#define TASK_PERIOD_PWM_MS     5U
#define TASK_PERIOD_LCD_MS     50U
#define TASK_PERIOD_UART_CMD_MS 2U

/* ============ 业务参数（需求 v1-260830，部分待确认，见 doc/修改计划.md §5） ============ */
#define RPM_MIN          100U     /* 转速下限（需求写 "100/200"，未确认，先取 100） */
#define RPM_STEP         10U      /* 旋钮分度：10 RPM */
#define RPM_RAMP_STEP    10U      /* 每 10ms 平滑步进（1000 RPM/s），2000RPM 约 2s 到顶 */
#define RPM_MAX          2000U    /* 转速上限（需求 2000） */
#define TIME_HOUR_MAX    9999U    /* 倒计时小时上限（需求 9999.59.59） */
#define SETTING_TIMEOUT_MS  5000U /* 设置无操作自动确认时间 */
#define PWM_INACTIVE_PERMILLE 0U  /* 停机时 0% 占空比，电机停转 */
#define KEY_DEBOUNCE_TICKS   3U

/* 开始/停止键（新板 V1.0：均低有效，外部 10k 上拉 R13/R11，按下=低） */
#define KEY_START_GPIO_PORT  GPIOA
#define KEY_START_PIN        GPIO_Pin_2   /* PA2 = MCU_SW_SRART */
#define KEY_STOP_GPIO_PORT   GPIOD
#define KEY_STOP_PIN         GPIO_Pin_7   /* PD7 = MCU_SW_STOP  */

#define EC11_GPIO_PORT GPIOC
/* 新板（V1.0 原理图）：A=PC5, B=PC3, 按键=PC4；全部高有效（公共端接 5V，外部 10k 下拉） */
#define EC11_PIN_A GPIO_Pin_5
#define EC11_PIN_B GPIO_Pin_3
#define EC11_PIN_KEY GPIO_Pin_4

/* HT1623 LCD 驱动（新板 V1.0 原理图，4 线串行接口） ---------------------------*/
#define HT1623_GPIO_PORT    GPIOD
#define HT1623_PIN_CS       GPIO_Pin_3   /* PD3 = MCU_LCD_CS_5V   */
#define HT1623_PIN_RD       GPIO_Pin_2   /* PD2 = MCU_LCD_RD_5V（只写模式，常高） */
#define HT1623_PIN_WR       GPIO_Pin_1   /* PD1 = MCU_LCD_WR_5V   */
#define HT1623_PORT_DATA    GPIOC
#define HT1623_PIN_DATA     GPIO_Pin_6   /* PC6 = MCU_LCD_DATA_5V */
#define HT1623_PORT_IRQ     GPIOC
#define HT1623_PIN_IRQ      GPIO_Pin_7   /* PC7 = MCU_LCD_IRQ_5V（本版不用） */

/* 玻璃屏只用到 SEG0..SEG28（玻璃脚 9..37）。
   HT1623 RAM 组织为 96×4 位：7 位地址，每地址 4 位数据（nibble）；
   addr = SEG*2 + COM组偏移，D0=COM0/4，D1=COM1/5，D2=COM2/6，D3=COM3/7 */
#define HT1623_SEG_COUNT    29U
#define HT1623_RAM_NIBBLES  (HT1623_SEG_COUNT * 2U)  /* 58 */

/* HT1623 命令码（9 位命令码，官方数据手册 Rev 2.00）：
   发送格式 = ID(100) + 命令码 9 位 + 末位 X（发 0），即先发 3 位 0x04，再发 cmd<<1 共 9 位。
   无 BIAS 命令：HT1623 固定 1/4 偏压、1/8 占空比 */
#define HT1623_CMD_SYS_DIS  0x00U   /* 100 0000-0000-X 关系统振荡器与 LCD 偏压 */
#define HT1623_CMD_SYS_EN   0x01U   /* 100 0000-0001-X 开系统振荡器 */
#define HT1623_CMD_LCD_OFF  0x02U   /* 100 0000-0010-X 关 LCD 显示 */
#define HT1623_CMD_LCD_ON   0x03U   /* 100 0000-0011-X 开 LCD 显示 */
#define HT1623_CMD_XTAL32K  0x1CU   /* 100 0001-11XX-X 外接 32.768kHz 晶振（板上 U46） */
#define HT1623_CMD_RC32K    0x18U   /* 100 0001-10XX-X 内部 RC 32kHz（当前使用；待 U46 晶振补焊后切回 XTAL32K） */

/* RPM → 占空比 线性回归：rpm = 206.22 * duty - 2604.31
   解出 duty% = (rpm * 100 + 260431) / 20622，0-100 钳位
   LUT 值 = duty% × 10，四舍五入，上限 1000 */
static const uint16_t RpmDutyLUT[][2] = {
  { 0,         0  },
  { 1000,      0  },
  { 2000,      0  },
  { 3000,      0  },
  { 4000,      0  },
  { 5000,    290  },
  { 6000,    330  },
  { 7000,    369  },
  { 8000,    417  },
  { 9000,    466  },
  { 10000,   514  },
  { 11000,   563  },
  { 12000,   621  },
  { 13000,   670  },
  { 14000,   718  },
  { 15000,   767  },
  { 16000,   815  },
  { 17000,   864  },
  { 18000,   912  },
  { 19000,   961  },
  { 20000,   1000  }
};
#define RPM_DUTY_LUT_ENTRIES (sizeof(RpmDutyLUT) / sizeof(RpmDutyLUT[0]))

/* Flash 存储 - 磨损均衡 -----------------------------------------------------*/
/* 使用最后 128 字节页，32 个 uint32 slot（仅低 16 位存转速），免去独立计数位置 */
#define FLASH_SAVE_PAGE_ADDR ((uint32_t)0x08003F80)
#define FLASH_SAVE_SLOTS     16U  /* 末页 128B / 8B 槽 */
#define FLASH_SAVE_SLOT_SIZE 8U   /* 槽布局: {u16 speed,u16 hours} + {u8 min,u8 sec,u16 rsv} */

/* ====================== 业务参数类型与 Flash 持久化 ====================== */

/* 用户设定参数：目标转速 + 倒计时(H-M-S) */
typedef struct {
  uint16_t speed;
  uint16_t hours;
  uint8_t  min;
  uint8_t  sec;
} UserParam_t;

/**
 * @brief  从 Flash 末页向前扫描返回最后一条有效参数（含范围校验）
 * @param  None
 * @return 有效参数；无有效记录返回 {0,0,0,0}
 * @note   空槽特征：槽内两个 u32 均为 0xFFFFFFFF（擦除态）
 */
static UserParam_t LoadParams(void)
{
  UserParam_t p;
  p.speed = 0U; p.hours = 0U; p.min = 0U; p.sec = 0U;

  for (int16_t i = (int16_t)(FLASH_SAVE_SLOTS - 1); i >= 0; i--)
  {
    uint32_t addr = FLASH_SAVE_PAGE_ADDR + (uint32_t)i * FLASH_SAVE_SLOT_SIZE;
    uint32_t w0 = *(volatile uint32_t*)addr;
    uint32_t w1 = *(volatile uint32_t*)(addr + 4U);

    if (w0 != 0xFFFFFFFFU && w1 != 0xFFFFFFFFU && (w1 >> 16) == 0U)
    {
      uint16_t sp = (uint16_t)(w0 & 0xFFFFU);
      uint16_t hr = (uint16_t)(w0 >> 16);
      uint8_t  mi = (uint8_t)(w1 & 0xFFU);
      uint8_t  se = (uint8_t)((w1 >> 8) & 0xFFU);
      if (sp <= RPM_MAX && hr <= TIME_HOUR_MAX && mi < 60U && se < 60U)
      {
        p.speed = sp; p.hours = hr; p.min = mi; p.sec = se;
      }
      return p;   /* 最后一条记录（无论校验是否通过都停止扫描） */
    }
  }
  return p;
}

/**
 * @brief  将用户参数写入 Flash 末页（磨损均衡：找空槽写入，写满擦除整页重写）
 * @param  p: 待保存参数
 * @return None
 */
static void SaveParams(const UserParam_t* p)
{
  uint8_t i;
  uint32_t addr;
  uint32_t w0 = (uint32_t)p->speed | ((uint32_t)p->hours << 16);
  uint32_t w1 = (uint32_t)p->min | ((uint32_t)p->sec << 8);

  for (i = 0; i < FLASH_SAVE_SLOTS; i++)
  {
    addr = FLASH_SAVE_PAGE_ADDR + (uint32_t)i * FLASH_SAVE_SLOT_SIZE;
    if (*(volatile uint32_t*)addr == 0xFFFFFFFFU)
    {
      FLASH_Unlock();
      FLASH_ProgramWord(addr, w0);
      FLASH_ProgramWord(addr + 4U, w1);
      FLASH_Lock();
      return;
    }
  }

  /* 所有槽已用完：擦除整页从头开始 */
  FLASH_Unlock();
  FLASH_ErasePage(FLASH_SAVE_PAGE_ADDR);
  FLASH_ProgramWord(FLASH_SAVE_PAGE_ADDR, w0);
  FLASH_ProgramWord(FLASH_SAVE_PAGE_ADDR + 4U, w1);
  FLASH_Lock();
}

/* Private variables ---------------------------------------------------------*/
static uint16_t PWMPeriod;

/* 输入事件（轮询任务置位，Task_App 消费清零） */
static volatile int32_t EncoderPosition = 0;
static volatile int8_t EncDeltaPending = 0;
static volatile uint8_t KeyShortPressEvent = 0;  /* 旋钮单击 */
static volatile uint8_t StartKeyEvent = 0;       /* 开始键按下沿 */
static volatile uint8_t StopKeyEvent  = 0;       /* 停止键按下沿 */

/* ====================== 业务状态机（需求：STANDBY/RUNNING/PAUSED + 设置叠加层） ====================== */
typedef enum {
  APP_STANDBY = 0,   /* 待机：显示上次参数，可直接启动 */
  APP_RUNNING,       /* 运行：转速 ramp、到达目标后倒计时 */
  APP_PAUSED         /* 暂停：电机停、倒计时冻结、可恢复 */
} AppMode_t;

/* 设置叠加层：可在 STANDBY / RUNNING（电机保持转）/ PAUSED 之上激活 */
typedef enum {
  FIELD_SPEED = 0,   /* 目标转速 */
  FIELD_TIME_H,      /* 倒计时-小时 */
  FIELD_TIME_M,      /* 倒计时-分钟 */
  FIELD_TIME_S       /* 倒计时-秒 */
} SettingField_t;

static volatile AppMode_t AppMode = APP_STANDBY;

/* 用户设定参数（上电从 Flash 恢复 / 设置确认后保存） */
static uint16_t ConfigRpm = 0;
static uint16_t ConfigHours = 0;
static uint8_t  ConfigMin = 0;
static uint8_t  ConfigSec = 0;

/* 运行模型与实际倒计时 */
static uint16_t MotorRpm = 0;          /* 实际输出转速（模型 ramp；P4 接入真实测速后校正） */
static uint16_t RemHours = 0;          /* 倒计时剩余 */
static uint8_t  RemMin = 0;
static uint8_t  RemSec = 0;
static volatile uint8_t RemActive = 0; /* RUNNING 且已到达目标 → 倒计时进行中 */

/* 设置与显示 */
static volatile uint8_t SettingActive = 0;
static SettingField_t  SettingField = FIELD_SPEED;
static uint16_t SettingIdleMs = 0;     /* 设置无操作计时（>=SETTING_TIMEOUT_MS 自动确认） */
static volatile uint8_t BlinkOn = 0;   /* 字段闪烁相位（Task_App 每 500ms 翻转） */
static volatile uint8_t SensorValid = 0; /* PT1000 插入（P4 由 ADS1115 采样更新） */
static volatile uint8_t LcdManual = 0; /* lcd 调试命令置位：暂停业务自动刷新，仅按脏标记送屏 */

/* PWM 状态 */
static volatile uint16_t CurrentDutyPermille = 0U;
static volatile uint8_t DutyChanged = 1U;
static volatile uint8_t MotorPwmManual = 0U; /* 手动占空比模式（标定/驱动链验证用） */

/* ====================== HT1623 显示缓冲与段位映射 ====================== */
static uint8_t LcdRam[HT1623_RAM_NIBBLES] = {0};
static volatile uint8_t LcdDirty = 0U;

typedef struct {
  uint8_t seg;   /* HT1623 SEG 编号 0..28 */
  uint8_t com;   /* 玻璃 COM 编号 1..8（HT1623 COM0..COM7 → RAM 位 = com-1） */
} LcdSeg_t;

#define LSEG(s, c) { (uint8_t)(s), (uint8_t)(c) }

/* 28 位数码管 × 7 段（顺序 a b c d e f g）
   数据来源：LVD13083AVTP1 规格书第 3 页引脚表（玻璃脚 9..37 = SEG0..SEG28） */
static const LcdSeg_t LcdDigitMap[28][7] = {
  /*  1 */ { LSEG(0,1),  LSEG(1,2),  LSEG(1,4),  LSEG(0,4),  LSEG(0,3),  LSEG(0,2),  LSEG(1,3)  },
  /*  2 */ { LSEG(2,1),  LSEG(3,2),  LSEG(3,4),  LSEG(2,4),  LSEG(2,3),  LSEG(2,2),  LSEG(3,3)  },
  /*  3 */ { LSEG(4,1),  LSEG(5,2),  LSEG(5,4),  LSEG(4,4),  LSEG(4,3),  LSEG(4,2),  LSEG(5,3)  },
  /*  4 */ { LSEG(6,1),  LSEG(7,2),  LSEG(7,4),  LSEG(6,4),  LSEG(6,3),  LSEG(6,2),  LSEG(7,3)  },
  /*  5 */ { LSEG(0,5),  LSEG(1,6),  LSEG(1,8),  LSEG(0,8),  LSEG(0,7),  LSEG(0,6),  LSEG(1,7)  },
  /*  6 */ { LSEG(2,5),  LSEG(3,5),  LSEG(3,7),  LSEG(2,8),  LSEG(2,7),  LSEG(2,6),  LSEG(3,6)  },
  /*  7 */ { LSEG(4,5),  LSEG(5,5),  LSEG(5,7),  LSEG(4,8),  LSEG(4,7),  LSEG(4,6),  LSEG(5,6)  },
  /*  8 */ { LSEG(6,5),  LSEG(7,5),  LSEG(7,7),  LSEG(6,8),  LSEG(6,7),  LSEG(6,6),  LSEG(7,6)  },
  /*  9 */ { LSEG(20,5), LSEG(21,5), LSEG(21,7), LSEG(20,8), LSEG(20,7), LSEG(20,6), LSEG(21,6) },
  /* 10 */ { LSEG(22,5), LSEG(23,5), LSEG(23,7), LSEG(22,8), LSEG(22,7), LSEG(22,6), LSEG(23,6) },
  /* 11 */ { LSEG(24,5), LSEG(25,5), LSEG(25,7), LSEG(24,8), LSEG(24,7), LSEG(24,6), LSEG(25,6) },
  /* 12 */ { LSEG(26,5), LSEG(27,5), LSEG(27,7), LSEG(26,8), LSEG(26,7), LSEG(26,6), LSEG(27,6) },
  /* 13 */ { LSEG(8,6),  LSEG(9,6),  LSEG(9,8),  LSEG(8,4),  LSEG(8,8),  LSEG(8,7),  LSEG(9,7)  },
  /* 14 */ { LSEG(10,6), LSEG(11,6), LSEG(11,8), LSEG(10,4), LSEG(10,8), LSEG(10,7), LSEG(11,7) },
  /* 15 */ { LSEG(12,5), LSEG(13,6), LSEG(13,8), LSEG(12,8), LSEG(12,7), LSEG(12,6), LSEG(13,7) },
  /* 16 */ { LSEG(14,5), LSEG(15,5), LSEG(15,7), LSEG(14,8), LSEG(14,7), LSEG(14,6), LSEG(15,6) },
  /* 17 */ { LSEG(9,4),  LSEG(9,3),  LSEG(9,2),  LSEG(9,1),  LSEG(8,1),  LSEG(8,3),  LSEG(8,2)  },
  /* 18 */ { LSEG(11,4), LSEG(11,3), LSEG(11,2), LSEG(11,1), LSEG(10,1), LSEG(10,3), LSEG(10,2) },
  /* 19 */ { LSEG(13,4), LSEG(13,3), LSEG(13,2), LSEG(13,1), LSEG(12,1), LSEG(12,3), LSEG(12,2) },
  /* 20 */ { LSEG(14,4), LSEG(15,4), LSEG(15,2), LSEG(14,1), LSEG(14,2), LSEG(14,3), LSEG(15,3) },
  /* 21 */ { LSEG(16,5), LSEG(17,6), LSEG(17,8), LSEG(16,8), LSEG(16,7), LSEG(16,6), LSEG(17,7) },
  /* 22 */ { LSEG(18,5), LSEG(19,5), LSEG(19,7), LSEG(18,8), LSEG(18,7), LSEG(18,6), LSEG(19,6) },
  /* 23 */ { LSEG(19,1), LSEG(18,2), LSEG(18,4), LSEG(19,4), LSEG(19,3), LSEG(19,2), LSEG(18,3) },
  /* 24 */ { LSEG(17,1), LSEG(16,1), LSEG(16,3), LSEG(17,4), LSEG(17,3), LSEG(17,2), LSEG(16,2) },
  /* 25 */ { LSEG(20,4), LSEG(21,3), LSEG(21,1), LSEG(20,1), LSEG(20,2), LSEG(20,3), LSEG(21,2) },
  /* 26 */ { LSEG(22,4), LSEG(23,3), LSEG(23,1), LSEG(22,1), LSEG(22,2), LSEG(22,3), LSEG(23,2) },
  /* 27 */ { LSEG(24,4), LSEG(25,4), LSEG(25,2), LSEG(24,1), LSEG(24,2), LSEG(24,3), LSEG(25,3) },
  /* 28 */ { LSEG(26,4), LSEG(27,4), LSEG(27,2), LSEG(26,1), LSEG(26,2), LSEG(26,3), LSEG(27,3) },
};

/* 图标段位表：S1..S30（十字架/时钟/感叹号/圆/向上/Rpm/°C/H/m/s/---- 等，具体对应待单点测试确认） */
static const LcdSeg_t LcdIconMap[30] = {
  /* S1  */ LSEG(1,1),   /* S2  */ LSEG(3,1),   /* S3  */ LSEG(5,1),
  /* S4  */ LSEG(28,2),  /* S5  */ LSEG(7,1),   /* S6  */ LSEG(1,5),
  /* S7  */ LSEG(7,8),   /* S8  */ LSEG(23,8),  /* S9  */ LSEG(27,8),
  /* S10 */ LSEG(3,8),   /* S11 */ LSEG(5,8),   /* S12 */ LSEG(9,5),
  /* S13 */ LSEG(10,5),  /* S14 */ LSEG(11,5),  /* S15 */ LSEG(13,5),
  /* S16 */ LSEG(21,8),  /* S17 */ LSEG(23,4),  /* S18 */ LSEG(25,8),
  /* S19 */ LSEG(28,3),  /* S20 */ LSEG(8,5),   /* S21 */ LSEG(15,8),
  /* S22 */ LSEG(12,4),  /* S23 */ LSEG(15,1),  /* S24 */ LSEG(19,8),
  /* S25 */ LSEG(16,4),  /* S26 */ LSEG(21,4),  /* S27 */ LSEG(28,4),
  /* S28 */ LSEG(25,1),  /* S29 */ LSEG(27,1),  /* S30 */ LSEG(28,1),
};

/* 数字字模：bit0=a ... bit6=g；索引 10 = 全灭（消隐） */
static const uint8_t LcdDigitFont[11] = {
  0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU, 0x00U
};

/* UART 命令输入系统 ---------------------------------------------------------*/
#define UART_RX_RING_SIZE  64U
#define CMD_LINE_MAX       64U

typedef void (*CmdCallback_t)(const char* args);

typedef struct {
  const char*    cmd;
  CmdCallback_t  cb;
} CmdEntry_t;

static volatile uint8_t  uartRxRing[UART_RX_RING_SIZE];
static volatile uint8_t  uartRxHead = 0U;
static volatile uint8_t  uartRxTail = 0U;
static uint8_t           cmdLineBuf[CMD_LINE_MAX];
static uint8_t           cmdLineLen = 0U;

/* 注册指令回调函数原型 */
static void Cmd_Help(const char* args);
static void Cmd_SetSpeed(const char* args);
static void Cmd_SetTime(const char* args);
static void Cmd_Start(const char* args);
static void Cmd_Stop(const char* args);
static void Cmd_Pwm(const char* args);
static void Cmd_Ads(const char* args);
static void Cmd_Tach(const char* args);
static void Cmd_Status(const char* args);
static void Cmd_Lcd(const char* args);

/* 指令表：添加新指令只需在此增加 { "name", CallbackFn } 项 */
static const CmdEntry_t cmdTable[] = {
  { "help",   Cmd_Help   },
  { "h",      Cmd_Help   },
  { "speed",  Cmd_SetSpeed },
  { "time",   Cmd_SetTime },
  { "start",  Cmd_Start  },
  { "stop",   Cmd_Stop   },
  { "pause",  Cmd_Stop   },   /* pause 为 stop 别名，语义=暂停 */
  { "pwm",    Cmd_Pwm    },
  { "adc",    Cmd_Ads    },
  { "tach",   Cmd_Tach   },
  { "status", Cmd_Status },
  { "lcd",    Cmd_Lcd    },
  { NULL,     NULL       }
};

/* Private function prototypes -----------------------------------------------*/
static void UART_Config(void);
static void TIM1_PWM_Config(void);
static void TIM1_SetDutyPermille(uint16_t dutyPermille);
static void HT1623_Init(void);

static void EC11_Config(void);
static void I2C_ADS1115_Init(void);
static void Ads_DelayMs(uint32_t ms);
static uint16_t Ads_ReadCh(uint8_t ch);
#if (BOOT_TACH_LOOP == 1U)
static void BootTachLoop(void);
#endif

/* 任务处理函数 */
static void Task_Encoder(void);
static void Task_App(void);
static void Task_PWM(void);
static void Task_LCD(void);
static void Task_UartCmd(void);

/* 业务辅助函数 */
static uint8_t RpmToDutyPercent(uint16_t rpm);
static uint16_t GetTargetDutyPermille(void);
static const char* GetModeName(AppMode_t mode);
static void AppStart(void);
static void AppStop(void);
static void AppCommitSetting(void);
static void AppEnterSetting(void);
static void RefreshDisplay(void);

/* HT1623 驱动与显示层 */
static void HT1623_Delay(void);
static void HT1623_PortInit(void);
static void HT1623_SendBits(uint16_t bits, uint8_t nbits);
static void HT1623_SendCmd(uint8_t cmd);
static void HT1623_WriteRam(uint8_t startAddr, const uint8_t* buf, uint8_t len);
static void Lcd_All(uint8_t on);
static void Lcd_SetSegment(uint8_t seg, uint8_t com, uint8_t on);
static void Lcd_ShowDigit(uint8_t digit, uint8_t num);
static void Lcd_SetIcon(uint8_t icon, uint8_t on);

/* 任务调度结构 */
typedef void (*TaskHandler_t)(void);
typedef struct {
  uint16_t period_ms;    /* 周期 */
  volatile uint16_t cnt; /* 倒计时 */
  TaskHandler_t handler;
} Task_t;

static Task_t tasks[] = {
  { TASK_PERIOD_ENCODER_MS,   0, Task_Encoder  },
  { TASK_PERIOD_UART_CMD_MS,  0, Task_UartCmd  },
  { TASK_PERIOD_APP_MS,       0, Task_App      },
  { TASK_PERIOD_PWM_MS,       0, Task_PWM      },
  { TASK_PERIOD_LCD_MS,       0, Task_LCD      },
};

#define TASKS_COUNT (sizeof(tasks)/sizeof(tasks[0]))

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
int main(void)
{
  /*!< At this stage the microcontroller clock setting is already configured,
       this is done through SystemInit() function which is called from startup
       file (KEIL_startup_hk32f0301mxxc.s) before to branch to application main.
       To reconfigure the default setting of SystemInit() function, refer to
       system_hk32f0301mxxc.c file
     */

  /* UART configuration */
  UART_Config();
  /* PWM配置 */
  TIM1_PWM_Config();
  /* EC11 GPIO配置 */
  EC11_Config();
  /* ADS1115 I2C 配置（PA1/PA3）：AIN1=电机光耦测速 SPEED_SW、AIN0=PT1000 */
  I2C_ADS1115_Init();

  /* HT1623 屏幕配置（新板 4 线串行接口） */
  HT1623_Init();
  /* 业务显示由 Task_LCD（50ms）自动整屏刷新接管；开机即显示恢复的 STANDBY 参数 */

  /* Configure SysTick Timer for 1ms interrupts */
  if (SysTick_Config(SystemCoreClock / 1000))
  {
    /* Capture error */
    while (1);
  }

  /* 从 Flash 恢复上次设置的目标转速与倒计时（H-M-S） */
  {
    UserParam_t p = LoadParams();
    ConfigRpm = p.speed;
    ConfigHours = p.hours;
    ConfigMin = p.min;
    ConfigSec = p.sec;
  }

  printf("\n\r===== Homogenizer Controller =====\n\r");
  printf("HK32F0301MxxxxC shell ready, type 'help'\n\r");
  printf("========================================\n\r\n\r");

  /* Loop until the end of transmission */
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_TC) == RESET)
  {
  }

  printf("Starting scheduler...\n\r");
  printf("homogenizer > ");

#if (BOOT_TACH_LOOP == 1U)
  BootTachLoop();   /* 上电即循环测速打印（不返回；验证完将 BOOT_TACH_LOOP 置 0 恢复 shell） */
#endif

  /* 主循环：轮询任务表，倒计时为0则执行任务并重置倒计时 */
  while(1)
  {
    for (uint32_t i = 0; i < TASKS_COUNT; ++i)
    {
      if (tasks[i].cnt == 0U)
      {
        /* 重置倒计时（关中断保证原子性），随后调用任务处理函数 */
        __disable_irq();
        tasks[i].cnt = tasks[i].period_ms;
        __enable_irq();

        tasks[i].handler();
      }
    }
  }
}
/**
 * @brief  编码器轮询任务：读取 EC11 旋转编码器 A/B 相 + 按键，处理正交解码与长短按去抖
 * @param  None
 * @return None
 */
static void Task_Encoder(void)
{
  static const int8_t transition[16] =
  {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
  };
  static uint8_t prevAB = 0x03;
  static int8_t stepAcc = 0;

  uint8_t a = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_A) != Bit_RESET) ? 1U : 0U;
  uint8_t b = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_B) != Bit_RESET) ? 1U : 0U;
  uint8_t currAB = (uint8_t)((a << 1) | b);
  uint8_t idx = (uint8_t)((prevAB << 2) | currAB);
  int8_t delta = transition[idx];

  if (delta != 0)
  {
    stepAcc += delta;

    if (stepAcc >= 4)
    {
      stepAcc = 0;
      EncoderPosition++;
      if (EncDeltaPending < 120)
      {
        EncDeltaPending++;
      }
    }
    else if (stepAcc <= -4)
    {
      stepAcc = 0;
      EncoderPosition--;
      if (EncDeltaPending > -120)
      {
        EncDeltaPending--;
      }
    }
  }

  prevAB = currAB;

  /* ---- 旋钮按键 EC11 KEY（PC4，高有效 按下=高；单击=确认/切换字段） ---- */
  /* 内部语义：raw=0 表示按下；在释放沿产生一次单击事件 */
  {
    static uint8_t st = 1U;   /* 0=pressed（按键状态） */
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_KEY) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 释放沿 → 单击 */
        {
          KeyShortPressEvent = 1U;
        }
      }
    }
    else
    {
      db = 0U;
    }
  }

  /* ---- 开始键（PA2，低有效 按下=低，外部上拉 R13）---- */
  {
    static uint8_t st = 1U;   /* 1=未按，0=按下 */
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(KEY_START_GPIO_PORT, KEY_START_PIN) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 按下沿 → 开始事件（单次，不重复） */
        {
          StartKeyEvent = 1U;
        }
      }
    }
    else
    {
      db = 0U;
    }
  }

  /* ---- 停止键（PD7，低有效 按下=低，外部上拉 R11）---- */
  {
    static uint8_t st = 1U;
    static uint8_t db = 0U;
    uint8_t raw = (GPIO_ReadInputDataBit(KEY_STOP_GPIO_PORT, KEY_STOP_PIN) == Bit_RESET) ? 1U : 0U;

    if (raw != st)
    {
      if (++db >= KEY_DEBOUNCE_TICKS)
      {
        db = 0U;
        st = raw;
        if (st == 1U)          /* 按下沿 → 停止事件 */
        {
          StopKeyEvent = 1U;
        }
      }
    }
    else
    {
      db = 0U;
    }
  }
}

/* ====================== 业务状态机（STEP3） ====================== */

static volatile uint8_t TimeEditedInSetting = 0; /* 设置期间是否改过时间（PAUSED 恢复时据此重载） */

/**
 * @brief  将当前设定参数写入 Flash（设置确认 / 启动 / 停止时调用）
 * @param  None
 * @return None
 */
static void AppCommitSetting(void)
{
  UserParam_t p;
  p.speed = ConfigRpm;
  p.hours = ConfigHours;
  p.min = ConfigMin;
  p.sec = ConfigSec;
  SaveParams(&p);
}

/**
 * @brief  进入设置叠加层（任一非设置状态单击旋钮触发）
 * @param  None
 * @return None
 */
static void AppEnterSetting(void)
{
  if (SettingActive != 0U)
  {
    return;
  }
  SettingActive = 1U;
  SettingField = FIELD_SPEED;
  SettingIdleMs = 0U;
  TimeEditedInSetting = 0U;
}

/**
 * @brief  开始/继续运行：设置确认并退出 → RUNNING，装载或续用倒计时
 * @param  None
 * @return None
 */
static void AppStart(void)
{
  if (SettingActive != 0U)          /* 设置中按开始：先确认写入并退出设置 */
  {
    AppCommitSetting();
    SettingActive = 0U;
    SettingIdleMs = 0U;
  }
  if (AppMode == APP_RUNNING)
  {
    return;
  }

  if (ConfigRpm == 0U)
  {
    ConfigRpm = RPM_MIN;            /* 无设定速度时给最小转速 */
  }

  if (AppMode != APP_PAUSED)
  {
    /* 全新启动：倒计时从设定时间装载 */
    RemHours = ConfigHours; RemMin = ConfigMin; RemSec = ConfigSec;
  }
  else if (TimeEditedInSetting != 0U)
  {
    /* 暂停后改过时间：按新设定重载倒计时（需求：改了参数按新参数重新运行） */
    RemHours = ConfigHours; RemMin = ConfigMin; RemSec = ConfigSec;
  }
  /* 否则：PAUSED 且未改时间 → 保留冻结的倒计时继续 */

  RemActive = 0U;                   /* 到达目标转速后置位开始递减 */
  AppMode = APP_RUNNING;
  DutyChanged = 1U;
}

/**
 * @brief  停止/暂停：RUNNING→PAUSED（倒计时冻结）；设置中则确认并退出
 * @param  None
 * @return None
 */
static void AppStop(void)
{
  if (SettingActive != 0U)
  {
    AppCommitSetting();
    SettingActive = 0U;
    SettingIdleMs = 0U;
  }
  if (AppMode == APP_RUNNING)
  {
    AppMode = APP_PAUSED;
    RemActive = 0U;                 /* 冻结：保留 RemHours/Min/Sec 供恢复 */
    DutyChanged = 1U;               /* MotorRpm 将 ramp 到 0 */
  }
}

/**
 * @brief  按当前编辑字段应用一次旋钮增量（单位步进）
 * @param  dir: +1 增加 / -1 减少
 * @return None
 */
static void ApplyKnobDelta(int8_t dir)
{
  switch (SettingField)
  {
    case FIELD_SPEED:
      if (dir > 0)
      {
        ConfigRpm += RPM_STEP;
        if (ConfigRpm > RPM_MAX) ConfigRpm = RPM_MAX;
      }
      else
      {
        if (ConfigRpm > RPM_MIN + RPM_STEP) ConfigRpm -= RPM_STEP;
        else ConfigRpm = RPM_MIN;   /* 下限钳位（需求未定义 0 档） */
      }
      break;

    case FIELD_TIME_H:
      if (dir > 0)
      {
        if (ConfigHours < TIME_HOUR_MAX) ConfigHours++;
      }
      else if (ConfigHours > 0U)
      {
        ConfigHours--;
      }
      TimeEditedInSetting = 1U;
      break;

    case FIELD_TIME_M:
      ConfigMin = (dir > 0) ? (uint8_t)((ConfigMin + 1U) % 60U)
                            : (uint8_t)((ConfigMin + 59U) % 60U);
      TimeEditedInSetting = 1U;
      break;

    case FIELD_TIME_S:
      ConfigSec = (dir > 0) ? (uint8_t)((ConfigSec + 1U) % 60U)
                            : (uint8_t)((ConfigSec + 59U) % 60U);
      TimeEditedInSetting = 1U;
      break;

    default: break;
  }
}

/**
 * @brief  倒计时每秒节拍（仅 RemActive 时调用）
 * @param  None
 * @return None
 */
static void CountdownTick(void)
{
  if (RemSec > 0U)
  {
    RemSec--;
  }
  else if (RemMin > 0U)
  {
    RemMin--; RemSec = 59U;
  }
  else if (RemHours > 0U)
  {
    RemHours--; RemMin = 59U; RemSec = 59U;
  }
  else
  {
    /* 倒计时归零：停止电机并保留设定参数（归零行为待确认，暂按此实现） */
    RemActive = 0U;
    AppMode = APP_STANDBY;
    MotorRpm = 0U;
    DutyChanged = 1U;
  }
}

/**
 * @brief  应用逻辑任务：四态状态机 + 设置编辑 + 转速 ramp + 倒计时
 * @param  None
 * @return None
 */
static void Task_App(void)
{
  int8_t delta = 0;
  uint8_t click = 0U;
  uint8_t start = 0U;
  uint8_t stop = 0U;
  static uint16_t blinkMs = 0U;
  static uint16_t secMs = 0U;

  __disable_irq();
  delta = EncDeltaPending;    EncDeltaPending = 0;
  click = KeyShortPressEvent; KeyShortPressEvent = 0U;
  start = StartKeyEvent;      StartKeyEvent = 0U;
  stop  = StopKeyEvent;       StopKeyEvent = 0U;
  __enable_irq();

  /* ---- 按键事件（优先级：开始 > 停止 > 单击/旋钮） ---- */
  if (start != 0U)
  {
    AppStart();
  }
  else if (stop != 0U)
  {
    AppStop();
  }
  else if (SettingActive != 0U)
  {
    if (click != 0U)   /* 单击：SPEED → TIME_H → TIME_M → TIME_S → SPEED 循环 */
    {
      SettingField = (SettingField_t)(((int)SettingField + 1) % 4);
      SettingIdleMs = 0U;
    }
    if (delta != 0U)   /* 旋钮：调整当前编辑字段 */
    {
      while (delta > 0) { ApplyKnobDelta(1);  delta--; }
      while (delta < 0) { ApplyKnobDelta(-1); delta++; }
      SettingIdleMs = 0U;
      DutyChanged = 1U;   /* RUNNING 下转速实时作用于电机 */
    }
  }
  else if (click != 0U)
  {
    AppEnterSetting();      /* 任一非设置状态单击 → 进入设置 */
  }

  /* ---- 10ms 时基：闪烁相位 500ms（供 LCD 字段闪烁） ---- */
  blinkMs += TASK_PERIOD_APP_MS;
  if (blinkMs >= 500U)
  {
    blinkMs = 0U;
    BlinkOn ^= 1U;
  }

  /* ---- 设置无操作 5s 自动确认写入 ---- */
  if (SettingActive != 0U)
  {
    SettingIdleMs += TASK_PERIOD_APP_MS;
    if (SettingIdleMs >= SETTING_TIMEOUT_MS)
    {
      AppCommitSetting();
      SettingActive = 0U;
      SettingIdleMs = 0U;
    }
  }

  /* ---- 电机模型 ramp：RUNNING→目标 ConfigRpm，其余→0 ---- */
  {
    uint16_t target = (AppMode == APP_RUNNING) ? ConfigRpm : 0U;
    if (MotorRpm != target)
    {
      if (MotorRpm < target)
      {
        MotorRpm += RPM_RAMP_STEP;
        if (MotorRpm > target) MotorRpm = target;
      }
      else
      {
        if (MotorRpm > RPM_RAMP_STEP) MotorRpm -= RPM_RAMP_STEP;
        else MotorRpm = 0U;
        if (MotorRpm < target) MotorRpm = target;
      }
      DutyChanged = 1U;
    }
  }

  /* ---- 倒计时：RUNNING 下到达目标后启动；每秒递减；归零停止 ---- */
  if (AppMode == APP_RUNNING)
  {
    if (RemActive == 0U)
    {
      /* 首次到达目标转速且设定了时间 → 启动倒计时 */
      if ((MotorRpm == ConfigRpm) && (ConfigRpm > 0U) &&
          ((ConfigHours | ConfigMin | ConfigSec) != 0U))
      {
        RemActive = 1U;
      }
    }
    else
    {
      secMs += TASK_PERIOD_APP_MS;
      if (secMs >= 1000U)
      {
        secMs = 0U;
        CountdownTick();
      }
    }
  }
}

/* PWM任务：在DutyChanged被置位时应用新的占空比 */
/**
 * @brief  PWM 任务：当 DutyChanged 置位时计算并应用新的占空比
 * @param  None
 * @return None
 */
static void Task_PWM(void)
{
  if (MotorPwmManual != 0U)
  {
    return;   /* 手动标定模式：不覆盖 CCR（用 pwm off 恢复自动） */
  }
  if (DutyChanged)
  {
    CurrentDutyPermille = GetTargetDutyPermille();
    TIM1_SetDutyPermille(CurrentDutyPermille);
    DutyChanged = 0U;
  }
}

/* UART 命令输入任务：2ms 轮询，接收完整行后解析执行 */
/**
 * @brief  解析并执行一条 UART 命令（从 cmdTable 查表匹配）
 * @param  line: 完整命令行字符串
 * @return None
 */
static void UART_ProcessCommandLine(const char* line)
{
  const char* p = line;
  while (*p == ' ') p++;
  if (*p == '\0') return;

  const char* args = p;
  while (*args != '\0' && *args != ' ') args++;
  uint8_t cmdLen = (uint8_t)(args - p);
  while (*args == ' ') args++;

  for (const CmdEntry_t* entry = cmdTable; entry->cmd != NULL; entry++)
  {
    if (strncmp(p, entry->cmd, cmdLen) == 0 && entry->cmd[cmdLen] == '\0')
    {
      entry->cb(args);
      return;
    }
  }

  printf("\r\nUnknown: %s\r\n", line);
}

/**
 * @brief  轮询 UART 接收寄存器与环形缓冲，提取字符并进行行编辑和命令解析
 * @param  None
 * @return None
 */
static void UART_PollRx(void)
{
  /* 将 UART 硬件接收到的字节全部读入环形缓冲 */
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_RXNE) == SET)
  {
    uint8_t ch = (uint8_t)UART_ReceiveData(EVAL_COM1);
    uint8_t next = (uint8_t)((uartRxHead + 1U) % UART_RX_RING_SIZE);
    if (next != uartRxTail)
    {
      uartRxRing[uartRxHead] = ch;
      uartRxHead = next;
    }
  }

  /* 从环形缓冲提取字符进行行编辑和解析 */
  while (uartRxTail != uartRxHead)
  {
    uint8_t ch = uartRxRing[uartRxTail];
    uartRxTail = (uint8_t)((uartRxTail + 1U) % UART_RX_RING_SIZE);

    if (ch == '\n' || ch == '\r')
    {
      /* 跳过紧随其后的连续换行符（兼容 \r\n / \n\r / 单个 \r / 单个 \n） */
      while (uartRxTail != uartRxHead)
      {
        uint8_t nxt = uartRxRing[uartRxTail];
        if (nxt == '\n' || nxt == '\r')
          uartRxTail = (uint8_t)((uartRxTail + 1U) % UART_RX_RING_SIZE);
        else
          break;
      }

      /* 处理收到的行（可能为空行） */
      cmdLineBuf[cmdLineLen] = '\0';
      UART_ProcessCommandLine((const char*)cmdLineBuf);
      cmdLineLen = 0U;

      /* 无论是否空行，都打印下一个提示符 */
      printf("\r\nhomogenizer > ");
    }
    else if (ch == '\b' || ch == 0x7F)
    {
      /* 退格：擦除终端上前一个字符 */
      if (cmdLineLen > 0U)
      {
        cmdLineLen--;
        printf("\b \b");
      }
    }
    else if (cmdLineLen < CMD_LINE_MAX - 1U)
    {
      /* 普通字符：存入缓冲区并回显（远程 echo，不依赖终端本地 echo 设置） */
      cmdLineBuf[cmdLineLen++] = ch;
      printf("%c", (char)ch);
    }
  }
}

/**
 * @brief  UART 命令输入轮询任务（2ms 周期）
 * @param  None
 * @return None
 */
static void Task_UartCmd(void)
{
  UART_PollRx();
}

/* ======================== 命令回调函数实现 ======================== */

/**
 * @brief  列出所有可用 UART 命令
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Help(const char* args)
{
  (void)args;
  printf("\r\nAvailable commands:\r\n");
  for (const CmdEntry_t* entry = cmdTable; entry->cmd != NULL; entry++)
  {
    printf("  %s\r\n", entry->cmd);
  }
}

/**
 * @brief  通过 UART 设置目标转速（speed 命令回调）
 * @param  args: 转速值字符串
 * @return None
 */
static void Cmd_SetSpeed(const char* args)
{
  int val = 0;

  while (*args == ' ') args++;
  while (*args >= '0' && *args <= '9')
  {
    val = val * 10 + (*args - '0');
    args++;
  }
  if (val < 0) val = 0;
  if (val > (int)RPM_MAX) val = RPM_MAX;

  ConfigRpm = (uint16_t)val;
  AppCommitSetting();                       /* speed 命令=直接确认写入 */
  printf("\r\nSpeed set to %u RPM\r\n", (unsigned)ConfigRpm);
}

/**
 * @brief  通过 UART 设置倒计时（time 命令回调：time <h> <m> <s>）
 * @param  args: "H M S" 三个十进制数
 * @return None
 */
static void Cmd_SetTime(const char* args)
{
  int h = 0, m = 0, s = 0;
  const char* p = args;

  while (*p == ' ') p++;
  while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
  while (*p == ' ') p++;
  while (*p >= '0' && *p <= '9') { m = m * 10 + (*p - '0'); p++; }
  while (*p == ' ') p++;
  while (*p >= '0' && *p <= '9') { s = s * 10 + (*p - '0'); p++; }

  if (h < 0) h = 0;
  if (h > (int)TIME_HOUR_MAX) h = TIME_HOUR_MAX;
  if (m < 0) m = 0;
  if (m > 59) m = 59;
  if (s < 0) s = 0;
  if (s > 59) s = 59;

  ConfigHours = (uint16_t)h;
  ConfigMin = (uint8_t)m;
  ConfigSec = (uint8_t)s;
  AppCommitSetting();                       /* time 命令=直接确认写入 */
  printf("\r\nTime set to %u:%02u:%02u\r\n", (unsigned)ConfigHours,
         (unsigned)ConfigMin, (unsigned)ConfigSec);
}

/**
 * @brief  通过 UART 启动/继续（start 命令回调）
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Start(const char* args)
{
  (void)args;
  AppStart();
  printf("\r\nState -> %s\r\n", GetModeName(AppMode));
}

/**
 * @brief  通过 UART 停止/暂停（stop/pause 命令回调）
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Stop(const char* args)
{
  (void)args;
  AppStop();
  printf("\r\nState -> %s\r\n", GetModeName(AppMode));
}

/**
 * @brief  手动 PWM 占空比调试命令：pwm <0-1000> 或 pwm off
 *         标定/驱动链验证用：直接写 TIM1 CCR，绕过占空比钳位与 LUT
 *         0=停，1000=100% 导通（PWM 高电平=电机通电）。pwm off 恢复自动（钳位 0%）。
 * @param  args: 千分比 0-1000 或 off
 * @return None
 */
static void Cmd_Pwm(const char* args)
{
  int val = 0;
  const char* p = args;

  while (*p == ' ') p++;

  if (strncmp(p, "off", 3U) == 0)
  {
    MotorPwmManual = 0U;
    DutyChanged = 1U;   /* 恢复自动：Task_PWM 立即写回自动占空比（当前钳位 0%） */
    printf("\r\nPWM: auto restored (duty clamped to 0%%)\r\n");
    return;
  }

  while (*p >= '0' && *p <= '9')
  {
    val = val * 10 + (*p - '0');
    p++;
  }
  if (val < 0) val = 0;
  if (val > 1000) val = 1000;

  MotorPwmManual = 1U;
  TIM1_SetDutyPermille((uint16_t)val);
  printf("\r\nPWM: manual duty = %d/1000 (%d.%u%%)\r\n",
         val, val / 10, (unsigned)(val % 10));
}

/**
 * @brief  读 ADS1115 两通道原始值（信号/接线验证用）
 * @param  args: 未使用
 * @return None
 */
static void Cmd_Ads(const char* args)
{
  (void)args;
  uint16_t a0 = Ads_ReadCh(0U);
  uint16_t a1 = Ads_ReadCh(1U);
  printf("\r\nADS: AIN0(PT1000)=%u  AIN1(SPEED_SW)=%u  (0xFFFF=bus err)\r\n",
         (unsigned)a0, (unsigned)a1);
}

/**
 * @brief  连续采样 AIN1 并统计电平跳变（手转电机数脉冲用）
 *         tach <n>：采样 n 次（默认 100），H 判据 raw>13000（≈2.5V）
 * @param  args: 采样次数
 * @return None
 */
static void Cmd_Tach(const char* args)
{
  const char* p = args;
  uint32_t cnt = 100U;
  uint32_t i, edges = 0U, hcnt = 0U, lcnt = 0U;
  uint16_t v, vmin = 0xFFFFU, vmax = 0U;
  uint8_t lvl, prev = 0xFFU;

  while (*p == ' ') p++;
  {
    uint32_t d = 0U;
    while (*p >= '0' && *p <= '9')
    {
      d = d * 10U + (uint32_t)(*p - '0');
      p++;
    }
    if (d > 0U && d < 2000U) cnt = d;
  }

  printf("\r\nTach: AIN1 x%lu (H if raw>13000, 每跳变打一行)\r\n",
         (unsigned long)cnt);
  for (i = 0U; i < cnt; i++)
  {
    v = Ads_ReadCh(1U);
    if (v != 0xFFFFU)
    {
      if (v < vmin) vmin = v;
      if (v > vmax) vmax = v;
      lvl = (v > 13000U) ? 1U : 0U;
      if (lvl) hcnt++; else lcnt++;
      if ((prev != 0xFFU) && (lvl != prev))
      {
        edges++;
        printf("  edge#%lu raw=%u %s\r\n",
               (unsigned long)edges, (unsigned)v, lvl ? "H" : "L");
      }
      prev = lvl;
    }
    Ads_DelayMs(2U);
  }
  printf("Tach done: edges=%lu high=%lu low=%lu min=%u max=%u\r\n",
         (unsigned long)edges, (unsigned long)hcnt, (unsigned long)lcnt,
         (unsigned)vmin, (unsigned)vmax);
}

/**
 * @brief  打印当前系统状态（模式、目标转速、实际转速、占空比）
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Status(const char* args)
{
  (void)args;
  uint8_t dutyPct = RpmToDutyPercent(MotorRpm);

  printf("\r\nMode:%-8s | Set:%4u | Motor:%4u | Duty:%3u%% | Rem:%u:%02u:%02u%s\r\n",
         GetModeName(AppMode),
         (unsigned)ConfigRpm,
         (unsigned)MotorRpm,
         (unsigned)dutyPct,
         (unsigned)RemHours, (unsigned)RemMin, (unsigned)RemSec,
         (RemActive != 0U) ? " (active)" : "");
}

/**
 * @brief  LCD 命令：lcd all | lcd off | lcd d1-28 | lcd s1-30
 *         all=全亮（点亮测试），off=全灭，d<n>=第 n 位数码位置显示 8，
 *         s<n>=点亮第 n 个图标（用于核对位置/图标映射）
 * @param  args: 子命令字符串
 * @return None
 */
static void Cmd_Lcd(const char* args)
{
  int val = 0;
  const char* p;

  while (*args == ' ') args++;

  /* auto：恢复业务自动刷新（退出 lcd 手动调试模式） */
  if (strncmp(args, "auto", 4U) == 0)
  {
    LcdManual = 0U;
    LcdDirty = 1U;
    printf("\r\nLCD: auto refresh on\r\n");
    return;
  }

  if (strncmp(args, "all", 3U) == 0)
  {
    LcdManual = 1U;
    Lcd_All(1U);
    printf("\r\nLCD: all segments on (manual)\r\n");
    return;
  }

  if (strncmp(args, "off", 3U) == 0)
  {
    LcdManual = 1U;
    Lcd_All(0U);
    printf("\r\nLCD: all segments off (manual)\r\n");
    return;
  }

  if ((args[0] == 'd' || args[0] == 'D'))
  {
    p = args + 1;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (val < 1 || val > 28)
    {
      printf("\r\nLCD: digit out of range 1-28\r\n");
      return;
    }
    LcdManual = 1U;
    Lcd_All(0U);
    Lcd_ShowDigit((uint8_t)val, 8U);
    printf("\r\nLCD: digit %d = 8 (manual)\r\n", val);
    return;
  }

  if ((args[0] == 's' || args[0] == 'S'))
  {
    p = args + 1;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (val < 1 || val > 30)
    {
      printf("\r\nLCD: icon out of range 1-30\r\n");
      return;
    }
    LcdManual = 1U;
    Lcd_All(0U);
    Lcd_SetIcon((uint8_t)val, 1U);
    printf("\r\nLCD: icon S%d on (manual)\r\n", val);
    return;
  }

  printf("\r\nLCD usage: lcd auto | lcd all | lcd off | lcd d1-28 | lcd s1-30\r\n");
}

/* ====================== 业务显示：字段布局 / 图标绑定 / 整屏刷新 ====================== */

/* 玻璃 28 个数码位的业务字段布局（推断初值，实测玻璃丝印后校准改此表即可）：
   位1-4 SetSpeed | 位5-12 SetTime(H4:M2:S2) | 位13-16 InstSpeed |
   位17-20 InstTemp(带小数点, P4 接入) | 位21-28 RemTime(H4:M2:S2) */
typedef enum {
  FLD_SET_SPEED = 0,
  FLD_SET_TIME,
  FLD_INST_SPEED,
  FLD_INST_TEMP,
  FLD_REM_TIME,
  FLD_COUNT
} LcdField_t;

typedef struct {
  uint8_t first;   /* 起始 digit 1..28 */
  uint8_t len;     /* 位数（时间字段 H=4 M=2 S=2 递增排布） */
} LcdFieldLayout_t;

static const LcdFieldLayout_t FieldLayout[FLD_COUNT] = {
  {  1, 4 },   /* FLD_SET_SPEED  位 1-4   */
  {  5, 8 },   /* FLD_SET_TIME   位 5-12  */
  { 13, 4 },   /* FLD_INST_SPEED 位 13-16 */
  { 17, 4 },   /* FLD_INST_TEMP  位 17-20 */
  { 21, 8 }    /* FLD_REM_TIME   位 21-28 */
};

/* 业务图标 → S 编号绑定（0=未绑定不亮；用 "lcd s1-30" 实测后填入对应编号即可）
   需求右上角图标：十字架=传感器插入、时钟=倒计时、感叹号=本版永灭、
   圆=电机转、▲=运行常亮（▲语义待确认） */
typedef enum {
  ICON_SENSOR = 0,   /* 十字架 */
  ICON_CLOCK,        /* 时钟 */
  ICON_WARN,         /* 感叹号 */
  ICON_MOTOR,        /* 圆 */
  ICON_ARROW         /* ▲ */
} IconRole_t;

static const uint8_t IconBind[5] = { 0U, 0U, 0U, 0U, 0U };

/**
 * @brief  按业务角色点亮/熄灭图标（未绑定时无操作）
 * @param  role: IconRole_t
 * @param  on: 1=亮 0=灭
 * @return None
 */
static void SetRoleIcon(uint8_t role, uint8_t on)
{
  uint8_t s = (role < 5U) ? IconBind[role] : 0U;
  if (s != 0U)
  {
    Lcd_SetIcon(s, on);
  }
}

/**
 * @brief  将某字段 len 个 digit 全部消隐
 * @param  first: 起始 digit
 * @param  len: 位数
 * @return None
 */
static void DrawBlankDigits(uint8_t first, uint8_t len)
{
  for (uint8_t i = 0U; i < len; i++)
  {
    Lcd_ShowDigit((uint8_t)(first + i), 10U);
  }
}

/**
 * @brief  右对齐显示数字，前导消隐；value==0 仅末位显示 0
 * @param  first: 起始 digit（1..28）
 * @param  len: 位宽
 * @param  value: 数值
 * @return None
 */
static void DrawNumberRL(uint8_t first, uint8_t len, uint32_t value)
{
  uint8_t buf[8];
  uint8_t n = 0U;
  uint32_t v = value;

  DrawBlankDigits(first, len);

  if (v == 0U)
  {
    Lcd_ShowDigit((uint8_t)(first + len - 1U), 0U);
    return;
  }
  while ((v != 0U) && (n < len))
  {
    buf[n++] = (uint8_t)(v % 10U);
    v /= 10U;
  }
  for (uint8_t i = 0U; i < n; i++)
  {
    Lcd_ShowDigit((uint8_t)(first + len - 1U - i), buf[i]);
  }
}

/**
 * @brief  两位数字显示（补零，0-99）
 * @param  first: 起始 digit
 * @param  v: 数值 0-99
 * @return None
 */
static void DrawTwoDigits(uint8_t first, uint8_t v)
{
  Lcd_ShowDigit(first, (uint8_t)(v / 10U));
  Lcd_ShowDigit((uint8_t)(first + 1U), (uint8_t)(v % 10U));
}

/**
 * @brief  绘制时间字段（H4 右对齐消隐 + M2 补零 + S2 补零）
 * @param  first: 字段起始 digit
 * @param  h/m/s: 时分秒
 * @return None
 */
static void DrawTimeField(uint8_t first, uint16_t h, uint8_t m, uint8_t s)
{
  DrawNumberRL(first, 4U, h);
  DrawTwoDigits((uint8_t)(first + 4U), m);
  DrawTwoDigits((uint8_t)(first + 6U), s);
}

/**
 * @brief  整屏业务刷新：按当前状态/参数重建 LcdRam 缓冲（闪烁字段按 BlinkOn）
 * @param  None
 * @return None
 * @note   由 Task_LCD 每 50ms 调用；手动 lcd 调试模式（LcdManual=1）下不调用
 */
static uint8_t LcdRamPrev[HT1623_RAM_NIBBLES]; /* 上次送屏缓冲（用于变更检测） */

static void RefreshDisplay(void)
{
  uint8_t f;

  memset(LcdRam, 0U, HT1623_RAM_NIBBLES);   /* 重建缓冲（先不置脏，末尾统一判定） */
  LcdDirty = 0U;

  /* ---- 图标 ---- */
  SetRoleIcon(ICON_MOTOR, (MotorRpm > 0U) ? 1U : 0U);
  SetRoleIcon(ICON_CLOCK, (RemActive != 0U) ? 1U : 0U);
  SetRoleIcon(ICON_ARROW, (AppMode == APP_RUNNING) ? 1U : 0U);
  SetRoleIcon(ICON_WARN, 0U);
  SetRoleIcon(ICON_SENSOR, (SensorValid != 0U) ? 1U : 0U);

  /* ---- Set Speed：编辑态（FIELD_SPEED）闪烁，否则常显 ---- */
  if (!(SettingActive && SettingField == FIELD_SPEED && BlinkOn == 0U))
  {
    DrawNumberRL(FieldLayout[FLD_SET_SPEED].first,
                 FieldLayout[FLD_SET_SPEED].len, ConfigRpm);
  }

  /* ---- Set Time：H/M/S 分区闪烁（当前编辑子字段） ---- */
  f = FieldLayout[FLD_SET_TIME].first;
  if (!(SettingActive && SettingField == FIELD_TIME_H && BlinkOn == 0U))
  {
    DrawNumberRL(f, 4U, ConfigHours);
  }
  if (!(SettingActive && SettingField == FIELD_TIME_M && BlinkOn == 0U))
  {
    DrawTwoDigits((uint8_t)(f + 4U), ConfigMin);
  }
  if (!(SettingActive && SettingField == FIELD_TIME_S && BlinkOn == 0U))
  {
    DrawTwoDigits((uint8_t)(f + 6U), ConfigSec);
  }

  /* ---- Instant Speed：P4 接入真实测速后替换为实测值（当前为模型转速） ---- */
  DrawNumberRL(FieldLayout[FLD_INST_SPEED].first,
               FieldLayout[FLD_INST_SPEED].len, MotorRpm);

  /* ---- Instant Temp：PT1000 插入才显示（SensorValid 由 P4 采样更新；当前未接入→熄灭） ---- */
  if (SensorValid != 0U)
  {
    DrawNumberRL(FieldLayout[FLD_INST_TEMP].first,
                 FieldLayout[FLD_INST_TEMP].len, 8888U);  /* 占位：P4 后改实时温度 */
  }

  /* ---- Remaining Time：STANDBY=0；RUNNING/PAUSED=冻结/递减值 ---- */
  DrawTimeField(FieldLayout[FLD_REM_TIME].first, RemHours, RemMin, RemSec);

  /* 缓冲内容有变化才置脏（避免每 50ms 全量串行写屏阻塞主循环） */
  if (memcmp(LcdRam, LcdRamPrev, HT1623_RAM_NIBBLES) != 0)
  {
    memcpy(LcdRamPrev, LcdRam, HT1623_RAM_NIBBLES);
    LcdDirty = 1U;
  }
}

/**
 * @brief  LCD 业务刷新任务（50ms）：
 *         自动模式：重建整屏缓冲并写屏；手动调试模式：仅按脏标记送屏
 * @param  None
 * @return None
 */
static void Task_LCD(void)
{
  if (LcdManual != 0U)
  {
    if (LcdDirty != 0U)
    {
      HT1623_WriteRam(0U, LcdRam, HT1623_RAM_NIBBLES);
      LcdDirty = 0U;
    }
    return;
  }

  RefreshDisplay();
  if (LcdDirty != 0U)
  {
    HT1623_WriteRam(0U, LcdRam, HT1623_RAM_NIBBLES);
    LcdDirty = 0U;
  }
}

/* RPM → 占空比(%) 查表线性插值（表值 ×10，插值后四舍五入） */
/**
 * @brief  将 RPM 转速通过查表线性插值转换为 PWM 占空比百分比
 * @param  rpm: 电机转速（RPM）
 * @return 占空比百分比（0-100）
 */
static uint8_t RpmToDutyPercent(uint16_t rpm)
{
  uint8_t i;

  if (rpm <= RpmDutyLUT[0][0])
    return (uint8_t)((RpmDutyLUT[0][1] + 5U) / 10U);

  for (i = 1; i < RPM_DUTY_LUT_ENTRIES; i++)
  {
    if (rpm <= RpmDutyLUT[i][0])
    {
      uint16_t rLo = RpmDutyLUT[i - 1][0];
      uint16_t rHi = RpmDutyLUT[i][0];
      uint16_t dLo = RpmDutyLUT[i - 1][1];  /* ×10 */
      uint16_t dHi = RpmDutyLUT[i][1];       /* ×10 */

      if (rHi == rLo) return (uint8_t)((dLo + 5U) / 10U);
      {
        uint16_t dutyX10 = dLo + (uint16_t)(((uint32_t)(dHi - dLo) * (uint32_t)(rpm - rLo)) / (rHi - rLo));
        return (uint8_t)((dutyX10 + 5U) / 10U);
      }
    }
  }

  return (uint8_t)((RpmDutyLUT[RPM_DUTY_LUT_ENTRIES - 1][1] + 5U) / 10U);
}

/**
 * @brief  根据应用模式和实际转速计算目标 PWM 占空比（千分比）
 * @param  None
 * @return 占空比千分比（0-1000，0 表示电机停转，1000 表示满功率）
 */
static uint16_t GetTargetDutyPermille(void)
{
#if (MOTOR_DUTY_CLAMP == 1U)
  /* 电机/新 LUT 未标定前占空比强制为 0，电机不动作 */
  return PWM_INACTIVE_PERMILLE;
#else
  if (AppMode == APP_RUNNING)
  {
    uint8_t dutyPct = RpmToDutyPercent(MotorRpm);
    return (uint16_t)dutyPct * 10U;
  }
  return PWM_INACTIVE_PERMILLE;
#endif /* MOTOR_DUTY_CLAMP */
}

/**
 * @brief  返回应用模式枚举对应的可读字符串名称
 * @param  mode: 应用模式（APP_STANDBY / APP_RUNNING / APP_PAUSED）
 * @return 模式名称字符串
 */
static const char* GetModeName(AppMode_t mode)
{
  if (mode == APP_RUNNING)
  {
    return "RUNNING";
  }
  if (mode == APP_PAUSED)
  {
    return "PAUSED";
  }
  return "STANDBY";
}

/**
  * @brief  Configure EC11 pins on PC5/PC3/PC4 as pull-down inputs (active high, 新板)
  * @param  None
  * @retval None
  */
static void EC11_Config(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);

  /* EC11 A/B/KEY：PC5/PC3/PC4，高有效（外部 10k 下拉），下拉输入 */
  GPIO_InitStructure.GPIO_Pin = EC11_PIN_A | EC11_PIN_B | EC11_PIN_KEY;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);

  /* 开始键 PA2 / 停止键 PD7：低有效（外部 10k 上拉），内部上拉输入 */
  GPIO_InitStructure.GPIO_Pin = KEY_START_PIN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(KEY_START_GPIO_PORT, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = KEY_STOP_PIN;
  GPIO_Init(KEY_STOP_GPIO_PORT, &GPIO_InitStructure);
}

/* ====================== ADS1115（I2C 0x48）最小驱动 ======================
   新板：PA1=SCL / PA3=SDA（AF0，外部 R14/R20 4.7k 上拉）
   AIN0=PT1000 温度分压、AIN1=电机光耦测速 SPEED_SW（R16 1k 上拉）
   仅实现单次转换轮询，用于驱动链/信号验证；P4 再扩展连续采样 */
#define ADS_I2C          I2C
#define ADS_I2C_TIMING   0x00201D2BU   /* 借 EVAL sEE 时序(48MHz SYSCLK) */
#define ADS_ADDR_W       0x90U         /* 0x48<<1 写 */
#define ADS_ADDR_R       0x91U         /* 0x48<<1|1 读 */
#define ADS_REG_CONV     0x00U
#define ADS_REG_CONFIG   0x01U
/* 配置：OS=启动单次转换 + MUX + PGA=±6.144V(000,允许0~5V) + 单次模式 + DR=860SPS + 比较器关 */
#define ADS_CFG_MUX_AIN0 0x4000U
#define ADS_CFG_MUX_AIN1 0x5000U
#define ADS_CFG_SINGLE   0x0100U
#define ADS_CFG_DR_860   0x00E0U

/**
 * @brief  I2C + ADS1115 初始化（新板 PA1/PA3，AF0）
 * @param  None
 * @return None
 */
static void I2C_ADS1115_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  I2C_InitTypeDef I2C_InitStructure;

  RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
  RCC_APBPeriph1ClockCmd(RCC_APBPeriph1_I2C, ENABLE);

  GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_0);
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_0);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_3;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
  I2C_InitStructure.I2C_AnalogFilter = I2C_AnalogFilter_Enable;
  I2C_InitStructure.I2C_DigitalFilter = 0x00;
  I2C_InitStructure.I2C_OwnAddress1 = 0x00;
  I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
  I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
  I2C_InitStructure.I2C_Timing = ADS_I2C_TIMING;
  I2C_Init(ADS_I2C, &I2C_InitStructure);
  I2C_Cmd(ADS_I2C, ENABLE);
}

/**
 * @brief  等待 I2C 标志（带超时）
 * @param  flag: I2C_ISR_* 标志
 * @return 0=OK 1=超时
 */
static uint8_t Ads_WaitFlag(uint32_t flag)
{
  uint32_t t = 0xFFFFFU;
  while (I2C_GetFlagStatus(ADS_I2C, flag) == RESET)
  {
    if (--t == 0U) return 1U;
  }
  return 0U;
}

/**
 * @brief  写 ADS1115 寄存器：pointer + 2 字节数据（触发单次转换）
 * @param  reg: 寄存器地址
 * @param  val: 16 位配置值
 * @return 0=OK 1=失败
 */
static uint8_t Ads_WriteReg(uint8_t reg, uint16_t val)
{
  uint8_t buf[3];
  buf[0] = reg;
  buf[1] = (uint8_t)(val >> 8);
  buf[2] = (uint8_t)(val & 0xFFU);

  I2C_TransferHandling(ADS_I2C, ADS_ADDR_W, 3U, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
  for (uint8_t i = 0U; i < 3U; i++)
  {
    if (Ads_WaitFlag(I2C_ISR_TXIS)) return 1U;
    I2C_SendData(ADS_I2C, buf[i]);
  }
  if (Ads_WaitFlag(I2C_ISR_STOPF)) return 1U;
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return 0U;
}

/**
 * @brief  写 ADS1115 指针寄存器（后续读转换结果用）
 * @param  reg: 指针目标寄存器
 * @return 0=OK 1=失败
 */
static uint8_t Ads_SetPointer(uint8_t reg)
{
  I2C_TransferHandling(ADS_I2C, ADS_ADDR_W, 1U, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
  if (Ads_WaitFlag(I2C_ISR_TXIS)) return 1U;
  I2C_SendData(ADS_I2C, reg);
  if (Ads_WaitFlag(I2C_ISR_STOPF)) return 1U;
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return 0U;
}

/**
 * @brief  读 2 字节转换结果（需先 SetPointer(CONV)）
 * @param  None
 * @return 16 位转换值；失败返回 0xFFFF
 */
static uint16_t Ads_ReadConv(void)
{
  uint16_t v = 0xFFFFU;

  I2C_TransferHandling(ADS_I2C, ADS_ADDR_R, 2U, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);
  for (uint8_t i = 0U; i < 2U; i++)
  {
    if (Ads_WaitFlag(I2C_ISR_RXNE)) return v;
    if (i == 0U)
    {
      v = (uint16_t)((uint16_t)I2C_ReceiveData(ADS_I2C) << 8);
    }
    else
    {
      v |= I2C_ReceiveData(ADS_I2C);
    }
  }
  Ads_WaitFlag(I2C_ISR_STOPF);
  I2C_ClearFlag(ADS_I2C, I2C_ICR_STOPCF);
  return v;
}

/**
 * @brief  粗延时（约 ms 级，用于等待 ADS1115 转换完成）
 * @param  ms: 毫秒数
 * @return None
 */
static void Ads_DelayMs(uint32_t ms)
{
  volatile uint32_t i;
  volatile uint32_t n = ms;
  while (n > 0U)
  {
    for (i = 0U; i < 9000U; i++)
    {
      (void)i;
    }
    n--;
  }
}

/**
 * @brief  读取指定通道一次单次转换结果（AIN0=PT1000, AIN1=光耦测速）
 * @param  ch: 0=AIN0, 1=AIN1
 * @return 原始 ADC 值（0xFFFF=错误）
 */
static uint16_t Ads_ReadCh(uint8_t ch)
{
  static const uint16_t mux[4] = { 0x4000U, 0x5000U, 0x6000U, 0x7000U }; /* AIN0..3 vs GND */
  uint16_t cfg;

  if (ch > 3U)
  {
    return 0xFFFFU;
  }
  cfg = (uint16_t)(0x8000U | ADS_CFG_SINGLE | ADS_CFG_DR_860 | mux[ch]); /* OS=1 触发单次转换 */

  if (Ads_WriteReg(ADS_REG_CONFIG, cfg)) return 0xFFFFU;  /* 触发单次转换 */
  Ads_DelayMs(10U);                                       /* 等转换完成（860SPS≈1.16ms，余量给足） */
  if (Ads_SetPointer(ADS_REG_CONV)) return 0xFFFFU;
  return Ads_ReadConv();
}

#if (BOOT_TACH_LOOP == 1U)
/**
 * @brief  上电即循环打印 AIN1 电机反馈（死循环，无需 PC 输入）
 *         静止无输出；电平跳变打印 E<边沿号>；每 256 次采样(~1s)打印一次统计
 * @param  None
 * @return None（不返回）
 */
static void BootTachLoop(void)
{
  uint16_t v[4];
  uint8_t i;

  printf("\r\n[TACH BOOT LOOP] 4 通道轮询（每行约 0.2s）：转动光栅看哪一路 conv 大幅跳变\r\n");
  while (1U)
  {
    for (i = 0U; i < 4U; i++)
    {
      v[i] = Ads_ReadCh(i);
    }
    printf("\r\nAIN0=%5u AIN1=%5u AIN2=%5u AIN3=%5u%s",
           (unsigned)v[0], (unsigned)v[1], (unsigned)v[2], (unsigned)v[3],
           ((v[0] | v[1] | v[2] | v[3]) == 0xFFFFU) ? " (err)" : "");
    Ads_DelayMs(150U);
  }
}
#endif /* BOOT_TACH_LOOP */

/* ====================== HT1623 驱动实现 ====================== */

/**
 * @brief  HT1623 位操作延时（@48MHz 约 8µs，WR 半周期；时钟频率 ~60kHz，满足 4~300kHz 与脉宽≥1.67µs）
 * @param  None
 * @return None
 */
static void HT1623_Delay(void)
{
  for (volatile uint32_t i = 0; i < 96U; ++i)
  {
  }
}

/**
 * @brief  HT1623 通信 GPIO 初始化：PD3=CS, PD2=RD, PD1=WR, PC6=DATA 推挽输出；
 *         CS 空闲高（低有效片选），RD 常高（只写模式），WR 空闲高。PC7=IRQ 不用
 * @param  None
 * @return None
 */
static void HT1623_PortInit(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

  GPIO_InitStructure.GPIO_Pin = HT1623_PIN_CS | HT1623_PIN_RD | HT1623_PIN_WR;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(HT1623_GPIO_PORT, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = HT1623_PIN_DATA;
  GPIO_Init(HT1623_PORT_DATA, &GPIO_InitStructure);

  /* 初始空闲电平 */
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_RD, Bit_SET);
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
  GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
}

/**
 * @brief  按 MSB 优先发送 nbits 位（数据在 WR 上升沿被锁存）
 * @param  bits: 位序列（3 位 ID、9 位命令码或 7 位地址）
 * @param  nbits: 位长
 * @return None
 */
static void HT1623_SendBits(uint16_t bits, uint8_t nbits)
{
  uint16_t mask = (uint16_t)(1U << (nbits - 1U));

  while (nbits-- != 0U)
  {
    if ((bits & mask) != 0U)
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_SET);
    }
    else
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
    }
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_RESET);
    HT1623_Delay();
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
    HT1623_Delay();
    mask >>= 1;
  }
}

/**
 * @brief  发送命令：CS=0 → ID(100,3位) → 命令码 9 位（末位 X=0）→ CS=1
 * @param  cmd: 9 位命令码（见 HT1623_CMD_* 宏）
 * @return None
 */
static void HT1623_SendCmd(uint8_t cmd)
{
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_RESET);
  HT1623_SendBits(0x04U, 3U);                      /* ID = 100（命令模式） */
  HT1623_SendBits((uint16_t)(cmd << 1), 9U);       /* 命令码 9 位，末位 X=0 */
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  HT1623_Delay();                                  /* CS 高电平≥tCS(700ns)，复位串行接口 */
  HT1623_Delay();
  HT1623_Delay();
}

/**
 * @brief  发送一个 nibble（4 位 RAM 数据），D0 先发（数据手册 WRITE: ...A6..A0 D0D1D2D3）
 * @param  nib: 低 4 位有效
 * @return None
 */
static void HT1623_SendNibble(uint8_t nib)
{
  for (uint8_t i = 0U; i < 4U; i++)
  {
    if (((nib >> i) & 1U) != 0U)
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_SET);
    }
    else
    {
      GPIO_WriteBit(HT1623_PORT_DATA, HT1623_PIN_DATA, Bit_RESET);
    }
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_RESET);
    HT1623_Delay();
    GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_WR, Bit_SET);
    HT1623_Delay();
  }
}

/**
 * @brief  写 RAM：CS=0 → ID(101,3位) → 7 位起始地址 → 连续写 nibble（地址自动递增）→ CS=1
 * @param  startAddr: 起始 nibble 地址（0..95）
 * @param  buf: 数据缓冲（每元素低 4 位有效）
 * @param  len: nibble 个数
 * @return None
 */
static void HT1623_WriteRam(uint8_t startAddr, const uint8_t* buf, uint8_t len)
{
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_RESET);
  HT1623_SendBits(0x05U, 3U);                      /* ID = 101（写模式） */
  HT1623_SendBits((uint16_t)startAddr, 7U);        /* 7 位地址 A6..A0 */
  while (len-- != 0U)
  {
    HT1623_SendNibble(*buf++);
  }
  GPIO_WriteBit(HT1623_GPIO_PORT, HT1623_PIN_CS, Bit_SET);
  HT1623_Delay();                                  /* CS 高电平≥tCS(700ns)，复位串行接口 */
  HT1623_Delay();
  HT1623_Delay();
}

/**
 * @brief  HT1623 初始化：清 RAM → 选外接 32.768kHz 晶振 → 开系统 → 开显示
 *         （HT1623 固定 1/4 偏压 1/8 占空比，无需 BIAS 命令；外部时钟源下 SYS DIS 无效）
 * @param  None
 * @return None
 */
static void HT1623_Init(void)
{
  HT1623_PortInit();

  Lcd_All(0U);
  HT1623_WriteRam(0U, LcdRam, HT1623_RAM_NIBBLES);

  HT1623_SendCmd(HT1623_CMD_RC32K);    /* 内部 RC 32kHz（调试：若外接晶振 U46 不起振，扫描会冻结在 COM0，仅 COM1 行可显示） */
  HT1623_SendCmd(HT1623_CMD_SYS_EN);    /* 开系统振荡器 */
  HT1623_SendCmd(HT1623_CMD_LCD_ON);    /* 开显示 */
}

/* ====================== 显示层 API ====================== */

/**
 * @brief  全屏开/关（含未使用段位，用于点亮测试）
 * @param  on: 1=全亮，0=全灭
 * @return None
 */
static void Lcd_All(uint8_t on)
{
  memset(LcdRam, (on != 0U) ? 0x0FU : 0x00U, HT1623_RAM_NIBBLES);
  LcdDirty = 1U;
}

/**
 * @brief  设置单个段位（SEG × COM 交点）
 * @param  seg: SEG 编号 0..28
 * @param  com: 玻璃 COM 编号 1..8（对应 HT1623 COM0..COM7）
 * @param  on: 1=亮，0=灭
 * @return None
 */
static void Lcd_SetSegment(uint8_t seg, uint8_t com, uint8_t on)
{
  uint8_t comIdx;
  uint8_t addr;
  uint8_t bit;

  if ((seg >= HT1623_SEG_COUNT) || (com < 1U) || (com > 8U))
  {
    return;
  }

  /* RAM nibble 地址：SEG*2 + COM高组偏移；位：COM0-3 → bit0-3，COM4-7 → bit0-3 */
  comIdx = (uint8_t)(com - 1U);
  addr = (uint8_t)(seg * 2U + comIdx / 4U);
  bit = comIdx & 3U;

  if (on != 0U)
  {
    LcdRam[addr] |= (uint8_t)(1U << bit);
  }
  else
  {
    LcdRam[addr] &= (uint8_t)~(1U << bit);
  }
  LcdDirty = 1U;
}

/**
 * @brief  在指定数码管位置显示数字
 * @param  digit: 1..28
 * @param  num: 0-9，10=消隐
 * @return None
 */
static void Lcd_ShowDigit(uint8_t digit, uint8_t num)
{
  uint8_t segBits;
  uint8_t s;

  if ((digit < 1U) || (digit > 28U))
  {
    return;
  }
  if (num > 10U)
  {
    num = 10U;
  }
  segBits = LcdDigitFont[num];

  for (s = 0U; s < 7U; s++)
  {
    Lcd_SetSegment(LcdDigitMap[digit - 1U][s].seg,
                   LcdDigitMap[digit - 1U][s].com,
                   ((segBits & (uint8_t)(1U << s)) != 0U) ? 1U : 0U);
  }
}

/**
 * @brief  控制图标 S1..S30
 * @param  icon: 1..30
 * @param  on: 1=亮，0=灭
 * @return None
 */
static void Lcd_SetIcon(uint8_t icon, uint8_t on)
{
  if ((icon < 1U) || (icon > 30U))
  {
    return;
  }
  Lcd_SetSegment(LcdIconMap[icon - 1U].seg, LcdIconMap[icon - 1U].com, on);
}

/**
  * @brief  Configure the UART Device
  * @param  None
  * @retval None
  */
static void UART_Config(void)
{
  UART_InitTypeDef UART_InitStructure;

  /* USARTx configured as follow:
  - BaudRate = 115200 baud
  - Word Length = 8 Bits
  - Stop Bit = 1 Stop Bit
  - Parity = No Parity
  - Hardware flow control disabled (RTS and CTS signals)
  - Receive and transmit enabled
  */
  UART_InitStructure.UART_BaudRate = 115200;
  UART_InitStructure.UART_WordLength = UART_WordLength_8b;
  UART_InitStructure.UART_StopBits = UART_StopBits_1;
  UART_InitStructure.UART_Parity = UART_Parity_No;
  UART_InitStructure.UART_Mode = UART_Mode_Rx | UART_Mode_Tx;

  HK_EVAL_COMInit(COM1, &UART_InitStructure);
}

/**
  * @brief  Configure TIM1 for PWM output
  * @param  None
  * @retval None
  */
static void TIM1_PWM_Config(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_OCInitTypeDef TIM_OCInitStructure;

  /* Enable peripheral clocks */
  RCC_APBPeriph2ClockCmd(RCC_APBPeriph2_TIM1, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);

  /* New board (V1.0): TIM1_CH2 is natively on PD6 (TSSOP20 pin3), no IOMUX needed.
     NOTE: the old IOMUX remap (PB5 position -> PD2) MUST be removed, otherwise
     pin11 loses SWCLK and the debug port breaks. */

  /* Configure PD6 as alternate function push-pull (TIM1_CH2) */
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  /* Connect TIM1_CH2 to PD6 (AF3, per TIM1 mapping) */
  GPIO_PinAFConfig(GPIOD, GPIO_PinSource6, GPIO_AF_3);

  /* Time base configuration for 10kHz PWM */
  PWMPeriod = (uint16_t)((SystemCoreClock / PWM_TARGET_FREQ_HZ) - 1U);
  TIM_TimeBaseStructure.TIM_Period = PWMPeriod;
  TIM_TimeBaseStructure.TIM_Prescaler = 0;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

  /* PWM 直出模式：PWM1 + PolarityHigh，CNT<CCR→HIGH=ON，占空比=CCR/ARR */
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;
  TIM_OCInitStructure.TIM_Pulse = 0;  /* Initial 0% duty cycle */
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;
  TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
  TIM_OC2Init(TIM1, &TIM_OCInitStructure);

  /* Enable preload for smooth CCR/ARR updates */
  TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
  TIM_ARRPreloadConfig(TIM1, ENABLE);

  /* Enable TIM1 counter */
  TIM_Cmd(TIM1, ENABLE);

  /* Enable TIM1 Main Output */
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

/**
  * @brief  Set TIM1 CH2 duty by permille (0..1000)
  * @param  dutyPermille: duty ratio in permille (1000 = 100%)
  * @retval None
  */
static void TIM1_SetDutyPermille(uint16_t dutyPermille)
{
  uint32_t compare;

  if (dutyPermille >= 1000U)
  {
    /* CCR > ARR keeps PWM1 output continuously active */
    compare = (uint32_t)PWMPeriod + 1U;
  }
  else
  {
    compare = ((uint32_t)(PWMPeriod + 1U) * dutyPermille) / 1000U;
  }

  TIM_SetCompare2(TIM1, (uint16_t)compare);

  /* Force UEV so new CCR takes effect immediately even with preload enabled */
  TIM_GenerateEvent(TIM1, TIM_EventSource_Update);
}

/**
  * @brief  SysTick interrupt handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  /* 为所有任务倒计时（每1ms调用一次） */
  for (uint32_t i = 0; i < TASKS_COUNT; ++i)
  {
    if (tasks[i].cnt > 0U)
    {
      tasks[i].cnt--;
    }
  }
}

/**
  * @brief  Retargets the C library printf function to the USART.
  * @param  None
  * @retval None
  */
int fputc(int ch, FILE *f)
{
  uint32_t guard;

  /* Place your implementation of fputc here */

  /* e.g. write a character to the USART */
  UART_SendData(EVAL_COM1, (uint8_t) ch);

  /* Loop until transmit data register is empty（带超时防外部串扰把打印卡死） */
  guard = 200000U;
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_TC) == RESET)
  {
    if (--guard == 0U)
    {
      break;
    }
  }

  return ch;
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  err_line: assert_param error line number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif
