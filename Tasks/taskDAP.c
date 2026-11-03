/*
 *  Task-DAP source file for firmware of CheapLink_X033
 *  Copyright (C) 2022-2026  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "ch32x035_usbfs_device.h"
#include "usbqueue.h"

extern TaskHandle_t taskHandleLED;

TaskHandle_t taskHandleDAP __attribute__ ((aligned (4)));

void task_DAP (void *pvParameters) {
    xTaskNotify (taskHandleLED, 0x32, eSetValueWithOverwrite);  // LED: Yellow 1Hz
    volatile BaseType_t waitFlag;
    volatile uint32_t notifyValue;
    while (1) {
        // NOTE: the 3rd parameter of xTaskNotifyWait() is an OUTPUT (the pending
        // notification value).  Testing the return value of xTaskNotifyWait()
        // instead only yields pdTRUE/pdFALSE and loses every notification bit.
        waitFlag = xTaskNotifyWait (0x0, 0xffffffffUL, (uint32_t *)&notifyValue, pdMS_TO_TICKS (5000));
        if (waitFlag == pdFALSE) {
            // Idle for 5s+
            if (USBFS_DevEnumStatus && !(USBFSD->MIS_ST & USBFS_UMS_SUSPEND)) {
                // USB device active
                xTaskNotify (taskHandleLED, 0x31, eSetValueWithOverwrite);  // LED: Yellow Still
            }
        } else {
            // ulBitsToClearOnExit = 0xffffffff clears the value on return, so both
            // masks below must be tested against this single copy.
            // Check if USB offline (bus reset / suspend): drop the whole pipeline.
            if (notifyValue & 0x00030000UL) {
                USBQueue_Purge();
            }
            // Check if command in queue
            if (notifyValue & 0x0000ffffUL) {
                USBQueue_DoProcess();
            }
        }
    }
    vTaskDelete (NULL);
}
