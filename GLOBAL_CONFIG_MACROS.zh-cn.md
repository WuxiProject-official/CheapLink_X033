# CheapLink 配置性全局宏定义说明

> 本文档使用LLM协助编撰。

本文档汇总本工程中具备"配置属性"的全局宏定义（开关、容量、频率、协议行为等），用于快速查阅和统一配置。

- 纳入: 会影响功能开关、协议行为、性能、资源占用、调度参数、设备枚举信息的宏。
- 不纳入: 单纯头文件保护宏、寄存器位掩码常量、协议命令 ID 常量、编译器关键字适配宏。

## 1. 工程级全局编译宏

来源:

- `.vscode/c_cpp_properties.json`
- `obj/*/subdir.mk` (编译命令行 `-D...`)

工程提供两个构建配置，除 `USE_PIOC_ACC` 外，二者使用的链接脚本也不同：

| 构建配置 | 产物目录 | `USE_PIOC_ACC` | 链接脚本 / RAM | 是否使用 PIOC 加速 |
|---|---|---:|---|---|
| `obj` | `obj/` | `1` | `Ld/Link_highcode_46k_pioc.ld`，RAM 16K | 是 |
| `obj-legacy` | `obj-legacy/` | `0` | `Ld/Link_highcode_46k.ld`，RAM 20K | 否 |

除上表两项外，两个配置的编译选项（`-O3`、LTO、`gnu99` 等）与预定义宏完全相同。`obj` 的 RAM 区域少 4K，是因为 PIOC 复用 4K 系统 SRAM 作为自己的程序 ROM，这部分必须从链接脚本的 RAM 区域中排除。

固件在启动时会把 PC18/PC19 配成上拉输入、把 PA5/PA7 先拉高再拉低，据此判断 PC18/PC19 是否与 SWD 网络电气相连，再决定 SWD 时序由谁产生。PC18/PC19 既是 PIOC 的两个 I/O 引脚，也是 CH32X033 自身的 SDI 调试口，因此两个配置都会先关闭片上调试口；只有 `obj-legacy` 在检测到 PC18/PC19 未接 SWD 时才会重新打开它。检测结果与硬件的对应关系见 `README.zh-cn.md` 的「构建配置与硬件检测」。

| 宏名 | 当前值 | 作用 | 典型取值 |
|---|---:|---|---|
| `MSOS_DESC` | `2` | 选择 Microsoft OS Descriptor 版本，影响 WinUSB/兼容性描述符路径 | `0`: 不启用; `1`: MS OS 1.0; `2`: MS OS 2.0 |
| `DAP_WITH_CDC` | `1` | 是否启用 CDC 串口复合设备功能 | `0`/`1` |
| `USE_MDK_RESET_WORKAROUND` | `1` | 启用针对 MDK 的目标复位兼容性处理 | `0`/`1` |
| `USE_PIOC_ACC` | `1`（`obj`）/ `0`（`obj-legacy`） | 启用 PIOC 加速 SW-DP 传输路径，提升高频 SWD 吞吐 | `0`/`1` |

注: `USE_PIOC_ACC` 未在工程配置中定义时，`DAP/DAP_config.h` 按 `0`（关闭）处理，避免单独编译某个源文件时误启用 PIOC 路径。

## 2. CMSIS-DAP 配置宏

来源: `DAP/DAP_config.h`

### 2.1 调试端口与时钟

| 宏名 | 当前值 | 说明 |
|---|---:|---|
| `CPU_CLOCK` | `48000000U` | DAP 固件运行 CPU 频率，用于计算时序 |
| `IO_PORT_WRITE_CYCLES` | `2U` | GPIO 写周期估计，影响 SWD/JTAG 实际时钟计算 |
| `TIMESTAMP_CLOCK` | `1000000U` | 测试域计时器时钟 (Hz)，决定 DAP 时间戳能力与分辨率，`0` 表示不支持时间戳 |
| `SWD_IO_DRV_SPEED` | `GPIO_Speed_50MHz` | SWD 引脚驱动档位。CH32X035 仅提供 50MHz，保留为宏以便派生设计调整 |
| `DAP_SWD` | `1` | 启用 SWD |
| `DAP_DEFAULT_PORT` | `1U` | 默认端口，`1`=SWD, `2`=JTAG |
| `DAP_DEFAULT_SWJ_CLOCK` | `1000000U` | 默认 SWJ 时钟 (Hz) |

注: `DELAY_SLOW_CYCLES`（默认 `8U`，慢速延时每次循环的周期数）与 `DELAY_FAST_CYCLES`（默认 `1U`，快速延时周期数，取值 0~3）位于 `DAP/DAP.h`，用于计算 MCU bit-bang 路径的时钟。

### 2.2 数据包与缓冲

| 宏名 | 当前值 | 说明 |
|---|---:|---|
| `DAP_PACKET_SIZE` | `64U` | DAP 报文大小（字节） |
| `DAP_PACKET_COUNT` | `20U` (`obj`) | DAP 报文缓冲个数，随加速模式变化，受到可用 RAM 大小限制 |

## 3. PIOC 加速配置宏

来源: `PIOC/dap_pioc.h`

| 宏名 | 当前值 | 说明 |
|---|---:|---|
| `PIOC_DEADLOCK_TIMEOUT_MS` | `1000U` | 等待 PIOC 完成一次 SWD 传输的上限（毫秒）。超出即判定为死锁，复位并重启 PIOC 核，返回错误 ACK 供主机恢复 |

说明:

- 超时判定基于 FreeRTOS 节拍（`xTaskGetTickCount()`），实际精度取决于 `configTICK_RATE_HZ`（本工程为 1000，即 1ms/tick）。
- 本节内容仅在 `USE_PIOC_ACC=1` 时参与编译。

## 4. USB 与中间件配置宏

### 4.1 USB 描述符与端点参数

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

### 4.2 USB 队列参数

来源: `Middleware/usbqueue.h`

| 宏名 | 当前值 | 说明 |
|---|---|---|
| `UQ_QUEUELEN` | `20` | USB 队列长度 |
| `UQ_PACKLEN_MAX` | `DEF_USBD_FS_PACK_SIZE` | 单包最大处理长度 |

## 5. 调试输出配置宏

来源: `Debug/debug.h`

| 宏名 | 当前值/默认值 | 说明 |
|---|---|---|
| `DEBUG` | 默认未定义 | 打开后启用 `PRINT(...)` 输出并选择 UART |
| `SDI_PRINT` | 默认 `SDI_PR_CLOSE` | SDI 输出开关 |

## 快速修改建议

- 变更工程级全局编译宏（如 `USE_PIOC_ACC`, `DAP_WITH_CDC`, `MSOS_DESC`）需要在项目级别修改，并全量重编译。
- 切换 `USE_PIOC_ACC` 建议直接切换构建配置（`obj` / `obj-legacy`），切换后同样需要全量重编译。
- 变更 USB 枚举信息改 `Middleware/usb_desc.h`。
- 变更 DAP 能力矩阵改 `DAP/DAP_config.h`。
- 变更 SWD 时钟档位相关参数：`CPU_CLOCK`、`SWD_IO_DRV_SPEED` 在 `DAP/DAP_config.h`，`DELAY_SLOW_CYCLES`、`DELAY_FAST_CYCLES` 在 `DAP/DAP.h`。
- 变更 PIOC 死锁超时改 `PIOC/dap_pioc.h`。
