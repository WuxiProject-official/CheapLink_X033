/*
 *  Task-DAP source file for firmware of CheapLink_X033
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

#include "FreeRTOS.h"
#include "task.h"
#include "ch32x035_usbfs_device.h"

extern TaskHandle_t taskHandleLED;
extern uint8_t USBQueue_DoProcess();

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
            USBQueue_DoProcess();
        }
    }
    vTaskDelete (NULL);
}
