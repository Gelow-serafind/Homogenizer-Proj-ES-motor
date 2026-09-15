/*******************************************************************************
* @filename:  display.c
* @brief:     显示模块实现（原 Lcd 系列 / RefreshDisplay / Task_LCD 迁移，行为等价）
* @note       段位映射来源：LVD13083AVTP1 规格书第 3 页（玻璃脚 9..37 = SEG0..SEG28）
*******************************************************************************/
#include "main.h"
#include <string.h>
#include "app_cfg.h"
#include "ht1623.h"
#include "event_bus.h"
#include "app_state.h"
#include "speed_sensor.h"
#include "temp_sensor.h"
#include "timer.h"
#include "display.h"

/* ====================== 段位映射表（实测校准只改此两张表，R6） ====================== */

typedef struct {
  uint8_t seg;   /* HT1623 SEG 编号 0..28 */
  uint8_t com;   /* 玻璃 COM 编号 1..8（HT1623 COM0..COM7 → RAM 位 = com-1） */
} lcd_seg_t;

#define LSEG(s, c) { (uint8_t)(s), (uint8_t)(c) }

/* 28 位数码管 × 7 段（顺序 a b c d e f g） */
static const lcd_seg_t lcdDigitMap[28][7] = {
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

/* 图标段位表：S1..S30（十字架/时钟/感叹号/圆/向上/Rpm/°C/H/m/s/---- 等，对应待实测确认） */
static const lcd_seg_t lcdIconMap[30] = {
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
static const uint8_t lcdDigitFont[11] = {
  0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU, 0x00U
};

/* ====================== 业务字段布局 / 图标角色绑定 ====================== */

/* 玻璃 28 个数码位的业务字段布局（推断初值，实测玻璃丝印后校准改此表）：
   位1-4 SetSpeed | 位5-12 SetTime(H4:M2:S2) | 位13-16 InstSpeed |
   位17-20 InstTemp | 位21-28 RemTime(H4:M2:S2) */
typedef enum {
  FLD_SET_SPEED = 0,
  FLD_SET_TIME,
  FLD_INST_SPEED,
  FLD_INST_TEMP,
  FLD_REM_TIME,
  FLD_COUNT
} lcd_field_t;

static const struct {
  uint8_t first;   /* 起始 digit 1..28 */
  uint8_t len;     /* 位数 */
} fieldLayout[FLD_COUNT] = {
  {  1, 4 },   /* FLD_SET_SPEED  位 1-4   */
  {  5, 8 },   /* FLD_SET_TIME   位 5-12  */
  { 13, 4 },   /* FLD_INST_SPEED 位 13-16 */
  { 17, 4 },   /* FLD_INST_TEMP  位 17-20 */
  { 21, 8 }    /* FLD_REM_TIME   位 21-28 */
};

/* 业务图标 → S 编号绑定（0=未绑定不亮；用 "lcd s1-30" 实测后填入） */
typedef enum {
  ROLE_SENSOR = 0,   /* 十字架：PT1000 在位（A5） */
  ROLE_CLOCK,        /* 时钟：倒计时中 */
  ROLE_WARN,         /* 感叹号：本版恒灭（A8） */
  ROLE_MOTOR,        /* 圆：转速 > 0 */
  ROLE_ARROW,        /* ▲：RUNNING 常亮（A7） */
  ROLE_COUNT
} icon_role_t;

static const uint8_t iconBind[ROLE_COUNT] = { 0U, 0U, 0U, 0U, 0U };

/* ====================== 模块状态 ====================== */

static uint8_t lcdRam[HT1623_RAM_NIBBLES];       /* 当前帧镜像 */
static uint8_t lcdRamPrev[HT1623_RAM_NIBBLES];   /* 上次送屏镜像（差分检测） */
static uint8_t lcdDirty  = 0U;
static uint8_t lcdManual = 0U;                   /* 1=lcd 调试命令手动模式 */
static uint8_t blinkPhase = 0U;                  /* 编辑字段闪烁相位（EVT_BLINK_TOGGLE 同步） */
static uint16_t dispSpeed = 0U;                  /* Instant Speed 显示平滑值（真实值低通版） */

/* static 前置原型 */
static void lcd_set_segment(uint8_t seg, uint8_t com, uint8_t on);
static void lcd_show_digit(uint8_t digit, uint8_t num);
static void lcd_set_icon(uint8_t icon, uint8_t on);
static void lcd_all(uint8_t on);
static void set_role_icon(uint8_t role, uint8_t on);
static void draw_blank_digits(uint8_t first, uint8_t len);
static void draw_number_rl(uint8_t first, uint8_t len, uint32_t value);
static void draw_two_digits(uint8_t first, uint8_t v);
static void draw_time_field(uint8_t first, uint16_t h, uint8_t m, uint8_t s);
static void display_smooth_speed(void);
static void display_render(void);

/* ====================== 底层段位操作 ====================== */

/**
  * @brief  设置单个段位（SEG × COM 交点）
  */
static void lcd_set_segment(uint8_t seg, uint8_t com, uint8_t on)
{
  uint8_t comIdx;
  uint8_t addr;
  uint8_t bit;

  if ((seg >= HT1623_SEG_COUNT) || (com < 1U) || (com > 8U))
  {
    return;
  }

  /* RAM nibble 地址：SEG*2 + COM组偏移；位：COM0-3 → bit0-3，COM4-7 → bit0-3 */
  comIdx = (uint8_t)(com - 1U);
  addr = (uint8_t)(seg * 2U + comIdx / 4U);
  bit = comIdx & 3U;

  if (on != 0U)
  {
    lcdRam[addr] |= (uint8_t)(1U << bit);
  }
  else
  {
    lcdRam[addr] &= (uint8_t)~(1U << bit);
  }
  lcdDirty = 1U;
}

/**
  * @brief  在指定数码管位置显示数字
  * @param  digit: 1..28
  * @param  num: 0-9，10=消隐
  */
static void lcd_show_digit(uint8_t digit, uint8_t num)
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
  segBits = lcdDigitFont[num];

  for (s = 0U; s < 7U; s++)
  {
    lcd_set_segment(lcdDigitMap[digit - 1U][s].seg,
                    lcdDigitMap[digit - 1U][s].com,
                    ((segBits & (uint8_t)(1U << s)) != 0U) ? 1U : 0U);
  }
}

/**
  * @brief  控制图标 S1..S30
  */
static void lcd_set_icon(uint8_t icon, uint8_t on)
{
  if ((icon < 1U) || (icon > 30U))
  {
    return;
  }
  lcd_set_segment(lcdIconMap[icon - 1U].seg, lcdIconMap[icon - 1U].com, on);
}

/**
  * @brief  全屏开/关（含未使用段位，用于点亮测试）
  */
static void lcd_all(uint8_t on)
{
  memset(lcdRam, (on != 0U) ? 0x0FU : 0x00U, HT1623_RAM_NIBBLES);
  lcdDirty = 1U;
}

/**
  * @brief  按业务角色点亮/熄灭图标（未绑定时无操作）
  */
static void set_role_icon(uint8_t role, uint8_t on)
{
  uint8_t s = (role < (uint8_t)ROLE_COUNT) ? iconBind[role] : 0U;

  if (s != 0U)
  {
    lcd_set_icon(s, on);
  }
}

/* ====================== 数字绘制 ====================== */

/**
  * @brief  将某字段 len 个 digit 全部消隐
  */
static void draw_blank_digits(uint8_t first, uint8_t len)
{
  for (uint8_t i = 0U; i < len; i++)
  {
    lcd_show_digit((uint8_t)(first + i), 10U);
  }
}

/**
  * @brief  右对齐显示数字，前导消隐；value==0 仅末位显示 0
  */
static void draw_number_rl(uint8_t first, uint8_t len, uint32_t value)
{
  uint8_t  buf[8];
  uint8_t  n = 0U;
  uint32_t v = value;

  draw_blank_digits(first, len);

  if (v == 0U)
  {
    lcd_show_digit((uint8_t)(first + len - 1U), 0U);
    return;
  }
  while ((v != 0U) && (n < len))
  {
    buf[n++] = (uint8_t)(v % 10U);
    v /= 10U;
  }
  for (uint8_t i = 0U; i < n; i++)
  {
    lcd_show_digit((uint8_t)(first + len - 1U - i), buf[i]);
  }
}

/**
  * @brief  两位数字显示（补零，0-99）
  */
static void draw_two_digits(uint8_t first, uint8_t v)
{
  lcd_show_digit(first, (uint8_t)(v / 10U));
  lcd_show_digit((uint8_t)(first + 1U), (uint8_t)(v % 10U));
}

/**
  * @brief  绘制时间字段（H4 右对齐消隐 + M2 补零 + S2 补零）
  */
static void draw_time_field(uint8_t first, uint16_t h, uint8_t m, uint8_t s)
{
  draw_number_rl(first, 4U, h);
  draw_two_digits((uint8_t)(first + 4U), m);
  draw_two_digits((uint8_t)(first + 6U), s);
}

/**
  * @brief  Instant Speed 显示层二次平滑（50ms 一拍，display_render 内调用）
  * @note   变速跟随：|原始−显示| > DISP_SPEED_JUMP_RPM → 每拍逼近一半（调速/启动不迟钝）；
  *         稳态小偏差 → 每拍仅跟随 1/DISP_SPEED_SS_DIV（τ≈0.4s，视觉稳定不跳字）；
  *         原始=0（停转）→ 立即归零（A9 衰减显示末端）；
  *         仅影响显示值：PID 反馈与 A12 到达判定仍用 speed_sensor 原始值
  */
static void display_smooth_speed(void)
{
  uint16_t raw = speed_sensor_get_rpm();
  int32_t  d   = (int32_t)raw - (int32_t)dispSpeed;

  if (raw == 0U)
  {
    dispSpeed = 0U;
  }
  else if (d > (int32_t)DISP_SPEED_JUMP_RPM)
  {
    dispSpeed += (uint16_t)((d + 1) / 2);
  }
  else if (d < -(int32_t)DISP_SPEED_JUMP_RPM)
  {
    dispSpeed -= (uint16_t)((-d + 1) / 2);
  }
  else
  {
    /* 稳态：小偏差强平滑（d/DIV 向零截断自带 ≈±DIV RPM 显示死区，防跳字） */
    dispSpeed = (uint16_t)((int32_t)dispSpeed + d / (int32_t)DISP_SPEED_SS_DIV);
  }
}

/* ====================== 业务渲染 ====================== */

/**
  * @brief  整屏业务刷新：重建 RAM 镜像（数据经各模块 getter 周期拉取）
  * @note   编辑字段在闪烁灭相消隐；有变化才置脏（Task 50ms 调用）
  */
static void display_render(void)
{
  uint16_t cfgRpm;
  uint16_t cfgH;
  uint8_t  cfgM;
  uint8_t  cfgS;
  uint16_t remH;
  uint8_t  remM;
  uint8_t  remS;
  app_mode_t   mode;
  edit_field_t edit;
  uint8_t f;

  memset(lcdRam, 0U, HT1623_RAM_NIBBLES);
  lcdDirty = 0U;

  mode    = app_state_get_mode();
  edit    = app_state_get_edit();
  cfgRpm  = app_state_get_cfg_rpm();
  app_state_get_cfg_time(&cfgH, &cfgM, &cfgS);
  timer_get_rem(&remH, &remM, &remS);

  /* Instant Speed 平滑更新放最前：图标与字段同用当拍值（无帧滞后） */
  display_smooth_speed();

  /* ---- 图标 ---- */
  /* 圆图标 = 显示平滑转速 > 0（闭环后数据源切 speed_sensor，真实转动状态；2026-09-15） */
  set_role_icon((uint8_t)ROLE_MOTOR, (dispSpeed > 0U) ? 1U : 0U);
  set_role_icon((uint8_t)ROLE_CLOCK, timer_is_active());
  set_role_icon((uint8_t)ROLE_ARROW, (mode == APP_RUNNING) ? 1U : 0U);
  set_role_icon((uint8_t)ROLE_WARN, 0U);
  set_role_icon((uint8_t)ROLE_SENSOR, temp_sensor_is_valid());

  /* ---- Set Speed：编辑中（EDIT_SPEED）且灭相 → 消隐 ---- */
  if (!((edit == EDIT_SPEED) && (blinkPhase == 0U)))
  {
    draw_number_rl(fieldLayout[FLD_SET_SPEED].first,
                   fieldLayout[FLD_SET_SPEED].len, cfgRpm);
  }

  /* ---- Set Time：H/M/S 分区闪烁（当前编辑子字段） ---- */
  f = fieldLayout[FLD_SET_TIME].first;
  if (!((edit == EDIT_TIME_H) && (blinkPhase == 0U)))
  {
    draw_number_rl(f, 4U, cfgH);
  }
  if (!((edit == EDIT_TIME_M) && (blinkPhase == 0U)))
  {
    draw_two_digits((uint8_t)(f + 4U), cfgM);
  }
  if (!((edit == EDIT_TIME_S) && (blinkPhase == 0U)))
  {
    draw_two_digits((uint8_t)(f + 6U), cfgS);
  }

  /* ---- Instant Speed：实测转速 + 显示层二次平滑（真实值低通版，稳态不跳字） ----
        变速跟随：|原始−显示| > DISP_SPEED_JUMP_RPM 快速跟随（/2）；否则每拍 1/8 强平滑；
        停转（原始=0）立即归零。控制链（PID/A12）仍用 speed_sensor 原始值，不受影响。 */
  draw_number_rl(fieldLayout[FLD_INST_SPEED].first,
                 fieldLayout[FLD_INST_SPEED].len, dispSpeed);

  /* ---- Instant Temp：探头在位才显示（P4 前恒 0 → 熄灭）；有效时显占位 8888 ---- */
  if (temp_sensor_is_valid() != 0U)
  {
    draw_number_rl(fieldLayout[FLD_INST_TEMP].first,
                   fieldLayout[FLD_INST_TEMP].len, 8888U);
  }

  /* ---- Remaining Time ---- */
  draw_time_field(fieldLayout[FLD_REM_TIME].first, remH, remM, remS);

  /* 缓冲有变化才置脏（避免每 50ms 全量串行写屏阻塞主循环） */
  if (memcmp(lcdRam, lcdRamPrev, HT1623_RAM_NIBBLES) != 0)
  {
    memcpy(lcdRamPrev, lcdRam, HT1623_RAM_NIBBLES);
    lcdDirty = 1U;
  }
}

/* ====================== 模块接口 ====================== */

void display_init(void)
{
  memset(lcdRam, 0, sizeof(lcdRam));
  memset(lcdRamPrev, 0, sizeof(lcdRamPrev));
  lcdDirty    = 0U;
  lcdManual   = 0U;
  blinkPhase  = 0U;

  ht1623_init();   /* 端口 + 清屏 + RC32K + 开显示 */
  evt_bus_subscribe(EVT_MOD_DISPLAY, EVT_BLINK_TOGGLE);
}

void display_on_event(evt_type_t type, uint16_t val)
{
  if (type == EVT_BLINK_TOGGLE)
  {
    blinkPhase = (uint8_t)val;   /* 与 app_state 相位同步（回调只刷新缓存） */
  }
}

void display_ops(void)
{
  if (lcdManual != 0U)
  {
    /* 手动调试模式：暂停业务渲染，仅按脏标记送屏 */
    if (lcdDirty != 0U)
    {
      ht1623_write_ram(0U, lcdRam, HT1623_RAM_NIBBLES);
      lcdDirty = 0U;
    }
    return;
  }

  display_render();
  if (lcdDirty != 0U)
  {
    ht1623_write_ram(0U, lcdRam, HT1623_RAM_NIBBLES);
    lcdDirty = 0U;
  }
}

void display_request_refresh(void)
{
  memset(lcdRamPrev, 0xFFU, HT1623_RAM_NIBBLES);   /* 强制下轮判定有差异 → 整片重写 */
}

void display_debug_all(uint8_t on)
{
  lcdManual = 1U;
  lcd_all(on);
}

void display_debug_digit(uint8_t n)
{
  lcdManual = 1U;
  lcd_all(0U);
  lcd_show_digit(n, 8U);
}

void display_debug_icon(uint8_t s)
{
  lcdManual = 1U;
  lcd_all(0U);
  lcd_set_icon(s, 1U);
}

void display_debug_auto(void)
{
  lcdManual = 0U;
  display_request_refresh();
}
