# Kconfig
# instead of setting 'n', leave it blank when you disable it.

define conf
ifdef $(1)
HEPUNION_DEF_CONFIG += -D$(1)
export $(1)
endif
endef
