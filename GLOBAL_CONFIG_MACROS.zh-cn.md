# CheapLink 配置性全局宏定义说明

> 本文档使用LLM协助编撰。

本文档汇总本工程中具备"配置属性"的全局宏定义（开关、容量、频率、协议行为等），用于快速查阅和统一配置。

- 纳入: 会影响功能开关、协议行为、性能、资源占用、调度参数、设备枚举信息的宏。
- 不纳入: 单纯头文件保护宏、寄存器位掩码常量、协议命令 ID 常量、编译器关键字适配宏。

## 1. 工程级全局编译宏

来源:

- `.vscode/c_cpp_properties.json`
- `obj/*/subdir.mk` (编译命令行 `-D...`)

| 宏名 | 当前值 | 作用 | 典型取值 |
|---|---:|---|---|
| `MSOS_DESC` | `2` | 选择 Microsoft OS Descriptor 版本，影响 WinUSB/兼容性描述符路径 | `0`: 不启用; `1`: MS OS 1.0; `2`: MS OS 2.0 |
| `DAP_WITH_CDC` | `1` | 是否启用 CDC 串口复合设备功能 | `0`/`1` |
| `USE_MDK_RESET_WORKAROUND` | `1` | 启用针对 MDK 的目标复位兼容性处理 | `0`/`1` |
| `USE_PIOC_ACC` | `1` | 启用 PIOC 加速 SW-DP 传输路径，提升高频 SWD 吞吐 | `0`/`1` |

## 2. CMSIS-DAP 配置宏

来源: `DAP/DAP_config.h`

### 2.1 调试端口与时钟

| 宏名 | 当前值 | 说明 |
|---|---:|---|
| `CPU_CLOCK` | `48000000U` | DAP 固件运行 CPU 频率，用于计算时序 |
| `IO_PORT_WRITE_CYCLES` | `2U` | GPIO 写周期估计，影响 SWD/JTAG 实际时钟计算 |
| `DAP_SWD` | `1` | 启用 SWD |
| `DAP_DEFAULT_PORT` | `1U` | 默认端口，`1`=SWD, `2`=JTAG |
| `DAP_DEFAULT_SWJ_CLOCK` | `1000000U` | 默认 SWJ 时钟 (Hz) |

### 2.2 数据包与缓冲

| 宏名 | 当前值 | 说明 |
|---|---:|---|
| `DAP_PACKET_SIZE` | `64U` | DAP 报文大小（字节） |
| `DAP_PACKET_COUNT` | `20U` | DAP 报文缓冲个数 |

## 3. USB 与中间件配置宏

### 3.1 USB 描述符与端点参数

来源: `Middleware/usb_desc.h`

| 宏名 | 当前值 | 说明 |
|---|---|---|
| `DEF_FILE_VERSION` | `0x01` | 固件/描述符版本号 |
| `DEF_USB_VID` | `0x0D28` | USB VID |
| `DEF_USB_PID` | `0x0204` | USB PID |
| `DEF_IC_PRG_VER` | `DEF_FILE_VERSION` | 设备版本字段 |
| `DEF_USBD_UEP0_SIZE` | `64` | EP0 包长 |
| `DEF_USBD_FS_PACK_SIZE` | `64` | 全速 FS 最大包长 |
| `DEF_USBD_ENDP1_SIZE` ~ `DEF_USBD_ENDP7_SIZE` | `DEF_USBD_FS_PACK_SIZE` | 各端点包长配置 |

注: `DEF_USBD_*_DESC_LEN` 系列宏主要用于根据描述符数组计算长度，属于描述符组织参数。

### 3.2 USB 队列参数

来源: `Middleware/usbqueue.h`

| 宏名 | 当前值 | 说明 |
|---|---|---|
| `UQ_QUEUELEN` | `20` (`USE_PIOC_ACC=1`) / `12` (`USE_PIOC_ACC=0`) | USB 队列长度，随加速模式变化 |
| `UQ_PACKLEN_MAX` | `DEF_USBD_FS_PACK_SIZE` | 单包最大处理长度 |

## 4. 调试输出配置宏

来源: `Debug/debug.h`

| 宏名 | 当前值/默认值 | 说明 |
|---|---|---|
| `DEBUG` | 默认未定义 | 打开后启用 `PRINT(...)` 输出并选择 UART |
| `SDI_PRINT` | 默认 `SDI_PR_CLOSE` | SDI 输出开关 |

## 快速修改建议

- 变更工程级全局编译宏（如 `USE_PIOC_ACC`, `DAP_WITH_CDC`, `MSOS_DESC`）需要在项目级别修改，并全量重编译。
- 变更 USB 枚举信息改 `Middleware/usb_desc.h`。
- 变更 DAP 能力矩阵改 `DAP/DAP_config.h`。
