/*
 *  USB Queue Middleware
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

#ifndef MIDWARE_USBQUEUE_H_
#define MIDWARE_USBQUEUE_H_

// 这个版本的USBQueue已经修改过。This version of USBQueue library has been modified.

#include "ch32x035.h"
#include <stdint.h>
#include <stddef.h>

// Portable configuration
// - You need to implement things below to integrate into your program.
// ##PORT_CONFIGURATION_BEGIN##

/*
 * UQ_QUEUELEN: Defines the queue length
 */
#define UQ_QUEUELEN 8

/*
 * UQ_PACKLEN_MAX: Defines the max length of USB packet received
 */
#define UQ_PACKLEN_MAX DEF_USBD_FS_PACK_SIZE

// ##PORT_CONFIGURATION_END##

extern void USBQueue_EpOUT_Handler (uint8_t len);
extern void USBQueue_EpIN_Handler();
extern void USBQueue_StatusReset();
extern uint8_t USBQueue_DoProcess();

#endif /* MIDWARE_USBQUEUE_H_ */
