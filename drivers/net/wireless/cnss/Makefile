# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for CNSS platform driver
#

obj-$(CONFIG_CNSS) += cnss.o

cnss-$(CONFIG_CNSS_PCI)  += cnss_pci.o
cnss-$(CONFIG_CNSS_SDIO) += cnss_sdio.o
cnss-y      += cnss_common.o
obj-$(CONFIG_CNSS_LOGGER)       += logger/
