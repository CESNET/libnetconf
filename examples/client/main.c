#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "commands.h"
#include "../../src/libnetconf.h"

#define VERSION "0.1"
#define PROMPT "netconf> "

volatile int done = 0;
extern COMMAND commands[];

int clb_print(const char* msg)
{
	return (fprintf(stderr, "libnetconf: %s\n", msg));
}

void print_version()
{
	fprintf(stdout, "libnetconf client version: %s\n", VERSION);
	fprintf(stdout, "compile time: %s, %s\n", __DATE__, __TIME__);
}

char * cmd_generator(const char *text, int state);
char ** cmd_completion(const char *text, int start, int end);


/**
 * \brief Tell the GNU Readline library how to complete commands.
 *
 * We want to try to complete on command names if this is the first word in the
 * line, or on filenames if not.
 */
void initialize_readline()
{
	/* Allow conditional parsing of the ~/.inputrc file. */
	rl_readline_name = "netconf";

	/* Tell the completer that we want a crack first. */
	rl_attempted_completion_function = cmd_completion;
}

/**
 * \brief Attempt to complete available program commands.
 *
 * Attempt to complete on the contents of #text. #start and #end bound the
 * region of rl_line_buffer that contains the word to complete. #text is the
 * word to complete.  We can use the entire contents of rl_line_buffer in case
 * we want to do some simple parsing.
 *
 * \return The array of matches, or NULL if there aren't any.
 */
char ** cmd_completion(const char *text, int start, int end)
{
	char **matches;

	matches = (char **) NULL;

	/* If this word is at the start of the line, then it is a command
	 to complete.  Otherwise it is the name of a file in the current
	 directory. */
	if (start == 0) {
		matches = rl_completion_matches(text, cmd_generator);
	}

	return (matches);
}

/* Generator function for command completion.  STATE lets us know whether
 to start from scratch; without any state (i.e. STATE == 0), then we
 start at the top of the list. */
char * cmd_generator(const char *text, int state)
{
	static int list_index, len;
	char *name;

	/* If this is a new word to complete, initialize now.  This includes
	 saving the length of TEXT for efficiency, and initializing the index
	 variable to 0. */
	if (!state) {
		list_index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches from the command list. */
	while ((name = commands[list_index].name) != NULL) {
		list_index++;

		if (strncmp(name, text, len) == 0) {
			return (strdup(name));
		}
	}

	/* If no names matched, then return NULL. */
	return ((char *) NULL);
}

int main(int argc, char *argv[])
{
	char *cmdline, *cmdstart;
	int i, j;
	char *cmd;

	initialize_readline();

	/* disable publickey authentication */
	nc_ssh_pref(NC_SSH_AUTH_PUBLIC_KEYS, -1);

	/* set verbosity and function to print libnetconf's messages */
	nc_callback_print(clb_print);

	while (!done) {
		/* get the command from user */
		cmdline = readline(PROMPT);

		/* EOF -> exit */
		if (cmdline == NULL) {
			done = 1;
			break;
		}

		/* empty line -> wait for another command */
		if (*cmdline == 0) {
			free(cmdline);
			continue;
		}

		/* Isolate the command word. */
		for (i = 0; cmdline[i] && whitespace (cmdline[i]); i++);
		cmdstart = cmdline + i;
		for (j = 0; cmdline[i] && !whitespace (cmdline[i]); i++, j++);
		cmd = strndup(cmdstart, j);

		/* parse the command line */
		for (i = 0; commands[i].name; i++) {
			if (strcmp(cmd, commands[i].name) == 0) {
				break;
			}
		}

		/* execute the command if any valid specified */
		if (commands[i].name) {
			commands[i].func(cmdstart);
			add_history(cmdline);
		} else {
			/* if unknown command specified, tell it to user */
			fprintf(stdout, "%s: no such command, type 'help' for more information.\n", cmd);
		}

		free(cmd);
		free(cmdline);
	}

	/* bye, bye */
	return (EXIT_SUCCESS);
}
