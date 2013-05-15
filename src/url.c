/**
 * \file url.c
 * \author Ondrej Vlk <ondrasek.vlk@gmail.com>
 * \brief libnetconf's implementation of the URL capability.
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

#include <curl/curl.h>
#include <stdlib.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include "url_internal.h"
#include "netconf_internal.h"


/* define init flags */
#ifdef CURL_GLOBAL_ACK_EINTR
#define INIT_FLAGS CURL_GLOBAL_SSL|CURL_GLOBAL_ACK_EINTR
#else
#define INIT_FLAGS CURL_GLOBAL_SSL
#endif

int nc_url_allowed_protocols = 0;

void nc_url_set_protocols( NC_URL_PROTOCOLS protocols )
{
	nc_url_allowed_protocols = protocols;
}

void nc_url_allow( NC_URL_PROTOCOLS protocol )
{
	nc_url_allowed_protocols = nc_url_allowed_protocols | protocol;
}

void nc_url_disable( NC_URL_PROTOCOLS protocol )
{
	nc_url_allowed_protocols = ~(~nc_url_allowed_protocols ^ protocol ) ;
}

int nc_url_is_enabled( NC_URL_PROTOCOLS protocol )
{
	return nc_url_allowed_protocols & protocol;
}

int nc_url_upload( const char * data, xmlChar * url ) {
	CURL * curl;
	CURLcode res;
	char curl_buffer[ CURL_ERROR_SIZE ];
	FILE * tmp_file;
	xmlDocPtr doc;
	xmlNodePtr root_element;
	
	if( strcmp( data, "" ) == 0 ) {
		ERROR( "%s: source file is empty", __func__)
		return EXIT_FAILURE;
	}
	
	doc = xmlParseMemory( data, strlen( data ) );
	root_element = xmlDocGetRootElement( doc );
	if( strcmp( ( char * )root_element->name, "config" ) != 0 ) {
		ERROR( "%s: source file does not contain config element", __func__ );
		return EXIT_FAILURE;
	}
	xmlFreeNode( root_element );
	xmlFreeDoc( doc );
	
	tmp_file = tmpfile();
	
	fprintf(tmp_file, "%s", data );
	printf( "%s", data );
	curl_global_init(INIT_FLAGS);
	curl = curl_easy_init();
	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_UPLOAD, 1L );
	curl_easy_setopt( curl, CURLOPT_READDATA, tmp_file );
	curl_easy_setopt( curl, CURLOPT_ERRORBUFFER, curl_buffer );
	res = curl_easy_perform( curl );
	
	if( res != CURLE_OK )
	{
		close( url_tmpfile );
		ERROR( "%s: curl error: %s", __func__, curl_buffer );
		return -1;
	}
	fclose( tmp_file );
	printf( "!%s!", curl_buffer );
	return EXIT_SUCCESS;
}

size_t nc_url_writedata( char *ptr, size_t size, size_t nmemb, void *userdata) {
	return write( url_tmpfile, ptr, size*nmemb );
}

int nc_url_delete_config( xmlChar * url )
{
	CURL * curl;
	CURLcode res;
	char curl_buffer[ CURL_ERROR_SIZE ];
	FILE * empty_file;
	
	
	empty_file = tmpfile();
	
	curl_global_init(INIT_FLAGS);
	curl = curl_easy_init();
	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_UPLOAD, 1L );
	curl_easy_setopt( curl, CURLOPT_READDATA, empty_file );
	curl_easy_setopt( curl, CURLOPT_ERRORBUFFER, curl_buffer );
	res = curl_easy_perform( curl );
	
	if( res != CURLE_OK )
	{
		close( url_tmpfile );
		ERROR( "%s: curl error: %s", __func__, curl_buffer );
		return -1;
	}
	//fclose( empty_file );
	
	return EXIT_SUCCESS;
}

int nc_url_get_rpc( xmlChar * url )
{
	CURL * curl;
	CURLcode res;
	char curl_buffer[ CURL_ERROR_SIZE ];
	char url_tmp_name[18] = "url_tmpfileXXXXXX";
	if( ( url_tmpfile = mkstemp( url_tmp_name ) ) < 0 )
	{
		ERROR( "%s: cannot open temporary file", __func__ );
	}
	unlink( url_tmp_name );
	VERB( "curl: getting file from url %s", url );
	curl_global_init(INIT_FLAGS);
	curl = curl_easy_init();
	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, nc_url_writedata );
	curl_easy_setopt( curl, CURLOPT_ERRORBUFFER, curl_buffer );
	res = curl_easy_perform( curl );
	
	if( res != CURLE_OK )
	{
		close( url_tmpfile );
		ERROR( "%s: curl error: %s", __func__, curl_buffer );
		return -1;
	}
	lseek( url_tmpfile, 0, SEEK_SET );
	return url_tmpfile;
}
