# Assembles third_party/hbmenu/bootstub.s into a flat binary.
WONDERFUL_TOOLCHAIN ?= /opt/wonderful
PREFIX := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-
OUT    ?= data/bootstub.bin
ELF    := third_party/hbmenu/build/bootstub.elf

$(OUT): third_party/hbmenu/bootstub.s tools/bootstub.mk
	mkdir -p third_party/hbmenu/build $(dir $(OUT))
	$(PREFIX)gcc -Wl,-Ttext=0 -x assembler-with-cpp -nostartfiles -nostdlib \
		third_party/hbmenu/bootstub.s -o $(ELF)
	$(PREFIX)objcopy -O binary $(ELF) $@
	@echo built $@: $$(stat -c %s $@) bytes
