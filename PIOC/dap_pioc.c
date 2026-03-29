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

#include "dap_pioc.h"
#include "ch32x035.h"
#include "PIOC_SFR.h"
#include "string.h"

#if USE_PIOC_ACC

__attribute__ ((aligned (16))) static const uint8_t PIOC_CODE[] =
#include "PIOC_SW-DP_inc.h"

void PIOC_DAP_Init (void) {
    GPIO_InitTypeDef GPIO_InitStructure =
        {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_18 | GPIO_Pin_19;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init (GPIOC, &GPIO_InitStructure);
    memcpy ((uint8_t *)(PIOC_SRAM_BASE), PIOC_CODE, sizeof (PIOC_CODE));
    R8_SYS_CFG |= RB_MST_RESET;                  // reset PIOC
    R8_SYS_CFG = RB_MST_IO_EN1 | RB_MST_IO_EN0;  // enable IO0&IO1
}

void PIOC_DAP_Reset (void) {
    R8_SYS_CFG |= RB_MST_RESET;     // reset PIOC
    R8_SYS_CFG &= (~RB_MST_RESET);  // reset PIOC
}

void PIOC_DAP_Run (void) {
    R8_SYS_CFG |= RB_MST_CLK_GATE;
}

void PIOC_DAP_Halt (void) {
    R8_SYS_CFG &= (~RB_MST_CLK_GATE);
}

inline void PIOC_DAP_Cmd (PIOC_DAPCmdType_t cmd, uint8_t cmd_arg) {
    // SFR_DATA_REG3 is used for command type
    R8_DATA_REG3 = (uint8_t)cmd;
    // SFR_DATA_REG4 is used for command argument
    R8_DATA_REG4 = cmd_arg;
}

uint8_t PIOC_DAP_ReadCmdResult (void) {
    return R8_DATA_REG5;  // SFR_DATA_REG5 is used for command result
}

inline void PIOC_DAP_WriteSFR (uint16_t index, uint8_t value) {
    (*((volatile unsigned char *)(PIOC_SFR_BASE + 0x20 + index))) = value;
}

void PIOC_DAP_PutData (uint8_t *data, uint16_t len) {
    // from PIOC_SRAM_BASE + 0x800, the next 2KB SRAM is used for data transfer
    memcpy ((uint8_t *)(PIOC_SRAM_BASE + 0x800), data, len);
}

#include "DAP_config.h"
#include "DAP.h"
extern volatile uint8_t DAP_PIOC_ClockDelay[2];

void PIOC_DAP_LoadCfg (void) {
    // SFR_DATA_REG0 is used for DAP turnaround cycle
    R8_DATA_REG0 = DAP_Data.swd_conf.turnaround;
    // SFR_DATA_REG1 is used for DAP idle cycle
    R8_DATA_REG1 = DAP_Data.transfer.idle_cycles;
    // SFR_DATA_REG2 is used for DAP data phase status
    R8_DATA_REG2 = DAP_Data.swd_conf.data_phase;
    // SFR_DATA_REG6/7 is used for DAP clock delay
    R8_DATA_REG6 = DAP_PIOC_ClockDelay[0];
    R8_DATA_REG7 = DAP_PIOC_ClockDelay[1];
}

int PIOC_DAP_GetItFlag (void) {
    // SFR_DATA_REG0 is used for DAP interrupt flag
    return (R8_SYS_CFG & RB_INT_REQ) != 0;
}

// SWD Transfer I/O
//   request: A[3:2] RnW APnDP
//   data:    DATA[31:0]
//   return:  ACK[2:0]
uint8_t SWD_Transfer (uint32_t request, uint32_t *data) {
    uint32_t tmp;
    // Enable PIOC GPIOs
    tmp = GPIOC->CFGXR;
    tmp &= ~(0x0000FF00UL);  // AF_PP
    tmp |= 0x00009900UL;
    GPIOC->CFGXR = tmp;
    // Disable MCU GPIOs
    GPIOA->BSHR = (GPIO_Pin_5 | GPIO_Pin_7);  // IPU
    tmp = GPIOA->CFGLR;
    tmp &= ~(0xF0F00000UL);                   // IPU
    tmp |= 0x80800000UL;
    GPIOA->CFGLR = tmp;
    // PIOC_DAP_LoadCfg();
    if (!(request & DAP_TRANSFER_RnW)) {
        // write transfer
        R32_DATA_REG16_19 = *data;
    }
    PIOC_DAP_Cmd (PIOC_DAP_SWDTRANSFER, (uint8_t)request);
    (void)R8_CTRL_RD;  // Clear flag
    // Set SFR_CTRL_WR to start PIOC
    if (DAP_Data.fast_clock != 0) {
        // for fast clock, set SFR_CTRL_WR[7]=1
        R8_CTRL_WR |= 0x80;
    } else {
        R8_CTRL_WR &= 0x7F;
    }
    // at this time PIOC is running, wait for transfer complete
    while ((R8_SYS_CFG & RB_DATA_SW_MR) == 0);
    uint8_t ack = R8_CTRL_RD;  // get ACK from SFR_CTRL_RD
    if (request & DAP_TRANSFER_RnW) {
        // read transfer
        *data = R32_DATA_REG16_19;
    }
    // Enable MCU GPIOs
    tmp = GPIOA->CFGLR;
    tmp &= ~(0xF0F00000UL);  // OPP
    tmp |= 0x10100000UL;
    GPIOA->CFGLR = tmp;
    // Disable PIOC GPIOs
    GPIOC->BSXR = (GPIO_Pin_18 | GPIO_Pin_19) >> 16;  // IPU
    tmp = GPIOC->CFGXR;
    tmp &= ~(0x0000FF00UL);                           // IPU
    tmp |= 0x00008800UL;
    GPIOC->CFGXR = tmp;
    return ack;
}

__attribute__ ((interrupt())) void PIOC_IRQHandler (void) {
    if ((R8_SYS_CFG & RB_INT_REQ) != RESET) {  // currently only used for testdomain timer
        DAP_Data.timestamp = TIMESTAMP_GET();
    }
}

#endif
