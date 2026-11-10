VERSION := $(shell sed -n 's/^version=//p' module.prop)
ZIG ?= $(HOME)/opt/zig/zig
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

package: build/pdcharge
	rm -rf build/module
	mkdir -p build/module/bin
	cp module.prop customize.sh service.sh uninstall.sh action.sh config.conf build/module/
	cp build/pdcharge build/module/bin/pdcharge
	chmod 0755 build/module/*.sh build/module/bin/pdcharge
	cd build/module && zip -qr ../PD2055-Charge-$(VERSION).zip .
	@echo build/PD2055-Charge-$(VERSION).zip

clean:
	rm -rf build

.PHONY: all package clean
