# Bluetooth Audio 引脚资源分配

本文档记录当前项目已经使用的 STM32F407VG 引脚，以及后续接入 SPI TFT 屏幕和旋转编码器时推荐的接线方案。

## 当前已占用引脚

### ES8311 音频 I2S2

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| I2S2_CK | PB10 | ES8311 BCLK |
| I2S2_WS | PB12 | ES8311 LRCK/WS |
| I2S2_SD | PC3 | I2S TX 数据 |
| I2S2_ext_SD | PC2 | I2S RX 数据 |
| I2S2_MCK | PC6 | ES8311 MCLK |

### ES8311 软件 I2C

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| I2C1_SCL | PC11 | 软件 I2C，当前用于 ES8311 控制 |
| I2C1_SDA | PC12 | 软件 I2C，当前用于 ES8311 控制 |

说明：这是软件 I2C，后续理论上可以换脚，但当前没有必要移动。

### 蓝牙 ESP32 HCI UART2

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| UART2_TX | PA2 | STM32 -> ESP32 |
| UART2_RX | PA3 | ESP32 -> STM32 |
| UART2_CTS/RTS | PA0 / PA1 | 当前 BT 配置开启硬件流控，建议保留 |

### 调试串口 UART1

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| UART1_TX | PA9 | RT-Thread 控制台 |
| UART1_RX | PA10 | RT-Thread 控制台 |

### 保留串口 UART3

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| UART3_TX | PD8 | 当前计划保留 |
| UART3_RX | PB11 | 当前计划保留 |

### 按键 / 编码器按压

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| KEY / ENC_SW | PC9 | 当前按键接口，后续可接编码器 SW |

### 调试接口

| 功能 | MCU 引脚 | 说明 |
| --- | --- | --- |
| SWDIO | PA13 | 调试下载 |
| SWCLK | PA14 | 调试下载 |

## 推荐新增模块接线

### SPI TFT 屏幕，推荐 SPI1

推荐屏幕：2.4 寸 ST7789 IPS SPI TFT，240x320，不带触摸。

| 屏幕引脚 | MCU 引脚 | 说明 |
| --- | --- | --- |
| SCL / SCK | PA5 | SPI1_SCK |
| SDA / MOSI | PA7 | SPI1_MOSI |
| CS | PA4 | TFT 片选 |
| DC / RS | PE4 | 数据/命令选择 |
| RST / RES | PE5 | 屏幕复位 |
| BL / LED | 3.3V 或 PWM GPIO | 第一版建议直接接 3.3V 常亮 |
| VCC | 3.3V | 按模块规格接，优先 3.3V 逻辑 |
| GND | GND | 共地 |

说明：
- SPI 屏通常只需要 SCK 和 MOSI，不需要 MISO。
- SPI2 不能用于屏幕，因为 SPI2 已经作为 I2S2 给 ES8311 使用。
- SPI3 不优先推荐，因为常见引脚可能会碰到 PC11/PC12 软件 I2C。
- 后续如果要高刷新率，可以考虑 SPI1 TX DMA。

### 旋转编码器

推荐模块：EC11/KY-040，带 CLK/DT/SW/VCC/GND。

| 编码器引脚 | MCU 引脚 | 说明 |
| --- | --- | --- |
| CLK / A | PB6 | 正交编码器 A 相 |
| DT / B | PB7 | 正交编码器 B 相 |
| SW | PC9 | 按压，复用当前按键接口 |
| VCC | 3.3V | 模块供电 |
| GND | GND | 共地 |

说明：
- PB6/PB7 当前未被工程明确占用。
- 后续如果旋转方向反了，优先软件里加 `ENCODER_REVERSE` 宏反转。
- 编码器 A/B 建议配置为输入上拉。

## 当前不建议占用的引脚

| 引脚 | 原因 |
| --- | --- |
| PA0 / PA1 | 蓝牙 UART2 当前开启硬件流控，代码会配置 RTS/CTS |
| PA2 / PA3 | 蓝牙 UART2 TX/RX |
| PA9 / PA10 | UART1 控制台 |
| PA13 / PA14 | SWD 调试下载 |
| PB10 / PB12 | ES8311 I2S2 |
| PC2 / PC3 / PC6 | ES8311 I2S2 |
| PC9 | 当前按键/后续编码器 SW |
| PC11 / PC12 | 当前 ES8311 软件 I2C |
| PD8 / PB11 | UART3，需要保留 |

## 后续软件配置提醒

接入 SPI TFT 时，至少需要确认以下开关或配置：

```c
RT_USING_SPI
BSP_USING_SPI1
HAL_SPI_MODULE_ENABLED
```

如果后续使用 SPI DMA 刷屏，需要额外确认：

```c
BSP_SPI1_TX_USING_DMA
```

当前 DMA 资源上，SPI1 TX DMA 与已有音频 I2S DMA、UART3 RX DMA、UART1 RX DMA 不直接冲突。

## 结论

按当前推荐接线，新增 TFT 屏幕和旋转编码器不会与现有 ES8311、蓝牙 UART、UART3、调试串口冲突。当前引脚资源足够，软件 I2C 虽然可以迁移，但目前没有迁移必要。
