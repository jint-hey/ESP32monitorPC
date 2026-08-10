# PC Hardware Monitor + ESP32 OLED Display

## 项目简介

本项目由 Windows PC 上位机和 ESP32-S3 OLED 显示端组成：

- `PC_Project`：采集 CPU、内存、GPU 名称与使用率，并通过 Codex App Server 查询 ChatGPT Codex 周额度；程序以 Windows 托盘应用运行，将数据打包后通过串口发送。
- `ESP32_Project`：使用 ESP-IDF 5.3.x 和 C++，通过 UART0 接收串口字节流，完成帧同步、CRC 校验和 payload 解包，在 0.96 英寸 SSD1306 128×64 OLED 上轮流显示硬件状态和 Codex 周额度。

PC 和 ESP32 共用同一套二进制串口协议。协议帧版本当前为 `0x01`，Codex payload schema 当前为 `2`，不兼容旧版 Codex schema `1`。

## 快速使用

### 运行 PC 上位机

仓库中已经提供编译后的程序，优先运行 Release 版本：

```text
PC_Project/x64/Release/PC_monitor.exe
```

需要查看调试日志时运行：

```text
PC_Project/x64/Debug/PC_monitor.exe
```

操作步骤：

1. 使用 Type-C 数据线连接 ESP32-S3 与 PC。
2. 双击 `PC_monitor.exe`。程序不会显示主窗口，而是在 Win11 通知区域运行；图标被折叠时可点击任务栏右侧的 `^` 查找。
3. 右键托盘图标，将鼠标移到“串口选择”，选择 ESP32 对应的 COM 端口（如果存在多个，可以挨个试）。
4. 不再使用时，通过托盘菜单的“退出”关闭程序并释放串口。

Release 用于日常运行；Debug 会额外打开日志终端。Codex CLI 不可用或未登录时，CPU、内存和 GPU 监控仍能继续工作。

### 使用 Flash Download Tool 烧录 ESP32 固件

仓库已保留烧录所需固件，无需安装 ESP-IDF 或自行构建。进入 `ESP32_Project/build` 即可找到文件；烧录地址也记录在同目录的 `flasher_args.json` 和 `flash_args` 中：

| 下载地址 | 仓库中的固件文件 |
|---:|---|
| `0x0` | `ESP32_Project/build/bootloader/bootloader.bin` |
| `0x8000` | `ESP32_Project/build/partition_table/partition-table.bin` |
| `0x10000` | `ESP32_Project/build/pc_hardware_display.bin` |

烧录步骤：

1. 从[乐鑫官方工具页面](https://www.espressif.com/en/tools-type/flash-download-tools)下载并启动 Flash Download Tool。
2. 启动界面选择 `ChipType: ESP32-S3`、`WorkMode: Develop`、`LoadMode: UART`。
3. 在下载列表添加上表三个 `.bin` 文件，勾选每一行并填写对应地址。
4. 选择 `SPI SPEED: 80MHz`、`SPI MODE: DIO`、`FLASH SIZE: 2MB`。
5. 先退出占用串口的 `PC_monitor.exe` 和 `idf.py monitor`，再选择 ESP32 对应的 COM 端口和下载波特率。
6. 点击 `START`。如果无法自动进入下载模式，按住开发板 `BOOT`，短按 `RESET`，松开 `RESET` 后再松开 `BOOT`，然后重新开始。
7. 下载完成后复位开发板，再启动 PC 上位机并选择该串口。

## 目录结构

```text
.
├─ PC_Project/
│  └─ PC_monitor/
│     ├─ main.cpp                       Windows GUI 入口及 Debug 日志控制
│     ├─ TrayApplication.*              托盘图标、串口选择和生命周期管理
│     ├─ CpuMonitor.*                   CPU 使用率采集
│     ├─ MemoryMonitor.*                内存使用率采集
│     ├─ GpuMonitor.*                   GPU 名称和使用率采集
│     ├─ HardwareMonitor.*              硬件快照汇总
│     ├─ HardwarePacketEncoder.*        硬件 payload 编码
│     ├─ HardwareSerialSender.*         硬件数据周期发送
│     ├─ CodexQuotaMonitor.*            Codex App Server 进程和查询管理
│     ├─ CodexQuotaJsonParser.*         Codex JSONL 响应解析及周窗口选择
│     ├─ CodexQuotaPacketEncoder.*      Codex v2 payload 编码
│     ├─ CodexQuotaSerialSender.*       Codex 快照异步发送
│     ├─ PacketProtocol.*               通用帧编码、解码和 CRC
│     ├─ SerialCommunicator.*           串口收发线程及队列
│     └─ ConsoleLogger.*                Debug 控制台日志
│
└─ ESP32_Project/
   ├─ CMakeLists.txt
   ├─ sdkconfig.defaults
   └─ main/
      ├─ app_main.cpp                   ESP32 应用入口
      ├─ app_config.hpp                 UART、I2C、页面周期和时区配置
      ├─ uart_receiver.*                UART0 驱动与接收任务
      ├─ byte_ring_buffer.hpp           2048字节接收环形缓冲区
      ├─ pc_protocol.*                  帧同步、CRC 校验和 Ping/Pong 编码
      ├─ hardware_state.*               硬件 payload 解包及线程安全快照
      ├─ codex_quota_state.*            Codex v2 payload 解包
      ├─ unix_time.hpp                  Unix 时间到本地日期的转换
      ├─ oled_display.*                 SSD1306 I2C 驱动和 framebuffer
      └─ oled_ui.*                      硬件/Codex 页面绘制与定时切换
```

## PC 工程

### 运行方式

PC 程序是 Windows GUI 托盘程序：

- 启动后在 Win11 通知区域显示图标。
- 右键图标可以选择当前串口或退出程序。
- Debug 配置自动创建日志终端；Release 配置不创建控制台窗口。
- 硬件采集、Codex 查询和串口发送使用独立线程，某一数据源失败不会阻塞其他数据源。

### PC 线程架构

PC 端使用 Win32 消息循环作为主线程，并将阻塞操作和周期任务拆分为后台 `std::thread`：

| 线程 | 创建条件 | 主要职责 |
|---|---|---|
| 主线程 | 程序启动 | Win32 消息循环、托盘菜单、串口选择、状态通知和退出流程 |
| `HardwareMonitor` | 程序启动 | 默认每1秒采集 CPU、内存和 GPU，更新线程安全硬件快照 |
| `ConsoleLogger` 硬件日志线程 | 仅 Debug 日志开启时 | 默认每1秒复制硬件快照并打印，不参与采集和串口发送 |
| `CodexQuotaMonitor` | 程序启动 | 管理隐藏的 `codex app-server` 子进程、JSONL 收发、60秒查询及异常退避重启 |
| `CodexQuotaSerialSender` | 程序启动 | 监听额度快照序号，额度变化或串口重连时异步发送 `0x03` 数据包 |
| `HardwareSerialSender` | 串口连接成功 | 每秒发送 `HardwareUsage`，连接后及每10秒发送 `HardwareInfo` |
| `SerialCommunicator::TxWorker` | 串口连接成功 | 从有界发送队列取帧并执行串口写入 |
| `SerialCommunicator::RxWorker` | 串口连接成功 | 串口读取、通用帧解码，并处理 Ping/Pong 等返回数据 |

硬件和 Codex 数据通过“后台生产快照、发送线程复制快照”的方式解耦。共享快照和生命周期状态由 `std::mutex`、`std::atomic` 保护，发送队列及停止/重连事件通过 `std::condition_variable` 唤醒。退出时由 `TrayApplication` 依次停止发送、采集和通信线程并执行 `join()`，避免线程继续访问已销毁对象。

### 硬件数据流

```text
CPU/Memory/GPU Monitor
        │
        ▼
 HardwareMonitor 快照
        │
        ▼
HardwarePacketEncoder
        │
        ▼
 HardwareSerialSender
        │
        ▼
 SerialCommunicator → PacketEncoder → COM Port
```

`HardwareInfo` 包含 GPU 名称，在连接成功后发送，并每10秒重发一次，使 ESP32 单独复位后能够恢复完整 GPU 名称。`HardwareUsage` 默认每秒发送一次最新使用率。

### Codex 数据流

```text
codex app-server --listen stdio://
        │ JSONL
        ▼
CodexQuotaJsonParser
        │ 选择 limitId=codex 的最长有效窗口
        ▼
CodexQuotaMonitor 线程安全快照
        │
        ▼
CodexQuotaPacketEncoder (schema v2)
        │
        ▼
CodexQuotaSerialSender → SerialCommunicator
```

Codex 查询使用官方 `account/rateLimits/read`：优先读取 `rateLimitsByLimitId` 中的 `codex` bucket，没有该视图时回退到 `rateLimits`。在 `primary` 和 `secondary` 中选择持续分钟数最长的有效窗口作为周额度。启动并认证后立即查询，每60秒主动刷新，同时响应 `account/rateLimits/updated` 通知。

程序不会抓取网页、浏览器 Cookie 或保存 ChatGPT Token。Codex App Server 接口说明见 [OpenAI Codex App Server 文档](https://developers.openai.com/codex/app-server)。

## ESP32 工程

### 硬件配置

配置集中在 `ESP32_Project/main/app_config.hpp`：

| 功能 | 当前配置 |
|---|---|
| UART | UART0，115200，8-N-1，无流控 |
| UART TX/RX | GPIO43 / GPIO44，使用开发板 Type-C 串口 |
| OLED | SSD1306，128×64，I2C地址 `0x3C` |
| OLED SDA/SCL | GPIO13 / GPIO14 |
| I2C频率 | 400 kHz |
| 页面切换 | 5秒 |
| Codex时区 | UTC+8，即 `480` 分钟 |

UART0 同时连接 PC 二进制协议，因此正常运行时不要开启 `idf.py monitor`，并保持 ESP-IDF 控制台日志关闭，避免日志文本混入协议字节流。Boot ROM 的启动文本不包含合法完整数据帧，解析器会继续搜索 `AA 55` 帧头。

### ESP32 FreeRTOS 任务架构

`app_main` 初始化状态仓库和 OLED 后创建两个长期运行的 FreeRTOS 任务，本身不承担循环处理：

| 任务 | 栈大小 | 优先级 | 周期/等待 | 主要职责 |
|---|---:|---:|---|---|
| `pc_uart_rx` | 4096 bytes | 10 | UART 最多阻塞20ms | 每次最多读取256字节，写入2048字节环形缓冲区，解析完整帧、更新状态，并回复 Pong |
| `oled_ui` | 4096 bytes | 5 | 100ms | 复制最新硬件/Codex 快照、绘制 SSD1306 framebuffer、刷新 OLED，并按配置切换页面 |

UART 接收任务的优先级高于 OLED 任务，屏幕刷新不会阻塞串口字节流接收。`HardwareStateStore` 和 `CodexQuotaStateStore` 使用 FreeRTOS mutex 保护共享快照：UART 任务只在更新快照时短暂持锁，OLED 任务复制完成后再进行格式化和 I2C 绘制，从而缩短临界区并避免显示到半包数据。

### 接收和显示流程

```text
UART0 driver RX buffer
        │ 每次最多读取256字节
        ▼
ByteRingBuffer<2048>
        │
        ▼
pc_protocol::Parser
  帧头同步 → 长度检查 → CRC检查
        │
        ├─ 0x01/0x02 → HardwareStateStore
        ├─ 0x03      → CodexQuotaStateStore
        └─ 0x10      → 原序号回复 Pong
                         │
                         ▼
                  OledUi（每100ms刷新）
```

OLED 硬件页和 Codex 页每5秒轮换。GPU 标签固定，详细名称在限定区域内横向滚动。Codex 页显示周额度剩余百分比、已用百分比、剩余额度进度条、窗口长度及 UTC+8 重置日期。

## 串口通用数据帧

### 串口参数

```text
波特率：115200
数据位：8
校验位：None
停止位：1
流控：None
字节序：所有多字节整数均为小端（Little Endian）
```

### 帧格式

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | SOF1 | 固定 `0xAA` |
| 1 | 1 | SOF2 | 固定 `0x55` |
| 2 | 1 | Version | 通用帧协议版本，当前 `0x01` |
| 3 | 1 | Type | 数据包类型 |
| 4 | 1 | Flags | 通用包标志，当前业务包通常为0 |
| 5 | 2 | Sequence | `uint16` 序号，小端 |
| 7 | 2 | PayloadLength | payload长度，小端，最大512 |
| 9 | N | Payload | 由Type决定的数据 |
| 9+N | 2 | CRC16 | CRC结果，小端 |

总帧长度：

```text
TotalLength = 11 + PayloadLength
```

序号由 PC 串口连接建立时从0开始递增，达到 `65535` 后按 `uint16` 回绕。当前遥测数据不要求 ACK，序号主要用于诊断顺序和丢包。

### CRC规则

使用 `CRC-16/CCITT-FALSE`：

```text
Polynomial = 0x1021
Initial    = 0xFFFF
RefIn      = false
RefOut     = false
XorOut     = 0x0000
```

CRC计算范围从 `Version` 开始，到 payload 最后一个字节结束：

```text
Version + Type + Flags + Sequence + PayloadLength + Payload
```

帧头 `AA 55` 不参与 CRC。计算得到的16位 CRC 以小端顺序写入帧尾，即低字节在前、高字节在后。

### 数据包类型

| Type | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0x01` | HardwareInfo | PC → ESP32 | GPU数量和UTF-8名称 |
| `0x02` | HardwareUsage | PC → ESP32 | CPU、内存和GPU使用率 |
| `0x03` | CodexQuota | PC → ESP32 | Codex单个周额度，payload schema v2 |
| `0x10` | Ping | PC → ESP32 | 连接探测，payload为空 |
| `0x11` | Pong | ESP32 → PC | 使用Ping的原序号回复，payload为空 |
| `0x20` | Command | 预留 | 命令扩展 |
| `0x21` | Ack | 预留 | 应答扩展 |
| `0x7F` | Error | 预留 | 错误扩展 |

## Payload 打包与解包规则

### HardwareInfo，Type `0x01`

该 payload 为变长格式：

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | GPU Count，范围0～2 |
| 1 | 变长 | 按GPU Count重复以下记录 |

每条 GPU 记录：

| 相对偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | GPU ID，范围 `0..GPU Count-1` |
| 1 | 2 | UTF-8名称字节长度，小端 |
| 3 | N | GPU名称UTF-8字节，不包含结尾 `\0` |

PC 最多发送两个 GPU，每个名称最多120字节。ESP32 要求每个 GPU ID 唯一、名称长度不越过 payload 边界，并且所有记录解析结束后偏移必须恰好等于 `PayloadLength`，否则整包拒绝。

### HardwareUsage，Type `0x02`

百分比统一使用“实际百分比 × 100”的无符号16位整数：

```text
0.00%   → 0
12.34%  → 1234
100.00% → 10000
```

固定字段：

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 2 | CPU使用率×100 |
| 2 | 2 | 内存使用率×100 |
| 4 | 1 | GPU Count，范围0～2 |

之后每个 GPU 占3字节：

| 相对偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | GPU ID |
| 1 | 2 | GPU使用率×100，小端 |

payload长度必须满足：

```text
PayloadLength = 5 + GPU_Count × 3
```

所以0、1、2个GPU时长度分别为5、8、11字节。ESP32 会将超过10000的百分比限制为10000，并拒绝重复或越界的 GPU ID。

### CodexQuota，Type `0x03`，schema v2

Codex payload 固定27字节。这里的 schema version 与通用帧头中的 Version 是两个独立版本号：

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | SchemaVersion | 固定为 `2`；其他版本直接忽略 |
| 1 | 1 | Status | `0=Unavailable`，`1=Valid`，`2=AuthRequired`，`3=CollectorError` |
| 2 | 1 | Flags | bit0=额度有效，bit1=已达到额度限制，其余位保留 |
| 3 | 8 | CollectedAt | PC采集时间，Unix秒，小端 `uint64` |
| 11 | 2 | UsedPercentX100 | 已用百分比×100 |
| 13 | 2 | RemainingPercentX100 | 剩余百分比×100 |
| 15 | 4 | WindowDurationMinutes | 额度窗口分钟数，小端 `uint32`；7天为10080 |
| 19 | 8 | ResetsAt | 下一次重置时间，Unix秒，小端 `uint64` |

当 flags bit0 为0时，偏移11～26的额度字段应全部为0。ESP32 只接受长度恰好为27、schema为2且状态值不大于3的数据包；百分比解包后限制在 `0..10000`。

OLED 使用 `CODEX_TIMEZONE_OFFSET_MINUTES` 将 `ResetsAt` 转为本地时间，默认 `480`，显示格式为：

```text
RST YY/MM/DD HH:MM
```

### Ping/Pong，Type `0x10/0x11`

Ping 和 Pong 的 payload 长度均为0。ESP32 收到 Ping 后立即发送 Pong，并将 Pong 的 Sequence 设置为 Ping 的 Sequence，便于 PC 判断链路是否可用。

## PC 打包步骤

1. 根据业务数据生成 payload；数值做范围限制并按小端写入。
2. 创建 Packet，指定 Type 和 Flags。
3. `SerialCommunicator` 分配 `uint16` Sequence。
4. 写入 `AA 55`、通用帧版本、类型、标志、序号和payload长度。
5. 复制 payload。
6. 对偏移2开始的 `7 + PayloadLength` 字节计算 CRC。
7. 将CRC低字节、高字节写入帧尾。
8. 将完整帧加入异步发送队列；队列满时丢弃本次发送，采集线程不阻塞。

## ESP32 解包步骤

1. UART0任务每20ms等待一次数据，每次最多读取256字节。
2. 字节写入2048字节环形缓冲区；缓冲区满时丢弃最旧字节并累计溢出次数。
3. 从缓冲区搜索连续帧头 `AA 55`，帧头前的无效字节逐个丢弃。
4. 不足9字节固定头时等待更多数据。
5. 校验通用帧版本和payload长度；长度大于512时丢弃一个字节并重新同步。
6. 不足完整帧长度时继续等待。
7. 复制完整候选帧并计算 CRC；CRC错误时丢弃一个字节，重新搜索下一帧头。
8. CRC正确后取出Type、Flags、Sequence和Payload，并从环形缓冲区移除整帧。
9. 根据Type分发给硬件状态、Codex状态或Ping/Pong处理模块；未知Type安全忽略。

这种“错误时只丢弃一个字节再重新搜索帧头”的策略，可以在启动日志、半包、粘包、丢字节或CRC错误后自动恢复同步。

## PC 工程编译

使用 Visual Studio 打开 `PC_Project/PC_monitor.slnx`，选择 `x64`：

- `Debug`：显示控制台日志，适合联调。
- `Release`：无控制台窗口，通过系统托盘运行。

PC 需要安装并登录支持 `account/rateLimits/read` 的 Codex CLI。可执行文件查找失败时，可以设置：

```powershell
$env:CODEX_EXE_PATH = "C:\path\to\codex.exe"
```
