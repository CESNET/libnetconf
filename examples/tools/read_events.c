#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libnetconf.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

void clb_print(NC_VERB_LEVEL level, const char* msg)
{
	switch (level) {
	case NC_VERB_ERROR:
		fprintf(stderr, "libnetconf ERROR: %s\n", msg);
		break;
	case NC_VERB_WARNING:
		fprintf(stderr, "libnetconf WARNING: %s\n", msg);
		break;
	case NC_VERB_VERBOSE:
		fprintf(stderr, "libnetconf VERBOSE: %s\n", msg);
		break;
	case NC_VERB_DEBUG:
		fprintf(stderr, "libnetconf DEBUG: %s\n", msg);
		break;
	}
}

int main(int argc, char* argv[])
{
	char* event;
	xmlDocPtr eventDoc;

	if (argc != 2) {
		fprintf(stdout, "Usage: %s <name of stream>\n", argv[0]);
		return (EXIT_FAILURE);
	}

	nc_verbosity(NC_VERB_VERBOSE);
	nc_callback_print(clb_print);


	nc_ntf_init();
	nc_ntf_stream_iter_start(argv[1]);
	while((event = nc_ntf_stream_iter_next(argv[1])) != NULL) {
		if ((eventDoc = xmlReadMemory(event, strlen(event), NULL, NULL, 0)) != NULL) {
			fprintf(stdout, "Event:\n");
			xmlDocFormatDump(stdout, eventDoc, 1);
			xmlFreeDoc(eventDoc);
		} else {
			fprintf(stdout, "Invalid event format.\n");
		}
		free(event);
	}
	nc_ntf_close();

	return (EXIT_SUCCESS);
}
