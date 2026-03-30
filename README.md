# CheapLink_X033

A cheap CMSIS-DAP debugger implemention.
要阅读该文件的简体中文版本，请转到[README.zh-cn.md](README.zh-cn.md)。  

## Features

- [x] Cheap hardware
- [x] WinUSB-based bulk transaction
- [x] Auto driver installation (requires Win10 or later)
- [x] SWD debugging
- [x] CDC serial up to 1M baudrate (only supports 8Nx,8Ex,8Ox)
- [x] Faster DAP transaction

## Benchmark(RAM read-write speed test)

Test is under default project configurations with `USE_PIOC_ACC=1`. Only an STM32G031G6 board was available for testing, so the 32768-byte benchmark fails. The speed was set to 114514 kHz to test the maximum throughput under `fast_clock`. Oscilloscope observation shows the SWD clock is about 4\~5 MHz, which is basically the upper limit of `SWD_Transfer` using PIOC.

```plain
PS > probe-rs benchmark --address 0x20000000 --chip STM32G031G6 --speed 1000
Probe: Probe type CMSIS-DAP, debug interface not specified, target chip STM32G031G6

Test: Speed 1000, Word size 32bit, Data length 4 bytes, Number of iterations 5
Results: Read: 6008.68 bytes/s Std Dev 258.79, Write: 4174358.97 bytes/s Std Dev 2196788.04

Test: Speed 1000, Word size 32bit, Data length 32 bytes, Number of iterations 5
Results: Read: 25403.97 bytes/s Std Dev 1360.03, Write: 25650.45 bytes/s Std Dev 715.89

Test: Speed 1000, Word size 32bit, Data length 128 bytes, Number of iterations 5
Results: Read: 34287.09 bytes/s Std Dev 1285.89, Write: 34905.57 bytes/s Std Dev 1131.74

Test: Speed 1000, Word size 32bit, Data length 2048 bytes, Number of iterations 5
Results: Read: 40811.80 bytes/s Std Dev 497.50, Write: 41419.63 bytes/s Std Dev 192.06

PS > probe-rs benchmark --address 0x20000000 --chip STM32G031G6 --speed 114514
Probe: Probe type CMSIS-DAP, debug interface not specified, target chip STM32G031G6

Test: Speed 114514, Word size 32bit, Data length 4 bytes, Number of iterations 5
Results: Read: 12567.02 bytes/s Std Dev 2168.23, Write: 4054545.45 bytes/s Std Dev 2460964.30

Test: Speed 114514, Word size 32bit, Data length 32 bytes, Number of iterations 5
Results: Read: 48189.81 bytes/s Std Dev 5874.81, Write: 48129.35 bytes/s Std Dev 4168.46

Test: Speed 114514, Word size 32bit, Data length 128 bytes, Number of iterations 5
Results: Read: 68092.67 bytes/s Std Dev 3397.02, Write: 76607.80 bytes/s Std Dev 2407.47

Test: Speed 114514, Word size 32bit, Data length 2048 bytes, Number of iterations 5
Results: Read: 92921.88 bytes/s Std Dev 2902.67, Write: 102276.04 bytes/s Std Dev 2628.99
```

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
