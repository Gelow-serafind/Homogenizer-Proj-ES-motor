# Homogenizer Software Demand Specification

## 1. Background

This project targets a homogenizer controller based on HK32F0301MF4P7C.
Core interaction is implemented by one EC11 rotary encoder (rotation + push key), one PWM output for motor drive, and one display page that shows speed and time.

This document refines customer narrative requirements into implementable software requirements.

## 2. Scope

- Implement operating logic for power-on, parameter setting, start/pause, and in-run parameter update.
- Use time-slice task scheduling and state machine architecture.
- Simulate display UI by UART `printf`, rendering a framed ASCII screen every 100 ms.

Out of scope:
- Real LCD driver implementation.
- Non-volatile persistence (EEPROM/Flash write-back).
- Closed-loop motor control.

## 3. Terms

- Speed: target motor speed percentage (0% to 100%, step 5%).
- Time: countdown time shown as `MM:SS`.
- Short press: press and release without long-press threshold reached.
- Long press: continuous key press for >= 3 seconds.

## 4. System Inputs and Outputs

## 4.1 Inputs

- Encoder rotation:
  - CW: increment selected parameter.
  - CCW: decrement selected parameter.
- Encoder key:
  - Short press: enter/advance setting flow.
  - Long press (3 s): start or pause.

## 4.2 Outputs

- PWM duty updated according to current speed setting.
- UART screen (100 ms refresh) showing:
  - Mode
  - Speed
  - Time
  - Key state
  - Encoder position (debug)

## 5. Functional Requirements

## 5.1 Power-on behavior

1. On power-up, system enters `IDLE` mode.
2. Default speed parameter shown is `0`.
3. Time shown is the last configured value in RAM model (initial default can be `01:00` if no persistence source).
4. No motor work starts automatically after power-up.

## 5.2 Parameter setting flow

1. First short press enters setting mode and starts from `SPEED` field.
2. While in setting mode, each short press switches the current selected field in fixed order: `SPEED -> MIN -> SEC -> SPEED -> ...`.
3. The selected field must blink to indicate edit focus.
4. Blink period is 500 ms.
5. Entering setting mode resets a 5-second inactivity timer.
6. Any short press that changes selected field resets the 5-second inactivity timer.
7. Any encoder rotation that changes the current selected value resets the 5-second inactivity timer.
8. If there is no operation for 5 seconds in setting mode, blinking stops and system exits setting mode.
9. Exiting setting mode must not auto-start motor when the system was not previously running.

## 5.3 Start and pause behavior

1. Long press (3 s) starts work from setting screen or non-setting screen.
2. In `RUNNING` mode, long press (3 s) pauses work.
3. When work starts, countdown decrements by real-time seconds (1 min = 60 s).

## 5.4 Parameter update during running

1. During running, short press enters setting mode without stopping motor.
2. Motor continues running while in in-run setting mode.
3. While in running-setting mode, encoder changes are applied to temporary edit values only.
4. While in running-setting mode, actual running target speed and actual countdown do not update immediately.
5. Only when setting mode exits are the edited speed and time committed as the new running target.
6. After exit from running-setting mode, PWM target speed updates to the committed speed.
7. After exit from running-setting mode, countdown time updates to the committed `MIN:SEC` value.

## 5.5 Boundaries

1. Speed range: `0..100` (%), step = `5`.
2. Minutes range: `0..99`.
3. Seconds range: `0..59`.
4. Countdown lower bound: `0`.

## 6. State Machine Requirements

## 6.1 States

- `IDLE`: not running, not setting.
- `SETTING`: editing one parameter field.
- `RUNNING`: motor active, countdown active.
- `PAUSED`: motor stopped by user or countdown end.

## 6.2 Events

- `EVT_KEY_SHORT`
- `EVT_KEY_LONG`
- `EVT_ENC_CW` / `EVT_ENC_CCW`
- `EVT_1S_TICK`
- `EVT_BLINK_TOGGLE`
- `EVT_SETTING_IDLE_TIMEOUT`

## 6.3 Main transitions

- `IDLE` + short -> `SETTING(SPEED)`
- `IDLE` + long -> `RUNNING`
- `SETTING` + short -> switch selected field to next one in cycle
- `SETTING` + no operation for 5 s -> exit to previous non-setting mode and commit edited values
- `SETTING` + long -> commit edited values and enter `RUNNING`
- `RUNNING` + long -> `PAUSED`
- `RUNNING` + short -> `SETTING(SPEED)` while motor keeps running at old target
- `PAUSED` + long -> `RUNNING`
- Any state + countdown reaches 0 while running -> `PAUSED`

## 7. Scheduling and Timing Requirements

Use cooperative task polling with per-task countdown counters:

- Encoder task: 2 ms (high frequency)
- Application state machine task: 10 ms
- PWM apply task: 5 ms
- UI print task: 100 ms
- Default/housekeeping task: 1000 ms

SysTick must run at 1 ms and decrement all task counters.

## 8. Display (UART Screen Simulation)

1. Refresh period fixed at 100 ms.
2. UART may use a compact single-line status output during logic debugging.
3. Active setting field should blink by replacing visible value with placeholder characters.
4. Show mode text clearly (`IDLE/SETTING/RUNNING/PAUSED`).

## 9. Non-functional Requirements

- Deterministic response to encoder and key events.
- No blocking delays in main loop tasks.
- Keep motor PWM update decoupled from UI print frequency.
- Maintain readable module structure and clear task responsibilities.

## 10. Acceptance Test Checklist

1. Power-on shows speed `0` and configured time value; motor not running.
2. Short press enters setting and highlights `SPEED`.
3. Rotation changes selected field with expected step and boundary behavior.
4. Each short press in setting switches selected field `SPEED -> MIN -> SEC -> SPEED`.
5. Setting mode exits only after 5 seconds with no operation, and does not auto-start from idle/paused context.
6. Long press from `IDLE` or `SETTING` starts running.
7. Long press during `RUNNING` pauses motor.
8. Countdown decreases once per second while running.
9. Short press during running enters setting, and motor keeps running.
10. Changing speed during running does not update PWM until setting exits.
11. Changing min/sec during running does not update countdown until setting exits.
12. UART screen refreshes at 100 ms and reflects current state correctly.

## 11. Implementation Note

Current implementation maps this demand to `src/main.c` by task scheduler + event-driven app state machine.
Future enhancement can split into modules:

- `app_state.c/.h` for business state machine
- `input_ec11.c/.h` for encoder and key events
- `ui_uart_screen.c/.h` for screen rendering
- `motor_pwm.c/.h` for PWM abstraction
