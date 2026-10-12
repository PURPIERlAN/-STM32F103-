/**
  ******************************************************************************
  * @file    key.h
  * @author  Xu Xiansen / K
  * @brief   五按键扫描（GPIO 上拉输入、按下接地）
  *
  * 按键定义（报告第 3 节）：
  *   UP=PB12, DOWN=PB13, LEFT=PB14, RIGHT=PB15, PAUSE=PA8
  * 消抖：每 25 ms 采样一次（TIM2 节拍驱动），连续两次采样一致才确认，
  *       可滤除普通机械触点抖动（报告第 5 节）。
  * ******************************************************************************
  */
#ifndef __KEY_H
#define __KEY_H

/* 按键事件（数值与内部扫描表一一对应，勿乱改） */
typedef enum
{
    KEY_EVT_NONE  = 0u,
    KEY_EVT_UP    = 1u,   /* PB12 */
    KEY_EVT_DOWN  = 2u,   /* PB13 */
    KEY_EVT_LEFT  = 3u,   /* PB14 */
    KEY_EVT_RIGHT = 4u,   /* PB15 */
    KEY_EVT_PAUSE = 5u    /* PA8  */
} KeyEvent;

/* 初始化按键 GPIO（上拉输入） */
void Key_Init(void);

/* 按键扫描：由主循环每 25 ms 调用一次，返回本次确认的按下事件。
 * 按住不放只报一次（松开后重新按下才有新事件） */
KeyEvent Key_Scan(void);

#endif /* __KEY_H */
