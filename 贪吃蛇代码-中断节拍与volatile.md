# 贪吃蛇代码-中断节拍与volatile

> `User/stm32f10x_it.c/.h` + `User/system_stm32f10x.c` — 全系统唯一的中断：TIM2 每 1ms 让 `g_msTicks++`。代码 4 行，撑起整个前后台系统；volatile 与 extern 两个关键字的教科书案例。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/User/stm32f10x_it.c`（中断服务）/ `.h`（原型）；`system_stm32f10x.c`（库自带，讲原理） |
| 职责 | 1ms 系统心跳；HardFault 兜底；SystemInit 配置 72MHz 时钟 |
| 配套 | TIM2 怎么配出 1ms → [[贪吃蛇代码-main主循环与时间片]] 第 6 段；调度模式 → [[定时器节拍与时间片调度]] |

## 完整代码：stm32f10x_it.c

```c
/**
  ******************************************************************************
  * @file    stm32f10x_it.c
  * @brief   中断服务函数
  *
  * TIM2 每 1 ms 溢出一次：1ms 系统节拍。
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
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);   /* 先清标志 */
        g_msTicks++;
    }
}
```

## 完整代码：stm32f10x_it.h（关键部分）

```c
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

#endif
```

## SystemInit 原理（system_stm32f10x.c，库自带不贴全文）

上电后启动文件（startup_stm32f10x_md.s）在跳进 main 之前先调 `SystemInit()`，它把时钟树配成：

```
HSE 8MHz 外部晶振
   → PLL ×9 = 72MHz（SYSCLK）
       → AHB  = 72MHz（CPU、DMA）
       → APB1 = 36MHz（TIM2 的时钟其实×2 回 72MHz）
       → APB2 = 72MHz（GPIO/SPI1/ADC1 的时钟源）
       → ADCCLK = 72/6 = 12MHz（main.c 里 RCC_PCLK2_Div6 配的）
   → FLASH 等待周期 2（72MHz 超过 48MHz 必须 2 wait states）
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| —— | **没有 72MHz 就没有一切时序**：main.c 算的"72/72/1000=1ms"、SPI 预分频 64→1.125MHz、ADC 12MHz 上限，全都以这个时钟树为前提。改晶振频率，全工程的分频计算跟着变（→ [[STM32F103C8T6]]） |
| 启动流程链 | `复位向量 → startup_.s（堆栈初始化、数据段搬运、bss 清零）→ SystemInit() → main()`——**main 不是程序的第一行**，这一段"看不见的开机仪式"由启动文件完成 |

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. extern：跨文件的"借条"

```c
/* main.c     */ volatile uint32_t g_msTicks = 0u;   /* 定义（分配内存） */
/* stm32f10x_it.c */ extern volatile uint32_t g_msTicks;   /* 声明（借来用） */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **定义 vs 声明**：定义分配存储，`extern` 声明只说"它在别的文件里，类型长这样"（→ [[C语言函数与程序结构]]） | 变量在 main.c 定义（全工程唯一一份），中断文件借来用。**两边的类型必须一字不差**——it.c 里若漏写 volatile，编译器不报错但行为会错（见第 2 段） |
| 更工程化的做法 | extern 共享全局变量是"能跑但欠优雅"的方案；进阶方向是提供一个 `SysTick_GetMs()` 内联函数统一读取（学完函数章节可以试着重构） |

### 2. volatile：为什么漏写会出鬼

```c
volatile uint32_t g_msTicks;   /* 三个字，缺一不可 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| volatile 禁止编译器**优化掉对变量的重复读取** | 主循环里 `while ((now - s_keyT) >= 25)` 前有 `now = g_msTicks`。开 -O2 优化后，编译器可能发现"循环里没人改 g_msTicks"（它看不见中断！），于是把读取**提到循环外只读一次**——now 永远不变，时间片调度全灭，**游戏卡死在开局** |
| volatile = "每次都老实去内存读" | 中断在任何指令间隙都可能改这个变量——编译器的静态分析无法感知，必须由**程序员用 volatile 声明**："别自作聪明"。这行代码漏写不会编译报错，只在板子上出诡异现象——**嵌入式第一隐形坑** |
| 判断标准 | 凡是**中断与主循环共享**的变量，或**映射到硬件寄存器**的指针，一律 volatile。本项目就 g_msTicks 一个，好习惯从这里养起 |

### 3. TIM2_IRQHandler：中断服务函数的"轻装上阵"

```c
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);   /* ① 先清标志 */
        g_msTicks++;                                  /* ② 再干活（极轻） */
    }
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 函数名 `TIM2_IRQHandler` 不是随便起的 | 启动文件的中断向量表里写死了这个名字——**链接器按名字把函数填进向量表**。改名=中断永远进不来（→ [[Keil工程配置踩坑]] 里启动文件的角色） |
| ① 清标志必须在**处理之前** | 不清标志：一退出中断立即重进，主循环被饿死。先清后干：干活的几 µs 里若又到期，退出后能再进一次不丢拍 |
| ② 中断里只 `++`——**轻中断原则** | 按键扫描、刷屏这类"重活"全部留给主循环。中断干重活的恶果：主循环时间片全乱、若中断里调用了不可重入函数还会出数据竞争。**中断只生产时间戳，消费全在主循环**——这就是前后台系统的分工契约（→ [[定时器节拍与时间片调度]]） |
| if 判断不是多余的 | 同一外设可能多种中断源共享一个入口，判断+清除"哪一个"是标准姿势 |

### 4. Fault Handler：错误兜底的两种姿势

```c
void HardFault_Handler(void)  { while (1) { } }   /* 死循环 */
void NMI_Handler(void)        { }                 /* 空实现 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `while(1){}`：**故意停机** | HardFault（非法内存访问/未定义指令）说明程序已失控——与其带着烂状态乱跑，不如原地冻结：**挂上调试器还能看现场**（调用栈、寄存器），比"重启假装没事"利于排障 |
| 空实现占位 | 启动文件里这些向量标了 weak（弱定义）——你在这里实现就覆盖默认，不实现也不会链接报错。**weak 符号 = 默认实现可被覆盖**的机制 |

### 5. 前后台系统全景：本页在全局的位置

```
┌─────────── 前台（中断层）───────────┐
│ TIM2_IRQHandler: g_msTicks++       │  ← 每 1ms 抢占主循环一次
└──────────────┬─────────────────────┘
               │ 共享变量 g_msTicks（volatile！）
┌──────────────▼─────────────────────┐
│ main.c while(1) 后台：              │
│  25ms 扫键 → 50ms 刷屏 → 周期推进游戏│  ← 所有"重活"
└────────────────────────────────────┘
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 共享数据的**生产者-消费者**结构：中断生产时间，主循环消费时间 | 没有 RTOS 的裸机项目里，这就是 90% 项目的真实骨架。学它的意义：将来上 RTOS，"任务+延时"不过是把这个骨架换成框架写法，**时间片调度的思想不变** |

## 本页 C 语言知识索引

- [[C语言基本数据类型与表达式]] — volatile 语义
- [[C语言函数与程序结构]] — extern 声明、定义与声明、链接性

## 本页程序原理索引

- [[定时器节拍与时间片调度]] — 前后台架构总览（本页是心跳端，main 篇是消费端）
- [[STM32F103C8T6]] — 时钟树、NVIC、TIM2 外设
- [[Keil工程配置踩坑]] — 启动文件与向量表

## Related

- [[贪吃蛇代码-main主循环与时间片]] — g_msTicks 的消费者
- [[贪吃蛇代码导读]] — 全部代码笔记的地图

## Sources

- [stm32f10x_it.c](../sources/SnakeGame_Keil5源码/stm32f10x_it.c) / [stm32f10x_it.h](../sources/SnakeGame_Keil5源码/stm32f10x_it.h) — 2026-09-18 起
- [system_stm32f10x.c](../sources/SnakeGame_Keil5源码/system_stm32f10x.c) — 库自带，时钟配置原理
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 第 2/5 节 TIM2 节拍，2026-09
