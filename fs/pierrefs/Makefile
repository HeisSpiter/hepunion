ifeq (${CONFIG_PIERREFS_FS},m)
include ${src}/conf.mk
endif
-include ${src}/priv_def.mk

# cf. include/linux/kernel.h
# enable pr_debug
ccflags-y += -DDEBUG
# sparse doesn't allow spaces
ccflags-y += -D'pr_fmt(fmt)=PIERREFS_NAME"\040%s:%d:%s[%d]:\040"fmt,__func__,__LINE__,current->comm,current->pid'

obj-$(CONFIG_PIERREFS_FS) += pierrefs.o
pierrefs-y := cow.o helpers.o main.o opts.o me.o wh.o

# all are boolean

