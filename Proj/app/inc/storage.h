/*******************************************************************************
* @filename:  storage.h
* @brief:     参数存储模块：末页槽磨损均衡、加载/分步写入、掉电保护（架构第 9 节）
* @note       确认时机（单击/5s/START，A15）→ storage_request_save 入队 →
*              storage_ops 每 10ms 写 1 个 word（写满 16 槽整页擦除重来）
*******************************************************************************/
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* 用户设定参数（权威副本在 app_state：cfg_rpm/cfg_h/m/s） */
typedef struct {
  uint16_t rpm;
  uint16_t h;
  uint8_t  m;
  uint8_t  s;
} user_param_t;

void    storage_init(void);                            /* 扫描最后有效槽 → 参数；无 → 默认（A13） */
void    storage_ops(void);                             /* 10ms：分步写（每周期 ≤1 word） */
uint8_t storage_request_save(const user_param_t *p);   /* 请求持久化；1=已入队 */
void    storage_get_params(user_param_t *p);

#endif /* STORAGE_H */
