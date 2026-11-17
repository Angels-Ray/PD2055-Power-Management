VERSION := $(shell sed -n 's/^version=//p' module.prop)
ZIG ?= $(HOME)/opt/zig/zig
ADB ?= adb
CC ?= cc
CFLAGS ?= -O2 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
VERSION_H := build/version.h

all: build/pdcharge

$(VERSION_H): module.prop Makefile
	@mkdir -p build
	@printf '#define PD_VERSION "%s"\n' '$(VERSION)' > $@

build/pdcharge-host: $(SRC) $(HDR) $(VERSION_H)
	$(CC) $(CFLAGS) -include $(VERSION_H) -o $@ $(SRC)

build/pdcharge: $(SRC) $(HDR) $(VERSION_H)
	$(ZIG) cc -target aarch64-linux-musl $(CFLAGS) -static -s \
		-include $(VERSION_H) -o $@ $(SRC)

kmod:
	bash src/kmod/build.sh

build/psset.ko: src/kmod/psset.c src/kmod/pd2055_cpufreq.c src/kmod/build.sh
	bash src/kmod/build.sh

app:
	$(MAKE) -C app

package: build/pdcharge kmod app
	rm -rf build/module
	mkdir -p build/module/bin build/module/kmod build/module/app
	cp module.prop customize.sh service.sh uninstall.sh action.sh config.conf build/module/
	cp build/pdcharge build/module/bin/pdcharge
	cp build/psset.ko build/pd2055_cpufreq.ko build/module/kmod/
	cp build/PD2055PowerManagement.apk build/module/app/PD2055PowerManagement.apk
	chmod 0755 build/module/*.sh build/module/bin/pdcharge
	cd build/module && zip -qr ../PD2055-Power-Management-$(VERSION).zip .
	@echo build/PD2055-Power-Management-$(VERSION).zip

build/drain-libs/libEGL.so:
	@mkdir -p build/drain-libs
	$(ADB) pull /system/lib64/libEGL.so $@

build/drain-libs/libGLESv3.so:
	@mkdir -p build/drain-libs
	$(ADB) pull /system/lib64/libGLESv3.so $@

build/drain: tools/drain/drain.c build/drain-libs/libEGL.so build/drain-libs/libGLESv3.so
	@mkdir -p build
	$(ZIG) build-exe -target aarch64-linux-android -O ReleaseSmall \
		--dynamic-linker /system/bin/linker64 -fentry=_start \
		-femit-bin=$@ tools/drain/drain.c \
		build/drain-libs/libEGL.so build/drain-libs/libGLESv3.so

drain: build/drain

clean:
	rm -rf build app/build

.PHONY: all app package clean drain kmod
