/**
  ******************************************************************************
  * @file    snake_game.c
  * @author  Xu Xiansen / K
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

/* 两个方向是否相反（(a+2)&3 即 a 的反方向） */
static uint8_t prv_is_opposite(SnakeDir a, SnakeDir b)
{
    return (uint8_t)((((uint8_t)a + 2u) & 3u) == (uint8_t)b);
}

/* 在空闲格中放置食物：先统计空格，再按随机索引选取（报告 snake_game 表） */
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
        return;   /* 8x8 全满：无空格可放（ practically 不可达） */
    }

    pick = (uint16_t)(prv_rand(g) % n);
    g->food.x = (uint8_t)(freeIdx[pick] % SNAKE_GRID_W);
    g->food.y = (uint8_t)(freeIdx[pick] / SNAKE_GRID_W);
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

    prv_spawn_food(g);
}

/* 从 READY/DEAD 进入 RUN */
void SnakeGame_Start(SnakeGame *g)
{
    if ((g->state == SNAKE_STATE_READY) || (g->state == SNAKE_STATE_DEAD))
    {
        g->state = SNAKE_STATE_RUN;
    }
}

/* 暂停键：RUN <-> PAUSE。暂停时画面与分数保持（报告测试清单第 4 条） */
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

/* 方向键入队：与当前方向相反则忽略（禁止 180 度掉头，报告需求映射表） */
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

/* 逻辑->像素映射（报告 SnakeGame_Pixel 表：食物周期闪烁，死亡时闪烁）
 * 返回 1：该格点亮；返回 0：熄灭 */
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
