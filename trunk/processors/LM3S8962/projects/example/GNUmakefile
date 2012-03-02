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
COMPONENTS  = freertos
COMPONENTS += private_ex1.console_hello_world  private_ex2.led_hello_world


# define the name of the file where the FreeRTOS main lives
freertos.main = freertos_main.c


#
# ALWAY LAST
# include the build tools makefile to get all of the standard targets
include ../../../../buildtools/GNUmakefile.build
