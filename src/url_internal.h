/**
 * \file url_internal.h
 * \author Ondrej Vlk <ondrasek.vlk@gmail.com>
 * \brief libnetconf's internal API to use the URL capability.
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

#ifndef DISABLE_URL
#ifndef URL_INTERNAL_H_
#define URL_INTERNAL_H_

#include "url.h"

/**
 * @brief Get rpc from remote source
 * @param url
 * @return fd on tmp file with remote rpc or -1 on error
 */
int nc_url_open(const char * url);

/**
 * @brief Uploads empty (<config></config>) file to remote target
 * @param url
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int nc_url_delete_config(const char *url);

/**
 * @brief Uploads data to remote target
 * @param data configuration data closed in <config> tag
 * @param url
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int nc_url_upload(char *data, const char *url);

/**
 * @brief Generate URL capability string from enabled protocols
 * @return capability string
 */
char* nc_url_gencap();

/**
 * Check if protocol is enabled in session
 * @param protocol protocol id
 * @param session
 * @return 0 if not supported, nonzero otherwise
 */
int nc_url_is_enabled(NC_URL_PROTOCOLS protocol);

/**
 * Get protocol id from url string
 * @param url
 * @return protocol id
 */
NC_URL_PROTOCOLS nc_url_get_protocol(const char *url);

// tmp file with config data from remote source
int url_tmpfile;

// Struct for uploading data with curl
struct nc_url_mem 
{ 
  char *memory; 
  size_t size; 
};

#endif /* URL_INTERNAL_H_ */
#endif /* DISABLE_URL */
