/**
 * \file notifications.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to handle NETCONF Notifications.
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#define _GNU_SOURCE
#define _BSD_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <poll.h>
#include <pthread.h>

#include <dbus/dbus.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "notifications.h"
#include "netconf_internal.h"
#include "messages_internal.h"
#include "netconf.h"
#include "session.h"

/* sleep time in dispatch loops in microseconds */
#define NCNTF_DISPATCH_SLEEP 100

#define NC_NTF_DBUS_PATH "/libnetconf/notifications/stream"
#define NC_NTF_DBUS_INTERFACE "libnetconf.notifications.stream"
static DBusConnection* dbus = NULL;
static pthread_mutex_t *dbus_mut = NULL;

/* name of the environment variable to change Events streams path */
#define STREAMS_PATH_ENV "LIBNETCONF_STREAMS"
#define STREAMS_PATH_DEF "/var/run/netconf_events"
static char* streams_path = NULL;

/*
 * STREAM FILE FORMAT
 * char[8] == "NCSTREAM"
 * uint16_t 0xffxx - magic number to detect byte order and file format version (xx)
 * uint16_t len1;
 * char[len1] name; - this must correspond with the file name
 * uint16_t len2;
 * char[len2] description;
 * uint8_t replay;
 * uint16_t part_number;
 * char[] records;
 *
 */

/* magic bytes to recognize libnetconf's stream files */
#define MAGIC_NAME "NCSTREAM"
#define MAGIC_VERSION 0xFF01

struct stream {
	int fd;
	char* name;
	char* desc;
	uint8_t replay;
	uint16_t part;
	int locked;
	unsigned int data;
	struct stream *next;
};

/* internal list of used streams with mutex to controll access into the list */
static struct stream *streams = NULL;
static pthread_mutex_t *streams_mut = NULL;

/* internal flag if the notification structures are properly initialized */
static int initialized = 0;

void ncntf_notif_free(nc_ntf *ntf)
{
	nc_msg_free((struct nc_msg*) ntf);
}

/*
 * free the stream structure
 */
static void nc_ntf_stream_free(struct stream *s)
{
	if (s == NULL) {
		return;
	}

	if (s->desc != NULL) {
		free(s->desc);
	}
	if (s->name != NULL) {
		free(s->name);
	}
	if (s->fd != -1) {
		close(s->fd);
	}
	free(s);
}

/*
 * Check accessibility of the path to the given directory path with stream files.
 *
 * returns 0 on success, non-zero value else
 */
static int check_streams_path(char* path)
{
	struct stat sb;

	/* check accessibility of the path */
	if (access(path, F_OK|R_OK|W_OK) != 0) {
		if (errno == ENOENT) {
			/* path does not exist -> create it */
			if (mkdir(path, 0777) == -1) {
				WARN("Unable to create Events streams directory %s (%s).", path, strerror(errno));
				return (EXIT_FAILURE);
			}
			return (EXIT_SUCCESS);
		}
		WARN("Unable to access Events streams directory %s (%s).", path, strerror(errno));
		return (EXIT_FAILURE);
	} else {
		/* check that the file is directory */
		if (stat(path, &sb) == -1) {
			WARN("Unable to get information about Events streams directory %s (%s).", path, strerror(errno));
			return (EXIT_FAILURE);
		}
		if (!S_ISDIR(sb.st_mode)) {
			WARN("Events streams directory path %s exists, but it is not a directory.", path);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
}

/*
 * Set path to the directory with the stream files. It can be set by
 * environment variable defined as macro STREAMS_PATH_ENV (LIBNETCONF_STREAMS).
 * If this variable is not defined, default value from macro STREAMS_PATH_DEF
 * (/var/run/netconf_events) is used.
 *
 * returns 0 on success, non-zero value else
 */
static int set_streams_path()
{
	char* env;

	/* forgot previously set value (do not free, it is static) */
	streams_path = NULL;

	/* try to get path from the environment variable */
	env = getenv(STREAMS_PATH_ENV);
	if (env != NULL) {
		VERB("Checking Events stream path %s from %s environment variable.", env, STREAMS_PATH_ENV);
		if (check_streams_path(env) == 0) {
			streams_path = env;
		}
	}
	if (streams_path == NULL) {
		/* try to use default path */
		VERB("Checking default Events stream path %s.", STREAMS_PATH_DEF);
		if (check_streams_path(STREAMS_PATH_DEF) == 0) {
			streams_path = STREAMS_PATH_DEF;
		}
	}

	if (streams_path == NULL) {
		return (EXIT_FAILURE);
	} else {
		return (EXIT_SUCCESS);
	}
}

/*
 * Create a new stream file and write the header corresponding to the given
 * stream structure. If the file is already opened (stream structure has file
 * descriptor), it only rewrites the header of the file. All data from the
 * existing file are lost!
 *
 * returns 0 on success, non-zero value else
 */
static int write_fileheader(struct stream *s)
{
	char* filepath = NULL;
	uint16_t len, version = MAGIC_VERSION;
	mode_t mask;

	/* check used variables */
	if (s == NULL || s->name == NULL || streams_path == NULL) {
		return (EXIT_FAILURE);
	}

	/* check if the corresponding file is already opened */
	if (s->fd == -1) {
		/* open and create/truncate the file */
		asprintf(&filepath, "%s/%s.%05d", streams_path, s->name, s->part);
		mask = umask(0000);
		s->fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0777);
		umask(mask);
		if (s->fd == -1) {
			ERROR("Unable to create Events stream file %s (%s)", filepath, strerror(errno));
			free(filepath);
			return (EXIT_FAILURE);
		}
		free(filepath);
	} else {
		/* truncate the file */
		ftruncate(s->fd, 0);
		lseek(s->fd, 0, SEEK_SET);
	}

	/* write the header */
	/* magic bytes */
	write(s->fd, &MAGIC_NAME, strlen(MAGIC_NAME));
	write(s->fd, &version, sizeof(uint16_t));
	/* stream name */
	len = (uint16_t) strlen(s->name) + 1;
	write(s->fd, &len, sizeof(uint16_t));
	write(s->fd, s->name, len);
	/* stream description */
	if (s->desc != NULL) {
		len = (uint16_t) strlen(s->desc) + 1;
		write(s->fd, &len, sizeof(uint16_t));
		write(s->fd, s->desc, len);
	} else {
		/* no description */
		len = 1;
		write(s->fd, &len, sizeof(uint16_t));
		write(s->fd, "", len);
	}
	/* replay flag */
	write(s->fd, &(s->replay), sizeof(uint8_t));
	/* part number */
	write(s->fd, &(s->part), sizeof(uint16_t));

	/* set where the data starts */
	s->data = lseek(s->fd, 0, SEEK_CUR);

	return (EXIT_SUCCESS);
}

/*
 * Read file header and fill the stream structure of the stream file specified
 * as filepath. If file is not proper libnetconf's stream file, NULL is
 * returned.
 */
static struct stream *read_fileheader(const char* filepath)
{
	struct stream *s;
	int fd;
	char magic_name[strlen(MAGIC_NAME)];
	uint16_t magic_number;
	uint16_t len;

	/* open the file */
	fd = open(filepath, O_RDWR);
	if (fd == -1) {
		ERROR("Unable to open Events stream file %s (%s)", filepath, strerror(errno));
		return (NULL);
	}

	/* create and fill the stream structure according to the file header */
	s = malloc(sizeof(struct stream));
	s->fd = fd;
	/* check magic bytes */
	read(s->fd, &magic_name, strlen(MAGIC_NAME));
	if (strncmp(magic_name, MAGIC_NAME, strlen(MAGIC_NAME)) != 0) {
		/* file is not of libnetconf's stream file format */
		free(s);
		return (NULL);
	}
	read(s->fd, &magic_number, sizeof(uint16_t));
	/* \todo: handle different endianity and versions */

	/* read the stream name */
	read(s->fd, &len, sizeof(uint16_t));
	s->name = malloc(len * sizeof(char));
	read(s->fd, s->name, len);
	/* read the description of the stream */
	read(s->fd, &len, sizeof(uint16_t));
	s->desc = malloc(len * sizeof(char));
	read(s->fd, s->desc, len);
	/* read the replay flag */
	read(s->fd, &(s->replay), sizeof(uint8_t));
	/* read the part number */
	read(s->fd, &(s->part), sizeof(uint16_t));

	s->locked = 0;
	s->next = NULL;

	/* move to the end of the file */
	s->data = lseek(s->fd, 0, SEEK_CUR);

	return (s);
}

/*
 * close all opened streams in the global list
 */
static void nc_ntf_streams_close(void)
{
	struct stream *s;

	pthread_mutex_lock(streams_mut);
	s = streams;
	while(s != NULL) {
		streams = s->next;
		nc_ntf_stream_free(s);
		s = streams;
	}
	pthread_mutex_unlock(streams_mut);
}

/*
 * close D-Bus communication channel
 */
static void nc_ntf_dbus_close(void)
{
	pthread_mutex_lock(dbus_mut);
	if (dbus != NULL) {
		dbus_connection_unref(dbus);
		dbus = NULL;
	}
	pthread_mutex_unlock(dbus_mut);
}

void ncntf_close(void)
{
	if (initialized == 1) {
		nc_ntf_dbus_close();
		nc_ntf_streams_close();
		pthread_mutex_destroy(streams_mut);
		pthread_mutex_destroy(dbus_mut);
		free(streams_mut);
		streams_mut = NULL;
		free(dbus_mut);
		dbus_mut = NULL;
		initialized = 0;
	}
}

/*
 * Modify the given list of files in the specified directory to keep only
 * regular files in the list. Parameter n specifies the number of items in the
 * list. The list is ussually obtained using scandir().
 *
 */
static void filter_reg_files(char* dirpath, struct dirent **filelist, int n)
{
	char* filepath;
	struct stat sb;

	assert(filelist != NULL);
	assert(dirpath != NULL);

	for(--n; n >= 0; n--) {
		if (filelist[n] == NULL) {
			continue;
		}
#ifdef _DIRENT_HAVE_D_TYPE
		if (filelist[n]->d_type == DT_UNKNOWN) {
			/* try another detection method -> use stat */
#endif
			/* d_type is not available -> use stat */
			asprintf(&filepath, "%s/%s", dirpath, filelist[n]->d_name);
			if (stat(filepath, &sb) == -1) {
				ERROR("stat() failed on file %s - %s (%s:%d)", filepath, strerror(errno), __FILE__, __LINE__);
				free(filelist[n]);
				filelist[n] = NULL;
				free(filepath);
				continue;
			}
			free(filepath);
			if (!S_ISREG(sb.st_mode)) {
				/* the file is not a regular file containing stream events */
				free(filelist[n]);
				filelist[n] = NULL;
				continue;
			}
#ifdef _DIRENT_HAVE_D_TYPE
		} else if (filelist[n]->d_type != DT_REG) {
			/* the file is not a regular file containing stream events */
			free(filelist[n]);
			filelist[n] = NULL;
			continue;
		}
#endif
	}
}

/*
 * Initiate the list of available streams. It opens all accessible stream files
 * from the stream directory.
 *
 * If the function is called repeatedly, stream files are closed and opened
 * again - current processing like iteration of the events in the stream must
 * start again.
 */
static int nc_ntf_streams_init(void)
{
	int n;
	struct dirent **filelist;
	struct stream *s;
	char* filepath;

	/* check the streams path */
	if (streams_path == NULL && set_streams_path() != 0) {
		return (EXIT_FAILURE);
	}

	/*
	 * lock the whole initialize operation, not only streams variable
	 * manipulation since this starts a complete work with the streams
	 */
	pthread_mutex_lock(streams_mut);

	if (streams != NULL) {
		/* streams already initiated - reinitialize the list */
		nc_ntf_streams_close();
	}

	/* explore the stream directory */
	n = scandir(streams_path, &filelist, NULL, alphasort);
	if (n < 0) {
		ERROR("Unable to read from Events streams directory %s (%s).", streams_path, strerror(errno));
		pthread_mutex_unlock(streams_mut);
		return (EXIT_FAILURE);
	}
	/* keep only regular files that could store the events stream */
	filter_reg_files(streams_path, filelist, n);
	/* and open all libnetconf's stream files - file's magic number is checked */
	for(--n; n >= 0; n--) {
		if (filelist[n] == NULL) { /* was not a regular file */
			continue;
		}

		asprintf(&filepath, "%s/%s", streams_path, filelist[n]->d_name);
		if ((s = read_fileheader(filepath)) != NULL) {
			/* add the stream file into the stream list */
			s->next = streams;
			streams = s;
		} /* else - not an event stream file */
		free(filepath);

		/* free the directory entry information */
		free(filelist[n]);
	}

	if (ncntf_stream_isavailable(NCNTF_STREAM_BASE) == 0) {
		ncntf_stream_new(NCNTF_STREAM_BASE, "NETCONF Base Notifications", 1);
	}

	pthread_mutex_unlock(streams_mut);
	free(filelist);

	return (EXIT_SUCCESS);
}

/*
 * Initialize D-Bus communication
 */
static int nc_ntf_dbus_init(void)
{
	DBusError dbus_err;

	pthread_mutex_lock(dbus_mut);
	if (dbus == NULL) {
		/* initialize the errors */
		dbus_error_init(&dbus_err);

		/* connect to the D-Bus */
		dbus = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_err);
		if (dbus_error_is_set(&dbus_err)) {
			ERROR("D-Bus connection error (%s)", dbus_err.message);
			dbus_error_free(&dbus_err);
		}
		if (dbus == NULL) {
			ERROR("Unable to connect to the D-Bus's system bus");
			pthread_mutex_unlock(dbus_mut);
			return (EXIT_FAILURE);
		}
	}
	pthread_mutex_unlock(dbus_mut);

	return (EXIT_SUCCESS);
}

int ncntf_init(void)
{
	int ret;
	pthread_mutexattr_t mattr;
	int r;

	if (initialized == 1) {
		return(EXIT_SUCCESS);
	}

	/* init streams' mutex if needed */
	if (streams_mut == NULL) {
		if (pthread_mutexattr_init(&mattr) != 0) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
		if ((streams_mut = malloc(sizeof(pthread_mutex_t))) == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			pthread_mutexattr_destroy(&mattr);
			return (EXIT_FAILURE);
		}
		/*
		 * mutex MUST be recursive since we use it to cover update
		 * operation and it internally calls sequence of close and init
		 */
		pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
		if ((r = pthread_mutex_init(streams_mut, &mattr)) != 0) {
			ERROR("Mutex initialization failed (%s).", strerror(r));
			pthread_mutexattr_destroy(&mattr);
			return (EXIT_FAILURE);
		}
		pthread_mutexattr_destroy(&mattr);
	}

	/* init dbus's mutex if needed */
	if (dbus_mut == NULL) {
		if (pthread_mutexattr_init(&mattr) != 0) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
		if ((dbus_mut = malloc(sizeof(pthread_mutex_t))) == NULL) {
			ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
			pthread_mutexattr_destroy(&mattr);
			return (EXIT_FAILURE);
		}
		/*
		 * mutex MUST be recursive since we use it this way
		 */
		pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
		if ((r = pthread_mutex_init(dbus_mut, &mattr)) != 0) {
			ERROR("Mutex initialization failed (%s).", strerror(r));
			pthread_mutexattr_destroy(&mattr);
			return (EXIT_FAILURE);
		}
		pthread_mutexattr_destroy(&mattr);
	}

	initialized = 1;
	/* initiate streams */
	if ((ret = nc_ntf_streams_init()) != 0) {
		initialized = 0;
		return (ret);
	}

	/* initiate DBus communication */
	if ((ret = nc_ntf_dbus_init()) != 0) {
		initialized = 0;
		return (ret);
	}

	return (EXIT_SUCCESS);
}

int ncntf_stream_new(const char* name, const char* desc, int replay)
{
	struct stream *s;

	if (initialized == 0) {
		return (EXIT_FAILURE);
	}

	pthread_mutex_lock(streams_mut);

	/* check the stream name if the requested stream already exists */
	for (s = streams; s != NULL; s = s->next) {
		if (strcmp(name, s->name) == 0) {
			WARN("Requested new stream \'%s\' already exists.", name);
			pthread_mutex_unlock(streams_mut);
			return (EXIT_FAILURE);
		}
	}

	s = malloc(sizeof(struct stream));
	if (s == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		pthread_mutex_unlock(streams_mut);
		return (EXIT_FAILURE);
	}
	s->name = strdup(name);
	s->desc = strdup(desc);
	s->replay = replay;
	s->locked = 0;
	s->part = 1; /* the first part of the stream */
	s->next = NULL;
	s->fd = -1;
	if (write_fileheader(s) != 0) {
		nc_ntf_stream_free(s);
		pthread_mutex_unlock(streams_mut);
		return (EXIT_FAILURE);
	} else {
		/* add created stream into the list */
		s->next = streams;
		streams = s;
		pthread_mutex_unlock(streams_mut);
		return (EXIT_SUCCESS);
	}
}

char** ncntf_stream_list(void)
{
	char** list;
	struct stream *s;
	int i;

	if (initialized == 0) {
		return (NULL);
	}

	pthread_mutex_lock(streams_mut);
	if (streams == NULL) {
		nc_ntf_streams_init();
	}

	for (s = streams, i = 0; s != NULL; s = s->next, i++);
	list = calloc(i + 1, sizeof(char*));
	if (list == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		pthread_mutex_unlock(streams_mut);
		return (NULL);
	}
	for (s = streams, i = 0; s != NULL; s = s->next, i++) {
		list[i] = strdup(s->name);
	}
	pthread_mutex_unlock(streams_mut);

	return(list);
}

int ncntf_stream_isavailable(const char* name)
{
	struct stream *s;

	if (initialized == 0 || name == NULL) {
		return(0);
	}

	pthread_mutex_lock(streams_mut);
	for (s = streams; s != NULL; s = s->next) {
		if (strcmp(s->name, name) == 0) {
			/* the specified stream does exist */
			pthread_mutex_unlock(streams_mut);
			return (1);
		}
	}
	pthread_mutex_unlock(streams_mut);

	return (0); /* the stream does not exist */
}

/*
 * Get the stream structure according to the given stream name
 */
static struct stream* nc_ntf_stream_get(const char* stream)
{
	struct stream *s;
	char* filepath;

	if (stream == NULL) {
		return (NULL);
	}

	if (streams == NULL) {
		nc_ntf_streams_init();
	}

	/* search for the specified stream in the list according to the name */
	for (s = streams; s != NULL; s = s->next) {
		if (strcmp(s->name, stream) == 0) {
			/* the specified stream does exist */
			return (s);
		}
	}

	/*
	 * the stream was not found in the current list - try to look at the
	 * stream directory if the stream file wasn't created meanwhile
	 */
	if (s == NULL) {
		/* try to localize so far unrecognized stream file */
		asprintf(&filepath, "%s/%s.%05d", streams_path, stream, 1);
		if ((s = read_fileheader(filepath)) != NULL) {
			/* add the stream file into the stream list */
			s->next = streams;
			streams = s;
		}
	}

	return (s);
}

/*
 * lock the stream file to avoid concurrent writing/reading from different
 * processes.
 */
static int nc_ntf_stream_lock(struct stream *s)
{
	off_t offset;

	/* this will be blocking, but all these locks should be short-term */
	offset = lseek(s->fd, 0, SEEK_CUR);
	lseek(s->fd, 0, SEEK_SET);
	if (lockf(s->fd, F_LOCK, 0) == -1) {
		lseek(s->fd, offset, SEEK_SET);
		ERROR("Stream file locking failed (%s).", strerror(errno));
		return (EXIT_FAILURE);
	}
	lseek(s->fd, offset, SEEK_SET);
	s->locked = 1;
	return (EXIT_SUCCESS);
}

/*
 * unlock the stream file after reading/writing
 */
static int nc_ntf_stream_unlock(struct stream *s)
{
	off_t offset;

	if (s->locked == 0) {
		/* nothing to do */
		return (EXIT_SUCCESS);
	}

	offset = lseek(s->fd, 0, SEEK_CUR);
	lseek(s->fd, 0, SEEK_SET);
	if (lockf(s->fd, F_ULOCK, 0) == -1) {
		lseek(s->fd, offset, SEEK_SET);
		ERROR("Stream file unlocking failed (%s).", strerror(errno));
		return (EXIT_FAILURE);
	}
	lseek(s->fd, offset, SEEK_SET);
	s->locked = 0;
	return (EXIT_SUCCESS);
}

/**
 * @ingroup notifications
 * @brief Store new event into the specified stream. Parameters are specific
 * for different events.
 *
 * ### Event parameters:
 * - #NC_NTF_GENERIC
 *  - **const char* content** Content of the notification as defined in RFC 5277.
 *  eventTime is added automatically. The string should be XML formatted.
 * - #NC_NTF_BASE_CFG_CHANGE
 *  - #NC_DATASTORE **datastore** Specify which datastore has changed.
 *  - #NC_NTF_EVENT_BY **changed_by** Specify the source of the change.
 *   - If the value is set to #NC_NTF_EVENT_BY_USER, following parameter is
 *   required:
 *  - **const struct nc_session* session** Session required the configuration change.
 * - #NC_NTF_BASE_CPBLT_CHANGE
 *  - **const struct nc_cpblts* old** Old list of capabilities.
 *  - **const struct nc_cpblts* new** New list of capabilities.
 *  - #NC_NTF_EVENT_BY **changed_by** Specify the source of the change.
 *   - If the value is set to #NC_NTF_EVENT_BY_USER, following parameter is
 *   required:
 *  - **const struct nc_session* session** Session required the configuration change.
 * - #NC_NTF_BASE_SESSION_START
 *  - **const struct nc_session* session** Started session (#NC_SESSION_STATUS_DUMMY session is also allowed).
 * - #NC_NTF_BASE_SESSION_END
 *  - **const struct nc_session* session** Finnished session (#NC_SESSION_STATUS_DUMMY session is also allowed).
 *  - #NC_SESSION_TERM_REASON **reason** Session termination reason.
 *   - If the value is set to #NC_SESSION_TERM_KILLED, following parameter is
 *   required.
 *  - **const char* killed-by-sid** The ID of the session that directly caused
 *  the session termination. If the session was terminated by a non-NETCONF
 *  process unknown to the server, use NULL as the value.
 *
 * ### Examples:
 * - nc_ntf_event_new("mystream", -1, NC_NTF_GENERIC, "<event>something happend</event>");
 * - nc_ntf_event_new("netconf", -1, NC_NTF_BASE_CFG_CHANGE, NC_DATASTORE_RUNNING, NC_NTF_EVENT_BY_USER, my_session);
 * - nc_ntf_event_new("netconf", -1, NC_NTF_BASE_CPBLT_CHANGE, old_cpblts, new_cpblts, NC_NTF_EVENT_BY_SERVER);
 * - nc_ntf_event_new("netconf", -1, NC_NTF_BASE_SESSION_START, my_session);
 * - nc_ntf_event_new("netconf", -1, NC_NTF_BASE_SESSION_END, my_session, NC_SESSION_TERM_KILLED, "123456");
 *
 * @param[in] stream Name of the stream where the event will be stored.
 * @param[in] etime Time of the event, if set to -1, current time is used.
 * @param[in] event Event type to distinguish following parameters.
 * @param[in] ... Specific parameters for different event types as described
 * above.
 * @return 0 for success, non-zero value else.
 */
int ncntf_event_new(char* stream, time_t etime, NCNTF_EVENT event, ...)
{
	char *event_time = NULL, *signal_object = NULL;
	char *content = NULL, *record = NULL;
	char *aux1 = NULL, *aux2 = NULL;
	NC_DATASTORE ds;
	NCNTF_EVENT_BY by;
	const struct nc_cpblts *old, *new;
	const struct nc_session *session;
	NC_SESSION_TERM_REASON reason;
	struct stream* s;
	int32_t len;
	int poffset, i, j;
	uint64_t etime64;
	va_list params;
	DBusMessage *signal = NULL;
	DBusMessageIter signal_args;

	/* check the stream */
	if (initialized == 0 || ncntf_stream_isavailable(stream) == 0) {
		return (EXIT_FAILURE);
	}

	va_start(params, event);

	/* get the event description */
	switch (event) {
	case NCNTF_GENERIC:
		content = va_arg(params, char *);
		if (content != NULL) {
			content = strdup(content);
		} else {
			ERROR("Missing parameter content to create GENERIC event record.");
			va_end(params);
			return (EXIT_FAILURE);
		}
		break;
	case NCNTF_BASE_CFG_CHANGE:
		ds = va_arg(params, NC_DATASTORE);
		by = va_arg(params, NCNTF_EVENT_BY);

		/* check datastore parameter */
		switch (ds) {
		case NC_DATASTORE_STARTUP:
			aux1 = "startup";
			break;
		case NC_DATASTORE_RUNNING:
			aux1 = "running";
			break;
		default:
			/* invalid value */
			ERROR("Invalid \'datastore\' parameter of %s.", __func__);
			va_end(params);
			return (EXIT_FAILURE);
			break;
		}

		/* check change-by parameter */
		switch (by) {
		case NCNTF_EVENT_BY_SERVER:
			/* BY_USER must be created dynamically, so allocate it
			 * dynamically also in this case to have single free();
			 */
			aux2 = strdup("<server/>");
			break;
		case NCNTF_EVENT_BY_USER:
			/* another parameter should be passed */
			session = va_arg(params, const struct nc_session*);
			if (session == NULL) {
				ERROR("Invalid \'session\' parameter of %s.", __func__);
				va_end(params);
				return (EXIT_FAILURE);
			}
			asprintf(&aux2, "<username>%s</username>"
					"<session-id>%s</session-id>"
					"<source-host>%s</source-host>",
					session->username,
					session->session_id,
					session->hostname);
			break;
		}

		/* no more parameters */
		va_end(params);

		asprintf(&content, "<netconf-config-change><datastore>%s</datastore>"
				"%s</netconf-config-change>",
				aux1, aux2);
		free(aux2);

		break;
	case NCNTF_BASE_CPBLT_CHANGE:
		/* \todo */
		old = va_arg(params, const struct nc_cpblts*);
		new = va_arg(params, const struct nc_cpblts*);
		by = va_arg(params, NCNTF_EVENT_BY);

		/* find created capabilities */
		for(i = 0; new->list[i] != NULL; i++) {
			/*
			 * check if the capability contains parameter (starting
			 * with '?'). Then the length of the capability URI
			 * without parameters is stored and used in comparison
			 */
			if ((aux1 = strchr(new->list[i], '?')) != NULL) {
				/* there are capability's parameters */
				poffset = (int)(aux1 - (new->list[i]));
			} else {
				poffset = strlen(new->list[i]);
			}
			/*
			 * traverse old capabilities and find out if the
			 * current new one is there
			 */
			j = 0;
			while(old->list[j] != NULL) {
				if (strncmp(new->list[i], old->list[j], poffset) == 0) {
					break;
				}
				j++;
			}

			aux1 = NULL;
			/* process the result of searching */
			if (old->list[j] != NULL) {
				/*
				 * new->list[i] can be the same as old->list[j]
				 * or there are modified parameters
				 */
				if ((old->list[j][poffset] == '?' || old->list[j][poffset] == '\0')
						&& (strcmp(new->list[i], old->list[j]) != 0)) {
					asprintf(&aux1, "<modified-capability>%s</modified-capability>", new->list[i]);
				}
			} else {
				/* aux1 is a new capability */
				asprintf(&aux1, "<added-capability>%s</added-capability>", new->list[i]);
			}

			if (aux1 != NULL) {
				/* add new information to the previous one */
				if (aux2 != NULL) {
					aux2 = realloc(aux2, strlen(aux2) + strlen(aux1) + 1);
				} else {
					aux2 = calloc(strlen(aux1) + 1, sizeof(char));
				}
				strncat(aux2, aux1, strlen(aux1));
				free(aux1);
			}
		}

		/* find deleted capabilities */
		for (i = 0; old->list[i] != NULL; i++) {
			/*
			 * find the end of the basic capability URI to not take
			 * parameters into acount (this case was processed in
			 * previous loop
			 */
			if ((aux1 = strchr(old->list[i], '?')) != NULL) {
				/* there are capability's parameters */
				poffset = (int) (aux1 - (old->list[i]));
			} else {
				poffset = strlen(old->list[i]);
			}

			/*
			 * traverse new capabilities and find out if some of the
			 * old capabilities is removed
			 */
			j = 0;
			while(new->list[j] != NULL) {
				if (strncmp(new->list[j], old->list[i], poffset) == 0) {
					break;
				}
				j++;
			}

			/* process the result */
			if (new->list[j] == NULL) {
				aux1 = NULL;
				/* old->list[i] is deleted capability */
				asprintf(&aux1, "<deleted-capability>%s</deleted-capability>", old->list[i]);
				if (aux2 != NULL) {
					aux2 = realloc(aux2, strlen(aux2) + strlen(aux1) + 1);
				} else {
					aux2 = calloc(strlen(aux1) + 1, sizeof(char));
				}
				strncat(aux2, aux1, strlen(aux1));
				free(aux1);
			}
		}

		/* check change-by parameter */
		switch (by) {
		case NCNTF_EVENT_BY_SERVER:
			/* BY_USER must be created dynamically, so allocate it
			 * dynamically also in this case to have single free();
			 */
			aux1 = strdup("<server/>");
			break;
		case NCNTF_EVENT_BY_USER:
			/* another parameter should be passed */
			session = va_arg(params, const struct nc_session*);
			if (session == NULL) {
				ERROR("Invalid \'session\' parameter of %s.", __func__);
				va_end(params);
				return (EXIT_FAILURE);
			}
			asprintf(&aux1, "<username>%s</username>"
					"<session-id>%s</session-id>"
					"<source-host>%s</source-host>",
					session->username,
					session->session_id,
					session->hostname);
			break;
		}

		/* no more parameters */
		va_end(params);

		if (aux2 == NULL) {
			aux2 = strdup("");
		}

		asprintf(&content, "<netconf-capability-change>%s%s</netconf-capability-change>",
				aux1, aux2);
		free(aux1);
		free(aux2);
		break;
	case NCNTF_BASE_SESSION_START:
		session = va_arg(params, const struct nc_session*);
		if (session == NULL) {
			ERROR("Invalid \'session\' parameter of %s.", __func__);
			va_end(params);
			return (EXIT_FAILURE);
		}
		asprintf(&content, "<netconf-session-start><username>%s</username>"
				"<session-id>%s</session-id>"
				"<source-host>%s</source-host></netconf-session-start>",
				session->username,
				session->session_id,
				session->hostname);

		/* no more parameters */
		va_end(params);

		break;
	case NCNTF_BASE_SESSION_END:
		session = va_arg(params, const struct nc_session*);
		reason = va_arg(params, NC_SESSION_TERM_REASON);

		/* check the result */
		if (session == NULL) {
			ERROR("Invalid \'session\' parameter of %s.", __func__);
			va_end(params);
			return (EXIT_FAILURE);
		}

		aux2 = NULL;
		if (reason == NC_SESSION_TERM_KILLED) {
			aux1 = va_arg(params, char*);
			if (aux1 != NULL) {
				asprintf(&aux2, "<killed-by>%s</killed-by>", aux1);
			}
		}
		/* if termination type is not kill, killed-by will not be used */
		if (aux2 == NULL) {
			/* aux2 have to be dynamically allocated */
			aux2 = strdup("");
		}

		/* prepare part of the content for the specific termination reason */
		asprintf(&aux1, "<termination-reason>%s</termination-reason>", nc_session_term_string(reason));

		/* compound the event content */
		asprintf(&content, "<netconf-session-end><username>%s</username>"
				"<session-id>%s</session-id>"
				"<source-host>%s</source-host>"
				"%s%s</netconf-session-end>",
				session->username,
				session->session_id,
				session->hostname,
				aux2, aux1);
		free(aux2);
		free(aux1);

		break;
	default:
		ERROR("Adding unsupported type of event.");
		return (EXIT_FAILURE);
		break;
	}
	va_end(params);

	/* process the EventTime */
	if (etime == -1) {
		etime = time(NULL);
	}
	if (etime == -1) {
		ERROR("Setting event time failed (%s).", strerror(errno));
		return (EXIT_FAILURE);
	}
	if ((event_time = nc_time2datetime(etime)) == NULL) {
		ERROR("Internal error when converting time formats (%s:%d).", __FILE__, __LINE__);
		return (EXIT_FAILURE);
	}
	etime64 = (uint64_t)etime;

	/* complete the event text */
	len = (int32_t) asprintf(&record, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<notification xmlns=\"%s\"><eventTime>%s</eventTime>"
			"%s</notification>",
			NC_NS_CAP_NOTIFICATIONS,
			event_time,
			content);
	free(event_time);
	free(content);
	if (len == -1) {
		ERROR("Creating event record failed.");
		return (EXIT_FAILURE);
	}
	len++; /* include termination null byte */

	/* write the event into the stream file */
	pthread_mutex_lock(streams_mut);
	s = nc_ntf_stream_get(stream);
	if (nc_ntf_stream_lock(s) == 0) {
		lseek(s->fd, 0, SEEK_END);
		write(s->fd, &len, sizeof(int32_t)); /* length of the record */
		write(s->fd, &etime64, sizeof(uint64_t)); /* event time in time_t format */
		write(s->fd, record, len); /* event record - <notification> xml document */
		nc_ntf_stream_unlock(s);
	} else {
		ERROR("Unable to write event into stream file %s (locking failed).", s->name);
		free(record);
		return (EXIT_FAILURE);
	}
	pthread_mutex_unlock(streams_mut);

	/* announce event via DBus */
	pthread_mutex_lock(dbus_mut);
	if (dbus != NULL) {
		/* create a signal and check for errors */
		asprintf(&signal_object, "%s/%s", NC_NTF_DBUS_PATH, stream);
		signal = dbus_message_new_signal(signal_object, NC_NTF_DBUS_INTERFACE, "Event");
		free(signal_object);
		if (signal == NULL) {
			WARN("Announcing event via DBus failed (creating DBus signal failed).");
			goto cleanup;
			/* event already successfully stored, return SUCCESS */
		}
		/* append arguments onto signal */
		dbus_message_iter_init_append(signal, &signal_args);
		if (!dbus_message_iter_append_basic(&signal_args, DBUS_TYPE_UINT64, &etime64)) {
			WARN("Announcing event via DBus failed (attaching event timestamp failed).");
			goto cleanup;
			/* event already successfully stored, return SUCCESS */
		}
		if (!dbus_message_iter_append_basic(&signal_args, DBUS_TYPE_STRING, &record)) {
			WARN("Announcing event via DBus failed (attaching event content failed).");
			goto cleanup;
			/* event already successfully stored, return SUCCESS */
		}

		/* send the message and flush the connection */
		if (!dbus_connection_send(dbus, signal, NULL)) {
			WARN("Announcing event via DBus failed (sending signal failed).");
			goto cleanup;
			/* event already successfully stored, return SUCCESS */
		}
		dbus_connection_flush(dbus);
	}

cleanup:
	pthread_mutex_unlock(dbus_mut);

	/* free DBus signal */
	if (signal != NULL) {
		dbus_message_unref(signal);
	}

	/* final cleanup */
	free(record);

	return (EXIT_SUCCESS);
}

void ncntf_stream_iter_start(const char* stream)
{
	struct stream *s;
	char* dbus_filter = NULL;
	DBusError err;

	if (initialized == 0) {
		return;
	}

	pthread_mutex_lock(streams_mut);
	if ((s = nc_ntf_stream_get(stream)) == NULL) {
		pthread_mutex_unlock(streams_mut);
		return;
	}
	lseek(s->fd, s->data, SEEK_SET);
	pthread_mutex_unlock(streams_mut);

	/* subscribe DBus signals for the stream */
	asprintf(&dbus_filter, "type='signal',interface='%s',path='%s/%s',member='Event'",
			NC_NTF_DBUS_INTERFACE, NC_NTF_DBUS_PATH, stream);
	dbus_error_init(&err);

	pthread_mutex_lock(dbus_mut);
	dbus_bus_add_match(dbus, dbus_filter, &err);
	dbus_connection_flush(dbus);
	pthread_mutex_unlock(dbus_mut);

	free(dbus_filter);

	if (dbus_error_is_set(&err)) {
		WARN("%s", err.message);
		dbus_error_free(&err);
	}
}

void ncntf_stream_iter_finnish(const char* stream)
{
	char* dbus_filter = NULL;
	DBusError err;

	/* unsubscribe DBus */
	asprintf(&dbus_filter, "type='signal',interface='%s',path='%s/%s',member='Event'",
			NC_NTF_DBUS_INTERFACE, NC_NTF_DBUS_PATH, stream);
	dbus_error_init(&err);

	pthread_mutex_lock(dbus_mut);
	dbus_bus_remove_match(dbus, dbus_filter, &err);
	dbus_connection_flush(dbus);
	pthread_mutex_unlock(dbus_mut);

	free(dbus_filter);

	if (dbus_error_is_set(&err)) {
		WARN("%s", err.message);
		dbus_error_free(&err);
	}
}

/*
 * Pop the next event record from the stream file.
 *
 * \todo: thread safety (?thread-specific variables)
 */
char* ncntf_stream_iter_next(const char* stream, time_t start, time_t stop, time_t *event_time)
{
	struct stream *s;
	int32_t len;
	uint64_t t;
	off_t offset;
	char* text;
	DBusMessage *signal;
	DBusMessageIter signal_args;

	if (initialized == 0) {
		return (NULL);
	}

	/* check time boundaries */
	if (start != -1 && stop != -1 && stop < start) {
		return (NULL);
	}

	pthread_mutex_lock(streams_mut);
	if ((s = nc_ntf_stream_get(stream)) == NULL) {
		pthread_mutex_unlock(streams_mut);
		return (NULL);
	}

	while (1) {
		if ((offset = lseek(s->fd, 0, SEEK_CUR)) < lseek(s->fd, 0, SEEK_END)) {
			/* there are still some data to read */
			lseek(s->fd, offset, SEEK_SET);
		} else {
			/* no more data */
			pthread_mutex_unlock(streams_mut);

			/* try DBus */
			while (1) {
				pthread_mutex_lock(dbus_mut);
				signal = dbus_connection_pop_message(dbus);
				pthread_mutex_unlock(dbus_mut);

				if (signal != NULL && dbus_message_is_signal(signal, NC_NTF_DBUS_INTERFACE, "Event")) {
					/* parse the message, according to the
					 * filter set in nc_ntf_stream_iter_start(),
					 * we have Event signal from the stream
					 * interface of the specified stream
					 */
					/* read the parameters */
					if (dbus_message_iter_init(signal, &signal_args)) {
						if (DBUS_TYPE_UINT64 != dbus_message_iter_get_arg_type(&signal_args)) {
							WARN("Unexpected DBus Event signal (timestamp is missing).");
							dbus_message_unref(signal);
							continue;
						}
						dbus_message_iter_get_basic(&signal_args, &t);
						/* check boundaries */
						if (start != -1 && start > t) {
							/*
							 * we're not interested in this event, it
							 * happened before specified start time
							 */
							dbus_message_unref(signal);
							continue; /* try next signal */
						}
						if (stop != -1 && stop < t) {
							/*
							 * we're not interested in this event, it
							 * happened after specified stop time
							 */
							dbus_message_unref(signal);
							continue; /* try next signal */
						}
						/* we're interested, read content */
						if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&signal_args)) {
							WARN("Unexpected DBus Event signal (content is missing).");
							dbus_message_unref(signal);
							continue;
						}
						dbus_message_iter_get_basic(&signal_args, &text);
						dbus_message_unref(signal);
						if (event_time != NULL) {
							*event_time = t;
						}
						return(text);
					}
				}
				/* else signal == NULL */
				break;
			}

			/* no more events */
			return (NULL);
		}

		if (nc_ntf_stream_lock(s) == 0) {
			read(s->fd, &len, sizeof(int32_t));
			read(s->fd, &t, sizeof(uint64_t));

			/* check boundaries */
			if (start != -1 && start > t) {
				/*
				 * we're not interested in this event, it
				 * happened before specified start time
				 */
				lseek(s->fd, len, SEEK_CUR);
				/* read another event */
				continue;
			}
			if (stop != -1 && stop < t) {
				/*
				 * we're not interested in this event, it
				 * happened after specified stop time
				 */
				lseek(s->fd, len, SEEK_CUR);
				/* read another event */
				continue;
			}

			/* we're interested, read content */
			text = malloc(len * sizeof(char));
			read(s->fd, text, len);
			nc_ntf_stream_unlock(s);
			break; /* end the reading loop */
		} else {
			ERROR("Unable to read event from stream file %s (locking failed).", s->name);
			pthread_mutex_unlock(streams_mut);
			return (NULL);
		}
	}

	pthread_mutex_unlock(streams_mut);

	if (event_time != NULL) {
		*event_time = t;
	}
	return (text);
}
static void ncntf_stdoutprint (time_t eventtime, const char* content)
{
	char* t = NULL;

	fprintf(stdout, "eventTime: %s\n%s\n", t = nc_time2datetime(eventtime), content);
	if (t != NULL) {
		free(t);
	}
}

nc_ntf* ncntf_notif_create(time_t event_time, const char* content)
{
	char* notif_data = NULL, *etime = NULL;
	xmlDocPtr notif_doc;
	nc_ntf* retval;

	if ((etime = nc_time2datetime(event_time)) == NULL) {
		ERROR("Converting time to string failed (%s:%d)", __FILE__, __LINE__);
		return (NULL);
	}

	asprintf(&notif_data, "<notification>%s</notification>", content);
	notif_doc = xmlReadMemory(notif_data, strlen(notif_data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (notif_doc == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		free(notif_data);
		free(etime);
		return (NULL);
	}
	free(notif_data);

	if (xmlNewChild(notif_doc->children, NULL, BAD_CAST "eventTime", BAD_CAST etime) == NULL) {
		ERROR("xmlAddChild failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(notif_doc);
		free(etime);
		return NULL;
	}
	free(etime);

	retval = malloc(sizeof(nc_rpc));
	if (retval == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	retval->doc = notif_doc;
	retval->msgid = NULL;
	retval->error = NULL;
	retval->with_defaults = NCDFLT_MODE_DISABLED;

	return (retval);
}

static NCNTF_EVENT ncntf_notif_get_type(nc_ntf* notif)
{
	xmlNodePtr root, node;

	if (notif == NULL || notif->doc == NULL) {
		ERROR("%s: Invalid input parameter.", __func__);
		return (NCNTF_ERROR);
	}

	if ((root = xmlDocGetRootElement (notif->doc)) == NULL) {
		ERROR("%s: Invalid message format, root element is missing.", __func__);
		return (NCNTF_ERROR);
	}

	if (xmlStrcmp(root->name, BAD_CAST "notification") == 0) {
		for (node = root->children; node != NULL; node = node->next) {
			if (node->name == NULL || xmlStrcmp(node->name, BAD_CAST "eventTime") == 0) {
				continue;
			}
			/* use first not eventTime element */
			break;
		}
		if (node == NULL) {
			ERROR("%s: Invalid Notification message - missing event description.", __func__);
			return (NCNTF_ERROR);
		}

		if (xmlStrcmp(node->name, BAD_CAST "replayComplete") == 0) {
			return (NCNTF_REPLAY_COMPLETE);
		} else if (xmlStrcmp(node->name, BAD_CAST "notificationComplete") == 0) {
			return (NCNTF_NTF_COMPLETE);
		} else if (xmlStrcmp(node->name, BAD_CAST "netconf-config-change") == 0) {
			return (NCNTF_BASE_CFG_CHANGE);
		} else if (xmlStrcmp(node->name, BAD_CAST "netconf-capability-change") == 0) {
			return (NCNTF_BASE_CPBLT_CHANGE);
		} else if (xmlStrcmp(node->name, BAD_CAST "netconf-session-start") == 0) {
			return (NCNTF_BASE_SESSION_START);
		} else if (xmlStrcmp(node->name, BAD_CAST "netconf-session-end") == 0) {
			return (NCNTF_BASE_SESSION_END);
		} else if (xmlStrcmp(node->name, BAD_CAST "netconf-configrmed-commit") == 0) {
			return (NCNTF_BASE_CONFIRMED_COMMIT);
		} else {
			return (NCNTF_GENERIC);
		}
	} else {
		ERROR("%s: Invalid Notification message - missing <notification> element.", __func__);
		return (NCNTF_ERROR);
	}
}

char* ncntf_notif_get_content(nc_ntf* notif)
{
	char * retval;
	xmlNodePtr root, node;
	xmlDocPtr aux_doc;
	xmlBufferPtr buffer;

	if (notif == NULL || notif->doc == NULL) {
		ERROR("%s: Invalid input parameter.", __func__);
		return (NULL);
	}

	if ((root = xmlDocGetRootElement (notif->doc)) == NULL) {
		ERROR("%s: Invalid message format, root element is missing.", __func__);
		return (NULL);
	}
	if (xmlStrcmp(root->name, BAD_CAST "notification") != 0) {
		ERROR("%s: Invalid message format, missing notification element.", __func__);
		return (NULL);
	}

	/* by copying node, move all needed namespaces into the content nodes */
	aux_doc = xmlNewDoc(BAD_CAST "1.0");
	xmlDocSetRootElement(aux_doc, xmlNewNode(NULL, BAD_CAST "content"));
	xmlAddChildList(aux_doc->children, xmlDocCopyNodeList(aux_doc, root->children));
	buffer = xmlBufferCreate ();
	for (node = aux_doc->children->children; node != NULL; node = node->next) {
		/* skip invalid nodes */
		if (node->name == NULL || node->ns == NULL || node->ns->href == NULL) {
			continue;
		}

		/* skip eventTime element */
		if (xmlStrcmp(node->name, BAD_CAST "eventTime") == 0 &&
				xmlStrcmp(node->ns->href, BAD_CAST NC_NS_CAP_NOTIFICATIONS) == 0) {
			continue;
		}

		/* dump content into the buffer */
		xmlNodeDump(buffer, aux_doc, node, 1, 1);
	}
	retval = strdup((char *)xmlBufferContent (buffer));
	xmlBufferFree (buffer);
	xmlFreeDoc(aux_doc);

	return retval;
}

time_t ncntf_notif_get_time(nc_ntf* notif)
{
	xmlXPathContextPtr notif_ctxt = NULL;
	xmlXPathObjectPtr result = NULL;
	xmlChar* datetime;
	time_t t = -1;

	if (notif == NULL || notif->doc == NULL) {
		return (-1);
	}

	/* create xpath evaluation context */
	if ((notif_ctxt = xmlXPathNewContext(notif->doc)) == NULL) {
		WARN("%s: Creating XPath context failed.", __func__)
		/* with-defaults cannot be found */
		return (-1);
	}
	if (xmlXPathRegisterNs(notif_ctxt, BAD_CAST "ntf", BAD_CAST NC_NS_CAP_NOTIFICATIONS) != 0) {
		xmlXPathFreeContext(notif_ctxt);
		return (-1);
	}

	/* get eventTime value */
	result = xmlXPathEvalExpression(BAD_CAST "/ntf:notification/ntf:eventTime", notif_ctxt);
	if (result != NULL) {
		if (result->nodesetval->nodeNr != 1) {
			t = -1;
		} else {
			t = nc_datetime2time((char*)(datetime = xmlNodeGetContent(result->nodesetval->nodeTab[0])));
			if (datetime != NULL) {
				xmlFree(datetime);
			}
		}
		xmlXPathFreeObject(result);
	}

	xmlXPathFreeContext(notif_ctxt);

	return (t);
}

/**
 * @return 0 on success,\n -1 on general error (invalid rpc),\n -2 on filter error (filter set but it is invalid)
 */
static int get_subscription_params(const nc_rpc* subscribe_rpc, char **stream, time_t *start, time_t *stop, struct nc_filter** filter)
{
	xmlXPathContextPtr srpc_ctxt = NULL;
	xmlXPathObjectPtr result = NULL;
	xmlChar* datetime;

	if (subscribe_rpc == NULL || nc_rpc_get_op(subscribe_rpc) != NC_OP_CREATESUBSCRIPTION) {
		return (-1);
	}

	/* create xpath evaluation context */
	if ((srpc_ctxt = xmlXPathNewContext(subscribe_rpc->doc)) == NULL) {
		ERROR("%s: Creating XPath context failed.", __func__);
		return (-1);
	}
	if (xmlXPathRegisterNs(srpc_ctxt, BAD_CAST "ntf", BAD_CAST NC_NS_CAP_NOTIFICATIONS) != 0) {
		ERROR("%s: Registering namespace for XPath context failed.", __func__);
		xmlXPathFreeContext(srpc_ctxt);
		return (-1);
	}

	/* get stream name from subscription */
	if (stream != NULL) {
		result = xmlXPathEvalExpression(BAD_CAST "/ntf:create-subscription/ntf:stream", srpc_ctxt);
		if (result == NULL || result->nodesetval == NULL || result->nodesetval->nodeNr != 1) {
			/* use default stream 'netconf' */
			*stream = strdup("netconf");
		} else {
			*stream = (char*) (xmlNodeGetContent(result->nodesetval->nodeTab[0]));
		}
		if (result != NULL) {
			xmlXPathFreeObject(result);
		}
	}

	/* get startTime from the subscription */
	if (start != NULL) {
		result = xmlXPathEvalExpression(BAD_CAST "//ntf:create-subscription/ntf:startTime", srpc_ctxt);
		if (result == NULL || result->nodesetval == NULL || result->nodesetval->nodeNr != 1) {
			*start = -1;
		} else {
			*start = nc_datetime2time((char*) (datetime = xmlNodeGetContent(result->nodesetval->nodeTab[0])));
			if (datetime != NULL) {
				xmlFree(datetime);
			}
		}
		if (result != NULL) {
			xmlXPathFreeObject(result);
		}
	}

	/* get stopTime from the subscription */
	if (stop != NULL) {
		result = xmlXPathEvalExpression(BAD_CAST "//ntf:create-subscription/ntf:stopTime", srpc_ctxt);
		if (result == NULL || result->nodesetval == NULL || result->nodesetval->nodeNr != 1) {
			*stop = -1;
		} else {
			*stop = nc_datetime2time((char*) (datetime = xmlNodeGetContent(result->nodesetval->nodeTab[0])));
			if (datetime != NULL) {
				xmlFree(datetime);
			}
		}
		if (result != NULL) {
			xmlXPathFreeObject(result);
		}
	}

	/* get filter from the subscription */
	if (filter != NULL) {
		result = xmlXPathEvalExpression(BAD_CAST "//ntf:create-subscription/ntf:filter", srpc_ctxt);
		if (result == NULL || result->nodesetval == NULL || result->nodesetval->nodeNr != 1) {
			/* do nothing - filter is not specified */
		} else {
			/* filter exist, check its correctness */
			if ((*filter = nc_rpc_get_filter(subscribe_rpc)) == NULL) {
				return (-2);
			}
		}
		if (result != NULL) {
			xmlXPathFreeObject(result);
		}
	}
	xmlXPathFreeContext(srpc_ctxt);
	return (0);
}

nc_reply *ncntf_check_subscription(const nc_rpc* subscribe_rpc)
{
	struct nc_err* e;
	char *stream = NULL, *auxs = NULL;
	struct nc_filter *filter = NULL;
	time_t start, stop;

	if (subscribe_rpc == NULL || nc_rpc_get_op(subscribe_rpc) != NC_OP_CREATESUBSCRIPTION) {
		return (nc_reply_error(nc_err_new(NC_ERR_INVALID_VALUE)));
	}

	switch(get_subscription_params(subscribe_rpc, &stream, &start, &stop, &filter)) {
	case 0:
		/* everything ok */
		break;
	case -1:
		/* rpc is invalid */
		return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
		break;
	case -2:
		/* filter is invalid */
		e = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "filter");
		return (nc_reply_error(e));
		break;
	default:
		/* unknown error */
		return (nc_reply_error(nc_err_new(NC_ERR_OP_FAILED)));
		break;
	}

	/* check existence of the stream */
	pthread_mutex_lock(streams_mut);
	if (nc_ntf_stream_get(stream) == NULL) {
		pthread_mutex_unlock(streams_mut);
		e = nc_err_new(NC_ERR_INVALID_VALUE);
		asprintf(&auxs, "Requested stream \'%s\' does not exist.", stream);
		nc_err_set(e, NC_ERR_PARAM_MSG, auxs);
		free(auxs);
		return (nc_reply_error(e));
	}
	pthread_mutex_unlock(streams_mut);

	/* check start and stop times */
	if ((stop != -1) && (start == -1)) {
		e = nc_err_new(NC_ERR_MISSING_ELEM);
		nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "startTime");
		return (nc_reply_error(e));
	}
	if (stop != -1 && start != -1 && start < stop) {
		e = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "stopTime");
		return (nc_reply_error(e));
	}
	if (start != -1 && start > time(NULL)) {
		e = nc_err_new(NC_ERR_BAD_ELEM);
		nc_err_set(e, NC_ERR_PARAM_TYPE, "protocol");
		nc_err_set(e, NC_ERR_PARAM_INFO_BADELEM, "startTime");
		return (nc_reply_error(e));
	}

	/* free unnecessary values */
	nc_filter_free(filter);
	free (stream);

	/* all is checked and correct */

	return(nc_reply_ok());
}

/**
 * @ingroup notifications
 * @brief Start sending notification according to the given
 * \<create-subscription\> NETCONF RPC request. All events from the specified
 * stream are processed and sent to the client until the stop time is reached
 * or until the session is terminated.
 *
 * @param[in] session NETCONF session where the notifications will be sent.
 * @param[in] subscribe_rpc \<create-subscription\> RPC, if any other RPC is
 * given, -1 is returned.
 *
 * @return number of sent notifications (including 0), -1 on error.
 */
long long int ncntf_dispatch_send(struct nc_session* session, const nc_rpc* subscribe_rpc)
{
	long long int count = 0;
	char* stream = NULL, *event = NULL, *time_s = NULL;
	struct nc_filter *filter = NULL;
	time_t start, stop;
	xmlDocPtr event_doc, filter_doc;
	xmlNodePtr event_node, aux_node, nodelist = NULL;
	nc_ntf* ntf;
	nc_reply *reply;

	if (session == NULL ||
			session->status != NC_SESSION_STATUS_WORKING ||
			subscribe_rpc == NULL ||
			nc_rpc_get_op(subscribe_rpc) != NC_OP_CREATESUBSCRIPTION) {
		ERROR("%s: Invalid parameters.", __func__);
		return (-1);
	}

	/* check subscription rpc */
	reply = ncntf_check_subscription(subscribe_rpc);
	if (nc_reply_get_type(reply) != NC_REPLY_OK) {
		ERROR("%s: create-subscription check failed (%s).", __func__, nc_reply_get_errormsg(reply));
		nc_reply_free(reply);
		return (-1);
	}
	nc_reply_free(reply);

	/* get parameters from subscription */
	if (get_subscription_params(subscribe_rpc, &stream, &start, &stop, &filter) != 0 ) {
		ERROR("Parsing create-subscription for parameters failed.");
		return (-1);
	}

	/* prepare xml doc for filtering */
	filter_doc = xmlNewDoc(BAD_CAST "1.0");
	filter_doc->encoding = xmlStrdup(BAD_CAST UTF8);

	ncntf_stream_iter_start(stream);
	while((event = ncntf_stream_iter_next(stream, start, stop, NULL)) != NULL) {
		if ((event_doc = xmlReadMemory(event, strlen(event), NULL, NULL, 0)) != NULL) {
			/* apply filter */
			if (filter != NULL) {

				/* filter all content nodes in notification */
				event_node = event_doc->children->children; /* doc -> <notification> -> <something> */
				while (event_node != NULL) {
					/* skip invalid nodes */
					if (event_node->name == NULL || event_node->ns == NULL || event_node->ns->href == NULL) {
						event_node = event_node->next;
						continue;
					}

					/* skip eventTime element */
					if (xmlStrcmp(event_node->name, BAD_CAST "eventTime") == 0 &&
							xmlStrcmp(event_node->ns->href, BAD_CAST NC_NS_CAP_NOTIFICATIONS) == 0) {
						event_node = event_node->next;
						continue;
					}

					/* filter notification content */
					xmlDocSetRootElement(filter_doc, xmlCopyNode(event_node, 1));

					/* detach and free currently filtered node from the original document */
					aux_node = event_node;
					event_node = event_node->next; /* find the next node to filter */
					xmlUnlinkNode(aux_node);
					xmlFreeNode(aux_node);

					/* filter the data */
					if (ncxml_filter(filter_doc, filter) != 0) {
						ERROR("Filter failed.");
						continue;
					}

					if (filter_doc->children != NULL) {
						aux_node = filter_doc->children;
						xmlUnlinkNode(aux_node);
						aux_node->next = nodelist;
						nodelist = aux_node;
					}
				}

				if (nodelist != NULL) {
					xmlAddChildList(event_doc->children, aux_node); /* into doc -> <notification> */
					nodelist = NULL;
				} else {
					/* nothing to send */
					xmlFreeDoc(event_doc);
					free(event);
					continue;
				}
			}

			ntf = malloc(sizeof(nc_rpc));
			if (ntf == NULL) {
				ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
				return (-1);
			}
			ntf->doc = event_doc;
			ntf->msgid = NULL;
			ntf->error = NULL;
			ntf->with_defaults = NCDFLT_MODE_DISABLED;

			nc_session_send_notif(session, ntf);
			ncntf_notif_free(ntf);
		} else {
			WARN("Invalid format of stored event, skipping.");
		}
		free(event);
	}
	xmlFreeDoc(filter_doc);
	nc_filter_free(filter);
	ncntf_stream_iter_finnish(stream);

	/* send notificationComplete Notification */
	ntf = malloc(sizeof(nc_rpc));
	if (ntf == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (-1);
	}
	asprintf(&event, "<notification xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\">"
			"<eventTime>%s</eventTime><notificationComplete/></notification>", time_s = nc_time2datetime(time(NULL)));
	free (time_s);
	ntf->doc = xmlReadMemory(event, strlen(event), NULL, NULL, 0);
	ntf->msgid = NULL;
	ntf->error = NULL;
	ntf->with_defaults = NCDFLT_MODE_DISABLED;
	nc_session_send_notif(session, ntf);
	ncntf_notif_free(ntf);
	free(event);

	return (count);
}

/**
 * @ingroup notifications
 * @brief Subscribe for receiving notifications from the given session
 * according to parameters in the given subscribtion RPC. Received notifications
 * are processed by the given process_ntf callback function. Functions stops
 * when the final notification <notificationComplete> is received or when the
 * session is terminated.
 *
 * @param[in] session NETCONF session where the notifications will be sent.
 * @param[in] subscribe_rpc \<create-subscription\> RPC, if any other RPC is
 * given, -1 is returned.
 * @param[in] process_ntf Callback function to process content of the
 * notification. If NULL, content of the notification is printed on stdout.
 *
 * @return number of received notifications, -1 on error.
 */
long long int ncntf_dispatch_receive(struct nc_session *session, const nc_rpc* subscribe_rpc, void (*process_ntf)(time_t eventtime, const char* content))
{
	long long int count = 0;
	nc_rpc *rpc;
	nc_reply *reply = NULL;
	nc_ntf* ntf = NULL;
	NC_MSG_TYPE type;
	NC_REPLY_TYPE type_reply;
	int eventfd = -1, dispatch = 1;
	LIBSSH2_POLLFD fds;
	int status;
	time_t event_time;
	char* content;

	if (session == NULL ||
			session->status != NC_SESSION_STATUS_WORKING ||
			subscribe_rpc == NULL ||
			nc_rpc_get_op(subscribe_rpc) != NC_OP_CREATESUBSCRIPTION) {
		ERROR("%s: Invalid parameters.", __func__);
		return (-1);
	}

	if ((eventfd = nc_session_get_eventfd(session)) == -1) {
		ERROR("Invalid NETCONF session input file descriptor.");
		return (-1);
	}

	/* send subscription */
	rpc = nc_msg_dup((struct nc_msg*)subscribe_rpc);
	type = nc_session_send_recv(session, rpc, &reply);
	nc_rpc_free(rpc);

	switch (type) {
	case NC_MSG_UNKNOWN:
		ERROR("Subscribing for notifications failed (receiving rpc-reply failed).");
		return (-1);
		break;
	case NC_MSG_NONE:
		/* NC_REPLY_ERROR was processed by a caller's callback function */
		return (-1);
		break;
	default:
		switch (type_reply = nc_reply_get_type(reply)) {
		case NC_REPLY_OK:
			break;
		case NC_REPLY_ERROR:
			ERROR("create-subscription failed (%s)", nc_reply_get_errormsg(reply));
			break;
		default:
			ERROR("create-subscription failed (unexpected operation result).");
			break;
		}
		nc_reply_free(reply);
		if (type_reply != NC_REPLY_OK) {
			return (-1);
		}
		break;
	}

	/* check function for notifications processing */
	if (process_ntf == NULL) {
		process_ntf = ncntf_stdoutprint;
	}

	/* main loop for receiving notifications */
	while(dispatch) {
		pthread_mutex_lock(&(session->mut_session));
		if (session->queue_event != NULL) {
			type = nc_session_recv_notif(session, &ntf);
			pthread_mutex_unlock(&(session->mut_session));

		} else {

			while (1) {
				/* start polling on input socket */
				fds.type = LIBSSH2_POLLFD_CHANNEL;
				fds.fd.channel = session->ssh_channel;
				fds.events = LIBSSH2_POLLFD_POLLIN;
				fds.revents = LIBSSH2_POLLFD_POLLIN;
				/*
				 * According to libssh2 documentation, standard poll should work, but it does not.
				 * It seems, that some data are stored in internal buffers and they are not seen
				 * by poll, but libssh2_poll on the channel.
				 */
				/*
				fds.fd = eventfd;
				fds.events = POLLIN;
				fds.revents = 0;
				status = poll(&fds, 1, 100);
				*/
				status = libssh2_poll(&fds, 1, 100);
				if (status == 0 || (status == -1 && errno == EINTR)) {
					/* poll timed out or was interrupted */
					continue;
				} else if (status < 0) {
					/* poll failed - something wrong happend, close this socket and wait for another request */
					ERROR("Input channel error");
					nc_session_close(session, NC_SESSION_TERM_DROPPED);
					dispatch = 0;  /* set the dispatch loop to end */
					break;
				}
				/* status > 0 */
				/* check the status of the socket */
				/* if nothing to read and POLLHUP (EOF) or POLLERR set */
				if ((fds.revents & POLLHUP) || (fds.revents & POLLERR)) {
					/* close client's socket (it's probably already closed by client */
					ERROR("Input channel closed");
					nc_session_close(session, NC_SESSION_TERM_DROPPED);
					dispatch = 0;  /* set the dispatch loop to end */
					break;
				}

				/* we have something to read, leave dispatch == 1 */
				break;
			}

			if (dispatch == 0) {
				pthread_mutex_unlock(&(session->mut_session));
				break; /* end the dispatch loop */
			}
			type = nc_session_recv_notif(session, &ntf);
			pthread_mutex_unlock(&(session->mut_session));
		}

		/* process current notification */
		switch (type) {
		case NC_MSG_UNKNOWN: /* error */
			dispatch = 0;
			continue; /* end the dispatch loop */
			break;
		case NC_MSG_NOTIFICATION:
			/* check for <notificationComplete> */
			if (ncntf_notif_get_type(ntf) == NCNTF_NTF_COMPLETE) {
				/* end of the Notification stream */
				dispatch = 0;
			}

			/* Parse XML to get parameters for callback function */
			event_time = ncntf_notif_get_time(ntf);
			content = ncntf_notif_get_content(ntf);
			if (event_time == -1 || content == NULL) {
				free(content);
				WARN("Invalid notification received. Ignoring.");
				continue; /* go for another notification */
			}
			process_ntf(event_time, content);
			free(content);
			break;
		default:
			/* no notification available, continue with polling */
			break;
		}
	}

	return (count);
}
