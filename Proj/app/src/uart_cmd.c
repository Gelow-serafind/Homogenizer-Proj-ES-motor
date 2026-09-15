/*******************************************************************************
* @filename:  uart_cmd.c
* @brief:     调试 shell 实现（原 UART 命令系统迁移；命令语义不变）
* @note       命令表：help/h、speed、time、start、stop、pause、pwm、adc、tach、knob、
*              status、lcd、pid、pidkp、pidki（速度环整定）
*******************************************************************************/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "app_cfg.h"
#include "ads1115.h"
#include "app_state.h"
#include "display.h"
#include "ec11.h"
#include "motor.h"
#include "timer.h"
#include "uart_cmd.h"

#define UART_RX_RING_SIZE 64U
#define CMD_LINE_MAX      64U

typedef void (*CmdCallback_t)(const char* args);

typedef struct {
  const char*   cmd;
  CmdCallback_t cb;
} CmdEntry_t;

static uint8_t uartRxRing[UART_RX_RING_SIZE];
static uint8_t uartRxHead = 0U;
static uint8_t uartRxTail = 0U;
static uint8_t cmdLineBuf[CMD_LINE_MAX];
static uint8_t cmdLineLen = 0U;

/* static 前置原型 */
static void uart_poll_rx(void);
static void uart_process_cmd_line(const char* line);
static const char* mode_name(app_mode_t m);
static void cmd_help(const char* args);
static void cmd_set_speed(const char* args);
static void cmd_set_time(const char* args);
static void cmd_start(const char* args);
static void cmd_stop(const char* args);
static void cmd_pwm(const char* args);
static void cmd_adc(const char* args);
static void cmd_tach(const char* args);
static void cmd_knob(const char* args);
static void cmd_status(const char* args);
static void cmd_lcd(const char* args);
static void cmd_pid(const char* args);
static void cmd_pidkp(const char* args);
static void cmd_pidki(const char* args);

/* 指令表：添加新指令只需在此增加 { "name", CallbackFn } 项 */
static const CmdEntry_t cmdTable[] = {
  { "help",   cmd_help      },
  { "h",      cmd_help      },
  { "speed",  cmd_set_speed },
  { "time",   cmd_set_time  },
  { "start",  cmd_start     },
  { "stop",   cmd_stop      },
  { "pause",  cmd_stop      },   /* pause 为 stop 别名，语义=暂停 */
  { "pwm",    cmd_pwm       },
  { "adc",    cmd_adc       },
  { "tach",   cmd_tach      },
  { "knob",   cmd_knob      },
  { "status", cmd_status    },
  { "lcd",    cmd_lcd       },
  { "pid",    cmd_pid       },
  { "pidkp",  cmd_pidkp     },
  { "pidki",  cmd_pidki     },
  { NULL,     NULL          }
};

/* ====================== 初始化与任务 ====================== */

void uart_cmd_init(void)
{
  UART_InitTypeDef UART_InitStructure;

  /* USART1: 115200 / 8bit / 1stop / no parity */
  UART_InitStructure.UART_BaudRate   = 115200;
  UART_InitStructure.UART_WordLength = UART_WordLength_8b;
  UART_InitStructure.UART_StopBits   = UART_StopBits_1;
  UART_InitStructure.UART_Parity     = UART_Parity_No;
  UART_InitStructure.UART_Mode       = UART_Mode_Rx | UART_Mode_Tx;

  HK_EVAL_COMInit(COM1, &UART_InitStructure);

  printf("\n\r===== Homogenizer Controller =====\n\r");
  printf("HK32F0301MxxxxC shell ready, type 'help'\n\r");
  printf("========================================\n\r\n\r");

  /* 等待发送完成 */
  while (UART_GetFlagStatus(EVAL_COM1, UART_FLAG_TC) == RESET)
  {
  }

  printf("Starting scheduler...\n\r");
  printf("homogenizer > ");
}

void uart_cmd_ops(void)
{
  uart_poll_rx();
}

/**
  * @brief  轮询 UART 接收寄存器与环形缓冲，提取字符并进行行编辑和命令解析
  */
static void uart_poll_rx(void)
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

    if ((ch == '\n') || (ch == '\r'))
    {
      /* 跳过紧随其后的连续换行符（兼容 \r\n / \n\r / 单 \r / 单 \n） */
      while (uartRxTail != uartRxHead)
      {
        uint8_t nxt = uartRxRing[uartRxTail];

        if ((nxt == '\n') || (nxt == '\r'))
        {
          uartRxTail = (uint8_t)((uartRxTail + 1U) % UART_RX_RING_SIZE);
        }
        else
        {
          break;
        }
      }

      cmdLineBuf[cmdLineLen] = '\0';
      uart_process_cmd_line((const char*)cmdLineBuf);
      cmdLineLen = 0U;

      printf("\r\nhomogenizer > ");
    }
    else if ((ch == '\b') || (ch == 0x7F))
    {
      /* 退格：擦除终端上前一个字符 */
      if (cmdLineLen > 0U)
      {
        cmdLineLen--;
        printf("\b \b");
      }
    }
    else if (cmdLineLen < (CMD_LINE_MAX - 1U))
    {
      /* 普通字符：存入缓冲区并回显（远程 echo） */
      cmdLineBuf[cmdLineLen++] = ch;
      printf("%c", (char)ch);
    }
    else
    {
      /* 行满：丢弃 */
    }
  }
}

/**
  * @brief  解析并执行一条命令行（从 cmdTable 查表匹配）
  */
static void uart_process_cmd_line(const char* line)
{
  const char* p = line;
  const char* args;
  uint8_t     cmdLen;

  while (*p == ' ')
  {
    p++;
  }
  if (*p == '\0')
  {
    return;
  }

  args = p;
  while ((*args != '\0') && (*args != ' '))
  {
    args++;
  }
  cmdLen = (uint8_t)(args - p);
  while (*args == ' ')
  {
    args++;
  }

  for (const CmdEntry_t* entry = cmdTable; entry->cmd != NULL; entry++)
  {
    if ((strncmp(p, entry->cmd, cmdLen) == 0) && (entry->cmd[cmdLen] == '\0'))
    {
      entry->cb(args);
      return;
    }
  }

  printf("\r\nUnknown: %s\r\n", line);
}

/**
  * @brief  模式枚举 → 可读名称（打印用）
  */
static const char* mode_name(app_mode_t m)
{
  if (m == APP_RUNNING)
  {
    return "RUNNING";
  }
  if (m == APP_PAUSED)
  {
    return "PAUSED";
  }
  return "STANDBY";
}

/* ====================== 命令回调实现 ====================== */

static void cmd_help(const char* args)
{
  (void)args;
  printf("\r\nAvailable commands:\r\n");
  for (const CmdEntry_t* entry = cmdTable; entry->cmd != NULL; entry++)
  {
    printf("  %s\r\n", entry->cmd);
  }
}

/**
  * @brief  speed <rpm>：设置目标转速（0..RPM_MAX）并确认写 Flash
  */
static void cmd_set_speed(const char* args)
{
  int val = 0;

  while (*args == ' ')
  {
    args++;
  }
  while ((*args >= '0') && (*args <= '9'))
  {
    val = val * 10 + (*args - '0');
    args++;
  }
  if (val < 0)
  {
    val = 0;
  }
  if (val > (int)RPM_MAX)
  {
    val = RPM_MAX;
  }

  app_state_set_speed((uint16_t)val);
  printf("\r\nSpeed set to %u RPM\r\n", (unsigned)app_state_get_cfg_rpm());
}

/**
  * @brief  time <h> <m> <s>：设置倒计时并确认写 Flash
  */
static void cmd_set_time(const char* args)
{
  uint32_t h = 0U;
  uint32_t m = 0U;
  uint32_t s = 0U;
  uint16_t cfgH;
  uint8_t  cfgM;
  uint8_t  cfgS;

  while (*args == ' ')
  {
    args++;
  }
  while ((*args >= '0') && (*args <= '9'))
  {
    h = h * 10U + (uint32_t)(*args - '0');
    if (h > 65535U)
    {
      h = 65535U;   /* 防超长输入溢出 */
    }
    args++;
  }
  while (*args == ' ')
  {
    args++;
  }
  while ((*args >= '0') && (*args <= '9'))
  {
    m = m * 10U + (uint32_t)(*args - '0');
    if (m > 999U)
    {
      m = 999U;
    }
    args++;
  }
  while (*args == ' ')
  {
    args++;
  }
  while ((*args >= '0') && (*args <= '9'))
  {
    s = s * 10U + (uint32_t)(*args - '0');
    if (s > 999U)
    {
      s = 999U;
    }
    args++;
  }

  /* 统一钳位（先钳位再转窄类型，避免回绕） */
  if (h > TIME_HOUR_MAX)
  {
    h = TIME_HOUR_MAX;
  }
  if (m > 59U)
  {
    m = 59U;
  }
  if (s > 59U)
  {
    s = 59U;
  }

  app_state_set_time((uint16_t)h, (uint8_t)m, (uint8_t)s);   /* 内部再钳位一次 */

  app_state_get_cfg_time(&cfgH, &cfgM, &cfgS);
  printf("\r\nTime set to %u:%02u:%02u\r\n",
         (unsigned)cfgH, (unsigned)cfgM, (unsigned)cfgS);
}

static void cmd_start(const char* args)
{
  (void)args;
  app_state_start();
  printf("\r\nState -> %s\r\n", mode_name(app_state_get_mode()));
}

static void cmd_stop(const char* args)
{
  (void)args;
  app_state_stop();
  printf("\r\nState -> %s\r\n", mode_name(app_state_get_mode()));
}

/**
  * @brief  pwm <0-1000> | pwm off：手动占空比（标定/驱动链验证，绕过 LUT 与钳位）
  */
static void cmd_pwm(const char* args)
{
  int val = 0;
  const char* p = args;

  while (*p == ' ')
  {
    p++;
  }

  if (strncmp(p, "off", 3U) == 0)
  {
    motor_manual_off();
    printf("\r\nPWM: auto restored (duty clamped to 0%%)\r\n");
    return;
  }

  while ((*p >= '0') && (*p <= '9'))
  {
    val = val * 10 + (*p - '0');
    p++;
  }
  if (val < 0)
  {
    val = 0;
  }
  if (val > 1000)
  {
    val = 1000;
  }

  motor_manual_duty((uint16_t)val);
  printf("\r\nPWM: manual duty = %d/1000 (%d.%u%%)\r\n",
         val, val / 10, (unsigned)(val % 10));
}

/**
  * @brief  pid：速度环诊断快照（目标/参考/实测/FF/P/I/积分/占空比与系数）
  */
static void cmd_pid(const char* args)
{
  motor_pid_info_t info;

  (void)args;
  motor_pid_get_info(&info);
  printf("\r\nloop=%s tgt=%u ref=%u meas=%u | ff=%d p=%d i=%d iacc=%d | duty=%u permille\r\n",
         (SPEED_LOOP_EN != 0U) ? "on" : "off",
         (unsigned)info.target_rpm, (unsigned)info.ref_rpm, (unsigned)info.meas_rpm,
         (int)info.ff_permille, (int)info.p_permille, (int)info.i_permille,
         (int)info.i_acc_permille, (unsigned)info.duty_permille);
  printf("kp(Q8)=%u ki(Q8)=%u imax=%u dmax=%u\r\n",
         (unsigned)motor_get_kp_q8(), (unsigned)motor_get_ki_q8(),
         (unsigned)SPEED_LOOP_I_MAX, (unsigned)SPEED_LOOP_DUTY_MAX);
}

/**
  * @brief  pidkp <n>：设置 P 系数（Q8，0..4095；64=0.25‰/RPM）
  */
static void cmd_pidkp(const char* args)
{
  uint32_t v = 0U;
  const char* p = args;

  while (*p == ' ')
  {
    p++;
  }
  while ((*p >= '0') && (*p <= '9'))
  {
    v = v * 10U + (uint32_t)(*p - '0');
    if (v > 4095U)
    {
      v = 4095U;
    }
    p++;
  }

  motor_set_kp_q8((uint16_t)v);
  printf("\r\nKp(Q8) = %u\r\n", (unsigned)motor_get_kp_q8());
}

/**
  * @brief  pidki <n>：设置 I 系数（Q8，0..4095；6≈0.023‰/RPM/拍）
  */
static void cmd_pidki(const char* args)
{
  uint32_t v = 0U;
  const char* p = args;

  while (*p == ' ')
  {
    p++;
  }
  while ((*p >= '0') && (*p <= '9'))
  {
    v = v * 10U + (uint32_t)(*p - '0');
    if (v > 4095U)
    {
      v = 4095U;
    }
    p++;
  }

  motor_set_ki_q8((uint16_t)v);
  printf("\r\nKi(Q8) = %u\r\n", (unsigned)motor_get_ki_q8());
}

/**
  * @brief  adc：读 ADS1115 两通道原始值（信号/接线验证用）
  */
static void cmd_adc(const char* args)
{
  uint16_t a0;
  uint16_t a1;

  (void)args;
  a0 = ads1115_read(ADS_CH_TEMP);
  a1 = ads1115_read(ADS_CH_SPEED);
  printf("\r\nADS: AIN0(PT1000)=%u  AIN1(SPEED_SW)=%u  (0xFFFF=bus err)\r\n",
         (unsigned)a0, (unsigned)a1);
}

/**
  * @brief  tach <n>：连续采样 AIN1（SPEED_SW）并统计电平跳变（手转电机数脉冲）
  */
static void cmd_tach(const char* args)
{
  const char* p = args;
  uint32_t cnt = 100U;
  uint32_t i;
  uint32_t edges = 0U;
  uint32_t hcnt = 0U;
  uint32_t lcnt = 0U;
  uint16_t v;
  uint16_t vmin = 0xFFFFU;
  uint16_t vmax = 0U;
  uint8_t  lvl;
  uint8_t  prev = 0xFFU;

  while (*p == ' ')
  {
    p++;
  }
  {
    uint32_t d = 0U;

    while ((*p >= '0') && (*p <= '9'))
    {
      d = d * 10U + (uint32_t)(*p - '0');
      p++;
    }
    if ((d > 0U) && (d < 2000U))
    {
      cnt = d;
    }
  }

  printf("\r\nTach: AIN1 x%lu (H if raw>13000, 每跳变打一行)\r\n",
         (unsigned long)cnt);

  for (i = 0U; i < cnt; i++)
  {
    v = ads1115_read(ADS_CH_SPEED);
    if (v != 0xFFFFU)
    {
      if (v < vmin)
      {
        vmin = v;
      }
      if (v > vmax)
      {
        vmax = v;
      }
      lvl = (v > 13000U) ? 1U : 0U;
      if (lvl != 0U)
      {
        hcnt++;
      }
      else
      {
        lcnt++;
      }
      if ((prev != 0xFFU) && (lvl != prev))
      {
        edges++;
        printf("  edge#%lu raw=%u %s\r\n",
               (unsigned long)edges, (unsigned)v, (lvl != 0U) ? "H" : "L");
      }
      prev = lvl;
    }
    ads1115_delay_ms(2U);
  }
  printf("Tach done: edges=%lu high=%lu low=%lu min=%u max=%u\r\n",
         (unsigned long)edges, (unsigned long)hcnt, (unsigned long)lcnt,
         (unsigned)vmin, (unsigned)vmax);
}

/**
  * @brief  knob：旋钮捕获统计（自上次执行以来，读取后清零）
  * @note   验证：先执行一次清零 → 匀速转过 N 格 → 再执行；
  *         edge4x 应≈4N（每格 4 边沿）、detent 应=N；isr 与 edge4x 差值=毛刺触发
  */
static void cmd_knob(const char* args)
{
  ec11_stats_t st;

  (void)args;
  ec11_take_stats(&st);
  printf("\r\n[KNOB] isr=%lu edge4x=%lu detent=%ld (自上次执行以来；已清零)\r\n",
         (unsigned long)st.isrCount, (unsigned long)st.edgeCount, (long)st.detentNet);
}

/**
  * @brief  status：打印模式、目标/实际转速、占空比、倒计时剩余
  */
static void cmd_status(const char* args)
{
  uint16_t remH;
  uint8_t  remM;
  uint8_t  remS;
  uint16_t cfgH;
  uint8_t  cfgM;
  uint8_t  cfgS;

  (void)args;
  timer_get_rem(&remH, &remM, &remS);
  app_state_get_cfg_time(&cfgH, &cfgM, &cfgS);

  printf("\r\nMode:%-8s | Set:%4u | Motor:%4u | Duty:%3u%% | Rem:%u:%02u:%02u%s\r\n",
         mode_name(app_state_get_mode()),
         (unsigned)app_state_get_cfg_rpm(),
         (unsigned)motor_get_out_rpm(),
         (unsigned)(motor_get_duty_permille() / 10U),
         (unsigned)remH, (unsigned)remM, (unsigned)remS,
         (timer_is_active() != 0U) ? " (active)" : "");
  printf("SetTime:%u:%02u:%02u\r\n", (unsigned)cfgH, (unsigned)cfgM, (unsigned)cfgS);
}

/**
  * @brief  lcd auto | all | off | d1-28 | s1-30：显示调试
  */
static void cmd_lcd(const char* args)
{
  int val = 0;
  const char* p;

  while (*args == ' ')
  {
    args++;
  }

  /* auto：恢复业务自动刷新（退出 lcd 手动调试模式） */
  if (strncmp(args, "auto", 4U) == 0)
  {
    display_debug_auto();
    printf("\r\nLCD: auto refresh on\r\n");
    return;
  }

  if (strncmp(args, "all", 3U) == 0)
  {
    display_debug_all(1U);
    printf("\r\nLCD: all segments on (manual)\r\n");
    return;
  }

  if (strncmp(args, "off", 3U) == 0)
  {
    display_debug_all(0U);
    printf("\r\nLCD: all segments off (manual)\r\n");
    return;
  }

  if ((args[0] == 'd') || (args[0] == 'D'))
  {
    p = args + 1;
    while (*p == ' ')
    {
      p++;
    }
    while ((*p >= '0') && (*p <= '9'))
    {
      val = val * 10 + (*p - '0');
      p++;
    }
    if ((val < 1) || (val > 28))
    {
      printf("\r\nLCD: digit out of range 1-28\r\n");
      return;
    }
    display_debug_digit((uint8_t)val);
    printf("\r\nLCD: digit %d = 8 (manual)\r\n", val);
    return;
  }

  if ((args[0] == 's') || (args[0] == 'S'))
  {
    p = args + 1;
    while (*p == ' ')
    {
      p++;
    }
    while ((*p >= '0') && (*p <= '9'))
    {
      val = val * 10 + (*p - '0');
      p++;
    }
    if ((val < 1) || (val > 30))
    {
      printf("\r\nLCD: icon out of range 1-30\r\n");
      return;
    }
    display_debug_icon((uint8_t)val);
    printf("\r\nLCD: icon S%d on (manual)\r\n", val);
    return;
  }

  printf("\r\nLCD usage: lcd auto | lcd all | lcd off | lcd d1-28 | lcd s1-30\r\n");
}

/* ====================== printf 重定向 ====================== */

/**
  * @brief  Retargets the C library printf function to the USART
  */
int fputc(int ch, FILE *f)
{
  uint32_t guard;

  (void)f;
  UART_SendData(EVAL_COM1, (uint8_t)ch);

  /* 等待发送完成（带超时防外部串扰把打印卡死） */
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

/* ====================== 上电测速调试循环（BOOT_TACH_LOOP=1） ====================== */

#if (BOOT_TACH_LOOP != 0U)
/**
  * @brief  上电即循环打印 4 通道（死循环，无需 PC 输入）
  *         转动光栅看哪一路 conv 大幅跳变（硬件信号定位期间使用）
  */
void uart_cmd_boot_tach_loop(void)
{
  uint16_t v[4];
  uint8_t i;

  printf("\r\n[TACH BOOT LOOP] 4 通道轮询（每行约 0.2s）：转动光栅看哪一路 conv 大幅跳变\r\n");
  while (1U)
  {
    for (i = 0U; i < 4U; i++)
    {
      v[i] = ads1115_read(i);
    }
    printf("\r\nAIN0=%5u AIN1=%5u AIN2=%5u AIN3=%5u%s",
           (unsigned)v[0], (unsigned)v[1], (unsigned)v[2], (unsigned)v[3],
           ((v[0] | v[1] | v[2] | v[3]) == 0xFFFFU) ? " (err)" : "");
    ads1115_delay_ms(150U);
  }
}
#endif /* BOOT_TACH_LOOP */
