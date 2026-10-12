# 贪吃蛇代码-snake_game状态机

> `App/snake_game.h` + `App/snake_game.c`（上半）— 纯逻辑游戏引擎：状态机、方向队列、每步推进 Tick。**零硬件依赖**，整个项目最值得精读的文件。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/snake_game.h`（接口）/ `.c`（实现，约 230 行） |
| 职责 | 游戏规则全部逻辑：状态机、移动、碰撞、进食、加速、放食物 |
| 依赖 | **只有 `<stdint.h>`**——不 include 任何 STM32 头文件，可整体搬到 PC/其他芯片 |
| 本页覆盖 | 头文件全文 + .c 的 Init / Start / TogglePause / QueueDirection / Tick |
| 配套 | 随机数、放食物、动画闪烁在 → [[贪吃蛇代码-snake_game随机与像素]] |

## 在工程分层中的位置

```
main.c（调度：什么时候调 Tick）
   │  调用
   ▼
snake_game.c（本页：游戏规则怎么算）←—— 只做算术，不碰硬件
   │  SnakeGame_Pixel()
   ▼
main.c App_DrawMatrix → max7219.c（把结果画出来）
```

这个"逻辑与硬件隔离"的分层意味着：把 snake_game.c 单独拷到电脑上就能跑单元测试；移植 HAL/CubeMX 时一行不用改。**头文件就是契约**——.h 声明"我能做什么"，.c 实现"我怎么做"，main.c 只看 .h。

## 完整代码：snake_game.h

```c
/**
  ******************************************************************************
  * @file    snake_game.h
  * @author  Xu Xiansen / K
  * @brief   8x8 LED 点阵贪吃蛇——纯逻辑游戏引擎（不依赖 HAL）
  *
  * 时序参数（报告第 5 节）：
  *   蛇初始移动周期 350 ms，每得 1 分减少 12 ms，下限 110 ms
  * ******************************************************************************
  */
#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <stdint.h>

/* 点阵网格：8x8，坐标 (0,0) 为左上角 */
#define SNAKE_GRID_W            8u
#define SNAKE_GRID_H            8u
#define SNAKE_MAX_LEN           (SNAKE_GRID_W * SNAKE_GRID_H)   /* 64 */

/* 速度参数（报告第 5 节） */
#define SNAKE_START_PERIOD_MS   350u    /* 初始移动周期 */
#define SNAKE_PERIOD_STEP_MS    12u     /* 每得分减少量   */
#define SNAKE_MIN_PERIOD_MS     110u    /* 周期下限       */

/* 方向：按 (dir + 1) & 3 即顺时针下一方向 */
typedef enum
{
    SNAKE_DIR_UP    = 0u,   /* y-1 */
    SNAKE_DIR_RIGHT = 1u,   /* x+1 */
    SNAKE_DIR_DOWN  = 2u,   /* y+1 */
    SNAKE_DIR_LEFT  = 3u,   /* x-1 */
    SNAKE_DIR_NONE  = 4u    /* 无按键时的占位 */
} SnakeDir;

/* 游戏状态机 */
typedef enum
{
    SNAKE_STATE_READY = 0u, /* 上电待机，按方向键开局 */
    SNAKE_STATE_RUN,        /* 游戏进行 */
    SNAKE_STATE_PAUSE,      /* 暂停（画面与分数保持） */
    SNAKE_STATE_DEAD        /* 撞墙/撞身死亡，画面闪烁 */
} SnakeState;

typedef struct
{
    uint8_t x;
    uint8_t y;
} SnakePoint;

typedef struct
{
    SnakePoint body[SNAKE_MAX_LEN]; /* body[0] 为蛇头 */
    uint8_t    length;              /* 当前蛇长 */
    SnakeDir   dir;                 /* 当前移动方向 */
    SnakeDir   queuedDir;           /* 按键队列方向（每步只生效一次） */
    SnakePoint food;                /* 食物坐标 */
    uint16_t   score;               /* 得分（= 食物数） */
    SnakeState state;               /* 状态机 */
    uint16_t   movePeriodMs;        /* 当前移动周期 350→110 ms */
    uint16_t   animMs;              /* 动画相位累计毫秒 */
    uint16_t   rngState;            /* LCG 随机状态 */
} SnakeGame;

/* 复位游戏并建立长度为 3 的初始蛇；seed 来自 ADC 浮空噪声等熵源 */
void     SnakeGame_Init(SnakeGame *g, uint16_t seed);

/* 从 READY/DEAD 状态进入 RUN */
void     SnakeGame_Start(SnakeGame *g);

/* PAUSE 键：RUN <-> PAUSE 切换 */
void     SnakeGame_TogglePause(SnakeGame *g);

/* 方向键：记录到 queuedDir；与当前方向相反则忽略（禁止 180 度掉头） */
void     SnakeGame_QueueDirection(SnakeGame *g, SnakeDir d);

/* 推进一步游戏状态：移动、越界/自碰撞判断、进食成长、速度更新 */
void     SnakeGame_Tick(SnakeGame *g);

/* 动画计时：主循环周期调用，驱动食物/死亡闪烁相位 */
void     SnakeGame_Animate(SnakeGame *g, uint16_t elapsedMs);

/* 当前移动周期 */
uint16_t SnakeGame_MovePeriod(const SnakeGame *g);

/* 逻辑->像素映射：返回 1 表示该格点亮。
 * RUN/PAUSE：蛇身常亮，食物按相位闪烁；DEAD：整屏按相位反相闪烁 */
uint8_t  SnakeGame_Pixel(const SnakeGame *g, uint8_t x, uint8_t y);

#endif /* SNAKE_GAME_H */
```

## 完整代码：snake_game.c（状态机部分）

```c
/**
  ******************************************************************************
  * @file    snake_game.c
  * @brief   8x8 点阵贪吃蛇——纯逻辑实现（不依赖任何外设库，可移植）
  *
  * 核心算法（设计报告第 4 节）：
  *   1. 蛇身用最大 64 的坐标数组保存，body[0] 为蛇头；
  *   2. 每次移动先根据方向得到 next，再完成越界和自碰撞判断；
  *   3. 若没有吃食物，自碰撞检测时忽略旧尾巴（尾巴会在本步移走）；
  *   4. 确认安全后从尾到头搬移坐标，将 next 写入 body[0]；
  *   5. 吃到食物则长度 +1、得分 +1、移动周期 -12ms（下限 110ms）；
  *   6. 放食物时先统计空格，再按随机索引放置，保证食物绝不落在蛇身上；
  *   7. 入队方向与当前方向相反则忽略，实现禁止 180 度掉头。
  * ******************************************************************************
  */
#include "snake_game.h"

/* 两个方向是否相反（(a+2)&3 即 a 的反方向） */
static uint8_t prv_is_opposite(SnakeDir a, SnakeDir b)
{
    return (uint8_t)((((uint8_t)a + 2u) & 3u) == (uint8_t)b);
}

/* 复位棋局：长度为 3 的初始蛇，位于第 4 行、蛇头朝右，等待开局 */
void SnakeGame_Init(SnakeGame *g, uint16_t seed)
{
    uint8_t i;

    g->length       = 3u;
    g->body[0].x    = 3u;  g->body[0].y = 4u;   /* 蛇头 */
    g->body[1].x    = 2u;  g->body[1].y = 4u;
    g->body[2].x    = 1u;  g->body[2].y = 4u;   /* 蛇尾 */
    g->dir          = SNAKE_DIR_RIGHT;
    g->queuedDir    = SNAKE_DIR_NONE;
    g->score        = 0u;
    g->state        = SNAKE_STATE_READY;
    g->movePeriodMs = SNAKE_START_PERIOD_MS;
    g->animMs       = 0u;
    g->rngState     = (uint16_t)(seed ^ 0xA5A5u);
    if (g->rngState == 0u) { g->rngState = 0x5A5Au; }

    for (i = 3u; i < SNAKE_MAX_LEN; i++)
    {
        g->body[i].x = 0xFFu;
        g->body[i].y = 0xFFu;
    }

    prv_spawn_food(g);   /* 函数体见 [[贪吃蛇代码-snake_game随机与像素]] */
}

/* 从 READY/DEAD 进入 RUN */
void SnakeGame_Start(SnakeGame *g)
{
    if ((g->state == SNAKE_STATE_READY) || (g->state == SNAKE_STATE_DEAD))
    {
        g->state = SNAKE_STATE_RUN;
    }
}

/* 暂停键：RUN <-> PAUSE。暂停时画面与分数保持 */
void SnakeGame_TogglePause(SnakeGame *g)
{
    if (g->state == SNAKE_STATE_RUN)
    {
        g->state = SNAKE_STATE_PAUSE;
    }
    else if (g->state == SNAKE_STATE_PAUSE)
    {
        g->state = SNAKE_STATE_RUN;
    }
    else
    {
        /* READY/DEAD 状态下暂停键无效 */
    }
}

/* 方向键入队：与当前方向相反则忽略（禁止 180 度掉头） */
void SnakeGame_QueueDirection(SnakeGame *g, SnakeDir d)
{
    if (d >= SNAKE_DIR_NONE)
    {
        return;   /* 非法方向 */
    }
    if (prv_is_opposite(d, g->dir) != 0u)
    {
        return;   /* 反向：忽略 */
    }
    if (d == g->dir)
    {
        return;   /* 同向重复按键：忽略 */
    }
    g->queuedDir = d;
}

/* 推进一步游戏状态（由主循环按当前 movePeriodMs 周期调用） */
void SnakeGame_Tick(SnakeGame *g)
{
    SnakePoint next;
    uint8_t eating;
    uint8_t checkLen;
    uint8_t i;
    uint16_t step;

    if (g->state != SNAKE_STATE_RUN)
    {
        return;
    }

    /* 应用队列方向：每步最多转向一次，杜绝一步内两次按键导致的掉头穿身 */
    if (g->queuedDir != SNAKE_DIR_NONE)
    {
        if (prv_is_opposite(g->queuedDir, g->dir) == 0u)
        {
            g->dir = g->queuedDir;
        }
        g->queuedDir = SNAKE_DIR_NONE;
    }

    next = g->body[0];
    switch (g->dir)
    {
    case SNAKE_DIR_UP:    next.y--; break;
    case SNAKE_DIR_DOWN:  next.y++; break;
    case SNAKE_DIR_LEFT:  next.x--; break;
    case SNAKE_DIR_RIGHT: next.x++; break;
    default: break;
    }

    /* 撞墙判断（uint8_t 下溢/溢出后自然 >= 边界） */
    if ((next.x >= SNAKE_GRID_W) || (next.y >= SNAKE_GRID_H))
    {
        g->state = SNAKE_STATE_DEAD;
        return;
    }

    eating = (uint8_t)((next.x == g->food.x) && (next.y == g->food.y));

    /* 自碰撞判断：不吃食物时旧尾巴会在本步让位，检测时忽略旧尾巴 */
    checkLen = (uint8_t)(eating != 0u ? g->length : (g->length - 1u));
    for (i = 0u; i < checkLen; i++)
    {
        if ((g->body[i].x == next.x) && (g->body[i].y == next.y))
        {
            g->state = SNAKE_STATE_DEAD;
            return;
        }
    }

    if (eating != 0u)
    {
        if (g->length >= SNAKE_MAX_LEN)
        {
            return;   /* 满盘：冻结在通关画面 */
        }
        g->length++;
        g->score++;

        /* 速度递增：每得 1 分减少 12 ms，下限 110 ms（报告第 5 节） */
        step = (uint16_t)((uint16_t)g->score * SNAKE_PERIOD_STEP_MS);
        if (step >= (SNAKE_START_PERIOD_MS - SNAKE_MIN_PERIOD_MS))
        {
            g->movePeriodMs = SNAKE_MIN_PERIOD_MS;
        }
        else
        {
            g->movePeriodMs = (uint16_t)(SNAKE_START_PERIOD_MS - step);
        }

        prv_spawn_food(g);
    }

    /* 从尾到头搬移坐标，蛇头写入 next */
    for (i = (uint8_t)(g->length - 1u); i > 0u; i--)
    {
        g->body[i] = g->body[i - 1u];
    }
    g->body[0] = next;
}
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. 头文件保护与宏参数

```c
#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H
...
#endif
#define SNAKE_MAX_LEN  (SNAKE_GRID_W * SNAKE_GRID_H)   /* 64 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| include guard 三件套 `#ifndef/#define/#endif`（→ [[C语言编译预处理]]） | 防止同一翻译单元被重复包含导致"重定义"编译错误——多文件工程的标配 |
| **宏整体加括号**：`(SNAKE_GRID_W * SNAKE_GRID_H)` | 宏是文本替换。若写成 `#define LEN 8+8`，`LEN*2` 会展开成 `8+8*2`=24！括号是宏的生命线 |
| 魔法数字全部提成宏（350/12/110） | 报告验收要改参数时只动一处；宏在编译期展开，**不占 RAM**（比 const 变量更适合小内存 MCU 的场合之一） |

### 2. 枚举建模：方向与状态

```c
typedef enum { SNAKE_DIR_UP = 0u, SNAKE_DIR_RIGHT = 1u, SNAKE_DIR_DOWN = 2u,
               SNAKE_DIR_LEFT = 3u, SNAKE_DIR_NONE = 4u } SnakeDir;
typedef enum { SNAKE_STATE_READY = 0u, SNAKE_STATE_RUN,
               SNAKE_STATE_PAUSE, SNAKE_STATE_DEAD } SnakeState;
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `typedef enum`：具名常量集合，底层就是 int（→ [[C语言基本数据类型与表达式]]） | 用 `SNAKE_DIR_UP` 而不是裸数字 0——**代码自解释**，改错编译器能抓到（比如比较方向和状态会被类型/逻辑暴露） |
| 方向值刻意排成**顺时针** UP=0→RIGHT=1→DOWN=2→LEFT=3 | 这不是随便排的：`(dir+1)&3` 自动得到顺时针方向、`(dir+2)&3` 自动得到反方向——**用编码设计消灭 if-else**，见第 4 段 |
| 后三个状态不写值，自动递增（1,2,3） | 枚举默认值规则，省代码且不会错 |

### 3. 结构体：把整个游戏装进一个盒子

```c
typedef struct { uint8_t x; uint8_t y; } SnakePoint;

typedef struct {
    SnakePoint body[64];    /* 蛇身数组，body[0] 是蛇头 */
    uint8_t    length;
    SnakeDir   dir, queuedDir;
    SnakePoint food;
    uint16_t   score, movePeriodMs, animMs, rngState;
    SnakeState state;
} SnakeGame;
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 结构体嵌结构体数组：`SnakePoint body[SNAKE_MAX_LEN]`（→ [[C语言结构体与联合体]]） | "**用数组当蛇身**"：不用链表！数组静态分配，没有 malloc，嵌入式里杜绝内存碎片/泄漏风险（对比：→ [[C语言动态存储与线性数据结构]] 讲的链表方案及其代价） |
| uint8_t 坐标 + uint16_t 计数：按数值范围选最小够用的类型 | `SnakeGame` 整个实例才 ~140 字节（64×2 坐标 + 若干小字段），单个 RAM 块（20KB）绰绰有余 |
| `queuedDir` 单独一个字段 | "按键了"和"脚步到了"是两个时刻——**中间需要缓冲**。这就是队列思想的最小化实现：容量只有 1 的队列 |

### 4. 反方向判断：一行位运算

```c
static uint8_t prv_is_opposite(SnakeDir a, SnakeDir b)
{
    return (uint8_t)((((uint8_t)a + 2u) & 3u) == (uint8_t)b);
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `(a + 2) & 3`：加 2 再对 4 取余（&3 等价于 %4，但更快）——**模运算环形计数**（→ [[C语言基本数据类型与表达式]]） | 方向编码顺时针排列的直接红利：UP(0)+2=DOWN(2)，RIGHT(1)+2=LEFT(3)。不写四个 if，一行搞定 |
| `static` 函数：文件私有（→ [[C语言函数与程序结构]]）；`prv_` 前缀 = private 惯例 | 帮助函数不进头文件——**接口最小化**，外界只见 8 个公开函数 |
| 返回 uint8_t 当布尔用（`!= 0u` 判断） | C89 风格显式比较，杜绝把指针/枚举当布尔的隐式转换隐患 |

### 5. Init：复位棋局的"填表"

```c
g->length = 3u;
g->body[0].x = 3u;  g->body[0].y = 4u;   /* 蛇头在 (3,4) 朝右 */
g->body[1].x = 2u;  ...
for (i = 3u; i < SNAKE_MAX_LEN; i++) { g->body[i].x = 0xFFu; g->body[i].y = 0xFFu; }
g->rngState = (uint16_t)(seed ^ 0xA5A5u);
if (g->rngState == 0u) { g->rngState = 0x5A5Au; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 逐字段赋值初始化（结构体成员用 `.` 访问，指针时用 `->`） | 上电画面固定：蛇头 (3,4)、长 3、朝右、350ms 周期、READY 态——**确定性开局**便于排查问题（"固定时长死亡"现象的根源就是它） |
| 无效位填 `0xFF`（而不是 0！） | 0 是合法坐标 (0,0)，填 0 会让"未用蛇身格"看起来像真蛇；0xFF 越界一眼可辨——**哨兵值选择有讲究** |
| 异或 0xA5A5 打散种子；种子 0 单独排除 | LCG 状态为 0 时序列退化（0×k+c 循环极短）——**边界输入防御** |

### 6. QueueDirection：入队前的三道安检

```c
if (d >= SNAKE_DIR_NONE)     return;   /* ① 非法值 */
if (prv_is_opposite(d, g->dir)) return;   /* ② 反向 */
if (d == g->dir)             return;   /* ③ 同向重复 */
g->queuedDir = d;
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **防御式编程**：先挡非法输入再干活，early return 提前退出 | 禁 180° 掉头是报告的硬性需求。注意这里只是"入队检查"，Tick 里应用时**还要再查一次**（双保险，防中途转向造成掉头穿身） |
| 三连 `if + return` 替代嵌套 if | 早退模式让主逻辑一路平铺，可读性远好于三层嵌套 |

### 7. Tick 第一步：状态守卫 + 队列消费

```c
if (g->state != SNAKE_STATE_RUN) return;          /* 守卫：只有 RUN 才推进 */
if (g->queuedDir != SNAKE_DIR_NONE) {
    if (prv_is_opposite(g->queuedDir, g->dir) == 0u)  g->dir = g->queuedDir;
    g->queuedDir = SNAKE_DIR_NONE;                 /* 用完即清，只生效一次 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 函数开头的**状态守卫**（guard clause） | PAUSE/DEAD 时即使误调 Tick 也纹丝不动——状态机把"什么状态下允许什么行为"编码进代码（→ [[贪吃蛇游戏逻辑]]） |
| 消费后立刻置 `SNAKE_DIR_NONE` | "**每步只转向一次**"：250ms 一步的周期里快速按两个键，若两个都生效就会一步掉头穿身。消费即清零是容量-1 队列的标准语义 |

### 8. Tick 第二步：算 next、撞墙判断的溢出妙用

```c
next = g->body[0];
switch (g->dir) {
case SNAKE_DIR_UP:    next.y--; break;
...
case SNAKE_DIR_RIGHT: next.x++; break;
}
if ((next.x >= SNAKE_GRID_W) || (next.y >= SNAKE_GRID_H)) { g->state = SNAKE_STATE_DEAD; return; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **无符号下溢**：x=0 时 `x--` 得 255；无符号上溢：x=7 时 `x++` 得 0 | 于是**只用一个 `>=` 判断就同时抓住"出左/上边界"（下溢成大数）和"出右/下边界"**——这是注释里"下溢/溢出后自然 >= 边界"的含义。有符号数反而要写四个比较 |
| switch 按方向平移坐标 | "先算虚拟下一步，验证后再落地"——**先判后动**是所有碰撞类游戏/物理引擎的通用顺序 |

### 9. Tick 第三步：踩尾巴合法的不对称碰撞

```c
eating = (next.x == g->food.x) && (next.y == g->food.y);
checkLen = eating ? g->length : (g->length - 1u);   /* 不吃食：忽略旧尾巴 */
for (i = 0u; i < checkLen; i++)
    if (body[i].x == next.x && body[i].y == next.y) { DEAD; return; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 三目运算符 `? :` 选检测长度（→ [[C语言流程控制]]） | **本项目最精妙的一处**：不吃食物时旧尾巴这一步会往前挪让位，所以头撞上旧尾巴位置其实没事——检测范围减 1；吃食物时尾巴不动，全长度检测。少了这个 `-1`，紧贴尾巴直行会误判死亡 |
| 循环线性查找 | 64 格规模线性扫完全够快（最坏 64 次比较 × 8×8 棋盘），无需哈希——**数据规模决定算法选择** |

### 10. Tick 第四步：吃食、加速、搬移蛇身

```c
if (eating) {
    g->length++;  g->score++;
    step = score * 12u;
    movePeriodMs = (step >= 350-110) ? 110 : (350 - step);   /* 钳位 */
    prv_spawn_food(g);
}
for (i = length - 1u; i > 0u; i--)  g->body[i] = g->body[i-1u];   /* 从尾到头搬移 */
g->body[0] = next;
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **钳位公式**：减到下限就钉住。等价写法 `max(110, 350-12*score)` | 速度曲线 350→338→…→122→110→110…，上限 20 分封顶。用减法实现"越来越快"，避开除法/浮点 |
| 搬移**必须从尾到头**：`body[i] = body[i-1]`（i 从大到小） | 若从头到尾搬，`body[1]=body[0]` 先执行，body[0] 的旧值（蛇头坐标）还没写进 body[1] 就被覆盖——**全体变蛇头**的经典 bug。这是数组原地搬移的铁律：**朝源头方向搬**（→ [[C语言数组与字符串]]） |
| 不吃食时不 `length++`：尾巴格被搬移自然"甩掉" | 数组模拟蛇的滑动：不增长时每步只有"平移"，增长时数组多占一格。全程零动态内存 |

## 本页 C 语言知识索引

- [[C语言编译预处理]] — include guard、带括号的宏
- [[C语言基本数据类型与表达式]] — 枚举、无符号溢出/下溢、位运算取模
- [[C语言结构体与联合体]] — 结构体嵌套、`.` 与 `->`
- [[C语言流程控制]] — early return、switch、三目运算
- [[C语言函数与程序结构]] — static 私有函数、接口(.h)与实现(.c)
- [[C语言数组与字符串]] — 数组原地搬移方向
- [[C语言动态存储与线性数据结构]] — 对照：为什么用数组不用链表

## 本页程序原理索引

- [[贪吃蛇游戏逻辑]] — 算法层总览（本页是它的代码落地）
- [[LED点阵贪吃蛇项目]] — 350/12/110 参数出处
- [[贪吃蛇代码-main主循环与时间片]] — 谁在什么周期调 Tick
- [[贪吃蛇代码-snake_game随机与像素]] — prv_spawn_food / Pixel 实现

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图

## Sources

- [snake_game.h](../sources/SnakeGame_Keil5源码/snake_game.h) / [snake_game.c](../sources/SnakeGame_Keil5源码/snake_game.c) — 2026-09-18 起
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 第 4 节核心算法，2026-09
