/*
 *  Task-DAP source file for firmware of CheapLink_X033
 *  Copyright (C) 2022-2025  WuxiProject
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
    volatile int32_t waitFlag;
    while (1) {
        waitFlag = xTaskNotifyWait (0x0, 0xffffffffUL, NULL, pdMS_TO_TICKS (5000));
        if (waitFlag == pdFALSE) {
            // Idle for 5s+
            if (USBFS_DevEnumStatus && !(USBFSD->MIS_ST & USBFS_UMS_SUSPEND)) {
                // USB device active
                xTaskNotify (taskHandleLED, 0x31, eSetValueWithOverwrite);  // LED: Yellow Still
            }
        } else {
            // Check if USB offline
            if (waitFlag & 0x00030000UL) {
                // Reset Queue.
                USBQueue_StatusReset();
            }
            // Check if command in queue
            if (waitFlag & 0x0000ffffUL) {
                USBQueue_DoProcess();
            }
        }
    }
    vTaskDelete (NULL);
}
