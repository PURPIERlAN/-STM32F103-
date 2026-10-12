/**
  ******************************************************************************
  * @file    snake_game.h
  * @author  Xu Xiansen / K
  * @brief   8x8 LED 点阵贪吃蛇——纯逻辑游戏引擎（不依赖 HAL）
  *
  * 设计报告对应关系（LED点阵贪吃蛇游戏系统设计报告.docx 第 4 节）：
  *   SnakeGame_Init           建立长度为 3 的初始蛇，随机种子由外部传入
  *                            （main.c 用 ADC 浮空通道噪声生成）
  *   SnakeGame_QueueDirection 记录下一次允许生效的方向，与当前方向相反则忽略
  *   SnakeGame_Tick           推进一次游戏状态：边界、自碰撞、进食、成长
  *   spawn_food               先统计空格，再按随机索引放置食物
  *   SnakeGame_Pixel          把逻辑转换为点阵像素：食物周期闪烁，死亡全屏闪烁
  *   SnakeGame_Animate        动画相位计时（食物/死亡闪烁），由主循环周期调用
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
