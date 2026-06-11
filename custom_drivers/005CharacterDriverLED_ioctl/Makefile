# =============================================================================
# Makefile for building Linux kernel modules + user-space apps.
#
# Supports:
#   - Native build  (default - on your development machine)
#   - Cross-compilation (for ARM, Raspberry Pi, etc.)
#
# Usage:
#   make            → Build kernel module for host (default)
#   make host       → Build kernel module for host
#   make app        → Build user-space ioctl test application (host)
#   make cross      → Cross-compile kernel module (ARM/RPi)
#   make cross-app  → Cross-compile user-space application (ARM/RPi)
#   make clean      → Clean all build artifacts
#   make help       → Show this help
# =============================================================================

# ====================== Module Configuration ======================
# Name of your kernel module (without .ko)
# Must match your .c filename:
#   gpio_char_ioctl_driver.c → gpio_char_ioctl_driver.o → gpio_char_ioctl_driver.ko
obj-m += gpio_char_ioctl_driver.o

# ====================== User-Space App Configuration ======================
# Source file and output binary name for the ioctl test application
APP_SRC  := ioctlapp.c
APP_BIN  := ioctlapp

# Compiler for native build
CC       ?= gcc

# Compiler flags: enable warnings, add debug symbols
APP_CFLAGS := -Wall -Wextra -g

# ====================== Kernel Build Directory ======================
# Default: use the running host kernel's headers/build system
KDIR ?= /lib/modules/$(shell uname -r)/build/

# ====================== Cross-Compilation Settings ======================
# Used only when you run "make cross".
# Adjust these for your target board.
ARCH         ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
KDIR_CROSS   ?= /home/sp/linux_kernel_foundation/rpi/linux/

# ====================== Build Targets ======================

# Default target
all: host

# ---------------------------------------------------------------------
# Native Build
# Builds against the currently running kernel.
# Simplest and most common approach for learning/testing.
# ---------------------------------------------------------------------
host:
	@echo "=== Building Kernel Module for Host ==="
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo "Build successful!"

# ---------------------------------------------------------------------
# User-Space Application Build (Host)
# Compiles ioctlapp.c into a native binary using gcc.
# ---------------------------------------------------------------------
app:
	@echo "=== Building User-Space App (host) ==="
	$(CC) $(APP_CFLAGS) -o $(APP_BIN) $(APP_SRC)
	@echo "Built: $(APP_BIN)"

# ---------------------------------------------------------------------
# Cross-Compilation Target
# Use when targeting Raspberry Pi or other ARM boards.
# ---------------------------------------------------------------------
cross:
	@echo "=== Building for Cross-Compilation (ARCH=$(ARCH)) ==="
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KDIR_CROSS) M=$(PWD) modules
	@echo "Cross-compile successful!"

# ---------------------------------------------------------------------
# Cross-Compile User-Space Application
# Uses the same cross toolchain prefix as the kernel module.
# ---------------------------------------------------------------------
cross-app:
	@echo "=== Cross-Compiling User-Space App (ARCH=$(ARCH)) ==="
	$(CROSS_COMPILE)gcc $(APP_CFLAGS) -o $(APP_BIN) $(APP_SRC)
	@echo "Built: $(APP_BIN)"

# ====================== Cleanup ======================
clean:
	@echo "=== Cleaning kernel module build files ==="
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@echo "=== Cleaning user-space app ==="
	rm -f $(APP_BIN)
	@echo "Optional: Cleaning cross-compile artifacts..."
	-$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KDIR_CROSS) M=$(PWD) clean 2>/dev/null || true
	@echo "Clean completed."

# ====================== Help ======================
help:
	@echo "=== Makefile Help ==="
	@echo "Usage:"
	@echo "  make            : Build kernel module for host (default)"
	@echo "  make host       : Build kernel module for host"
	@echo "  make app        : Build user-space ioctl app (host)"
	@echo "  make cross      : Cross-compile kernel module (ARM/RPi)"
	@echo "  make cross-app  : Cross-compile user-space app (ARM/RPi)"
	@echo "  make clean      : Remove all generated files"
	@echo "  make help       : Show this help"
	@echo ""
	@echo "Configuration:"
	@echo "  Edit KDIR_CROSS and CROSS_COMPILE variables for your board"
	@echo "  Edit APP_SRC / APP_BIN to change the app source or output name"

# Mark targets as not being files
.PHONY: all host app cross cross-app clean help
