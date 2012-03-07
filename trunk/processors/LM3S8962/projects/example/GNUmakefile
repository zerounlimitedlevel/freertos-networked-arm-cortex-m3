#################################################
#
# Project:
#   example
#
# Description: 
#   This is an example project to help people learn the build system
#
# Author:
#   Quentin Arce
#
#################################################
# ALWAYS FIRST
# include the base makefile to get the make env setup
include ../../../../buildtools/GNUmakefile.base
#

# inport some private code from outside of this repository (the example is in this repository... but it doesn't have to be)
PRIVATE = private_ex1:svn:https://freertos-networked-arm-cortex-m3.googlecode.com/svn/scratch/private_code_example/number_1
PRIVATE += private_ex2:svn:https://freertos-networked-arm-cortex-m3.googlecode.com/svn/scratch/private_code_example/number_2

# list the componets you want built
COMPONENTS  = freertos_kernel freertos_minimal 
COMPONENTS += private_ex1.console_hello_world  private_ex2.led_hello_world


# define the name of the file where the FreeRTOS main lives
freertos_kernel.main = freertos_main.c
freertos_kernel.port = GCC/ARM_CM3
freertos_kernel.memmang = heap_2.c

# need to move these to another module...
freertos_kernel.cfiles += timertest.c ParTest.c IntQueueTimer.c ustdlib.c emac.c

#
# ALWAY LAST
# include the build tools makefile to get all of the standard targets
include ../../../../buildtools/GNUmakefile.build


all ::
	@echo ALL: $(COMPONENTS)
	@echo Public: $(COMPONENTS_PUBLIC)
	@echo PROJECT: $(PROCESSOR_DIR)
	@echo Priv: $(COMPONENTS_PRIVATE)
	@echo freertos.obj is $(freertos.obj)
	@echo " "
	@echo "---------------"

all :: $(freertos_kernel.obj) $(freertos_minimal.obj)
	@echo after the build

all :: example.bin

VPATH += ../../ ../../../drivers/LuminaryMicro/

LIBS = ../../../drivers/LuminaryMicro/arm-none-eabi-gcc/libdriver.a

$(OBJDIR)/startup.o : startup.c
	$(CC) -c $(CFLAGS) -O1 $< -o $@

example.bin : example.axf
	$(OBJCOPY) example.axf -O binary example.bin

example.axf : $(freertos_kernel.obj) $(freertos_minimal.obj) $(OBJDIR)/startup.o
	$(CC) $(CFLAGS) $?  $(LIBS) $(LINKER_FLAGS)



