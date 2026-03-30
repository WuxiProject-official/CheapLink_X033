# About PIOC

> (Translated) PIOC is a programmable I/O microcontroller, also called eMCU. This eMCU is based on single clock-cycle RISC8B instruction set core and runs at the same frequency as the system. It has a ROM size of 2048 instructions(4K-byte) and 49 SFRs, also with a timer/counter and supports controlling 2 I/O pins.  
> 《可编程协议I/O微控制器PIOC》 Rev1 WCH  

要阅读该文件的简体中文版本，请转到[README.zh-cn.md](README.zh-cn.md)。  

## Features

- RISC8B core which has optimized single clock-cycle instruction set(including bit operations). Fully statically designed and able to run at the same frequency as the system.
- Uses 4K-byte system SRAM as 2048 instructions sized program ROM. Supports halt and dynamically reloading.
- Provides 33 bytes bidirectional registers and 1 byte unidirectional register on each direction. Provides standalone stack with depth 6.
- 8-bit auto-reload TIMER0 which can be used for timeout reset, programmable clock or 8-bit PWM generation.
- Supports 2 GPIOs for communication protocol control and input level change detection.

For more information please refer to official documents in [CH32X035EVT](https://www.wch.cn/downloads/CH32X035EVT_ZIP.html).

## PIOC in this Project

This project currently uses PIOC for:

- SWD_Transfer

Other communication functions are still implemented by MCU using bit-bang.

To enable PIOC based SW-DP acceleration, set global marco `USE_PIOC_ACC` to `1` and **rebuild all**.

## Development of PIOC

PIOC is based on an *ancient* RISC core and its toolchain is cimple. So be careful with limitions declared in manuals.  
The vendor has only provided its toolchain on Windows platform so you may need a VM if you're developing on non-Windows environment.  
Due to license restrictions, this repository does not include the toolchain executable. You may find `WASM53B.EXE` and `BIN_HEX.EXE` in official EVT pack from `CH32X035EVT/EXAM/PIOC/Tool_Manual/Tool` and copy them to `PIOC/` to build PIOC binary.  

After you modified the `PIOC_SW-DP.ASM`, just double-click `PIOC_GEN.BAT` in the same directory and it will automatically call assembler then generate new `PIOC_SW-DP_inc.h` which contains PIOC binary would be included in `dap_pioc.c`.  

Always **rebuild all** after generated new PIOC binary to ensure the new data has been loaded.  
