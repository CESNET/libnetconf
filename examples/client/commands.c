#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <libnetconf.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libxml/parser.h>

#include "commands.h"
#include "mreadline.h"

#define NC_CAP_CANDIDATE_ID "urn:ietf:params:netconf:capability:candidate:1.0"
#define NC_CAP_STARTUP_ID   "urn:ietf:params:netconf:capability:startup:1.0"
#define NC_CAP_ROLLBACK_ID  "urn:ietf:params:netconf:capability:rollback-on-error:1.0"

extern int done;
volatile int verb_level = 0;

void print_version ();

struct nc_session* session = NULL;

#define BUFFER_SIZE 1024

COMMAND commands[] = {
		{"help", cmd_help, "Display this text"},
		{"connect", cmd_connect, "Connect to the NETCONF server"},
		{"disconnect", cmd_disconnect, "Disconnect from the NETCONF server"},
		{"copy-config", cmd_copyconfig, "NETCONF <copy-config> operation"},
		{"delete-config", cmd_deleteconfig, "NETCONF <delete-config> operation"},
		{"edit-config", cmd_editconfig, "NETCONF <edit-config> operation"},
		{"get", cmd_get, "NETCONF <get> operation"},
		{"get-config", cmd_getconfig, "NETCONF <get-config> operation"},
		{"kill-session", cmd_killsession, "NETCONF <kill-session> operation"},
		{"lock", cmd_lock, "NETCONF <lock> operation"},
		{"unlock", cmd_unlock, "NETCONF <unlock> operation"},
		{"status", cmd_status, "Print information about current NETCONF session"},
		{"user-rpc", cmd_userrpc, "Send own content in RPC envelop (for DEBUG purpose)"},
		{"verbose", cmd_verbose, "Enable/disable verbose messages"},
		{"quit", cmd_quit, "Quit the program"},
/* synonyms for previous commands */
		{"debug", cmd_debug, NULL},
		{"?", cmd_help, NULL},
		{"exit", cmd_quit, NULL},
		{NULL, NULL, NULL}
};

struct arglist {
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
void init_arglist (struct arglist *args)
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
void clear_arglist (struct arglist *args)
{
	int i = 0;

	if (args && args->list) {
		for (i = 0; i < args->count; i++) {
			if (args->list[i]) {
				free (args->list[i]);
			}
		}
		free (args->list);
	}

	init_arglist (args);
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
void addargs (struct arglist *args, char *format, ...)
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

int cmd_status (char* arg)
{
	const char *s;
	struct nc_cpblts* cpblts;

	if (session == NULL) {
		fprintf (stdout, "Client is not connected to any NETCONF server.\n");
	} else {
		fprintf (stdout, "Current NETCONF session:\n");
		fprintf (stdout, "  ID          : %s\n", nc_session_get_id (session));
		fprintf (stdout, "  Host        : %s\n", nc_session_get_host (session));
		fprintf (stdout, "  Port        : %s\n", nc_session_get_port (session));
		fprintf (stdout, "  User        : %s\n", nc_session_get_user (session));
		fprintf (stdout, "  Capabilities:\n");
		cpblts = nc_session_get_cpblts (session);
		if (cpblts != NULL) {
			nc_cpblts_iter_start (cpblts);
			while ((s = nc_cpblts_iter_next (cpblts)) != NULL) {
				fprintf (stdout, "\t%s\n", s);
			}
		}
	}

	return (EXIT_SUCCESS);
}

static NC_DATASTORE get_datastore(const char* paramtype, const char* operation, struct arglist *cmd, int index)
{
	int valid = 0;
	char *datastore;
	NC_DATASTORE retval = NC_DATASTORE_NONE;

	if (index == cmd->count) {

userinput:

		datastore = malloc (sizeof(char) * BUFFER_SIZE);
		if (datastore == NULL) {
			ERROR(operation, "memory allocation error (%s).", strerror (errno));
			return (NC_DATASTORE_NONE);
		}

		/* repeat user input until valid datastore is selected */
		while (!valid) {
			/* get mandatory argument */
			INSTRUCTION("Select %s datastore (running", paramtype);
			if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
				fprintf (stdout, "|startup");
			}
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
				fprintf (stdout, "|candidate");
			}
			fprintf (stdout, "): ");
			scanf ("%1023s", datastore);

			/* validate argument */
			if (strcmp (datastore, "running") == 0) {
				valid = 1;
				retval = NC_DATASTORE_RUNNING;
			}
			if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) && strcmp (datastore, "startup") == 0) {
				valid = 1;
				retval = NC_DATASTORE_STARTUP;
			}
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) && strcmp (datastore, "candidate") == 0) {
				valid = 1;
				retval = NC_DATASTORE_CANDIDATE;
			}

			if (!valid) {
				ERROR(operation, "invalid %s datastore type.", paramtype);
			} else {
				free(datastore);
			}
		}
	} else if ((index + 1) == cmd->count) {
		datastore = cmd->list[index];

		/* validate argument */
		if (strcmp (datastore, "running") == 0) {
			valid = 1;
			retval = NC_DATASTORE_RUNNING;
		}
		if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) && strcmp (datastore, "startup") == 0) {
			valid = 1;
			retval = NC_DATASTORE_STARTUP;
		}
		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) && strcmp (datastore, "candidate") == 0) {
			valid = 1;
			retval = NC_DATASTORE_CANDIDATE;
		}

		if (!valid) {
			goto userinput;
		}
	} else {
		ERROR(operation, "invalid parameters, see \'%s --help\'.", operation);
		return (NC_DATASTORE_NONE);
	}

	return (retval);
}

static struct nc_filter *set_filter(const char* operation, const char *file)
{
	int filter_fd;
	struct stat filter_stat;
	char *filter_s;
	struct nc_filter *filter = NULL;

	if (operation == NULL) {
		return (NULL);
	}

	if (file) {
		/* open filter from the file */
		filter_fd = open(optarg, O_RDONLY);
		if (filter_fd == -1) {
			ERROR(operation, "unable to open filter file (%s).", strerror(errno));
			return (NULL);
		}

		/* map content of the file into the memory */
		fstat(filter_fd, &filter_stat);
		filter_s = (char*) mmap(NULL, filter_stat.st_size, PROT_READ, MAP_PRIVATE, filter_fd, 0);
		if (filter_s == MAP_FAILED) {
			ERROR(operation, "mmapping filter file failed (%s).", strerror(errno));
			close(filter_fd);
			return (NULL);
		}

		/* create the filter according to the file content */
		filter = nc_filter_new(NC_FILTER_SUBTREE, filter_s);

		/* unmap filter file and close it */
		munmap(filter_s, filter_stat.st_size);
		close(filter_fd);
	} else {
		/* let user write filter interactively */
		INSTRUCTION("Type the filter (close editor by Ctrl-D):\n");
		filter_s = mreadline(NULL);
		if (filter_s == NULL) {
			ERROR(operation, "reading filter failed.");
			return (NULL);
		}

		/* create the filter according to the file content */
		filter = nc_filter_new(NC_FILTER_SUBTREE, filter_s);

		/* cleanup */
		free(filter_s);
	}

	return (filter);
}

void cmd_editconfig_help()
{
	char *rollback;

	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_ROLLBACK_ID)) {
		rollback = "|rollback";
	} else {
		rollback = "";
	}

	/* if session not established, print complete help for all capabilities */
	fprintf (stdout, "edit-config [--help] [--defop <merge|replace|none>] [--error <stop|continue%s>] [--config <file>] running", rollback);
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		fprintf (stdout, "|startup");
	}
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		fprintf (stdout, "|candidate");
	}
	fprintf (stdout, "\n");
}

int cmd_editconfig (char *arg)
{
	int c;
	char *config_m = NULL, *config = NULL;
	int config_fd;
	struct stat config_stat;
	NC_DATASTORE target;
	NC_EDIT_DEFOP_TYPE defop = 0; /* do not set this parameter by default */
	NC_EDIT_ERROPT_TYPE erropt = 0; /* do not set this parameter by default */
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"config", 1, 0, 'c'},
			{"defop", 1, 0, 'd'},
			{"error", 1, 0, 'e'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	/* rocess command line parameters */
	while ((c = getopt_long (cmd.count, cmd.list, "c:d:e:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'c':
			/* open edit configuration data from the file */
			config_fd = open(optarg, O_RDONLY);
			if (config_fd == -1) {
				ERROR("edit-config", "unable to open edit data file (%s).", strerror(errno));
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}

			/* map content of the file into the memory */
			fstat(config_fd, &config_stat);
			config_m = (char*) mmap(NULL, config_stat.st_size, PROT_READ, MAP_PRIVATE, config_fd, 0);
			if (config_m == MAP_FAILED) {
				ERROR("edit-config", "mmapping edit data file failed (%s).", strerror(errno));
				clear_arglist(&cmd);
				close(config_fd);
				return (EXIT_FAILURE);
			}

			/* make a copy of the content to allow closing the file */
			config = strdup(config_m);

			/* unmap edit data file and close it */
			munmap(config_m, config_stat.st_size);
			close(config_fd);

			break;
		case 'd':
			/* validate default operation */
			if (strcmp (optarg, "merge") == 0) {
				defop = NC_EDIT_DEFOP_MERGE;
			} else if (strcmp (optarg, "replace") == 0) {
				defop = NC_EDIT_DEFOP_REPLACE;
			} else if (strcmp (optarg, "none") == 0) {
				defop = NC_EDIT_DEFOP_NONE;
			} else {
				ERROR("edit-config", "invalid default operation %s.", optarg);
				cmd_editconfig_help ();
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}

			break;
		case 'e':
			/* validate error option */
			if (strcmp (optarg, "stop") == 0) {
				defop = NC_EDIT_ERROPT_STOP;
			} else if (strcmp (optarg, "continue") == 0) {
				defop = NC_EDIT_ERROPT_CONT;
			} else if (nc_cpblts_enabled (session, NC_CAP_ROLLBACK_ID) && strcmp (optarg, "rollback") == 0) {
				defop = NC_EDIT_ERROPT_ROLLBACK;
			} else {
				ERROR("edit-config", "invalid error-option %s.", optarg);
				cmd_editconfig_help ();
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}

			break;
		case 'h':
			cmd_editconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("edit-config", "unknown option -%c.", c);
			cmd_editconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	/* get what datastore is target of the operation */
	target = get_datastore("target", "edit-config", &cmd, optind);

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (target == NC_DATASTORE_NONE) {
		return (EXIT_FAILURE);
	}

	/* check if edit configuration data were specified */
	if (config == NULL) {
		/* let user write edit data interactively */
		INSTRUCTION("Type the edit configuration data (close editor by Ctrl-D):\n");
		config = mreadline(NULL);
		if (config == NULL) {
			ERROR("edit-config", "reading filter failed.");
			return (EXIT_FAILURE);
		}
	}

	/* create requests */
	rpc = nc_rpc_editconfig(target, defop, erropt, config);
	free(config);
	if (rpc == NULL) {
		ERROR("edit-config", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("edit-config", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("edit-config", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("edit-config", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);

	return (EXIT_SUCCESS);
}

void cmd_copyconfig_help ()
{
	char *datastores;

	if (session == NULL) {
		/* if session not established, print complete help for all capabilities */
		datastores = "running|startup|candidate";
	} else {
		if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
				datastores = "running|startup|candidate";
			} else {
				datastores = "running|startup";
			}
		} else if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			datastores = "running|candidate";
		} else {
			datastores = "running";
		}
	}

	fprintf (stdout, "copy-config [--help] [--source %s | --config <file>] %s\n", datastores, datastores);
}

int cmd_copyconfig (char *arg)
{
	int c;
	int config_fd;
	struct stat config_stat;
	char *config = NULL, *config_m = NULL;
	NC_DATASTORE target;
	NC_DATASTORE source = NC_DATASTORE_NONE;
	struct nc_filter *filter = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"config", 1, 0, 'c'},
			{"source", 1, 0, 's'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("copy-config", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "c:s:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'c':
			/* open edit configuration data from the file */
			config_fd = open(optarg, O_RDONLY);
			if (config_fd == -1) {
				ERROR("copy-config", "unable to open local datastore file (%s).", strerror(errno));
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}

			/* map content of the file into the memory */
			fstat(config_fd, &config_stat);
			config_m = (char*) mmap(NULL, config_stat.st_size, PROT_READ, MAP_PRIVATE, config_fd, 0);
			if (config_m == MAP_FAILED) {
				ERROR("copy-config", "mmapping local datastore file failed (%s).", strerror(errno));
				clear_arglist(&cmd);
				close(config_fd);
				return (EXIT_FAILURE);
			}

			/* make a copy of the content to allow closing the file */
			config = strdup(config_m);

			/* unmap local datastore file and close it */
			munmap(config_m, config_stat.st_size);
			close(config_fd);
			break;
		case 's':
			/* validate argument */
			if (strcmp (optarg, "running") == 0) {
				source = NC_DATASTORE_RUNNING;
			}
			if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) && strcmp (optarg, "startup") == 0) {
				source = NC_DATASTORE_STARTUP;
			}
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) && strcmp (optarg, "candidate") == 0) {
				source = NC_DATASTORE_CANDIDATE;
			}

			if (source == NC_DATASTORE_NONE) {
				ERROR("copy-config", "invalid source datastore specified (%s).", optarg);
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}
			break;
		case 'h':
			cmd_copyconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("copy-config", "unknown option -%c.", c);
			cmd_copyconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	target = get_datastore("target", "copy-config", &cmd, optind);

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (target == NC_DATASTORE_NONE) {
		return (EXIT_FAILURE);
	}

	/* check if edit configuration data were specified */
	if (source == NC_DATASTORE_NONE && config == NULL) {
		/* let user write edit data interactively */
		INSTRUCTION("Type the content of a configuration datastore (close editor by Ctrl-D):\n");
		config = mreadline(NULL);
		if (config == NULL) {
			ERROR("copy-config", "reading filter failed.");
			return (EXIT_FAILURE);
		}
	}

	/* create requests */
	rpc = nc_rpc_copyconfig (source, target, config);
	nc_filter_free(filter);
	if (rpc == NULL) {
		ERROR("copy-config", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("copy-config", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("copy-config", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("copy-config", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);

	return (EXIT_SUCCESS);
}

void cmd_get_help ()
{
	fprintf (stdout, "get [--help] [--filter[=file]]\n");
}


int cmd_get (char *arg)
{
	int c;
	char *data = NULL;
	struct nc_filter *filter = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"filter", 2, 0, 'f'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "f:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'f':
			filter = set_filter("get", optarg);
			if (filter == NULL) {
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}
			break;
		case 'h':
			cmd_get_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("get", "unknown option -%c.", c);
			cmd_get_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	if (optind > cmd.count) {
		ERROR("get", "invalid parameters, see \'get --help\'.");
		clear_arglist(&cmd);
		return (EXIT_FAILURE);
	}

	/* arglist is no more needed */
	clear_arglist(&cmd);

	/* create requests */
	rpc = nc_rpc_get (filter);
	nc_filter_free(filter);
	if (rpc == NULL) {
		ERROR("get", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("get", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_DATA:
		INSTRUCTION("Result:\n");
		fprintf(stdout, "%s\n", data = nc_reply_get_data (reply));
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("get", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("get", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);
	if (data) {
		free (data);
	}

	return (EXIT_SUCCESS);
}

void cmd_deleteconfig_help ()
{
	char *datastores = NULL;

	if (session == NULL) {
		datastores = "startup|candidate";
	} else if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
			datastores = "startup|candidate";
		} else {
			datastores = "startup";
		}
	} else if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		datastores = "candidate";
	} else {
		fprintf (stdout, "delete-config can not be used in the current session.\n");
		return;
	}

	fprintf (stdout, "delete-config [--help]  %s\n", datastores);
}

int cmd_deleteconfig (char *arg)
{
	int c;
	NC_DATASTORE target;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("delete-config", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	if (!nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) && !nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		ERROR ("delete-config", "operation can not be used in the current session.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			cmd_deleteconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("delete-config", "unknown option -%c.", c);
			cmd_deleteconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	target = get_datastore("target", "delete-config", &cmd, optind);
	while (target == NC_DATASTORE_RUNNING) {
		fprintf (stdout, "delete-config: <running> datastore cannot be deleted.");
		target = get_datastore("target", "delete-config", &cmd, cmd.count);
	}

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (target == NC_DATASTORE_NONE) {
		return (EXIT_FAILURE);
	}

	/* create requests */
	rpc = nc_rpc_deleteconfig(target);
	if (rpc == NULL) {
		ERROR("delete-config", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("delete-config", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("delete-config", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("delete-config", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);

	return (EXIT_SUCCESS);
}

void cmd_killsession_help ()
{
	fprintf (stdout, "kill-session [--help] <sessionID>\n");
}

int cmd_killsession (char *arg)
{
	int c;
	char *id;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("kill-session", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			cmd_killsession_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("kill-session", "unknown option -%c.", c);
			cmd_killsession_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	if ((optind + 1) == cmd.count) {
		id = strdup(cmd.list[optind]);
	} else {
		id = malloc (sizeof(char) * BUFFER_SIZE);
		if (id == NULL) {
			ERROR("kill-session", "memory allocation error (%s).", strerror (errno));
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
		id[0] = 0;

		while (strlen(id) == 0) {
			/* get mandatory argument */
			INSTRUCTION("Set session ID to kill: ");
			scanf ("%1023s", id);
		}
	}

	/* arglist is no more needed */
	clear_arglist(&cmd);

	/* create requests */
	rpc = nc_rpc_killsession(id);
	free(id);
	if (rpc == NULL) {
		ERROR("kill-session", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("kill-session", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("kill-session", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("kill-session", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);

	return (EXIT_SUCCESS);
}

void cmd_getconfig_help ()
{
	/* if session not established, print complete help for all capabilities */
	fprintf (stdout, "get-config [--help] [--filter[=file]] running");
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		fprintf (stdout, "|startup");
	}
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		fprintf (stdout, "|candidate");
	}
	fprintf (stdout, "\n");
}

int cmd_getconfig (char *arg)
{
	int c;
	char *data = NULL;
	NC_DATASTORE target;
	struct nc_filter *filter = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"filter", 2, 0, 'f'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get-config", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "f:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'f':
			filter = set_filter("get-config", optarg);
			if (filter == NULL) {
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}
			break;
		case 'h':
			cmd_getconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("get-config", "unknown option -%c.", c);
			cmd_getconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	target = get_datastore("target", "get-config", &cmd, optind);

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (target == NC_DATASTORE_NONE) {
		return (EXIT_FAILURE);
	}

	/* create requests */
	rpc = nc_rpc_getconfig (target, filter);
	nc_filter_free(filter);
	if (rpc == NULL) {
		ERROR("get-config", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("get-config", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_DATA:
		INSTRUCTION("Result:\n");
		fprintf(stdout, "%s\n", data = nc_reply_get_data (reply));
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("get-config", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("get-config", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);
	if (data) {
		free (data);
	}

	return (EXIT_SUCCESS);
}

void cmd_un_lock_help (char* operation)
{
	/* if session not established, print complete help for all capabilities */
	fprintf (stdout, "%s running", operation);
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		fprintf (stdout, "|startup");
	}
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		fprintf (stdout, "|candidate");
	}
	fprintf (stdout, "\n");
}

#define LOCK_OP 1
#define UNLOCK_OP 2
int cmd_un_lock (int op, char *arg)
{
	int c;
	NC_DATASTORE target;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;
	char *operation;

	switch (op) {
	case LOCK_OP:
		operation = "lock";
		break;
	case UNLOCK_OP:
		operation = "unlock";
		break;
	default:
		ERROR("cmd_un_lock()", "Wrong use of internal function (Invalid parameter)");
		return (EXIT_FAILURE);
	}

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR(operation, "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			cmd_un_lock_help (operation);
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR(operation, "unknown option -%c.", c);
			cmd_un_lock_help (operation);
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	target = get_datastore("target", operation, &cmd, optind);

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (target == NC_DATASTORE_NONE) {
		return (EXIT_FAILURE);
	}

	/* create requests */
	switch (op) {
	case LOCK_OP:
		rpc = nc_rpc_lock(target);
		break;
	case UNLOCK_OP:
		rpc = nc_rpc_unlock(target);
		break;
	}
	if (rpc == NULL) {
		ERROR(operation, "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR(operation, "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR(operation, "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR(operation, "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);

	return (EXIT_SUCCESS);
}

int cmd_lock (char *arg)
{
	return cmd_un_lock (LOCK_OP, arg);
}

int cmd_unlock (char *arg)
{
	return cmd_un_lock (UNLOCK_OP, arg);
}

void cmd_connect_help ()
{
	fprintf (stdout, "connect [--help] [--port <num>] [--login <username>] host\n");
}

int cmd_connect (char* arg)
{
	char *host = NULL, *user = NULL;
	int hostfree = 0;
	unsigned short port = 830;
	int c;
	struct arglist cmd;
	struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"port", 1, 0, 'p'},
			{"login", 1, 0, 'l'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session != NULL) {
		ERROR("connect", "already connected to %s.", nc_session_get_host (session));
		return (EXIT_FAILURE);
	}

	/* process given arguments */
	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "hp:l:", long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			cmd_connect_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		case 'p':
			port = (unsigned short) atoi (optarg);
			break;
		case 'l':
			user = optarg;
			break;
		default:
			ERROR("connect", "unknown option -%c.", c);
			cmd_connect_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}
	if (optind == cmd.count) {
		/* get mandatory argument */
		host = malloc (sizeof(char) * BUFFER_SIZE);
		if (host == NULL) {
			ERROR("connect", "memory allocation error (%s).", strerror (errno));
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
		hostfree = 1;
		INSTRUCTION("Hostname to connect to: ");
		scanf ("%1023s", host);
	} else if ((optind + 1) == cmd.count) {
		host = cmd.list[optind];
	}

	/* create the session */
	session = nc_session_connect (host, port, user, NULL);
	if (session == NULL) {
		ERROR("connect", "connecting to the %s failed.", host);
		if (hostfree) {
			free (host);
		}
		clear_arglist(&cmd);
		return (EXIT_FAILURE);
	}
	if (hostfree) {
		free (host);
	}
	clear_arglist(&cmd);

	return (EXIT_SUCCESS);
}

int cmd_disconnect (char* arg)
{
	if (session == NULL) {
		ERROR("disconnect", "not connected to any NETCONF server.");
	} else {
		nc_session_close (session, "NETCONF session closed by client");
		nc_session_free (session);
		session = NULL;
	}

	return (EXIT_SUCCESS);
}

int cmd_quit (char* arg)
{
	done = 1;
	if (session != NULL) {
		cmd_disconnect (NULL);
	}
	return (0);
}

int cmd_verbose (char *arg)
{
	if (verb_level != 1) {
		verb_level = 1;
		nc_verbosity (NC_VERB_VERBOSE);
		fprintf (stdout, "Verbose level set to VERBOSE\n");
	} else {
		verb_level = 0;
		nc_verbosity (NC_VERB_ERROR);
		fprintf (stdout, "Verbose messages switched off\n");
	}

	return (EXIT_SUCCESS);
}

int cmd_debug (char *arg)
{
	if (verb_level != 2) {
		verb_level = 2;
		nc_verbosity (NC_VERB_DEBUG);
		fprintf (stdout, "Verbose level set to DEBUG\n");
	} else {
		verb_level = 0;
		nc_verbosity (NC_VERB_ERROR);
		fprintf (stdout, "Verbose messages switched off\n");
	}

	return (EXIT_SUCCESS);
}

int cmd_help (char* arg)
{
	int i;
	char *cmd = NULL;
	char cmdline[BUFFER_SIZE];

	strtok (arg, " ");
	if ((cmd = strtok (NULL, " ")) == NULL) {
		/* generic help for the application */
		print_version ();

generic_help:
		INSTRUCTION("Available commands:\n");

		for (i = 0; commands[i].name; i++) {
			if (commands[i].helpstring != NULL) {
				fprintf (stdout, "  %-15s %s\n", commands[i].name, commands[i].helpstring);
			}
		}
	} else {
		/* print specific help for the selected command */

		/* get the command of the specified name */
		for (i = 0; commands[i].name; i++) {
			if (strcmp(cmd, commands[i].name) == 0) {
				break;
			}
		}

		/* execute the command's help if any valid command specified */
		if (commands[i].name) {
			snprintf(cmdline, BUFFER_SIZE, "%s --help", commands[i].name);
			commands[i].func(cmdline);
		} else {
			/* if unknown command specified, print the list of commands */
			fprintf(stdout, "Unknown command \'%s\'\n", cmd);
			goto generic_help;
		}
	}

	return (0);
}

void cmd_userrpc_help()
{
	fprintf (stdout, "user-rpc [--help] [--file <file>]]\n");
}

int cmd_userrpc(char *arg)
{
	int c;
	int config_fd;
	struct stat config_stat;
	char *config = NULL, *config_m = NULL;
	char *data = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"file", 1, 0, 'f'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("user-rpc", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "f:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'f':
			/* open edit configuration data from the file */
			config_fd = open(optarg, O_RDONLY);
			if (config_fd == -1) {
				ERROR("user-rpc", "unable to open local file (%s).", strerror(errno));
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}

			/* map content of the file into the memory */
			fstat(config_fd, &config_stat);
			config_m = (char*) mmap(NULL, config_stat.st_size, PROT_READ, MAP_PRIVATE, config_fd, 0);
			if (config_m == MAP_FAILED) {
				ERROR("user-rpc", "mmapping local datastore file failed (%s).", strerror(errno));
				clear_arglist(&cmd);
				close(config_fd);
				return (EXIT_FAILURE);
			}

			/* make a copy of the content to allow closing the file */
			config = strdup(config_m);

			/* unmap local datastore file and close it */
			munmap(config_m, config_stat.st_size);
			close(config_fd);
			break;
		case 'h':
			cmd_userrpc_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("user-rpc", "unknown option -%c.", c);
			cmd_userrpc_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	/* arglist is no more needed */
	clear_arglist(&cmd);

	if (config == NULL) {
		INSTRUCTION("Type the content of a RPC operation (close editor by Ctrl-D):\n");
		config = mreadline(NULL);
		if (config == NULL) {
			ERROR("copy-config", "reading filter failed.");
			return (EXIT_FAILURE);
		}
	}

	/* create requests */
	rpc = nc_rpc_generic (config);
	free(config);
	if (rpc == NULL) {
		ERROR("user-rpc", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		nc_rpc_free (rpc);
		if (nc_session_get_status(session) != NC_SESSION_STATUS_WORKING) {
			ERROR("user-rpc", "receiving rpc-reply failed.");
			INSTRUCTION("Closing the session.\n");
			cmd_disconnect(NULL);
			return (EXIT_FAILURE);
		}
		return (EXIT_SUCCESS);
	}
	nc_rpc_free (rpc);

	/* parse result */
	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_OK:
		INSTRUCTION("Result OK\n");
		break;
	case NC_REPLY_DATA:
		INSTRUCTION("Result:\n");
		fprintf(stdout, "%s\n", data = nc_reply_get_data (reply));
		if (data) {
			free (data);
		}
		break;
	case NC_REPLY_ERROR:
		/* wtf, you shouldn't be here !?!? */
		ERROR("user-rpc", "operation failed, but rpc-error was not processed.");
		break;
	default:
		ERROR("user-rpc", "unexpected operation result.");
		break;
	}
	nc_reply_free (reply);

	return (EXIT_SUCCESS);
}

