
# Homogenizer Controller Project (HK32F0301MF4P7C)

<p align="center"><a href="README.zh-CN.md" style="background-color:#0078d4;color:#ffffff;padding:10px 18px;border-radius:6px;text-decoration:none;font-weight:bold;">查看中文说明 / View Chinese README</a></p>

> This README is based on the actual logic in `Proj/src/main.c` and has been cleaned of outdated legacy descriptions.
>
> This project is a proprietary commercial project owned by Hangzhou Cloudvoyage Cross-border E-commerce Co., Ltd. It is not open source and is subject to company licensing terms.

## Project Overview

This project implements a homogenizer controller prototype using the HK32F0301MF4P7C MCU. The core design is based on:

- `SysTick` 1 ms timing with a task array polling scheduler
- EC11 rotary encoder A/B 4x decoding plus short/long press key detection
- PWM motor speed control
- TM1729 LCD-style numeric display
- UART periodic status output

## Key Features

- Encoder rotation adjusts the currently selected parameter
- Short press enters setting mode or switches fields
- Long press (3 seconds) starts or pauses the motor
- Running mode decrements countdown timer; reaching zero pauses the motor
- Setting mode allows editing speed, minutes, and seconds
- 5 seconds of inactivity exits setting mode and applies changes
- When editing speed during running, PWM preview follows the edited speed

## Actual Behavior

1. On power-up, the MCU initializes UART, TIM1 PWM, EC11 GPIO, and TM1729, then displays `1234` as an LCD startup test pattern.
2. The main loop polls task handlers by period: encoder, application logic, PWM, UART print, LCD refresh, and idle.
3. UART prints a status line every 100 ms, including mode, speed, time, key state, and encoder position.
4. In `SETTING` mode, the current field blinks and a 5-second inactivity timeout exits and commits settings.
5. Long press for 3 seconds: if currently `RUNNING`, the system pauses; otherwise it starts the motor and enters `RUNNING`.
6. In running mode, TIM1 PWM output reflects the running speed; in paused/idle mode, PWM output is zero.

## Logic Details

### Input Handling

- `Task_Encoder()` runs every 2 ms:
  - Reads encoder A/B on `PC3`/`PC4` and performs 4x decoding.
  - Reads key on `PC5` with 3-tick debouncing.
  - A 3-second hold generates `KeyLongPressEvent`; releasing generates `KeyShortPressEvent`.

### State Machine

- `IDLE`: standby mode, PWM output is 0.
- `SETTING`: configuration mode, editable speed/minute/second fields with blink.
- `RUNNING`: motor is active, countdown decrements once per second.
- `PAUSED`: motor stopped, current configuration preserved.

### Key Actions

- Short press:
  - If already in `SETTING`, switches field: `SPEED -> MIN -> SEC -> SPEED`.
  - Otherwise enters `SETTING` mode.
- Long press:
  - If in `RUNNING`, pauses the motor.
  - Otherwise starts the motor.

### Start / Pause Behavior

- Start:
  - If countdown is zero, reloads from current configured minutes and seconds.
  - Sets `MotorSpeedPercent` to the configured speed.
  - Triggers PWM update.
- Pause:
  - Sets `MotorSpeedPercent` to 0.
  - PWM output goes to zero.
  - Countdown stops decrementing.

### Setting Mode Behavior

- Entering `SETTING` mode saves the current countdown as a backup so time does not continue during editing.
- When exiting `SETTING` mode:
  - If coming from `RUNNING`, it returns to running.
  - If speed was edited while running, the running PWM preview follows the edited speed.
  - The countdown value updates once editing completes.
  - If coming from `PAUSED` or `IDLE`, it returns to that state or idle.
- A 5-second inactivity timeout exits setting mode and commits changes.

### Countdown and Idle Task

- `Task_Idle()` decrements `CountdownSec` only when `RUNNING` and only once per second.
- When `CountdownSec` reaches zero, automatic pause is invoked.

## PWM and Display Logic

### TIM1 PWM

- PWM uses `TIM1 CH2` on pin `PD2`.
- The frequency target is `PWM_TARGET_FREQ_HZ = 17570UL`.
- `Task_PWM()` updates CCR2 only when `DutyChanged` is set.
- Output rules:
  - `RUNNING`: uses `MotorSpeedPercent`.
  - `SETTING` while from `RUNNING`:
    - If speed is being edited, preview the edited speed.
    - Otherwise use the current running speed.
  - Other modes: output zero.

### TM1729 Display

- `Task_TM1729()` refreshes the display every 50 ms.
- Display behavior:
  - `SETTING` with speed editing: speed is shown and blinks; RPM indicator is on.
  - `SETTING` with time editing: minutes and seconds are shown, selected field blinks; RPM indicator is off.
  - Other states: show current speed or zero with RPM indicator on.

## UART Status Output

- `Task_Print()` prints one line every 100 ms including:
  - `MODE`, `SPEED`, `TIME`, `KEY`, and `POS`
- In setting mode, the currently blinking field may show placeholder dashes.

## Keil Build Information

- Project file: `Proj/MDK-ARM/Project.uvprojx`
- Target MCU: HK32F0301MF4P7C
- Compiler: ARMCLANG V6.19
- Main groups: CMSIS, User, Driver, Board
- Include paths cover:
  - `inc/`
  - `src/`
  - `Libraries/CMSIS/...`
  - `Libraries/HK32F0301MxxC_StdPeriph_Driver/...`
  - `Boards/Board_HK32F0301MF4P7C_EVAL/...`
- Compatibility note: `NOP()` macro compatibility for ARM Compiler 6 is handled in `Libraries/HK32F0301MxxC_StdPeriph_Driver/inc/hk32f0301mxxc_def.h` by detecting `__ARMCC_VERSION` and avoiding unsupported `asm("nop")` syntax.
- Output files: `Objects/Project.axf`, `Objects/Project.hex`
- Supported programmers: ULINK, STLink, etc.

## Main Hardware Connections

- EC11 encoder: `PC3` / `PC4` / `PC5`
- PWM output: `PD2` (`TIM1_CH2`)
- LCD TM1729: `PC6` / `PC7`
- UART debug: `USART1` (`EVAL_COM1`, 115200 8N1)

## Usage and Debugging

1. Open `Proj/MDK-ARM/Project.uvprojx` in Keil.
2. Select the target device and build the project.
3. Download the program to the board.
4. Connect serial to `EVAL_COM1` at 115200 8N1.
5. Power on and verify LCD and UART output.

## Contribution

Issues and PRs are welcome. Please fork and work on a feature branch, following existing code style.

## License

This project is a proprietary commercial project owned by Hangzhou Cloudvoyage Cross-border E-commerce Co., Ltd. It is not open source. No rights are granted to copy, modify, distribute, sublicense, or sell this software without prior written permission. See `LICENSE` for details.

## Chinese Version

See `README.zh-CN.md` for the Chinese localized version.