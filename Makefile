# Project Name
TARGET = TDM_Daisy

CXXFLAGS = -Wall -g -Os

# https://electro-smith.github.io/libDaisy/md_doc_2md_2__a7___getting-_started-_daisy-_bootloader.html
# APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = src/main.cpp
CPP_SOURCES += src/include/WavHexaPlayer.cpp

# Inclusion du fichier d'encodage FatFs pour le support LFN
C_SOURCES += $(LIBDAISY_DIR)/Middlewares/Third_Party/FatFs/src/option/ccsbcs.c

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
