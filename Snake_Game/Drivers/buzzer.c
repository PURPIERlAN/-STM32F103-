/**
  ******************************************************************************
  * @file    buzzer.c
  * @author  Xu Xiansen / K
  * @brief   蜂鸣器实现（PA0 推挽输出）
  *
  * 触发极性适配（2026-09-20）：
  *   实测模块为"低电平触发"型（PA0=低 时响，=高 时停），
  *   由下方 BUZZER_ACTIVE_LOW 宏控制，换高电平触发模块时改回 0 即可。
  * ******************************************************************************
  */
#include "buzzer.h"
#include "stm32f10x.h"

/* 1 = 低电平触发模块（响=拉低）；0 = 高电平触发（响=拉高） */
#define BUZZER_ACTIVE_LOW   1u

#if (BUZZER_ACTIVE_LOW == 1u)
#define BUZZER_ON_LEVEL()   GPIO_ResetBits(GPIOA, GPIO_Pin_0)   /* 低 = 响 */
#define BUZZER_OFF_LEVEL()  GPIO_SetBits(GPIOA, GPIO_Pin_0)     /* 高 = 停 */
#else
#define BUZZER_ON_LEVEL()   GPIO_SetBits(GPIOA, GPIO_Pin_0)
#define BUZZER_OFF_LEVEL()  GPIO_ResetBits(GPIOA, GPIO_Pin_0)
#endif

void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_0;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    BUZZER_OFF_LEVEL();   /* 上电默认静音（低触发模块即拉高） */
}

void Buzzer_On(void)
{
    BUZZER_ON_LEVEL();
}

void Buzzer_Off(void)
{
    BUZZER_OFF_LEVEL();
}
