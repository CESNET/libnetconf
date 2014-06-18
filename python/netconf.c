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
	nc_session_free(self->session);
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

	char *kwlist[] = {"host", "port", "user", "capabilities", NULL};

	/* Get input parameters */
	if (! PyArg_ParseTupleAndKeywords(args, keywords, "s|HsO!", kwlist, &host, &port, &user, &PyList_Type, &(PyCpblts))) {
		PyErr_SetString(PyExc_AttributeError, "test");
		return -1;
	}

	if (PyCpblts != NULL) {
		cpblts = nc_cpblts_new(NULL);
		if (PyList_Check(PyCpblts) && ((l = PyList_Size(PyCpblts)) > 0)) {
			for (i = 0; i < l; i++) {
				PyObject *PyStr = PyUnicode_AsEncodedString(PyList_GetItem(PyCpblts, i), "UTF-8", NULL);
				item = PyBytes_AS_STRING(PyStr);
				nc_cpblts_add(cpblts, item);
				Py_XDECREF(PyStr);
			}
		}
	}

	session = nc_session_connect(host, port, user, cpblts);
	if (session == NULL) {
		return -1;
	}

	nc_session_free(self->session);
	self->session = session;

	return 0;
}

static PyObject * ncSession_id(ncSessionObject *self)
{
	if (self->session == NULL) {
		PyErr_SetString(PyExc_AttributeError, "session");
		return (NULL);
	}

	return PyUnicode_FromFormat("%s", nc_session_get_id(self->session));
}

static PyMethodDef ncSessionMethods[] = {
		{"id", (PyCFunction)ncSession_id, METH_NOARGS, "Return the NETCONF session id."},
		{NULL, NULL, 0, NULL}
};

static PyTypeObject ncSessionType = {
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
		0, /* tp_getset */
		0, /* tp_base */
		0, /* tp_dict */
		0, /* tp_descr_get */
		0, /* tp_descr_set */
		0, /* tp_dictoffset */
		(initproc) ncSessionInit, /* tp_init */
		0, /* tp_alloc */
		0, /* tp_new */
};

static struct PyModuleDef ncModule = {
		PyModuleDef_HEAD_INIT,
		"netconf",
		"NETCONF Protocol implementation",
		-1,
};

/* module initializer */
PyMODINIT_FUNC PyInit_netconf(void)
{
	PyObject *nc;

	/* initiate libnetconf - all subsystems */
	nc_init(NC_INIT_ALL);

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

	return nc;
}
