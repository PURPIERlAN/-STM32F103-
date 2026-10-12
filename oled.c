/**
  ******************************************************************************
  * @file    oled.c
  * @author  Xu Xiansen / K
  * @brief   SSD1306 OLED 128x64 驱动实现（STM32 标准外设库 3.5，硬件 I2C1）
  *
  * 硬件连接（报告第 3 节）：
  *   PB6 -> OLED SCL（I2C1）
  *   PB7 -> OLED SDA（I2C1）
  *   I2C 地址通常为 0x3C
  *
  * 说明：F1 硬件 I2C 在总线异常时可能挂起，所有等待均带超时保护，
  * 超时后复位 I2C1 外设恢复总线，避免主循环卡死。
  * OLED 模块需带 4.7k 上拉（市售 0.96 寸模块一般板载）。
  * ******************************************************************************
  */
#include "oled.h"
#include "oledfont.h"
#include "stm32f10x.h"

#define I2C_TIMEOUT_LOOPS   30000u   /* 事件等待超时计数（避免死等） */

/* 本地显存：8 页 x 128 字节（每字节 8 个纵向像素，低位在上） */
static uint8_t s_gram[OLED_PAGES][OLED_WIDTH];

/* ---------------- I2C1 底层 ---------------- */

static void prv_I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    I2C_InitTypeDef  i2c;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PB6 SCL / PB7 SDA：复用开漏输出，靠模块板载上拉 */
    gpio.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00u;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed          = 400000u;   /* 快速模式 */
    I2C_Init(I2C1, &i2c);

    I2C_Cmd(I2C1, ENABLE);
}

/* I2C 异常恢复：关闭并重新初始化 I2C1，释放总线 */
static void prv_I2C_Recover(void)
{
    I2C_Cmd(I2C1, DISABLE);
    I2C_DeInit(I2C1);
    prv_I2C_GPIO_Init();
}

/* SSD1306 写事务：START + ADDR(W) + ctrl + len 字节 + STOP
 * ctrl: 0x00 命令流, 0x40 数据流；返回 0 成功，1 超时失败 */
static uint8_t prv_I2C_Write(uint8_t ctrl, const uint8_t *buf, uint16_t len)
{
    uint32_t t;

    /* 1. 等待总线空闲 */
    t = I2C_TIMEOUT_LOOPS;
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) != RESET)
    {
        if (--t == 0u) { prv_I2C_Recover(); return 1u; }
    }

    /* 2. 起始位 EV5 */
    I2C_GenerateSTART(I2C1, ENABLE);
    t = I2C_TIMEOUT_LOOPS;
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT) == ERROR)
    {
        if (--t == 0u) { prv_I2C_Recover(); return 1u; }
    }

    /* 3. 从机地址 EV6 */
    I2C_Send7bitAddress(I2C1, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    t = I2C_TIMEOUT_LOOPS;
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == ERROR)
    {
        if (--t == 0u) { I2C_GenerateSTOP(I2C1, ENABLE); prv_I2C_Recover(); return 1u; }
    }

    /* 4. 控制字节 + 数据流 */
    I2C_SendData(I2C1, ctrl);
    t = I2C_TIMEOUT_LOOPS;
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTING) == ERROR)
    {
        if (--t == 0u) { I2C_GenerateSTOP(I2C1, ENABLE); prv_I2C_Recover(); return 1u; }
    }

    while (len > 0u)
    {
        I2C_SendData(I2C1, *buf);
        buf++;
        len--;
        t = I2C_TIMEOUT_LOOPS;
        while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTING) == ERROR)
        {
            if (--t == 0u) { I2C_GenerateSTOP(I2C1, ENABLE); prv_I2C_Recover(); return 1u; }
        }
    }

    /* 5. 等最后一字节完全移出 EV8-2，再 STOP */
    t = I2C_TIMEOUT_LOOPS;
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == ERROR)
    {
        if (--t == 0u) { prv_I2C_Recover(); return 1u; }
    }
    I2C_GenerateSTOP(I2C1, ENABLE);

    return 0u;
}

/* 写单条命令 */
static void prv_OLED_Cmd(uint8_t cmd)
{
    uint8_t c = cmd;
    (void)prv_I2C_Write(0x00u, &c, 1u);
}

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

void OLED_Init(void)
{
    prv_I2C_GPIO_Init();

    prv_OLED_Cmd(0xAEu);   /* 关显示 */
    prv_OLED_Cmd(0xD5u);   /* 时钟分频/振荡频率 */
    prv_OLED_Cmd(0x80u);
    prv_OLED_Cmd(0xA8u);   /* 复用率 1/64 */
    prv_OLED_Cmd(0x3Fu);
    prv_OLED_Cmd(0xD3u);   /* 显示偏移 0 */
    prv_OLED_Cmd(0x00u);
    prv_OLED_Cmd(0x40u);   /* 起始行 0 */

    prv_OLED_Cmd(0x8Du);   /* 电荷泵使能（模块无外部 VCC 时必须） */
    prv_OLED_Cmd(0x14u);

    prv_OLED_Cmd(0x20u);   /* 寻址模式：水平 */
    prv_OLED_Cmd(0x00u);
    prv_OLED_Cmd(0xA1u);   /* 段映射：左右正常 */
    prv_OLED_Cmd(0xC8u);   /* COM 扫描：上下反向（默认视角） */
    prv_OLED_Cmd(0xDAu);   /* COM 引脚配置 */
    prv_OLED_Cmd(0x12u);
    prv_OLED_Cmd(0x81u);   /* 对比度 */
    prv_OLED_Cmd(0xCFu);
    prv_OLED_Cmd(0xD9u);   /* 预充电周期 */
    prv_OLED_Cmd(0xF1u);
    prv_OLED_Cmd(0xDBu);   /* VCOMH 电压 */
    prv_OLED_Cmd(0x40u);
    prv_OLED_Cmd(0xA4u);   /* 显示跟随 RAM */
    prv_OLED_Cmd(0xA6u);   /* 非反相 */

    OLED_Clear();
    OLED_Update();

    prv_OLED_Cmd(0xAFu);   /* 开显示 */
}
