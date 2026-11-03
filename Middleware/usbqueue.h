/*
 *  USB Queue Middleware
 *  Copyright (C) 2022-2026  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
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
#define UQ_QUEUELEN 20

/*
 * UQ_PACKLEN_MAX: Defines the max length of USB packet received
 */
#define UQ_PACKLEN_MAX DEF_USBD_FS_PACK_SIZE

// ##PORT_CONFIGURATION_END##

#include "FreeRTOS.h"
extern void USBQueue_EpOUT_Handler (uint8_t len,BaseType_t *taskWoken);
extern void USBQueue_EpIN_Handler();
/* USBQueue_StatusReset: resets queue indices/flags only (safe from USB ISR).
 * Does NOT clear the data buffers and does NOT re-arm the OUT endpoint. */
extern void USBQueue_StatusReset();
/* USBQueue_Purge: clears the buffers, then calls USBQueue_StatusReset() and
 * re-arms the OUT endpoint.  Only call from task context (e.g. startup). */
extern void USBQueue_Purge();
/* Called by the USB device stack from SET/CLEAR_FEATURE(ENDPOINT_HALT).
 * halted: 0 = CLEAR_FEATURE, non-zero = SET_FEATURE.
 * ep:     endpoint address, DEF_UEPn | DEF_UEP_IN / DEF_UEP_OUT. */
extern void USBQueue_EpHalt_Handler (uint8_t halted, uint8_t ep);
extern uint8_t USBQueue_DoProcess();

#endif /* MIDWARE_USBQUEUE_H_ */
