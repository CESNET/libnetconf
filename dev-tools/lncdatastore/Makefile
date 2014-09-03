CC = gcc
CFLAGS = -Wall -Wextra -g
CPPFLAGS = -DRCSID=\"\" -DNC_WORKINGDIR_PATH=\"\" -DDISABLE_VALIDATION
INCLUDE = -isystem ../../src
LIB = -pthread -lreadline -lxslt -lxml2 -ldl -lssh2 -lcurl

LIBNETCONF_OBJS = ../../src/session.c ../../src/internal.c ../../src/with_defaults.c ../../src/nacm.c ../../src/url.c ../../src/messages.c ../../src/notifications.c ../../src/callbacks.c ../../src/error.c ../../src/transapi/transapi.c ../../src/transapi/xmldiff.c ../../src/datastore/edit_config.c ../../src/transapi/yinparser.c ../../src/datastore/custom/datastore_custom.c ../../src/datastore/file/datastore_file.c ../../src/datastore/empty/datastore_empty.c

lncdatastore: main.o commands.o mreadline.o $(LIBNETCONF_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $^ $(LIB) -o $@

main.o: main.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

commands.o: commands.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^

mreadline.o: mreadline.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(INCLUDE) -o $@ $^


clean:
	@-rm -rf *.o lncdatastore
