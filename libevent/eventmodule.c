/*
 * eventmodule.c:  a wrapper for libevent (http://monkey.org/~provos/libevent/)
 * Copyright (c) 2006 Andy Gross <andy@andygross.org>
 * Copyright (c) 2006 Nick Mathewson 
 * See LICENSE.txt for licensing information.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <event.h>
#include <Python.h>
#include <structmember.h>

#define DEFAULT_NUM_PRIORITIES 3
 
/*  
 * EventBaseObject wraps a (supposedly) thread-safe libevent dispatch context.
 */
typedef struct EventBaseObject { 
    PyObject_HEAD
    struct event_base *ev_base;
} EventBaseObject;

/* Forward declaration of CPython type object */
static PyTypeObject EventBase_Type;


/*  
 * EventObject wraps a libevent 'struct event'
 */
typedef struct EventObject { 
    PyObject_HEAD
    struct event ev;
    EventBaseObject *eventBase;
    PyObject *callback;
} EventObject;

/* Forward declaration of CPython type object */
static PyTypeObject Event_Type;

/* EventObject prototypes */
static PyObject *Event_New(PyTypeObject *, PyObject *, PyObject *);
static int Event_Init(EventObject *, PyObject *, PyObject *);

/* Singleton default event base */
static EventBaseObject *defaultEventBase;

/* Reference to the logging callback */
static PyObject *logCallback;

/* Error Objects */
PyObject *EventErrorObject;

/* Typechecker */
int EventBase_Check(PyObject *o) { 
    return ((o->ob_type) == &EventBase_Type);
}

/* Construct a new EventBaseObject */
static PyObject *EventBase_New(PyTypeObject *type, PyObject *args, 
			       PyObject *kwds) 
{
    EventBaseObject *self = NULL;
    assert(type != NULL && type->tp_alloc != NULL);
    self = (EventBaseObject *)type->tp_alloc(type, 0);
    if (self != NULL) { 
	self->ev_base = event_init();
	if (self->ev_base == NULL)  { 
	    return NULL;
	}
    }
    return (PyObject *)self;
}

/* EventBaseObject initializer */
static int EventBase_Init(EventBaseObject *self, PyObject *args, 
			  PyObject *kwargs) 
{ 
    static char *kwlist[] = {"numPriorities", NULL};
    int numPriorities = 0;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:event", kwlist, 
				     &numPriorities))
	return -1;
    
    if (!numPriorities)
	numPriorities = DEFAULT_NUM_PRIORITIES;
    
    if ( (event_base_priority_init(self->ev_base, numPriorities)) < 0) { 
	return -1;
    }
    return 0;
}

/* EventBaseObject destructor */
static void EventBase_Dealloc(EventBaseObject *obj) { 
    obj->ob_type->tp_free((PyObject *)obj);
}	

/* EventBaseObject methods */
PyDoc_STRVAR(EventBase_LoopDoc,
"loop(self, [flags=0])\n\
\n\
Perform one iteration of the event loop.  Valid flags arg EVLOOP_NONBLOCK \n\
and EVLOOP_ONCE.");
static PyObject *EventBase_Loop(EventBaseObject *self, PyObject *args, 
				PyObject *kwargs) 
{ 
    static char *kwlist[] = {"flags", NULL};
    int flags = 0;
    int rv = 0;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:loop", kwlist, &flags))
	return NULL;
    
    rv = event_base_loop(self->ev_base, flags);
    return PyInt_FromLong(rv);
}
PyDoc_STRVAR(EventBase_LoopExitDoc,
"loopExit(self, seconds=0)\n\
\n\
Cause the event loop to exit after <seconds> seconds.");
static PyObject *EventBase_LoopExit(EventBaseObject *self, PyObject *args, 
				    PyObject *kwargs) { 
    static char *  kwlist[] = {"seconds", NULL};
    struct timeval tv;
    int            rv = 0;
    double         exitAfterSecs = 0;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "d:loopExit", 
				     kwlist, &exitAfterSecs))
	return NULL;
    
    tv.tv_sec = (long) exitAfterSecs;
    tv.tv_usec = (exitAfterSecs - (long) exitAfterSecs) * 1000000;
    rv = event_base_loopexit(self->ev_base, &tv);
    return PyInt_FromLong(rv);
}

PyDoc_STRVAR(EventBase_DispatchDoc,
"dispatch(self)\n\
\n\
Run the main dispatch loop associated with this event base.  This function\n\
only terminates when no events remain, or the loop is terminated via an \n\
explicit call to EventBase.loopExit() or via a signal.");
static PyObject *EventBase_Dispatch(EventBaseObject *self, PyObject *args,
				    PyObject *kwargs) { 

    int rv = event_base_dispatch(self->ev_base);
    return PyInt_FromLong(rv);

}

PyDoc_STRVAR(EventBase_CreateEventDoc,
"createEvent(self, fd, events, callback)\n\
\n\
Create a new Event object for the given file descriptor that will call\n\
<callback> with a 3-tuple of (fd, events, eventObject) when the event\n\
fires. The first argument, fd, can be either an integer file descriptor\n\
or a 'file-like' object with a fileno() method.");
static EventObject *EventBase_CreateEvent(EventBaseObject *self, 
					  PyObject *args, PyObject *kwargs) 
{ 
    EventObject *newEvent = NULL;

    newEvent = (EventObject *)Event_New(&Event_Type,NULL,NULL);

    if (Event_Init(newEvent, args, kwargs) < 0)
	return NULL;
	
    if (PyObject_CallMethod((PyObject *)newEvent, 
			    "setEventBase", "O", self) == NULL)
	return NULL;
    return newEvent;
}

PyDoc_STRVAR(EventBase_CreateTimerDoc,
"createTimer(self, callback) -> new timer Event\n\
\n\
Create a new timer object that will call <callback>.  The timeout is not\n\
specified here, but rather via the Event.addToLoop([timeout]) method");
static EventObject *EventBase_CreateTimer(EventBaseObject *self, 
					  PyObject *args, PyObject *kwargs) 
{ 
    static char *kwlist[] = {"callback", NULL};
    EventObject *newTimer = NULL;
    PyObject    *callback = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:createTimer", 
				     kwlist, &callback))
	return NULL;

    newTimer = (EventObject *)PyObject_CallMethod((PyObject *)self,
						  "createEvent", 
						  "OiO", Py_None, EV_TIMEOUT,
						  callback);
    return newTimer;
}

PyDoc_STRVAR(EventBase_CreateSignalHandlerDoc,
"createSignalHandler(self, signum, callback) -> new signal handler Event\n\
\n\
Create a new signal handler object that will call <callback> when the signal\n\
is received.  Signal handlers are by default persistent - you must manually\n\
remove them with removeFromLoop().");
static EventObject *EventBase_CreateSignalHandler(EventBaseObject *self, 
						  PyObject *args, 
						  PyObject *kwargs) { 
    static char *kwlist[] = {"signal", "callback", NULL};
    EventObject *newSigHandler = NULL;
    PyObject    *callback = NULL;
    int          sig = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "iO:createSignalHandler", 
				     kwlist, &sig, &callback))
	return NULL;

    newSigHandler = (EventObject *)PyObject_CallMethod((PyObject *)self,
						       "createEvent", 
						       "iiO", 
						       sig, 
						       EV_SIGNAL|EV_PERSIST,
						       callback);
    return newSigHandler;
}


static PyGetSetDef EventBase_Properties[] = {
    {NULL},
};

static PyMemberDef EventBase_Members[] = {
    {NULL},
};


static PyMethodDef EventBase_Methods[] = { 
    {"loop",                     (PyCFunction)EventBase_Loop,        
     METH_VARARGS|METH_KEYWORDS, EventBase_LoopDoc}, 
    {"loopExit",                 (PyCFunction)EventBase_LoopExit,    
     METH_VARARGS|METH_KEYWORDS, EventBase_LoopExitDoc},
    {"createEvent",              (PyCFunction)EventBase_CreateEvent, 
     METH_VARARGS|METH_KEYWORDS, EventBase_CreateEventDoc},
    {"createSignalHandler",      (PyCFunction)EventBase_CreateSignalHandler,
     METH_VARARGS|METH_KEYWORDS, EventBase_CreateSignalHandlerDoc},
    {"createTimer",              (PyCFunction)EventBase_CreateTimer, 
     METH_VARARGS|METH_KEYWORDS, EventBase_CreateTimerDoc},
    {"dispatch",                 (PyCFunction)EventBase_Dispatch,
     METH_NOARGS,                EventBase_DispatchDoc},
    {NULL},
};

static PyTypeObject EventBase_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,                      
    "event.EventBase",                         /*tp_name*/
    sizeof(EventBaseObject),                   /*tp_basicsize*/
    0,                                         /*tp_itemsize*/
    /* methods */
    (destructor)EventBase_Dealloc,             /*tp_dealloc*/
    0,                                         /*tp_print*/
    0,                                         /*tp_getattr*/
    0,                                         /*tp_setattr*/
    0,                                         /*tp_compare*/
    0,                                         /*tp_repr*/
    0,                                         /*tp_as_number*/
    0,                                         /*tp_as_sequence*/
    0,                                         /*tp_as_mapping*/
    0,                                         /*tp_hash*/
    0,                                         /*tp_call*/
    0,                                         /*tp_str*/
    PyObject_GenericGetAttr,                   /*tp_getattro*/
    PyObject_GenericSetAttr,                   /*tp_setattro*/
    0,                                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /*tp_flags*/
    0,                                         /*tp_doc*/
    0,                                         /*tp_traverse*/
    0,                                         /*tp_clear*/
    0,                                         /*tp_richcompare*/
    0,                                         /*tp_weaklistoffset*/
    0,                                         /*tp_iter*/
    0,                                         /*tp_iternext*/
    EventBase_Methods,                         /*tp_methods*/
    EventBase_Members,                         /*tp_members*/
    EventBase_Properties,                      /*tp_getset*/
    0,                                         /*tp_base*/
    0,                                         /*tp_dict*/
    0,                                         /*tp_descr_get*/
    0,                                         /*tp_descr_set*/
    0,                                         /*tp_dictoffset*/
    (initproc)EventBase_Init,                  /*tp_init*/
    PyType_GenericAlloc,                       /*tp_alloc*/
    EventBase_New,                             /*tp_new*/
    PyObject_Del,                              /*tp_free*/
    0,                                         /*tp_is_gc*/
};




/* Typechecker */
int Event_Check(PyObject *o) { 
    return ((o->ob_type) == &Event_Type);
}

/* Construct a new EventObject */
static PyObject *Event_New(PyTypeObject *type, PyObject *args, 
			   PyObject *kwargs) 
{
    EventObject *self = NULL;
    assert(type != NULL && type->tp_alloc != NULL);
    self = (EventObject *)type->tp_alloc(type, 0);
    self->eventBase = NULL;
    return (PyObject *)self;
}

/* Callback thunk. */
static void __libevent_ev_callback(int fd, short events, void *arg) {
    EventObject    *ev = arg;
    PyObject       *result;
    PyObject       *tuple = PyTuple_New(3);
    PyTuple_SET_ITEM(tuple, 0, PyInt_FromLong(fd));
    PyTuple_SET_ITEM(tuple, 1, PyInt_FromLong(events));
    PyTuple_SET_ITEM(tuple, 2, (PyObject *) ev);
    Py_INCREF((PyObject *) ev);
    result = PyObject_Call(ev->callback, tuple, NULL);
    Py_DECREF((PyObject *) ev);
    //Py_DECREF(tuple);
    if (result) { 
	Py_DECREF(result);
    }
    else { 
      /* 
       * The callback raised an exception. This usually isnt a problem because
       * the callback's caller is in Python-land. Here, we don't have many
       * good options.  For now, we just print the exception.  The commented
       * out code below is supposed to asynchronously raise an exception in
       * the main thread, but that doesn't work if libevent is blocked on
       * an I/O call like select() or kevent().  We could terminate the 
       * event loop from here, but that seems a little drastic.  Somehow,
       * we should move the callback invocation to Python.  I think.
       */
      /* 
       PyThreadState  *ts = PyThreadState_Get();
       int r  = PyThreadState_SetAsyncExc(ts->thread_id, EventErrorObject);
       printf("%d\n", r);
      */
      PyErr_Print();
      PyErr_WriteUnraisable(ev->callback);

    }
}


/* EventObject initializer */
static int Event_Init(EventObject *self, PyObject *args, PyObject *kwargs) { 
    int             fd = -1;
    PyObject        *fdObj = NULL;
    int             events = 0;
    PyObject        *callback = NULL;
    static char     *kwlist[] = {"fd", "events", "callback", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OiO:event", kwlist,
				     &fdObj, &events, &callback))
	return -1;
    
    if (!PyCallable_Check(callback)) {
	PyErr_SetString(EventErrorObject,"callback argument must be callable");
	return -1;
    }
    
    if (fdObj != Py_None) { 
	if ( (fd = PyObject_AsFileDescriptor(fdObj)) == -1 ) { 
	    return -1;
	}
    }
    event_set(&self->ev, fd, events, __libevent_ev_callback, self);
    if (! event_initialized(&self->ev) )
	return -1; 
    
    Py_INCREF(callback);
    self->callback = callback;
    return 0;
}	

PyDoc_STRVAR(Event_SetPriorityDoc,
"setPriority(self, priority)\n\
\n\
Set the priority for this event.");
static PyObject *Event_SetPriority(EventObject *self, PyObject *args, 
				   PyObject *kwargs) 
{ 
    static char *kwlist[] = {"priority", NULL};
    int          priority = 0;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs , "|i:setPriority", 
				     kwlist, &priority))
	return NULL;
    if (event_priority_set(&self->ev, priority) < 0) { 
	PyErr_SetString(EventErrorObject, 
			"error setting event priority - event is either already active or priorities are not enabled");
	return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(Event_AddToLoopDoc,
"addToLoop(self, timeout=-1)\n\
\n\
Add this event to the event loop, with a timeout of <timeout> seconds.\n\
A timeout value of -1 seconds causes the event to remain in the loop \n\
until it fires or is manually removed with removeFromLoop().");
static PyObject *Event_AddToLoop(EventObject *self, PyObject *args, 
				 PyObject *kwargs) { 
    double          timeout = -1.0;
    struct timeval  tv;
    static char    *kwlist[] = {"timeout", NULL};
    int             rv;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|d:addToLoop", kwlist,
				     &timeout))
        return NULL;

    if (timeout >= 0.0) {
        tv.tv_sec = (long) timeout;
        tv.tv_usec = (timeout - (long) timeout) * 1000000;
        rv = event_add(&((EventObject *) self)->ev, &tv);
    }
    else { 
        rv = event_add(&((EventObject *) self)->ev, NULL);
    }
    if (rv != 0) {
        PyErr_SetFromErrno(EventErrorObject);
        return NULL;
    }
    Py_INCREF(self);
    Py_INCREF(Py_None);
    return Py_None;
}
PyDoc_STRVAR(Event_RemoveFromLoopDoc,
"removeFromLoop(self)\n\
\n\
Remove the event from the event loop.");
static PyObject *Event_RemoveFromLoop(EventObject *self, PyObject *args, 
				      PyObject *kwargs) { 

    if (event_del(&self->ev) < 0) { 
	PyErr_SetFromErrno(EventErrorObject);
	return NULL;
    }
    Py_DECREF(self);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *Event_SetEventBase(EventObject *self, PyObject *args, 
				    PyObject *kwargs) { 
    static char *kwlist[] = {"eventBase", NULL};
    PyObject    *eventBase;
    int          rv = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &eventBase))
	return NULL;
    
    if (!EventBase_Check(eventBase)) { 
	PyErr_SetString(EventErrorObject, "argument is not an EventBase object");
	return NULL;
    }
    rv = event_base_set(((EventBaseObject *)eventBase)->ev_base, &self->ev);
    if (rv < 0) { 
	PyErr_SetString(EventErrorObject, "unable to set event base");
	return NULL;
    }
    if (self->eventBase != NULL) { 
	Py_XDECREF(self->eventBase);
    }
    Py_INCREF(eventBase);
    self->eventBase = (EventBaseObject *)eventBase;
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(Event_PendingDoc,
"pending(self)\n\
\n\
Returns the event flags set for this event OR'd together.");
static PyObject *Event_Pending(EventObject *self, PyObject *args, 
			       PyObject *kwargs) { 
    int flags;
    flags = event_pending(&((EventObject *) self)->ev,
			  EV_TIMEOUT | EV_READ | EV_WRITE | EV_SIGNAL, NULL);
    return PyInt_FromLong(flags);    
}

PyDoc_STRVAR(Event_GetTimeoutDoc,
"getTimeout(self)\n\
\n\
Returns the expiration time of this event.");
static PyObject *Event_GetTimeout(EventObject *self, PyObject *args, 
				  PyObject *kwargs) { 
    double          d;
    struct timeval  tv;

    tv.tv_sec = -1;
    event_pending(&((EventObject *) self)->ev, 0, &tv);

    if (tv.tv_sec > -1) {
	d = tv.tv_sec + (tv.tv_usec / 1000000.0);
	return PyFloat_FromDouble(d);
    }       
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(Event_FilenoDoc,
"fileno(self)\n\
\n\
Return the integer file descriptor number associated with this event.\n\
Not especially meaningful for signal or timer events.");
static PyObject *Event_Fileno(EventObject *self, PyObject *args, 
			      PyObject *kwargs) { 
    return PyInt_FromLong(self->ev.ev_fd);
}


/* EventObject destructor */
static void Event_Dealloc(EventObject *obj) { 
    Py_XDECREF(obj->eventBase);
    Py_XDECREF(obj->callback);
    obj->ob_type->tp_free((PyObject *)obj);
}	

static PyObject *Event_Repr(EventObject *self) {
	char            buf[512];
	PyOS_snprintf(buf, sizeof(buf),
		      "<event object, fd=%ld, events=%d>",
		      (long) self->ev.ev_fd,
		      (int) self->ev.ev_events);
	return PyString_FromString(buf);
}

#define OFF(x) offsetof(EventObject, x)
static PyMemberDef Event_Members[] = {
    {"eventBase", T_OBJECT, OFF(eventBase),     
     RO, "The EventBase for this event object"},
    {"callback",  T_OBJECT, OFF(callback),      
     RO, "The callback for this event object"},
    {"events",    T_SHORT,  OFF(ev.ev_events),  
     RO, "Events registered for this event object"},
    {"numCalls",  T_SHORT,  OFF(ev.ev_ncalls), 
     RO, "Number of times this event has been called"},   
    {"priority",  T_INT,    OFF(ev.ev_pri),     
     RO, "Event priority"},
    {"flags",     T_INT,    OFF(ev.ev_flags),   
     RO, "Event flags (internal)"},
    {NULL}
};
#undef OFF

static PyGetSetDef Event_Properties[] = {
    {NULL},
};

static PyMethodDef Event_Methods[] = { 
    {"addToLoop",                (PyCFunction)Event_AddToLoop,      
     METH_VARARGS|METH_KEYWORDS, Event_AddToLoopDoc},
    {"removeFromLoop",           (PyCFunction)Event_RemoveFromLoop, 
     METH_NOARGS,                Event_RemoveFromLoopDoc},     
    {"fileno",                   (PyCFunction)Event_Fileno,         
     METH_NOARGS,                Event_FilenoDoc},
    {"setPriority",              (PyCFunction)Event_SetPriority,
     METH_VARARGS|METH_KEYWORDS, Event_SetPriorityDoc},
    {"setEventBase",             (PyCFunction)Event_SetEventBase,
     METH_VARARGS|METH_KEYWORDS},
    {"pending",                  (PyCFunction)Event_Pending,
     METH_NOARGS,                Event_PendingDoc},
    {"getTimeout",               (PyCFunction)Event_GetTimeout,
     METH_NOARGS,                Event_GetTimeoutDoc},
    {NULL},
};

static PyTypeObject Event_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,                      
    "event.Event",                             /*tp_name*/
    sizeof(EventObject),                       /*tp_basicsize*/
    0,                                         /*tp_itemsize*/
    /* methods */
    (destructor)Event_Dealloc,                 /*tp_dealloc*/
    0,                                         /*tp_print*/
    0,                                         /*tp_getattr*/
    0,                                         /*tp_setattr*/
    0,                                         /*tp_compare*/
    (reprfunc)Event_Repr,                      /*tp_repr*/
    0,                                         /*tp_as_number*/
    0,                                         /*tp_as_sequence*/
    0,                                         /*tp_as_mapping*/
    0,                                         /*tp_hash*/
    0,                                         /*tp_call*/
    0,                                         /*tp_str*/
    PyObject_GenericGetAttr,                   /*tp_getattro*/
    PyObject_GenericSetAttr,                   /*tp_setattro*/
    0,                                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /*tp_flags*/
    0,                                         /*tp_doc*/
    0,                                         /*tp_traverse*/
    0,                                         /*tp_clear*/
    0,                                         /*tp_richcompare*/
    0,                                         /*tp_weaklistoffset*/
    0,                                         /*tp_iter*/
    0,                                         /*tp_iternext*/
    Event_Methods,                             /*tp_methods*/
    Event_Members,                             /*tp_members*/
    Event_Properties,                          /*tp_getset*/
    0,                                         /*tp_base*/
    0,                                         /*tp_dict*/
    0,                                         /*tp_descr_get*/
    0,                                         /*tp_descr_set*/
    0,                                         /*tp_dictoffset*/
    (initproc)Event_Init,                      /*tp_init*/
    PyType_GenericAlloc,                       /*tp_alloc*/
    Event_New,                                 /*tp_new*/
    PyObject_Del,                              /*tp_free*/
    0,                                         /*tp_is_gc*/
};



static PyObject *EventModule_setLogCallback(PyObject *self, PyObject *args, 
					    PyObject *kwargs) { 
    static char  *kwlist[] = {"callback", NULL};
	
    if (!PyArg_ParseTupleAndKeywords(args,kwargs,"O:setLogCallback", kwlist, 
				     &logCallback))
	return NULL;
    
    if (!PyCallable_Check(logCallback)) { 
        PyErr_SetString(EventErrorObject, "log callback is not a callable");
	logCallback = NULL;
	return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef EventModule_Functions[] = { 
    {"setLogCallback", (PyCFunction)EventModule_setLogCallback, 
     METH_VARARGS|METH_KEYWORDS},	
    {NULL},
};

#define ADDCONST(mod, name, const)  PyModule_AddIntConstant(mod, name, const)
DL_EXPORT(void) initevent(void)
{
    PyObject       *m, *d;
	
    m = Py_InitModule("event", EventModule_Functions);
    d = PyModule_GetDict(m);

    if (EventErrorObject == NULL) {
	EventErrorObject = PyErr_NewException("libevent.EventError", 
					      NULL, NULL);
        if (EventErrorObject == NULL)
            return;
    }
    Py_INCREF(EventErrorObject);
    PyModule_AddObject(m, "EventError", EventErrorObject);

    if (PyType_Ready(&EventBase_Type) < 0) { 
	return;
    }
    PyModule_AddObject(m, "EventBase", (PyObject *)&EventBase_Type);
    
    if (PyType_Ready(&Event_Type) < 0)
	return;
    PyModule_AddObject(m, "Event", (PyObject *)&Event_Type);	
    
    defaultEventBase = (EventBaseObject *)EventBase_New(&EventBase_Type, 
							NULL, NULL);

    if (defaultEventBase == NULL) { 
	PyErr_SetString(EventErrorObject, 
			"error: couldn't create default event base");
	return;
    }
    if (EventBase_Init(defaultEventBase, PyTuple_New(0), NULL) < 0) { 
	PyErr_SetString(EventErrorObject, 
			"error: couldn't initialize default event base");
	return;
    }
    PyModule_AddObject(m, "DefaultEventBase", (PyObject *)defaultEventBase);
    
    /* Add constants to the module */
    ADDCONST(m, "EV_READ", EV_READ);
    ADDCONST(m, "EV_WRITE", EV_WRITE);
    ADDCONST(m, "EV_TIMEOUT", EV_TIMEOUT);
    ADDCONST(m, "EV_SIGNAL", EV_SIGNAL);
    ADDCONST(m, "EV_PERSIST", EV_PERSIST);
    ADDCONST(m, "EVLOOP_ONCE", EVLOOP_ONCE);
    ADDCONST(m, "EVLOOP_NONBLOCK", EVLOOP_NONBLOCK);
    PyModule_AddObject(m, "LIBEVENT_VERSION", 
		       PyString_FromString(event_get_version()));
    PyModule_AddObject(m, "LIBEVENT_METHOD",
		       PyString_FromString(event_get_method()));
}
