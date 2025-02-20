/*
 *  Main function file for firmware of CheapLink_X033
 *  Copyright (C) 2022-2024  WuxiProject
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

#include "debug.h"

#include "FreeRTOS.h"
#include "task.h"

#include "ch32x035_usbfs_device.h"
#include "usbqueue.h" // 这个版本的USBQueue已经修改过。This version of USBQueue library has been modified.

#include "DAP_config.h"
#include "DAP.h"

extern TaskHandle_t taskHandleLED;
extern void task_LED(void *pvParameters);
extern TaskHandle_t taskHandleDAP;
extern void task_DAP(void *pvParameters);
extern TaskHandle_t taskHandleSER;
extern void task_SER(void *pvParameters);

extern void USBFS_IRQHandler(void) __attribute__((interrupt())) __attribute__((section(".highcode")));

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
    SystemCoreClockUpdate();
    Delay_Init();
#if DEBUG
#if SDI_PRINT == SDI_PR_CLOSE
    USART_Printf_Init(921600);
#else
    SDI_Printf_Enable();
#endif
    PRINT("SystemClk:%d\r\n", SystemCoreClock);
    PRINT("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
#endif

    char snbuf[9];
    snprintf(snbuf, 9, "%08X", (X035CHIPSN1 ^ ~X035CHIPSN2));
    for (uint8_t i = 0; i < 8; i++)
    {
        MySerNumInfo[12 + 2 * i] = snbuf[i];
    }
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_SupplyVoltage());
    SetVTFIRQ((u32)USBFS_IRQHandler, USBFS_IRQn, 0, ENABLE);
    NVIC_EnableIRQ(USBFS_IRQn);
    DAP_Setup();

    xTaskCreate((TaskFunction_t)task_LED, (const char *)"LED", (uint16_t)128,
                (void *)NULL,
                (UBaseType_t)1, (TaskHandle_t *)&taskHandleLED);
    xTaskCreate((TaskFunction_t)task_DAP, (const char *)"DAP", (uint16_t)128,
                (void *)NULL,
                (UBaseType_t)3, (TaskHandle_t *)&taskHandleDAP);
#if DAP_WITH_CDC
    xTaskCreate((TaskFunction_t)task_SER, (const char *)"SER", (uint16_t)128,
                (void *)NULL,
                (UBaseType_t)3, (TaskHandle_t *)&taskHandleSER);
#endif

    vTaskStartScheduler();

    while (1)
    {
        ;
    }
}
