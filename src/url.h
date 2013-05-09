typedef unsigned char xmlChar;

int nc_url_get_rpc( xmlChar * url );
int nc_url_delete_config( xmlChar * url );
int nc_url_upload( const char * data, xmlChar * url );

typedef enum
{
	NC_URL_SCP   =   1,
	NC_URL_HTTP  =   2,
	NC_URL_HTTPS =   4,
	NC_URL_FTP   =   8,
	NC_URL_SFTP  =  16,
	NC_URL_FTPS  =  32,
	NC_URL_FILE  =  64,
	NC_URL_ALL   = 127
} NC_URL_PROTOCOLS;

int url_tmpfile;