CONTIKI = ../../../..
OBJCOPY	= avr-objcopy

##
PRINTF_LIB_FLT  = -Wl,-u,vfprintf -lprintf_flt -lm
PRINTF_LIB_MIN  = -Wl,-u,vfprintf -lprintf_min
PRINTF_LIB	= $(PRINTF_LIB_FLT)
CLIBS		= $(PRINTF_LIB) 


CUSTOM_RULE_LINK = 1
%.$(TARGET): %.co $(PROJECT_OBJECTFILES) $(PROJECT_LIBRARIES) contiki-$(TARGET).a 
	$(LD) $(LDFLAGS) $(TARGET_STARTFILES) ${filter-out %.a,$^} ${filter %.a,$^} $(TARGET_LIBFILES) -o $@ $(CLIBS)


APPS=serial-shell

include $(CONTIKI)/Makefile.include



all: hex bc 

hex :
	$(OBJCOPY) bc.avr-RSS2 -j .text -j .data -O ihex bc.hex

