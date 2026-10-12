/**
  ******************************************************************************
  * @file    oled.h
  * @author  Xu Xiansen / K
  * @brief   0.96 寸 SSD1306 OLED 128x64 驱动（I2C1: PB6=SCL, PB7=SDA, 地址 0x3C）
  *
  * 用于显示 SCORE 与游戏状态（报告需求映射表：实时计分验收项）。
  * ******************************************************************************
  */
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
