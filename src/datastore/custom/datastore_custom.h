/**
 * \file datastore_file.h
 * \author Robin Obůrka <robin.oburka@nic.cz>
 * \brief NETCONF datastore handling function prototypes and structures for file datastore implementation.
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
	 * \return 0 for success, 1 for failure.
	 */
	int (*init)(void *data);
	/**
	 * \brief Called after the last use of the data store.
	 *
	 * This is called after the library stops using the data store.
	 * Use this place to free whatever resources (including data, if
	 * it was allocated.
	 */
	void (*free)(void *data);
	/**
	 * \brief Was the content of data store changed?
	 */
	int (*was_changed)(void *data);
	/**
	 * \brief Drop all changes.
	 *
	 * \return 0 for success, 1 for error.
	 */
	int (*rollback)(void *data);
	/**
	 * \brief Lock the data store from other processes.
	 *
	 * \param data The user data.
	 * \param target Which data store should be locked.
	 * \param error Set this in case of EXIT_FAILURE, to indicate what went wrong.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*lock)(void *data, NC_DATASTORE target, struct nc_err** error);
	/**
	 * \brief The counter-part of lock.
	 */
	int (*unlock)(void *data, NC_DATASTORE target, struct nc_err** error);
	/**
	 * @brief Get content of the config.
	 *
	 * The ownership of the returned string is passed onto the
	 * caller. So, allocate it and forget.
	 */
	char *(*getconfig)(void *data, NC_DATASTORE target, struct nc_err **error);
	/**
	 * \brief Copy config from one data store to another.
	 *
	 * \param data The user data.
	 * \param target Where to copy.
	 * \param source From where to copy.
	 * \param config ?
	 * \param error Fill in in case of error.
	 * \return EXIT_SUCCESS or EXIT_FAILURE.
	 */
	int (*copyconfig)(void *data, NC_DATASTORE target, NC_DATASTORE source, char* config, struct nc_err** error);
	/**
	 * \brief Make the given data source empty.
	 */
	int (*deleteconfig)(void *data, NC_DATASTORE target, struct nc_err** error);
	/**
	 * \brief Perform the editconfig operation.
	 */
	int (*editconfig)(void *data, NC_DATASTORE target, const char *config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error);
};

/**
 * \brief Set custom data stored in custom datastore.
 *
 * Call after allocating the custom data store, but before initializing it.
 *
 * \param custom_data Any user provided data, passed to all the callbacks, but left intact by the
 *     library.
 * \param callbacks Definition of what callbacks to use to perform various operations.
 */
void ncds_custom_set_data(struct ncds_ds* datastore, void *custom_data, const struct ncds_custom_funcs *callbacks);

#endif /* DATASTORE_CUSTOM_H */
