#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>

#include <libnetconf.h>

#include <libxml/tree.h>

#include "configuration.h"
#include "commands.h"

/* NetConf Client home */
#define NCC_DIR ".netconf_client"

static char *config_path;

/**
 * @brief Checks if netconf client configuration directory exist. If not and create is not 0, tries to create it.
 *
 * Updates the config_path global variable.
 *
 * @param[in] create Try to create the directory if not exist?
 *
 * @return EXIT_SUCCESS if directory exist (was created) EXIT_FAILURE if not exist (can't be created)
 */
int check_client_dir (int create)
{
	struct stat st;
	char * user_home, *tmp_path;
	struct passwd *pw;

	if ((user_home = strdup(getenv ("HOME"))) == NULL) {
		pw = getpwuid (getuid ());
		user_home = strdup (pw->pw_dir);
	}
	asprintf (&config_path, "%s/%s", user_home, NCC_DIR);
	free (user_home);

	if (stat (config_path, &st) == -1 && errno == ENOENT) {
		ERROR ("check_client_dir", "NETCONF client folder does not exist. Creating.");
		if (mkdir (config_path, 0777)) {
			ERROR ("check_client_dir", "Failed to create NETCONF client folder. Will be unable to load command history and configuration.");
			free (config_path);
			config_path = NULL;
			return EXIT_FAILURE;
		}
	}

	asprintf (&tmp_path, "%s/history", config_path);
	if (stat (tmp_path, &st) == -1 && errno == ENOENT) {
		ERROR ("check_client_dir", "History file does not exist. Creating.");
		if (creat (tmp_path, 0666)) {
			ERROR ("check_client_dir", "Failed to create history file.");
			free (tmp_path);
			return EXIT_FAILURE;
		}
	}
	free (tmp_path);

	return EXIT_SUCCESS;
}

/**
 * \brief Load NETCONF client stored configuration and history from previous instances
 */
int load_config (struct nc_cpblts ** cpblts)
{
	char * tmp_path, *tmp_cap;
	xmlDocPtr config_doc;
	xmlNodePtr config_cap;

	*cpblts = NULL;

	if (check_client_dir(1)) {
		return EXIT_FAILURE;
	}

	/* read history from file */
	asprintf (&tmp_path, "%s/history", config_path);
	if (read_history (tmp_path)) {
		ERROR ("load_config", "Failed to load history from previous runs.");
	}
	free (tmp_path);

	*cpblts = nc_cpblts_new(NULL);

	asprintf (&tmp_path, "%s/config.xml", config_path);
	if ((config_doc = xmlReadFile (tmp_path, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN)) == NULL) {
		ERROR ("load_config", "Failed to load configuration of NETCONF client.");
	} else {
		/* doc -> <netconf-client/>*/
		if (config_doc->children != NULL && xmlStrEqual (config_doc->children->name, BAD_CAST "netconf-client")) {
			/* doc -> <netconf-client> -> <capabilities>*/
			if (config_doc->children->children != NULL && xmlStrEqual (config_doc->children->children->name, BAD_CAST "capabilities")) {
				config_cap = config_doc->children->children->children;
				while (config_cap) {
					tmp_cap = (char *)xmlNodeGetContent(config_cap);
					nc_cpblts_add(*cpblts, tmp_cap);
					free (tmp_cap);
					config_cap = config_cap->next;
				}
			}
		}
		xmlFreeDoc (config_doc);
	}
	free (tmp_path);

	if (nc_cpblts_count(*cpblts) == 0) {
		nc_cpblts_free(*cpblts);
		*cpblts = NULL;
	}

	return EXIT_SUCCESS;
}

/**
 * \brief Store configuration and history
 */
int store_config (struct nc_cpblts * cpblts)
{
	char * tmp_path;
	const char * cap;
	FILE * config_file;
	xmlDocPtr config_doc;
	xmlNodePtr config_root, config_caps;

	asprintf (&tmp_path, "%s/history", config_path);
	write_history (tmp_path);
	free (tmp_path);

	asprintf (&tmp_path, "%s/config.xml", config_path);
	if ((config_file = fopen(tmp_path, "w")) == NULL) {
		ERROR ("store_config", "Can not write configuration to file %s", config_path);
		free (tmp_path);
		return EXIT_FAILURE;
	}
	free (tmp_path);

	config_doc = xmlNewDoc(BAD_CAST "1.0");
	config_root = xmlNewDocNode(config_doc, NULL, BAD_CAST "netconf-client", NULL);
	xmlDocSetRootElement(config_doc, config_root);
	config_caps = xmlNewChild(config_root, NULL, BAD_CAST "capabilities", NULL);
	nc_cpblts_iter_start(cpblts);
	while ((cap = nc_cpblts_iter_next(cpblts)) != NULL) {
		xmlNewChild(config_caps, NULL, BAD_CAST "capability", BAD_CAST cap);
	}

	xmlDocDump(config_file, config_doc);

	xmlFreeDoc (config_doc);
	free (config_path);
	config_path = NULL;
	return EXIT_SUCCESS;
}
