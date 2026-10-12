/**
  ******************************************************************************
  * @file    stm32f10x_conf.h
  * @author  Xu Xiansen / K
  * @brief   标准外设库配置：只包含本工程用到的外设头文件
  * ******************************************************************************
  */
#ifndef __STM32F10X_CONF_H
#define __STM32F10X_CONF_H

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_flash.h"
#include "stm32f10x_adc.h"
#include "misc.h"

#ifdef  USE_FULL_ASSERT
/* 断言失败时进入该函数（可在此打断点定位违规参数） */
void assert_failed(uint8_t* file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t*)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0)
#endif /* USE_FULL_ASSERT */

#endif /* __STM32F10X_CONF_H */
