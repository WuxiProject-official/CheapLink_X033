/*
 *  Customized file of DAP
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdint.h>

const uint8_t _AIRCR_RESET_Sequence[22] =
{
// Uses SCB->AIRCR to issue SYSRESETREQ
		0x00,// unused
		0x04, // 4 commands
		0x08, 0x00, 0x00, 0x00, 0x00, // Wdp 0x8 0x00000000, select AP bank 0
		0x01, 0x12, 0x00, 0x00, 0x03, // Wap 0x0 0x03000012, set 32Bit, AddrInc1Word
		0x05, 0x0c, 0xed, 0x00, 0xe0, // Wap 0x4 0xe000ed0c, set target addr 0xe000ed0c
		0x0d, 0x04, 0x00, 0xfa, 0x05 // Wap 0xc 0x05fa0004, write target addr
		};
