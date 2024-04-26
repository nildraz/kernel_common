# Makefile for Discretix CC63P Linux device driver

MODULE_NAME = cryptodev

obj-$(CONFIG_CRYPTO_DEV) := $(MODULE_NAME).o
$(MODULE_NAME)-objs := ioctl.o main.o cryptlib.o authenc.o zc.o util.o 

DX_ROM_VERSION ?= 0x03000009U
DX_CC_SRAM_SIZE ?= 4096

EXTRA_CFLAGS += -DDX_CC_REE #-DCOMP_IN_WQ
EXTRA_CFLAGS += -DDX_ROM_VERSION=$(DX_ROM_VERSION)
ifneq ($(DX_CC_SRAM_SIZE),)
EXTRA_CFLAGS += -DDX_CC_SRAM_SIZE=$(DX_CC_SRAM_SIZE)
endif
ifeq ($(DX_CONFIG_DISABLE_COHERENT_DMA_OPS),1)
EXTRA_CFLAGS += -DDISABLE_COHERENT_DMA_OPS
endif
ifeq ($(DX_FPGA_ENV),1)
EXTRA_CFLAGS += -DDX_FPGA_ENV
endif

ifeq ($(DX_CONFIG_TEST_48BIT_DMA_ADDR),1)
EXTRA_CFLAGS += -DDX_DMA_48BIT_SIM
endif

ifeq ($(DEBUG),1) # Remove optimization flags
EXTRA_CFLAGS += -DDX_DEBUG
define remove_o2
	CFLAGS_REMOVE_$(1) = -O2
endef
$(foreach obj,$(MODULE_NAME)-objs,$(eval $(call remove_o2,$(obj))))
EXTRA_CFLAGS += -DDEBUG -O1
endif

$(info $(if $(DEBUG),DEBUG=$(DEBUG)))

