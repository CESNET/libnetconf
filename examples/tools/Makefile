#
# Copyright (c) 2012 CESNET
#
# LICENSE TERMS
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the Company nor the names of its contributors
#    may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL) version 2 or later, in which case the provisions
# of the GPL apply INSTEAD OF those given above.
#
# This software is provided ``as is'', and any express or implied
# warranties, including, but not limited to, the implied warranties of
# merchantability and fitness for a particular purpose are disclaimed.
# In no event shall the company or contributors be liable for any
# direct, indirect, incidental, special, exemplary, or consequential
# damages (including, but not limited to, procurement of substitute
# goods or services; loss of use, data, or profits; or business
# interruption) however caused and on any theory of liability, whether
# in contract, strict liability, or tort (including negligence or
# otherwise) arising in any way out of the use of this software, even
# if advised of the possibility of such damage.
#

NAME = netconf-events

srcdir = .
abs_srcdir = /home/fuf/libnetconf/examples/tools
prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin

# Set path to search for libnetconf if installed into other than default location.
LIBNETCONF_SEARCH = "-Wl,-rpath,$(prefix)/lib"

CC = gcc
LIBS = -lxml2 -lz -lm -ldl  -L../../ $(LIBNETCONF_SEARCH) -lnetconf -lxml2
CFLAGS = -Wall -I/usr/include/libxml2  -g -DDEBUG -pthread -I/usr/include/dbus-1.0 -I/usr/lib/dbus-1.0/include 
CPPFLAGS =  -DRCSID=\"$(IDGIT)\" -I../../src/
INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
SETBIT = 0
SETUSER = 
SETGROUP = 

INSTALL_BIN_OPTIONS = 
ifeq ($(SETBIT), 1)
INSTALL_BIN_OPTIONS = --mode=4755 --owner=$(SETUSER)
endif
ifeq ($(SETBIT), 2)
INSTALL_BIN_OPTIONS = --mode=2575 --group=$(SETGROUP)
endif
ifeq ($(SETBIT), 3)
INSTALL_BIN_OPTIONS = --mode=6775 --owner=$(SETUSER) --group=$(SETGROUP)
endif

INCLUDES= -I$(srcdir)/../../src

ifeq "$(shell pwd)" "$(shell readlink -f $(srcdir))"
OBJDIR= .obj
else
OBJDIR= .

endif

SRCS = read_events.c

HDRS = 

OBJS = $(SRCS:%.c=$(OBJDIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o $(NAME)

$(OBJDIR)/%.o: %.c
	@[ -d $$(dirname $@) ] || \
		(mkdir -p $$(dirname $@))
	$(CC) $(CFLAGS) $(INCLUDES) $(CPPFLAGS) -fPIC -c $< -o $@

.PHONY: install
install: $(NAME)
	[ -d $(DESTDIR)/$(bindir) ] || \
		(mkdir -p $(DESTDIR)/$(bindir); chmod 755 $(DESTDIR)/$(bindir))
	$(INSTALL_PROGRAM) $(INSTALL_BIN_OPTIONS) $(NAME) $(DESTDIR)/$(bindir)/;

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)/$(bindir)/$(NAME);

.PHONY: clean
clean:
	rm -rf $(NAME) .obj $(OBJS); 
