/**
  ******************************************************************************
  * @file    max7219.c
  * @author  Xu Xiansen / K
  * @brief   MAX7219 8x8 点阵驱动实现（STM32 标准外设库 3.5）
  *
  * 硬件连接（报告第 3 节）：
  *   PA5 -> MAX7219 CLK (SCK, SPI1)
  *   PA7 -> MAX7219 DIN (MOSI, SPI1)
  *   PA4 -> MAX7219 CS  (软件片选，空闲拉高，帧传输期间拉低)
  *
 * SPI 配置：主机 / 8 位 / CPOL=0 CPHA=0（CLK 空闲低电平，上升沿锁存）/
 * MSB 先行 / 预分频 64 -> 72MHz/64 = 1.125 MHz
 *
 * 电平兼容说明（2026-09-20 实测）：
 *   MAX7219 用 5V 供电时 VIH 门槛 3.5V，高于 STM32 的 3.3V 输出，
 *   高速时钟下临界采样会出错（表现为显示规律乱码/亮线）。
 *   对策 1：降速到 1.125 MHz，给临界电平留足建立时间（本文件已采用）；
 *   对策 2（根治）：模块 VCC 串 2 只 1N4148/1N4007 降压到约 4.3V，
 *          VIH 门槛随之降到约 3.0V，3.3V 直连彻底稳定。
  *
  * 适配提示：不同点阵模块的行/列走线可能有差异，若画面方向不对，
  * 调整下方 MAX7219_ROW_FLIP / MAX7219_COL_FLIP 两个宏即可。
  * ******************************************************************************
  */
#include "max7219.h"
#include "stm32f10x.h"

/* 显示方向适配宏：接线后画面不对时按需置 1 */
#define MAX7219_ROW_FLIP    0u   /* 1 = 行序上下翻转 */
#define MAX7219_COL_FLIP    0u   /* 1 = 行内位序左右镜像 */

/* 软件片选：PA4，CS 拉低期间连续发 16 位（寄存器+数据），拉高锁存 */
#define MAX7219_CS_LOW()    GPIO_ResetBits(GPIOA, GPIO_Pin_4)
#define MAX7219_CS_HIGH()   GPIO_SetBits(GPIOA, GPIO_Pin_4)

/* SPI1 发送一个字节（阻塞式，读走 RXNE 防溢出标志） */
static void prv_SPI1_SendByte(uint8_t dat)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    {
        /* 等待发送缓冲空 */
    }
    SPI_I2S_SendData(SPI1, dat);

    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET)
    {
        /* 等待接收完成（全双工回环） */
    }
    SPI_I2S_ReceiveData(SPI1);   /* 读走数据清 RXNE */

    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET)
    {
        /* 等待总线空闲，确保移位完成后再拉高 CS */
    }
}

/* GPIO/SPI1 外设初始化 */
static void prv_SPI1_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef  spi;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    /* PA5 SCK / PA7 MOSI：复用推挽输出 */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA4 CS：普通推挽输出，空闲高电平 */
    gpio.GPIO_Pin   = GPIO_Pin_4;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    MAX7219_CS_HIGH();

    /* SPI1 主机，Mode0，8bit，MSB 先行，软件 NSS，1.125 MHz（低速换电平裕量） */
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial      = 7u;
    SPI_Init(SPI1, &spi);

    SPI_Cmd(SPI1, ENABLE);
}

void MAX7219_WriteReg(uint8_t reg, uint8_t val)
{
    MAX7219_CS_LOW();
    prv_SPI1_SendByte(reg);
    prv_SPI1_SendByte(val);
    MAX7219_CS_HIGH();
}

void MAX7219_SetRow(uint8_t row, uint8_t rowBits)
{
    if (row > 7u)
    {
        return;
    }
#if (MAX7219_ROW_FLIP == 1u)
    row = (uint8_t)(7u - row);
#endif
#if (MAX7219_COL_FLIP == 1u)
    {
        uint8_t i;
        uint8_t rev = 0u;
        for (i = 0u; i < 8u; i++)
        {
            if ((rowBits & (uint8_t)(1u << i)) != 0u)
            {
                rev |= (uint8_t)(1u << (7u - i));
            }
        }
        rowBits = rev;
    }
#endif
    MAX7219_WriteReg((uint8_t)(MAX7219_REG_DIGIT0 + row), rowBits);
}

void MAX7219_Clear(void)
{
    uint8_t r;
    for (r = 0u; r < 8u; r++)
    {
        MAX7219_SetRow(r, 0x00u);
    }
}

void MAX7219_Init(void)
{
    prv_SPI1_GPIO_Init();

    MAX7219_WriteReg(MAX7219_REG_SHUTDOWN,   0x00u);  /* 先关显示 */
    MAX7219_WriteReg(MAX7219_REG_DISPLAYTEST, 0x00u); /* 关测试模式 */
    MAX7219_WriteReg(MAX7219_REG_DECODEMODE,  0x00u); /* 无 BCD 译码，直接点阵 */
    MAX7219_WriteReg(MAX7219_REG_SCANLIMIT,   0x07u); /* 扫描全部 8 行 */
    MAX7219_WriteReg(MAX7219_REG_INTENSITY,   0x04u); /* 中等亮度，兼顾面包板供电 */

    MAX7219_Clear();

    MAX7219_WriteReg(MAX7219_REG_SHUTDOWN,   0x01u);  /* 正常工作 */
}
