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

/**
 * Maximum time in microseconds to wait for PIOC to complete an SWD transfer.
 * If the PIOC does not assert RB_DATA_SW_MR within this window, a deadlock is
 * assumed, the PIOC core is reset and restarted, and DAP_TRANSFER_FAULT is
 * returned so the host can recover.  Override in your build system if needed.
 *
 * Worst-case SWD transaction at 1 kHz clock:
 *   1 (start) + 3 (APnDP/RnW/A[2:3]) + 1 (stop) + 1 (park)
 *   + 3 (turnaround) + 3 (ACK) + 32 (data) + 1 (parity)
 *   + 3 (turnaround) + 255 (max idle cycles) ≈ 303 bits → ~303 ms.
 * 1000 ms provides a comfortable safety margin while still catching hangs.
 */
#ifndef PIOC_DEADLOCK_TIMEOUT_US
#define PIOC_DEADLOCK_TIMEOUT_US  1000000U
#endif

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
extern void PIOC_DAP_WriteSFR(uint16_t index, uint8_t value);
extern void PIOC_DAP_PutData (uint8_t *data, uint16_t len);
extern void PIOC_DAP_LoadCfg(void);
extern int PIOC_DAP_GetItFlag(void);

extern  uint8_t SWD_Transfer(uint32_t request,uint32_t *data);

#endif /* __DAP_PIOC_H__ */