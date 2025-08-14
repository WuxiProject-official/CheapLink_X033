/*
 *  IRQHandler wrapper for firmware of CheapLink_X033
 *  Copyright (C) 2022-2025  WuxiProject
 *
 *  SPDX-License-Identifier: MPL-2.0
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

.extern USBFS_IRQHandler_C
.global USBFS_IRQHandler
.align 8
.section .highcode
.func
USBFS_IRQHandler:
	// Protect Caller-saved registers & switch to IRQ stack(main stack)
	addi	sp,sp,-112
	sw	x1,108(sp)
	sw	x5,104(sp)
	sw	x6,100(sp)
	sw	x7,96(sp)
	sw	x8,92(sp)
	sw	x9,88(sp)
	sw	x10,84(sp)
	sw	x11,80(sp)
	sw	x12,76(sp)
	sw	x13,72(sp)
	sw	x14,68(sp)
	sw	x15,64(sp)
	sw	x16,60(sp)
	sw	x17,56(sp)
	sw	x18,52(sp)
	sw	x19,48(sp)
	sw	x20,44(sp)
	sw	x21,40(sp)
	sw	x22,36(sp)
	sw	x23,32(sp)
	sw	x24,28(sp)
	sw	x25,24(sp)
	sw	x26,20(sp)
	sw	x27,16(sp)
	sw	x28,12(sp)
	sw	x29,8(sp)
	sw	x30,4(sp)
	sw	x31,0(sp)
	// Protect other registers here if required.
	csrrw   sp,mscratch,sp
    // addi    sp,sp,-4
    // csrr    t0,mscratch
    // sw  t0,0(sp)

	// Call wrapped real IRQ_Handler
	call	USBFS_IRQHandler_C

	// Switch back to task stack & restore Caller-saved registers
    // lw  t0,0(sp)
    // csrw    mscratch,t0
    // addi    sp,sp,4
	csrrw   sp,mscratch,sp
	// Restore other registers here if required.
	lw	x31,0(sp)
	lw	x30,4(sp)
	lw	x29,8(sp)
	lw	x28,12(sp)
	lw	x27,16(sp)
	lw	x26,20(sp)
	lw	x25,24(sp)
	lw	x24,28(sp)
	lw	x23,32(sp)
	lw	x22,36(sp)
	lw	x21,40(sp)
	lw	x20,44(sp)
	lw	x19,48(sp)
	lw	x18,52(sp)
	lw	x17,56(sp)
	lw	x16,60(sp)
	lw	x15,64(sp)
	lw	x14,68(sp)
	lw	x13,72(sp)
	lw	x12,76(sp)
	lw	x11,80(sp)
	lw	x10,84(sp)
	lw	x9,88(sp)
	lw	x8,92(sp)
	lw	x7,96(sp)
	lw	x6,100(sp)
	lw	x5,104(sp)
	lw	x1,108(sp)
	addi	sp,sp,112
	mret
.endfunc
