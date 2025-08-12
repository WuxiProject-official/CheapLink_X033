/*
 *  USB Queue Middleware
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <usbqueue.h>  // 这个版本的USBQueue已经修改过。This version of USBQueue library has been modified.

// Portable implemention
// - You need to implement things below to integrate into your program.
// ##PORT_IMPLEMENTION_BEGIN##

#include <ch32x035_usbfs_device.h>

/*
 * UQ_EP_UP: Defines the endpoint to upload data
 */
#define UQ_EP_UP DEF_UEP2

/*
 * UQ_EP_DN: Defines the endpoint to receive data
 */
#define UQ_EP_DN DEF_UEP1

extern volatile uint8_t USBFS_Endp_Busy[];

// Prepare data for IN endpoint upload.
uint8_t USBQueue_EPUpload (uint8_t *buf, uint16_t len) {
    while (USBFS_Endp_Busy[UQ_EP_UP]) {
        ;
    }
    return USBFS_Endp_DataUp (UQ_EP_UP, buf, len, DEF_UEP_DMA_LOAD);
}

// Prepare buffer for OUT transaction.
void USBQueue_SetEPDNAddr (volatile uint8_t *buffer) {
    USBFSD->UEP1_DMA = (uint32_t)buffer;
}

// Control the OUT(downstream) endpoint ACK status.
void USBQueue_SetEPDNAck (FunctionalState state) {
    if (DISABLE == state) {
        // NAK
        USBFSD->UEP1_CTRL_H &= ~USBFS_UEP_R_RES_MASK;
        USBFSD->UEP1_CTRL_H |= USBFS_UEP_R_RES_NAK;
    } else {
        // ACK
        USBFSD->UEP1_CTRL_H &= ~USBFS_UEP_R_RES_MASK;
        USBFSD->UEP1_CTRL_H |= USBFS_UEP_R_RES_ACK;
    }
}

// ##PORT_IMPLEMENTION_END##

#include "DAP.h"

void memset_v (volatile void *p, int val, size_t len) {
    uint8_t val8 = val & 0xff;
    volatile uint8_t *ptr8 = (volatile uint8_t *)p;
    if (len < 8) {
        for (int i = 0; i < len; i++)
            ptr8[i] = val8;
    } else {
        uint32_t destAddr = (uint32_t)p;
        int start_offset = destAddr & 0x00000003UL;
        if (start_offset) {
            // addr not aligned
            len -= 4 - start_offset;
            for (int i = 0; i < 4 - start_offset; i++)
                ptr8[i] = val8;
        }
        // start from a word-aligned addr
        volatile uint32_t *ptr32 = (volatile uint32_t *)((destAddr & 0xfffffffcUL) + (start_offset ? 4 : 0));
        uint32_t val32 = (val8 << 24) | (val8 << 16) | (val8 << 8) | (val8);
        for (int i = 0; i < (len >> 2); i++)
            ptr32[i] = val32;
        // handle tail bytes
        size_t bytesLeft = len & 0x00000003UL;
        if (bytesLeft) {
            ptr8 = (volatile uint8_t *)(ptr32 + (len >> 2));
            for (int i = 0; i < bytesLeft; i++)
                ptr8[i] = val8;
        }
    }
}

#define MEMCLEAR(x) (memset ((x), 0x00, sizeof ((x))))

volatile uint8_t UQ_InQueue[UQ_QUEUELEN][UQ_PACKLEN_MAX] __attribute__ ((aligned (4)));
volatile uint8_t UQ_OutQueue[UQ_QUEUELEN][UQ_PACKLEN_MAX] __attribute__ ((aligned (4)));
static volatile uint8_t UQ_InLen[UQ_QUEUELEN], UQ_OutLen[UQ_QUEUELEN];
static volatile uint8_t UQ_InPtrIn = 0, UQ_InPtrOut = 0;
static volatile uint8_t UQ_InCntIn = 0, UQ_InCntOut = 0;
static volatile uint8_t UQ_OutPtrIn = 0, UQ_OutPtrOut = 0;
static volatile uint8_t UQ_OutCntIn = 0, UQ_OutCntOut = 0;
static volatile uint8_t UQ_InIdle = 1, UQ_OutIdle = 1;

void USBQueue_StatusReset() {
    MEMCLEAR (UQ_InQueue);
    MEMCLEAR (UQ_OutQueue);
    MEMCLEAR (UQ_InLen);
    MEMCLEAR (UQ_OutLen);
    UQ_InPtrIn = 0, UQ_InPtrOut = 0;
    UQ_InCntIn = 0, UQ_InCntOut = 0;
    UQ_OutPtrIn = 0, UQ_OutPtrOut = 0;
    UQ_OutCntIn = 0, UQ_OutCntOut = 0;
    UQ_InIdle = 1, UQ_OutIdle = 1;
    USBQueue_SetEPDNAddr (UQ_InQueue[UQ_InPtrIn]);
    USBQueue_SetEPDNAck (ENABLE);
    // If other reset operation required, process below.
}

#include "FreeRTOS.h"
#include "task.h"
extern TaskHandle_t taskHandleDAP;

void USBQueue_EpOUT_Handler (uint8_t len,BaseType_t *taskWoken) {
    if (UQ_InQueue[UQ_InPtrIn][0] == ID_DAP_TransferAbort) {
        DAP_TransferAbort = 1U;
    } else {
        UQ_InLen[UQ_InPtrIn] = len;
        UQ_InPtrIn++;
        if (UQ_InPtrIn >= UQ_QUEUELEN)  // loopback
        {
            UQ_InPtrIn = 0;
        }
        UQ_InCntIn++;
        xTaskNotifyFromISR (taskHandleDAP, 0x01, eSetBits, taskWoken);
    }
    if ((uint8_t)(UQ_InCntIn - UQ_InCntOut) != UQ_QUEUELEN) {
        USBQueue_SetEPDNAddr (UQ_InQueue[UQ_InPtrIn]);
        USBQueue_SetEPDNAck (ENABLE);
    } else {
        USBQueue_SetEPDNAck (DISABLE);
        UQ_InIdle = 1;
    }
}

void USBQueue_EpIN_Handler() {
    if (UQ_OutCntIn != UQ_OutCntOut) {
        // left packets in queue
        USBQueue_EPUpload (UQ_OutQueue[UQ_OutPtrOut], UQ_OutLen[UQ_OutPtrOut]);
        UQ_OutPtrOut++;
        if (UQ_OutPtrOut >= UQ_QUEUELEN)  // loopback
        {
            UQ_OutPtrOut = 0U;
        }
        UQ_OutCntOut++;
    } else {
        UQ_OutIdle = 1;
    }
}

uint8_t USBQueue_DoProcess() {
    uint8_t n, flagProcessed = 0;
    uint32_t taskFlag = 0;
    while (UQ_InCntIn != UQ_InCntOut) {  // Unhandled request in queue
        // Prepare DAP cmds
        n = UQ_InPtrOut;
        while (UQ_InQueue[n][0] == ID_DAP_QueueCommands) {
            UQ_InQueue[n][0] = ID_DAP_ExecuteCommands;
            n++;
            if (n == UQ_QUEUELEN) {
                // reached end
                n = 0U;
            }
            if (n == UQ_InPtrIn) {
                // queue full
                xTaskNotifyWait (0x0, 0x0000ffffUL, (uint32_t *)&taskFlag, portMAX_DELAY);
                if (taskFlag & 0x80U) {
                    // drop remained commands
                    break;
                }
            }
        }
        // Start to process
        UQ_OutLen[UQ_OutPtrIn] = (uint8_t)DAP_ExecuteCommand (UQ_InQueue[UQ_InPtrOut], UQ_OutQueue[UQ_OutPtrIn]);
        UQ_InPtrOut++;
        if (UQ_InPtrOut == UQ_QUEUELEN)  // loopback
        {
            UQ_InPtrOut = 0U;
        }
        UQ_InCntOut++;

        if (UQ_InIdle)                                               // if EpOut paused
        {
            if ((uint8_t)(UQ_InCntIn - UQ_InCntOut) != UQ_QUEUELEN)  // if Queue not full
            {
                // start EpOut recv
                UQ_InIdle = 0U;
                USBQueue_SetEPDNAddr (UQ_InQueue[UQ_InPtrIn]);
                USBQueue_SetEPDNAck (ENABLE);
            }
        }

        // Update Response Index and Count
        UQ_OutPtrIn++;
        if (UQ_OutPtrIn == UQ_QUEUELEN)  // loopback
        {
            UQ_OutPtrIn = 0U;
        }
        UQ_OutCntIn++;

        if (UQ_OutIdle)                       // if EpIn finished
        {
            if (UQ_OutCntIn != UQ_OutCntOut)  // if Queue not empty
            {
                n = UQ_OutPtrOut++;
                if (UQ_OutPtrOut == UQ_QUEUELEN)  // loopback
                {
                    UQ_OutPtrOut = 0U;
                }
                UQ_OutCntOut++;
                UQ_OutIdle = 0U;
                USBQueue_EPUpload (UQ_OutQueue[n], UQ_OutLen[n]);
            }
        }
        flagProcessed++;
    }
    return flagProcessed;
}
