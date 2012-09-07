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

void load_config (struct nc_cpblts **cpblts)
{
	struct passwd * pw;
	char * user_home, *netconf_dir, * history_file, *config_file;
	char * tmp_cap, * prio, * key_priv, * key_pub;
	int ret, history_fd, config_fd;
	xmlDocPtr config_doc;
	xmlNodePtr config_cap, tmp_node, tmp_auth, tmp_pref, tmp_key;

	(*cpblts) = nc_session_get_cpblts_default();

	if ((user_home = strdup(getenv ("HOME"))) == NULL) {
		pw = getpwuid (getuid ());
		user_home = strdup (pw->pw_dir);
	}

	asprintf (&netconf_dir, "%s/%s", user_home, NCC_DIR);
	ret = access (netconf_dir, R_OK|X_OK);
	if (ret == -1) {
		if (errno == ENOENT) {
			ERROR ("load_config", "Configuration directory (%s) does not exist, create it.", netconf_dir);
			if (mkdir (netconf_dir, 0777)) {
				ERROR ("load_config", "Directory can not be created");
				free (netconf_dir);
				return;
			}
		} else {
			ERROR ("load_config", "Directory (%s) exist but cannot be accessed", netconf_dir);
			free (netconf_dir);
			return;
		}
	}

	asprintf (&history_file, "%s/history", netconf_dir);
	ret = access (history_file, R_OK);
	if (ret == -1) {
		if(errno == ENOENT) {
			ERROR ("load_config", "History file (%s) does not exit, create it", history_file);
			if ((history_fd = creat (history_file, 0666)) == -1) {
				ERROR ("load_config", "History file can not be created");
			} else {
				close(history_fd);
			}
		}
	} else {
		/* file exist and is accessible */
		if (read_history (history_file)) {
			ERROR ("load_config", "Failed to load history from previous runs.");
		}
	}

	asprintf (&config_file, "%s/config.xml", netconf_dir);
	ret = access (config_file, R_OK);
	if (ret == -1) {
		if (errno == ENOENT) {
			ERROR ("load_config", "Configuration file (%s) does not exit, create it", config_file);
			if ((config_fd = creat (config_file, 0666)) == -1) {
				ERROR ("load_config", "Configuration file can not be created");
			} else {
				close (config_fd);
			}
		} else {
			ERROR ("load_config", "Configuration file can not accessed: %s", strerror(errno));
		}
	} else {
		/* file exist and is accessible */
		if ((config_doc = xmlReadFile (config_file, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN)) == NULL) {
			ERROR ("load_config", "Failed to load configuration of NETCONF client.");
		} else {
			/* doc -> <netconf-client/>*/
			if (config_doc->children != NULL && xmlStrEqual(config_doc->children->name, BAD_CAST "netconf-client")) {
				tmp_node = config_doc->children->children;
				while (tmp_node) {
					/* doc -> <netconf-client> -> <capabilities> */
					if (xmlStrEqual (tmp_node->name, BAD_CAST "capabilities")) {
						nc_cpblts_free(*cpblts);
						(*cpblts) = nc_cpblts_new(NULL);
						config_cap = tmp_node->children;
						while (config_cap) {
							tmp_cap = (char *)xmlNodeGetContent(config_cap);
							nc_cpblts_add(*cpblts, tmp_cap);
							free (tmp_cap);
							config_cap = config_cap->next;
						}
					/* doc -> <netconf-client> -> <authentication> */
					} else if (xmlStrEqual (tmp_node->name, BAD_CAST "authentication")) {
						tmp_auth = tmp_node->children;
						while (tmp_auth) {
							if (xmlStrEqual(tmp_auth->name, BAD_CAST "pref")) {
								tmp_pref = tmp_auth->children;
								while (tmp_pref) {
									prio = (char*)xmlNodeGetContent(tmp_pref);
									if (xmlStrEqual(tmp_pref->name, BAD_CAST "publickey")) {
										nc_ssh_pref(NC_SSH_AUTH_PUBLIC_KEYS, atoi(prio));
									} else if (xmlStrEqual(tmp_pref->name, BAD_CAST "interactive")) {
										nc_ssh_pref(NC_SSH_AUTH_INTERACTIVE, atoi(prio));
									} else if (xmlStrEqual(tmp_pref->name, BAD_CAST "password")) {
										nc_ssh_pref(NC_SSH_AUTH_PASSWORD, atoi(prio));
									}
									free(prio);
									tmp_pref = tmp_pref->next;
								}
							} else if (xmlStrEqual(tmp_auth->name, BAD_CAST "keys")) {
								tmp_key = tmp_auth->children;
								while (tmp_key) {
									if (xmlStrEqual(tmp_key->name, BAD_CAST "key-path")) {
										key_priv = (char*)xmlNodeGetContent(tmp_key);
										asprintf(&key_pub, "%s.pub", key_priv);
										nc_set_keypair_path (key_priv, key_pub);
										free (key_priv);
										free (key_pub);
									}
									tmp_key = tmp_key->next;
								}
							}
							tmp_auth = tmp_auth->next;
						}
					}
					tmp_node = tmp_node->next;
				}
			}
			xmlFreeDoc (config_doc);
		}
	}

	free (config_file);
	free (history_file);
	free (user_home);
	free (netconf_dir);
}

/**
 * \brief Store configuration and history
 */
void store_config (struct nc_cpblts * cpblts)
{
	struct passwd * pw;
	char * user_home, *netconf_dir, * history_file, *config_file;
	const char * cap;
	int history_fd, ret;
	xmlDocPtr config_doc;
	xmlNodePtr config_caps;
	FILE * config_f;

	if ((user_home = strdup(getenv ("HOME"))) == NULL) {
		pw = getpwuid (getuid ());
		user_home = strdup (pw->pw_dir);
	}

	asprintf (&netconf_dir, "%s/%s", user_home, NCC_DIR);
	ret = access (netconf_dir, R_OK|W_OK|X_OK);
	if (ret == -1) {
		if (errno == ENOENT) {
			/* directory does not exist, create it */
			if (mkdir (netconf_dir, 0777)) {
				/* directory can not be created */
				free (netconf_dir);
				return;
			}
		} else {
			/* directory exist but cannot be accessed */
			free (netconf_dir);
			return;
		}
	}

	asprintf (&history_file, "%s/history", netconf_dir);
	ret = access (history_file, R_OK|W_OK);
	if (ret == -1) {
		if(errno == ENOENT) {
			/* file does not exit, create it */
			if ((history_fd = creat (history_file, 0666)) == -1) {
				/* history file can not be created */
			} else {
				close(history_fd);
			}
		}
	}

	if (write_history (history_file)) {
		ERROR ("save_config", "Failed to save history.");
	}

	asprintf (&config_file, "%s/config.xml", netconf_dir);
	if (access (config_file, R_OK|W_OK) == -1 ||
			(config_doc = xmlReadFile(config_file, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN|XML_PARSE_NOERROR)) == NULL) {
		config_doc = xmlNewDoc (BAD_CAST "1.0");
		config_doc->children = xmlNewDocNode(config_doc, NULL, BAD_CAST "netconf-client", NULL);
	}
	if (config_doc != NULL) {
		if (config_doc->children != NULL && xmlStrEqual(config_doc->children->name, BAD_CAST "netconf-client")) {
			config_caps = config_doc->children->children;
			while (config_caps != NULL && !xmlStrEqual(config_caps->name, BAD_CAST "capabilities")) {
				config_caps = config_caps->next;
			}
			if (config_caps != NULL) {
				xmlUnlinkNode (config_caps);
				xmlFreeNode (config_caps);
			}
			config_caps = xmlNewChild(config_doc->children, NULL, BAD_CAST "capabilities", NULL);
			nc_cpblts_iter_start(cpblts);
			while ((cap = nc_cpblts_iter_next(cpblts)) != NULL) {
				xmlNewChild(config_caps, NULL, BAD_CAST "capability", BAD_CAST cap);
			}
		}
		if ((config_f = fopen(config_file, "w")) == NULL || xmlDocFormatDump(config_f, config_doc, 1) < 0) {
			ERROR ("store_config", "Can not write configuration to file %s", config_file);
		} else {
			fclose(config_f);
		}
		xmlFreeDoc (config_doc);
	} else {
		ERROR ("store_config", "Can not write configuration to file %s", config_file);
	}

	free (config_file);
}
