PfConfStr = CONFIG_HEPUNION_FS=${CONFIG_HEPUNION_FS}

define PfConf
ifdef ${1}
PfConfStr += ${1}=${${1}}
endif
endef

PfConfAll =

$(foreach i, ${PfConfAll}, \
	$(eval $(call PfConf,CONFIG_HEPUNION_${i})))

PfConfName = ${obj}/conf.str
${PfConfName}.tmp: FORCE
	@echo ${PfConfStr} | tr ' ' '\n' | sed -e 's/^/"/' -e 's/$$/\\n"/' > $@
${PfConfName}: ${PfConfName}.tmp
	@diff -q $< $@ > /dev/null 2>&1 || { \
	echo '  GEN    ' $@; \
	cp -p $< $@; \
	}
FORCE:
clean-files += ${PfConfName} ${PfConfName}.tmp

-include ${srctree}/${src}/conf_priv.mk
