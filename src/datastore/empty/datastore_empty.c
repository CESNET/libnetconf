/**
 * \file datastore_empty.c
 * \author David Kupka <dkupka@cesnet.cz>
 * \brief NETCONF datastore handling function prototypes and structures for state only devices
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

#include "datastore_empty.h"
#include <stdlib.h>

int ncds_empty_init (struct ncds_ds * ds)
{
	/* nothing to do (for now) */
	return EXIT_SUCCESS;
}

void ncds_empty_free (struct ncds_ds * ds)
{
	free (ds);
	/* nothing else to do */
	return;
}

int ncds_empty_lock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	return EXIT_SUCCESS;
}

int ncds_empty_unlock(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	return EXIT_SUCCESS;
}

char* ncds_empty_getconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	return strdup ("");
}

int ncds_empty_copyconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, NC_DATASTORE source, char* config, struct nc_err** error)
{
	return EXIT_SUCCESS;
}

int ncds_empty_deleteconfig(struct ncds_ds* ds, const struct nc_session* session, NC_DATASTORE target, struct nc_err** error)
{
	return EXIT_SUCCESS;
}

int ncds_empty_editconfig(struct ncds_ds *ds, const struct nc_session * session, NC_DATASTORE target, const char * config, NC_EDIT_DEFOP_TYPE defop, NC_EDIT_ERROPT_TYPE errop, struct nc_err **error)
{
	return EXIT_SUCCESS;
}
