VPATH += $(OUTPUT)

OBJS_ASMS = $(notdir $(SRCS_ASMS:%.S=%.o))
OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_IEC = $(notdir $(SRCS_IEC:%.iec=%.o))
OBJS_NANO = $(notdir $(SRCS_NANO:%.nan=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.o))
OBJS_HTML = $(notdir $(SRCS_HTML:%.html=%.o))
OBJS_YAML = $(notdir $(SRCS_YAML:%.yaml=%.o))
OBJS_RBF = $(notdir $(SRCS_RBF:%.rbf=%.o))
OBJS_APP = $(notdir $(SRCS_APP:%.app=%.ao))
CHK_BIN  = $(notdir $(SRCS_BIN:%.bin=%.chk))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_ASMS) $(OBJS_C) $(OBJS_CC) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_6502) $(OBJS_BIN) $(OBJS_RBF) $(OBJS_APP) $(OBJS_HTML) $(OBJS_YAML)) 
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: clean all mem gitinfo

all: $(OUTPUT) gitinfo $(RESULT) $(FINAL)
	@echo Making all requires $(OUTPUT), $(RESULT) and $(FINAL)	

# T174 D3: the commit's version data live in ONE generated source, $(OUTPUT)/gitinfo.c, which is the only file that holds hash, tag, date or the version
# stamp. gitinfo.h is the same text on every commit (declarations and the APP_* names the sources use), so no source that includes it compiles again when
# the commit changes: a new commit recompiles gitinfo.c and links. Both are written beside and moved over, so a compile running in parallel (make -j) reads the
# old or the new file, never half of one (T174), and each is replaced only when its content differs, so an unchanged tree recompiles nothing. The content derives
# from the commit alone (no wall-clock minute, no host name - in a container that is a fresh id every run). gitinfo has to run before the compile targets
# (ci/trxapps.sh, tools/build_*.sh run it first and alone): gitinfo.c is a source of the link.
gitinfo:: | $(OUTPUT)
	@printf '%s\n' \
	  '/* Generated file, do not edit. Declarations only: the values are in gitinfo.c, so a new commit does not touch the sources that include this. */' \
	  '#ifdef __cplusplus' 'extern "C" {' '#endif' \
	  'extern const char app_version_tag[], app_version_branch[], app_version_date[], app_version_hash[], app_build_date[], app_build_machine[];' \
	  'const char *app_version_string(void);' \
	  'const char *app_host_version_string(void);' \
	  'const char *app_bundle_version_string(void);' \
	  '#ifdef __cplusplus' '}' '#endif' \
	  '#define APP_VERSION_TAG    app_version_tag' \
	  '#define APP_VERSION_BRANCH app_version_branch' \
	  '#define APP_VERSION_DATE   app_version_date' \
	  '#define APP_VERSION_HASH   app_version_hash' \
	  '#define APP_VERSION_STRING app_version_string()' \
	  '#define APP_HOST_VERSION_STRING app_host_version_string()' \
	  '#define APP_BUNDLE_VERSION_STRING app_bundle_version_string()' \
	  '#define APP_BUILD_DATE     app_build_date' \
	  '#define APP_BUILD_MACHINE  app_build_machine' >$(OUTPUT)/gitinfo.h.tmp
	@if cmp -s $(OUTPUT)/gitinfo.h.tmp $(OUTPUT)/gitinfo.h; then rm -f $(OUTPUT)/gitinfo.h.tmp; else mv -f $(OUTPUT)/gitinfo.h.tmp $(OUTPUT)/gitinfo.h; fi
	@echo '/* Generated file, do not edit. The version data of this commit (T174 D3). */' >$(OUTPUT)/gitinfo.c.tmp
	@echo '#include "gitinfo.h"' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_version_tag[]    = "'`git describe --tags`'";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_version_branch[] = "'`git describe --all`'";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_version_date[]   = "'`git log -n 1 --format=%ai`'";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_version_hash[]   = "'`git rev-parse --short HEAD`'";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_build_date[]     = "'`git log -n 1 --format=%cd --date=format:'%F %R'`'";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char app_build_machine[]  = "trx";' >>$(OUTPUT)/gitinfo.c.tmp
	@echo '/* The version strings are stamp fields in the image (T172, T221): the own one of the application (VERSION_COMPONENT of its Makefile: hostapp or coreapp), the host gateware and the bundle. ci/package.sh patches them per commit (tools/stamp_version.py), so a cached app build shows the versions of the bundle it ships in, not of the build that filled the cache. */' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char *app_version_string(void) { static const volatile char f[64] = "TRXVSTAMP:'`"$$(git rev-parse --show-toplevel)/ci/version.sh" $(VERSION_COMPONENT)`'"; return (const char *)f + 10; }' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char *app_host_version_string(void) { static const volatile char f[64] = "TRXHSTAMP:'`"$$(git rev-parse --show-toplevel)/ci/version.sh" host`'"; return (const char *)f + 10; }' >>$(OUTPUT)/gitinfo.c.tmp
	@echo 'const char *app_bundle_version_string(void) { static const volatile char f[64] = "TRXBSTAMP:'`"$$(git rev-parse --show-toplevel)/ci/version.sh" bundle`'"; return (const char *)f + 10; }' >>$(OUTPUT)/gitinfo.c.tmp
	@if cmp -s $(OUTPUT)/gitinfo.c.tmp $(OUTPUT)/gitinfo.c; then rm -f $(OUTPUT)/gitinfo.c.tmp; else mv -f $(OUTPUT)/gitinfo.c.tmp $(OUTPUT)/gitinfo.c; fi

mem: $(OUTPUT)/$(PRJ).mem

$(OUTPUT):
	@echo creating output directory $(OUTPUT)
	@mkdir -p $(OUTPUT)

$(RESULT):
	@echo creating result directory $(RESULT)
	@mkdir -p $(RESULT)
		
$(RESULT)/$(PRJ).a: $(OBJS_C)
	@echo Creating Archive $@
	$(AR) -rc "$@" $(ALL_OBJS)

$(RESULT)/$(PRJ).u2u: $(OUTPUT)/$(PRJ).out
	@echo Creating Updater Binary $@
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo Creating Binary $@
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).app: $(OUTPUT)/$(PRJ).shex
	@echo Creating Binary Application $@
	@$(HEX2BIN) -r $< $@

$(RESULT)/$(PRJ).cfw: $(OUTPUT)/$(PRJ).shex
	@echo Creating Binary Application Format 1 $@
	$(HEX2BIN) -r -F $(PRODFAMILY) -P $(PRODNAME) $< $@

$(RESULT)/$(PRJ).hex: $(OUTPUT)/$(PRJ).out
	@echo Creating Hex File for On Chip Memory $@
	@elf2hex --input=$< 0x0000 $(HEXLAST) --width=32 --little-endian-mem --create-lanes=0 --record=4 --output=$@ --base=$(HEXBASE) --end=$(HEXEND)

$(OUTPUT)/$(PRJ).shex: $(OUTPUT)/$(PRJ).out
	@echo Creating Hex File to extract startaddr $@
	@$(OBJCOPY) -O ihex  $< $@

#	@$(BIN2HEX) $< $@

%.chk: %.bin
	@echo Calculating checksum of $(<F) binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

%.chk: %.b
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $(OUTPUT)/$(<F) $(OUTPUT)/$(@F)

%.b: %.svf
	@echo Converting $(<F) to binary $(@F)..
	@$(SVFDUMP) $< >$(OUTPUT)/svf_dump.txt
	@mv dump.bin $(OUTPUT)/$@

%.o: %.bin
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.html
	@echo Converting $(<F) html to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.yaml
	@echo Converting $(<F) document to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.bin: %.srec
	@echo Converting $(<F) SREC to $(@F)..
	@$(OBJCOPY) -I srec -O binary $< $@

%.swp: %.rbf
	@echo Swapping FPGA Image $(<F)
	@$(SWAP) $< $(OUTPUT)/$@
	
# Essentially do a swapped copy of the rbf first to the output directory and then convert to elf..
%.o: %.rbf
	@echo Converting $(<F) FPGA Image to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(SWAP) $< $(OUTPUT)/$(@F)
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(OUTPUT)/$(@F) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.ao: %.app
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.65: %.tas
	@echo Assembling $<
	@$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

%.b: %.iec
	@echo Assembling IEC processor code $(<F)
	@python $(PARSE_IEC) $< $(OUTPUT)/$(@F) >python_out_iec

%.b: %.nan
	@echo Assembling Nano processor code $(<F)
	@python $(PARSE_NANO) $< $(OUTPUT)/$(@F) >python_out_nano

%.b: %.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@$(PROMGEN) -r $< $(OUTPUT)/$(@F) 

%.o: %.b
	@echo Converting binary $(<F) to .o
	@$(eval fn := $(OUTPUT)/$(<F))
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$(fn)))))
	@$(eval becomes := _$(subst .,_,$(subst /,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(fn) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.65
	@echo Converting binary $(<F) to .o
	@$(eval fn := $(OUTPUT)/$(<F))
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$(fn)))))
	@$(eval becomes := _$(subst .,_,$(subst /,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(fn) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end
    	
%.o: %.s
	@echo Assembling $(<F)
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.S
	@echo Assembling $(<F)
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c
	@echo Compiling $(<F)
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<
	@$(CC) $(COPTIONS) -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.o: %.cc
	@echo Compiling $(<F)
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<
	@$(CPP) $(CPPOPT) -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.cc
	@$(CPP) -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(RESULT)/$(PRJ).elf: $(OUTPUT)/$(PRJ).out $(RESULT)
	@echo Providing ELF.
	@cp $(OUTPUT)/$(PRJ).out $(RESULT)/$(PRJ).elf
	
$(OUTPUT)/$(PRJ).s00: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) -w $< $@ 1000000

$(OUTPUT)/$(PRJ).sim: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) -w $< $@ 1000000 $(SIMOFFSET) 

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) $< $@ 2048

$(OUTPUT)/$(PRJ).m32: $(RESULT)/$(PRJ).bin
	@echo Make mem 32...
	@$(MAKEMEM) -w $< $@ 2048

LINKMETHOD ?= gcc

include ../../../common/$(LINKMETHOD).mk

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

# A header named by an old .d file and deleted since (uistate.h, T192.3) must not stop an incremental build: -MP above writes an empty
# rule per header into every new .d; this catches the .d files written before it (integ-1 CI run 1481: "No rule to make target uistate.h").
%.h: ;

clean:
	@rm -rf $(OUTPUT) $(RESULT) $(OUTPUTDIRS) python*out*

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)

   
