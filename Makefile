TC32_GCC := ../tc32-vendor/bin/tc32-elf-gcc
TC32_OBJDUMP := ../tc32-vendor/bin/tc32-elf-objdump
TC32_NM := ../tc32-vendor/bin/tc32-elf-nm

CFLAGS := -O2 -fomit-frame-pointer -finline-functions -finline-small-functions -Wall -Wextra -std=gnu99 \
	-I../tc_ble_single_sdk/drivers/B85 \
	-I../tc_ble_single_sdk/drivers/B85/lib/include \
	-I../tc_ble_single_sdk/common \
	-I../tc_ble_single_sdk/common/usb \
	-I../tc_ble_single_sdk/mcu \
	-I../tc_ble_single_sdk

SRC := aes.c analog.c audio.c bsp.c clock.c efuse.c emi.c gpio.c i2c.c pm.c pm_32k_rc.c pm_32k_xtal.c random.c rf_drv.c spi.c timer.c uart.c usbhw.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean compare compare-all symbols

all: $(OBJ)

%.o: %.c
	$(TC32_GCC) $(CFLAGS) -c $< -o $@

symbols: all
	@mkdir -p reports
	@for o in $(OBJ); do \
		base=$${o%.o}; \
		$(TC32_NM) --defined-only $$o > reports/$$base.nm.txt; \
	done

compare: all
	@mkdir -p reports
	@for o in $(OBJ); do \
		base=$${o%.o}; \
		$(TC32_OBJDUMP) -drwC ../drivers/$$base.o > reports/$$base.orig.objdump.txt; \
		$(TC32_OBJDUMP) -drwC $$o > reports/$$base.recon.objdump.txt; \
		diff -u reports/$$base.orig.objdump.txt reports/$$base.recon.objdump.txt > reports/$$base.diff || true; \
	done

compare-all: compare symbols

clean:
	rm -f $(OBJ)
	rm -f reports/*.diff reports/*.objdump.txt reports/*.nm.txt
