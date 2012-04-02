#******************************************************************************
#
# Makefile - Rules for building the board specific examples.
#
# Copyright (c) 2007-2010 Texas Instruments Incorporated.  All rights reserved.
# Software License Agreement
# 
# Texas Instruments (TI) is supplying this software for use solely and
# exclusively on TI's microcontroller products. The software is owned by
# TI and/or its suppliers, and is protected under applicable copyright
# laws. You may not combine this software with "viral" open-source
# software in order to form a larger program.
# 
# THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
# NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
# NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
# CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
# DAMAGES, FOR ANY REASON WHATSOEVER.
# 
# This is part of revision 6075 of the Stellaris Firmware Development Package.
#
#******************************************************************************

DIRS=dk-lm3s9b96 \
     ek-lm3s811  \
     ek-lm3s1968 \
     ek-lm3s2965 \
     ek-lm3s3748 \
     ek-lm3s6965 \
     ek-lm3s8962 \
     ek-lm3s9b90 \
     ek-lm3s9b92 \
     rdk-acim    \
     rdk-bdc     \
     rdk-bdc24   \
     rdk-bldc    \
     rdk-idm     \
     rdk-idm-l35 \
     rdk-idm-sbc \
     rdk-s2e     \
     rdk-stepper

all:
	@for i in ${DIRS};                  \
	 do                                 \
	     if [ -d $${i} ];               \
	     then                           \
	         make -C $${i} || exit $$?; \
	     fi;                            \
	 done

clean:
	@for i in ${DIRS};            \
	 do                           \
	     if [ -d $${i} ];         \
	     then                     \
	         make -C $${i} clean; \
	     fi;                      \
	 done
	@rm -f ${wildcard *~} __dummy__
