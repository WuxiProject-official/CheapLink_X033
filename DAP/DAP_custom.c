/*
 *  Customized file of DAP
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
