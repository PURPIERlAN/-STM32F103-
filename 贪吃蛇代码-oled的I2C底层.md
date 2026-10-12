# 贪吃蛇代码-oled的I2C底层

> `App/oled.c`（传输层）— I2C1 五步事务（BUSY→START→ADDR→DATA→STOP），每步带超时，超时自动复位外设恢复总线——**F1 硬件 I2C "会卡死" 的治本写法**。

| 项目 | 内容 |
|------|------|
| 文件位置 | `SnakeGame_Keil5/App/oled.c`（本页覆盖 prv_I2C_GPIO_Init / prv_I2C_Recover / prv_I2C_Write / prv_OLED_Cmd / OLED_Init） |
| 职责 | 把字节流变成 I2C 波形；外设初始化；异常自愈 |
| 硬件 | PB6=SCL / PB7=SDA（复用开漏 + 板载 4.7k 上拉），SSD1306 地址 0x3C |
| 原理页 | 总线协议 → [[I2C协议]]；芯片命令表 → [[SSD1306]]；绘制层 → [[贪吃蛇代码-oled显存与字库]] |

## 完整代码（oled.c 传输层部分）

```c
#include "oled.h"
#include "oledfont.h"
#include "stm32f10x.h"

#define I2C_TIMEOUT_LOOPS   30000u   /* 事件等待超时计数（避免死等） */

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
```

## 逐段对应拆解：代码 ↔ C 语言 ↔ 程序原理

### 1. GPIO 模式：I2C 为什么必须开漏

```c
gpio.GPIO_Mode = GPIO_Mode_AF_OD;   /* 复用开漏——不是推挽！ */
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| AF_OD（复用开漏）vs max7219.c 的 AF_PP（复用推挽）：一字之差，总线语义完全不同 | **I2C 是"线与"总线**：开漏输出只能拉低不能主动拉高，高电平靠外部上拉电阻"拽"上去。这样多个设备同时驱动总线时不会打架（一个拉低即全低），还支持从机时钟拉伸。推挽输出接 I2C 总线 = 两个输出对顶 = 短路（→ [[I2C协议]]） |
| 上拉电阻不在这份代码里 | 模块板载 4.7kΩ。裸芯片方案才需要自己配——**硬件问题硬件解决，软件只管配对模式** |

### 2. 超时等待的三行骨架（全文重复 7 次的模式）

```c
t = I2C_TIMEOUT_LOOPS;                    /* 30000 次预算 */
while (等某个事件 != 完成)
{
    if (--t == 0u) { 处理后事; return 1u; }   /* 预算烧完 → 放弃 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `--t` 前缀递减当倒计时器；`t` 用 uint32_t | **软件看门狗**：硬件 I2C 出错时（从机不 ACK、总线被拉死）事件永远等不到，裸 `while` 就**永久挂死主循环**——游戏冻结、按键失灵，全板假死。超时退出把它变成"丢一帧，活着" |
| 返回值约定：0 成功 / 1 失败 | C 语言**错误码惯例**（对比指针/异常方案）：调用方 `if (prv_I2C_Write(...) != 0u) return;`（→ [[贪吃蛇代码-oled显存与字库]] 第 6 段） |
| 30000 次循环多长？ | 72MHz 下大约几 ms 量级——足够正常事件（µs 级）完成许多倍，又不会卡主循环太久 |

### 3. 五步事务：一次完整写流程

```
① BUSY 清      总线没人用
② START (EV5)  产生起始条件，进入主机模式
③ ADDR  (EV6)  发 0x3C 写地址，从机 ACK
④ DATA  (EV8)  控制字节 + len 个数据字节逐个发
⑤ 最后一字节移完 (EV8-2) → STOP 结束
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 每步"发命令 → 等事件"两行一组，顺序**不可乱** | F1 硬件 I2C 是**事件驱动状态机**：库函数 I2C_CheckEvent 检查的 EV5/EV6/EV8 是参考手册里的事件编号。上一步没完成就发下一步 = 总线状态错乱 |
| `const uint8_t *buf` + `buf++` 遍历 | 发送缓冲区只读——写函数不许动源数据（→ [[C语言指针]]） |
| ③④ 步失败时先补发 STOP 再恢复 | 事务中途夭折，总线还停在"会话中"状态——**先礼貌挂断（STOP）再重启**，否则 SDA 可能被从机扣着拉低 |
| EV8（TRANSMITTING）vs EV8-2（TRANSMITTED）的区别 | 前者="又腾出一个缓冲位，可以塞下一个字节"（边发边装），后者="最后一个字节彻底移出了线"。STOP 必须等后者——**提早 STOP 会截尾** |

一句话理解五步事务：**打电话**——①确认线路空闲 ②拨号（START）③对方接听确认身份（地址+ACK）④说正事（数据逐个 ACK）⑤说"就到这吧"（STOP）。任何一步对方没反应，超时就挂断重来。

### 4. prv_I2C_Recover：软复位外设

```c
static void prv_I2C_Recover(void)
{
    I2C_Cmd(I2C1, DISABLE);   /* 关外设 */
    I2C_DeInit(I2C1);         /* 寄存器全部恢复复位值 */
    prv_I2C_GPIO_Init();      /* 重新走一遍完整初始化 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| 函数复用：Recover 直接调 Init 的封装 | **"重启解决 90% 问题"在寄存器层面也成立**：F1 硬件 I2C 的已知顽疾（状态机卡死在 BUSY）软件等不回来，唯一的办法是把外设整个归零重来。DeInit+Init 后下一帧自动重试成功（调用方下一轮 50ms 就会再刷） |
| static：恢复机制是实现细节 | 调用方只知道"返回 1=这帧失败了"，不知道也不会关心内部重试过——**封装故障处理** |

### 5. prv_OLED_Cmd 与 OLED_Init 序列

```c
static void prv_OLED_Cmd(uint8_t cmd)
{
    uint8_t c = cmd;
    (void)prv_I2C_Write(0x00u, &c, 1u);   /* 0x00=命令流，1 字节 */
}
```

| C 语言知识 | 程序原理 |
|-----------|---------|
| `(void)` 显式丢弃返回值 | "我知道会失败但不处理"要**写出来**——初始化阶段失败也照样走完序列（屏幕不亮好过启动卡死），读者一眼看清这是有意为之 |
| `&c` 取局部变量的地址传入 | Write 要指针，单个值就得"先存变量再取址"——C 语言没有引用传参的写法（→ [[C语言指针]]） |

初始化命令速查（完整命令表 → [[SSD1306]]）：

| 命令 | 含义 | 为什么必须有 |
|------|------|------------|
| 0xAE / 0xAF | 关显示 / 开显示 | 配置期间别闪花屏；全部就绪才"睁眼" |
| 0x8D 0x14 | **电荷泵使能** | 模块没有外部 VCC 供电脚时，OLED 靠内部电荷泵升压——不开它屏幕全黑（新模块"不亮"第一嫌疑） |
| 0x20 0x00 | 水平寻址模式 | 配合显存整屏推送：写完自动跳下一页 |
| 0xA1 / 0xC8 | 段映射 / COM 扫描方向 | 决定画面上下左右朝向——装反了就改这两条 |
| 0x81 0xCF | 对比度 | 亮度手感调校 |
| 0xA8 0x3F | 复用率 1/64 | 64 行屏的固定搭配 |

## 本页 C 语言知识索引

- [[C语言流程控制]] — 超时倒计时、五步顺序结构
- [[C语言指针]] — const 缓冲区遍历、取址传单值
- [[C语言函数与程序结构]] — static 分层、错误码返回、(void) 丢弃

## 本页程序原理索引

- [[I2C协议]] — 开漏线与、START/STOP、ACK 机制（本页是主机发送方向的完整落地）
- [[SSD1306]] — 命令/数据流区分、初始化命令表
- [[贪吃蛇代码-oled显存与字库]] — 上游：显存与 Update 的调用方式
- [[贪吃蛇代码-max7219点阵驱动]] — 对照：SPI 的三等待范式（两条总线两种脾气）

## Related

- [[贪吃蛇代码导读]] — 全部代码笔记的地图

## Sources

- [oled.c](../sources/SnakeGame_Keil5源码/oled.c) — 2026-09-18 起
- [STM32F10xxx参考手册（中文）..pdf](../sources/STM32F10xxx参考手册（中文）..pdf) — I2C 事件 EV5/EV6/EV8 定义，2026-09
