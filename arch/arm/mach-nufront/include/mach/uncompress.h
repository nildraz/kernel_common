/*
 *
 *  Copyright (C) 2010 NUFRONT Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/mach-types.h>

#define NPSC_UART0_BASE                0x06100000 /* UART 0 64K */
#define UART_0_LSR (*(volatile unsigned int*)(NPSC_UART0_BASE + 0x14))
#define UART_0_THR (*(volatile unsigned int*)(NPSC_UART0_BASE + 0x00))

#define UART_LSR_DR     0x01            /* Data ready */
#define UART_LSR_OE     0x02            /* Overrun */
#define UART_LSR_PE     0x04            /* Parity error */
#define UART_LSR_FE     0x08            /* Framing error */
#define UART_LSR_BI     0x10            /* Break */
#define UART_LSR_THRE   0x20            /* Xmit holding register empty */
#define UART_LSR_TEMT   0x40            /* Xmitter empty */
#define UART_LSR_ERR    0x80            /* Error */

/*
 * This does not append a newline
 */
static inline void putc(int data)
{
	while ((UART_0_LSR & UART_LSR_THRE) == 0)
		;
	UART_0_THR = data;
}

static inline void flush(void)
{
	while ((UART_0_LSR & UART_LSR_TEMT) == 0)
		;
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
