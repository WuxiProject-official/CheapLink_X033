/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32x035_usbfs_device.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/04/06
 * Description        : This file provides all the USBFS firmware functions.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include <ch32x035_usbfs_device.h>

#include "usbqueue.h"

// FreeRTOS is needed by USBFS_Device_Init(): see the Delay_Us() guard there.
#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t taskHandleDAP;
#if DAP_WITH_CDC
extern TaskHandle_t taskHandleSER;
#endif
extern TaskHandle_t taskHandleLED;

// 为确保宏定义生效，请在全局宏定义中设置MSOS_DESC和DAP_WITH_CDC的值。

/*******************************************************************************/
/* Variable Definition */
/* Global */
const uint8_t *pUSBFS_Descr;

/* Setup Request */
volatile uint8_t USBFS_SetupReqCode;
volatile uint8_t USBFS_SetupReqType;
volatile uint16_t USBFS_SetupReqValue;
volatile uint16_t USBFS_SetupReqIndex;
volatile uint16_t USBFS_SetupReqLen;

/* USB Device Status */
volatile uint8_t USBFS_DevConfig;
volatile uint8_t USBFS_DevAddr;
volatile uint8_t USBFS_DevSleepStatus;
volatile uint8_t USBFS_DevEnumStatus;

/* Endpoint Buffer */
uint8_t USBFS_EP0_4Buf[DEF_USBD_UEP0_SIZE] __attribute__ ((aligned (4)));

/* USB IN Endpoint Busy Flag */
volatile uint8_t USBFS_Endp_Busy[DEF_UEP_NUM];

/******************************************************************************/
/* Interrupt Service Routine Declaration*/
void USBFS_IRQHandler (void)  __attribute__((interrupt()));// __attribute__ ((section (".highcode")));

/* Endpoint HALT (stall) bitmap.  HALT is protocol-stack state:
 * bit index = endpoint number, +16 when the direction is IN, matching the
 * direction bit (bit7) of the endpoint address.
 *   bit0..15  = OUT EP0..15
 *   bit16..31 = IN  EP0..15
 * Bits are only set/cleared inside the USB interrupt (SET/CLEAR_FEATURE are
 * handled in the EP0 interrupt context), so task side only reads it. */
static volatile uint32_t USBFS_EPHaltBitmap = 0;

#define USBFS_EPHALT_BIT(ep)  (1UL << (((ep) & 0x0F) + ((((ep) & DEF_UEP_IN) != 0) ? 16 : 0)))

uint8_t USBFS_IsEPHalted (uint8_t ep) {
    return (USBFS_EPHaltBitmap & USBFS_EPHALT_BIT(ep)) ? 1U : 0U;
}

/*********************************************************************
 * @fn      USBFS_SetEPHalt
 *
 * @brief   Apply the host's SET/CLEAR_FEATURE(ENDPOINT_HALT) to one endpoint.
 *          The endpoint register and the HALT bit are updated first, then the
 *          queue layer is notified so it can invalidate the whole pipeline.
 *
 * @param   halted - 0 = CLEAR_FEATURE, non-zero = SET_FEATURE
 * @param   ep     - endpoint address, DEF_UEPn | DEF_UEP_IN / DEF_UEP_OUT
 *
 * @return  none
 */
static void USBFS_SetEPHalt (uint8_t halted, uint8_t ep) {
    volatile uint8_t *ctrl;
    uint32_t resMask, resStall, resNak, bit;
    switch (ep) {
    case (DEF_UEP_OUT | DEF_UEP1):
        ctrl = &USBFSD->UEP1_CTRL_H, resMask = USBFS_UEP_R_RES_MASK;
        resStall = USBFS_UEP_R_RES_STALL, resNak = USBFS_UEP_R_RES_NAK;
        break;
    case (DEF_UEP_IN | DEF_UEP2):
        ctrl = &USBFSD->UEP2_CTRL_H, resMask = USBFS_UEP_T_RES_MASK;
        resStall = USBFS_UEP_T_RES_STALL, resNak = USBFS_UEP_T_RES_NAK;
        break;
    default:
        /* CDC endpoints keep their original handling (see CLEAR/SET_FEATURE). */
        return;
    }
    bit = USBFS_EPHALT_BIT (ep);
    if (halted) {
        USBFS_EPHaltBitmap |= bit;
        // SET_FEATURE: stall the endpoint. Do not touch the data toggle.
        *ctrl = (*ctrl & ~resMask) | resStall;
    } else {
        USBFS_EPHaltBitmap &= ~bit;
        // CLEAR_FEATURE: whole-byte write => TOG back to DATA0, RES = NAK.
        // The queue layer re-arms the endpoint after this callback returns.
        *ctrl = resNak;
    }
    USBQueue_EpHalt_Handler (halted, ep);
}

/*********************************************************************
 * @fn      USBFS_RCC_Init
 *
 * @brief   Initializes the USBFS clock configuration.
 *
 * @return  none
 */
void USBFS_RCC_Init (void) {
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_USBFS, ENABLE);
}

#if DAP_WITH_CDC
uint8_t CDC_linecoding[8];
extern void CDCSerial_EpOUT_Handler (uint8_t len, BaseType_t *taskWoken);
extern void CDCSerial_EpIN_Handler (BaseType_t *taskWoken);
extern int __attribute__ ((noinline)) CDCSerial_Init (uint8_t linecoding[8]);
#endif

/*********************************************************************
 * @fn      USBFS_Device_Endp_Init
 *
 * @brief   Initializes USB device endpoints.
 *
 * @return  none
 */
void USBFS_Device_Endp_Init (void) {

    /* Endpoint HALT is protocol-stack state: a bus reset invalidates all of it. */
    USBFS_EPHaltBitmap = 0;

    USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN;
#if DAP_WITH_CDC
    USBFSD->UEP2_3_MOD = USBFS_UEP2_TX_EN | USBFS_UEP3_TX_EN;
    USBFSD->UEP567_MOD = USBFS_UEP5_RX_EN;
#else
    USBFSD->UEP2_3_MOD = USBFS_UEP2_TX_EN;
#endif

    USBFSD->UEP0_DMA = (uint32_t)USBFS_EP0_4Buf;

    USBFSD->UEP0_CTRL_H = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
    USBFSD->UEP1_CTRL_H = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP2_TX_LEN = 0;
    USBFSD->UEP2_CTRL_H = USBFS_UEP_T_RES_NAK;
#if DAP_WITH_CDC
    USBFSD->UEP3_TX_LEN = 0;
    USBFSD->UEP3_CTRL_H = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP5_CTRL_H = USBFS_UEP_R_RES_ACK;
#endif

    /* Clear End-points Busy Status */
    for (uint8_t i = 0; i < DEF_UEP_NUM; i++) {
        USBFS_Endp_Busy[i] = 0;
    }
}

/*********************************************************************
 * @fn      GPIO_USB_INIT
 *
 * @brief   Initializes USB GPIO.
 *
 * @return  none
 */
void GPIO_USB_INIT (void) {
    GPIO_InitTypeDef GPIO_InitStructure =
        {0};

    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_16;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init (GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_17;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init (GPIOC, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      USBFS_Device_Init
 *
 * @brief   Initializes USB device.
 *
 * @return  none
 */
void USBFS_Device_Init (FunctionalState sta, PWR_VDD VDD_Voltage) {
    if (sta) {
        GPIO_USB_INIT();
        if (VDD_Voltage == PWR_VDD_5V) {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_PHY_V33)) | UDP_PUE_10K | USB_IOEN;
        } else {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK)) | USB_PHY_V33 | UDP_PUE_1K5 | USB_IOEN;
        }
        USBFSD->BASE_CTRL = 0x00;
        USBFS_Device_Endp_Init();
        USBFSD->DEV_ADDR = 0x00;
        USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
        USBFSD->INT_FG = 0xff;
        USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
        USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
        /* Do NOT enable the USB interrupt here: the RTOS tasks (and therefore
         * taskHandleDAP / taskHandleLED) may not exist yet, and xTaskNotifyFromISR()
         * on a NULL handle is undefined behaviour.  The caller enables it once the
         * queue is armed and the tasks are created. */
        // NVIC_EnableIRQ (USBFS_IRQn);
    } else {
        AFIO->CTLR = AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_IOEN);
        USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        // Let the SIE reset settle.
        // Delay_Us()/Delay_Ms() must NOT be used once the scheduler is running:
        // they reprogram SysTick->CMP/CTLR and leave SysTick stopped (CTLR bit0
        // cleared), which destroys the FreeRTOS tick. vTaskDelay() yields instead.
        if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
            Delay_Us (10);
        } else {
            vTaskDelay (pdMS_TO_TICKS (1));  // comfortably exceeds the 10 us settle time
        }
        USBFSD->BASE_CTRL = 0x00;
        NVIC_DisableIRQ (USBFS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBFS_Endp_DataUp
 *
 * @brief   USBFS device data upload
 *
 * @return  none
 */
uint8_t USBFS_Endp_DataUp (uint8_t endp, uint8_t *pbuf, uint16_t len,
                           uint8_t mod) {

    /* DMA config, endp_ctrl config, endp_len config */
    if ((endp >= DEF_UEP1) && (endp <= DEF_UEP7)) {
        if (USBFS_Endp_Busy[endp] == 0) {
            /* Set end-point busy */
            USBFS_Endp_Busy[endp] = 0x01;
            switch (endp) {
            case DEF_UEP2:
                /* only DMA mode */
                USBFSD->UEP2_DMA = (uint32_t)pbuf;
                USBFSD->UEP2_TX_LEN = len;
                USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
                break;
#if DAP_WITH_CDC
            case DEF_UEP3:
                /* only DMA mode */
                USBFSD->UEP3_DMA = (uint32_t)pbuf;
                USBFSD->UEP3_TX_LEN = len;
                USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
                break;
#endif
            }
        } else {
            return 1;
        }
    } else {
        return 1;
    }
    return 0;
}

/*********************************************************************
 * @fn      USBFS_IRQHandler
 *
 * @brief   This function handles HD-FS exception.
 *
 * @return  none
 */
void USBFS_IRQHandler (void) {
    GET_INT_SP();

    uint8_t intflag, intst, errflag;
    uint16_t len;
    BaseType_t taskWoken = pdFALSE;

    intflag = USBFSD->INT_FG;
    intst = USBFSD->INT_ST;

    if (intflag & USBFS_UIF_TRANSFER) {
        switch (intst & USBFS_UIS_TOKEN_MASK) {
        /* data-in stage processing */
        case USBFS_UIS_TOKEN_IN:
            switch (intst & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK)) {
            /* end-point 0 data in interrupt */
            case USBFS_UIS_TOKEN_IN | DEF_UEP0:
                if (USBFS_SetupReqLen == 0) {
                    USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                }
                if ((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
                    if (USBFS_SetupReqType & USB_REQ_TYP_VENDOR) {
                        /* Manufacturer request */
#if MSOS_DESC == 1
                        if (USBFS_SetupReqCode == 0x11) {
                            if (USBFS_SetupReqIndex == 0x0004 || USBFS_SetupReqIndex == 0x0005) {
                                len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                                memcpy (USBFS_EP0_4Buf, pUSBFS_Descr, len);
                                USBFS_SetupReqLen -= len;
                                pUSBFS_Descr += len;
                                USBFSD->UEP0_TX_LEN = len;
                                USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
                            }
                        }
#elif MSOS_DESC == 2
                        if (USBFS_SetupReqCode == 0x01) {  // vendorCode
                            if (USBFS_SetupReqIndex == 0x0007) {
                                len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                                memcpy (USBFS_EP0_4Buf, pUSBFS_Descr, len);
                                USBFS_SetupReqLen -= len;
                                pUSBFS_Descr += len;
                                USBFSD->UEP0_TX_LEN = len;
                                USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
                            }
                        }
#endif
                    }
                } else {
                    /* Standard request endpoint 0 Data upload */
                    switch (USBFS_SetupReqCode) {
                    case USB_GET_DESCRIPTOR:
                        len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                        memcpy (USBFS_EP0_4Buf, pUSBFS_Descr, len);
                        USBFS_SetupReqLen -= len;
                        pUSBFS_Descr += len;
                        USBFSD->UEP0_TX_LEN = len;
                        USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
                        break;

                    case USB_SET_ADDRESS:
                        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | USBFS_DevAddr;
                        break;

                    default:
                        break;
                    }
                }
                break;

                /* end-point 2 data in interrupt */
            case (USBFS_UIS_TOKEN_IN | DEF_UEP2):
                USBFSD->UEP2_CTRL_H ^= USBFS_UEP_T_TOG;
                USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                USBFS_Endp_Busy[DEF_UEP2] = 0;
                USBQueue_EpIN_Handler();
                break;
#if DAP_WITH_CDC
                /* end-point 3 data in interrupt */
            case (USBFS_UIS_TOKEN_IN | DEF_UEP3):
                USBFSD->UEP3_CTRL_H ^= USBFS_UEP_T_TOG;
                USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                USBFS_Endp_Busy[DEF_UEP3] = 0;
                CDCSerial_EpIN_Handler (&taskWoken);
                break;
#endif

            default:
                break;
            }
            break;

            /* data-out stage processing */
        case USBFS_UIS_TOKEN_OUT:
            switch (intst & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK)) {
            /* end-point 0 data out interrupt */
            case USBFS_UIS_TOKEN_OUT | DEF_UEP0:
                len = USBFSD->RX_LEN;
                if (intst & USBFS_UIS_TOG_OK) {
                    if ((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
                        /* Non-standard request end-point 0 Data download */
                        USBFS_SetupReqLen = 0;
#if DAP_WITH_CDC
                        if (USBFS_SetupReqCode == CDC_SET_LINE_CODING) {
                            /* Save relevant parameters such as serial port baud rate */
                            /* The downlinked data is processed in the endpoint 0 OUT packet, the 7 bytes of the downlink are, in order
                             4 bytes: baud rate value: lowest baud rate byte, next lowest baud rate byte, next highest baud rate byte, highest baud rate byte.
                             1 byte: number of stop bits (0: 1 stop bit; 1: 1.5 stop bit; 2: 2 stop bits).
                             1 byte: number of parity bits (0: None; 1: Odd; 2: Even; 3: Mark; 4: Space).
                             1 byte: number of data bits (5,6,7,8,16); */
                            // CDC_linecoding[0] = USBFS_EP0_4Buf[0];
                            // CDC_linecoding[1] = USBFS_EP0_4Buf[1];
                            // CDC_linecoding[2] = USBFS_EP0_4Buf[2];
                            // CDC_linecoding[3] = USBFS_EP0_4Buf[3];
                            // CDC_linecoding[4] = USBFS_EP0_4Buf[4];
                            // CDC_linecoding[5] = USBFS_EP0_4Buf[5];
                            // CDC_linecoding[6] = USBFS_EP0_4Buf[6];
                            memcpy (CDC_linecoding, USBFS_EP0_4Buf, 7);

                            if (CDCSerial_Init (CDC_linecoding)) {  // Wrong parameters
                                USBFS_SetupReqLen = 1;
                                USBFSD->UEP0_TX_LEN = 0;
                                USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL;
                            }
                        }
#endif
                    } else {
                        /* Standard request end-point 0 Data download */
                        /* Add your code here */
                    }
                    if (USBFS_SetupReqLen == 0) {
                        USBFSD->UEP0_TX_LEN = 0;
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    }
                }
                break;

                /* end-point 1 data out interrupt */
            case USBFS_UIS_TOKEN_OUT | DEF_UEP1:
                USBFSD->UEP1_CTRL_H ^= USBFS_UEP_R_TOG;
                USBQueue_EpOUT_Handler (USBFSD->RX_LEN, &taskWoken);
                break;

#if DAP_WITH_CDC
                /* end-point 5 data out interrupt */
            case USBFS_UIS_TOKEN_OUT | DEF_UEP5:
                USBFSD->UEP5_CTRL_H ^= USBFS_UEP_R_TOG;
                CDCSerial_EpOUT_Handler (USBFSD->RX_LEN, &taskWoken);
                break;
#endif

            default:
                break;
            }
            break;

            /* Setup stage processing */
        case USBFS_UIS_TOKEN_SETUP:
            USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

            /* Store All Setup Values */
            USBFS_SetupReqType = pUSBFS_SetupReqPak->bRequestType;
            USBFS_SetupReqCode = pUSBFS_SetupReqPak->bRequest;
            USBFS_SetupReqLen = pUSBFS_SetupReqPak->wLength;
            USBFS_SetupReqValue = pUSBFS_SetupReqPak->wValue;
            USBFS_SetupReqIndex = pUSBFS_SetupReqPak->wIndex;
            len = 0;
            errflag = 0;
            if ((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
                /* usb non-standard request processing */
                if (USBFS_SetupReqType & USB_REQ_TYP_CLASS) {
                    /* Class requests */
                    switch (USBFS_SetupReqCode) {
#if DAP_WITH_CDC
                    case CDC_GET_LINE_CODING:
                        pUSBFS_Descr = CDC_linecoding;
                        len = 7;
                        break;

                    case CDC_SET_LINE_CODING:
                    case CDC_SET_LINE_CTLSTE:
                    case CDC_SEND_BREAK:
                        len = USBFS_SetupReqLen;  //?
                        break;
#endif

                    default:
                        errflag = 0xff;
                        break;
                    }
                } else if (USBFS_SetupReqType & USB_REQ_TYP_VENDOR) {
                    /* Manufacturer request */
#if MSOS_DESC == 1
                    if (USBFS_SetupReqCode == 0x11) {
                        if (USBFS_SetupReqIndex == 0x0004) {
                            pUSBFS_Descr = (uint8_t *)WCID1Desc;
                            len = 40;
                        } else if (USBFS_SetupReqIndex == 0x0005) {
                            pUSBFS_Descr = (uint8_t *)WCID1DescEx;
                            len = 142;
                        }
                    }
#elif MSOS_DESC == 2
                    if (USBFS_SetupReqCode == 0x01) {  // vendorCode
                        if (USBFS_SetupReqIndex == 0x0007) {
                            pUSBFS_Descr = (uint8_t *)MyWinusbDesc;
#if DAP_WITH_CDC
                            len = 170;
#else
                            len = 162;
#endif
                        }
                    }
#endif
                } else {
                    errflag = 0xFF;
                }

                /* Copy Descriptors to Endp0 DMA buffer */
                if (USBFS_SetupReqLen > len) {
                    USBFS_SetupReqLen = len;
                }
                len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                memcpy (USBFS_EP0_4Buf, pUSBFS_Descr, len);
                pUSBFS_Descr += len;
            } else {
                /* usb standard request processing */
                switch (USBFS_SetupReqCode) {
                /* get device/configuration/string/report/... descriptors */
                case USB_GET_DESCRIPTOR:
                    // Call external descriptor func
                    len = USB_GetDescBuf (USBFS_SetupReqValue, &pUSBFS_Descr, &errflag);

                    /* Copy Descriptors to Endp0 DMA buffer */
                    if (USBFS_SetupReqLen > len) {
                        USBFS_SetupReqLen = len;
                    }
                    len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                    memcpy (USBFS_EP0_4Buf, pUSBFS_Descr, len);
                    pUSBFS_Descr += len;
                    break;

                    /* Set usb address */
                case USB_SET_ADDRESS:
                    USBFS_DevAddr = (uint8_t)(USBFS_SetupReqValue & 0xFF);
                    break;

                    /* Get usb configuration now set */
                case USB_GET_CONFIGURATION:
                    USBFS_EP0_4Buf[0] = USBFS_DevConfig;
                    if (USBFS_SetupReqLen > 1) {
                        USBFS_SetupReqLen = 1;
                    }
                    break;

                    /* Set usb configuration to use */
                case USB_SET_CONFIGURATION:
                    USBFS_DevConfig = (uint8_t)(USBFS_SetupReqValue & 0xFF);
                    USBFS_DevEnumStatus = 0x01;
                    if (taskHandleLED) {
                        xTaskNotifyFromISR (taskHandleLED, 0x31, eSetValueWithOverwrite, NULL);  // LED: Yellow Still
                    }
                    break;

                    /* Clear or disable one usb feature */
                case USB_CLEAR_FEATURE:
                    if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        /* clear one device feature */
                        if ((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP) {
                            /* clear usb sleep status, device not prepare to sleep */
                            USBFS_DevSleepStatus &= ~0x01;
                        }
                    } else if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        /* Clear End-point Feature */
                        if ((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT) {
                            switch ((uint8_t)(USBFS_SetupReqIndex & 0xFF)) {
                            case (DEF_UEP_IN | DEF_UEP2):
                            case (DEF_UEP_OUT | DEF_UEP1):
                                /* DAP pipe (EP1 OUT / EP2 IN): the protocol stack
                                 * un-stalls the endpoint and the queue layer
                                 * invalidates the whole pipe. */
                                USBFS_SetEPHalt (0U, (uint8_t)(USBFS_SetupReqIndex & 0xFF));
                                break;
#if DAP_WITH_CDC
                            case (DEF_UEP_IN | DEF_UEP3):
                                /* Set End-point 3 IN NAK */
                                USBFSD->UEP3_CTRL_H = USBFS_UEP_T_RES_NAK;
                                break;

                            case (DEF_UEP_OUT | DEF_UEP5):
                                /* Set End-point 5 OUT ACK */
                                USBFSD->UEP5_CTRL_H = USBFS_UEP_R_RES_ACK;
                                break;
#endif

                            default:
                                errflag = 0xFF;
                                break;
                            }
                        } else {
                            errflag = 0xFF;
                        }
                    } else {
                        errflag = 0xFF;
                    }
                    break;

                    /* set or enable one usb feature */
                case USB_SET_FEATURE:
                    if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        /* Set Device Feature */
                        if ((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP) {
                            if (MyCfgDescr[7] & 0x20) {
                                /* Set Wake-up flag, device prepare to sleep */
                                USBFS_DevSleepStatus |= 0x01;
                            } else {
                                errflag = 0xFF;
                            }
                        } else {
                            errflag = 0xFF;
                        }
                    } else if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        /* Set End-point Feature */
                        if ((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT) {
                            /* Set end-points status stall */
                            switch ((uint8_t)(USBFS_SetupReqIndex & 0xFF)) {
                            case (DEF_UEP_OUT | DEF_UEP1):
                            case (DEF_UEP_IN | DEF_UEP2):
                                /* DAP pipe (EP1 OUT / EP2 IN): stall the endpoint
                                 * and let the queue layer invalidate the pipe. */
                                USBFS_SetEPHalt (1U, (uint8_t)(USBFS_SetupReqIndex & 0xFF));
                                break;

#if DAP_WITH_CDC
                            case (DEF_UEP_IN | DEF_UEP3):
                                /* Set End-point 3 IN STALL */
                                USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_STALL;
                                break;

                            case (DEF_UEP_OUT | DEF_UEP5):
                                /* Set End-point 5 OUT STALL */
                                USBFSD->UEP5_CTRL_H = (USBFSD->UEP5_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_STALL;
                                break;
#endif

                            default:
                                errflag = 0xFF;
                                break;
                            }
                        } else {
                            errflag = 0xFF;
                        }
                    } else {
                        errflag = 0xFF;
                    }
                    break;

                    /* This request allows the host to select another setting for the specified interface  */
                case USB_GET_INTERFACE:
                    USBFS_EP0_4Buf[0] = 0x00;
                    if (USBFS_SetupReqLen > 1) {
                        USBFS_SetupReqLen = 1;
                    }
                    break;

                case USB_SET_INTERFACE:
                    break;

                    /* host get status of specified device/interface/end-points */
                case USB_GET_STATUS:
                    USBFS_EP0_4Buf[0] = 0x00;
                    USBFS_EP0_4Buf[1] = 0x00;
                    if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                        if (USBFS_DevSleepStatus & 0x01) {
                            USBFS_EP0_4Buf[0] = 0x02;
                        }
                    } else if ((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        switch ((uint8_t)(USBFS_SetupReqIndex & 0xFF)) {
                        case (DEF_UEP_OUT | DEF_UEP1):
                            if (((USBFSD->UEP1_CTRL_H) & USBFS_UEP_R_RES_MASK) == USBFS_UEP_R_RES_STALL) {
                                USBFS_EP0_4Buf[0] = 0x01;
                            }
                            break;

                        case (DEF_UEP_IN | DEF_UEP2):
                            if (((USBFSD->UEP2_CTRL_H) & USBFS_UEP_T_RES_MASK) == USBFS_UEP_T_RES_STALL) {
                                USBFS_EP0_4Buf[0] = 0x01;
                            }
                            break;

#if DAP_WITH_CDC
                        case (DEF_UEP_IN | DEF_UEP3):
                            if (((USBFSD->UEP3_CTRL_H) & USBFS_UEP_T_RES_MASK) == USBFS_UEP_T_RES_STALL) {
                                USBFS_EP0_4Buf[0] = 0x01;
                            }
                            break;

                        case (DEF_UEP_OUT | DEF_UEP5):
                            if (((USBFSD->UEP5_CTRL_H) & USBFS_UEP_R_RES_MASK) == USBFS_UEP_R_RES_STALL) {
                                USBFS_EP0_4Buf[0] = 0x01;
                            }
                            break;
#endif

                        default:
                            errflag = 0xFF;
                            break;
                        }
                    } else {
                        errflag = 0xFF;
                    }

                    if (USBFS_SetupReqLen > 2) {
                        USBFS_SetupReqLen = 2;
                    }

                    break;

                default:
                    errflag = 0xFF;
                    break;
                }
            }
            /* errflag = 0xFF means a request not support or some errors occurred, else correct */
            if (errflag == 0xff) {
                /* if one request not support, return stall */
                USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
            } else {
                /* end-point 0 data Tx/Rx */
                if (USBFS_SetupReqType & DEF_UEP_IN) {
                    /* tx */
                    len = (USBFS_SetupReqLen > DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                    USBFS_SetupReqLen -= len;
                    USBFSD->UEP0_TX_LEN = len;
                    USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                } else {
                    /* rx */
                    if (USBFS_SetupReqLen == 0) {
                        USBFSD->UEP0_TX_LEN = 0;
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    } else {
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                    }
                }
            }
            break;

        default:
            break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    } else if (intflag & USBFS_UIF_BUS_RST) {
        /* usb reset interrupt processing */
        USBFS_DevConfig = 0;
        USBFS_DevAddr = 0;
        USBFS_DevSleepStatus = 0;
        USBFS_DevEnumStatus = 0;

        USBFSD->DEV_ADDR = 0;
        USBFS_Device_Endp_Init();
        /* The device stack just reset every endpoint register; the queue layer
         * state (indices / idle flags) must be invalidated too, otherwise the
         * first command after re-enumeration may be written into a slot that is
         * logically already dequeued. */
        USBQueue_StatusReset();
        USBFSD->UEP1_CTRL_H = USBFS_UEP_R_RES_ACK;  // Explicitly re-arm EP1 OUT
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
        // Notify tasks (handles are NULL until the scheduler has created them)
        if (taskHandleDAP) {
            xTaskNotifyFromISR (taskHandleDAP, 0x00010000UL, eSetBits, &taskWoken);
        }
#if DAP_WITH_CDC
        if (taskHandleSER) {
            xTaskNotifyFromISR (taskHandleSER, 0x00010000UL, eSetBits, &taskWoken);
        }
#endif
        if (taskHandleLED) {
            xTaskNotifyFromISR (taskHandleLED, 0x32, eSetValueWithOverwrite, NULL);  // LED: Yellow 1Hz
        }
    } else if (intflag & USBFS_UIF_SUSPEND) {
        /* usb suspend interrupt processing */
        if (USBFSD->MIS_ST & USBFS_UMS_SUSPEND) {
            USBFS_DevSleepStatus |= 0x02;
            // Notify tasks
            if (taskHandleDAP) {
                xTaskNotifyFromISR (taskHandleDAP, 0x00020000UL, eSetBits, &taskWoken);
            }
#if DAP_WITH_CDC
            if (taskHandleSER) {
                xTaskNotifyFromISR (taskHandleSER, 0x00020000UL, eSetBits, &taskWoken);
            }
#endif
            if (taskHandleLED) {
                xTaskNotifyFromISR (taskHandleLED, 0x32, eSetValueWithOverwrite, NULL);  // LED: Yellow 1Hz
            }
            if (USBFS_DevSleepStatus == 0x03) {
                /* Handling usb sleep here */
            }
        } else {
            USBFS_DevSleepStatus &= ~0x02;
        }
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
    } else {
        /* other interrupts */
        USBFSD->INT_FG = intflag;
    }
    portYIELD_FROM_ISR (taskWoken);
    FREE_INT_SP();
}
