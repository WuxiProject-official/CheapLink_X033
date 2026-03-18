/*
 *  Task-Serial source file for firmware of CheapLink_X033
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#if DAP_WITH_CDC
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "semphr.h"
#include <ch32x035.h>
#include "ch32x035_usbfs_device.h"

TaskHandle_t taskHandleSER __attribute__ ((aligned (4)));

#define CDCSER_UP_ENABLE 1
#define CDCSER_DOWN_ENABLE 1

#if CDCSER_UP_ENABLE
// Upstream defines
#define CDCSER_EPUP_LEN 64U
#define CDCSER_BUFFERUP_LEN (1 << 9U)
#define CDCSER_BUFFERUP_THRESHOLD (1 << 4U)
#define CDCSER_DMARX_LEN (1 << 8U)
#endif

#if CDCSER_DOWN_ENABLE
//  Downstream defines
#define CDCSER_EPDOWN_LEN 64U
#define CDCSER_BUFFERDOWN_LEN (1 << 9U)
#define CDCSER_BUFFERDOWN_THRESHOLD (1 << 4U)
#define CDCSER_DMATX_LEN (1 << 8U)
#endif

#if CDCSER_UP_ENABLE
volatile uint8_t CDCQueueUp[CDCSER_EPUP_LEN] __attribute__ ((aligned (4)));
volatile uint8_t CDC_DMARxBuf[CDCSER_DMARX_LEN];
StreamBufferHandle_t sbUp = NULL;
static volatile uint8_t CDCSerial_UpIdleUsb = 1;

static volatile uint32_t CDCSerial_LastUpLen = 0;

// Prepare data for IN endpoint upload.
uint8_t CDCSerial_EPUpload (uint8_t *buf, uint16_t len) {
    CDCSerial_LastUpLen = len;
    return USBFS_Endp_DataUp (DEF_UEP3, buf, len, DEF_UEP_DMA_LOAD);
}
#endif

#if CDCSER_DOWN_ENABLE
volatile uint8_t CDCQueueDown[2][CDCSER_EPDOWN_LEN] __attribute__ ((aligned (4)));
volatile uint8_t CDC_DMATxBuf[CDCSER_DMATX_LEN];
static volatile uint8_t CDCSerial_DownPtrUsb = 0;
StreamBufferHandle_t sbDown = NULL;
SemaphoreHandle_t semaDmaTx = NULL;

// Prepare buffer for OUT transaction.
static void CDCSerial_SetEPDNAddr (volatile uint8_t *buffer) {
    USBFSD->UEP5_DMA = (uint32_t)buffer;
}

// Control the OUT(downstream) endpoint ACK status.
static void CDCSerial_SetEPDNAck (FunctionalState state) {

    if (DISABLE == state) {
        // NAK
        USBFSD->UEP5_CTRL_H &= ~USBFS_UEP_R_RES_MASK;
        USBFSD->UEP5_CTRL_H |= USBFS_UEP_R_RES_NAK;
    } else {  // ACK
        USBFSD->UEP5_CTRL_H &= ~USBFS_UEP_R_RES_MASK;
        USBFSD->UEP5_CTRL_H |= USBFS_UEP_R_RES_ACK;
    }
}
#endif

void CDCSerial_EpOUT_Handler (uint8_t len, BaseType_t *taskWoken) {
#if CDCSER_DOWN_ENABLE
    // Set ACK state
    if (xStreamBufferSpacesAvailable (sbDown) < (CDCSER_EPDOWN_LEN) + len)
        CDCSerial_SetEPDNAck (DISABLE);
    // Toggle buffer
    CDCSerial_DownPtrUsb = CDCSerial_DownPtrUsb ? 0 : 1;
    CDCSerial_SetEPDNAddr (CDCQueueDown[CDCSerial_DownPtrUsb]);
    // Push buffer
    xStreamBufferSendFromISR (sbDown, CDCQueueDown[!CDCSerial_DownPtrUsb], len, taskWoken);
    // Send notification
    xTaskNotifyFromISR (taskHandleSER, 0x01, eSetBits, taskWoken);
#endif
}

void CDCSerial_EpIN_Handler (BaseType_t *taskWoken) {
#if CDCSER_UP_ENABLE
    uint16_t usbUpLen = xStreamBufferReceiveFromISR (sbUp, CDCQueueUp, CDCSER_EPUP_LEN, taskWoken);
    if (usbUpLen) {                                    // left data in queue
        CDCSerial_EPUpload (CDCQueueUp, usbUpLen);
    } else {                                           // empty queue
        if (CDCSerial_LastUpLen == CDCSER_EPUP_LEN) {  // need another zero-len packet
            CDCSerial_EPUpload (CDCQueueUp, 0);
        }
        CDCSerial_UpIdleUsb = 1;
    }
#endif
}

extern void memset_v (volatile void *p, int val, size_t len);
#define MEMCLEAR(x) (memset ((x), 0x00, sizeof ((x))))

void CDCSerial_QueueReset() {
#if CDCSER_UP_ENABLE
    MEMCLEAR (CDCQueueUp);
    if (sbUp != NULL)
        xStreamBufferReset (sbUp);
    else
        sbUp = xStreamBufferCreate (CDCSER_BUFFERUP_LEN, CDCSER_BUFFERUP_THRESHOLD);
    CDCSerial_UpIdleUsb = 1;
    DMA_Cmd (DMA1_Channel6, DISABLE);
    DMA1_Channel6->CNTR = CDCSER_DMARX_LEN;
    DMA1_Channel6->MADDR = (uint32_t)&CDC_DMARxBuf[0];
#endif

#if CDCSER_DOWN_ENABLE
    MEMCLEAR (CDCQueueDown);
    if (sbDown != NULL)
        xStreamBufferReset (sbDown);
    else
        sbDown = xStreamBufferCreate (CDCSER_BUFFERDOWN_LEN, CDCSER_BUFFERDOWN_THRESHOLD);
    CDCSerial_DownPtrUsb = 0;
    CDCSerial_SetEPDNAddr (CDCQueueDown[CDCSerial_DownPtrUsb]);
    CDCSerial_SetEPDNAck (ENABLE);
    DMA_Cmd (DMA1_Channel7, DISABLE);
    DMA1_Channel7->CNTR = CDCSER_DMATX_LEN;
    DMA1_Channel7->MADDR = (uint32_t)&CDC_DMATxBuf[0];
    if (semaDmaTx == NULL)
        semaDmaTx = xSemaphoreCreateBinary();
    xSemaphoreGive (semaDmaTx);
#endif
    //  If other reset operation required, process below.
}

void USART2_IRQHandler (void) __attribute__ ((interrupt()));  // __attribute__((section(".highcode")));

void USART2_IRQHandler (void) {
    BaseType_t taskWoken = pdFALSE;
    if (USART_GetITStatus (USART2, USART_IT_IDLE) != RESET) {  // Idle
#if CDCSER_UP_ENABLE
        if (DMA1_Channel6->CNTR != 0 && DMA1_Channel6->CNTR != (CDCSER_DMARX_LEN >> 1)) {
            // only handle unfinished DMA
            DMA1_Channel6->CFGR &= (uint16_t)(~DMA_CFGR1_EN);
            DMA1->INTFCR = DMA1_IT_HT6 | DMA1_IT_TC6;
            uint16_t rxCnt = (CDCSER_DMARX_LEN - DMA1_Channel6->CNTR);
            // Check if USB offline
            if ((USBFS_DevEnumStatus != 0) && ((USBFS_DevSleepStatus & 0x02) == 0)) {
                // Push data
                if (rxCnt > (CDCSER_DMARX_LEN >> 1)) {  // Buffer 1
                    xStreamBufferSendFromISR (sbUp, &CDC_DMARxBuf[CDCSER_DMARX_LEN >> 1], rxCnt - (CDCSER_DMARX_LEN >> 1), &taskWoken);
                } else {                                // Buffer 0
                    xStreamBufferSendFromISR (sbUp, &CDC_DMARxBuf[0], rxCnt, &taskWoken);
                }
                // Reset buffer
                DMA1_Channel6->CNTR = CDCSER_DMARX_LEN;
                DMA1_Channel6->MADDR = (uint32_t)(&CDC_DMARxBuf[0]);
                DMA1_Channel6->CFGR |= DMA_CFGR1_EN;
                // Notify main thread
                if (CDCSerial_UpIdleUsb) {  // if USB Idle
                    xTaskNotifyFromISR (taskHandleSER, 0x02U, eSetBits, &taskWoken);
                }
            }
        }
#endif
        // Clear flag
        (void)USART_ReceiveData (USART2);
    }
    if (USART_GetITStatus (USART2, USART_IT_PE) != RESET) {  // Parity Err
        ;
        // Clear flag
        (void)USART_ReceiveData (USART2);
    }
    if (USART_GetITStatus (USART2, USART_IT_ORE_ER)) {  // Overrun
        ;
        // Clear flag
        (void)USART_ReceiveData (USART2);
    }
    if (USART_GetITStatus (USART2, USART_IT_NE)) {  // Noise
        ;
        // Clear flag
        (void)USART_ReceiveData (USART2);
    }
    if (USART_GetITStatus (USART2, USART_IT_FE)) {  // Frame err
        ;
        // Clear flag
        (void)USART_ReceiveData (USART2);
    }
    portYIELD_FROM_ISR (taskWoken);
}

// DMA RX2
void DMA1_Channel6_IRQHandler (void) __attribute__ ((interrupt()));  // __attribute__((section(".highcode")));

void DMA1_Channel6_IRQHandler (void) {
    BaseType_t taskWoken = pdFALSE;
    if (DMA_GetITStatus (DMA1_IT_TC6) != RESET) {  // TC, Using buffer 1.
        DMA1->INTFCR = DMA1_IT_TC6;
#if CDCSER_UP_ENABLE
        // Check if USB offline
        if ((USBFS_DevEnumStatus != 0) && ((USBFS_DevSleepStatus & 0x02) == 0)) {
            // Push data
            xStreamBufferSendFromISR (sbUp, &CDC_DMARxBuf[CDCSER_DMARX_LEN >> 1], (CDCSER_DMARX_LEN >> 1), &taskWoken);
            // Notify main thread
            if (CDCSerial_UpIdleUsb)  // if USB Idle
            {
                xTaskNotifyFromISR (taskHandleSER, 0x02U, eSetBits, &taskWoken);
            }
        }
#endif
    }
    if (DMA_GetITStatus (DMA1_IT_HT6) != RESET) {  // HT, Using buffer 0.
        DMA1->INTFCR = DMA1_IT_HT6;
#if CDCSER_UP_ENABLE
        // Check if USB offline
        if ((USBFS_DevEnumStatus != 0) && ((USBFS_DevSleepStatus & 0x02) == 0)) {
            // Push data
            xStreamBufferSendFromISR (sbUp, &CDC_DMARxBuf[0], (CDCSER_DMARX_LEN >> 1), &taskWoken);
            // Notify main thread
            if (CDCSerial_UpIdleUsb)  // if USB Idle
            {
                xTaskNotifyFromISR (taskHandleSER, 0x02U, eSetBits, &taskWoken);
            }
        }
#endif
    }
    portYIELD_FROM_ISR (taskWoken);
}

// DMA_TX2
void DMA1_Channel7_IRQHandler (void) __attribute__ ((interrupt()));  // __attribute__((section(".highcode")));

void DMA1_Channel7_IRQHandler (void) {
    BaseType_t taskWoken = pdFALSE;
    if (DMA_GetITStatus (DMA1_IT_TC7) != RESET) {  // TC
        DMA1_Channel7->CFGR &= (uint16_t)(~DMA_CFGR1_EN);
        DMA1->INTFCR = DMA1_IT_TC7;
#if CDCSER_DOWN_ENABLE
        // Check if USB offline
        if ((USBFS_DevEnumStatus != 0) && ((USBFS_DevSleepStatus & 0x02) == 0)) {
            uint16_t dmaTxLen = xStreamBufferReceiveFromISR (sbDown, CDC_DMATxBuf, CDCSER_DMATX_LEN, &taskWoken);
            if (dmaTxLen) {
                DMA1_Channel7->MADDR = (uint32_t)&CDC_DMATxBuf[0];
                DMA1_Channel7->CNTR = dmaTxLen;
                DMA_Cmd (DMA1_Channel7, ENABLE);
            } else {
                xSemaphoreGiveFromISR (semaDmaTx, &taskWoken);
            }
            if (xStreamBufferSpacesAvailable (sbDown) >= (CDCSER_EPDOWN_LEN << 1U))
                CDCSerial_SetEPDNAck (ENABLE);
        }
#endif
    }
    portYIELD_FROM_ISR (taskWoken);
}

void CDCSerial_InitUART (uint32_t baudrate, uint16_t databit, uint16_t paritybit,
                         uint16_t stopbit) {
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);

    // Init GPIO
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init (GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    // Init UART
    USART_InitTypeDef USART_InitStructure = {0};
    USART_DeInit (USART2);
    (void)(USART2->STATR);
    (void)(USART2->DATAR);
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = databit;
    USART_InitStructure.USART_StopBits = stopbit;
    USART_InitStructure.USART_Parity = paritybit;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init (USART2, &USART_InitStructure);
#if CDCSER_UP_ENABLE
    USART_ITConfig (USART2, USART_IT_IDLE, ENABLE);
#endif
    USART_ITConfig (USART2, USART_IT_PE, ENABLE);
    USART_ITConfig (USART2, USART_IT_ERR, ENABLE);
    NVIC_SetPriority (USART2_IRQn, 6);
    NVIC_EnableIRQ (USART2_IRQn);
    USART_Cmd (USART2, ENABLE);

    // Init DMA
    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&USART2->DATAR);
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;

#if CDCSER_UP_ENABLE
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)&CDC_DMARxBuf[0];
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = CDCSER_DMARX_LEN;
    DMA_DeInit (DMA1_Channel6);
    DMA_Init (DMA1_Channel6, &DMA_InitStructure);
    DMA_ITConfig (DMA1_Channel6, DMA_IT_HT, ENABLE);
    DMA_ITConfig (DMA1_Channel6, DMA_IT_TC, ENABLE);
    DMA_ClearITPendingBit (DMA1_IT_GL6);
    DMA_ClearITPendingBit (DMA1_IT_TC6);
    DMA_ClearITPendingBit (DMA1_IT_HT6);
    NVIC_SetPriority (DMA1_Channel6_IRQn, 6);
    NVIC_EnableIRQ (DMA1_Channel6_IRQn);
    DMA_Cmd (DMA1_Channel6, ENABLE);
    USART_DMACmd (USART2, USART_DMAReq_Rx, ENABLE);
#endif

#if CDCSER_DOWN_ENABLE
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)&CDC_DMATxBuf[0];
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = CDCSER_DMATX_LEN;
    DMA_DeInit (DMA1_Channel7);
    DMA_Init (DMA1_Channel7, &DMA_InitStructure);
    DMA_ITConfig (DMA1_Channel7, DMA_IT_HT, DISABLE);
    DMA_ITConfig (DMA1_Channel7, DMA_IT_TC, ENABLE);
    DMA_ClearITPendingBit (DMA1_IT_GL7);
    DMA_ClearITPendingBit (DMA1_IT_TC7);
    DMA_ClearITPendingBit (DMA1_IT_HT7);
    NVIC_SetPriority (DMA1_Channel7_IRQn, 6);
    NVIC_EnableIRQ (DMA1_Channel7_IRQn);
    USART_DMACmd (USART2, USART_DMAReq_Tx, ENABLE);
#endif
}

int __attribute__ ((noinline)) CDCSerial_Init (uint8_t linecoding[8]) {
    uint32_t baudrate = linecoding[0];
    baudrate += ((uint32_t)linecoding[1] << 8);
    baudrate += ((uint32_t)linecoding[2] << 16);
    baudrate += ((uint32_t)linecoding[3] << 24);
    uint16_t databit = linecoding[6], paritybit = linecoding[5], stopbit = linecoding[4];
    if (
        (baudrate < 800UL || baudrate > 1000000UL)  // out of 800-1M
        || (databit != 0 && databit != 8)           // out of 8
        || (paritybit > 2)                          // out of N,O,E
        || (stopbit > 2)                            // out of 1,1.5,2
    ) {
        return 1;
    } else {
        if (databit == 8 || databit == 0)
            databit = USART_WordLength_8b;
        if (paritybit == 0)
            paritybit = USART_Parity_No;
        else if (paritybit == 1) {
            paritybit = USART_Parity_Odd;
            databit = USART_WordLength_9b;
        } else if (paritybit == 2) {
            paritybit = USART_Parity_Even;
            databit = USART_WordLength_9b;
        }
        if (stopbit == 0)
            stopbit = USART_StopBits_1;
        else if (stopbit == 1)
            stopbit = USART_StopBits_1_5;
        else if (stopbit == 2)
            stopbit = USART_StopBits_2;
        CDCSerial_InitUART (baudrate, databit, paritybit, stopbit);
        return 0;
    }
}

void task_SER (void *pvParameters) {
    uint32_t notifyFlag;
    while (1) {
        xTaskNotifyWait (0x0, 0xffffffffUL, &notifyFlag, portMAX_DELAY);
        // Check if USB offline
        if (notifyFlag & 0x00030000UL) {
            // Reset Queue.
            CDCSerial_QueueReset();
        }
        if (notifyFlag & 0x00000001UL) {                          // Downstream packet pending
#if CDCSER_DOWN_ENABLE
            if (xSemaphoreTake (semaDmaTx, pdMS_TO_TICKS (5))) {  // DMA not running
                uint16_t dmaTxLen = xStreamBufferReceive (sbDown, CDC_DMATxBuf, CDCSER_DMATX_LEN, pdMS_TO_TICKS (5));
                if (dmaTxLen) {
                    DMA1_Channel7->MADDR = (uint32_t)&CDC_DMATxBuf[0];
                    DMA1_Channel7->CNTR = dmaTxLen;
                    DMA_Cmd (DMA1_Channel7, ENABLE);
                } else {  // Nothing to send
                    xSemaphoreGive (semaDmaTx);
                }
            }
#endif
        }
        if (notifyFlag & 0x00000002UL) {  // Upstream packet pending
#if CDCSER_UP_ENABLE
            uint16_t usbUpLen = xStreamBufferReceive (sbUp, CDCQueueUp, CDCSER_EPUP_LEN, pdMS_TO_TICKS (1));
            if (usbUpLen) {
                CDCSerial_UpIdleUsb = 0U;
                CDCSerial_EPUpload (CDCQueueUp, usbUpLen);
            }
#endif
        }
    }
    vTaskDelete (NULL);
}
#endif
