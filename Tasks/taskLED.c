/*
 *  Task-LED source file for firmware of CheapLink_X033
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

TaskHandle_t taskHandleLED __attribute__ ((aligned (4)));

void task_LED (void *pvParameters) {
    volatile uint8_t i = 0, LED_State = 0;
    volatile uint32_t notify = 0xffffffff, flashDelayTicks = 0;
    while (1) {
        switch (LED_State) {
        case 0x11:
            // Red Still
            GPIOC->BCR = GPIO_Pin_3;
            GPIOA->BSHR = GPIO_Pin_0 | GPIO_Pin_1;
            break;

        case 0x12:
        // Red Flash(1Hz)
        case 0x13:
            // Red Flash(5Hz)
            if (i) {
                i = 0;
                GPIOC->BSHR = GPIO_Pin_3;
            } else {
                i = 1;
                GPIOC->BCR = GPIO_Pin_3;
            }
            GPIOA->BSHR = GPIO_Pin_0 | GPIO_Pin_1;
            break;

        case 0x21:
            // Green Still
            GPIOA->BCR = GPIO_Pin_1;
            GPIOA->BSHR = GPIO_Pin_0;
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x22:
        // Green Flash(1Hz)
        case 0x23:
            // Green Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_1;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_1;
            }
            GPIOA->BSHR = GPIO_Pin_0;
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x31:
            // Yellow Still
            GPIOA->BCR = GPIO_Pin_1;
            GPIOC->BCR = GPIO_Pin_3;
            GPIOA->BSHR = GPIO_Pin_0;
            break;

        case 0x32:
        // Yellow Flash(1Hz)
        case 0x33:
            // Yellow Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_1;
                GPIOC->BSHR = GPIO_Pin_3;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_1;
                GPIOC->BCR = GPIO_Pin_3;
            }
            GPIOA->BSHR = GPIO_Pin_0;
            break;

        case 0x41:
            // Blue Still
            GPIOA->BCR = GPIO_Pin_0;
            GPIOA->BSHR = GPIO_Pin_1;
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x42:
        // Blue Flash(1Hz)
        case 0x43:
            // Blue Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_0;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_0;
            }
            GPIOA->BSHR = GPIO_Pin_1;
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x51:
            // Magenta Still
            GPIOA->BCR = GPIO_Pin_0;
            GPIOC->BCR = GPIO_Pin_3;
            GPIOA->BSHR = GPIO_Pin_1;
            break;

        case 0x52:
        // Magenta Flash(1Hz)
        case 0x53:
            // Magenta Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_0;
                GPIOC->BSHR = GPIO_Pin_3;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_0;
                GPIOC->BCR = GPIO_Pin_3;
            }
            GPIOA->BSHR = GPIO_Pin_1;
            break;

        case 0x61:
            // Cyan Still
            GPIOA->BCR = GPIO_Pin_0 | GPIO_Pin_1;
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x62:
        // Cyan Flash(1Hz)
        case 0x63:
            // Cyan Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_0 | GPIO_Pin_1;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_0 | GPIO_Pin_1;
            }
            GPIOC->BSHR = GPIO_Pin_3;
            break;

        case 0x71:
            // White Still
            GPIOA->BCR = GPIO_Pin_0 | GPIO_Pin_1;
            GPIOC->BCR = GPIO_Pin_3;
            break;

        case 0x72:
        // White Flash(1Hz)
        case 0x73:
            // White Flash(5Hz)
            if (i) {
                i = 0;
                GPIOA->BSHR = GPIO_Pin_0 | GPIO_Pin_1;
                GPIOC->BSHR = GPIO_Pin_3;
            } else {
                i = 1;
                GPIOA->BCR = GPIO_Pin_0 | GPIO_Pin_1;
                GPIOC->BCR = GPIO_Pin_3;
            }
            break;

        default:
            // Off
            GPIOA->BSHR = GPIO_Pin_0 | GPIO_Pin_1;
            GPIOC->BSHR = GPIO_Pin_3;
            break;
        }
        if (xTaskNotifyWait (0x0, 0xffffffffUL, &notify, flashDelayTicks) == pdTRUE) {
            i = 0;
            LED_State = notify & 0x000000ff;
            if ((LED_State & 0x0f) == 0x02) {
                flashDelayTicks = pdMS_TO_TICKS (500);
            } else if ((LED_State & 0x0f) == 0x03) {
                flashDelayTicks = pdMS_TO_TICKS (200);
            } else {
                flashDelayTicks = portMAX_DELAY;
            }
        }
    }
    vTaskDelete (NULL);
}
