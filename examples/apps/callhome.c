/*
 * reverse_server.c
 *
 *  Created on: 12. 3. 2014
 *      Author: krejci
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include "libnetconf.h"
#include "libnetconf_ssh.h"
#include "libnetconf_tls.h"

#define STUNNEL "/usr/sbin/stunnel"

void clb_print(NC_VERB_LEVEL level, const char* msg)
{

	switch (level) {
	case NC_VERB_ERROR:
		syslog(LOG_ERR, "%s", msg);
		break;
	case NC_VERB_WARNING:
		syslog(LOG_WARNING, "%s", msg);
		break;
	case NC_VERB_VERBOSE:
		syslog(LOG_INFO, "%s", msg);
		break;
	case NC_VERB_DEBUG:
		syslog(LOG_DEBUG, "%s", msg);
		break;
	}
}

static void print_usage (char * progname)
{
	fprintf (stdout, "Usage: %s [-ht] host:port\n", progname);
	fprintf (stdout, " -h       display help\n");
	fprintf (stdout, " -t       Use TLS (SSH is used by default)\n");
	exit (0);
}
#define OPTSTRING "ht"

int main(int argc, char* argv[])
{
	struct nc_mngmt_server *srv;
	char *host, *port;
	int next_option;
	int retpid, pid, status;
	NC_TRANSPORT proto = NC_TRANSPORT_SSH;

	/* parse given options */
	while ((next_option = getopt (argc, argv, OPTSTRING)) != -1) {
		switch (next_option) {
		case 'h':
			print_usage(argv[0]);
			break;
		case 't':
			proto = NC_TRANSPORT_TLS;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
	if ((optind + 1) > argc) {
		fprintf(stderr, "Missing host:port specification.");
		print_usage(argv[0]);
	} else if ((optind + 1) < argc) {
		fprintf(stderr, "Missing host:port specification.");
		print_usage(argv[0]);
	} else {
		host = strdup(argv[optind]);
		port = strrchr(host, ':');
		if (port == NULL) {
			fprintf(stderr, "Missing port specification.");
			print_usage(argv[0]);
		}
		port[0] = '\0';
		port++;
	}

	char* const arg[] = {STUNNEL, "./stunnel.callhome.conf", NULL};

	openlog("callhome", LOG_PID, LOG_DAEMON);
	nc_callback_print(clb_print);
	nc_verbosity(NC_VERB_DEBUG);


	nc_session_transport(proto);
	srv = nc_callhome_mngmt_server_add(NULL, host, port);
	if (proto == NC_TRANSPORT_TLS) {
		pid = nc_callhome_connect(srv, 5, 3, STUNNEL, arg);
	} else {
		/* for SSH we don't need to specify specific arguments */
		pid = nc_callhome_connect(srv, 5, 3, NULL, NULL);
	}

	printf("Working in background...\n");
	retpid = waitpid(pid, &status, 0); // wait for child process to end
	if (retpid != pid) {
		if (retpid == -1) {
			printf("errno(%d) [%s]\n", errno, strerror(errno));
		} else {
			printf("pid != retpid (%d)\n", retpid);
			if (WIFCONTINUED(status)) {
				printf("WIFCONTINUED\n");
			}
			if (WIFEXITED(status)) {
				printf("WIFEXITED\n");
			}
			if (WIFSIGNALED(status)) {
				printf("WIFSIGNALED\n");
			}
			if (WIFSTOPPED(status)) {
				printf("WIFSTOPPED\n");
			}
		}
	}

	return (0);
}



