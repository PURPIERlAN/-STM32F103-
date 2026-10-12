# 贪吃蛇代码-main主循环与时间片

> `User/main.c` — 系统集成入口：TIM2 1ms 心跳驱动主循环四件事（25ms 扫按键 / 50ms 刷动画点阵 / 按移动周期推进游戏 / 按需刷 OLED），外加 ADC 噪声随机种子。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/User/main.c`（约 270 行） |
| 职责 | 初始化全部外设 → 无限主循环做**时间片调度**，自己不实现任何具体功能 |
| 上游 | `g_msTicks` 由中断提供（→ [[贪吃蛇代码-中断节拍与volatile]]） |
| 下游 | 调用 App 层全部模块（snake_game / max7219 / oled / key / buzzer） |

## 在工程分层中的位置

```
┌─ User/main.c ←—— 你在这里：调度者（只管"什么时候做什么"）
├─ App/snake_game.c     纯游戏逻辑（管"游戏规则"）
├─ App/max7219.c        点阵驱动（管"显示硬件"）
├─ App/oled.c           OLED 驱动
├─ App/key.c            按键扫描
├─ App/buzzer.c         蜂鸣器
└─ Drivers/…            标准外设库（管"寄存器"）
```

main.c 里**没有一行游戏规则、没有一行寄存器操作**——它只回答"此刻该轮到谁干活"。这就是裸机程序的**前后台架构**：中断是"前台"（提供心跳），主循环是"后台"（干所有活）。

## 完整代码

```c
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
    OLED_Init();       /* I2C1 + SSD1306 */
    Key_Init();        /* 五按键上拉输入 */
    Buzzer_Init();     /* PA0 蜂鸣器 */

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
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. 全局数据声明

```c
volatile uint32_t g_msTicks = 0u;   /* 中断里递增 */
static SnakeGame  s_game;           /* 整个游戏就这一个实例 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `volatile`：告诉编译器"这个变量随时会变，每次都要真读内存，不许缓存到寄存器优化掉" | `g_msTicks` 被中断改、主循环读——不加 volatile，开优化后主循环可能永远读到旧值（详见 [[贪吃蛇代码-中断节拍与volatile]]） |
| `static` 修饰全局变量：把作用域限制在本文件内 | 模块封装：`s_game` 是整个系统的"游戏状态单体"，别处不许直接摸，只能通过函数接口访问 |
| `uint32_t` 定宽类型（来自 `<stdint.h>`） | 嵌入式里类型宽度必须明确：32 位毫秒计数约 49.7 天才回绕一次 |

### 2. 时间片调度骨架（主循环的核心套路）

```c
while (1)
{
    uint32_t now = g_msTicks;
    if ((now - s_keyT) >= 25u)  { /* 扫按键 */ s_keyT = now; }
    if ((now - s_animT) >= 50u) { /* 刷动画 */ s_animT = now; }
    if (... (now - s_moveT) >= SnakeGame_MovePeriod(&s_game)) { /* 推进游戏 */ s_moveT = now; }
    ...
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **无符号减法天然抗回绕**：`now - s_keyT` 即便发生 0x00000000→0xFFFFFFFF 回绕，差值仍然正确 | 这是嵌入式时间片调度**标准写法**。不要写 `if (now >= s_keyT + 25)`——加法会溢出回绕出错，减法不会 |
| `switch-case` + `break`（→ [[C语言流程控制]]） | 按键事件分发：一个事件一个动作，`default` 兜底 |
| 函数调用作条件：`SnakeGame_MovePeriod(&s_game)` | 游戏速度是**变量**——吃得越多周期越短，同一个调度骨架自动适应 |

一句话理解时间片调度：**把 CPU 想象成一个同时盯四个闹钟的厨师**——哪个闹钟响了就做那道菜的"一步"，做完继续回头盯表。没有操作系统，也能"同时"干四件事。

### 3. 死亡/进食音效的触发时机

```c
uint16_t scoreBefore = s_game.score;
SnakeGame_Tick(&s_game);
if (s_game.state == SNAKE_STATE_DEAD)      App_BeepStart(500u);
else if (s_game.score != scoreBefore)      App_BeepStart(60u);
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| "调用前后快照对比"模式：先存旧值再调用，靠差分推断发生了什么 | `SnakeGame_Tick` 不主动发声（逻辑层零硬件依赖），main 层负责把"逻辑变化"翻译成"硬件反馈"——分层职责的教科书操作 |
| `else if` 链：死亡判断优先于进食判断 | 撞墙那一步即使"吃到食物"也不会加分成死亡（Tick 里碰撞判断在进食之前 return），但保险起见仍先判死亡 |

### 4. OLED 按需刷新（脏标记思想）

```c
static uint16_t s_shownScore = 0xFFFFu;   /* 初始值故意不可能是真实分数 */
if ((s_game.score != s_shownScore) || ... ) { App_DrawOled(&s_game); s_shown... = ...; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 用"上一次显示值"做**影子副本**，三个 `!=` 比较就是脏检查 | I2C 刷一整屏要几毫秒，没必要每圈都刷——**只在数据变了才刷**，主循环时间片更稳 |
| 哨兵初值 `0xFFFF` / `-1`：保证第一次必定刷新 | 上电第一次比较必然"不相等"，避免"上电不显示"的初始化遗漏 bug |

### 5. 蜂鸣器非阻塞计时

```c
s_beepEnd = g_msTicks + (uint32_t)ms;   /* 开响：记下结束时刻 */
...
if ((s_beeping != 0u) && (((int32_t)(now - s_beepEnd)) >= 0)) { Buzzer_Off(); s_beeping = 0u; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **绝不用 delay 等待**——响多久是"记一个截止时刻"而不是死等 | 阻塞 500ms 会让按键扫描、显示全停摆。非阻塞 = 状态 + 时刻，主循环每圈顺手检查 |
| 强转 `(int32_t)(now - s_beepEnd) >= 0`：用**有符号比较**处理"还没到点" | 无符号时"没到点"是大正数，没法和"已过点"区分方向；转 int32 后负数=没到点。和第 2 段的"无符号减法"是一对孪生技巧，适用场景不同 |

### 6. ADC 噪声随机种子

```c
gpio.GPIO_Mode = GPIO_Mode_AIN;                    /* PA1 设为模拟输入且悬空 */
seed = (uint16_t)((seed << 3) | (seed >> 13));     /* 16 位循环左移 3 位 */
seed ^= (uint16_t)ADC_GetConversionValue(ADC1);    /* 异或混入本次采样 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 移位 + 或 + 异或的位运算组合（→ [[C语言基本数据类型与表达式]]）；`seed>>13` 与 `seed<<3` 拼成**循环移位** | 浮空引脚的电平是热噪声+工频干扰，天然不可预测 → 采样值就是熵源。完整流程详见 [[ADC噪声随机种子]] |
| `while (EOC == RESET) && (--t != 0u)` 带超时计数 | 即使硬件异常也不死等——超时保护是嵌入式 I/O 的标配习惯 |

### 7. 手写整数转字符串

```c
while (v != 0u) { tmp[n++] = '0' + (v % 10u); v /= 10u; }   /* 逐位取出（低位在前） */
for (i = 0; i < n; i++) buf[i] = tmp[n-1-i];                /* 反转成正序 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `%10` 取个位、`/10` 去掉个位——进制分解的基本功；`'0' + x` 利用 ASCII 连续性把数字变字符 | 不用 `sprintf`/`itoa`：MDK 微库里 printf 一族会拉大好几 KB 代码体积（32KB 限制要省着用），手写 15 行更划算（→ [[Keil工程配置踩坑]]） |
| `tmp[6]`：uint16 最大 65535 = 5 位 + `\0`，刚好 | 边界意识：缓冲区大小要按"最大可能值"算，不留隐患 |

### 8. 按键事件 → 游戏动作的翻译官

```c
static void App_HandleDir(SnakeDir d)
{
    if (s_game.state == SNAKE_STATE_READY)  { SnakeGame_Start(...); QueueDirection(...); s_moveT = g_msTicks; }
    else if (s_game.state == SNAKE_STATE_DEAD) { SnakeGame_Init(..., g_msTicks 混合); SnakeGame_Start(...); ... }
    else { SnakeGame_QueueDirection(&s_game, d); }
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 按状态分支的 if-else 链——把"同一个输入在不同状态下含义不同"显式化 | 同一个方向键：READY 时是"开始键"、DEAD 时是"重开键"、RUN 时才是"转向键"。这叫**输入语义随状态变化**，是状态机应用的活例子（→ [[贪吃蛇游戏逻辑]]） |
| `(uint16_t)((g_msTicks & 0xFFFFu) ^ (g_msTicks >> 7))` | 重开局时毫秒计数被你按键的时机"搅乱"，成了第二个熵源——死亡重开的食物位置每次不同 |

### 9. 点阵绘制：逻辑到硬件的最后一跳

```c
for (row = 0; row < 8; row++) {
    bits = 0u;
    for (col = 0; col < 8; col++)
        if (SnakeGame_Pixel(g, col, row))  bits |= (1u << col);
    MAX7219_SetRow(row, bits);
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 双层 for 遍历 8×8；`bits |= (1u << col)` 把"该格亮不亮"**打包成一个字节**的 8 个位 | 逻辑层给的是逐格 true/false，MAX7219 要的是一行一个字节（→ [[MAX7219]]）。这里的位打包就是"逻辑坐标 → 硬件位图"的翻译 |
| `const SnakeGame *g` 只读指针传参 | 绘制函数被明令禁止改游戏状态——用 const 把设计意图写进类型系统 |

## 本页 C 语言知识索引

- [[C语言基本数据类型与表达式]] — volatile、位运算、定宽类型、ASCII 算术
- [[C语言流程控制]] — while(1)、switch-case、if-else 链
- [[C语言函数与程序结构]] — static 全局/函数、声明与定义分离
- [[C语言数组与字符串]] — 手写 itoa 的数组操作
- [[C语言指针]] — 结构体指针传参、const 限定

## 本页程序原理索引

- [[定时器节拍与时间片调度]] — 本页就是该模式的完整实现
- [[ADC噪声随机种子]] — 第 6 段的原理详解
- [[STM32F103C8T6]] — 72MHz 时钟、TIM2、NVIC 优先级
- [[贪吃蛇游戏逻辑]] — Tick/Pixel 被调用的规则
- [[三极管驱动蜂鸣器]] — Buzzer_On/Off 背后的电路

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图
- [[贪吃蛇代码-中断节拍与volatile]] — g_msTicks 的生产者
- [[贪吃蛇代码-snake_game状态机]] — 被调度的核心逻辑
- [[LED点阵贪吃蛇项目]] — 项目主页与参数表

## Sources

- [main.c](../sources/SnakeGame_Keil5源码/main.c) — 2026-09-18 起
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 第 2 节主循环设计，2026-09
