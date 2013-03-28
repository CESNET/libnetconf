/*
* This is automaticaly generated callbacks file
* It contains 2 parts: Configuration callbacks and state data callbacks.
* Do NOT alter function signatures or any structure untill you exactly know what you are doing.
*/

#include <libxml/tree.h>
#include <libnetconf.h>
#include <libnetconf/transapi.h>
#include <unistd.h>
#include <pthread.h>

char * status = "off";
int toasting = 0;

/**
 * @brief Retrieve state data from device and return them as serialized XML *
 * @param model	Device data model. Serialized YIN.
 * @param running	Running datastore content. Serialized XML.
 * @param[out] err	Double poiter to error structure. Fill error when some occurs.
 *
 * @return State data as serialized XML or NULL in case of error.
 */
char * get_state_data (char * model, char * running, struct nc_err **err)
{
	xmlDocPtr state;
	xmlNodePtr root;
	xmlNsPtr ns;
	xmlBufferPtr buf = xmlBufferCreate();
	char * ret;

	state = xmlNewDoc (BAD_CAST "1.0");
	root = xmlNewDocNode (state, NULL, BAD_CAST "toaster", NULL);
	xmlDocSetRootElement (state, root);
	ns = xmlNewNs (root, BAD_CAST "http://netconfcentral.org/ns/toaster", NULL);
	xmlNewChild (root, ns, BAD_CAST "toasterManufacturer", BAD_CAST "CESNET, z.s.p.o.");
	xmlNewChild (root, ns, BAD_CAST "toasterModelNumber", BAD_CAST "lnetconf-0.x");
	xmlNewChild (root, ns, BAD_CAST "toasterStatus", BAD_CAST (strcmp(status, "off") == 0 ? "up" : "down" ));

	xmlNodeDump (buf, state, root, 0, 0);
	ret = strdup(xmlBufferContent(buf));

	xmlBufferFree(buf);
	xmlFreeDoc(state);
	
	return ret;
}

/*
* CONFIGURATION callbacks
* Here follows set of callback functions run every time some change in associated part of running datastore occurs.
* You can safely modify the bodies of all function as well as add new functions for better lucidity of code.
*/

/**
 * @brief This callback will be run when node in path / changes
 *
 * @param op	Observed change in path. XMLDIFF_OP type.
 * @param node	Modified node. if op == XMLDIFF_REM its copy of node removed.
 * @param data	Double pointer to void. Its passed to every callback. You can share data using it.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
/* !DO NOT ALTER FUNCTION SIGNATURE! */
int callback_ (XMLDIFF_OP op, xmlNodePtr node, void ** data)
{
	status = (op == XMLDIFF_REM ? "off" : "on");
	switch (op) {
	case XMLDIFF_REM:
		status = "off";
		if (toasting != 0) {
			fprintf (stderr, "Interrupting ongoing toasting!\n");
			toasting = 0;
		}
		break;
	case XMLDIFF_ADD:
		status = "on";
		break;
	default:
		/* 
		 * should not happen
		 * Container can be removed or added but holds no configuration informations.
		 */
		return EXIT_FAILURE;
		break;
	}

	fprintf (stderr, "Turning toaster %s", status);
	return EXIT_SUCCESS;
}

/*
* Structure transapi_config_callbacks provide mapping between callback and path in configuration datastore.
* It is used by libnetconf library to decide which callbacks will be run.
* DO NOT alter this structure
*/
struct transapi_config_callbacks clbks =  {
	.callbacks_count = 1,
	.data = NULL,
	.callbacks = {
		{.path = "/", .func = callback_}
	}
};

