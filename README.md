# CheapLink_X033

![GitHub Release](https://img.shields.io/github/v/release/WuxiProject-official/CheapLink_X033?include_prereleases&display_name=release&link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%2Freleases%2Flatest)
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/WuxiProject-official/CheapLink_X033/total?link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%2Freleases%2Flatest)
![GitHub License](https://img.shields.io/github/license/WuxiProject-official/CheapLink_X033?link=https%3A%2F%2Fgithub.com%2FWuxiProject-official%2FCheapLink_X033%3Ftab%3Dreadme-ov-file%23license-information)

A cheap CMSIS-DAP debugger implemention.  
要阅读该文件的简体中文版本，请转到[README.zh-cn.md](README.zh-cn.md)。  

## Features

- [x] Cheap hardware
- [x] WinUSB-based bulk transaction
- [x] Auto driver installation (requires Win10 or later)
- [x] SWD debugging
- [x] CDC serial up to 1M baudrate (only supports 8Nx,8Ex,8Ox)
- [x] Faster DAP transaction
- [x] Automatic detection of the PIOC electrical connection, with fallback

## Build configurations and hardware detection

The project ships two build configurations. They differ in `USE_PIOC_ACC` and in the linker script:

| Configuration | `USE_PIOC_ACC` | Linker script | PIOC acceleration |
|---|---:|---|---|
| `obj` | 1 | `Ld/Link_highcode_46k_pioc.ld` (16 KB RAM) | yes |
| `obj-legacy` | 0 | `Ld/Link_highcode_46k.ld` (20 KB RAM) | no |

The `obj` configuration leaves 4 KB less RAM to the application, because the PIOC reuses 4 KB of system SRAM as its own program ROM.

In the bit-bang path the MCU generates the SWD timing on PA5 (SWCLK), PA6 (SWDIO in) and PA7 (SWDIO out); in the PIOC path the PIOC additionally uses PC18 and PC19 to generate it.

Since PC18/PC19 are also the SDI debug port of the CH32X033 itself, the debug port has to be turned off in the following cases:

- With PIOC acceleration the PIOC drives PC18/PC19 directly, so the on-chip debug port would fight the PIOC output.
- On boards that route PC18/PC19 to the SWD nets (that is, boards designed for the PIOC implementation), the on-chip debug interface is electrically connected to the pins that generate the SWD signals, so the SWD timing sent to the target may disturb the operation of the on-chip debug interface.

To work on both kinds of boards without separate firmware images, the firmware probes the electrical connection during startup. PC18/PC19 are configured as inputs with pull-up, PA5/PA7 are driven high and then low, and PC18/PC19 are read back each time. If both of them follow the driven level, the PIOC pins are wired to the SWD nets.

| Configuration | PC18/PC19 wired to SWD | SWD timing | Debug port | Startup indication |
|---|---|---|---|---|
| `obj` | yes | PIOC | off | none, this is the expected behaviour |
| `obj-legacy` | no | MCU bit-bang | on | none, this is the expected behaviour |
| `obj` | no | MCU bit-bang (fallback) | off | red LED warning, 2 s startup delay |
| `obj-legacy` | yes | MCU bit-bang | off | red LED warning, 2 s startup delay |

The two combinations that show the red indication are the mismatched ones: a PIOC build on a board without PIOC wiring, and a legacy build on a board that provides it. Both keep working; the indication only tells you that the configuration does not match the electrical connection of the hardware. In the first case the firmware falls back to bit-bang at runtime (`DAP_PIOC_FallbackFlag`); in the second case the debug port stays off.

## Benchmark (RAM read-write speed test)

### End-to-end throughput

Measured with `swd_ram_bench.py` on an STM32H743/H750/H753 target (`DBGMCU_IDCODE` Dev_ID = `0x450`) using the PIOC-accelerated build configuration (`obj`, `USE_PIOC_ACC=1`); median of 5 valid rounds after one warm-up round. Throughputs are in KiB/s.

| Requested SWCLK | 4 KiB write | 64 KiB write | 64 KiB read | Marginal cost | Asymptotic write |
|---|---|---|---|---|---|
| 2000 kHz | 104.5 | 109.7 | 109.1 | 8.87 µs/B | 110 KiB/s |
| 4000 kHz | 170.2 | 183.6 | 179.9 | 5.29 µs/B | 185 KiB/s |
| 8000 kHz | 196.5 | 214.9 | 208.0 | 4.52 µs/B | 216 KiB/s |
| 16000 kHz | 196.7 | 214.9 | 208.0 | 4.52 µs/B | 216 KiB/s |

Fitting the transfer time against the block size splits the result into a fixed cost of about 1.8\~1.9 ms per `load_image`/`dump_image` command and a per-byte marginal cost. The 4 KiB samples are diluted by that fixed cost, so only the asymptotic rate describes the link itself. The marginal cost stops improving at 8 MHz: beyond that the ceiling is the USB/protocol layer (64-byte CMSIS-DAPv2 packets over full-speed USB) rather than SWCLK, and requesting a higher speed changes nothing.

The clock labels above are the values requested through `adapter speed`, not a measured SWCLK.

### Reproducing the numbers

`swd_ram_bench.py` in this repository drives OpenOCD over its telnet interface with `load_image`/`dump_image`, sweeps SWD clocks and block sizes, and reports read/write throughput together with a read-back verification. The script only needs the Python standard library.

```plain
# Defaults: cmsis-dap + stm32h7x, RAM at 0x24000000, OpenOCD started by the script
PS > python swd_ram_bench.py

# Sweep clocks and block sizes, 5 valid rounds per combination
PS > python swd_ram_bench.py --speed 2000,4000,8000,16000 --sizes 4k,64k --rounds 5

# Attach to an already running OpenOCD instead of spawning one
PS > python swd_ram_bench.py --attach --addr 0x24000000
```

Use `--openocd` or the `OPENOCD` environment variable to point at the OpenOCD executable. `--interface` and `--target` select other OpenOCD configurations; with a custom configuration the target check is skipped, and confirming that the configuration matches the target becomes your job. The script halts the target and overwrites the RAM at the start address, so mind regions such as the stack when resuming.

## License Information

The source code of this project consists of four parts and follows different licenses:

1. **Core logic code** (all files located in the `Tasks/` , `Middleware/` directory, `main. c` , `DAP_custom.c` in the `User/` directory, `PIOC_SW-DP.ASM` in the `PIOC/` directory):
    This part of the code is developed by **WuxiProject** and released under **Mozilla Public License 2.0 (MPL-2.0)**. Please refer to the `LICENSE` document for the full text of the license. According to MPL-2.0, we provide the complete source code for this part of the code.

2. **CMSIS-DAP Implementation Code** (all files located in the `DAP/` directory):
    This part of the code is developed by **ARM** and released under **Apache License 2.0**. Please refer to the `DAP/LICENSE.txt` file for the full license. Due to the necessity of building this firmware, according to Apache License 2.0, we provide the complete source code and its original license and copyright information for this part of the code.

3. **FreeRTOS code** (all files located in the `RTOS/` directory and `FreeRTOSConfig.h` under the `User/` directory):
    This part of the code is developed by **Amazon** and published under **MIT License**. Please refer to the `RTOS/LICENSE.md` file for the full license. Due to the necessity of building this firmware, according to the MIT License, we provide the complete source code and its original license and copyright information for this part of the code.

4. **Proprietary code provided by microcontroller manufacturer** (all files located in the `Core/`, `Debug/`, `Ld/`, `Peripheral/`, `Startup/`, `Vendor/` directories, `PIOC_INC.ASM` under the `PIOC/` directory):
    This part of the code is provided by **WCH** and is subject to its proprietary license. Its main terms include:
    - The code and generated binary files can only be used to run on microcontrollers produced by WCH.
    - The copyright belongs to WCH company and the original copyright statement must be retained.
    Please refer to the beginning of any of the above code files or contact WCH for further details regarding the original license statement.

### About binary file distribution

The precompiled firmware files we provide (`CheapLink_X033.hex` and `CheapLink_X033.bin`) are a combination of the aforementioned code sections. Therefore, the use of this binary file must comply with the license terms of MPL-2.0, Apache License 2.0, MIT License, and WCH simultaneously. **The most important point is that this firmware can only be used to run on microcontrollers produced by WCH.**
