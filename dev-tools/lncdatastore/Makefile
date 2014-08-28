CC = gcc
CFLAGS = -Wall -Wextra -g
CPPFLAGS = -DRCSID=\"\" -DNC_WORKINGDIR_PATH=\"\" -DDISABLE_VALIDATION
INCLUDE = -isystem ../../src
LIB = -pthread -lreadline -lxslt -lxml2 -ldl -lssh2 -lcurl

LIBNETCONF_OBJS = session.o ../../.obj/src/internal.o ../../.obj/src/with_defaults.o ../../.obj/src/nacm.o ../../.obj/src/url.o ../../.obj/src/messages.o ../../.obj/src/notifications.o ../../.obj/src/callbacks.o ../../.obj/src/error.o ../../.obj/src/transapi/transapi.o ../../.obj/src/transapi/xmldiff.o ../../.obj/src/datastore/edit_config.o ../../.obj/src/transapi/yinparser.o ../../.obj/src/datastore/custom/datastore_custom.o ../../.obj/src/datastore/file/datastore_file.o ../../.obj/src/datastore/empty/datastore_empty.o

lncdatastore: main.o commands.o mreadline.o $(LIBNETCONF_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $^ $(LIB) -o $@

main.o: main.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

commands.o: commands.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

mreadline.o: mreadline.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

# objects that could be compiled with TLS, we don't need that
session.o: ../../src/session.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

clean:
	@-rm -rf *.o lncdatastore
