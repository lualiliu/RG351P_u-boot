#
# (C) Copyright 2007 Semihalf
#
# SPDX-License-Identifier:	GPL-2.0+
#

ifeq ($(ARCH),powerpc)
LOAD_ADDR = 0x40000
endif
ifeq ($(ARCH),arm)
LOAD_ADDR = 0x1000000
endif

# Resulting ELF and binary exectuables will be named demo and demo.bin
extra-y = demo

# Source files located in the examples/api directory
OBJ-y += crt0.o
OBJ-y += demo.o
OBJ-y += glue.o
OBJ-y += libgenwrap.o

# Source files which exist outside the examples/api directory
EXT_COBJ-y += lib/crc32.o
EXT_COBJ-y += lib/ctype.o
EXT_COBJ-y += lib/div64.o
EXT_COBJ-y += lib/string.o
EXT_COBJ-y += lib/time.o
EXT_COBJ-y += lib/vsprintf.o
EXT_COBJ-$(CONFIG_LIB_UUID) += lib/uuid.o
EXT_SOBJ-$(CONFIG_PPC) += arch/powerpc/lib/ppcstring.o

# Create a list of object files to be compiled
OBJS := $(OBJ-y) $(notdir $(EXT_COBJ-y) $(EXT_SOBJ-y))
targets += $(OBJS)
OBJS := $(addprefix $(obj)/,$(OBJS))

#########################################################################

quiet_cmd_link_demo = LD      $@
cmd_link_demo = $(LD) --gc-sections -Ttext $(LOAD_ADDR) -o $@ $(filter-out $(PHONY), $^) $(PLATFORM_LIBS)

$(obj)/demo: $(OBJS) FORCE
	$(call if_changed,link_demo)

# demo.bin is never genrated. Is this necessary?
OBJCOPYFLAGS_demo.bin := -O binary
$(obj)/demo.bin: $(obj)/demo FORCE
	$(call if_changed,objcopy)

# Rule to build generic library C files
$(addprefix $(obj)/,$(notdir $(EXT_COBJ-y))): $(obj)/%.o: lib/%.c FORCE
	$(call cmd,force_checksrc)
	$(call if_changed_rule,cc_o_c)

# Rule to build architecture-specific library assembly files
$(addprefix $(obj)/,$(notdir $(EXT_SOBJ-y))): $(obj)/%.o: arch/powerpc/lib/%.S FORCE
	$(call if_changed_dep,as_o_S)
