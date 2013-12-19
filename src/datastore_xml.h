/**
 * \file datastore_xml.h
 * \author David Kupka <dkupka@cesnet.cz>
 * \brief NETCONF datastore handling function prototypes and structures - XML variants.
 *
 * Copyright (C) 2013 CESNET, z.s.p.o.
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

#ifndef DATASTORE_XML_H_
#define DATASTORE_XML_H_

#include <libxml/tree.h>

#include "datastore.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup store
 * @brief Set validators (or disable validation) on the specified datastore.
 *
 * To make this function available, you have to include libnetconf_xml.h.
 *
 * @param[in] ds Datastore structure to be configured.
 * @param[in] enable 1 to enable validation on the datastore according to the
 * following parameters, 0 to disable validation (following parameters will be
 * ignored as well as automatically or previously set validators).
 * @param[in] relaxng Path to the Relax NG schema for validation of the
 * datastore content syntax. To generate it, use the lnc-tool(1) script. NULL
 * if syntactic validation not required.
 * @param[in] schematron Path to the Schematron XSLT stylesheet for validation of
 * the datastore content semantics. To generate it, use the lnc-tool(1) script.
 * NULL if semantic validation not required.
 * @param[in] valid_func Pointer to a callback function that is used for
 * additional validation of the configuration data in the datastore. It can
 * perform any specific check for the datastore (e.g. check for presence of
 * referred files). If no such check is needed, parameter can be set to NULL.
 * <BR>
 * Validation callback function receives configuration data as a libxml2's
 * xmlDocPtr. As a result it returns EXIT_SUCCESS if validation checks passed
 * and EXIT_FAILURE when an error occurred. An error description may be
 * returned via the \p err parameter.
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int ncds_set_validation2(struct ncds_ds* ds, int enable, const char* relaxng,
    const char* schematron,
    int (*valid_func)(const xmlDocPtr config, struct nc_err **err));

#ifdef __cplusplus
}
#endif

#endif /* DATASTORE_XML_H_ */
