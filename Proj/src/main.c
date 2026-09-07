/*******************************************************************************
* @filename:  main.c
* /

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "hk32f0301mxxc_flash.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define PWM_TARGET_FREQ_HZ 17570UL

/* 任务周期（ms），可通过宏调整每个任务响应时间 */
#define TASK_PERIOD_ENCODER_MS 2U
#define TASK_PERIOD_APP_MS     10U
#define TASK_PERIOD_PWM_MS     5U
#define TASK_PERIOD_TM1729_MS  50U
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
#define EC11_PIN_A GPIO_Pin_3
#define EC11_PIN_B GPIO_Pin_4
#define EC11_PIN_KEY GPIO_Pin_5

/* TM1729 (legacy wiring compatible with old project) */
#define TM1729_GPIO_PORT      GPIOC
#define TM1729_PIN_SCL        GPIO_Pin_6
#define TM1729_PIN_SDA        GPIO_Pin_7
#define TM1729_SLAVE_ADDRESS  0x7CU
#define TM1729_ICSET          0xEAU
#define TM1729_BLKCTL         0xF0U
#define TM1729_DISCTL         0xA2U
#define TM1729_MODESET        0xC8U
#define TM1729_APCTL          0xFCU
#define TM1729_ADSET          0x00U
#define TM1729_LCD_BUF_LEN    14U

#define TM1729_COM1 0U
#define TM1729_COM2 1U
#define TM1729_COM3 2U
#define TM1729_COM4 3U

/* 根据用户提供的 LCD 真值表建立玻璃屏引脚映射：PIN5..PIN13 为 9 条 SEG 线 */
#define TM1729_SEG_PIN5   0U
#define TM1729_SEG_PIN6   4U
#define TM1729_SEG_PIN7   8U
#define TM1729_SEG_PIN8   12U
#define TM1729_SEG_PIN9   16U
#define TM1729_SEG_PIN10  20U
#define TM1729_SEG_PIN11  24U
#define TM1729_SEG_PIN12  28U
#define TM1729_SEG_PIN13  32U

#define LCD_1stNUM_a   (TM1729_SEG_PIN5 + TM1729_COM1)
#define LCD_1stNUM_b   (TM1729_SEG_PIN6 + TM1729_COM1)
#define LCD_1stNUM_c   (TM1729_SEG_PIN6 + TM1729_COM3)
#define LCD_1stNUM_d   (TM1729_SEG_PIN5 + TM1729_COM4)
#define LCD_1stNUM_e   (TM1729_SEG_PIN5 + TM1729_COM3)
#define LCD_1stNUM_f   (TM1729_SEG_PIN5 + TM1729_COM2)
#define LCD_1stNUM_g   (TM1729_SEG_PIN6 + TM1729_COM2)

#define LCD_2ndNUM_a   (TM1729_SEG_PIN13 + TM1729_COM1)
#define LCD_2ndNUM_b   (TM1729_SEG_PIN12 + TM1729_COM1)
#define LCD_2ndNUM_c   (TM1729_SEG_PIN12 + TM1729_COM3)
#define LCD_2ndNUM_d   (TM1729_SEG_PIN13 + TM1729_COM4)
#define LCD_2ndNUM_e   (TM1729_SEG_PIN13 + TM1729_COM3)
#define LCD_2ndNUM_f   (TM1729_SEG_PIN13 + TM1729_COM2)
#define LCD_2ndNUM_g   (TM1729_SEG_PIN12 + TM1729_COM2)

#define LCD_3ndNUM_a   (TM1729_SEG_PIN11 + TM1729_COM1)
#define LCD_3ndNUM_b   (TM1729_SEG_PIN10 + TM1729_COM1)
#define LCD_3ndNUM_c   (TM1729_SEG_PIN10 + TM1729_COM3)
#define LCD_3ndNUM_d   (TM1729_SEG_PIN11 + TM1729_COM4)
#define LCD_3ndNUM_e   (TM1729_SEG_PIN11 + TM1729_COM3)
#define LCD_3ndNUM_f   (TM1729_SEG_PIN11 + TM1729_COM2)
#define LCD_3ndNUM_g   (TM1729_SEG_PIN10 + TM1729_COM2)

#define LCD_4ndNUM_a   (TM1729_SEG_PIN9 + TM1729_COM1)
#define LCD_4ndNUM_b   (TM1729_SEG_PIN8 + TM1729_COM1)
#define LCD_4ndNUM_c   (TM1729_SEG_PIN8 + TM1729_COM3)
#define LCD_4ndNUM_d   (TM1729_SEG_PIN9 + TM1729_COM4)
#define LCD_4ndNUM_e   (TM1729_SEG_PIN9 + TM1729_COM3)
#define LCD_4ndNUM_f   (TM1729_SEG_PIN9 + TM1729_COM2)
#define LCD_4ndNUM_g   (TM1729_SEG_PIN8 + TM1729_COM2)

#define LCD_5thNUM_a   (TM1729_SEG_PIN7 + TM1729_COM1)
#define LCD_5thNUM_b   (TM1729_SEG_PIN12 + TM1729_COM4)
#define LCD_5thNUM_c   (TM1729_SEG_PIN8 + TM1729_COM4)
#define LCD_5thNUM_d   (TM1729_SEG_PIN7 + TM1729_COM4)
#define LCD_5thNUM_e   (TM1729_SEG_PIN7 + TM1729_COM3)
#define LCD_5thNUM_f   (TM1729_SEG_PIN7 + TM1729_COM2)
#define LCD_5thNUM_g   (TM1729_SEG_PIN10 + TM1729_COM4)

#define LCD_RPM_S1     (TM1729_SEG_PIN6 + TM1729_COM4)

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

static uint8_t TM1729_BitBuf[TM1729_LCD_BUF_LEN] = {0};
static const uint8_t TM1729_NumModel[17] = {
  0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
  0x77U, 0x7CU, 0x39U, 0x5EU, 0x79U, 0x71U, 0x00U
};
const unsigned char LCD_NumPos[] =
{
  LCD_1stNUM_a,
  LCD_1stNUM_b,
  LCD_1stNUM_c,
  LCD_1stNUM_d,
  LCD_1stNUM_e,
  LCD_1stNUM_f,
  LCD_1stNUM_g,

  LCD_2ndNUM_a,
  LCD_2ndNUM_b,
  LCD_2ndNUM_c,
  LCD_2ndNUM_d,
  LCD_2ndNUM_e,
  LCD_2ndNUM_f,
  LCD_2ndNUM_g,

  LCD_3ndNUM_a,
  LCD_3ndNUM_b,
  LCD_3ndNUM_c,
  LCD_3ndNUM_d,
  LCD_3ndNUM_e,
  LCD_3ndNUM_f,
  LCD_3ndNUM_g,

  LCD_4ndNUM_a,
  LCD_4ndNUM_b,
  LCD_4ndNUM_c,
  LCD_4ndNUM_d,
  LCD_4ndNUM_e,
  LCD_4ndNUM_f,
  LCD_4ndNUM_g,

  LCD_5thNUM_a,
  LCD_5thNUM_b,
  LCD_5thNUM_c,
  LCD_5thNUM_d,
  LCD_5thNUM_e,
  LCD_5thNUM_f,
  LCD_5thNUM_g,
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

/* 指令表：添加新指令只需在此增加 { "name", CallbackFn } 项 */
static const CmdEntry_t cmdTable[] = {
  { "help",   Cmd_Help   },
  { "h",      Cmd_Help   },
  { "speed",  Cmd_SetSpeed },
  { "start",  Cmd_Start  },
  { "pause",  Cmd_Pause  },
  { "status", Cmd_Status },
  { NULL,     NULL       }
};

/* Private function prototypes -----------------------------------------------*/
static void UART_Config(void);
static void TIM1_PWM_Config(void);
static void TIM1_SetDutyPermille(uint16_t dutyPermille);
static void TM1729_Init(void);
static void TM1729_DemoUpdate(AppMode_t mode);

static void EC11_Config(void);

/* 任务处理函数 */
static void Task_Encoder(void);
static void Task_App(void);
static void Task_PWM(void);
static void Task_TM1729(void);
static void Task_UartCmd(void);

/* 业务辅助函数 */
static uint8_t RpmToDutyPercent(uint16_t rpm);
static uint16_t GetTargetDutyPermille(void);
static const char* GetModeName(AppMode_t mode);
static uint16_t LoadLastRpm(void);
static void SaveRpmToFlash(uint16_t rpm);

static void TM1729_ShortDelay(void);
static void TM1729_Start(void);
static void TM1729_Stop(void);
static void TM1729_SendByte(uint8_t dat);
static void TM1729_SetBit(uint8_t bit);
static void TM1729_ClrBit(uint8_t bit);
static void TM1729_SetNumAt(uint8_t idx, uint8_t num);
static void TM1729_Show4Digits(uint8_t d3, uint8_t d2, uint8_t d1, uint8_t d0);
static void TM1729_SetRpmIndicator(uint8_t enable);
static void TM1729_ShowValueOnRange(uint16_t value, uint8_t firstIdx, uint8_t lastIdx);
static void TM1729_WriteBuffer(void);
static void TM1729_PortInit(void);

/* 任务调度结构 */
typedef void (*TaskHandler_t)(void);
typedef struct {
  uint16_t period_ms;    /* 周期 */
  volatile uint16_t cnt; /* 倒计时 */
  TaskHandler_t handler;
} Task_t;

static Task_t tasks[] = {
  { TASK_PERIOD_ENCODER_MS, 0, Task_Encoder },
  { TASK_PERIOD_UART_CMD_MS, 0, Task_UartCmd },
  { TASK_PERIOD_APP_MS,     0, Task_App     },
  { TASK_PERIOD_PWM_MS,     0, Task_PWM     },
  { TASK_PERIOD_TM1729_MS,  0, Task_TM1729  },
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
  /* TM1729 屏幕配置（复用老项目接线与时序） */
  TM1729_Init();

  /* Configure SysTick Timer for 1ms interrupts */
  if (SysTick_Config(SystemCoreClock / 1000))
  {
    /* Capture error */
    while (1);
  }

  /* 从 Flash 恢复上次设置的转速 */
  ConfigRpm = LoadLastRpm();
  FlashLastSavedRpm = ConfigRpm;

  printf("\n\r========================================\n\r");
  printf("HK32F0301MxxxxC Homogenizer Demo\n\r");
  printf("========================================\n\r");
  printf("UART1: 115200 baud, 8N1\n\r");
  printf("TIM1 PWM: 17.57kHz, PD2 pin (CH2)\n\r");
  printf("EC11 A/B/KEY: PC3/PC4/PC5 (active low)\n\r");
  printf("TM1729 SCL/SDA: PC6/PC7\n\r");
  printf("Long Press (3s): start / pause\n\r");
  printf("Knob: adjust RPM (step 1000, min 5000)\n\r");
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

  /* Active-low key with debounce + short/long press events */
  {
    uint8_t keyRaw = (GPIO_ReadInputDataBit(EC11_GPIO_PORT, EC11_PIN_KEY) != Bit_RESET) ? 1U : 0U;

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
  int32_t encPos = 0;
  uint8_t longEvt = 0U;

  __disable_irq();
  delta = EncDeltaPending;
  EncDeltaPending = 0;
  encPos = EncoderPosition;
  KeyShortPressEvent = 0U;
  longEvt = KeyLongPressEvent;
  KeyLongPressEvent = 0U;
  __enable_irq();

  if (delta != 0)
  {
    printf("\r\nhomogenizer > Knob cnt: %ld\r\nhomogenizer > ", (long)encPos);
  }

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

/* TM1729 显示任务：50ms 刷新一次显示缓冲 */
/**
 * @brief  TM1729 LCD 显示任务：50ms 周期刷新显示缓冲（停止时显示目标转速，运行时显示实际转速）
 * @param  None
 * @return None
 */
static void Task_TM1729(void)
{
  uint16_t rpmToShow;
  memset(TM1729_BitBuf, 0, sizeof(TM1729_BitBuf));

  /* 停止时显示目标转速，运行时显示实际输出转速 */
  rpmToShow = (AppMode == APP_MODE_RUNNING) ? MotorRpm : ConfigRpm;
  TM1729_ShowValueOnRange(rpmToShow, 0U, 4U);
  TM1729_SetRpmIndicator(1U);

  TM1729_WriteBuffer();
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
  if (AppMode == APP_MODE_RUNNING)
  {
    uint8_t dutyPct = RpmToDutyPercent(MotorRpm);
    return (uint16_t)dutyPct * 10U;
  }
  return PWM_INACTIVE_PERMILLE;
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
  * @brief  Configure EC11 pins on PC3/PC4/PC5 as pull-up inputs
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
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(EC11_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief  TM1729 位操作短延时（软件空循环）
 * @param  None
 * @return None
 */
static void TM1729_ShortDelay(void)
{
  for (volatile uint32_t i = 0; i < 32U; ++i)
  {
  }
}

/**
 * @brief  TM1729 I2C 类通信起始条件：SCL 高时 SDA 下降沿
 * @param  None
 * @return None
 */
static void TM1729_Start(void)
{
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_SET);
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_SET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_RESET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_RESET);
}

/**
 * @brief  TM1729 I2C 类通信停止条件：SCL 高时 SDA 上升沿
 * @param  None
 * @return None
 */
static void TM1729_Stop(void)
{
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_SET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_RESET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_SET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_RESET);
}

/**
 * @brief  向 TM1729 发送一个字节（位操作模拟串行时序，MSB 优先）
 * @param  dat: 待发送的字节
 * @return None
 */
static void TM1729_SendByte(uint8_t dat)
{
  for (uint8_t i = 0U; i < 8U; ++i)
  {
    GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_RESET);
    if ((dat & 0x80U) != 0U)
    {
      GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_SET);
    }
    else
    {
      GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_RESET);
    }
    TM1729_ShortDelay();
    GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_SET);
    TM1729_ShortDelay();
    dat <<= 1;
  }

  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_RESET);
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SDA, Bit_RESET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_SET);
  TM1729_ShortDelay();
  GPIO_WriteBit(TM1729_GPIO_PORT, TM1729_PIN_SCL, Bit_RESET);
}

/**
 * @brief  设置 TM1729 显示缓冲中的指定位
 * @param  bit: 位编号（0-34，对应 LCD_NumPos 中的位位置）
 * @return None
 */
static void TM1729_SetBit(uint8_t bit)
{
  uint8_t tmp = (uint8_t)(7U - (bit % 8U));
  TM1729_BitBuf[bit / 8U] |= (uint8_t)(1U << tmp);
}

/**
 * @brief  清除 TM1729 显示缓冲中的指定位
 * @param  bit: 位编号（0-34，对应 LCD_NumPos 中的位位置）
 * @return None
 */
static void TM1729_ClrBit(uint8_t bit)
{
  uint8_t tmp = (uint8_t)(7U - (bit % 8U));
  TM1729_BitBuf[bit / 8U] &= (uint8_t)(~(1U << tmp));
}

/**
 * @brief  在 TM1729 显示缓冲的指定数码管位置设置数字
 * @param  idx: 数码管索引（0-4）
 * @param  num: 要显示的数字（0-9），16 为空白
 * @return None
 */
static void TM1729_SetNumAt(uint8_t idx, uint8_t num)
{
  uint8_t seg = TM1729_NumModel[(num <= 16U) ? num : 16U];

  if (idx >= 5U)
  {
    return;
  }

  for (uint8_t i = 0U; i < 7U; ++i)
  {
    if ((seg & 0x01U) != 0U)
    {
      TM1729_SetBit(LCD_NumPos[(idx * 7U) + i]);
    }
    else
    {
      TM1729_ClrBit(LCD_NumPos[(idx * 7U) + i]);
    }
    seg >>= 1;
  }
}

/**
 * @brief  在 LCD 上显示 4 位数字（清除缓冲后写入）
 * @param  d3: 千位数字
 * @param  d2: 百位数字
 * @param  d1: 十位数字
 * @param  d0: 个位数字
 * @return None
 */
static void TM1729_Show4Digits(uint8_t d3, uint8_t d2, uint8_t d1, uint8_t d0)
{
  memset(TM1729_BitBuf, 0, sizeof(TM1729_BitBuf));

  TM1729_SetNumAt(0U, d3);
  TM1729_SetNumAt(1U, d2);
  TM1729_SetNumAt(2U, d1);
  TM1729_SetNumAt(3U, d0);
}

/**
 * @brief  控制 LCD 上 RPM 单位指示段的显示/隐藏
 * @param  enable: 非 0 显示，0 隐藏
 * @return None
 */
static void TM1729_SetRpmIndicator(uint8_t enable)
{
  if (enable != 0U)
  {
    TM1729_SetBit(LCD_RPM_S1);
  }
  else
  {
    TM1729_ClrBit(LCD_RPM_S1);
  }
}

/**
 * @brief  在 LCD 指定数码管范围内显示数值（右对齐，高位消隐）
 * @param  value: 待显示数值
 * @param  firstIdx: 起始数码管索引（0-4）
 * @param  lastIdx: 结束数码管索引（0-4，需 >= firstIdx）
 * @return None
 */
static void TM1729_ShowValueOnRange(uint16_t value, uint8_t firstIdx, uint8_t lastIdx)
{
  uint8_t i;
  uint8_t hasOutput = 0U;

  if ((firstIdx >= 5U) || (lastIdx >= 5U) || (firstIdx > lastIdx))
  {
    return;
  }

  for (i = firstIdx; i <= lastIdx; ++i)
  {
    TM1729_SetNumAt(i, 16U);
  }

  for (i = lastIdx; ; --i)
  {
    if ((value == 0U) && (hasOutput != 0U))
    {
      break;
    }

    TM1729_SetNumAt(i, (uint8_t)(value % 10U));
    value /= 10U;
    hasOutput = 1U;

    if (i == firstIdx)
    {
      break;
    }
  }
}

/**
 * @brief  将显示缓冲通过 TM1729 写入 LCD 硬件
 * @param  None
 * @return None
 */
static void TM1729_WriteBuffer(void)
{
  TM1729_Start();
  TM1729_SendByte(TM1729_SLAVE_ADDRESS);
  TM1729_SendByte(TM1729_ADSET);
  for (uint8_t i = 0U; i < TM1729_LCD_BUF_LEN; ++i)
  {
    TM1729_SendByte(TM1729_BitBuf[i]);
  }
  TM1729_Stop();
}

/**
 * @brief  初始化 TM1729 通信 GPIO 引脚（PC6/PC7 推挽输出）
 * @param  None
 * @return None
 */
static void TM1729_PortInit(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

  GPIO_InitStructure.GPIO_Pin = TM1729_PIN_SCL | TM1729_PIN_SDA;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(TM1729_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief  TM1729 LCD 驱动初始化：GPIO 配置、发送 IC 设置指令、显示测试图案
 * @param  None
 * @return None
 */
static void TM1729_Init(void)
{
  TM1729_PortInit();

  TM1729_Stop();
  TM1729_Start();
  TM1729_SendByte(TM1729_SLAVE_ADDRESS);
  TM1729_SendByte(TM1729_ICSET);
  TM1729_SendByte(TM1729_DISCTL);
  TM1729_SendByte(TM1729_BLKCTL);
  TM1729_SendByte(TM1729_APCTL);
  TM1729_SendByte(TM1729_MODESET);
  TM1729_Stop();

  /* 上电先显示固定测试内容 */
  memset(TM1729_BitBuf, 0, sizeof(TM1729_BitBuf));
  TM1729_SetNumAt(0U, 1U);
  TM1729_SetNumAt(1U, 2U);
  TM1729_SetNumAt(2U, 3U);
  TM1729_SetNumAt(3U, 4U);
  TM1729_SetNumAt(4U, 16U);
  TM1729_SetRpmIndicator(1U);
  TM1729_WriteBuffer();
}

/**
 * @brief  LCD 演示更新：停止时显示 1111，运行时显示 3333
 * @param  mode: 应用模式
 * @return None
 */
static void TM1729_DemoUpdate(AppMode_t mode)
{
  /* 状态机演示码：STOPPED/RUNNING -> 1111/3333 */
  if (mode == APP_MODE_RUNNING)
  {
    TM1729_Show4Digits(3U, 3U, 3U, 3U);
  }
  else
  {
    TM1729_Show4Digits(1U, 1U, 1U, 1U);
  }

  TM1729_WriteBuffer();
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

#if defined(HK32F0301MF4P7C)
  /* Ensure package pin mux routes TIM1_CH2 signal to PD2 */
  GPIO_IOMUX_ChangePin(IOMUX_PIN11, IOMUX_PB5_SEL_PD2);
#endif

  /* Configure PD2 as alternate function push-pull (TIM1_CH2) */
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  /* Connect TIM1_CH2 to PD2 (AF3, per TIM1 mapping) */
  GPIO_PinAFConfig(GPIOD, GPIO_PinSource2, GPIO_AF_3);

  /* Time base configuration for 17.57kHz PWM */
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
