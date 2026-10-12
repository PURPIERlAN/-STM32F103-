/**
  ******************************************************************************
  * @file    buzzer.h
  * @author  Xu Xiansen / K
  * @brief   蜂鸣器提示音（PA0，可选拓展：进食/死亡提示，报告总体方案表）
  * ******************************************************************************
  */
#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdint.h>

/* 初始化 PA0 推挽输出，默认关闭 */
void Buzzer_Init(void);

/* 打开/关闭蜂鸣器
 * 触发极性由 buzzer.c 的 BUZZER_ACTIVE_LOW 决定（当前模块：低电平响） */
void Buzzer_On(void);
void Buzzer_Off(void);

#endif /* __BUZZER_H */
