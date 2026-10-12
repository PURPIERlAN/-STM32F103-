# 贪吃蛇代码-buzzer与条件编译

> `App/buzzer.h/.c` + `User/stm32f10x_conf.h` — 全项目最短的驱动，却藏着一个重要工程思想：**用条件编译把"硬件差异"收敛到一个宏**。conf.h 则展示标准库的裁剪配置。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/buzzer.h` / `.c`（约 60 行）；`User/stm32f10x_conf.h` |
| 职责 | 蜂鸣器开/关（含触发极性适配）；标准库头文件按需包含 + 断言配置 |
| 硬件 | PA0 推挽输出 → 三极管驱动电路见 [[三极管驱动蜂鸣器]]（低电平触发实测记录） |

## 完整代码：buzzer.h

```c
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
```

## 完整代码：buzzer.c

```c
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
```

## 完整代码：stm32f10x_conf.h

```c
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
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. BUZZER_ACTIVE_LOW：一个宏管极性

```c
#define BUZZER_ACTIVE_LOW   1u

#if (BUZZER_ACTIVE_LOW == 1u)
#define BUZZER_ON_LEVEL()   GPIO_ResetBits(GPIOA, GPIO_Pin_0)   /* 低 = 响 */
#define BUZZER_OFF_LEVEL()  GPIO_SetBits(GPIOA, GPIO_Pin_0)
#else
#define BUZZER_ON_LEVEL()   GPIO_SetBits(GPIOA, GPIO_Pin_0)     /* 高 = 响 */
#define BUZZER_OFF_LEVEL()  GPIO_ResetBits(GPIOA, GPIO_Pin_0)
#endif
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `#if / #else / #endif` **条件编译**：预处理器在编译前二选一，另一支根本不生成代码（→ [[C语言编译预处理]]） | 蜂鸣器模块分"高电平触发"和"低电平触发"两种，**行为完全相反**。实测手头模块是低电平触发（5V 供电一直响、3.3V 才正常——破案过程见 [[三极管驱动蜂鸣器]]）。把差异钉死在一个宏里，换模块只改 `1u→0u` |
| `#if` 里比较的是**宏的值**，预处理期完成 | 运行时 `if (active_low)` 也能实现，但每个调用点多一次分支、且两套逻辑都在 Flash 里——**编译期决策零成本**。凡是"装好后不会变"的差异，都该用条件编译 |
| 抽象屏障：On/Off 对外语义恒定 | main.c 调 `Buzzer_On()` 永远表示"响"，至于"响是拉高还是拉低"被隔离在 .c 内部——**硬件差异不出驱动层**，这正是分层架构的价值 |

### 2. Buzzer_Init：上电默认静音

```c
gpio.GPIO_Mode  = GPIO_Mode_Out_PP;   /* 推挽输出 */
gpio.GPIO_Speed = GPIO_Speed_2MHz;    /* 蜂鸣器不需要高速 */
BUZZER_OFF_LEVEL();                    /* 先置"停"再离开 Init */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| —— | **安全默认态**：任何驱动 Init 完成后都应处于"无害状态"。蜂鸣器若上电默认响，用户第一印象就是"板子坏了" |
| GPIO_Speed 选 2MHz 而不是 50MHz | 输出速度越高边沿越陡、EMI 越大——蜂鸣器开关是毫秒级事件，2MHz 绰绰有余。**按需选参数**的习惯 |
| 电路视角 | PA0 经 1kΩ 限流电阻驱动 S8050 NPN 基极，集电极串蜂鸣器到 5V；1N4148 反向并联吸收线圈断电反冲——完整电路 → [[三极管驱动蜂鸣器]] |

### 3. stm32f10x_conf.h：标准库的总闸

```c
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"    /* 只留本项目用到的 8 个外设 */
...
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| conf.h 是**唯一**被 stm32f10x.h 包含的用户配置头，这里是"总闸" | 标准库 3.5.0 支持全部外设，头文件全开会拖慢编译、引入无关依赖。**按需列入**本项目实际用到的 gpio/rcc/spi/i2c/tim/flash/adc/misc——每个对应 main.c 或驱动里真实调用的库函数 |
| `assert_param(expr)` 宏在库源码里到处出现 | 标准库每个函数入口都检查参数合法性（比如"指针非空""引脚号合法"）。**开启 = 调试时提前死在出错点**，关掉 = 零开销发布。开发期建议开（Options → C/C++ 定义 USE_FULL_ASSERT），发布时关 |

```c
#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0 : assert_failed(__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0)
#endif
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `__FILE__ / __LINE__` 预定义宏：编译期注入文件名行号 | 断言触发时你直接知道哪个库函数的哪个参数违规——**定位效率碾压 printf 大法** |
| `(expr) ? (void)0 : assert_failed(...)` 三目强制两边同型 | C 语言的奇技：三目要求两分支类型一致，`(void)0` 就是让"什么都不做"合法的表达式 |

## 本页 C 语言知识索引

- [[C语言编译预处理]] — 条件编译、宏函数、预定义宏、include 组织（本页是三连实例）
- [[C语言函数与程序结构]] — 接口语义稳定性
- [[C语言流程控制]] — 三目运算符

## 本页程序原理索引

- [[三极管驱动蜂鸣器]] — 低电平触发的电路原理与破案过程
- [[STM32F103C8T6]] — GPIO 推挽输出、时钟门控
- [[Keil工程配置踩坑]] — USE_FULL_ASSERT 在哪开

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图
- [[贪吃蛇代码-main主循环与时间片]] — BeepStart/Off 的调用节奏（非阻塞音效）

## Sources

- [buzzer.h](../sources/SnakeGame_Keil5源码/buzzer.h) / [buzzer.c](../sources/SnakeGame_Keil5源码/buzzer.c) — 2026-09-18 起
- [stm32f10x_conf.h](../sources/SnakeGame_Keil5源码/stm32f10x_conf.h) — 2026-09-18
