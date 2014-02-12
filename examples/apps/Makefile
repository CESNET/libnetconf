#
# Makefile for libnetconf example applications
#
# Author: Radek Krejci <rkrejci@cesnet.cz>
# Copyright (c) 2012-2014 CESNET, z.s.p.o.
#

CC      = gcc
CFLAGS  = -Wall
INCLUDE = -I../../src/ 
LIB     = -lnetconf
LIBPATH	= -L../../.libs/

all: get

get: get.c
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LIBPATH) $(LIB)

clean:
	rm -f *.o
	rm -f get

