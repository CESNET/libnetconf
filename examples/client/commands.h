
#include <stdlib.h>

#ifndef COMMANDS_H_
#define COMMANDS_H_


int cmd_connect(char* arg);
int cmd_disconnect(char* arg);
int cmd_getconfig(char *arg);
int cmd_help(char* arg);
int cmd_status(char* arg);
int cmd_quit(char* arg);
int cmd_debug(char *arg);
int cmd_verbose(char *arg);

typedef struct
{
	char *name; /* User printable name of the function. */
	int (*func)(char*); /* Function to call to do the command. */
	char *helpstring; /* Documentation for this function.  */
} COMMAND;


#endif /* COMMANDS_H_ */
