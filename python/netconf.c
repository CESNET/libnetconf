#include <Python.h>

#include <syslog.h>

#include <libnetconf.h>

#include "netconf.h"

extern PyTypeObject ncSessionType;

PyObject *libnetconfError;
PyObject *libnetconfWarning;

static int syslogEnabled = 1;
static void clb_print(NC_VERB_LEVEL level, const char* msg)
{
	switch (level) {
	case NC_VERB_ERROR:
		PyErr_SetString(libnetconfError, msg);
		if (syslogEnabled) {syslog(LOG_ERR, "%s", msg);}
		break;
	case NC_VERB_WARNING:
		if (syslogEnabled) {syslog(LOG_WARNING, "%s", msg);}
		PyErr_WarnEx(libnetconfWarning, msg, 1);
		break;
	case NC_VERB_VERBOSE:
		if (syslogEnabled) {syslog(LOG_INFO, "%s", msg);}
		break;
	case NC_VERB_DEBUG:
		if (syslogEnabled) {syslog(LOG_DEBUG, "%s", msg);}
		break;
	}
}

static PyObject *setSyslog(PyObject *self, PyObject *args, PyObject *keywds)
{
	char* name = NULL;
	static char* logname = NULL;
	static int option = LOG_PID;
	static int facility = LOG_DAEMON;

	static char *kwlist[] = {"enabled", "name", "option", "facility", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, keywds, "p|sii", kwlist, &syslogEnabled, &name, &option, &facility)) {
		return NULL;
	}

	if (name) {
		free(logname);
		logname = strdup(name);

		closelog();
		openlog(logname, option, facility);
	}

	Py_RETURN_NONE;
}

static PyObject *setVerbosity(PyObject *self, PyObject *args, PyObject *keywds)
{
	int level = NC_VERB_ERROR; /* 0 */

	static char *kwlist[] = {"level", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, keywds, "i", kwlist, &level)) {
		return NULL;
	}

	/* normalize level value if not from the enum */
	if (level < NC_VERB_ERROR) {
		nc_verbosity(NC_VERB_ERROR);
	} else if (level > NC_VERB_DEBUG) {
		nc_verbosity(NC_VERB_DEBUG);
	} else {
		nc_verbosity(level);
	}

	Py_RETURN_NONE;
}

static PyObject *getCapabilities(PyObject *self)
{
	struct nc_cpblts *cpblts;
	PyObject *result = NULL;
	int c, i;

	cpblts = nc_session_get_cpblts_default();

	result = PyList_New(c = nc_cpblts_count(cpblts));
	nc_cpblts_iter_start(cpblts);
	for (i = 0; i < c; i++) {
		PyList_SET_ITEM(result, i, PyUnicode_FromString(nc_cpblts_iter_next(cpblts)));
	}
	nc_cpblts_free(cpblts);

	return (result);
}

static PyMethodDef netconfMethods[] = {
		{"setVerbosity", (PyCFunction)setVerbosity, METH_VARARGS | METH_KEYWORDS, "Set verbose level (0-3)."},
		{"setSyslog", (PyCFunction)setSyslog, METH_VARARGS | METH_KEYWORDS, "Set application settings for syslog."},
		{"getCapabilities", (PyCFunction)getCapabilities, METH_NOARGS, "Get list of default capabilities from libnetconf."},
		{NULL, NULL, 0, NULL}
};

static struct PyModuleDef ncModule = {
		PyModuleDef_HEAD_INIT,
		"netconf",
		"NETCONF Protocol implementation",
		-1,
		netconfMethods,
};

/* module initializer */
PyMODINIT_FUNC PyInit_netconf(void)
{
	PyObject *nc;

	/* initiate libnetconf - all subsystems */
	nc_init(NC_INIT_ALL);

	/* set print callback */
	nc_callback_print (clb_print);

	ncSessionType.tp_new = PyType_GenericNew;
	if (PyType_Ready(&ncSessionType) < 0) {
	    return NULL;
	}

	/* create netconf as the Python module */
	nc = PyModule_Create(&ncModule);
	if (nc == NULL) {
		return NULL;
	}

    Py_INCREF(&ncSessionType);
    PyModule_AddObject(nc, "Session", (PyObject *)&ncSessionType);

    PyModule_AddStringConstant(nc, "NETCONFv1_0", NETCONF_CAP_BASE10);
    PyModule_AddStringConstant(nc, "NETCONFv1_1", NETCONF_CAP_BASE11);
    PyModule_AddStringConstant(nc, "TRANSPORT_SSH", NETCONF_TRANSPORT_SSH);
    PyModule_AddStringConstant(nc, "TRANSPORT_TLS", NETCONF_TRANSPORT_TLS);

    PyModule_AddIntConstant(nc, "WD_ALL", NCWD_MODE_ALL);
    PyModule_AddIntConstant(nc, "WD_ALL_TAGGED", NCWD_MODE_ALL_TAGGED);
    PyModule_AddIntConstant(nc, "WD_TRIM", NCWD_MODE_TRIM);
    PyModule_AddIntConstant(nc, "WD_MODE_EXPLICIT", NCWD_MODE_EXPLICIT);

    PyModule_AddIntConstant(nc, "RUNNING", NC_DATASTORE_RUNNING);
    PyModule_AddIntConstant(nc, "STARTUP", NC_DATASTORE_STARTUP);
    PyModule_AddIntConstant(nc, "CANDIDATE", NC_DATASTORE_CANDIDATE);

	/* init libnetconf exceptions for use in clb_print() */
	libnetconfError = PyErr_NewExceptionWithDoc("netconf.Error", "Error passed from the underlying libnetconf library.", NULL, NULL);
	Py_INCREF(libnetconfError);
	PyModule_AddObject(nc, "Error", libnetconfError);

	libnetconfWarning = PyErr_NewExceptionWithDoc("netconf.Warning", "Warning passed from the underlying libnetconf library.", PyExc_Warning, NULL);
	Py_INCREF(libnetconfWarning);
	PyModule_AddObject(nc, "Warning", libnetconfWarning);

	return nc;
}
