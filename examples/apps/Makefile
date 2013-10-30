#
# Makefile for libnetconf example applications
#
# Author: Radek Krejci <rkrejci@cesnet.cz>
# Copyright (C) 2013 CESNET, z.s.p.o.
#

CC      = gcc
CFLAGS  = -Wall
INCLUDE = 
LIB     = -lnetconf
LIBPATH	= -L../../.libs/

all: get

get: get.c
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LIBPATH) $(LIB)

clean:
	rm -f *.o
	rm -f get

