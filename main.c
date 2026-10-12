/**
  ******************************************************************************
  * @file    main.c
  * @author  Xu Xiansen / K
  * @brief   LED 点阵贪吃蛇——系统集成入口（STM32F103C8T6，标准外设库 3.5）
  *
  * 系统（报告第 2 节）：
  *   - 主控 STM32F103C8T6，上电 SystemInit() 配置 72 MHz（HSE 8MHz x PLL9）
  *   - TIM2 每 1 ms 产生系统节拍（1 kHz）
  *   - SPI1 连接 MAX7219 点阵；I2C1 连接 SSD1306 OLED
  *   - 五按键上拉输入、按下接地；PA0 蜂鸣器（可选）
  *
  * 主循环只做四件事（报告第 2 节）：
  *   1) 按固定周期（25 ms）扫描按键；
  *   2) 到达移动周期时更新游戏（350ms 起，每分 -12ms，下限 110ms）；
  *   3) 持续刷新点阵（食物/死亡闪烁相位）；
  *   4) 更新 OLED 分数与状态。
  * ******************************************************************************
  */
#include "stm32f10x.h"
#include "snake_game.h"
#include "max7219.h"
#include "oled.h"
#include "key.h"
#include "buzzer.h"

/* TIM2 1ms 节拍计数（stm32f10x_it.c 中断里递增） */
volatile uint32_t g_msTicks = 0u;

static SnakeGame s_game;

/* 蜂鸣器提示音计时 */
static uint32_t s_beepEnd = 0u;
static uint8_t  s_beeping = 0u;

/* 主循环时间片变量 */
static uint32_t s_keyT = 0u;        /* 按键上次采样时刻 */
static uint32_t s_animT = 0u;       /* 动画上次刷新时刻 */
static uint32_t s_moveT = 0u;       /* 游戏上次推进时刻 */
static uint16_t s_shownScore = 0xFFFFu;
static uint16_t s_shownPeriod = 0u;
static int      s_shownState = -1;

/* ---------------- 内部函数 ---------------- */

static void App_TIM2_Init(void);
static uint16_t App_GetAdcNoiseSeed(void);
static void App_DrawMatrix(const SnakeGame *g);
static void App_DrawOled(const SnakeGame *g);
static void App_HandleDir(SnakeDir d);
static void App_BeepStart(uint16_t ms);
static void App_U16ToStr(uint16_t v, char *buf);

int main(void)
{
    /* SystemInit() 已由启动文件调用：HSE 8MHz * 9 = 72MHz，FLASH 2 等待 */

    App_TIM2_Init();   /* 1ms 系统节拍 */

    MAX7219_Init();    /* SPI1 + 点阵 */

    /* ===== 开机链路自检（2026-09-21 新增，验收后可整段删除）=====
     * 显示测试模式 = 芯片强制点亮全部 64 个 LED（与显存内容无关）。
     * 上电后应看到：全亮 1 秒 -> 灭一下 -> 游戏待机画面。
     *   全亮出现 -> CS/CLK/DIN 三线全通，SPI 命令能到达 MAX7219；
     *   纹丝不动 -> 命令没锁存进去（查线 / 查供电），不用怀疑代码。 */
    MAX7219_WriteReg(MAX7219_REG_DISPLAYTEST, 0x01u);
    {
        uint32_t t0 = g_msTicks;
        while ((g_msTicks - t0) < 1000u)
        {
            /* 忙等 1 秒：TIM2 中断持续递增 g_msTicks */
        }
    }
    MAX7219_WriteReg(MAX7219_REG_DISPLAYTEST, 0x00u);

    OLED_Init();       /* I2C1 + SSD1306 */
    Key_Init();        /* 五按键上拉输入 */
    Buzzer_Init();     /* PA0 蜂鸣器 */

    /* ===== 蜂鸣器开机自检（2026-09-23 新增，验收后可整段删除）=====
     * 上电后"哔—哔"两短鸣（各 150ms）：
     *   听到     -> PA0 输出 + 蜂鸣器接线全通，死亡/进食鸣叫必然正常；
     *   没听到   -> 查接线：VCC 必须接 3V3（接 5V 会常响），SIG 接 PA0。 */
    {
        uint32_t t0 = g_msTicks;
        Buzzer_On();
        while ((g_msTicks - t0) < 150u) { }   /* 第一声 150ms */
        Buzzer_Off();
        while ((g_msTicks - t0) < 450u) { }   /* 静音 300ms */
        Buzzer_On();
        while ((g_msTicks - t0) < 600u) { }   /* 第二声 150ms */
        Buzzer_Off();
    }

    /* 随机种子：ADC 浮空通道噪声（报告 SnakeGame_Init 关键点） */
    SnakeGame_Init(&s_game, App_GetAdcNoiseSeed());

    App_DrawMatrix(&s_game);   /* READY 画面：初始蛇 + 食物 */
    App_DrawOled(&s_game);

    while (1)
    {
        uint32_t now = g_msTicks;

        /* 1. 每 25 ms 扫描一次按键（消抖由 Key_Scan 双采样完成） */
        if ((now - s_keyT) >= 25u)
        {
            KeyEvent e = Key_Scan();
            switch (e)
            {
            case KEY_EVT_UP:    App_HandleDir(SNAKE_DIR_UP);    break;
            case KEY_EVT_DOWN:  App_HandleDir(SNAKE_DIR_DOWN);  break;
            case KEY_EVT_LEFT:  App_HandleDir(SNAKE_DIR_LEFT);  break;
            case KEY_EVT_RIGHT: App_HandleDir(SNAKE_DIR_RIGHT); break;
            case KEY_EVT_PAUSE: SnakeGame_TogglePause(&s_game); break;
            default: break;
            }
            s_keyT = now;
        }

        /* 2. 每 50 ms 推进动画相位并持续刷新点阵 */
        if ((now - s_animT) >= 50u)
        {
            SnakeGame_Animate(&s_game, (uint16_t)(now - s_animT));
            App_DrawMatrix(&s_game);
            s_animT = now;
        }

        /* 3. 到达移动周期时推进游戏（RUN 状态下） */
        if ((s_game.state == SNAKE_STATE_RUN) &&
            ((now - s_moveT) >= SnakeGame_MovePeriod(&s_game)))
        {
            uint16_t scoreBefore = s_game.score;

            s_moveT = now;
            SnakeGame_Tick(&s_game);

            if (s_game.state == SNAKE_STATE_DEAD)
            {
                App_BeepStart(500u);   /* 死亡长鸣 */
            }
            else if (s_game.score != scoreBefore)
            {
                App_BeepStart(60u);    /* 进食短鸣 */
            }
        }

        /* 4. 分数/速度/状态变化时更新 OLED（死亡后保留最终分数） */
        if ((s_game.score != s_shownScore) ||
            (s_game.movePeriodMs != s_shownPeriod) ||
            ((int)s_game.state != s_shownState))
        {
            App_DrawOled(&s_game);
            s_shownScore  = s_game.score;
            s_shownPeriod = s_game.movePeriodMs;
            s_shownState  = (int)s_game.state;
        }

        /* 蜂鸣器计时到点关闭 */
        if ((s_beeping != 0u) && (((int32_t)(now - s_beepEnd)) >= 0))
        {
            Buzzer_Off();
            s_beeping = 0u;
        }
    }
}

/* ---------------- TIM2：1ms 系统节拍 ---------------- */
static void App_TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 72 MHz / 72 = 1 MHz 计数频率；计数 1000 溢出 -> 1 kHz（1 ms） */
    tim.TIM_Prescaler     = (uint16_t)(72u - 1u);
    tim.TIM_Period        = (uint16_t)(1000u - 1u);
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    nvic.NVIC_IRQChannel                   = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority        = 0u;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

/* ---------------- ADC 噪声随机种子 ----------------
 * PA1 浮空（悬空引脚），采样其热噪声/工频干扰作熵源（报告建议做法）。
 * 采样完成后关闭 ADC 以降低功耗。 */
static uint16_t App_GetAdcNoiseSeed(void)
{
    ADC_InitTypeDef  adc;
    GPIO_InitTypeDef gpio;
    uint16_t seed = 0u;
    uint8_t i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);   /* ADCCLK = 72/6 = 12 MHz (< 14 MHz) */

    gpio.GPIO_Pin  = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_AIN;     /* 模拟输入，引脚浮空 */
    GPIO_Init(GPIOA, &gpio);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1u;
    ADC_Init(ADC1, &adc);
    ADC_Cmd(ADC1, ENABLE);

    /* 校准（F1 上电必须） */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET) { }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) != RESET) { }

    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1u, ADC_SampleTime_239Cycles5);

    for (i = 0u; i < 8u; i++)
    {
        uint32_t t = 100000u;
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while ((ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) && (--t != 0u)) { }
        if (t != 0u)
        {
            seed = (uint16_t)((seed << 3) | (seed >> 13));           /* 循环左移混入 */
            seed ^= (uint16_t)ADC_GetConversionValue(ADC1);
        }
    }

    ADC_Cmd(ADC1, DISABLE);
    return seed;
}

/* ---------------- 显示：点阵 ---------------- */
static void App_DrawMatrix(const SnakeGame *g)
{
    uint8_t row, col, bits;

    for (row = 0u; row < 8u; row++)
    {
        bits = 0u;
        for (col = 0u; col < 8u; col++)
        {
            if (SnakeGame_Pixel(g, col, row) != 0u)
            {
                bits |= (uint8_t)(1u << col);
            }
        }
        MAX7219_SetRow(row, bits);
    }
}

/* ---------------- 显示：OLED ---------------- */
static void App_U16ToStr(uint16_t v, char *buf)
{
    char tmp[6];
    uint8_t n = 0u, i;

    if (v == 0u)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (v != 0u)
    {
        tmp[n] = (char)('0' + (v % 10u));
        n++;
        v = (uint16_t)(v / 10u);
    }
    for (i = 0u; i < n; i++)
    {
        buf[i] = tmp[n - 1u - i];
    }
    buf[n] = '\0';
}

static void App_DrawOled(const SnakeGame *g)
{
    char num[6];

    OLED_Clear();

    /* 大号分数（12x16 放大字形，第 0~1 页） */
    OLED_ShowBigString(0u, 0u, "SCORE:");
    App_U16ToStr(g->score, num);
    OLED_ShowBigString(78u, 0u, num);

    /* 状态行（第 6 页，6x8） */
    switch (g->state)
    {
    case SNAKE_STATE_READY:
        OLED_ShowString6x8(0u, 6u, "PRESS ARROW KEY!");
        break;
    case SNAKE_STATE_RUN:
        OLED_ShowString6x8(0u, 6u, "RUN  SPD:");
        App_U16ToStr(g->movePeriodMs, num);
        OLED_ShowString6x8(54u, 6u, num);
        OLED_ShowString6x8(54u + (uint8_t)(6u * 3u), 6u, "ms");
        break;
    case SNAKE_STATE_PAUSE:
        OLED_ShowString6x8(0u, 6u, "PAUSED");
        break;
    case SNAKE_STATE_DEAD:
    default:
        OLED_ShowString6x8(0u, 6u, "GAME OVER!");
        break;
    }

    OLED_Update();
}

/* ---------------- 按键 -> 游戏 ---------------- */
static void App_HandleDir(SnakeDir d)
{
    if (s_game.state == SNAKE_STATE_READY)
    {
        /* 待机：按任意方向键开局，并立即生效该方向 */
        SnakeGame_Start(&s_game);
        SnakeGame_QueueDirection(&s_game, d);
        s_moveT = g_msTicks;   /* 对齐移动节拍 */
    }
    else if (s_game.state == SNAKE_STATE_DEAD)
    {
        /* 死亡：按方向键重新开局，用毫秒计数增加随机性 */
        SnakeGame_Init(&s_game, (uint16_t)((g_msTicks & 0xFFFFu) ^ (uint16_t)(g_msTicks >> 7)));
        SnakeGame_Start(&s_game);
        SnakeGame_QueueDirection(&s_game, d);
        s_moveT = g_msTicks;
    }
    else
    {
        SnakeGame_QueueDirection(&s_game, d);
    }
}

/* ---------------- 蜂鸣器 ---------------- */
static void App_BeepStart(uint16_t ms)
{
    s_beepEnd = g_msTicks + (uint32_t)ms;
    s_beeping = 1u;
    Buzzer_On();
}
