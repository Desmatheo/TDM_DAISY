# Project Name
TARGET = daisy_tdm_hexa

# https://electro-smith.github.io/libDaisy/md_doc_2md_2__a7___getting-_started-_daisy-_bootloader.html
# APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = 	src/main.cpp \
             	$(wildcard src/EffetEarth/Earth.cpp) \
             	$(wildcard src/EffetDelay/Delay.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/*.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/dsp/delays/*.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/dsp/filters/*.cpp) \


# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR = lib/DaisySP

USE_DAISYSP_LGPL = 1

CPP_STANDARD = -std=gnu++20

C_INCLUDES += -Ilib/Q/q_lib/include \
              -Ilib/infra/include \
              -Ilib/gcem/include \
			  -Isrc


OPT = -O3 \
	-ffast-math \
	-flto 

APP_TYPE = BOOT_QSPI

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
