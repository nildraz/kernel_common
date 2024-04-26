#
# Copyright (C) 2014 Nufront Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation version 2.
#
# This program is distributed "as is" WITHOUT ANY WARRANTY of any
# kind, whether express or implied; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

obj-$(CONFIG_ARCH_NPSC01)	:= mach-npsc01.o
obj-$(CONFIG_PL330_DMA)     += dma.o
obj-$(CONFIG_SMP)  += headsmp.o platsmp.o
obj-$(CONFIG_CPU_IDLE)	+= cpuidle.o
obj-$(CONFIG_MFD_SYSCON)        += nufront-apbmisc.o
obj-y               += nufront-dbg.o
obj-$(CONFIG_PM) += n7s3.o sleep.o
