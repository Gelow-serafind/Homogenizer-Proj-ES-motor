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

/* 任务周期（ms），可通过宏调整每个任务响应时间 */
#define TASK_PERIOD_ENCODER_MS 2U
#define TASK_PERIOD_APP_MS     10U
#define TASK_PERIOD_PWM_MS     5U
#define TASK_PERIOD_LCD_MS     50U
#define TASK_PERIOD_UART_CMD_MS 2U

/* 业务参数 */
#define RPM_STEP_MIN      5000U    /* 最小运行转速 */
#define RPM_STEP_NORMAL   1000U    /* 旋钮分度值 */
#define RPM_RAMP_STEP     50U      /* 每10ms平滑步进（5000 RPM/s） */
#define RPM_MAX           20000U   /* 最大显示转速 */
#define PWM_INACTIVE_PERMILLE 0U     /* 停机时 0% 占空比，电机停转 */
#define LONG_PRESS_MS          2000U
#define KEY_DEBOUNCE_TICKS     3U

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
#define FLASH_SAVE_SLOTS     32U
#define FLASH_SAVE_SLOT_SIZE 4U

static uint16_t FlashLastSavedRpm = 0xFFFF;

/* 从后往前扫描，返回最后一个有效转速值；无则返回 0
   仅接受高 16 位为 0 的条目（排除旧版本时间数据等残留） */
/**
 * @brief  从 Flash 最后 128 字节页读取上次保存的转速值（从后往前扫描）
 * @param  None
 * @return 上次保存的转速值，若无有效数据则返回 0
 */
static uint16_t LoadLastRpm(void)
{
    for (int16_t i = (int16_t)(FLASH_SAVE_SLOTS - 1); i >= 0; i--)
    {
        uint32_t word = *(volatile uint32_t*)(FLASH_SAVE_PAGE_ADDR + (uint32_t)i * FLASH_SAVE_SLOT_SIZE);
        if (word != 0xFFFFFFFFU && (word >> 16) == 0)
        {
            return (uint16_t)(word & 0xFFFFU);
        }
    }
    return 0;
}

/* 找第一个空位（低 16 位为 0xFFFF）写入；无空位则擦除整页后写入 slot 0 */
/**
 * @brief  将转速值写入 Flash 最后 128 字节页（磨损均衡：找空位写入或擦除整页重写）
 * @param  rpm: 待保存的转速值
 * @return None
 */
static void SaveRpmToFlash(uint16_t rpm)
{
    uint8_t i;
    uint32_t word;
    uint32_t addr;

    for (i = 0; i < FLASH_SAVE_SLOTS; i++)
    {
        addr = FLASH_SAVE_PAGE_ADDR + (uint32_t)i * FLASH_SAVE_SLOT_SIZE;
        word = *(volatile uint32_t*)addr;
        if ((word & 0xFFFFU) == 0xFFFFU)
        {
            FLASH_Unlock();
            FLASH_ProgramWord(addr, (uint32_t)rpm);
            FLASH_Lock();
            return;
        }
    }

    /* 所有 slot 已用完，擦除整页从头开始 */
    FLASH_Unlock();
    FLASH_ErasePage(FLASH_SAVE_PAGE_ADDR);
    FLASH_ProgramWord(FLASH_SAVE_PAGE_ADDR, (uint32_t)rpm);
    FLASH_Lock();
}

/* Private variables ---------------------------------------------------------*/
static uint16_t PWMPeriod;

/* 编码器基础信息 */
static volatile int32_t EncoderPosition = 0;
static volatile int8_t EncDeltaPending = 0;
static volatile uint8_t KeyShortPressEvent = 0;
static volatile uint8_t KeyLongPressEvent = 0;
static volatile uint8_t KeyPressedState = 1U; /* active-low: 0=pressed */

/* 业务数据 */
typedef enum {
  APP_MODE_STOPPED = 0,
  APP_MODE_RUNNING
} AppMode_t;

static volatile AppMode_t AppMode = APP_MODE_STOPPED;

static uint16_t ConfigRpm = 0;    /* 目标转速，旋钮调整此值 */
static uint16_t MotorRpm = 0;     /* 实际输出转速（运行时平滑 ramp） */

static volatile uint16_t CurrentDutyPermille = 0U;
static volatile uint8_t DutyChanged = 1U;

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
static void Cmd_Start(const char* args);
static void Cmd_Pause(const char* args);
static void Cmd_Status(const char* args);
static void Cmd_Lcd(const char* args);

/* 指令表：添加新指令只需在此增加 { "name", CallbackFn } 项 */
static const CmdEntry_t cmdTable[] = {
  { "help",   Cmd_Help   },
  { "h",      Cmd_Help   },
  { "speed",  Cmd_SetSpeed },
  { "start",  Cmd_Start  },
  { "pause",  Cmd_Pause  },
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
static uint16_t LoadLastRpm(void);
static void SaveRpmToFlash(uint16_t rpm);

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

  /* HT1623 屏幕配置（新板 4 线串行接口） */
  HT1623_Init();
  /* 上电全亮测试：验证整屏点亮与对比度，后续用 lcd 命令逐点测试 */
  Lcd_All(1U);

  /* Configure SysTick Timer for 1ms interrupts */
  if (SysTick_Config(SystemCoreClock / 1000))
  {
    /* Capture error */
    while (1);
  }

  /* 从 Flash 恢复上次设置的转速 */
  ConfigRpm = LoadLastRpm();
  FlashLastSavedRpm = ConfigRpm;

  printf("\n\r===== Homogenizer Controller =====\n\r");
  printf("HK32F0301MxxxxC shell ready, type 'help'\n\r");
  printf("========================================\n\r\n\r");

  /* Loop until the end of transmission */
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_TC) == RESET)
  {
  }

  printf("Starting scheduler...\n\r");
  printf("homogenizer > ");

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

  static uint8_t keyStable = 1U;
  static uint8_t keyDebounce = 0;
  static uint16_t keyPressMs = 0U;
  static uint8_t longFired = 0U;

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

  /* 新板按键高有效（外部下拉，按下=高）：读到高电平即按下，下面沿用 0=按下 的内部语义 */
  {
    uint8_t keyRaw = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_KEY) == Bit_RESET) ? 1U : 0U;

    if (keyRaw != keyStable)
    {
      if (++keyDebounce >= KEY_DEBOUNCE_TICKS)
      {
        keyDebounce = 0;
        keyStable = keyRaw;
        KeyPressedState = keyStable;

        if (keyStable == 0U)
        {
          keyPressMs = 0U;
          longFired = 0U;
        }
        else
        {
          if ((longFired == 0U) && (keyPressMs > 0U))
          {
            KeyShortPressEvent = 1U;
          }
          keyPressMs = 0U;
          longFired = 0U;
        }
      }
    }
    else
    {
      keyDebounce = 0;

      if (keyStable == 0U)
      {
        if (keyPressMs < 60000U)
        {
          keyPressMs += TASK_PERIOD_ENCODER_MS;
        }

        if ((longFired == 0U) && (keyPressMs >= LONG_PRESS_MS))
        {
          KeyLongPressEvent = 1U;
          longFired = 1U;
        }
      }
    }
  }
}

/**
 * @brief  应用逻辑任务：处理编码器事件、更新目标转速、运行/停止切换、转速平滑 ramp
 * @param  None
 * @return None
 */
static void Task_App(void)
{
  int8_t delta = 0;
  uint8_t longEvt = 0U;

  __disable_irq();
  delta = EncDeltaPending;
  EncDeltaPending = 0;
  KeyShortPressEvent = 0U;
  longEvt = KeyLongPressEvent;
  KeyLongPressEvent = 0U;
  __enable_irq();

  /* 长按：运行/停止切换 */
  if (longEvt != 0U)
  {
    if (AppMode == APP_MODE_RUNNING)
    {
      AppMode = APP_MODE_STOPPED;
      MotorRpm = 0U;
      DutyChanged = 1U;
    }
    else
    {
      AppMode = APP_MODE_RUNNING;
      /* MotorRpm 从 0 开始 ramp 到 ConfigRpm */
      DutyChanged = 1U;
    }
  }

  /* 转动旋钮：调整目标转速，分度值 1000，0↔5000 跳变 */
  while (delta > 0)
  {
    if (ConfigRpm == 0)
    {
      ConfigRpm = RPM_STEP_MIN;              /* 0 → 5000 */
    }
    else if (ConfigRpm < RPM_STEP_MIN)
    {
      ConfigRpm = RPM_STEP_MIN;
    }
    else
    {
      ConfigRpm += RPM_STEP_NORMAL;
      if (ConfigRpm > RPM_MAX) ConfigRpm = RPM_MAX;
    }
    delta--;
  }

  while (delta < 0)
  {
    if (ConfigRpm <= RPM_STEP_MIN)
    {
      ConfigRpm = 0;                          /* ≤5000 → 0 */
    }
    else
    {
      ConfigRpm -= RPM_STEP_NORMAL;
      if (ConfigRpm < RPM_STEP_MIN) ConfigRpm = 0;
    }
    delta++;
  }

  /* ConfigRpm 变更时保存到 Flash（磨损均衡，仅值不同时写入） */
  if (ConfigRpm != FlashLastSavedRpm)
  {
    SaveRpmToFlash(ConfigRpm);
    FlashLastSavedRpm = ConfigRpm;
  }

  /* 运行时平滑 ramp：MotorRpm 均匀趋近 ConfigRpm */
  if (AppMode == APP_MODE_RUNNING && MotorRpm != ConfigRpm)
  {
    if (MotorRpm < ConfigRpm)
    {
      MotorRpm += RPM_RAMP_STEP;
      if (MotorRpm > ConfigRpm) MotorRpm = ConfigRpm;
    }
    else
    {
      if (MotorRpm > RPM_RAMP_STEP)
        MotorRpm -= RPM_RAMP_STEP;
      else
        MotorRpm = 0;
      if (MotorRpm < ConfigRpm) MotorRpm = ConfigRpm;
    }
    DutyChanged = 1U;
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
  if (val > RPM_MAX) val = RPM_MAX;

  ConfigRpm = (uint16_t)val;
  printf("\r\nSpeed set to %u RPM\r\n", (unsigned)ConfigRpm);
}

/**
 * @brief  通过 UART 启动电机（start 命令回调）
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Start(const char* args)
{
  (void)args;
  if (AppMode != APP_MODE_RUNNING)
  {
    AppMode = APP_MODE_RUNNING;
    DutyChanged = 1U;
    printf("\r\nStart\r\n");
  }
}

/**
 * @brief  通过 UART 暂停电机（pause 命令回调）
 * @param  args: 命令参数（未使用）
 * @return None
 */
static void Cmd_Pause(const char* args)
{
  (void)args;
  if (AppMode == APP_MODE_RUNNING)
  {
    AppMode = APP_MODE_STOPPED;
    MotorRpm = 0U;
    DutyChanged = 1U;
    printf("\r\nPause\r\n");
  }
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

  printf("\r\nMode:%-10s | Target:%5u RPM | Motor:%5u RPM | Duty:%3u%%\r\n",
         GetModeName(AppMode),
         (unsigned)ConfigRpm,
         (unsigned)MotorRpm,
         (unsigned)dutyPct);
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

  if (strncmp(args, "all", 3U) == 0)
  {
    Lcd_All(1U);
    printf("\r\nLCD: all segments on\r\n");
    return;
  }

  if (strncmp(args, "off", 3U) == 0)
  {
    Lcd_All(0U);
    printf("\r\nLCD: all segments off\r\n");
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
    Lcd_All(0U);
    Lcd_ShowDigit((uint8_t)val, 8U);
    printf("\r\nLCD: digit %d = 8\r\n", val);
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
    Lcd_All(0U);
    Lcd_SetIcon((uint8_t)val, 1U);
    printf("\r\nLCD: icon S%d on\r\n", val);
    return;
  }

  printf("\r\nLCD usage: lcd all | lcd off | lcd d1-28 | lcd s1-30\r\n");
}

/* LCD 显示任务：50ms 检查脏标记，按需写入 HT1623 RAM */
/**
 * @brief  HT1623 LCD 显示任务：仅当显示缓冲变更时全量写入（29 字节）
 * @param  None
 * @return None
 */
static void Task_LCD(void)
{
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
  if (AppMode == APP_MODE_RUNNING)
  {
    uint8_t dutyPct = RpmToDutyPercent(MotorRpm);
    return (uint16_t)dutyPct * 10U;
  }
  return PWM_INACTIVE_PERMILLE;
#endif /* MOTOR_DUTY_CLAMP */
}

/**
 * @brief  返回应用模式枚举对应的可读字符串名称
 * @param  mode: 应用模式（APP_MODE_STOPPED / APP_MODE_RUNNING）
 * @return 模式名称字符串
 */
static const char* GetModeName(AppMode_t mode)
{
  if (mode == APP_MODE_RUNNING)
  {
    return "RUNNING";
  }
  return "STOPPED";
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

  GPIO_InitStructure.GPIO_Pin = EC11_PIN_A | EC11_PIN_B | EC11_PIN_KEY;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);
}

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
  /* Place your implementation of fputc here */

  /* e.g. write a character to the USART */
  UART_SendData(EVAL_COM1, (uint8_t) ch);

  /* Loop until transmit data register is empty */
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_TC) == RESET)
  {
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
