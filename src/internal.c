/**
 * \file internal.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of internal libnetconf's functions.
 *
 * Copyright (c) 2012-2014 CESNET, z.s.p.o.
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

#define _BSD_SOURCE
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#include "netconf_internal.h"
#include "nacm.h"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

/* defined in datastore.c */
int ncds_sysinit(int flags);

int verbose_level = 0;

/* this instance is running as first after reboot or system-wide nc_close */
/* used in nc_device_init to decide if erase running or not */
int first_after_close = 0;

void nc_verbosity(NC_VERB_LEVEL level)
{
	verbose_level = level;
}

void prv_vprintf(NC_VERB_LEVEL level, const char *format, va_list args)
{
#define PRV_MSG_SIZE 4096
	char prv_msg[PRV_MSG_SIZE];

	if (callbacks.print != NULL) {
		vsnprintf(prv_msg, PRV_MSG_SIZE - 1, format, args);
		prv_msg[PRV_MSG_SIZE - 1] = '\0';
		callbacks.print(level, prv_msg);

	}
#undef PRV_MSG_SIZE
}

void prv_printf(NC_VERB_LEVEL level, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	prv_vprintf(level, format, ap);
	va_end(ap);
}

void nc_verb_verbose(const char *format, ...)
{
	va_list argptr;
	if (verbose_level >= NC_VERB_VERBOSE) {
		va_start(argptr, format);
		prv_vprintf(NC_VERB_VERBOSE, format, argptr);
		va_end(argptr);
	}
}

void nc_verb_warning(const char *format, ...)
{
	va_list argptr;

	if (verbose_level >= NC_VERB_WARNING) {
		va_start(argptr, format);
		prv_vprintf(NC_VERB_WARNING, format, argptr);
		va_end(argptr);
	}
}

void nc_verb_error(const char *format, ...)
{
	va_list argptr;

	if (verbose_level >= NC_VERB_ERROR) {
		va_start(argptr, format);
		prv_vprintf(NC_VERB_ERROR, format, argptr);
		va_end(argptr);
	}
}

struct nc_shared_info *nc_info = NULL;
static int shmid = -1;

#define NC_INIT_DONE  0x00000001
int nc_init_flags = 0;

int nc_init(int flags)
{
	int retval = 0, r;
	key_t key = -4;
	first_after_close = 1;
	char* t;
	pthread_rwlockattr_t rwlockattr;

	if (nc_init_flags & NC_INIT_DONE) {
		ERROR("libnetconf already initiated!");
		return (-1);
	}

	DBG("Shared memory key: %d", key);
	shmid = shmget(key, sizeof(struct nc_shared_info), IPC_CREAT | IPC_EXCL | FILE_PERM);
	if (shmid == -1 && errno == EEXIST) {
		shmid = shmget(key, sizeof(struct nc_shared_info), FILE_PERM);
		retval = 1;
		first_after_close = 0;
	}
	if (shmid == -1) {
		ERROR("Accessing shared memory failed (%s).", strerror(errno));
		return (-1);
	}
	DBG("Shared memory ID: %d", shmid);

	/* attach memory */
	nc_info = shmat(shmid, NULL, 0);
	if (nc_info == (void*) -1) {
		ERROR("Attaching shared memory failed (%s).", strerror(errno));
		nc_info = NULL;
		return (-1);
	}

	/* todo use locks */
	if (first_after_close) {
		/* remove the global session information file if left over a previous libnetconf instance */
		if ((unlink(SESSIONSFILE_PATH) == -1) && (errno != ENOENT)) {
			ERROR("Unable to remove the session information file (%s)", strerror(errno));
			shmdt(nc_info);
			return (-1);
		}
		
		/* lock */
		pthread_rwlockattr_init(&rwlockattr);
		pthread_rwlockattr_setpshared(&rwlockattr, PTHREAD_PROCESS_SHARED);
		if ((r = pthread_rwlock_init(&(nc_info->lock), &rwlockattr)) != 0) {
			ERROR("Shared information lock initialization failed (%s)", strerror(r));
			shmdt(nc_info);
			return (-1);
		}
		pthread_rwlockattr_destroy(&rwlockattr);
		memset(nc_info, 0, sizeof(struct nc_shared_info));

		/* init the information structure */
		pthread_rwlock_wrlock(&(nc_info->lock));
		strncpy(nc_info->stats.start_time, t = nc_time2datetime(time(NULL)), TIME_LENGTH);
		free(t);
	} else {
		pthread_rwlock_wrlock(&(nc_info->lock));
	}
	nc_info->stats.participants++;
	pthread_rwlock_unlock(&(nc_info->lock));

	/*
	 * check used flags according to a compile time settings
	 */
#ifndef DISABLE_NOTIFICATIONS
	if (flags & NC_INIT_NOTIF) {
		nc_init_flags |= NC_INIT_NOTIF;
	}
#endif /* DISABLE_NOTIFICATIONS */
	if (flags & NC_INIT_NACM) {
		nc_init_flags |= NC_INIT_NACM;
	}
	if (flags & NC_INIT_MONITORING) {
		nc_init_flags |= NC_INIT_MONITORING;
	}
	if (flags & NC_INIT_WD) {
		nc_init_flags |= NC_INIT_WD;
	}
#ifndef DISABLE_VALIDATION
	if (flags & NC_INIT_VALIDATE) {
		nc_init_flags |= NC_INIT_VALIDATE;
	}
#endif
#ifndef DISABLE_URL
	if (flags & NC_INIT_URL) {
		nc_init_flags |= NC_INIT_URL;
	}
#endif
	if (flags & NC_INIT_KEEPALIVECHECK) {
		nc_init_flags |= NC_INIT_KEEPALIVECHECK;
	}

	/*
	 * init internal datastores - they have to be initiated before they are
	 * used by their subsystems initiated below
	 */
	if (ncds_sysinit(nc_init_flags) != EXIT_SUCCESS) {
		shmdt(nc_info);
		nc_init_flags = 0;
		return (-1);
	}

	/* init NETCONF sessions statistics */
	if (nc_init_flags & NC_INIT_MONITORING) {
		nc_session_monitoring_init();
	}

	/* init NETCONF with-defaults capability */
	if (nc_init_flags & NC_INIT_WD) {
		ncdflt_set_basic_mode(NCWD_MODE_EXPLICIT);
		ncdflt_set_supported(NCWD_MODE_ALL
		    | NCWD_MODE_ALL_TAGGED
		    | NCWD_MODE_TRIM
		    | NCWD_MODE_EXPLICIT);
	}

#ifndef DISABLE_NOTIFICATIONS
	/* init Notification subsystem */
	if (nc_init_flags & NC_INIT_NOTIF) {
		if (ncntf_init() != EXIT_SUCCESS) {
			shmdt(nc_info);
			/* remove flags of uninitiated subsystems */
			nc_init_flags &= !(NC_INIT_NOTIF & NC_INIT_NACM);
			return (-1);
		}
	}
#endif

	/* init Access Control subsystem */
	if (nc_init_flags & NC_INIT_NACM) {
		if (nacm_init() != EXIT_SUCCESS) {
			shmdt(nc_info);
			/* remove flags of uninitiated subsystems */
			nc_init_flags &= !NC_INIT_NACM;
			return (-1);
		}
	}

	nc_init_flags |= NC_INIT_DONE;
	return (retval);
}

int nc_close(int system)
{
	struct shmid_ds ds;
	int retval = 0;

	if (shmid == -1 || nc_info == NULL) {
		/* we've not been initiated */
		return (-1);
	}

	if (system) {
		if (shmctl(shmid, IPC_STAT, &ds) == -1) {
			ERROR("Unable to get the status of shared memory (%s).", strerror(errno));
			return (-1);
		}
		if (ds.shm_nattch == 1) {
			shmctl(shmid, IPC_RMID, NULL);
		} else {
			retval = 1;
		}
	}

	pthread_rwlock_wrlock(&(nc_info->lock));
	nc_info->stats.participants--;
	pthread_rwlock_unlock(&(nc_info->lock));
	shmdt(nc_info);
	nc_info = NULL;

	/* close NETCONF session statistics */
	nc_session_monitoring_close();

	/* close all remaining datastores */
	ncds_cleanall();

#ifndef DISABLE_NOTIFICATIONS
	/* close Notification subsystem */
	if (nc_init_flags & NC_INIT_NOTIF) {
		ncntf_close();
	}
#endif
	/* close Access Control subsystem */
	if (nc_init_flags & NC_INIT_NACM) {
		nacm_close();
	}

	nc_init_flags = 0;
	return (retval);
}

char* nc_clrwspace (const char* in)
{
	int i, j = 0, len = strlen(in);
	char* retval = strdup(in);
	if (retval == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (isspace(retval[0])) {
		/* remove leading whitespace characters */
		for (i = 0, j = 0; i < len ; i++, j++) {
			while (retval[i] != '\0' && isspace(retval[i])) {
				i++;
			}
			retval[j] = retval[i];
		}
	}

	/* remove trailing whitespace characters */
	while (j >= 0 && isspace(retval[j])) {
		retval[j] = '\0';
		j--;
	}

	return (retval);
}

void nc_clip_occurences_with(char *str, char sought, char replacement)
{
	int adjacent = 0;
	int clipped = 0;

	if (str == NULL) {
		return;
	}

	while (*str != '\0') {
		if (*str != sought) {
			if (clipped != 0) {
				/* Hurl together. */
				*(str - clipped) = *str;
			}
			adjacent = 0;
		} else if (adjacent == 0) {
			/*
			 * Found first character from a possible sequence of
			 * characters. The whole sequence is going to be
			 * replaced by only one replacement character.
			 */
			*(str - clipped) = replacement;
			/* Next occurrence will be adjacent. */
			adjacent = 1;
		} else {
			++clipped;
		}

		/* Next character. */
		++str;
	}

	if (clipped != 0) {
		/* New string end. */
		*(str - clipped) = '\0';
	}
}

char* nc_skip_xmldecl(const char* xmldoc)
{
	char *s;

	if (xmldoc == NULL) {
		return (NULL);
	}

	/* skip leading whitespaces */
	s = index(xmldoc, '<');
	if (s == NULL) {
		/* not a valid XML document */
		return (NULL);
	}

	/* see http://www.w3.org/TR/REC-xml/#NT-XMLDecl */
	if (strncmp(s, "<?xml", 5) == 0) {
		/* We got a "real" XML document. Now move after the XML declaration */
		s = index(s, '>');
		if (s == NULL || s[-1] != '?') {
			/* invalid XML declaration, corrupted document */
			return (NULL);
		}
		s++; /* move after ?> */
	}

	return (s);
}

char** nc_get_grouplist(const char* username)
{
	struct passwd* p;
	struct group* g;
	int i, j, k;
	gid_t *glist;
	char** retval = NULL;

	/* get system groups for the username */
	if (username != NULL && (p = getpwnam(username)) != NULL) {
		i = 0;
		/* this call end with -1, but sets i to contain count of groups */
		getgrouplist(username, p->pw_gid, NULL, &i);
		if (i != 0) {
			glist = malloc(i * sizeof (gid_t));
			retval = malloc((i+1) * sizeof(char*));
			if (glist == NULL || retval == NULL) {
				ERROR("Memory reallocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
				free(retval);
				free(glist);
				return NULL;
			}

			if (getgrouplist(username, p->pw_gid, glist, &i) != -1) {
				for (j = 0, k = 0; j < i; j++) {
					g = getgrgid(glist[j]);
					if (g && g->gr_name) {
						retval[k++] = strdup(g->gr_name);
					}
				}
				retval[k] = NULL; /* list termination */
			} else {
				WARN("%s: unable to get list of groups (getgrouplist() failed)", __func__);
			}
			free(glist);
		}
	}

	return (retval);
}

time_t nc_datetime2time(const char* datetime)
{
	struct tm time;
	char* dt;
	int i;
	long int shift, shift_m;
	time_t retval;

	if (datetime == NULL) {
		return (-1);
	} else {
		dt = strdup(datetime);
	}

	if (strlen(dt) < 20 || dt[4] != '-' || dt[7] != '-' || dt[13] != ':' || dt[16] != ':') {
		ERROR("Wrong date time format not compliant to RFC 3339.");
		free(dt);
		return (-1);
	}

	memset(&time, 0, sizeof(struct tm));
	time.tm_year = atoi(&dt[0]) - 1900;
	time.tm_mon = atoi(&dt[5]) - 1;
	time.tm_mday = atoi(&dt[8]);
	time.tm_hour = atoi(&dt[11]);
	time.tm_min = atoi(&dt[14]);
	time.tm_sec = atoi(&dt[17]);

	retval = timegm(&time);

	/* apply offset */
	i = 19;
	if (dt[i] == '.') { /* we have fractions to skip */
		for (i++; isdigit(dt[i]); i++);
	}
	if (dt[i] == 'Z' || dt[i] == 'z') {
		/* zero shift */
		shift = 0;
	} else if (dt[i+3] != ':') {
		/* wrong format */
		ERROR("Wrong date time shift format not compliant to RFC 3339.");
		free(dt);
		return (-1);
	} else {
		shift = strtol(&dt[i], NULL, 10);
		shift = shift * 60 * 60; /* convert from hours to seconds */
		shift_m = strtol(&dt[i+4], NULL, 10) * 60; /* includes conversion from minutes to seconds */
		/* correct sign */
		if (shift < 0) {
			shift_m *= -1;
		}
		/* connect hours and minutes of the shift */
		shift = shift + shift_m;
	}
	/* we have to shift to the opposite way to correct the time */
	retval -= shift;

	free(dt);
	return (retval);
}

char* nc_time2datetime(time_t time)
{
	char* date = NULL;
	char* zoneshift = NULL;
        int zonediff, zonediff_h, zonediff_m;
        struct tm tm;

	if (gmtime_r(&time, &tm) == NULL) {
		return (NULL);
	}

	if (tm.tm_isdst < 0) {
		zoneshift = NULL;
	} else {
		if (tm.tm_gmtoff == 0) {
			/* time is Zulu (UTC) */
			if (asprintf(&zoneshift, "Z") == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				return (NULL);
			}
		} else {
			zonediff = tm.tm_gmtoff;
			zonediff_h = zonediff / 60 / 60;
			zonediff_m = zonediff / 60 % 60;
			if (asprintf(&zoneshift, "%s%02d:%02d",
			                (zonediff < 0) ? "-" : "+",
			                zonediff_h,
			                zonediff_m) == -1) {
				ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
				return (NULL);
			}
		}
	}
	if (asprintf(&date, "%04d-%02d-%02dT%02d:%02d:%02d%s",
			tm.tm_year + 1900,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
	                (zoneshift == NULL) ? "" : zoneshift) == -1) {
		free(zoneshift);
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	free (zoneshift);

	return (date);
}

/**
 * @brief Learn whether the namespace definition is used as namespace in the
 * subtree.
 * @param[in] node Node where to start checking.
 * @param[in] ns Namespace to find.
 * @return 0 if the namespace is not used, 1 if the usage of the namespace was found
 */
static int nc_find_namespace_usage(xmlNodePtr node, xmlNsPtr ns)
{
	xmlNodePtr child;
	xmlAttrPtr prop;

	/* check the element itself */
	if (node->ns == ns) {
		return 1;
	} else {
		/* check attributes of the element */
		for (prop = node->properties; prop != NULL; prop = prop->next) {
			if (prop->ns == ns) {
				return 1;
			}
		}

		/* go recursive into children */
		for (child = node->children; child != NULL; child = child->next) {
			if (child->type == XML_ELEMENT_NODE && nc_find_namespace_usage(child, ns) == 1) {
				return 1;
			}
		}
	}

	return 0;
}

/**
 * @brief Remove namespace definition from the node which are no longer used.
 * @param[in] node XML element node where to check for namespace definitions
 */
void nc_clear_namespaces(xmlNodePtr node)
{
	xmlNsPtr ns, prev = NULL;

	if (node == NULL || node->type != XML_ELEMENT_NODE) {
		return;
	}

	for (ns = node->nsDef; ns != NULL; ) {
		if (nc_find_namespace_usage(node, ns) == 0) {
			/* no one use the namespace - remove it */
			if (prev == NULL) {
				node->nsDef = ns->next;
				xmlFreeNs(ns);
				ns = node->nsDef;
			} else {
				prev->next = ns->next;
				xmlFreeNs(ns);
				ns = prev->next;
			}
		} else {
			/* check another namespace definition */
			prev = ns;
			ns = ns->next;
		}
	}
}
