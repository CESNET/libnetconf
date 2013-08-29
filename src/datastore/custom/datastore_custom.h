/**
 * \file datastore_custom.h
 * \author Robin Obůrka <robin.oburka@nic.cz>
 * \brief NETCONF datastore handling function prototypes and structures for
 * custom datastore implementation.
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

#ifndef DATASTORE_CUSTOM_H
#define DATASTORE_CUSTOM_H

struct ncds_ds;
struct nc_err;

/**
 * \page datastores Datastores Usage
 *
 * NETCONF defines usage of three datastores: running (mandatory),
 * startup (optional via :startup capability) and candidate (optional via
 * :candidate capability). libnetconf provides all these datastores.
 *
 * In addition to the described NETCONF point of view, libnetconf divides all
 * datastores (running, startup and candidate) into datastore parts connected
 * with a specific (basic) configuration data model. Each datastore part is
 * created by ncds_new() or ncds_new_transapi() function. According to the
 * specified datastore type, server should set up additional datastore settings
 * (see sections below). Finnaly, to activate datastore and to get its unique
 * identifier, ncds_init() function must be called.
 *
 * If you want to use some data model that extends (by import or augment
 * statement) any of the used data models, functions ncds_add_model() or
 * ncds_add_models_path() can be used to specify location of the extension data
 * model(s).
 *
 * By default, all features defined in configuration data models are disabled.
 * To enable specific features or all features at once, you can use
 * ncds_feature_enable() and ncds_features_enableall() functions.
 *
 * To finish changes made to the datastores (adding augment data models,
 * enabling and disabling features, etc.), server must call ncds_consolidate()
 * function.
 *
 * As a next step, device controlled by the server should be initialized. This
 * should includes copying startup configuration data into the running
 * datastore (and applying them to the current device settings).
 *
 * ## Datastore Settings ##
 *
 * - Empty Datastore (*NCDS_TYPE_EMPTY*)
 *
 *   There is no additional settings for this datastore type.
 *
 * - \ref fileds (*NCDS_TYPE_FILE*)
 *
 *   ncds_file_set_path() to set file to store datastore content.
 *
 * - \ref customds (*NCDS_TYPE_CUSTOM*)
 *
 *   This type of datastore implementation is provided by the server, not by
 *   libnetconf.
 *
 *   ncds_custom_set_data() sets server specific functions implementing the
 *   datastore. In this case, server is required to implement functions
 *   from #ncds_custom_funcs structure.
 *
 */

/**
 * \defgroup customds Custom Datastore
 * \ingroup store
 * \brief libnetconf's API to use a server-specific datastore implementation.
 *
 * \addtogroup customds
 * @{
 */

/**
 * \brief Public callbacks for the data store.
 *
 * These are the callbacks that need to be provided by the server
 * to be used from the custom data store.
 */
struct ncds_custom_funcs {
	/**
	 * \brief Called before the data store is used.
	 *
	 * This callback is called before the data store is used (but
	 * after the data has been set).
	 *
	 * \param[in] data The user data.
	 * \return 0 for success, 1 for failure.
	 */
	int (*init)(void *data);
	/**
	 * \brief Called after the last use of the data store.
	 *
	 * This is called after the library stops using the data store.
	 * Use this place to free whatever resources (including data, if
	 * it was allocated.
	 *
	 * \param[in] data The user data.
	 */
	void (*free)(void *data);
	/**
	 * \brief Was the content of data store changed?
	 *
	 * \param[in] data The user data.
	 * \return 0 if content not changed, non-zero else
	 */
	int (*was_changed)(void *data);
	/**
	 * \brief Revert the last change.
	 *
	 * \param[in] data The user data.
	 * \return 0 for success, 1 for error.
	 */
	int (*rollback)(void *data);
	/**
	 * \brief Lock the data store from other processes.
	 *
	 * \param[in] data The user data.
	 * \param[in] target Which data store should be locked.
	 * \param[in] session_id ID of the session requesting the lock.
	 * \param[out] error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*lock)(void *data, NC_DATASTORE target, const char* session_id, struct nc_err** error);
	/**
	 * \brief The counter-part of lock.
	 *
	 * Function must check that the datastore was locked by the same session
	 * (according to the provided session_id) that is now requesting its unlock.
	 *
	 * \param[in] data The user data.
	 * \param[in] target Which data store should be unlocked.
	 * \param[in] session_id ID of the session requesting the unlock.
	 * \param[out] error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*unlock)(void *data, NC_DATASTORE target, const char* session_id, struct nc_err** error);
	/**
	 * \brief Is datastore currently locked?
	 *
	 * If function is not implemented, libnetconf will use internal
	 * information about the lock. Note, that this information is process
	 * specific. If your server runs in multiple processes, libnetconf's
	 * information might not be valid. In such a case you should properly
	 * implement this function to share lock information.
	 *
	 * Note, that session_id and datetime can be NULL when caller does not
	 * need this information.
	 *
	 * To announce, that this function is not implemented, set it to NULL in
	 * callbacks parameter passed to the ncds_custom_set_data() function.
	 *
	 * \param[in] data The user data
	 * \param[in] target Which datastore lock information is required.
	 * \param[out] session_id Which session has locked the datastore.
	 * \param[out] datatime When the datastore was locked (RFC 3339 format)
	 * \return
	 * - 0 datastore is not locked
	 * - 1 datastore is locked
	 * - negative value - error
	 */
	int (*is_locked)(void *data, NC_DATASTORE target, const char** session_id, const char** datetime);
	/**
	 * @brief Get content of the config.
	 *
	 * The ownership of the returned string is passed onto the
	 * caller. So, allocate it and forget.
	 *
	 * \param[in] data The user data.
	 * \param[in] target Where to read data from.
	 * \param[out] error Set this in case of error, to indicate what went wrong.
	 * \return Serialized content of the datastore, NULL on error
	 */
	char *(*getconfig)(void *data, NC_DATASTORE target, struct nc_err **error);
	/**
	 * \brief Copy config from one data store to another.
	 *
	 * \param[in] data The user data.
	 * \param[in] target Where to copy.
	 * \param[in] source From where to copy.
	 * \param[in] config Custom data if source parameter is NC_DATASTORE_CONFIG
	 * \param[out] error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*copyconfig)(void *data, NC_DATASTORE target, NC_DATASTORE source, char* config, struct nc_err** error);
	/**
	 * \brief Make the given data source empty.
	 *
	 * \param[in] data The user data.
	 * \param[in] target Which part (running, startup, candidate) is supposed to be cleaned out.
	 * \param[out] error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*deleteconfig)(void *data, NC_DATASTORE target, struct nc_err** error);
	/**
	 * \brief Perform the editconfig operation.
	 *
	 * \param[in] data The user data.
	 * \param[in] rpc RPC message with the request. RPC message is used only
	 * for access control. If rpc is NULL access control is skipped.
	 * \param[in] target What datastore part is going to be modified.
	 * \param[in] config Edit configuration data.
	 * \param[in] defop Default edit operation.
	 * \param[in] errop Error-option.
	 * \param[out] error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*editconfig)(void *data, const nc_rpc* rpc, NC_DATASTORE target, const char *config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);
};

/**
 * \brief Set custom data stored in custom datastore.
 *
 * Call after allocating the custom data store, but before initializing it.
 * \param datastore Custom datastore to store the data
 * \param custom_data Any user provided data, passed to all the callbacks, but
 * left intact by the library.
 * \param callbacks Definition of what callbacks to use to perform various operations.
 */
void ncds_custom_set_data(struct ncds_ds* datastore, void *custom_data, const struct ncds_custom_funcs *callbacks);

/** @}*/

#endif /* DATASTORE_CUSTOM_H */
