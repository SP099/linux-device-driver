# This is a standard, reusable Makefile for building Linux kernel modules.
# It supports:
#   - Native build (default - on your development machine)
#   - Cross-compilation (for ARM, etc.)
#
# How to use:
#   make          -> Build for current host
#   make cross    -> Build for cross-compile target (ARM/RPi)
#   make clean    -> Clean build artifacts
# =============================================================================

# ====================== Module Configuration ======================
# Name of your kernel module (without .ko)
# Must match your .c filename (e.g., pcdrv_single_device.c → pcdrv_single_device.o)
obj-m += pcd_device_setup.o pcd_platform_driver.o

# ====================== Kernel Build Directory ======================
# Default: Use host kernel headers
KDIR ?= /lib/modules/$(shell uname -r)/build/

# ====================== Cross-Compilation Settings ======================
# These are used only when you run "make cross"
# Change these according to your target board

ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
KDIR_CROSS ?= /home/sp/linux_kernel_foundation/rpi/linux/

# ====================== Build Targets ======================

# Default target: Native build for current machine
all: host

# ---------------------------------------------------------------------
# Native Build (Recommended for learning and testing)
#
# Uses the running kernel's build system.
# This is the simplest and most common way to build modules.
# ---------------------------------------------------------------------
host:
	@echo "=== Building Kernel Module for Host ==="
	make -C $(KDIR) M=$(PWD) modules
	@echo "Build successful!"

# ---------------------------------------------------------------------
# Cross-Compilation Target
#
# Use this when you want to build for Raspberry Pi or other ARM boards.
# ---------------------------------------------------------------------
cross:
	@echo "=== Building for Cross-Compilation (ARCH=$(ARCH)) ==="
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KDIR_CROSS) M=$(PWD) modules
	@echo "Cross-compile successful!"

# ====================== Cleanup ======================
clean:
	@echo "=== Cleaning build files ==="
	make -C $(KDIR) M=$(PWD) clean
	@echo "Optional: Cleaning cross-compile artifacts..."
	-make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KDIR_CROSS) M=$(PWD) clean 2>/dev/null || true
	@echo "Clean completed."

# ====================== Help ======================
help:
	@echo "=== Makefile Help ==="
	@echo "Usage:"
	@echo "  make              : Build for current host (default)"
	@echo "  make host         : Build for current host"
	@echo "  make cross        : Cross-compile (ARM/Raspberry Pi)"
	@echo "  make clean        : Remove generated files"
	@echo "  make help         : Show this help"
	@echo ""
	@echo "Configuration:"
	@echo "  Edit KDIR_CROSS and CROSS_COMPILE variables for your setup"

# Mark targets as not being files
.PHONY: all host cross clean help
