#ARMADEUS_BASE_DIR=../../../..$(ARMADEUS_ROOTFS_DIR)
#-include $(ARMADEUS_BASE_DIR)/Makefile.in

CC:=gcc
#CC:=gcc pci_debug.c -o pci_debug -lreadline -lcurses

CFLAGS = -Wall
LDFLAGS += -lreadline -lcurses
#INSTALL_DIR = /usr/bin/

default: pci_debug

pci_debug: pci_debug.c

