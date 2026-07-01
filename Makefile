# Project Name
TARGET = daisy_tdm_hexa

# https://electro-smith.github.io/libDaisy/md_doc_2md_2__a7___getting-_started-_daisy-_bootloader.html
# APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = 	src/main.cpp \
             	$(wildcard src/EffetEarth/Earth.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/*.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/dsp/delays/*.cpp) \
              	$(wildcard src/EffetEarth/Dattorro/dsp/filters/*.cpp) \


# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR = lib/DaisySP

CPP_STANDARD = -std=gnu++20

C_INCLUDES += -Ilib/Q/q_lib/include \
              -Ilib/Q/infra/include \
              -Ilib/gcem/include \
			  -Isrc


OPT = -O3 -ffast-math

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

