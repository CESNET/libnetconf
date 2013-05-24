/*
* This is automaticaly generated callbacks file
* It contains 3 parts: Configuration callbacks, RPC callbacks and state data callbacks.
* Do NOT alter function signatures or any structure untill you exactly know what you are doing.
*/

#include <stdlib.h>
#include <libxml/tree.h>
#include <libnetconf_xml.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

/* Determines whether XML arguments are passed as (xmlDocPtr) or (char *). */
int with_libxml2 = 1;

char * status = "off";
int toasting = 0;
pthread_mutex_t cancel_mutex;
volatile int cancel;

/**
 * @brief Initialize plugin after loaded and before any other functions are called.
 *
 * @return New content of running datastore reflecting current device state.
 */
int init(void)
{



	return EXIT_SUCCESS;
}

/**
 * @brief Free all resources allocated on plugin runtime and prepare plugin for removal.
 */
void close(void)
{
	status = "off";
	return;
}
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
	int ret = EXIT_FAILURE;
	time_t t;

	if (op <= 0 || op > (XMLDIFF_MOD | XMLDIFF_CHAIN | XMLDIFF_ADD | XMLDIFF_REM)) {
		fprintf (stderr, "internal error: Invalid operation (out of range)!");
		ret = -1;
	} else if ((op & XMLDIFF_ADD) && (op & XMLDIFF_REM)) {
		fprintf (stderr, "internal error: Invalid operation (ADD and REM set)!");
		ret = -2;
	} else {
		if (op & XMLDIFF_MOD) {/* some child(s) was changed and has no callback */
			fprintf (stderr, "Node was modified.\n");
			/* TODO: process the change */
		}

		if (op & XMLDIFF_CHAIN) { /* some child(s) was changed and it was processed by its callback */
			fprintf (stderr, "Child(s) of node was modified.\n");
			/* TODO: finalize children operation */
		}

		if (op & XMLDIFF_REM) {
			status = "off";
			if (toasting != 0) {
				fprintf (stderr, "Interrupting ongoing toasting!\n");
				toasting = 0;
			}
		} else if (op & XMLDIFF_ADD) {
			status = "on";
		}
		ret = EXIT_SUCCESS;
	}

	fprintf (stderr, "Turning toaster %s\n", status);
	return ret;
}

/*
* Structure transapi_config_callbacks provide mapping between callback and path in configuration datastore.
* It is used by libnetconf library to decide which callbacks will be run.
* DO NOT alter this structure
*/
struct transapi_xml_data_callbacks clbks =  {
	.callbacks_count = 1,
	.data = NULL,
	.callbacks = {
		{.path = "/", .func = callback_}
	}
};

/*
* RPC callbacks
* Here follows set of callback functions run every time RPC specific for this device arrives.
* You can safely modify the bodies of all function as well as add new functions for better lucidity of code.
* Every function takes array of inputs as an argument. On few first lines they are assigned to named variables. Avoid accessing the array directly.
* If input was not set in RPC message argument in set to NULL.
*/

void * make_toast (void * doneness)
{
	pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

	/* pretend toasting */
	sleep (*(int*)doneness);

	/* BEGIN of critical section */
	pthread_mutex_lock (&cancel_mutex);
	if (cancel) {
		pthread_mutex_unlock (&cancel_mutex);
		cancel = 0;
		return NULL;
	}
	/* turn off */
	toasting = 0;
	ncntf_event_new(-1, NCNTF_GENERIC, "<toastDone><toastStatus>done</toastStatus></toastDone>");
	/* END of critical section */
	pthread_mutex_unlock (&cancel_mutex);
	return NULL;
}

nc_reply * rpc_make_toast (xmlNodePtr input[])
{
	xmlNodePtr toasterDoneness = input[0];
	xmlNodePtr toasterToastType = input[1];

	struct nc_err * err;
	int doneness; 
	pthread_t tid;

	if (strcmp(status, "off") == 0) {
		return nc_reply_error(nc_err_new (NC_ERR_RES_DENIED));
	}else if (toasting) {
		return nc_reply_error (nc_err_new (NC_ERR_IN_USE));
	}

	if (toasterDoneness == NULL) {
		doneness = 5;
	} else {
		doneness = atoi (xmlNodeGetContent(toasterDoneness));
	}

	if (doneness == 0) { /* doneness must be from <1,10> */
		return nc_reply_error (nc_err_new (NC_ERR_INVALID_VALUE));
	} else { /* all seems ok */
		toasting = 1;
		pthread_mutex_destroy (&cancel_mutex);
		if (pthread_mutex_init (&cancel_mutex, NULL) || pthread_create (&tid, NULL, make_toast, &doneness)) { /* cannot turn heater on :-) */
			err = nc_err_new (NC_ERR_OP_FAILED);
			nc_err_set (err, NC_ERR_PARAM_MSG, "Toaster is broken!");
			toasting = 0;
			ncntf_event_new(-1, NCNTF_GENERIC, "<toastDone><toastStatus>error</toastStatus></toastDone>");
			return nc_reply_error (err);
		}
		pthread_detach (tid);
	}

	return nc_reply_ok(); 
}

nc_reply * rpc_cancel_toast (xmlNodePtr input[])
{
	nc_reply * reply;
	struct nc_err * err;

	if (strcmp(status, "off") == 0) {
		return nc_reply_error(nc_err_new (NC_ERR_RES_DENIED));
	}

	/* BEGIN of critical section */
	pthread_mutex_lock (&cancel_mutex);
	if (toasting == 0) {
		err = nc_err_new (NC_ERR_OP_FAILED);
		nc_err_set (err, NC_ERR_PARAM_MSG, "There is no toasting in progress.");
		reply = nc_reply_error(err);
	} else {
		cancel = 1;
		toasting = 0;
		reply = nc_reply_ok();
		ncntf_event_new(-1, NCNTF_GENERIC, "<toastDone><toastStatus>cancelled</toastStatus></toastDone>");
	}
	/* END of critical section */
	pthread_mutex_unlock (&cancel_mutex);

	return reply;
}
/*
* Structure transapi_rpc_callbacks provide mapping between callbacks and RPC messages.
* It is used by libnetconf library to decide which callbacks will be run when RPC arrives.
* DO NOT alter this structure
*/
struct transapi_xml_rpc_callbacks rpc_clbks = {
	.callbacks_count = 2,
	.callbacks = {
		{.name="make-toast", .func=rpc_make_toast, .arg_count=2, .arg_order={"toasterDoneness", "toasterToastType"}},
		{.name="cancel-toast", .func=rpc_cancel_toast, .arg_count=0, .arg_order={}}
	}
};

