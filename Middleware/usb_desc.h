/*
 *  USB Descriptors for firmware of CheapLink_X033
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef USER_USB_DESC_H_
#define USER_USB_DESC_H_

#include "debug.h"

/******************************************************************************/
/* global define */
/* file version */
#define DEF_FILE_VERSION             0x01
/* usb device info define  */
#define DEF_USB_VID                  0x0D28
#define DEF_USB_PID                  0x0204
/* USB device descriptor, device serial number(bcdDevice) */
#define DEF_IC_PRG_VER               DEF_FILE_VERSION

/******************************************************************************/
/* usb device endpoint size define */
#define DEF_USBD_UEP0_SIZE           64     /* usb hs/fs device end-point 0 size */
/* FS */
#define DEF_USBD_FS_PACK_SIZE        64     /* usb fs device max bluk/int pack size */
#define DEF_USBD_FS_ISO_PACK_SIZE    1023   /* usb fs device max iso pack size */
/* LS */
#define DEF_USBD_LS_UEP0_SIZE        8      /* usb ls device end-point 0 size */
#define DEF_USBD_LS_PACK_SIZE        64     /* usb ls device max int pack size */

/* Pack size */
#define DEF_USBD_ENDP1_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP2_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP3_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP4_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP5_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP6_SIZE          DEF_USBD_FS_PACK_SIZE
#define DEF_USBD_ENDP7_SIZE          DEF_USBD_FS_PACK_SIZE

/******************************************************************************/
/* usb device Descriptor length, length of usb descriptors, if one descriptor not
 * exists , set the length to 0  */
#define DEF_USBD_DEVICE_DESC_LEN     ((uint8_t)MyDevDescr[0])
#define DEF_USBD_CONFIG_DESC_LEN     ((uint16_t)MyCfgDescr[2] + (uint16_t)(MyCfgDescr[3] << 8))
#define DEF_USBD_REPORT_DESC_LEN     0
#define DEF_USBD_LANG_DESC_LEN       ((uint16_t)MyLangDescr[0])
#define DEF_USBD_MANU_DESC_LEN       ((uint16_t)MyManuInfo[0])
#define DEF_USBD_PROD_DESC_LEN       ((uint16_t)MyProdInfo[0])
#define DEF_USBD_SN_DESC_LEN         ((uint16_t)MySerNumInfo[0])
#define DEF_USBD_BOS_DESC_LEN        33

/******************************************************************************/
/* external variables */
extern const uint8_t MyDevDescr[];
extern const uint8_t MyCfgDescr[];
extern const uint8_t MyLangDescr[];
extern const uint8_t MyManuInfo[];
extern const uint8_t MyProdInfo[];
extern uint8_t MySerNumInfo[];
extern const uint8_t StrDescCustom5[];
extern const uint8_t MsOs1Desc[];
extern const uint8_t WCID1Desc[];
extern const uint8_t WCID1DescEx[];
extern const uint8_t MyBosDesc[];
extern const uint8_t MyWinusbDesc[];

extern uint16_t __attribute__ ((noinline)) USB_GetDescBuf (volatile uint16_t USBFS_SetupReqValue, const uint8_t **pUSBFS_Descr, uint8_t *errflag);

#endif /* USER_USB_DESC_H_ */
