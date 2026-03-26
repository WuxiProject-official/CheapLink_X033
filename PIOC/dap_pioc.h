/*
 *  PIOC DAP accelerate implementation
 *  Copyright (C) 2022-2026  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef __DAP_PIOC_H__
#define __DAP_PIOC_H__

#include <stdint.h>

typedef enum {
    PIOC_DAP_SWDTRANSFER = 0x01,
    PIOC_DAP_SWJSEQ = 0x02,
} PIOC_DAPCmdType_t;

extern void PIOC_DAP_Init (void);
extern void PIOC_DAP_Reset (void);
extern void PIOC_DAP_Run (void);
extern void PIOC_DAP_Halt(void);
extern void PIOC_DAP_Cmd(PIOC_DAPCmdType_t cmd, uint8_t arg);
extern uint8_t PIOC_DAP_ReadCmdResult(void);
extern void PIOC_DAP_LoadCfg(void);
extern int PIOC_DAP_GetItFlag(void);

extern  uint8_t SWD_Transfer(uint32_t request,uint32_t *data);

#endif /* __DAP_PIOC_H__ */