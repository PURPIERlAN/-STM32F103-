# 贪吃蛇代码-oled显存与字库

> `App/oled.h` + `App/oled.c`（绘制层）+ `App/oledfont.h` — 帧缓冲思想：先在 RAM 里画好一整屏，再一次性推给 OLED。本页管"画"，I2C 怎么送见 [[贪吃蛇代码-oled的I2C底层]]。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/oled.h` / `oled.c`（约 290 行，本页覆盖绘制层）/ `oledfont.h` |
| 职责 | 本地显存管理、像素读写、6×8 字符渲染、12×16 大字放大、整屏刷新调度 |
| 硬件 | SSD1306 128×64，I2C1@0x3C，PB6/PB7 → 原理页 [[SSD1306]]、总线 [[I2C协议]] |

## 在工程分层中的位置

```
main.c App_DrawOled() —— "SCORE:12 显示在左上角"
        │ 调用
        ▼
oled.c 绘制层（本页）—— 在 s_gram[8][128] 显存里画像素/字形
        │ OLED_Update() 整屏推送
        ▼
oled.c I2C 层（→ 贪吃蛇代码-oled的I2C底层）—— 显存字节变 I2C 波形
        ▼
SSD1306 芯片 —— 自己点亮对应的 OLED 像素
```

## 完整代码：oled.h

```c
#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#define OLED_I2C_ADDR        (0x3Cu << 1)   /* 7 位地址 0x3C，I2C 发送用 8 位形式 */

#define OLED_WIDTH           128u
#define OLED_HEIGHT          64u
#define OLED_PAGES           8u

/* 初始化 I2C1 与 SSD1306（标准初始化命令序列） */
void OLED_Init(void);

/* 清空本地显存（不清屏幕，需 OLED_Update） */
void OLED_Clear(void);

/* 将本地显存整屏刷到 OLED（128x64 分 8 页传输） */
void OLED_Update(void);

/* 设置一个像素 */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);

/* 6x8 字符/字符串：x 为像素列(0~127)，page 为页(0~7) */
void OLED_ShowChar6x8(uint8_t x, uint8_t page, char ch);
void OLED_ShowString6x8(uint8_t x, uint8_t page, const char *str);

/* 2 倍放大字符串（6x8 字形放大为 12x16，占据相邻两页），用于大号分数显示 */
void OLED_ShowBigString(uint8_t x, uint8_t page, const char *str);

#endif /* __OLED_H */
```

## 完整代码：oled.c（绘制层）

```c
#include "oled.h"
#include "oledfont.h"
#include "stm32f10x.h"

/* 本地显存：8 页 x 128 字节（每字节 8 个纵向像素，低位在上） */
static uint8_t s_gram[OLED_PAGES][OLED_WIDTH];

/* ---------------- 显存绘制 ---------------- */

void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    uint8_t page, bit;

    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
    {
        return;
    }
    page = (uint8_t)(y >> 3u);
    bit  = (uint8_t)(y & 0x07u);

    if (on != 0u)
    {
        s_gram[page][x] |= (uint8_t)(1u << bit);
    }
    else
    {
        s_gram[page][x] &= (uint8_t)(~(1u << bit));
    }
}

void OLED_Clear(void)
{
    uint8_t i, j;
    for (i = 0u; i < OLED_PAGES; i++)
    {
        for (j = 0u; j < OLED_WIDTH; j++)
        {
            s_gram[i][j] = 0x00u;
        }
    }
}

void OLED_ShowChar6x8(uint8_t x, uint8_t page, char ch)
{
    uint8_t i;
    const uint8_t *glyph;

    if ((ch < (char)FONT6X8_FIRST_CHAR) || (ch > (char)FONT6X8_LAST_CHAR))
    {
        ch = ' ';
    }
    glyph = &F6x8[ch - FONT6X8_FIRST_CHAR][0];

    for (i = 0u; i < FONT6X8_WIDTH; i++)
    {
        if ((uint16_t)x + i < OLED_WIDTH)
        {
            s_gram[page][x + i] = glyph[i];
        }
    }
}

void OLED_ShowString6x8(uint8_t x, uint8_t page, const char *str)
{
    while ((*str) != '\0')
    {
        OLED_ShowChar6x8(x, page, *str);
        str++;
        x = (uint8_t)(x + FONT6X8_WIDTH);
        if (x >= OLED_WIDTH) { break; }
    }
}

/* 6x8 字形 2 倍放大（12x16），page 为上页，下一页自动取 page+1 */
void OLED_ShowBigString(uint8_t x, uint8_t page, const char *str)
{
    while ((*str) != '\0')
    {
        char ch = *str;
        uint8_t col, bit;
        const uint8_t *glyph;

        if ((ch < (char)FONT6X8_FIRST_CHAR) || (ch > (char)FONT6X8_LAST_CHAR))
        {
            ch = ' ';
        }
        glyph = &F6x8[ch - FONT6X8_FIRST_CHAR][0];

        for (col = 0u; col < FONT6X8_WIDTH; col++)
        {
            uint8_t bits = glyph[col];
            for (bit = 0u; bit < 8u; bit++)
            {
                if ((bits & (uint8_t)(1u << bit)) != 0u)
                {
                    /* 源像素 (col, bit) -> 2x2 放大 */
                    uint8_t bx = (uint8_t)(x + col * 2u);
                    uint8_t by = (uint8_t)(page * 8u + bit);
                    OLED_SetPixel(bx, by, 1u);
                    OLED_SetPixel((uint8_t)(bx + 1u), by, 1u);
                    OLED_SetPixel(bx, (uint8_t)(by + 1u), 1u);
                    OLED_SetPixel((uint8_t)(bx + 1u), (uint8_t)(by + 1u), 1u);
                }
            }
        }
        str++;
        x = (uint8_t)(x + FONT6X8_WIDTH * 2u);
        if (x >= OLED_WIDTH) { break; }
    }
}

/* ---------------- 对外接口 ---------------- */

void OLED_Update(void)
{
    static uint8_t flushBuf[OLED_WIDTH];   /* 复用一行缓冲，逐页传输 */

    static const uint8_t colRange[6] = {0x21u, 0x00u, 0x7Fu, 0x22u, 0x00u, 0x07u};
    uint8_t page;

    /* 水平寻址模式：列 0~127，页 0~7，之后连写 8 页 */
    (void)prv_I2C_Write(0x00u, &colRange[0], 6u);

    for (page = 0u; page < OLED_PAGES; page++)
    {
        uint8_t i;
        for (i = 0u; i < OLED_WIDTH; i++)
        {
            flushBuf[i] = s_gram[page][i];
        }
        if (prv_I2C_Write(0x40u, flushBuf, OLED_WIDTH) != 0u)
        {
            return;   /* I2C 异常已恢复，等下次刷新 */
        }
    }
}
```

## 完整代码：oledfont.h（6×8 ASCII 字库，95 字符）

```c
#ifndef __OLEDFONT_H
#define __OLEDFONT_H

#include <stdint.h>

#define FONT6X8_FIRST_CHAR   0x20u
#define FONT6X8_LAST_CHAR    0x7Eu
#define FONT6X8_WIDTH        6u

static const uint8_t F6x8[95][6] =
{
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 ' ' */
    {0x00,0x00,0x00,0x2F,0x00,0x00}, /* 0x21 '!' */
    {0x00,0x00,0x07,0x00,0x07,0x00}, /* 0x22 '"' */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* 0x23 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* 0x24 '$' */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* 0x25 '%' */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* 0x26 '&' */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* 0x27 '\'' */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* 0x28 '(' */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* 0x29 ')' */
    {0x14,0x08,0x3E,0x08,0x14,0x00}, /* 0x2A '*' */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* 0x2B '+' */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* 0x2C ',' */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* 0x2D '-' */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* 0x2E '.' */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* 0x2F '/' */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* 0x30 '0' */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* 0x31 '1' */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* 0x32 '2' */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* 0x33 '3' */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* 0x34 '4' */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* 0x35 '5' */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* 0x36 '6' */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* 0x37 '7' */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* 0x38 '8' */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* 0x39 '9' */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* 0x3A ':' */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* 0x3B ';' */
    {0x08,0x14,0x22,0x41,0x00,0x00}, /* 0x3C '<' */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* 0x3D '=' */
    {0x00,0x41,0x22,0x14,0x08,0x00}, /* 0x3E '>' */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* 0x3F '?' */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* 0x40 '@' */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* 0x41 'A' */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* 0x42 'B' */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* 0x43 'C' */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* 0x44 'D' */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* 0x45 'E' */
    {0x7F,0x09,0x09,0x09,0x01,0x00}, /* 0x46 'F' */
    {0x3E,0x41,0x49,0x49,0x7A,0x00}, /* 0x47 'G' */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* 0x48 'H' */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* 0x49 'I' */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* 0x4A 'J' */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* 0x4B 'K' */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* 0x4C 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F,0x00}, /* 0x4D 'M' */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* 0x4E 'N' */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* 0x4F 'O' */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* 0x50 'P' */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* 0x51 'Q' */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* 0x52 'R' */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* 0x53 'S' */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* 0x54 'T' */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* 0x55 'U' */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* 0x56 'V' */
    {0x3F,0x40,0x38,0x40,0x3F,0x00}, /* 0x57 'W' */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* 0x58 'X' */
    {0x07,0x08,0x70,0x08,0x07,0x00}, /* 0x59 'Y' */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* 0x5A 'Z' */
    {0x00,0x7F,0x41,0x41,0x00,0x00}, /* 0x5B '[' */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* 0x5C '\\' */
    {0x00,0x41,0x41,0x7F,0x00,0x00}, /* 0x5D ']' */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* 0x5E '^' */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* 0x5F '_' */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* 0x60 '`' */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* 0x61 'a' */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* 0x62 'b' */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* 0x63 'c' */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* 0x64 'd' */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* 0x65 'e' */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* 0x66 'f' */
    {0x0C,0x52,0x52,0x52,0x3E,0x00}, /* 0x67 'g' */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* 0x68 'h' */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* 0x69 'i' */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* 0x6A 'j' */
    {0x7F,0x10,0x28,0x44,0x00,0x00}, /* 0x6B 'k' */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* 0x6C 'l' */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* 0x6D 'm' */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* 0x6E 'n' */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* 0x6F 'o' */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* 0x70 'p' */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* 0x71 'q' */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* 0x72 'r' */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* 0x73 's' */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* 0x74 't' */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* 0x75 'u' */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* 0x76 'v' */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* 0x77 'w' */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* 0x78 'x' */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* 0x79 'y' */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* 0x7A 'z' */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* 0x7B '{' */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* 0x7C '|' */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* 0x7D '}' */
    {0x08,0x08,0x2A,0x1C,0x08,0x00}  /* 0x7E '~' */
};

#endif /* __OLEDFONT_H */
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. 本地显存：`s_gram[8][128]` 是一块"草稿纸"

```c
static uint8_t s_gram[OLED_PAGES][OLED_WIDTH];   /* 8 页 × 128 列 = 1024 字节 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 二维数组（→ [[C语言数组与字符串]]），static 文件私有 | **帧缓冲（framebuffer）思想**：画图先在内存草稿纸上改，画完一次性誊给屏幕。没有显存的话，画一个字就要发起一次 I2C 传输——又慢又碎 |
| 1024 字节占 RAM 的 5% | 显存大小的算术：128×64 像素 ÷ 8（每字节 8 像素）= 1024。这就是"页结构"存在的意义——**用字节打包位** |
| `OLED_PAGES` 宏与 SSD1306 的 8 页结构对应 | SSD1306 把 64 行分成 8 页（PAGE0~7），每页 128 字节、每字节管 8 行纵向像素——**显存布局必须迁就芯片的寻址结构**（→ [[SSD1306]]） |

### 2. SetPixel：位运算分解坐标

```c
page = y >> 3;              /* y ÷ 8 → 第几页（右移 3 位=除以 8） */
bit  = y & 0x07;            /* y % 8 → 页内第几位 */
s_gram[page][x] |=  (1 << bit);   /* 点亮：或上 1 */
s_gram[page][x] &= ~(1 << bit);   /* 熄灭：与上 0（其余位不动） */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `y>>3` 与 `y&7`：移位除法 + 掩码取余，一组"除 8 拆商余"的组合拳（→ [[C语言基本数据类型与表达式]]） | 屏幕上任意一点 (x,y) 对应显存 `s_gram[y/8][x]` 的第 `y%8` 位——**坐标到字节位的映射公式**是所有点阵屏编程的第一课 |
| `|=` 置位、`&= ~` 清位：**位操作双璧**，只动目标位不伤邻居 | 一个字节里挤着 8 个像素，改一个必须"外科手术式"操作。这四行是嵌入式最高频的代码之一，必须条件反射 |
| 越界直接 return | 坐标边界防御——画到屏外悄悄丢弃而不是崩溃 |

### 3. ShowChar6x8：查表渲染

```c
if (ch < 0x20 || ch > 0x7E)  ch = ' ';        /* 越界字符当空格 */
glyph = &F6x8[ch - 0x20][0];                   /* ASCII → 数组下标 */
for (i = 0; i < 6; i++)
    s_gram[page][x + i] = glyph[i];            /* 6 个字节直接覆盖进显存 */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `&F6x8[...][0]`：取二维数组一行的首地址当一维数组用（→ [[C语言指针]]） | **字库 = 查表**：字符 'A' 的形状提前画好存成 6 字节。渲染就是"把 6 个字节抄进显存" |
| `ch - FONT6X8_FIRST_CHAR`：下标平移 | 字库从空格(0x20)开始存，ASCII 减 0x20 才是数组下标——**区间映射**惯用手法 |
| 直接赋值 `=` 而不是 `|=` | 字符格要**覆盖**背景（字后面不该有旧像素残留）——与 SetPixel 的 `|=` 形成对比：改单像素用位操作，整块覆盖用赋值 |

### 4. ShowString6x8：指针遍历 + 步进

```c
while ((*str) != '\0')
{
    OLED_ShowChar6x8(x, page, *str);
    str++;                       /* 指针走到下一字符 */
    x += FONT6X8_WIDTH;          /* 光标右移 6 像素 */
    if (x >= OLED_WIDTH) break;  /* 出屏停笔 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `*str` 解引用 + `str++`：**C 字符串遍历标准姿势**（→ [[C语言指针]]），`'\0'` 是天然终止符 | 字符串渲染=循环调字符渲染；`const char *str` 只读承诺——不许函数偷改字符串字面量 |
| 6×8=每字符 6 列宽 | 显示坐标以**像素**为单位而不是字符格——排版自由度最大，代价是调用方自己算位置（main 里 `54u + 6u*3u` 就是在手算偏移） |

### 5. ShowBigString：2×2 放大 = 每个源像素变四格

```c
for (col...) for (bit...)          /* 扫字形 6×8 的每一位 */
    if (bits & (1 << bit))         /* 该位是 1（笔画） */
    {
        bx = x + col*2;  by = page*8 + bit;    /* 源坐标 → 屏坐标 */
        SetPixel(bx,   by,   1);  SetPixel(bx+1, by,   1);   /* 2x2 复制 */
        SetPixel(bx,   by+1, 1);  SetPixel(bx+1, by+1, 1);
    }
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 逐位检测 `bits & (1<<bit)`：把字节**拆回像素**（SetPixel 的逆过程） | 小字放大不用另做一套字库——**运行时把 6×8 字形逐位放大成 12×16**。空间（不加大字库）换时间（多耗点 CPU），分数只有一行，划算 |
| `page*8 + bit`：把"页+位"还原成绝对 y 坐标 | 与 SetPixel 的 `y>>3`/`y&7` **互为逆运算**——一对函数放一起看，坐标体系立刻通透 |
| 字宽步进 ×2 | 小字占 6 列，大字占 12 列——布局参数随放大倍数走，不改字库 |

### 6. OLED_Update：打包推送

```c
static const uint8_t colRange[6] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
prv_I2C_Write(0x00, colRange, 6);        /* 命令：设水平寻址窗口（全屏） */
for (page = 0; page < 8; page++)
{
    flushBuf 拷入 s_gram[page];
    prv_I2C_Write(0x40, flushBuf, 128);  /* 数据：整页 128 字节连发 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `static` 局部缓冲：函数退出值还在，下次复用同一块内存 | 1024 字节显存**不直接发**，先抄进 128 字节缓冲逐页推——避免超大临时变量，也让传输节奏可控 |
| 控制字节 `0x00`=命令流、`0x40`=数据流 | SSD1306 的 I2C 协议：地址后的第一个字节声明"后面是命令还是数据"（→ [[SSD1306]]）。`0x21`=设列范围、`0x22`=设页范围——设定寻址窗口后数据自动递增填充 |
| `if (prv_I2C_Write(...) != 0u) return;` | I2C 出错就放弃本帧，**等下一轮 50ms 刷新**——显示容忍偶尔丢帧，但绝不容忍卡死主循环（对比 [[贪吃蛇代码-oled的I2C底层]] 的恢复机制） |
| 水平寻址模式的便利 | 寻址窗口设好后连发 128×8 字节自动回卷——不用每页重新定位，传输效率高 |

### 7. 字库数据本身

| C 语言知识 | 程序原理 |
|-----------|---------|
| `static const uint8_t F6x8[95][6]`：95 个可打印 ASCII（空格到 `~`）× 6 字节 = 570 字节常量，**放 Flash 不占 RAM**（const + static 保证） | **纵向取模**：每字节的 8 个位对应 8 行纵向像素、低位在上，第 6 字节是字间距列（通常 0）。例：'1' = `{0x00,0x42,0x7F,0x40,0x00,0x00}`，把 0x7F 二进制展开 01111111 就是中间一竖贯穿 8 行 |
| 数组初始化器一行一字符+注释 | 自制字库的工作量在"画字形"——生成器或手工点阵；代码只是数据容器 |

## 本页 C 语言知识索引

- [[C语言数组与字符串]] — 二维显存、字库常量表
- [[C语言指针]] — 行首地址、字符串遍历、const 限定
- [[C语言基本数据类型与表达式]] — 移位/掩码/位清除四大件
- [[C语言流程控制]] — 嵌套循环、提前终止
- [[C语言编译预处理]] — static const 进 Flash 的原因

## 本页程序原理索引

- [[SSD1306]] — 页结构显存、初始化命令、寻址模式
- [[I2C协议]] — 显存数据怎么变成两根线上的波形
- [[贪吃蛇代码-oled的I2C底层]] — prv_I2C_Write 的实现与超时恢复
- [[贪吃蛇代码-main主循环与时间片]] — 谁在什么时候调 ShowBigString/Update

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图

## Sources

- [oled.h](../sources/SnakeGame_Keil5源码/oled.h) / [oled.c](../sources/SnakeGame_Keil5源码/oled.c) / [oledfont.h](../sources/SnakeGame_Keil5源码/oledfont.h) — 2026-09-18 起
- [LED点阵贪吃蛇游戏系统设计报告.docx](../sources/LED点阵贪吃蛇游戏系统设计报告.docx) — 实时计分验收项，2026-09
