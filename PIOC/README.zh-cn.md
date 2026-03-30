# 关于 PIOC

> PIOC是一个可编程协议I/O微控制器PIOC，即eMCU，该eMCU基于单时钟周期的精简指令集RISC8B内核，运行于系统主频，具有2K指令的程序ROM和49个SFR寄存器及PWM定时/计数器，支持2个I/O引脚的协议控制。  
> 《可编程协议I/O微控制器PIOC》 版本1 WCH  

To read English version of this file，please turn to [README.md](README.md).  

## Features

- RISC8B内核，优化的单周期位操作指令集，全静态设计，支持系统主频。
- 复用了4K字节的系统SRAM作为2K容量的程序ROM，支持程序暂停和动态加载。
- 提供33字节的双向寄存器和单向各1个寄存器，提供6级独立堆栈。
- 8位自动重加载定时器0，用于超时复位、可编程时钟或者8位PWM输出。
- 支持2个通用双向I/O协议控制，支持输入电平变化检测。

更多信息请参见[CH32X035EVT](https://www.wch.cn/downloads/CH32X035EVT_ZIP.html)中的官方文档。

## 本项目中的PIOC

本项目中目前将PIOC用于以下功能:

- SWD_Transfer

其他通信功能仍然由MCU通过bit-bang来实现。  

若要使用PIOC加速的SW-DP通信，请设置全局的宏定义`USE_PIOC_ACC`为`1`，然后**全部重新构建**.

## PIOC的开发

PIOC基于一个较老的RISC内核，其工具链也相对简单，因此请特别注意手册中注明的各项限制。  
厂商目前仅提供 Windows 平台的工具链，如果你在非 Windows 环境下开发，可能需要使用虚拟机。  
受许可证限制，本仓库不包含工具链可执行文件。你可以在官方 EVT 包的`CH32X035EVT/EXAM/PIOC/Tool_Manual/Tool`目录中找到`WASM53B.EXE`和`BIN_HEX.EXE`，并将它们复制到`PIOC/`目录以构建PIOC代码数据。  

修改`PIOC_SW-DP.ASM`后，请双击运行同目录下的`PIOC_GEN.BAT`，脚本会调用汇编器并生成新的`PIOC_SW-DP_inc.h`。该文件包含的PIOC代码数据会被`dap_pioc.c`引用。  

生成新的PIOC代码数据后，请务必执行一次**全部重新构建**，以确保新数据已被正确加载到最终的固件中。
