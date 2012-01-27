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

CONFIG_PIERREFS_FS = m
PIERREFS_DEF_CONFIG = -UCONFIG_PIERREFS
include config.mk
export CONFIG_PIERREFS_FS

EXTRA_CFLAGS := -I${CURDIR}/include
EXTRA_CFLAGS += ${AUFS_DEF_CONFIG}

MakeMod = ${MAKE} -C ${KDIR} M=${CURDIR}/fs/pierrefs EXTRA_CFLAGS="${EXTRA_CFLAGS}"

all: pierrefs.ko usr/include/linux/pierrefs_type.h

clean:
	${MakeMod} $@
	find . -type f -name '*~' | xargs -r ${RM}
	${RM} -r pierrefs.ko usr

install: fs/pierrefs/pierrefs.ko
	${MakeMod} modules_install

install_header install_headers: usr/include/linux/pierrefs_type.h
	install -o root -g root -p usr/include/linux/pierrefs_type.h \
		${DESTDIR}/usr/include/linux

pierrefs.ko: fs/pierrefs/pierrefs.ko
	ln -f $< $@

fs/pierrefs/pierrefs.ko:
	@echo ${EXTRA_CFLAGS}
	${MakeMod} modules

usr/include/linux/pierrefs_type.h: d = $(shell echo ${CURDIR} | cut -c2-)
usr/include/linux/pierrefs_type.h:
	echo '$$(install-file):srctree= $$(install-file):objtree=' |\
	tr ' ' '\n' |\
	${MAKE} -rR -C ${KDIR} \
		-f scripts/Makefile.headersinst \
		-f - \
		-f Makefile \
		obj=${d}/include/linux dst=${d}/usr/include/linux
	test -s $@
