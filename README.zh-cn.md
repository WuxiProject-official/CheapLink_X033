# CheapLink_X033

![GitHub Release](https://img.shields.io/github/v/release/WuxiProject-official/CheapLink_X033?include_prereleases&display_name=release&link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%2Freleases%2Flatest)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/WuxiProject-official/CheapLink_X033/total?link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%2Freleases%2Flatest)
![GitHub License](https://img.shields.io/github/license/WuxiProject-official/CheapLink_X033?link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%2Fblob%2Fmain%2FREADME.zh-cn.md%23%25E8%25AE%25B8%25E5%258F%25AF%25E8%25AF%2581%25E4%25BF%25A1%25E6%2581%25AF-license-information)

一个廉价的CMSIS-DAP调试器实现。  
To read English version of this file，please turn to [README.md](README.md).  

## 特性

- [x] 低成本硬件
- [x] 基于WinUSB的快速批量传输
- [x] 无需手动安装驱动（Win10或更高版本）
- [x] SWD调试协议
- [x] 高达1M波特率的CDC串口（仅支持8位数据位下无校验、奇校验、偶校验）
- [x] 更快速的DAP传输
- [x] 自动检测 PIOC 电气连接并回退

## 构建配置与硬件检测

工程提供两个构建配置，差别在 `USE_PIOC_ACC` 与链接脚本：

| 构建配置 | `USE_PIOC_ACC` | 链接脚本 | 是否使用 PIOC 加速 |
|---|---:|---|---|
| `obj` | 1 | `Ld/Link_highcode_46k_pioc.ld`（16KB RAM） | 是 |
| `obj-legacy` | 0 | `Ld/Link_highcode_46k.ld`（20KB RAM） | 否 |

`obj` 少 4KB RAM，是因为 PIOC 复用了 4KB 系统 SRAM 作为自己的程序 ROM。

bit-bang 路径下由 MCU 在 PA5（SWCLK）、PA6（SWDIO 输入）、PA7（SWDIO 输出）上产生 SWD 时序；PIOC 路径下 PIOC 还会使用 PC18、PC19 产生。

由于 PC18/PC19 同时也是 CH32X033 自身的 SDI 调试口，以下情况必须关闭调试口：

- 启用 PIOC 加速时，PC18/PC19 由 PIOC 直接驱动，片上调试口会与 PIOC 的输出相冲突；
- 在把 PC18/PC19 接到 SWD 网络的板子上（即按照 PIOC 实现设计的板子），片上调试接口与产生 SWD 信号的引脚电气相连，发往目标的 SWD 时序可能会干扰片上调试接口的运行。

为了让同一份固件适配两类板子，固件在启动时检测电气连接：把 PC18/PC19 配成上拉输入，把 PA5/PA7 先拉高再拉低，每次读回 PC18/PC19。若两者都跟随驱动电平，说明 PIOC 引脚确实接到了 SWD 网络上。

| 构建配置 | PC18/PC19 是否接到 SWD | SWD 时序 | 调试口 | 启动指示 |
|---|---|---|---|---|
| `obj` | 是 | PIOC | 关闭 | 无，这是预期内行为 |
| `obj-legacy` | 否 | MCU bit-bang | 开启 | 无，这是预期内行为 |
| `obj` | 否 | MCU bit-bang（回退） | 关闭 | 红色 LED 警告 + 2 秒启动延时 |
| `obj-legacy` | 是 | MCU bit-bang | 关闭 | 红色 LED 警告 + 2 秒启动延时 |

两种出现红色指示的组合都属于「构建配置与硬件不匹配」：PIOC 固件烧到了没有 PIOC 走线的板子，以及 legacy 固件烧到了提供该走线的板子。两种情况下固件仍可正常工作，指示只是提示配置与硬件电气连接不一致。前一种情况下固件会在运行期回退到 bit-bang（`DAP_PIOC_FallbackFlag`）；后一种情况下调试口保持关闭。

## 性能测试（RAM读写测速）

### 端到端吞吐

使用仓库内的 `swd_ram_bench.py`，目标为 STM32H743/H750/H753（`DBGMCU_IDCODE` Dev_ID = `0x450`），启用 PIOC 加速的构建配置（`obj`，`USE_PIOC_ACC=1`），取预热 1 轮后 5 个有效轮次的中位数。吞吐单位为 KiB/s。

| 请求 SWCLK | 4KiB 写 | 64KiB 写 | 64KiB 读 | 边际成本 | 渐进写速率 |
|---|---|---|---|---|---|
| 2000kHz | 104.5 | 109.7 | 109.1 | 8.87 µs/B | 110 KiB/s |
| 4000kHz | 170.2 | 183.6 | 179.9 | 5.29 µs/B | 185 KiB/s |
| 8000kHz | 196.5 | 214.9 | 208.0 | 4.52 µs/B | 216 KiB/s |
| 16000kHz | 196.7 | 214.9 | 208.0 | 4.52 µs/B | 216 KiB/s |

把传输耗时对块大小做线性拟合，可分离出每条 `load_image`/`dump_image` 命令约 1.8\~1.9ms 的固定开销与每字节的边际成本。4KiB 样本被固定开销严重稀释，只有渐进速率才反映链路本身的能力。边际成本在 8MHz 之后不再改善：再往上限制来自 USB/协议层（全速 USB 上的 64 字节 CMSIS-DAPv2 包），而不是 SWCLK，继续提高请求频率不会改变结果。

表中的时钟是经 `adapter speed` 下发的请求值，并非实测 SWCLK。

### 复现方法

仓库内的 `swd_ram_bench.py` 通过 OpenOCD 的 telnet 接口执行 `load_image`/`dump_image`，逐档扫描 SWD 时钟与块大小，输出读写吞吐率并读回校验数据。该脚本仅依赖 Python 标准库。

```plain
# 默认：cmsis-dap + stm32h7x，RAM 位于 0x24000000，由脚本自行拉起 OpenOCD
PS > python swd_ram_bench.py

# 扫描时钟与块大小，每个组合取 5 个有效轮次
PS > python swd_ram_bench.py --speed 2000,4000,8000,16000 --sizes 4k,64k --rounds 5

# 连接已手动启动的 OpenOCD，不自行拉起
PS > python swd_ram_bench.py --attach --addr 0x24000000
```

用 `--openocd` 或环境变量 `OPENOCD` 指定 OpenOCD 可执行文件路径。`--interface` 与 `--target` 可改用其它 OpenOCD 配置；改用自定义配置时目标检测会被跳过，配置与目标是否匹配需自行确认。测速前脚本会自动 halt 目标，并覆盖起始地址处的 RAM 内容，恢复运行时请留意栈等区域。

## 许可证信息 (License Information)

本项目的源代码由四部分组成，并遵循不同的许可证：

1. **核心业务逻辑代码** (位于 `Tasks/` 、 `Middleware/` 目录、位于 `User/` 目录下的 `main.c` 、 `DAP_custom.c`、位于 `PIOC/` 目录下的 `PIOC_SW-DP.ASM`):
    这部分代码由 **WuxiProject 乌西** 开发，基于 **Mozilla Public License 2.0 (MPL-2.0)** 发布。许可证全文请见 `LICENSE` 文件。根据MPL-2.0，我们提供这部分代码的完整源码。

2. **CMSIS-DAP实现代码** (位于 `DAP/` 目录):
    这部分代码由 **ARM** 开发，基于 **Apache License 2.0** 发布。许可证全文请见 `DAP/LICENSE.txt` 文件。由于构建此固件的必要性，根据Apache License 2.0，我们提供这部分代码的完整源码及其原始许可与版权信息。

3. **FreeRTOS代码** (位于 `RTOS/` 目录、位于 `User/` 目录下的 `FreeRTOSConfig.h`):
    这部分代码由 **Amazon** 开发，基于 **MIT License** 发布。许可证全文请见 `RTOS/LICENSE.md` 文件。由于构建此固件的必要性，根据MIT License，我们提供这部分代码的完整源码及其原始许可与版权信息。

4. **微控制器厂商提供的代码** (位于 `Core/` 、 `Debug/` 、 `Ld/` 、 `Peripheral/` 、 `Startup/` 、 `Vendor/` 目录、位于 `PIOC/` 目录下的 `PIOC_INC.ASM`):
    这部分代码由 **WCH** 提供，并受其专有许可证约束。其主要条款包括：
    - 代码及生成的二进制文件仅可用于在 WCH 生产的微控制器上运行。
    - 版权归属于 WCH 公司，必须保留原始的版权声明。
    原始许可证声明请参考任一上述代码文件的开头，或与 WCH 联系以进一步确认详情。

### 关于二进制文件分发

我们提供的预编译固件文件 (`CheapLink_X033.hex` 和 `CheapLink_X033.bin`) 是上述几部分代码的组合。因此，使用此二进制文件必须同时遵守MPL-2.0、Apache License 2.0、MIT License和WCH的许可证条款。最重要的一点是，**此固件仅可用于在WCH生产的微控制器上运行**。
