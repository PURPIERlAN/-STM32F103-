/**
  ******************************************************************************
  * @file    stm32f10x_it.h
  * @author  Xu Xiansen / K
  * @brief   中断处理函数声明
  * ******************************************************************************
  */
#ifndef __STM32F10X_IT_H
#define __STM32F10X_IT_H

#include "stm32f10x.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void TIM2_IRQHandler(void);

#endif /* __STM32F10X_IT_H */
