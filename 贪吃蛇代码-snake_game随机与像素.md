# 贪吃蛇代码-snake_game随机与像素

> `App/snake_game.c`（下半）— LCG 伪随机数、空格池放食物、动画相位计时、逻辑→像素映射。游戏引擎的"另一半"。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/snake_game.c`（prv_rand / prv_spawn_food / Animate / MovePeriod / Pixel） |
| 职责 | 随机性来源、食物放置算法、闪烁动画、游戏状态到点阵像素的最终翻译 |
| 配套 | 状态机与 Tick 在 → [[贪吃蛇代码-snake_game状态机]] |

## 完整代码

```c
#define FOOD_BLINK_HALF_MS     200u   /* 食物闪烁半周期：亮 200ms / 灭 200ms */
#define DEAD_BLINK_HALF_MS     250u   /* 死亡闪烁半周期：亮 250ms / 灭 250ms */
#define ANIM_MOD_MS            2000u  /* 动画相位取模（400 与 500 的公倍数） */

/* 16 位线性同余伪随机数：种子来自外部熵源（main.c 用 ADC 浮空噪声生成） */
static uint16_t prv_rand(SnakeGame *g)
{
    g->rngState = (uint16_t)((uint32_t)g->rngState * 20077u + 12345u);
    /* 高低位异或混合，改善低位序列周期过短的问题 */
    return (uint16_t)((g->rngState >> 1) ^ (uint16_t)(g->rngState << 7));
}

/* 在空闲格中放置食物：先统计空格，再按随机索引选取 */
static void prv_spawn_food(SnakeGame *g)
{
    uint8_t freeIdx[SNAKE_MAX_LEN];   /* 空格线性索引表（y*8+x） */
    uint16_t n = 0u;                  /* 空格计数 */
    uint16_t pick;
    uint8_t x, y, k;
    uint8_t onSnake;

    for (y = 0u; y < SNAKE_GRID_H; y++)
    {
        for (x = 0u; x < SNAKE_GRID_W; x++)
        {
            onSnake = 0u;
            for (k = 0u; k < g->length; k++)
            {
                if ((g->body[k].x == x) && (g->body[k].y == y))
                {
                    onSnake = 1u;
                    break;
                }
            }
            if (onSnake == 0u)
            {
                freeIdx[n] = (uint8_t)((y * SNAKE_GRID_W) + x);
                n++;
            }
        }
    }

    if (n == 0u)
    {
        return;   /* 8x8 全满：无空格可放（practically 不可达） */
    }

    pick = (uint16_t)(prv_rand(g) % n);
    g->food.x = (uint8_t)(freeIdx[pick] % SNAKE_GRID_W);
    g->food.y = (uint8_t)(freeIdx[pick] / SNAKE_GRID_W);
}

/* 动画相位计时：主循环周期调用（如每 50ms 传一次增量） */
void SnakeGame_Animate(SnakeGame *g, uint16_t elapsedMs)
{
    g->animMs = (uint16_t)((g->animMs + elapsedMs) % ANIM_MOD_MS);
}

/* 当前移动周期 */
uint16_t SnakeGame_MovePeriod(const SnakeGame *g)
{
    return g->movePeriodMs;
}

/* 逻辑->像素映射：返回 1 该格点亮，0 熄灭
 * RUN/PAUSE：蛇身常亮，食物按相位闪烁；DEAD：整屏反相闪烁 */
uint8_t SnakeGame_Pixel(const SnakeGame *g, uint8_t x, uint8_t y)
{
    uint8_t i;
    uint8_t on = 0u;

    /* 蛇身常亮 */
    for (i = 0u; i < g->length; i++)
    {
        if ((g->body[i].x == x) && (g->body[i].y == y))
        {
            on = 1u;
            break;
        }
    }

    /* 食物：不在蛇身上时按相位闪烁 */
    if (on == 0u)
    {
        if ((g->food.x == x) && (g->food.y == y))
        {
            if ((g->animMs % (FOOD_BLINK_HALF_MS * 2u)) < FOOD_BLINK_HALF_MS)
            {
                on = 1u;
            }
        }
    }

    /* 死亡状态：整屏反相闪烁（冻结画面 + 视觉告警） */
    if (g->state == SNAKE_STATE_DEAD)
    {
        if ((g->animMs % (DEAD_BLINK_HALF_MS * 2u)) < DEAD_BLINK_HALF_MS)
        {
            return (uint8_t)(1u - on);
        }
    }

    return on;
}
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. 16 位 LCG 伪随机数

```c
g->rngState = (uint16_t)((uint32_t)g->rngState * 20077u + 12345u);
return (uint16_t)((g->rngState >> 1) ^ (uint16_t)(g->rngState << 7));
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **LCG 公式**：`state = state × A + C`（A=20077, C=12345）。乘法在 uint16 域会溢出截断，所以先升 uint32 再截回来——**刻意利用溢出**而不是回避它 | 线性同余生成器：每次对旧状态做线性变换，序列看似杂乱但完全由**种子决定**。好处：可复现（同种子同局面）、极省资源（2 次运算）。对比报告第 7 节的 xorshift32，功能等价 |
| `(uint16_t)(x << 7)`：先左移再截断，移出高位丢弃 | LCG 的低位周期短（低位比特序列规律明显），`>>1 ^ <<7` 高低位交叉混合把"好用的高位"拉下来掺进低位——**PC 上 rand() 的祖师爷就是这个套路** |
| 为什么不用 `rand()`（stdlib） | 微库 rand() 也是 LCG 且占更多代码体积；自己写 3 行可控可移植（→ [[Keil工程配置踩坑]] 的 32KB 预算观） |

一句话理解：**伪随机数是"假随机、真规律"**——像走迷宫，起点（种子）相同则路线完全相同；种子来自 ADC 噪声（→ [[ADC噪声随机种子]]）或按键时机（→ [[贪吃蛇代码-main主循环与时间片]]），所以每局不同。

### 2. 空格池放食物：两遍法

```c
/* 第一遍：把所有空格编号收进池子 */
for (y...) for (x...) {
    if (该格不在蛇身上) { freeIdx[n] = y*8 + x; n++; }
}
/* 第二遍：随机抽一个编号 */
pick = prv_rand(g) % n;
food.x = freeIdx[pick] % 8;    /* 编号 % 8 → 列 */
food.y = freeIdx[pick] / 8;    /* 编号 / 8 → 行 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| **二维↔一维坐标互换**：`idx = y*W + x` 编号，`x = idx % W`、`y = idx / W` 还原（→ [[C语言数组与字符串]]） | 二维棋盘在内存里是一维数组——`freeIdx[8]` 存的是"第几格"而不是坐标对，省一半空间。行列互换公式必须吃透 |
| 三层嵌套循环 8×8×length 最坏 64×64=4096 次比较 @72MHz 不到 1ms | "**拒绝采样 vs 先收集再选**"：朴素做法是"随机猜一格，压蛇就重猜"——蛇越长重猜越频繁，残局可能卡死。空格池保证**一次命中且必然合法** |
| `break` 提前退出内层 + `n == 0` 边界返回 | 找到即停省时间；全满（通关画面）不崩溃——**算法边界等于游戏边界** |

### 3. 动画相位：一个计数器管两种闪烁

```c
void SnakeGame_Animate(SnakeGame *g, uint16_t elapsedMs)
{
    g->animMs = (g->animMs + elapsedMs) % ANIM_MOD_MS;   /* 0~1999 循环 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `% 2000` 取模：uint16 累计到 2000 就归零循环 | animMs 是**全局时钟的"动画专用秒针"**。主循环每 50ms 喂一次增量，闪烁判断全靠对它取模 |
| 为什么取模 2000：食物周期 400ms、死亡周期 500ms 的**公倍数** | 两个不同频率的闪烁共用一个相位计数器，取 2000 保证两者相位关系每 2 秒对齐一次，视觉无跳变。选公倍数是**多定时器共享计数**的通用技巧 |

### 4. Pixel：三层优先级的像素翻译官

```c
uint8_t SnakeGame_Pixel(const SnakeGame *g, uint8_t x, uint8_t y)
{
    /* 第 1 层：蛇身常亮 */
    for (i...) if (body[i] == (x,y)) { on = 1; break; }
    /* 第 2 层：食物在非蛇身格上闪烁 */
    if (!on && 是食物格 && (animMs % 400) < 200)  on = 1;
    /* 第 3 层：死亡时整屏反相 */
    if (state == DEAD && (animMs % 500) < 250)    return 1 - on;
    return on;
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `on` 标志位 + break 的查找模式；三层 if **由高到低**覆盖 | 像素归属有优先级：蛇身 > 食物 > 背景。先判蛇身可避免"食物被蛇压住还闪烁"的诡异画面 |
| `(animMs % 400) < 200`：**取模做方波**——前半周期亮、后半周期灭 | 闪烁的本质是"时间 → 0/1"的方波函数。改半周期只动宏；相位由 Animate 统一供给，**显示与计时解耦** |
| `1u - on`：1 变 0、0 变 1 的**反相** | 死亡告警不用另存"死亡画面"——冻结画面 + 逐格取反，两行代码得到整屏闪烁。逻辑层的所有输出最终都收敛到这一个函数，main 层拿它逐格打包成字节（→ [[贪吃蛇代码-main主循环与时间片]] 第 9 段） |

### 5. MovePeriod：一个"只读窗口"函数

```c
uint16_t SnakeGame_MovePeriod(const SnakeGame *g) { return g->movePeriodMs; }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `const SnakeGame *g`：承诺不修改，只读访问（→ [[C语言指针]]） | main 层调度需要知道"现在多快"，但**不许直接翻引擎的口袋**。访问器函数是封装的 C 语言形态——成员变了（比如以后速度公式改了）调用方一行不用动 |

## 本页 C 语言知识索引

- [[C语言基本数据类型与表达式]] — 溢出截断、移位异或、取模方波
- [[C语言数组与字符串]] — 二维↔一维索引互换、空格池数组
- [[C语言流程控制]] — 嵌套循环、break、标志位
- [[C语言指针]] — const 只读指针
- [[C语言动态存储与线性数据结构]] — 对照理解"确定性内存"的好处

## 本页程序原理索引

- [[贪吃蛇游戏逻辑]] — 五条关键算法总览
- [[ADC噪声随机种子]] — rngState 的种子从哪来
- [[MAX7219]] — Pixel 的返回值最终变成哪一行的哪个位
- [[贪吃蛇代码-main主循环与时间片]] — Animate/Pixel 的调用节拍

## Related

- [[贪吃蛇代码-snake_game状态机]] — 同文件上半：状态机与 Tick
- [[贪吃蛇代码导读]] — 全部代码笔记的地图

## Sources

- [snake_game.c](../sources/SnakeGame_Keil5源码/snake_game.c) — 2026-09-18 起
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 第 4 节 SnakeGame_Pixel 表、第 7 节随机数，2026-09
