# ESP32 PC hardware monitor

该目录是 ESP-IDF 5.3.x 的 ESP32-S3 C++ 工程，用于通过 UART0 接收 PC 上位机发送的 CPU、内存和 GPU 数据，并在 0.96 寸 SSD1306 OLED 上显示。工程同时以独立低优先级任务运行路由器设备监控；这部分不会占用 UART0、I2C 或修改原有 OLED 页面。

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
- `oled_ui.*`：三页轮换显示 PC 硬件、Codex 配额和路由检测设备信息；长名称在限定区域内往返滚动。
- `components/router_monitor/`：Wi-Fi、TP-Link 在线设备查询、PushPlus 通知和 NVS 状态持久化。

协议与 `PC_Project/PC_monitor/PacketProtocol.*` 保持一致：帧头 `AA 55`，多字节字段为小端，最大 payload 为 512 字节，CRC 不包含帧头。

## 编译和烧录

先编辑 `spiffs/config.properties`。Wi-Fi、路由器、监控目标及 PushPlus 都从该文件读取；实际配置已加入 `.gitignore`。配置模板位于 `spiffs/config.example.properties`。

```powershell
cd ESP32_Project
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash
```

`flash` 会同时写入原 PC 监控固件和 SPIFFS 配置分区。路由状态保存在 NVS 中。当前硬件配置按 8 MB Quad Flash + 8 MB Octal PSRAM 设置。

OLED 每隔 `main/app_config.hpp` 中的 `OLED_PAGE_SWITCH_SECONDS` 切换页面。新增路由页显示检测状态、目标、匹配设备名称、IP 和 MAC；PC 上位机断开时该页仍会继续显示。

工程默认关闭 ESP-IDF 控制台日志，避免日志文本混入 UART0 的二进制协议。烧录时先退出占用该串口的 PC 上位机；运行上位机时也应先关闭 `idf.py monitor`。Boot ROM 在复位时输出的启动文字不会影响接收器，协议解析器会自动重新同步到 `AA 55` 帧头。
