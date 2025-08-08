# CheapLink_X033

A cheap CMSIS-DAP debugger implemention.
要阅读该文件的简体中文版本，请转到[README.zh-cn.md](README.zh-cn.md)。  

## Features

- [x] Cheap hardware
- [x] WinUSB-based bulk transaction
- [x] Auto driver installation (requires Win10 or later)
- [x] SWD debugging
- [x] CDC serial up to 1M baudrate (only supports 8Nx,8Ex,8Ox)
- [ ] Faster DAP transaction

## License Information

The source code of this project consists of four parts and follows different licenses:

1. **Core logic code** (all files located in the `Tasks/` , `Middleware/` directory, `main. c` , `DAP_custom.c` in the `User/` directory):
    This part of the code is developed by **WuxiProject** and released under **Mozilla Public License 2.0 (MPL-2.0)**. Please refer to the `LICENSE` document for the full text of the license. According to MPL-2.0, we provide the complete source code for this part of the code.

2. **CMSIS-DAP Implementation Code** (all files located in the `DAP/` directory):
    This part of the code is developed by **ARM** and released under **Apache License 2.0**. Please refer to the `DAP/LICENSE.txt` file for the full license. Due to the necessity of building this firmware, according to Apache License 2.0, we provide the complete source code and its original license and copyright information for this part of the code.

3. **FreeRTOS code** (all files located in the `RTOS/` directory and `FreeRTOSConfig.h` under the `User/` directory):
    This part of the code is developed by **Amazon** and published under **MIT License**. Please refer to the `RTOS/LICENSE.md` file for the full license. Due to the necessity of building this firmware, according to the MIT License, we provide the complete source code and its original license and copyright information for this part of the code.

4. **Proprietary code provided by microcontroller manufacturer** (all files located in the `Core/`, `Debug/`, `Ld/`, `Peripheral/`, `Startup/`, `Vendor/` directories):
    This part of the code is provided by **WCH** and is subject to its proprietary license. Its main terms include:
    - The code and generated binary files can only be used to run on microcontrollers produced by WCH.
    - The copyright belongs to WCH company and the original copyright statement must be retained.
    Please refer to the beginning of any of the above code files or contact WCH for further details regarding the original license statement.

### About binary file distribution

The precompiled firmware files we provide (`CheapLink_X033.hex` and `CheapLink_X033.bin`) are a combination of the aforementioned code sections. Therefore, the use of this binary file must comply with the license terms of MPL-2.0, Apache License 2.0, MIT License, and WCH simultaneously. **The most important point is that this firmware can only be used to run on microcontrollers produced by WCH.**
