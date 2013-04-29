#include "url.h"

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

int nc_get_rpc( const char * url, char * buffer )
{
	CURL * curl;
	
	curl_global_init( CURL_GLOBAL_SSL | CURL_GLOBAL_ACK_EINTR );
	curl = curl_easy_init();
	curl_easy_setopt( curl, CURLOPT_URL, url );
	
	
}
