/*******************************************************************************
* @filename:  button.h
* @brief:     按键/编码器扫描模块：EC11 4 倍频解码、三键消抖、按下沿/增量事件
* @note       按键极性集中在 app_cfg.h：EC11 A/B/KEY 高有效；START/STOP 低有效；
*              消抖 3 次稳定判定（2ms×3=6ms）；事件全部经 evt_bus_post 广播
*******************************************************************************/
#ifndef BUTTON_H
#define BUTTON_H

void button_init(void);   /* EC11 A/B + 旋钮键 PC4 + START PA2 + STOP PD7 */
void button_ops(void);    /* 2ms：解码增量 → 事件；三键消抖 → 事件 */

#endif /* BUTTON_H */
