/**
 * \file callbacks.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Default callbacks and functions to set application's callbacks.
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

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#include "netconf_internal.h"

void callback_sshauth_interactive_default (const char* name,
		int name_len,
		const char* instruction,
		int instruction_len,
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void** abstract);

char* callback_sshauth_publickey_default (const char* username,
		const char* hostname,
		const char* privatekey_filepath);

char* callback_sshauth_password_default (const char* username, const char* hostname);
int callback_ssh_hostkey_check_default (const char* hostname, int keytype, const char* fingerprint);

struct callbacks callbacks = {
		NULL, /* message printing callback */
		callback_sshauth_interactive_default, /* default keyboard_interactive callback */
		callback_sshauth_password_default, /* default password callback */
		callback_sshauth_publickey_default, /* default publickey (get passphrase) callback */
		callback_ssh_hostkey_check_default, /* default hostkey check callback */
		NULL, /* publickey file path */
		NULL  /* privatekey file path */
};

void nc_callback_print(int (*func)(const char* msg))
{
	callbacks.print = func;
}

void nc_callback_sshauth_interactive(void (*func)(const char* name,
		int name_len,
		const char* instruction,
		int instruction_len,
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void** abstract))
{
	if (func != NULL) {
		callbacks.sshauth_interactive = func;
	}
}

void nc_callback_sshauth_password(char* (*func)(const char* username,
		const char* hostname))
{
	if (func != NULL) {
		callbacks.sshauth_password = func;
	}
}

void nc_callback_sshauth_passphrase(char* (*func)(const char* username,
		const char* hostname, const char* priv_key_file))
{
	if (func != NULL) {
		callbacks.sshauth_passphrase = func;
	}
}

void nc_callback_ssh_host_authenticity_check(int (*func)(const char* hostname,
		int keytype, const char* fingerprint))
{
	if (func != NULL) {
		callbacks.hostkey_check = func;
	}
}

char* callback_sshauth_password_default (const char* username,
		const char* hostname)
{
	char* buf;
	int buflen = 1024, len = 0;
	int c;
	struct termios newterm, oldterm;

	buf = malloc (buflen * sizeof(char));
	if (buf == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	if (tcgetattr(STDIN_FILENO, &oldterm) != 0) {
		ERROR("tcgetattr: %s", strerror(errno));
	}

	fprintf(stdout, "%s@%s password: ", username, hostname);
	fflush(stdout);

	/* system("stty -echo"); */
	newterm = oldterm;
	newterm.c_lflag &= ~ECHO;
	tcflush(STDIN_FILENO, TCIFLUSH);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &newterm) != 0) {
		ERROR("tcseattr: %s", strerror(errno));
	}

	while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') {
		if (len >= (buflen-1)) {
			buflen *= 2;
			buf = realloc (buf, buflen*sizeof (char));
		}
		buf[len++] = (char)c;
	}
	buf[len++] = 0; /* terminating null byte */

	/* system ("stty echo"); */
	if (tcsetattr(STDIN_FILENO, TCSANOW, &oldterm) != 0) {
		ERROR("tcseattr: %s", strerror(errno));
	}

	fprintf(stdout, "\n");

	return (buf);
}

/**
 * @brief Default callback for \"keyboard-interactive\" authentication method
 *
 * called by libssh2, see libssh2 doc for details
 */
void callback_sshauth_interactive_default (const char* name,
		int name_len,
		const char* instruction,
		int instruction_len,
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void** abstract)
{
	int i, buflen = 8;
	int c;
	struct termios newterm, oldterm;

	if (tcgetattr(STDIN_FILENO, &oldterm) != 0) {
		ERROR("tcgetattr: %s", strerror(errno));
	}

	for (i=0; i<num_prompts; i++) {
		fwrite (prompts[i].text, sizeof(char), prompts[i].length, stdout);
		fflush(stdout);
		if (prompts[i].echo == 0) {
			/* system("stty -echo"); */
			newterm = oldterm;
			newterm.c_lflag &= ~ECHO;
			tcflush(STDIN_FILENO, TCIFLUSH);
			if (tcsetattr(STDIN_FILENO, TCSANOW, &newterm) != 0) {
				ERROR("tcseattr: %s", strerror(errno));
			}
		}
		responses[i].text = malloc (buflen*sizeof(char));
		responses[i].length = 0;

		while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') {
			if (responses[i].length >= (buflen-1)) {
				buflen *= 2;
				responses[i].text = realloc (responses[i].text, buflen*sizeof (char));
			}
			responses[i].text[responses[i].length++] = c;
		}
		/* terminating null byte */
		responses[i].text[responses[i].length++] = '\0';

		/* system ("stty echo"); */
		if (tcsetattr(STDIN_FILENO, TCSANOW, &oldterm) != 0) {
			ERROR("tcseattr: %s", strerror(errno));
		}

		fprintf(stdout, "\n");
	}
	return;
}

char* callback_sshauth_publickey_default (const char* username,
		const char* hostname,
		const char* privatekey_filepath)
{
	int c;
	char* buf;
	int buflen = 1024, len = 0;

	buf = malloc (buflen * sizeof(char));
	if (buf == NULL) {
		ERROR("Memory allocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	fprintf(stdout, "Enter passphrase for key '%s':", privatekey_filepath);
	system("stty -echo");
	fflush(stdin);

	while ((c = getchar ()) != '\n') {
		if (len >= (buflen-1)) {
			buflen *= 2;
			buf = realloc (buf, buflen*sizeof (char));
		}
		buf[len++] = (char)c;
	}
	buf[len++] = 0; /* terminating null byte */
	system ("stty echo");
	fprintf(stdout, "\n");

	return (buf);
}

int callback_ssh_hostkey_check_default (const char* hostname, int keytype, const char* fingerprint)
{
	int c;
	char answer[5];

	fprintf(stdout, "The authenticity of host \'%s\' can't be established.\n", hostname);
	fprintf(stdout, "%s key fingerprint is %s.\n", (keytype == 2) ? "DSS" : "RSA", fingerprint);
	fprintf(stdout, "Are you sure you want to continue connecting (yes/no)? ");

again:
	fscanf(stdin, "%4s", answer);
	while ((c = getchar ()) != EOF && c != '\n');

	fflush(stdin);
	if (strcmp("yes", answer) == 0) {
		return (EXIT_SUCCESS);
	} else if (strcmp("no", answer) == 0) {
		return (EXIT_FAILURE);
	} else {
		fprintf(stdout, "Please type 'yes' or 'no': ");
		goto again;
	}
}

void nc_set_publickey_path (const char* path)
{
	if (path != NULL) {
		if (callbacks.publickey_filename != NULL) {
			free (callbacks.publickey_filename);
		}
		callbacks.publickey_filename = strdup (path);
	}
}

void nc_set_privatekey_path (const char* path)
{
	if (path != NULL) {
		if (callbacks.privatekey_filename != NULL) {
			free (callbacks.privatekey_filename);
		}
		callbacks.privatekey_filename = strdup (path);
	}
}


