# CheapLink_X033

一个廉价的CMSIS-DAP调试器实现。
To read English version of this file，please turn to [README.md](README.md)。  

## 特性

- [x] 低成本硬件
- [x] 基于WinUSB的快速批量传输
- [x] 无需手动安装驱动（Win10或更高版本）
- [x] SWD调试协议
- [x] 高达1M波特率的CDC串口（仅支持8位数据位下无校验、奇校验、偶校验）
- [ ] 更快速的DAP传输

## 许可证信息 (License Information)

本项目的源代码由四部分组成，并遵循不同的许可证：

1. **核心业务逻辑代码** (位于 `Tasks/` 、 `Middleware/` 目录、位于 `User/` 目录下的 `main.c` 、 `DAP_custom.c`):
    这部分代码由 **WuxiProject 乌西** 开发，基于 **Mozilla Public License 2.0 (MPL-2.0)** 发布。许可证全文请见 `LICENSE` 文件。根据MPL-2.0，我们提供这部分代码的完整源码。

2. **CMSIS-DAP实现代码** (位于 `DAP/` 目录):
    这部分代码由 **ARM** 开发，基于 **Apache License 2.0** 发布。许可证全文请见 `DAP/LICENSE.txt` 文件。由于构建此固件的必要性，根据Apache License 2.0，我们提供这部分代码的完整源码及其原始许可与版权信息。

3. **FreeRTOS代码** (位于 `RTOS/` 目录、位于 `User/` 目录下的 `FreeRTOSConfig.h`):
    这部分代码由 **Amazon** 开发，基于 **MIT License** 发布。许可证全文请见 `RTOS/LICENSE.md` 文件。由于构建此固件的必要性，根据MIT License，我们提供这部分代码的完整源码及其原始许可与版权信息。

4. **微控制器厂商提供的代码** (位于 `Core/` 、 `Debug/` 、 `Ld/` 、 `Peripheral/` 、 `Startup/` 、 `Vendor/` 目录):
    这部分代码由 **WCH** 提供，并受其专有许可证约束。其主要条款包括：
    - 代码及生成的二进制文件仅可用于在 WCH 生产的微控制器上运行。
    - 版权归属于 WCH 公司，必须保留原始的版权声明。
    原始许可证声明请参考任一上述代码文件的开头，或与 WCH 联系以进一步确认详情。

### 关于二进制文件分发

我们提供的预编译固件文件 (`CheapLink_X033.hex` 和 `CheapLink_X033.bin`) 是上述几部分代码的组合。因此，使用此二进制文件必须同时遵守MPL-2.0、Apache License 2.0、MIT License和WCH的许可证条款。最重要的一点是，**此固件仅可用于在WCH生产的微控制器上运行**。
