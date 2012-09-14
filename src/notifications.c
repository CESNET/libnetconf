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

#include "notifications.h"
#include "netconf_internal.h"
#include "messages_internal.h"
#include "netconf.h"

/* name of the environment variable to change Events streams path */
#define STREAMS_PATH_ENV "LIBNETCONF_STREAMS"
#define STREAMS_PATH_DEF "/var/run/netconf_events"
static char* streams_path = NULL;

/*
 * STREAM FILE FORMAT
 *
 * uint16_t len1;
 * char[len1] name; - this must correspond with the file name
 * uint16_t len2;
 * char[len2] description;
 * uint8_t replay;
 * uint16_t part_number;
 * char[] records;
 *
 */


struct stream {
	int fd;
	char* name;
	char* desc;
	uint8_t replay;
	uint16_t part;
	struct stream *next;
};

struct stream *streams = NULL;

void nc_ntf_free(nc_ntf *ntf)
{
	nc_msg_free((struct nc_msg*) ntf);
}

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

static int write_fileheader(struct stream *s)
{
	char* filepath = NULL;
	uint16_t len;
	mode_t mask;

	if (s == NULL || s->name == NULL || streams_path == NULL) {
		return (EXIT_FAILURE);
	}

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
	}

	/* write the header */
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

	return (EXIT_SUCCESS);
}

static struct stream *read_fileheader(const char* filepath)
{
	struct stream *s;
	int fd;
	uint16_t len;

	fd = open(filepath, O_RDWR);
	if (fd == -1) {
		ERROR("Unable to open Events stream file %s (%s)", filepath, strerror(errno));
		return (NULL);
	}

	s = malloc(sizeof(struct stream));
	s->fd = fd;
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

	s->next = NULL;

	/* move to the end of the file */
	lseek(s->fd, 0, SEEK_END);

	return (s);
}

void nc_ntf_streams_close(void)
{
	struct stream *s = streams;

	while(s != NULL) {
		streams = s->next;
		nc_ntf_stream_free(s);
		s = streams;
	}
}

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

int nc_ntf_streams_init(void)
{
	int n;
	struct dirent **filelist;
	struct stream *s;
	char* filepath;

	if (streams_path == NULL && set_streams_path() != 0) {
		return (EXIT_FAILURE);
	}

	if (streams != NULL) {
		/* streams already initiated */
		return (EXIT_SUCCESS);
	}

	n = scandir(streams_path, &filelist, NULL, alphasort);
	if (n < 0) {
		ERROR("Unable to read from Events streams directory %s (%s).", streams_path, strerror(errno));
		return (EXIT_FAILURE);
	}
	filter_reg_files(streams_path, filelist, n);
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
	free(filelist);

	return (EXIT_SUCCESS);
}

static int nc_ntf_streams_update(void)
{
	nc_ntf_streams_close();
	return (nc_ntf_streams_init());
}

/**
 * \todo Implement this function.
 * @ingroup notifications
 * @brief Create new NETCONF event stream.
 * @param[in] name Name of the stream.
 * @param[in] desc Description of the stream.
 * @param[in] replay Specify if the replay is allowed (1 for yes, 0 for no).
 * @return 0 on success, non-zero value else.
 */
int nc_ntf_stream_new(const char* name, const char* desc, int replay)
{
	struct stream *s;

	if (streams == NULL) {
		nc_ntf_streams_init();
	} else {
		nc_ntf_streams_update();
	}

	/* check the stream name if the requested stream already exists */
	for (s = streams; s != NULL; s = s->next) {
		if (strcmp(name, s->name) == 0) {
			WARN("Requested new stream \'%s\' already exists.", name);
			return (EXIT_FAILURE);
		}
	}

	s = malloc(sizeof(struct stream));
	if (s == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (EXIT_FAILURE);
	}
	s->name = strdup(name);
	s->desc = strdup(desc);
	s->replay = replay;
	s->part = 1; /* the first part of the stream */
	s->next = NULL;
	s->fd = -1;
	if (write_fileheader(s) != 0) {
		nc_ntf_stream_free(s);
		return (EXIT_FAILURE);
	} else {
		/* add created stream into the list */
		s->next = streams;
		streams = s;
	}

	return (EXIT_SUCCESS);
}

char** nc_ntf_stream_list(void)
{
	char** list;
	struct stream *s;
	int i;

	if (streams == NULL) {
		nc_ntf_streams_init();
	} else {
		nc_ntf_streams_update();
	}

	for (s = streams, i = 0; s != NULL; s = s->next, i++);
	DBG("number of streams: %d", i);
	list = calloc(i + 1, sizeof(char*));
	if (list == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	for (s = streams, i = 0; s != NULL; s = s->next, i++) {
		list[i] = strdup(s->name);
	}
	return(list);
}
