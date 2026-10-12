# 贪吃蛇代码-key按键扫描

> `App/key.h` + `App/key.c` — 五按键扫描：25ms 双采样消抖、按下沿触发、按住不重复。表驱动 + 两级状态变量的消抖范本。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/key.h` / `.c`（约 90 行） |
| 职责 | 把"引脚电平"翻译成"按键事件"（按下瞬间发一次，之后按住不再发） |
| 硬件 | PB12~15 = 上下左右、PA8 = 暂停；上拉输入，按键另一端统一接 GND → [[LED点阵贪吃蛇项目]] |
| 原理页 | 消抖原理详解 → [[按键消抖]] |

## 完整代码：key.h

```c
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
```

## 完整代码：key.c

```c
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

    /* 上拉输入：按键一端接 GPIO，另一端统一接 GND，按下读 0 */
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
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. 扫描表：`GPIO_TypeDef *const` 数组

```c
static GPIO_TypeDef *const s_port[5] = { GPIOB, GPIOB, GPIOB, GPIOB, GPIOA };
static const uint16_t      s_pin[5]  = { GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14,
                                         GPIO_Pin_15, GPIO_Pin_8 };
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **复杂声明解剖**：`GPIO_TypeDef *const s_port[5]` = "5 个元素的数组，每元素是 **const 指针**（指向可变，指针本身不可变）"（→ [[C语言复杂类型声明]] 完整规则） | 5 个按键分属 GPIOB/GPIOA 两个端口——用**表驱动**代替 5 段复制粘贴代码。加第 6 个键只需在表里加一行 |
| `const` 的双重含义：这里保护**指针本身**不被改写 | 表是编译期常量（进 Flash 不占 RAM 副本）。GPIOB 这样的"端口地址"是库里的外设结构体宏（强制类型转换过的地址常量） |
| `static`：表只属于本文件 | 扫描表是实现细节，不进 .h——**封装**（→ [[C语言函数与程序结构]]） |

### 2. Key_Init：上拉输入为什么"按下读 0"

```c
gpio.GPIO_Mode = GPIO_Mode_IPU;   /* 内部上拉输入 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 位掩码组合 `GPIO_Pin_12 \| GPIO_Pin_13 \| ...` 一次配四个脚 | IPU（上拉输入）：引脚内部接着一个弱上拉电阻，**没人碰它时读 1**；按键按下把引脚直通 GND，读 0。所以"按下=0" |
| —— | 上拉的好处：**不用外接电阻**、悬空不漂浮（对比浮空输入 IN_FLOATING 读噪声）。引脚图见 [[LED点阵贪吃蛇项目]] |

### 3. Key_Scan：两级状态 + 双采样消抖

```c
uint8_t pressed = (GPIO_ReadInputDataBit(...) == Bit_RESET) ? 1u : 0u;   /* 读 0 → 按下=1 */

if (pressed == s_last[i])            /* ① 本次与上次一致，才值得看 */
{
    if (pressed && !s_stable[i])     /* ② 稳定态是"没按"，但连续两次读到"按下" */
        { s_stable[i] = 1;  evt = ...; }   /* → 确认按下，发事件 */
    else if (!pressed && s_stable[i])
        { s_stable[i] = 0; }         /* ③ 连续两次读到"松开" → 确认释放 */
}
s_last[i] = pressed;                 /* 记住本次，供下轮对照 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `s_last`（上次瞬时电平）与 `s_stable`（已确认稳态）**两级变量**——这是本文件的核心设计 | 机械触点闭合瞬间会弹跳几 ms（电平乱跳）。**25ms 间隔采两次、两次相同才认账**：弹跳期内的不一致样本直接被 ① 滤掉。参数依据见 [[按键消抖]] |
| 调用时机约定：main 每 25ms 调一次 | 消抖参数"25ms"不在 key.c 里，而在**调用节拍**里——函数与调用方的契约写在注释里。改消抖时间 = 改 main 的时间片 |
| `evt = (KeyEvent)(KEY_EVT_UP + i)`：**枚举算术** | 枚举值 1~5 与数组下标 0~4 平移对应，一次循环同时扫 5 个键。头文件里"勿乱改"的注释就是警告这个**位置耦合**——表驱动省代码的代价是约定脆弱，必须注释 |
| `if (evt == KEY_EVT_NONE)` 只收第一个事件 | 一圈最多报一个键（同圈多键按下，后面的键下一圈再报）——**一次一个事件**避免 main 端处理歧义 |

### 4. 边沿触发语义：按住为什么不连发

```
时刻线（每格 25ms）：
按下 → 弹跳 →  采样1=1, 采样2=1  →  stable 0→1，【发事件】
按住 →      采样全 1          →  stable 已是 1，不再发
松开 → 弹跳 →  采样1=0, 采样2=0  →  stable 1→0，【不发事件，只复位】
再按 →                      →  才有下一次事件
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 状态转移图（0→1 触发、1→1 保持、1→0 复位）就是**每个按键一个微型状态机** | 游戏里"按住上键蛇不该一直转向"——方向键要的是**事件**（下压瞬间）而不是**电平**（持续状态）。"松开确认"分支看似没输出，实为下一次触发的必要前提 |

## 本页 C 语言知识索引

- [[C语言复杂类型声明]] — `GPIO_TypeDef *const arr[5]` 实例
- [[C语言指针]] — const 指针数组
- [[C语言数组与字符串]] — 表驱动
- [[C语言基本数据类型与表达式]] — 枚举算术、三目运算、位掩码
- [[C语言流程控制]] — 两级状态判断

## 本页程序原理索引

- [[按键消抖]] — 为什么是 25ms 双采样、抖动波形图
- [[STM32F103C8T6]] — GPIO 上拉输入模式
- [[LED点阵贪吃蛇项目]] — 按键接线（另一端共地）
- [[贪吃蛇代码-main主循环与时间片]] — 25ms 调用节拍与事件分发

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图
- [[贪吃蛇代码-snake_game状态机]] — 事件最终去哪（QueueDirection / TogglePause）

## Sources

- [key.h](../sources/SnakeGame_Keil5源码/key.h) / [key.c](../sources/SnakeGame_Keil5源码/key.c) — 2026-09-18 起
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 第 3/5 节按键与消抖，2026-09
