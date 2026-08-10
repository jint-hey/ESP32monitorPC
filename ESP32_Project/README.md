# ESP32 PC hardware monitor

该目录是 ESP-IDF 5.3.x 的 ESP32-S3 C++ 工程，用于通过 UART0 接收 PC 上位机发送的 CPU、内存和 GPU 数据，并在 0.96 寸 SSD1306 OLED 上显示。

## 硬件连接

- UART：UART0，115200、8-N-1，TX=GPIO43，RX=GPIO44；使用开发板连接 Type-C 的串口通道。
- OLED：SSD1306 128x64，I2C1，地址 `0x3C`，SDA=GPIO6，SCL=GPIO7，时钟 400 kHz。

引脚和串口参数集中在 `main/app_config.hpp`，如果实际开发板接线不同，只需修改该文件。

## 模块

- `uart_receiver.*`：UART0 驱动和接收任务。
- `byte_ring_buffer.hpp`：固定容量环形缓冲区；满时丢弃最旧数据并继续寻找下一帧。
- `pc_protocol.*`：帧同步、长度检查、CRC-16/CCITT-FALSE 校验、Ping/Pong 编码。
- `hardware_state.*`：HardwareInfo/HardwareUsage 解包和线程安全快照。
- `oled_display.*`：SSD1306 I2C 驱动、显存和 6x8 ASCII 绘制。
- `oled_ui.*`：CPU、MEM、GPU 同页显示，固定标签及百分比，GPU 名称在限定区域内往返滚动。

协议与 `PC_Project/PC_monitor/PacketProtocol.*` 保持一致：帧头 `AA 55`，多字节字段为小端，最大 payload 为 512 字节，CRC 不包含帧头。

## 编译和烧录

```powershell
cd ESP32_Project
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash
```

工程默认关闭 ESP-IDF 控制台日志，避免日志文本混入 UART0 的二进制协议。烧录时先退出占用该串口的 PC 上位机；运行上位机时也应先关闭 `idf.py monitor`。Boot ROM 在复位时输出的启动文字不会影响接收器，协议解析器会自动重新同步到 `AA 55` 帧头。
