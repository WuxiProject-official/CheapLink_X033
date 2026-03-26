/*
 *  Main function file for firmware of CheapLink_X033
 *  Copyright (C) 2022-2026  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "debug.h"

#include "FreeRTOS.h"
#include "task.h"

#include "ch32x035_usbfs_device.h"
#include "usbqueue.h"  // 这个版本的USBQueue已经修改过。This version of USBQueue library has been modified.

#include "DAP_config.h"
#include "DAP.h"

extern TaskHandle_t taskHandleLED;
extern void task_LED (void *pvParameters);
extern TaskHandle_t taskHandleDAP;
extern void task_DAP (void *pvParameters);
extern TaskHandle_t taskHandleSER;
extern void task_SER (void *pvParameters);

extern void USBFS_IRQHandler (void);  // Assembly wrapper

#if DAP_WITH_CDC
extern void CDCSerial_QueueReset();
#endif

volatile char taskname;

void vApplicationStackOverflowHook (TaskHandle_t xTask, char *pcTaskName) {

    taskDISABLE_INTERRUPTS();
    taskname = pcTaskName[0];
    while (1);
}

static void ConvertUint32ToUnicode16Str(uint32_t num, unsigned char *str) {
    // Note: this does not append null terminator
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t digit = (num >> ((7 - i) << 2)) & 0x0F;
        str[2 * i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        str[2 * i + 1] = 0;  // constant 0 for high byte in Unicode-16
    }
}

__attribute__ ((noreturn)) int main (void) {
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_0);
    SystemCoreClockUpdate();
    Delay_Init();
#if DEBUG
#if SDI_PRINT == SDI_PR_CLOSE
    USART_Printf_Init (921600);
#else
    SDI_Printf_Enable();
#endif
    // Print hello info if in debug mode
    PRINT ("CheapLink_X033 V1.0.3.0 running on CH32X035(%04X)-%08X%08X clk=%d\r\n",
           DBGMCU_GetCHIPID() >> 16, X035CHIPSN1, X035CHIPSN2, SystemCoreClock);

#endif

    // Init queue
    USBQueue_StatusReset();
#if DAP_WITH_CDC
    CDCSerial_QueueReset();
#endif

    // Prepare USB desc SN
    uint32_t chipNum = (X035CHIPSN1 ^ ~X035CHIPSN2);
    ConvertUint32ToUnicode16Str(chipNum, MySerNumInfo + 12);
    // Init USB
    USBFS_RCC_Init();
    USBFS_Device_Init (ENABLE, PWR_VDD_SupplyVoltage());
    SetVTFIRQ ((u32)USBFS_IRQHandler, USBFS_IRQn, 0, ENABLE);
    NVIC_EnableIRQ (USBFS_IRQn);
    DAP_Setup();

    xTaskCreate ((TaskFunction_t)task_LED,
                 (const char *)"LED",
                 (uint16_t)128,
                 (void *)NULL,
                 (UBaseType_t)1,
                 (TaskHandle_t *)&taskHandleLED);
    xTaskCreate ((TaskFunction_t)task_DAP,
                 (const char *)"DAP",
                 (uint16_t)192,
                 (void *)NULL,
                 (UBaseType_t)3,
                 (TaskHandle_t *)&taskHandleDAP);
#if DAP_WITH_CDC
    xTaskCreate ((TaskFunction_t)task_SER,
                 (const char *)"SER",
                 (uint16_t)128,
                 (void *)NULL,
                 (UBaseType_t)3,
                 (TaskHandle_t *)&taskHandleSER);
#endif

    vTaskStartScheduler();

    while (1) {
        ;
    }
}
