#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <libnetconf.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include "commands.h"

extern int done;

void print_version();

struct nc_session* session = NULL;

#define BUFFER_SIZE 1024

COMMAND commands[] = {
		{ "connect", cmd_connect, "Connect to the NETCONF server" },
		{ "disconnect", cmd_disconnect, "Disconnect from the NETCONF server" },
		{ "help", cmd_help, "Display this text" },
		{ "status", cmd_status, "Print information about current NETCONF session" },
		{ "quit", cmd_quit, "Quit the program" },
	/* synonyms for previous commands */
		{ "?", cmd_help, NULL },
		{ "exit", cmd_quit, NULL },
		{ NULL, NULL, NULL } \
};

struct arglist
{
	char **list;
	int count;
	int size;
};

/**
 * \brief Initiate arglist to defined values
 *
 * \param args          pointer to the arglist structure
 * \return              0 if success, non-zero otherwise
 */
void init_arglist(struct arglist *args)
{
	if (args != NULL) {
		args->list = NULL;
		args->count = 0;
		args->size = 0;
	}
}

/**
 * \brief Clear arglist including free up allocated memory
 *
 * \param args          pointer to the arglist structure
 * \return              none
 */
void clear_arglist(struct arglist *args)
{
	int i = 0;

	if (args && args->list) {
		for (i = 0; i < args->count; i++) {
			if (args->list[i]) {
				free(args->list[i]);
			}
		}
		free(args->list);
	}

	init_arglist(args);
}

/**
 * \brief add arguments to arglist
 *
 * Adds erguments to arglist's structure. Arglist's list variable
 * is used to building execv() arguments.
 *
 * \param args          arglist to store arguments
 * \param format        arguments to add to the arglist
 */
void addargs(struct arglist *args, char *format, ...)
{
	va_list arguments;
	char *aux = NULL, *aux1 = NULL;
	int len;

	if (args == NULL)
		return;

	/* store arguments to aux string */
	va_start(arguments, format);
	if ((len = vasprintf(&aux, format, arguments)) == -1)
		perror("addargs - vasprintf");
	va_end(arguments);

	/* parse aux string and store it to the arglist */
	/* find \n and \t characters and replace them by space */
	while ((aux1 = strpbrk(aux, "\n\t")) != NULL) {
		*aux1 = ' ';
	}
	/* remember the begining of the aux string to free it after operations */
	aux1 = aux;

	/*
	 * get word by word from given string and store words separately into
	 * the arglist
	 */
	for (aux = strtok(aux, " "); aux != NULL; aux = strtok(NULL, " ")) {
		if (!strcmp(aux, ""))
			continue;

		if (args->list == NULL) { /* initial memory allocation */
			if ((args->list = (char **) malloc(8 * sizeof(char *))) == NULL)
				perror("Fatal error while allocating memmory");
			args->size = 8;
			args->count = 0;
		} else if (args->count + 2 >= args->size) {
			/*
			 * list is too short to add next to word so we have to
			 * extend it
			 */
			args->size += 8;
			args->list = realloc(args->list, args->size * sizeof(char *));
		}
		/* add word in the end of the list */
		if ((args->list[args->count] = (char *) malloc((strlen(aux) + 1) * sizeof(char))) == NULL)
			perror("Fatal error while allocating memmory");
		strcpy(args->list[args->count], aux);
		args->list[++args->count] = NULL; /* last argument */
	}
	/* clean up */
	free(aux1);
}

int cmd_status(char* arg)
{
	char *s;
	struct nc_cpblts* cpblts;

	if (session == NULL) {
		fprintf(stdout, "Client is not connected to any NETCONF server.\n");
	} else {
		fprintf(stdout, "Current NETCONF session:\n");
		fprintf(stdout, "  ID          : %s\n", s = nc_session_get_id(session));
		if (s != NULL) {
			free(s);
		}
		fprintf(stdout, "  Host        : %s\n", s = nc_session_get_host(session));
		if (s != NULL) {
			free(s);
		}
		fprintf(stdout, "  Port        : %s\n", s = nc_session_get_port(session));
		if (s != NULL) {
			free(s);
		}
		fprintf(stdout, "  User        : %s\n", s = nc_session_get_user(session));
		if (s != NULL) {
			free(s);
		}
		fprintf(stdout, "  Capabilities:\n");
		cpblts = nc_session_get_cpblts(session);
		if (cpblts != NULL) {
			nc_cpblts_iter_start(cpblts);
			while ((s = nc_cpblts_iter_next(cpblts)) != NULL) {
				fprintf(stdout, "\t%s\n", s);
				free(s);
			}
		}
	}

	return (EXIT_SUCCESS);
}

void cmd_connect_help()
{
	fprintf(stdout, "connect [--help] [--port <num>] [--login <username>] host\n");
}

int cmd_connect(char* arg)
{
	char *host = NULL, *user = NULL;
	int hostfree = 0;
	unsigned short port = 0;
	int c;
	struct arglist cmd;
	struct option long_options[] = {
			{ "help", 0, 0, 'h' },
			{ "port", 2, 0, 'p' },
			{ "login", 2, 0, 'l' },
			{ 0, 0, 0, 0 }
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session != NULL) {
		fprintf(stderr, "Client is already connected to %s\n", host = nc_session_get_host(session));
		if (host != NULL) {
			free(host);
		}
		return (EXIT_FAILURE);
	}

	/* process given arguments */
        init_arglist(&cmd);
        addargs (&cmd, "%s", arg);

        while ((c = getopt_long(cmd.count, cmd.list, "hp:l:", long_options, &option_index)) != -1) {
        	switch(c) {
        	case 'h':
        		cmd_connect_help();
        		return (EXIT_SUCCESS);
        		break;
        	case 'p':
        		port = (unsigned short) atoi(optarg);
        		break;
        	case 'l':
        		user = optarg;
        		break;
        	default:
        		fprintf(stderr, "connect: unknown option %c\n", c);
        		cmd_connect_help();
        		return (EXIT_FAILURE);
        	}
        }
        if (optind == cmd.count) {
        	/* get mandatory argument */
        	host = malloc(sizeof(char) * BUFFER_SIZE);
        	if (host == NULL) {
        		fprintf(stderr, "Memory allocation error (%s)\n", strerror (errno));
        		return (EXIT_FAILURE);
        	}
        	hostfree = 1;
        	fprintf(stdout,"  Set the hostname to connect with: ");
        	scanf("%1023s", host);
        } else if ((optind + 1) == cmd.count) {
        	host = cmd.list[optind];
        }

        /* create the session */
	session = nc_session_connect(host, port, user, NULL);
	if (session == NULL) {
		fprintf(stderr, "Connecting to the %s failed!\n", host);
		if (hostfree) {free(host);}
		return (EXIT_FAILURE);
	}
	if (hostfree) {free(host);}

	return (EXIT_SUCCESS);
}

int cmd_disconnect(char* arg)
{
	if (session == NULL) {
		fprintf(stderr, "Client is not connected to any NETCONF server.\n");
	} else {
		nc_session_close(session);
		session = NULL;
	}

	return (EXIT_SUCCESS);
}

int cmd_quit(char* arg)
{
	done = 1;
	if (session != NULL) {
		cmd_disconnect(NULL);
	}
	return (0);
}

int cmd_help(char* arg)
{
	int i;

	print_version();
	fprintf(stdout, "\nAvailable commands:\n");

	for (i = 0; commands[i].name; i++) {
		if (commands[i].helpstring != NULL) {
			fprintf(stdout, "  %-10s %s\n", commands[i].name, commands[i].helpstring);
		}
	}

	return (0);
}
