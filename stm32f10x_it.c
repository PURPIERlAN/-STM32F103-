/**
  ******************************************************************************
  * @file    stm32f10x_it.c
  * @author  Xu Xiansen / K
  * @brief   中断服务函数
  *
  * TIM2 每 1 ms 溢出一次（报告第 2、5 节）：1ms 系统节拍。
  * 节拍只做一件极轻的事——g_msTicks++，按键扫描/游戏推进/显示刷新
  * 全部由主循环基于该计数的时间片驱动，中断里不做重活。
  * ******************************************************************************
  */
#include "stm32f10x_it.h"

extern volatile uint32_t g_msTicks;

/* Cortex-M3 系统异常（本项目未使用，保留空实现） */
void NMI_Handler(void)         { }
void HardFault_Handler(void)   { while (1) { } }   /* 死循环便于挂调试器看现场 */
void MemManage_Handler(void)   { while (1) { } }
void BusFault_Handler(void)    { while (1) { } }
void UsageFault_Handler(void)  { while (1) { } }
void SVC_Handler(void)         { }
void DebugMon_Handler(void)    { }
void PendSV_Handler(void)      { }
void SysTick_Handler(void)     { }

/* TIM2 更新中断：1ms 系统节拍 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        g_msTicks++;
    }
}
