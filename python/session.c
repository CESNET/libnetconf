#include <Python.h>
#include <structmember.h>

#include <libnetconf.h>

typedef struct {
	PyObject_HEAD
	struct nc_session* session;
} ncSessionObject;

static PyMemberDef ncSessionMembers[] = {
	{NULL}  /* Sentinel */
};

static void ncSessionFree(ncSessionObject *self)
{
	PyObject *err_type, *err_value, *err_traceback;

	/* save the current exception state */
	PyErr_Fetch(&err_type, &err_value, &err_traceback);

	nc_session_free(self->session);

	/* restore the saved exception state */
	PyErr_Restore(err_type, err_value, err_traceback);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static int ncSessionInit(ncSessionObject *self, PyObject *args, PyObject *keywords)
{
	char *host = NULL, *user = NULL;
	unsigned short port = 830;
	PyObject *PyCpblts = NULL;
	struct nc_session *session;
	struct nc_cpblts *cpblts = NULL;
	char* item = NULL;
	Py_ssize_t l, i;
	int ret;

	char *kwlist[] = {"host", "port", "user", "capabilities", NULL};

	/* Get input parameters */
	if (! PyArg_ParseTupleAndKeywords(args, keywords, "s|HsO!", kwlist, &host, &port, &user, &PyList_Type, &(PyCpblts))) {
		return -1;
	}

	if (PyCpblts != NULL) {
		cpblts = nc_cpblts_new(NULL);
		if (PyList_Check(PyCpblts) && ((l = PyList_Size(PyCpblts)) > 0)) {
			for (i = 0; i < l; i++) {
				PyObject *PyUni = PyList_GetItem(PyCpblts, i);
				if (! PyUnicode_Check(PyUni)) {
					PyErr_SetString(PyExc_TypeError, "Capabilities list must contain strings.");
					nc_cpblts_free(cpblts);
					return -1;
				}
				PyObject *PyStr = PyUnicode_AsEncodedString(PyUni, "UTF-8", NULL);
				if (PyStr == NULL) {
					nc_cpblts_free(cpblts);
					return -1;
				}
				item = PyBytes_AsString(PyStr);
				if (item == NULL) {
					nc_cpblts_free(cpblts);
					Py_XDECREF(PyStr);
					return -1;
				}
				ret = nc_cpblts_add(cpblts, item);
				Py_XDECREF(PyStr);
				if (ret != EXIT_SUCCESS) {
					nc_cpblts_free(cpblts);
					return -1;
				}
			}
		}
	}

	session = nc_session_connect(host, port, user, cpblts);
	nc_cpblts_free(cpblts);
	if (session == NULL) {
		PyErr_SetString(PyExc_Exception, "Connecting to the NETCONF server failed.");
		return -1;
	}

	nc_session_free(self->session);
	self->session = session;

	return 0;
}

static PyObject *ncSessionGetId(ncSessionObject *self, void *closure)
{
	return PyUnicode_FromFormat("%s", nc_session_get_id(self->session));
}

static PyObject *ncSessionGetHost(ncSessionObject *self, void *closure)
{
	return PyUnicode_FromFormat("%s", nc_session_get_host(self->session));
}

static PyObject *ncSessionGetPort(ncSessionObject *self, void *closure)
{
	return PyUnicode_FromFormat("%s", nc_session_get_port(self->session));
}

static PyObject *ncSessionGetUser(ncSessionObject *self, void *closure)
{
	return PyUnicode_FromFormat("%s", nc_session_get_user(self->session));
}

static PyObject *ncSessionGetTransport(ncSessionObject *self, void *closure)
{
	NC_TRANSPORT t = nc_session_get_transport(self->session);
	return PyUnicode_FromFormat("%s", (t == NC_TRANSPORT_TLS) ? "TLS" : "SSH");
}

static PyObject *ncSessionGetCapabilities(ncSessionObject *self, void *closure)
{
	struct nc_cpblts* cpblts;
	PyObject *list, *pyItem;
	const char *item;
	int pos;

	cpblts = nc_session_get_cpblts(self->session);
	if (cpblts == NULL) {
		return (NULL);
	}

	list = PyList_New(0);
	nc_cpblts_iter_start(cpblts);
	while((item = nc_cpblts_iter_next(cpblts)) != NULL) {
		if (PyList_Append(list, PyUnicode_FromFormat("%s", item)) != 0) {
			for (pos = 0; PyList_GET_SIZE(list); pos++) {
				pyItem = PyList_GetItem(list, pos);
				Py_DECREF(pyItem);
			}
			Py_DECREF(list);
			return NULL;
		}
	}

	return list;
}

static PyObject *ncSessionGetVersion(ncSessionObject *self, void *closure)
{
	int ver;
	const char* ver_s;

	ver = nc_session_get_version(self->session);
	switch(ver) {
	case 0:
		ver_s = "1.0";
		break;
	case 1:
		ver_s = "1.1";
		break;
	default:
		ver_s = "unknown";
		break;
	}

	return PyUnicode_FromFormat("%s", ver_s);
}

static PyGetSetDef ncSessionGetSeters[] = {
    {"id", (getter)ncSessionGetId, NULL, "NETCONF Session id.", NULL},
    {"host", (getter)ncSessionGetHost, NULL, "Host where the NETCONF Session is connected.", NULL},
    {"port", (getter)ncSessionGetPort, NULL, "Port number where the NETCONF Session is connected.", NULL},
    {"user", (getter)ncSessionGetUser, NULL, "Username of the user connected with the NETCONF Session.", NULL},
    {"transport", (getter)ncSessionGetTransport, NULL, "Transport protocol used for the NETCONF Session.", NULL},
    {"version", (getter)ncSessionGetVersion, NULL, "NETCONF Protocol version used for the NETCONF Session.", NULL},
    {"capabilities", (getter)ncSessionGetCapabilities, NULL, "Capabilities of the NETCONF Session.", NULL},
    {NULL}  /* Sentinel */
};

static PyMethodDef ncSessionMethods[] = {
		{NULL, NULL, 0, NULL}
};

PyTypeObject ncSessionType = {
		PyVarObject_HEAD_INIT(NULL, 0)
		"netconf.Session", /* tp_name */
		sizeof(ncSessionObject), /* tp_basicsize */
		0, /* tp_itemsize */
		(destructor) ncSessionFree, /* tp_dealloc */
		0, /* tp_print */
		0, /* tp_getattr */
		0, /* tp_setattr */
		0, /* tp_reserved */
		0, /* tp_repr */
		0, /* tp_as_number */
		0, /* tp_as_sequence */
		0, /* tp_as_mapping */
		0, /* tp_hash  */
		0, /* tp_call */
		0, /* tp_str */
		0, /* tp_getattro */
		0, /* tp_setattro */
		0, /* tp_as_buffer */
		Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
		"NETCONF Session", /* tp_doc */
		0, /* tp_traverse */
		0, /* tp_clear */
		0, /* tp_richcompare */
		0, /* tp_weaklistoffset */
		0, /* tp_iter */
		0, /* tp_iternext */
		ncSessionMethods, /* tp_methods */
		ncSessionMembers, /* tp_members */
		ncSessionGetSeters, /* tp_getset */
		0, /* tp_base */
		0, /* tp_dict */
		0, /* tp_descr_get */
		0, /* tp_descr_set */
		0, /* tp_dictoffset */
		(initproc) ncSessionInit, /* tp_init */
		0, /* tp_alloc */
		0, /* tp_new */
};

