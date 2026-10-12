/**
  ******************************************************************************
  * @file    key.c
  * @author  Xu Xiansen / K
  * @brief   五按键扫描实现（25ms 双采样消抖，按下沿触发）
  * ******************************************************************************
  */
#include "key.h"
#include "stm32f10x.h"

/* 扫描表：与 KeyEvent 枚举顺序一致（UP/DOWN/LEFT/RIGHT/PAUSE） */
static GPIO_TypeDef *const s_port[5] = { GPIOB, GPIOB, GPIOB, GPIOB, GPIOA };
static const uint16_t      s_pin[5]  = { GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14,
                                         GPIO_Pin_15, GPIO_Pin_8 };

static uint8_t s_last[5]   = {0u};   /* 上一次采样电平（1=按下） */
static uint8_t s_stable[5] = {0u};   /* 已确认的稳定状态（1=按住中） */

void Key_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

    /* 上拉输入：按键一端接 GPIO，另一端统一接 GND，按下读 0（报告第 3 节） */
    gpio.GPIO_Pin  = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
}

KeyEvent Key_Scan(void)
{
    KeyEvent evt = KEY_EVT_NONE;
    uint8_t i;

    for (i = 0u; i < 5u; i++)
    {
        uint8_t pressed = (GPIO_ReadInputDataBit(s_port[i], s_pin[i]) == Bit_RESET) ? 1u : 0u;

        if (pressed == s_last[i])
        {
            /* 连续两次采样一致才改变稳定状态（25ms 间隔过滤抖动） */
            if ((pressed != 0u) && (s_stable[i] == 0u))
            {
                s_stable[i] = 1u;
                if (evt == KEY_EVT_NONE)
                {
                    evt = (KeyEvent)(KEY_EVT_UP + i);   /* 本次确认的按下事件 */
                }
            }
            else if ((pressed == 0u) && (s_stable[i] != 0u))
            {
                s_stable[i] = 0u;   /* 确认释放，允许下一次按下触发 */
            }
            else
            {
                /* 状态保持 */
            }
        }
        s_last[i] = pressed;
    }

    return evt;
}
