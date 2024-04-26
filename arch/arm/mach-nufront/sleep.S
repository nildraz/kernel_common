/**
 * low level PM for n7/n7t
 **************************************************************************************************
 * v0.1 2014-Nov-10 by <Li Le>
 * 	<1> Use idmap_pgd instead of temporary pgd, we can successfully jump into SARRAM;
 *	<2> Need set SYS_STAT to 3, or fw will goto fail path;
 *	<3> JTAG lost connection when reading DCTRL(0x050E005C);
 *		<3.1> NEON? no
 *		<3.2> VFP? no
 *		<3.3> L2C? no
 *		<3.4> SCU? no
 *		<3.5> Fixed this bug after keeping SoC default PRCM settings
 *	<4> SoC does NOT pull-down PWR_REQ when using fwbin?
 *		<4.1> code(WFI) after dead loop is optimized out due to
 *		      arm-none-eabi- tool chain bug;
 *	<5> SoC does NOT pull-down PWR_REQ when using kernel, even if WFI is executed?
 *		<5.1> fwbin is tested under UBOOT, PWR_REQ can be pull-ed down, but using
 *		      the same fwbin kernel fails to pull down PWR_REQ;
 *		<5.2> Keep *most* PRCM configs same as UBOOT, kernel still fails to pull-down
 *		      PWR_REQ, but SoC do resume flow quickly(just configurate POWER_KEY
 *		      as wakeup source, actually there's not any wakeup event generated)?
 *		      A1: Huge mistake due to S3_MODE corrupted by garbage default values;
 *		      A2:
 *
 *	<6> Kernel n7s3 driver failed to load fw into SARRAM?
 *		<6.1> Wakeup memory clock?
 *	<7> You can NOT do "bl" like instructions in jump_to_fw, cause @sp stays the same,
 *		but you do it "bl" like instructions in real world, "ldm sp, {}" will bite you;
 **************************************************************************************************
 * v0.2 2014-Dec by <Li Le>
 *	<8> Kernel resume to boot process?
 *		<8.1> L2C operations needed?
 *
 *	<9> Prefetch abort(0xFFFF000C) happens after resume?
 *		<9.1> ldmfd	sp!, {r4 - r11, pc}, while @pc is a invaild value
 *
 *	<10> printk(logbuf stuff) is NOT reliable to debug such low level crux, cause it's
 *		not a realtime tool, messages are stored into __log_buf firstly, and then spit out
 *		by uart, so you need LL_DEBUG(Early Printk)
 **************************************************************************************************
 * v1.0 2014-Dec-04
 *	<11> Verified under UP on NU7 platform, every thing works perfectly
 *	<12> System will fail to suspend or resume, if code is unaligned
 *		<11.1> Debugging code with single step, system can suspend and resume
 *		successfully, but fails to suspend if break ponints are removed
 *	<13> Resume stucks at romcode, but all inst are 0?
 *		<13.1> ROM clock is disabled;
 *	<14> Resume flow fails to jump to __n7s3_resume on some boards?
 *		<14.1> __n7s3_resume code is not match with entry address, even if I disable ddr
 *		auto-self-refresh;
 *		<14.2> Run fwbin on fail boards, test results show that ddr is buggy;
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/thread_info.h>
#include <asm/proc-fns.h>
#include "cp15.h"


#define SLEEP_SAVE_SIZE		15

	.text

.macro	virt_to_phys, virt, phys_offset, page_offset
	sub	\virt, \virt, \page_offset
	add	\virt, \virt, \phys_offset
.endm

/*
 *	NEVER EVER touch this file unless u TRUELY UNDERSTAND EVERY DETAILS
 *	r0 - suspend fn arg
 *	r4 - size of save block
 *	lr - exit entry for failure
 *
 *	|---------------|
 *	|	lr	|
 *	|     r11-r4	|
 *	|---------------|
 *	|		|
 *	|   save block	| --> 32
 *	|		|
 *	|---------------|
 *	|   resume fn	|
 *	|   virtual sp 	|
 *	|     idpgd	|
 *	|---------------| --> sp
 */
	.align	CONFIG_ARM_L1_CACHE_SHIFT
ENTRY(__n7s3_cpu_sleep)
	ldr	r9, [sp]
	ldmia	r0, {r5, r6, r11}
	virt_to_phys	r0, r5, r6

	@ldr	r11, =0xdead
	@cmp	r1, r11
	@.word	0x0afffffe
	@.word	0x1afffffe
	@.word	0xeafffffe

	dmb
	mrc	p15, 0, r1, c1, c0, 0
	bic	r1, r1, #0x4			@ SCTLR.C
	mcr 	p15, 0, r1, c1, c0, 0
	dmb
	isb
	dsb

	clrex
	mrc	p15, 0, r1, c1, c0, 1
	bic	r1, r1, #0x40			@ ACTLR.SMP
	mcr	p15, 0, r1, c1, c0, 1
	isb
	/*************************************
	 * another fresh new world
	 * dagerous after this line
	 * MUST do all what u want before this
	 ************************************/
	mcr	p15, 0, r9, c2, c0, 0   /*write r9 to 32-bit TTBR0*/
	isb

	adr	r1, reminder_loc
	ldm	r1, {r10}
	virt_to_phys	r10, r5, r6
	bx	r10
ENDPROC(__n7s3_cpu_sleep)
	.ltorg
	.align
reminder_loc:
	.long	jump_to_fw


//ENTRY(tegra_disable_clean_inv_dcache)
//	stmfd	sp!, {r0, r4-r5, r7, r9-r11, lr}
//	dmb					@ ensure ordering
//
//	/* Disable the D-cache */
//	mrc	p15, 0, r2, c1, c0, 0
//	bic	r2, r2, #CR_C
//	mcr	p15, 0, r2, c1, c0, 0
//	isb
//
//	/* Flush the D-cache */
//	bl	v7_flush_dcache_louis
//
//	/* Trun off coherency */
//	exit_smp r4, r5
//
//	ldmfd	sp!, {r0, r4-r5, r7, r9-r11, pc}
//ENDPROC(tegra_disable_clean_inv_dcache)
//
ENTRY(__n7s3_cpu_resume)
	clrex

	ldr	r0, =0x058016e0
	ldr	r0, [r0]
	ldr	r1, =0xdeaddead
	cmp	r0, r1
	.word	0x0afffffe

	bl v7_invalidate_l1

	ldr	r1, =0xC09
	ldr	r2, =0xFFF
	mrc	p15, 0, r0, c0, c0, 0		@ read midr
	mov	r0, r0, lsr #4
	and	r0, r0, r2
	cmp	r0, r1
	bne	common_way

common_way:
	mov	r0, r0

	b	cpu_resume
ENDPROC(__n7s3_cpu_resume)

	.align	CONFIG_ARM_L1_CACHE_SHIFT
	.pushsection	.idmap.text,"ax"
ENTRY(jump_to_fw)
#if 0
	ldr	r0, =sleep_save_block
	add	r3, sp, #(12 + 32)
	mov	r10, #SLEEP_SAVE_SIZE
	add	r2, r3, r10, lsl #2
	mov	r4, r3
	virt_to_phys	r0, r5, r6

1: 	ldr	r7, [r3], #4
	str	r7, [r0], #4
	teq	r2, r3
	bne	1b
#endif

	mrc  	p15, 0, r0, c1, c0, 0   	@ SCTLR
	bic  	r0, r0, #0xd           		@ disable CAM
	mcr  	p15, 0, r0, c1, c0, 0

	mov	r0, #0
	mcr     p15, 0, r0, c8, c7, 0   	@ invalidate whole TLB
	mcr     p15, 0, r0, c7, c5, 6   	@ invalidate BTC
	isb
	dsb
	mrc		p15, 0, r0, c0, c0, 0			@ read id reg

	mov	r0, #0
	mov	r2, #0
	mov	r3, #0
	mrc	p15, 4, r1, c15, c0, 0		@ CBAR
	movw	r2, #0x1000			@ GIC_DIST_IF
	movw	r3, #0x2000			@ GIC_CPU_IF
	str	r0, [r1, r2]
	str	r0, [r1, r3]
	mov pc, r11
//	bx	r11				@ fwbin
ENDPROC(jump_to_fw)
	.type	sleep_save_block, %object
sleep_save_block:
	.rept	SLEEP_SAVE_SIZE
	.long	0
	.endr
	.popsection
