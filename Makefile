KDIR = /lib/modules/$(shell uname -r)/build
Conf1=${KDIR}/include/config/auto.conf
Conf2=${KDIR}/.config
ifeq "t" "$(shell test -e ${Conf1} && echo t)"
include ${Conf1}
else ifeq "t" "$(shell test -e ${Conf2} && echo t)"
include ${Conf2}
else
$(warning could not find kernel config file. internal auto-config may fail)
endif

CONFIG_HEPUNION_FS = m
HEPUNION_DEF_CONFIG = -UCONFIG_HEPUNION
include config.mk
export CONFIG_HEPUNION_FS

EXTRA_CFLAGS := -I${CURDIR}/include
EXTRA_CFLAGS += ${HEPUNION_DEF_CONFIG}

MakeMod = ${MAKE} -C ${KDIR} M=${CURDIR}/fs/hepunion EXTRA_CFLAGS="${EXTRA_CFLAGS}"

all: hepunion.ko include/linux/hepunion_type.h

clean:
	${MakeMod} $@
	find . -type f -name '*~' | xargs -r ${RM}
	${RM} -r hepunion.ko usr
	${MAKE} -C tests clean

install: fs/hepunion/hepunion.ko
	${MakeMod} modules_install

install_header install_headers: usr/include/linux/hepunion_type.h
	install -o root -g root -p usr/include/linux/hepunion_type.h \
		${DESTDIR}/usr/include/linux

hepunion.ko: fs/hepunion/hepunion.ko
	ln -f $< $@

fs/hepunion/hepunion.ko:
	@echo ${EXTRA_CFLAGS}
	${MakeMod} modules

usr/include/linux/hepunion_type.h: d = $(shell echo ${CURDIR} | cut -c2-)
usr/include/linux/hepunion_type.h:
	echo '$$(install-file):srctree= $$(install-file):objtree=' |\
	tr ' ' '\n' |\
	${MAKE} -rR -C ${KDIR} \
		-f scripts/Makefile.headersinst \
		-f - \
		-f Makefile \
		obj=${d}/include/linux dst=${d}/usr/include/linux
	test -s $@

tests:
	${MAKE} -C tests

.PHONY: tests
