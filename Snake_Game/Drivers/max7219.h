/**
  ******************************************************************************
  * @file    max7219.h
  * @author  Xu Xiansen / K
  * @brief   MAX7219 8x8 LED 点阵驱动（SPI1: PA5=SCK, PA7=DIN, PA4=CS 软件控制）
  *
  * 设计报告对应：SPI1 连接 MAX7219 点阵模块，MCU 只需通过 SPI 更新
  * 显示 RAM（DIGIT0~DIGIT7），MAX7219 自行多路扫描，闪烁小、CPU 占用低。
  * ******************************************************************************
  */
#ifndef __MAX7219_H
#define __MAX7219_H

#include <stdint.h>

/* MAX7219 寄存器地址 */
#define MAX7219_REG_NOOP        0x00u
#define MAX7219_REG_DIGIT0      0x01u   /* 行 0 显示 RAM */
#define MAX7219_REG_DIGIT1      0x02u
#define MAX7219_REG_DIGIT2      0x03u
#define MAX7219_REG_DIGIT3      0x04u
#define MAX7219_REG_DIGIT4      0x05u
#define MAX7219_REG_DIGIT5      0x06u
#define MAX7219_REG_DIGIT6      0x07u
#define MAX7219_REG_DIGIT7      0x08u
#define MAX7219_REG_DECODEMODE  0x09u
#define MAX7219_REG_INTENSITY   0x0Au   /* 亮度 0x00~0x0F */
#define MAX7219_REG_SCANLIMIT   0x0Bu
#define MAX7219_REG_SHUTDOWN    0x0Cu   /* 0x00 关显示, 0x01 正常 */
#define MAX7219_REG_DISPLAYTEST 0x0Fu

/* 初始化 SPI1 与 MAX7219（关译码、全行扫描、中等亮度、清屏后点亮） */
void MAX7219_Init(void);

/* 写单个寄存器：reg=寄存器号, val=数据 */
void MAX7219_WriteReg(uint8_t reg, uint8_t val);

/* 设置一行点阵：row 0~7, rowBits 的 bit0~bit7 对应该行列 0~7 */
void MAX7219_SetRow(uint8_t row, uint8_t rowBits);

/* 整屏清空 */
void MAX7219_Clear(void);

#endif /* __MAX7219_H */
