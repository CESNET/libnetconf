/**
 * \file callbacks.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Default callbacks and functions to set the application's callbacks.
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#ifdef ENABLE_DNSSEC
#	include <validator/validator.h>
#	include <validator/resolver.h>
#	include <validator/validator-compat.h>
#endif

#include "netconf_internal.h"

static const char rcsid[] __attribute__((used)) ="$Id: "__FILE__": "RCSID" $";

#ifndef DISABLE_LIBSSH
static void callback_sshauth_interactive_default (const char* name,
		int name_len,
		const char* instruction,
		int instruction_len,
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void** abstract);

static char* callback_sshauth_publickey_default (const char* username,
		const char* hostname,
		const char* privatekey_filepath);

static char* callback_sshauth_password_default (const char* username, const char* hostname);
static int callback_ssh_hostkey_check_default (const char* hostname, LIBSSH2_SESSION *session);
#endif

struct callbacks callbacks = {
		NULL, /* message printing callback */
		NULL, /* process_error_reply callback */
#ifndef DISABLE_LIBSSH
		callback_sshauth_interactive_default, /* default keyboard_interactive callback */
		callback_sshauth_password_default, /* default password callback */
		callback_sshauth_publickey_default, /* default publickey (get passphrase) callback */
		callback_ssh_hostkey_check_default, /* default hostkey check callback */
		{ NULL }, /* publickey file path */
		{ NULL },  /* privatekey file path */
		{ 0 }
#endif
};

API void nc_callback_print(void (*func)(NC_VERB_LEVEL level, const char* msg))
{
	callbacks.print = func;
}

API void nc_callback_error_reply(void (*func)(const char* tag,
		const char* type,
		const char* severity,
		const char* apptag,
		const char* path,
		const char* message,
		const char* attribute,
		const char* element,
		const char* ns,
		const char* sid))
{
	callbacks.process_error_reply = func;
}

#ifndef DISABLE_LIBSSH
API void nc_callback_sshauth_interactive(void (*func)(const char* name,
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
	} else {
		callbacks.sshauth_interactive = callback_sshauth_interactive_default;
	}
}

API void nc_callback_sshauth_password(char* (*func)(const char* username,
		const char* hostname))
{
	if (func != NULL) {
		callbacks.sshauth_password = func;
	} else {
		callbacks.sshauth_password = callback_sshauth_password_default;
	}
}

API void nc_callback_sshauth_passphrase(char* (*func)(const char* username,
		const char* hostname, const char* priv_key_file))
{
	if (func != NULL) {
		callbacks.sshauth_passphrase = func;
	} else {
		callbacks.sshauth_passphrase = callback_sshauth_publickey_default;
	}
}

API void nc_callback_ssh_host_authenticity_check(int (*func)(const char* hostname,
		LIBSSH2_SESSION *session))
{
	if (func != NULL) {
		callbacks.hostkey_check = func;
	} else {
		callbacks.hostkey_check = callback_ssh_hostkey_check_default;
	}
}

static char* callback_sshauth_password_default (const char* username,
		const char* hostname)
{
	char* buf, *newbuf;
	int buflen = 1024, len = 0;
	char c = 0;
	struct termios newterm, oldterm;
	FILE* tty;

	buf = malloc (buflen * sizeof(char));
	if (buf == NULL) {
		ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
		return (NULL);
	}

	if ((tty = fopen("/dev/tty", "r+")) == NULL) {
		ERROR("Unable to open the current terminal (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
		return (NULL);
	}

	if (tcgetattr(fileno(tty), &oldterm) != 0) {
		ERROR("Unable to get terminal settings (%d: %s).", __LINE__, strerror(errno));
		return (NULL);
	}

	fprintf(tty, "%s@%s password: ", username, hostname);
	fflush(tty);

	/* system("stty -echo"); */
	newterm = oldterm;
	newterm.c_lflag &= ~ECHO;
	newterm.c_lflag &= ~ICANON;
	tcflush(fileno(tty), TCIFLUSH);
	if (tcsetattr(fileno(tty), TCSANOW, &newterm) != 0) {
		ERROR("Unable to change terminal settings for hiding password (%d: %s).", __LINE__, strerror(errno));
		return (NULL);
	}

	while (fread(&c, 1, 1, tty) == 1 && c != '\n') {
		if (len >= (buflen-1)) {
			buflen *= 2;
			newbuf = realloc(buf, buflen*sizeof (char));
			if (newbuf == NULL) {
				ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));

				/* remove content of the buffer */
				memset(buf, 0, len);
				free(buf);

				/* restore terminal settings */
				if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
					ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
				}
				return (NULL);
			} else {
				buf = newbuf;
			}
		}
		buf[len++] = c;
	}
	buf[len++] = 0; /* terminating null byte */

	/* system ("stty echo"); */
	if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
		ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
		/*
		 * terminal probably still hides input characters, but we have password
		 * and anyway we are unable to set terminal to the previous state, so
		 * just continue
		 */
	}
	fprintf(tty, "\n");

	fclose(tty);
	return (buf);
}

/**
 * @brief Default callback for the \"keyboard-interactive\" authentication method
 *
 * called by libssh2, see http://www.libssh2.org/libssh2_userauth_keyboard_interactive.html for details
 */
static void callback_sshauth_interactive_default (const char*  UNUSED(name),
		int  UNUSED(name_len),
		const char*  UNUSED(instruction),
		int  UNUSED(instruction_len),
		int num_prompts,
		const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
		LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
		void**  UNUSED(abstract))
{
	int i;
	unsigned int buflen = 8;
	char c = 0;
	struct termios newterm, oldterm;
	char* newtext;
	FILE* tty;

	if ((tty = fopen("/dev/tty", "r+")) == NULL) {
		ERROR("Unable to open the current terminal (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
		return;
	}

	if (tcgetattr(fileno(tty), &oldterm) != 0) {
		ERROR("Unable to get terminal settings (%d: %s).", __LINE__, strerror(errno));
		return;
	}

	for (i=0; i<num_prompts; i++) {
		if (fwrite (prompts[i].text, sizeof(char), prompts[i].length, tty) == 0) {
			ERROR("Writing the authentication prompt into stdout failed.");
			return;
		}
		fflush(tty);
		if (prompts[i].echo == 0) {
			/* system("stty -echo"); */
			newterm = oldterm;
			newterm.c_lflag &= ~ECHO;
			tcflush(fileno(tty), TCIFLUSH);
			if (tcsetattr(fileno(tty), TCSANOW, &newterm) != 0) {
				ERROR("Unable to change terminal settings for hiding password (%d: %s).", __LINE__, strerror(errno));
				return;
			}
		}
		responses[i].length = 0;
		responses[i].text = malloc (buflen*sizeof(char));
		if (responses[i].text == 0) {
			ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
			/* restore terminal settings */
			if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
				ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
			}
			return;
		}

		while (fread(&c, 1, 1, tty) == 1 && c != '\n') {
			if (responses[i].length >= (buflen-1)) {
				buflen *= 2;
				newtext = realloc(responses[i].text, buflen*sizeof (char));
				if (newtext == NULL) {
					ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
					/* remove all answers, something really bad is happening */
					for(; i >= 0; i--) {
						memset(responses[i].text, 0, responses[i].length);
						free(responses[i].text);
						responses[i].length = 0;
					}
					/* restore terminal settings */
					if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
						ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
					}
					return;
				} else {
					responses[i].text = newtext;
				}
			}
			responses[i].text[responses[i].length++] = c;
		}
		/* terminating null byte */
		responses[i].text[responses[i].length++] = '\0';

		/* system ("stty echo"); */
		if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
			ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
			/*
			 * terminal probably still hides input characters, but we have password
			 * and anyway we are unable to set terminal to the previous state, so
			 * just continue
			 */
		}

		fprintf(tty, "\n");
		fflush(tty);
	}

	fclose(tty);
	return;
}

static char* callback_sshauth_publickey_default (const char*  UNUSED(username),
		const char*  UNUSED(hostname),
		const char* privatekey_filepath)
{
	char c, *buf, *newbuf;
	int buflen = 1024, len = 0;
	struct termios newterm, oldterm;
	FILE* tty;

	buf = malloc (buflen * sizeof(char));
	if (buf == NULL) {
		ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
		return (NULL);
	}

	if ((tty = fopen("/dev/tty", "r+")) == NULL) {
		ERROR("Unable to open the current terminal (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
		return (NULL);
	}

	if (tcgetattr(fileno(tty), &oldterm) != 0) {
		ERROR("Unable to get terminal settings (%d: %s).", __LINE__, strerror(errno));
		return (NULL);
	}

	fprintf(tty, "Enter passphrase for the key '%s':", privatekey_filepath);
	fflush(tty);

	/* system("stty -echo"); */
	newterm = oldterm;
	newterm.c_lflag &= ~ECHO;
	newterm.c_lflag &= ~ICANON;
	tcflush(fileno(tty), TCIFLUSH);
	if (tcsetattr(fileno(tty), TCSANOW, &newterm) != 0) {
		ERROR("Unable to change terminal settings for hiding password (%d: %s).", __LINE__, strerror(errno));
		return (NULL);
	}

	while (fread(&c, 1, 1, tty) == 1 && c != '\n') {
		if (len >= (buflen-1)) {
			buflen *= 2;
			newbuf = realloc (buf, buflen*sizeof (char));
			if (newbuf == NULL) {
				ERROR("Memory allocation failed (%s:%d - %s).", __FILE__, __LINE__, strerror(errno));
				/* remove content of the buffer */
				memset(buf, 0, len);
				free(buf);

				/* restore terminal settings */
				if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
					ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
				}

				return (NULL);
			}
			buf = newbuf;
		}
		buf[len++] = (char)c;
	}
	buf[len++] = 0; /* terminating null byte */

	/* system ("stty echo"); */
	if (tcsetattr(fileno(tty), TCSANOW, &oldterm) != 0) {
		ERROR("Unable to restore terminal settings (%d: %s).", __LINE__, strerror(errno));
		/*
		 * terminal probably still hides input characters, but we have password
		 * and anyway we are unable to set terminal to the previous state, so
		 * just continue
		 */
	}
	fprintf(tty, "\n");

	fclose(tty);
	return (buf);
}

#ifdef ENABLE_DNSSEC

/* return 0 (DNSSEC + key valid), 1 (unsecure DNS + key valid), 2 (key not found or an error) */
/* type - 1 (RSA), 2 (DSA), 3 (ECDSA); alg - 1 (SHA1), 2 (SHA-256) */
static int callback_ssh_hostkey_hash_dnssec_check(const char* hostname, const char* sha1hash, int type, int alg) {
	ns_msg handle;
	ns_rr rr;
	val_status_t val_status;
	const unsigned char* rdata;
	unsigned char buf[4096];
	int buf_len = 4096;
	int ret = 0, i, j, len;

	/* class 1 - internet, type 44 - SSHFP */
	len = val_res_query(NULL, hostname, 1, 44, buf, buf_len, &val_status);

	if (len < 0 || !val_istrusted(val_status)) {
		ret = 2;
		goto finish;
	}

	if (ns_initparse(buf, len, &handle) < 0) {
		ERROR("Failed to initialize DNSSEC response parser.");
		ret = 2;
		goto finish;
	}

	if ((i = libsres_msg_getflag(handle, ns_f_rcode)) != 0) {
		ERROR("DNSSEC query returned %d.", i);
		ret = 2;
		goto finish;
	}

	if (!libsres_msg_getflag(handle, ns_f_ad)) {
		/* response not secured by DNSSEC */
		ret = 1;
	}

	/* query section */
	if (ns_parserr(&handle, ns_s_qd, 0, &rr)) {
		ERROR("DNSSEC query section parser fail.");
		ret = 2;
		goto finish;
	}

	if (strcmp(hostname, ns_rr_name(rr)) != 0 || ns_rr_type(rr) != 44 || ns_rr_class(rr) != 1) {
		ERROR("DNSSEC query in the answer does not match the original query.");
		ret = 2;
		goto finish;
	}

	/* answer section */
	i = 0;
	while (ns_parserr(&handle, ns_s_an, i, &rr) == 0) {
		if (ns_rr_type(rr) != 44) {
			++i;
			continue;
		}

		rdata = ns_rr_rdata(rr);
		if (rdata[0] != type) {
			++i;
			continue;
		}
		if (rdata[1] != alg) {
			++i;
			continue;
		}

		/* we found the correct SSHFP entry */
		rdata += 2;
		for (j = 0; j < 20; ++j) {
			if (rdata[j] != (unsigned char)sha1hash[j]) {
				ret = 2;
				goto finish;
			}
		}

		/* server fingerprint is supported by a DNS entry,
		 * we have already determined if DNSSEC was used or not
		 */
		goto finish;
	}

	/* no match */
	ret = 2;
finish:
	val_free_validator_state();
	return ret;
}

#endif

static int callback_ssh_hostkey_check_default (const char* hostname, LIBSSH2_SESSION *session)
{
	struct passwd *pw;
	char *knownhosts_dir = NULL;
	char *knownhosts_file = NULL;
	LIBSSH2_KNOWNHOSTS *knownhosts;
	int c, i;
	int ret, knownhost_check = -1;
	int fd;
	int hostkey_type, hostkey_typebit;
	size_t len;
	struct libssh2_knownhost *ssh_host = NULL;
	char answer[5];
	const char *remotekey, *fingerprint_raw;
#ifdef ENABLE_DNSSEC
	const char* fingerprint_sha1raw;
#endif
	/*
	 * to print MD5 raw hash, we need 3*16 + 1 bytes (4 characters are printed
	 * all the time, but except the last one, NULL terminating bytes are
	 * rewritten by the following value). In the end, the last ':' is removed
	 * for nicer output, so there are two terminating NULL bytes in the end.
	 */
	char fingerprint_md5[49];

	/* get current user to locate SSH known_hosts file */
	pw = getpwuid(getuid());
	if (pw == NULL) {
		/* unable to get correct username (errno from getpwuid) */
		ERROR("Unable to set a username for the SSH connection (%s).", strerror(errno));
		return (EXIT_FAILURE);
	} else if (asprintf(&knownhosts_dir, "%s/.ssh", pw->pw_dir) == -1) {
		ERROR("asprintf() failed (%s:%d).", __FILE__, __LINE__);
		return (EXIT_FAILURE);
	}

	knownhosts = libssh2_knownhost_init(session);
	if (knownhosts == NULL) {
		ERROR("Unable to create knownhosts structure (%s:%d).", __FILE__, __LINE__);
		free(knownhosts_dir);
		return (EXIT_FAILURE);
	}

	/* set general knownhosts file used also by OpenSSH's applications */
	if (asprintf(&knownhosts_file, "%s/known_hosts", knownhosts_dir) == -1) {
		ERROR("%s: asprintf() failed.", __func__);
		free(knownhosts_dir);
		libssh2_knownhost_free(knownhosts);
		return(EXIT_FAILURE);
	}

	/* get all the hosts */
	ret = libssh2_knownhost_readfile(knownhosts, knownhosts_file, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	if (ret < 0) {
		/*
		 * default known_hosts may contain keys that are not supported
		 * by libssh2, so try to use libnetconf's specific known_hosts
		 * file located in the same directory and named
		 * 'netconf_known_hosts'
		 */
		free(knownhosts_file);
		knownhosts_file = NULL;
		libssh2_knownhost_free(knownhosts);
		if (asprintf(&knownhosts_file, "%s/netconf_known_hosts", knownhosts_dir) == -1) {
			ERROR("%s: asprintf() failed.", __func__);
			return(EXIT_FAILURE);
		}
		/* create own knownhosts file if it does not exist */
		if (eaccess(knownhosts_file, F_OK) != 0) {
			if ((fd = creat(knownhosts_file, S_IWUSR | S_IRUSR |S_IRGRP | S_IROTH)) != -1) {
				close(fd);
			}
		}
		knownhosts = libssh2_knownhost_init(session);
		if (knownhosts == NULL) {
			ERROR("Unable to create knownhosts structure (%s:%d).", __FILE__, __LINE__);
			return (EXIT_FAILURE);
		}
		ret = libssh2_knownhost_readfile(knownhosts, knownhosts_file, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	}
	free(knownhosts_dir);

	/* get host's key */
	remotekey = libssh2_session_hostkey(session, &len, &hostkey_type);
	if (remotekey == NULL && hostkey_type == LIBSSH2_HOSTKEY_TYPE_UNKNOWN) {
		ERROR("Unable to get host key.");
		libssh2_knownhost_free(knownhosts);
		return (EXIT_FAILURE);
	}
	hostkey_typebit = (hostkey_type == LIBSSH2_HOSTKEY_TYPE_RSA) ? LIBSSH2_KNOWNHOST_KEY_SSHRSA : LIBSSH2_KNOWNHOST_KEY_SSHDSS;

	if (ret < 0) {
		WARN("Unable to check against the knownhost file (%s).", knownhosts_file);
		libssh2_knownhost_free(knownhosts);
		knownhosts = libssh2_knownhost_init(session);
keynotfound:

		if (stdin != NULL && stdout != NULL) {
			/* MD5 hash size is 16B, SHA1 hash size is 20B */
			fingerprint_raw = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_MD5);
			for (i = 0; i < 16; i++) {
				sprintf(&fingerprint_md5[i * 3], "%02x:", (uint8_t) fingerprint_raw[i]);
			}
			fingerprint_md5[47] = 0;

#ifdef ENABLE_DNSSEC
			fingerprint_sha1raw = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
			ret = callback_ssh_hostkey_hash_dnssec_check(hostname, fingerprint_sha1raw,
														(hostkey_type == LIBSSH2_HOSTKEY_TYPE_RSA ? 1 : 2), 1);

			/* DNSSEC SSHFP check successful, that's enough */
			if (ret == 0) {
				DBG("DNSSEC SSHFP check successful");
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_SUCCESS);
			}
#endif

			/* try to get result from user */
			fprintf(stdout, "The authenticity of the host \'%s\' cannot be established.\n", hostname);
			fprintf(stdout, "%s key fingerprint is %s.\n", (hostkey_type == LIBSSH2_HOSTKEY_TYPE_RSA) ? "RSA" : "DSS", fingerprint_md5);
#ifdef ENABLE_DNSSEC
			if (ret == 2) {
				fprintf(stdout, "No matching host key fingerprint found in DNS.\n");
			} else if (ret == 1) {
				fprintf(stdout, "Matching host key fingerprint found in DNS.\n");
			}
#endif
			fprintf(stdout, "Are you sure you want to continue connecting (yes/no)? ");

askuseragain:
			if (fscanf(stdin, "%4s", answer) == EOF) {
				ERROR("fscanf() failed (%s).", strerror(errno));
				free(knownhosts_file);
				return (EXIT_FAILURE);
			}
			while ((c = getchar()) != EOF && c != '\n');

			fflush(stdin);
			if (strcmp("yes", answer) == 0) {
				/* store the key into the host file */
				ret = libssh2_knownhost_add(knownhosts,
						hostname,
						NULL,
						remotekey,
						len,
						LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | hostkey_typebit,
						NULL);
				if (ret != 0) {
					WARN("Adding the known host %s failed!", hostname);
				} else if (knownhosts_file != NULL) {
					ret = libssh2_knownhost_writefile(knownhosts,
							knownhosts_file,
							LIBSSH2_KNOWNHOST_FILE_OPENSSH);
					if (ret) {
						WARN("Writing %s failed!", knownhosts_file);
					}
				} else {
					WARN("Unknown known_hosts file location, skipping the writing of your decision.");
				}
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_SUCCESS);
			} else if (strcmp("no", answer) == 0) {
				libssh2_knownhost_free(knownhosts);
				free(knownhosts_file);
				return (EXIT_FAILURE);
			} else {
				fprintf(stdout, "Please type 'yes' or 'no': ");
				goto askuseragain;
			}
		} else {
			ERROR("Unable to check host interactively.");
			libssh2_knownhost_free(knownhosts);
			free(knownhosts_file);
			return (EXIT_FAILURE);
		}
	} else {
		knownhost_check = libssh2_knownhost_check(knownhosts,
				hostname,
				remotekey,
				len,
				LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | hostkey_typebit,
				&ssh_host);

		DBG("Host check result: %d, \nmatching key: %s\n", knownhost_check, (ssh_host && ssh_host->key) ? ssh_host->key : "<none>");

		switch (knownhost_check) {
		case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
			ERROR("Remote host %s identification changed!", hostname);
			ret = EXIT_FAILURE;
			break;
		case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
			ERROR("Knownhost checking failed.");
			ret = EXIT_FAILURE;
			break;
		case LIBSSH2_KNOWNHOST_CHECK_MATCH:
			ret = EXIT_SUCCESS;
			break;
		case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
			goto keynotfound;
		default:
			ERROR("Unknown result (%d) of the libssh2_knownhost_check().", knownhost_check);
			ret = EXIT_FAILURE;
		}

		libssh2_knownhost_free(knownhosts);
		free(knownhosts_file);
		return (ret);
	}

	/* it never should be here */
	return (EXIT_FAILURE);
}

/* op = 1 (add), 2 (remove) */
static void nc_publickey_path (const char* path, int op)
{
	int i;

	if (path == NULL) {
		return;
	}

	for (i = 0; i < SSH2_KEYS; ++i) {
		if (op == 1 && callbacks.publickey_filename[i] == NULL) {
			callbacks.publickey_filename[i] = strdup(path);
			break;
		}
		if (op == 2 && callbacks.publickey_filename[i] != NULL && strcmp(callbacks.publickey_filename[i], path) == 0) {
			free(callbacks.publickey_filename[i]);
			callbacks.publickey_filename[i] = NULL;
			break;
		}
	}

	if (i == SSH2_KEYS) {
		if (op == 1) {
			ERROR("Too many SSH public keys.");
		}
		if (op == 2) {
			ERROR("The SSH public key to delete was not found.");
		}
	}
}

/* op = 1 (add), 2 (remove) */
static void nc_privatekey_path (const char* path, int op)
{
	FILE* key;
	char line[128];
	int i;

	if (path == NULL) {
		return;
	}

	for (i = 0; i < SSH2_KEYS; ++i) {
		if (op == 1 && callbacks.privatekey_filename[i] == NULL) {
			callbacks.privatekey_filename[i] = strdup(path);
			break;
		}
		if (op == 2 && callbacks.privatekey_filename[i] != NULL && strcmp(callbacks.privatekey_filename[i], path) == 0) {
			free(callbacks.privatekey_filename[i]);
			callbacks.privatekey_filename[i] = NULL;
			callbacks.key_protected[i] = 0;
			break;
		}
	}

	if (i == SSH2_KEYS) {
		if (op == 1) {
			ERROR("Too many SSH private keys.");
		}
		if (op == 2) {
			ERROR("The SSH private key to delete was not found.");
		}
	}

	if (op == 1) {
		if ((key = fopen (path, "r")) != NULL) {
			/* Key type line */
			if (fgets(line, sizeof(line), key) == NULL) {
				ERROR("fgets() on %s failed.", path);
				return; /* error */
			}
			/* encryption information or key */
			if (fgets(line, sizeof(line), key) == NULL) {
				ERROR("fgets() on %s failed.", path);
				return; /* error */
			}
			if (strcasestr (line, "encrypted") != NULL) {
				callbacks.key_protected[i] = 1;
			}
		}
	}
}

API void nc_set_keypair_path(const char* privkey, const char* pubkey)
{
	nc_privatekey_path(privkey, 1);
	nc_publickey_path(pubkey, 1);
}

API void nc_del_keypair_path(const char* privkey, const char* pubkey)
{
	nc_privatekey_path(privkey, 2);
	nc_publickey_path(pubkey, 2);
}

#endif /* not DISABLE_LIBSSH */
