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

#include "usb_desc.h"

/* Device Descriptor */
const uint8_t MyDevDescr[] =
    {
        0x12,        // bLength
        0x01,        // bDescriptorType (Device)
#if MSOS_DESC == 1
        0x00, 0x02,  // bcdUSB 2.00
#elif MSOS_DESC == 2
        0x10, 0x02,  // bcdUSB 2.10
#endif
        0x00,                                               // bDeviceClass
        0x00,                                               // bDeviceSubClass
        0x00,                                               // bDeviceProtocol
        DEF_USBD_UEP0_SIZE,                                 // bMaxPacketSize0 64
        (uint8_t)DEF_USB_VID, (uint8_t)(DEF_USB_VID >> 8),  // idVendor
        (uint8_t)DEF_USB_PID, (uint8_t)(DEF_USB_PID >> 8),  // idProduct
        0x10, 0x01,                                         // bcdDevice
        0x01,                                               // iManufacturer (String Index)
        0x02,                                               // iProduct (String Index)
        0x03,                                               // iSerialNumber (String Index)
        0x01,                                               // bNumConfigurations 1
};

/* Configuration Descriptor */
const uint8_t MyCfgDescr[] =
    {
        // 配置描述符
        0x09,      // 描述符大小
        0x02,      // 描述符类型（是配置描述符）
#if DAP_WITH_CDC
        98, 0x00,  // 配置总长度
        0x03,      // 配置接口数
#else
        32, 0x00,    // 配置总长度
        0x01,        // 配置接口数
#endif
        0x01,  // 配置值
        0x02,  // 配置字符串描述符索引
        0x80,  // 机供
        0xFA,  // 请求500mA

        // 接口描述符1
        0x09,  // 描述符大小
        0x04,  // 描述符类型（是接口描述符）
        0x00,  // 接口编号
        0x00,  // 接口替用设置（？）
        0x02,  // 扣除端点0后的端点数
        0xFF,  // 接口类（是Vendor）
        0x00,  // 自定义
        0x00,  // 自定义
        0x02,  // 接口字符串描述符索引

        // 端点描述符
        0x07,                       // 描述符大小
        0x05,                       // 描述符类型（是端点描述符）
        0x01,                       // 是EP1OUT
        0x02,                       // 是批量传输
        DEF_USBD_ENDP1_SIZE, 0x00,  // 端点大小
        0x00,                       // 无意义

        // 端点描述符
        0x07,                       // 描述符大小
        0x05,                       // 描述符类型（是端点描述符）
        0x82,                       // 是EP2IN
        0x02,                       // 是批量传输
        DEF_USBD_ENDP2_SIZE, 0x00,  // 端点大小
        0x00,                       // 无意义

#if DAP_WITH_CDC
               // IAD描述符
        0x08,  // 描述符大小
        0x0B,  // 描述符类型（是IAD描述符）
        0x01,  // 第一个被关联的接口号
        0x02,  // 共关联几个接口
        0x02,  // 接口类（是CDC）
        0x02,  // ACM类型
        0x01,  // AT协议（？）
        0x05,  // IAD字符串描述符索引

        // 接口描述符2
        0x09,  // 描述符大小
        0x04,  // 描述符类型（是接口描述符）
        0x01,  // 接口编号
        0x00,  // 接口替用设置（？）
        0x01,  // 扣除端点0后的端点数
        0x02,  // 接口类（是CDC）
        0x02,  // ACM类型
        0x01,  // AT协议
        0x00,  // 接口字符串描述符索引

        // CDC功能描述符
        0x05,        // 描述符大小
        0x24,        // 描述符类型（是CDC功能描述符）
        0x00,        // CDC描述符子类型：是功能描述符头
        0x10, 0x01,  // CDC版本1.1

        // CDC功能描述符
        0x05,  // 描述符大小
        0x24,  // 描述符类型（是CDC功能描述符）
        0x01,  // CDC描述符子类型：是呼叫管理描述符
        0x00,  // 设备不处理呼叫管理
        0x01,  // CDC-Data接口号

        // CDC功能描述符
        0x04,  // 描述符大小
        0x24,  // 描述符类型（是CDC功能描述符）
        0x02,  // CDC描述符子类型：是ACM描述符
        0x02,  // ？

        // CDC功能描述符
        0x05,  // 描述符大小
        0x24,  // 描述符类型（是CDC功能描述符）
        0x06,  // CDC描述符子类型：是联合功能描述符
        0x01,  // CDC-Comm接口号
        0x02,  // CDC-Data接口号

        // 端点描述符
        0x07,                       // 描述符大小
        0x05,                       // 描述符类型（是端点描述符）
        0x84,                       // 是EP4IN
        0x03,                       // 是中断传输
        DEF_USBD_ENDP4_SIZE, 0x00,  // 端点大小
        0x01,                       // 轮询周期1ms

        // 接口描述符3
        0x09,  // 描述符大小
        0x04,  // 描述符类型（是接口描述符）
        0x02,  // 接口编号
        0x00,  // 接口替用设置（？）
        0x02,  // 扣除端点0后的端点数
        0x0A,  // 接口类（是CDC-Data）
        0x00,  // 无
        0x00,  // 无
        0x00,  // 接口字符串描述符索引

        // 端点描述符
        0x07,                       // 描述符大小
        0x05,                       // 描述符类型（是端点描述符）
        0x05,                       // 是EP5OUT
        0x02,                       // 是批量传输
        DEF_USBD_ENDP3_SIZE, 0x00,  // 端点大小
        0x00,                       // 无意义

        // 端点描述符
        0x07,                       // 描述符大小
        0x05,                       // 描述符类型（是端点描述符）
        0x83,                       // 是EP3IN
        0x02,                       // 是批量传输
        DEF_USBD_ENDP3_SIZE, 0x00,  // 端点大小
        0x00,                       // 无意义
#endif
};

/* Language Descriptor */
const uint8_t MyLangDescr[] =
    {0x04, 0x03, 0x09, 0x04};

/* Manufacturer Descriptor */
const uint8_t MyManuInfo[] =
    {
        // Str desc: "WuxiProject"
        24,    // Desc length = 24
        0x03,  // Str type desc
        0x57, 0x00, 0x75, 0x00, 0x78, 0x00, 0x69, 0x00, 0x50, 0x00, 0x72, 0x00,
        0x6f, 0x00, 0x6a, 0x00, 0x65, 0x00, 0x63, 0x00, 0x74, 0x00};

/* Product Information */
const uint8_t MyProdInfo[] =
    {
        // Str desc: "CheapLink CMSIS-DAP"
        40,    // Desc length = 40
        0x03,  // Str type desc
        0x43, 0x00, 0x68, 0x00, 0x65, 0x00, 0x61, 0x00, 0x70, 0x00, 0x4c, 0x00,
        0x69, 0x00, 0x6e, 0x00, 0x6b, 0x00, 0x20, 0x00, 0x43, 0x00, 0x4d, 0x00,
        0x53, 0x00, 0x49, 0x00, 0x53, 0x00, 0x2d, 0x00, 0x44, 0x00, 0x41, 0x00,
        0x50, 0x00};

/* Serial Number Information */
uint8_t MySerNumInfo[] =
    {
        // Str desc: "TSHE-xxxxxxxx"
        28,    // Desc length = 28
        0x03,  // Str type desc
        0x54, 0x00, 0x53, 0x00, 0x48, 0x00, 0x45, 0x00, 0x2d, 0x00, 0x30, 0x00,
        0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00,
        0x30, 0x00};

#if DAP_WITH_CDC
const uint8_t StrDescCustom5[] =
    {
        // Str desc: "CheapLink USBCDC"
        34,    // Desc length = 34
        0x03,  // Str type desc
        0x43, 0x00, 0x68, 0x00, 0x65, 0x00, 0x61, 0x00, 0x70, 0x00, 0x4c, 0x00,
        0x69, 0x00, 0x6e, 0x00, 0x6b, 0x00, 0x20, 0x00, 0x55, 0x00, 0x53, 0x00,
        0x42, 0x00, 0x43, 0x00, 0x44, 0x00, 0x43, 0x00};
#endif

#if MSOS_DESC == 1
const uint8_t MsOs1Desc[] =
    {
        // Str desc: "MSFT100"
        18,    // Desc length = 18
        0x03,  // Str type desc
        0x4d, 0x00, 0x53, 0x00, 0x46, 0x00, 0x54, 0x00, 0x31, 0x00, 0x30, 0x00,
        0x30, 0x00, 0x11, 0x00};

const uint8_t WCID1Desc[] =
    {0x28, 0x00, 0x00,
     0x00,  // Length 40
     0x00,
     0x01,  // WCID1.0
     0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00,  // First Interface 0
     0x01,  // ?
     'W', 'I', 'N', 'U', 'S', 'B', 0x00,
     0x00,  // CompatibleID {'W', 'I', 'N', 'U', 'S', 'B', '\0', 0x00} for WINUSB
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00};

const uint8_t WCID1DescEx[] =
    {0x8e, 0x00, 0x00,
     0x00,  // Length 142
     0x00,
     0x01,  // WCID1.0
     0x05, 0x00,
     0x01, 0x00,
     132, 0x00, 0x00, 0x00,
     0x01, 0x00, 0x00, 0x00,
     40,
     0x00,  // sizeof(struct MS_DEV_GUID_NAME)
     // MS_DEV_GUID_NAME
     'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I',
     0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, 'a', 0x00,
     'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 0, 0,
     78,
     0x00, 0x00, 0x00,  // sizeof (struct MS_DEV_INT_GUID)
     // MS_DEV_INT_GUID
     '{', 0, 'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0, 'D', 0,
     '-', 0, '2', 0, '9', 0, '3', 0, 'B', 0, '-', 0, '4', 0, '6', 0, '6', 0,
     '3', 0, '-', 0, 'A', 0, 'A', 0, '3', 0, '6', 0, '-', 0, '1', 0, 'A', 0,
     'A', 0, 'E', 0, '4', 0, '6', 0, '4', 0, '6', 0, '3', 0, '7', 0, '7', 0,
     '6', 0, '}', 0, 0, 0};
#elif MSOS_DESC == 2
const uint8_t MyBosDesc[] =
    {
        // BOS Base Descriptor
        0x05,        // bLength
        0x0f,        // bDescriptorType
        0x21, 0x00,  // wTotalLength
        0x01,        // bNumDeviceCaps

        // BOS Device Capability Descriptor
        0x1c,                                            // bLength
        0x10,                                            // bDescriptorType
        0x05,                                            // bDevCapabilityType
        0x00,                                            // bReserved
        0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,  // bPlatformCapabilityUUID_16
        0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,  // bPlatformCapabilityUUID_16
        0x00, 0x00, 0x03, 0x06,                          // dwWindowsVersion Win8.1+
#if DAP_WITH_CDC
        170, 0,                                          // wDescriptorSetTotalLength
#else
        162, 0,     // wDescriptorSetTotalLength
#endif
        0x01,                                            // bVendorCode
        0x00,                                            // bAltEnumCode
};

const uint8_t MyWinusbDesc[] =
    {
        // MSOS2.0 descriptor set descriptor
        0x0a, 0x00,             /* wLength */
        0x00, 0x00,             /* wDescriptorType */
        0x00, 0x00, 0x03, 0x06, /* dwWindowsVersion */
#if DAP_WITH_CDC
        0xaa, 0x00,             /* wDescriptorSetTotalLength */
        // MSOS2.0 function subset descriptor
        0x08, 0x00, /* wLength */
        0x02, 0x00, /* wDescriptorType */
        0x00,       /* bFirstInterface */
        0x00,       /* bReserved */
        0xa0, 0x00, /* wSubsetLength */
#else
        0xa2, 0x00, /* wDescriptorSetTotalLength */
#endif
        // MSOS2.0 compatible ID descriptor
        0x14, 0x00,                                     /* wLength */
        0x03, 0x00,                                     /* wDescriptorType */
        'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,       /* cCID_8 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* cSubCID_8 */
        // MSOS2.0 registry property descriptor
        0x84, 0x00, /* wLength */
        0x04, 0x00, /* wDescriptorType */
        0x07, 0x00, /* wPropertyDataType */
        0x2a, 0x00, /* wPropertyNameLength */
        /* DeviceInterfaceGUIDs */
        'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, /* wcPropertyName_21 */
        'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, /* wcPropertyName_21 */
        't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, /* wcPropertyName_21 */
        'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, /* wcPropertyName_21 */
        'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, /* wcPropertyName_21 */
        0x00, 0x00,                                 /* wcPropertyName_21 */
        0x50, 0x00,                                 /* wPropertyDataLength */
        /* {CDB3B5AD-293B-4663-AA36-1AAE46463776} */
        '{', 0x00, 'C', 0x00, 'D', 0x00, 'B', 0x00,   /* wcPropertyData_40 */
        '3', 0x00, 'B', 0x00, '5', 0x00, 'A', 0x00,   /* wcPropertyData_40 */
        'D', 0x00, '-', 0x00, '2', 0x00, '9', 0x00,   /* wcPropertyData_40 */
        '3', 0x00, 'B', 0x00, '-', 0x00, '4', 0x00,   /* wcPropertyData_40 */
        '6', 0x00, '6', 0x00, '3', 0x00, '-', 0x00,   /* wcPropertyData_40 */
        'A', 0x00, 'A', 0x00, '3', 0x00, '6', 0x00,   /* wcPropertyData_40 */
        '-', 0x00, '1', 0x00, 'A', 0x00, 'A', 0x00,   /* wcPropertyData_40 */
        'E', 0x00, '4', 0x00, '6', 0x00, '4', 0x00,   /* wcPropertyData_40 */
        '6', 0x00, '3', 0x00, '7', 0x00, '7', 0x00,   /* wcPropertyData_40 */
        '6', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00, /* wcPropertyData_40 */
};
#endif

#include "ch32x035_usb.h"

uint16_t __attribute__ ((noinline)) USB_GetDescBuf (volatile uint16_t USBFS_SetupReqValue, const uint8_t **pUSBFS_Descr, uint8_t *errflag) {
    uint16_t len = 0;
    switch ((uint8_t)(USBFS_SetupReqValue >> 8)) {
    /* get usb device descriptor */
    case USB_DESCR_TYP_DEVICE:
        *pUSBFS_Descr = MyDevDescr;
        len = DEF_USBD_DEVICE_DESC_LEN;
        break;

        /* get usb configuration descriptor */
    case USB_DESCR_TYP_CONFIG:
        *pUSBFS_Descr = MyCfgDescr;
        len = DEF_USBD_CONFIG_DESC_LEN;
        break;

        /* get usb string descriptor */
    case USB_DESCR_TYP_STRING:
        switch ((uint8_t)(USBFS_SetupReqValue & 0xFF)) {
        /* Descriptor 0, Language descriptor */
        case DEF_STRING_DESC_LANG:
            *pUSBFS_Descr = MyLangDescr;
            len = DEF_USBD_LANG_DESC_LEN;
            break;

            /* Descriptor 1, Manufacturers String descriptor */
        case DEF_STRING_DESC_MANU:
            *pUSBFS_Descr = MyManuInfo;
            len = DEF_USBD_MANU_DESC_LEN;
            break;

            /* Descriptor 2, Product String descriptor */
        case DEF_STRING_DESC_PROD:
            *pUSBFS_Descr = MyProdInfo;
            len = DEF_USBD_PROD_DESC_LEN;
            break;

            /* Descriptor 3, Serial-number String descriptor */
        case DEF_STRING_DESC_SERN:
            *pUSBFS_Descr = MySerNumInfo;
            len = DEF_USBD_SN_DESC_LEN;
            break;

#if DAP_WITH_CDC
        case 5:
            *pUSBFS_Descr = StrDescCustom5;
            len = StrDescCustom5[0];
            break;
#endif

#if MSOS_DESC == 1
        case 0xee:
            *pUSBFS_Descr = MsOs1Desc;
            len = MsOs1Desc[0];
            break;
#endif
        default:
            *errflag = 0xFF;
            break;
        }
        break;

#if MSOS_DESC == 2
    case USB_DESCR_TYP_BOS:
        // BOS desc
        *pUSBFS_Descr = MyBosDesc;
        len = DEF_USBD_BOS_DESC_LEN;
        break;
#endif

    default:
        *errflag = 0xFF;
        break;
    }
    return len;
}
