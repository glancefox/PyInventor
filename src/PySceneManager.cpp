/**
 * \file   
 * \brief      PySceneManager class implementation.
 * \author     Thomas Moeller
 * \details
 *
 * Copyright (C) the PyInventor contributors. All rights reserved.
 * This file is part of PyInventor, distributed under the BSD 3-Clause
 * License. For full terms see the included COPYING file.
 */


#include <Inventor/SoSceneManager.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/engines/SoEngine.h>
#include <Inventor/elements/SoGLLazyElement.h> // for GL.h, whose location is distribution specific under system/ or sys/
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/projectors/SbSphereSheetProjector.h>
#include <Inventor/SoDB.h>

#ifdef TGS_VERSION
#include <Inventor/events/SoMouseWheelEvent.h>
#include <Inventor/sys/SoGL.h>
#endif

#if defined (TGS_VERSION) && (SO_INVENTOR_VERSION >= 8500)
#include <Inventor/devices/SoGLContext.h>
#define SOGLCONTEXT_CREATE(ctx) if (!ctx) { \
		HDC hDC = ::wglGetCurrentDC(); \
		PIXELFORMATDESCRIPTOR pfd; \
		::DescribePixelFormat(hDC, ::GetPixelFormat(hDC), sizeof(PIXELFORMATDESCRIPTOR), &pfd); \
		ctx = new SoGLContext(hDC, &pfd, 0, ::wglGetCurrentContext()); }
#define SOGLCONTEXT_UNREF(ctx) if (ctx) ctx->unref()
#define SOGLCONTEXT_BIND(ctx) if (ctx) ctx->bind()
#else
class SoGLContext;
#define SOGLCONTEXT_CREATE(ctx) 
#define SOGLCONTEXT_UNREF(ctx) 
#define SOGLCONTEXT_BIND(ctx) 
#endif



#include "PySceneManager.h"

#pragma warning ( disable : 4127 ) // conditional expression is constant in Py_DECREF
#pragma warning ( disable : 4244 ) // possible loss of data when converting int to short in SbVec2s



PyTypeObject *PySceneManager::getType()
{
	static PyMemberDef members[] = 
	{
		{"scene", T_OBJECT_EX, offsetof(Object, scene), 0, "scene graph"},
		{"redisplay", T_OBJECT_EX, offsetof(Object, renderCallback), 0, "render callback"},
		{"background", T_OBJECT_EX, offsetof(Object, backgroundColor), 0, "background color"},
		{NULL}  /* Sentinel */
	};

	static PyMethodDef methods[] = 
	{
		{"render", (PyCFunction) render, METH_NOARGS, "Renders the scene into an OpenGL context" },
		{"resize", (PyCFunction) resize, METH_VARARGS, "Sets the window size" },
		{"mouse_button", (PyCFunction) mouse_button, METH_VARARGS, "Sends mouse button event into the scene for processing" },
		{"mouse_move", (PyCFunction) mouse_move, METH_VARARGS, "Sends mouse move event into the scene for processing" },
		{"key", (PyCFunction) key, METH_VARARGS, "Sends keyboard event into the scene for processing" },
		{"view_all", (PyCFunction) view_all, METH_VARARGS, "Initializes camera so that the entire scene is visible" },
		{"interaction", (PyCFunction) interaction, METH_VARARGS, "Sets the mouse interaction mode (0 = SCENE, 1 = CAMERA)" },
		{NULL}  /* Sentinel */
	};

	static PyTypeObject managerType = 
	{
		PyVarObject_HEAD_INIT(NULL, 0)
		"SceneManager",            /* tp_name */
		sizeof(Object),            /* tp_basicsize */
		0,                         /* tp_itemsize */
		(destructor) tp_dealloc,   /* tp_dealloc */
		0,                         /* tp_print */
		0,                         /* tp_getattr */
		0,                         /* tp_setattr */
		0,                         /* tp_reserved */
		0,                         /* tp_repr */
		0,                         /* tp_as_number */
		0,                         /* tp_as_sequence */
		0,                         /* tp_as_mapping */
		0,                         /* tp_hash  */
		0,                         /* tp_call */
		0,                         /* tp_str */
		0,                         /* tp_getattro */
		(setattrofunc)tp_setattro, /* tp_setattro */
		0,                         /* tp_as_buffer */
		Py_TPFLAGS_DEFAULT |
			Py_TPFLAGS_BASETYPE,   /* tp_flags */
		"Scene manager object",    /* tp_doc */
		0,                         /* tp_traverse */
		0,                         /* tp_clear */
		0,                         /* tp_richcompare */
		0,                         /* tp_weaklistoffset */
		0,                         /* tp_iter */
		0,                         /* tp_iternext */
		methods,                   /* tp_methods */
		members,                   /* tp_members */
		0,                         /* tp_getset */
		0,                         /* tp_base */
		0,                         /* tp_dict */
		0,                         /* tp_descr_get */
		0,                         /* tp_descr_set */
		0,                         /* tp_dictoffset */
		(initproc) tp_init,        /* tp_init */
		0,                         /* tp_alloc */
		tp_new,                    /* tp_new */
	};

	return &managerType;
}


void PySceneManager::tp_dealloc(Object* self)
{
	if (self->sceneManager)
	{
		delete self->sceneManager;
		self->sceneManager = 0;
	}

	if (self->sphereSheetProjector)
	{
		delete self->sphereSheetProjector;
		self->sphereSheetProjector = 0;
	}

	SOGLCONTEXT_UNREF(self->context);

	Py_XDECREF(self->scene);
	Py_XDECREF(self->renderCallback);

    Py_TYPE(self)->tp_free((PyObject*)self);
}


PyObject* PySceneManager::tp_new(PyTypeObject *type, PyObject* /*args*/, PyObject* /*kwds*/)
{
	PySceneObject::initSoDB();

    Object *self = (Object *)type->tp_alloc(type, 0);
    if (self != NULL) 
	{
		self->scene = 0;
		self->renderCallback = 0;
		self->sceneManager = 0;
		self->sphereSheetProjector = 0;
		self->context = 0;
		self->manipMode = Object::SCENE;
		self->isManipulating = false;
	}

    return (PyObject *) self;
}


int PySceneManager::tp_init(Object *self, PyObject *args, PyObject *kwds)
{
	PyObject *background = 0;
	static char *kwlist[] = { "background", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &background))
        return -1;

	self->scene = PySceneObject::createWrapper("Separator");
	self->sceneManager = new SoSceneManager();
	if (self->scene && self->sceneManager)
	{
		self->sceneManager->setSceneGraph((SoNode*) ((PySceneObject::Object*) self->scene)->inventorObject);
	}
	self->sceneManager->setRenderCallback(renderCBFunc, self);
	self->sceneManager->activate();

	self->sphereSheetProjector = new SbSphereSheetProjector();
	if (self->sphereSheetProjector)
	{
		SbViewVolume viewVolume;
		viewVolume.ortho(-1, 1, -1, 1, -1, 1);
		self->sphereSheetProjector->setViewVolume(viewVolume);
		self->sphereSheetProjector->setSphere(SbSphere(SbVec3f(0, 0, 0), .7f));
	}

	Py_INCREF(Py_None);
	self->renderCallback = Py_None;

    // configure background color
	if (background && PySequence_Check(background))
	{
		PyObject *seq = PySequence_Fast(background, "expected a sequence");
		size_t n = PySequence_Size(seq);
        if (n == 3)
        {
            SbColor color(0, 0, 0);
            for (int i = 0; i < 3; ++i)
            {
                PyObject *seqItem = PySequence_GetItem(seq, i);
                if (seqItem)
                {
                    if (PyFloat_Check(seqItem))
                        color[i] = (float) PyFloat_AsDouble(seqItem);
                    else if (PyLong_Check(seqItem))
                        color[i] = (float) PyLong_AsDouble(seqItem);
                }
            }
            if (self->sceneManager)
                self->sceneManager->setBackgroundColor(color);
        }
        
		Py_XDECREF(seq);
	}
    
	return 0;
}


int PySceneManager::tp_setattro(Object* self, PyObject *attrname, PyObject *value)
{
	int result = 0;

	result = PyObject_GenericSetAttr((PyObject*) self, attrname, value);

	const char *attr = PyUnicode_AsUTF8(attrname);
	if (attr && (strcmp(attr, "scene") == 0))
	{
		if (PyNode_Check(self->scene))
		{
			SoFieldContainer *fc = ((PySceneObject::Object*) self->scene)->inventorObject;
			if (fc && fc->isOfType(SoNode::getClassTypeId()))
			{
				self->sceneManager->setSceneGraph((SoNode*) fc);
				self->sceneManager->scheduleRedraw();
			}
			else
			{
				PyErr_SetString(PyExc_IndexError, "Scene object must be of type SoNode");
			}
		}
		else
		{
			PyErr_SetString(PyExc_IndexError, "Scene must be of type Node");
		}
	}
	else if (attr && (strcmp(attr, "background") == 0))
	{
        SoSFColor tmp;
        tmp.setValue(SbColor(0, 0, 0));
        if (PySceneObject::setField(&tmp, value) == 0)
        {
            self->sceneManager->setBackgroundColor(tmp.getValue());
            self->sceneManager->scheduleRedraw();
        }
    }

	return result;
}


void PySceneManager::renderCBFunc(void *userdata, SoSceneManager * /*mgr*/)
{
	Object *self = (Object *) userdata;
	if ((self != NULL) && (self->renderCallback != NULL) && PyCallable_Check(self->renderCallback))
	{
		PyObject *value = PyObject_CallObject(self->renderCallback, NULL);
		if (value != NULL)
		{
			Py_DECREF(value);
		}
	}
}


PyObject* PySceneManager::render(Object *self)
{
	SOGLCONTEXT_CREATE(self->context);
	SOGLCONTEXT_BIND(self->context);

	self->sceneManager->render();
    
    // need to flush or nothing will be shown on OS X
    glFlush();

    Py_INCREF(Py_None);
    return Py_None;
}


PyObject* PySceneManager::resize(Object *self, PyObject *args)
{
    int width = 0, height = 0;
    if (PyArg_ParseTuple(args, "ii", &width, &height))
	{
        // for Coin both setWindowSize() and setSize() must be called
        // in order to get correct rendering and event handling
		self->sceneManager->setWindowSize(SbVec2s(width, height));
        self->sceneManager->setSize(SbVec2s(width, height));
	}

    Py_INCREF(Py_None);
    return Py_None;
}


SoCamera *PySceneManager::getCamera(Object *self)
{
	SoCamera *camera = 0;
	if (self && self->sceneManager)
	{
		SoSearchAction search;
		search.setType(SoCamera::getClassTypeId());
		search.setInterest(SoSearchAction::FIRST);
		search.apply(self->sceneManager->getSceneGraph());

		if (search.getPath())
		{
			camera = (SoCamera*) search.getPath()->getTail();
		}
	}

	return camera;
}


void PySceneManager::rotateCamera(SoCamera *camera, SbRotation orient)
{
	if (camera)
	{
		SbVec3f dir;
		camera->orientation.getValue().multVec(SbVec3f(0.f, 0.f, -1.f), dir);
		SbVec3f center = camera->position.getValue() + dir * camera->focalDistance.getValue();
		camera->orientation.setValue(orient * camera->orientation.getValue());
		camera->orientation.getValue().multVec(SbVec3f(0.f, 0.f, -1.f), dir);
		camera->position.setValue(center - dir * camera->focalDistance.getValue());
	}
}


void PySceneManager::processEvent(Object *self, SoEvent *e)
{
	SbVec2f normalizedPosition = e->getNormalizedPosition(SbViewportRegion(self->sceneManager->getSize()));

	switch (self->manipMode)
	{
	case Object::CAMERA:
		{
			if (self->sphereSheetProjector)
			{
				if (SO_MOUSE_PRESS_EVENT(e, BUTTON1))
				{
					self->sphereSheetProjector->project(normalizedPosition);
					self->isManipulating = true;
				}
				else if (SO_MOUSE_RELEASE_EVENT(e, BUTTON1))
				{
					self->isManipulating = false;
				}
				else if (e->isOfType(SoLocation2Event::getClassTypeId()))
				{
					if (self->isManipulating)
					{
						SbRotation rot;
						self->sphereSheetProjector->projectAndGetRotation(normalizedPosition, rot);
						rot.invert();
						rotateCamera(getCamera(self), rot);
					}
				}
			}
		} break;

	default:
		{
			if (self->sceneManager->processEvent(e))
			{
				SoDB::getSensorManager()->processDelayQueue(FALSE);
			}
		}
	}
}


PyObject* PySceneManager::mouse_button(Object *self, PyObject *args)
{
    int button = 0, state = 0, x = 0, y = 0;
    if (PyArg_ParseTuple(args, "iiii", &button, &state, &x, &y))
	{
		y = self->sceneManager->getWindowSize()[1] - y;

        if (button > 2)
		{
			// buttons 3 and 4 mean mouse wheel
			if (self->manipMode == Object::SCENE)
			{
				#ifdef TGS_VERSION
				// Coin does not have wheel event (yet)
				SoMouseWheelEvent ev;
				ev.setTime(SbTime::getTimeOfDay());
				ev.setDelta(button == 3 ? -120 : 120);
				processEvent(self, &ev);
		        #endif
			}
			else
			{
				SoCamera *camera = getCamera(self);
				if (camera)
				{
					camera->scaleHeight(button == 3 ? 0.9f : 1.f / 0.9f);
				}
			}
		}
		else
		{
			SoMouseButtonEvent ev;
			ev.setTime(SbTime::getTimeOfDay());
			ev.setPosition(SbVec2s(x, y));
			ev.setButton((SoMouseButtonEvent::Button) (button + 1));
			ev.setState(state ? SoMouseButtonEvent::UP : SoMouseButtonEvent::DOWN);
			processEvent(self, &ev);
		}
	}

    Py_INCREF(Py_None);
    return Py_None;
}


PyObject* PySceneManager::mouse_move(Object *self, PyObject *args)
{
    int x = 0, y = 0;
    if (PyArg_ParseTuple(args, "ii", &x, &y))
	{
		y = self->sceneManager->getWindowSize()[1] - y;

		SoLocation2Event ev;
		ev.setTime(SbTime::getTimeOfDay());
		ev.setPosition(SbVec2s(x, y));
		processEvent(self, &ev);
	}

    Py_INCREF(Py_None);
    return Py_None;
}


PyObject* PySceneManager::key(Object *self, PyObject *args)
{
    char key;
    if (PyArg_ParseTuple(args, "c", &key))
	{
		bool mapped = true;
		SoKeyboardEvent ev;
		ev.setTime(SbTime::getTimeOfDay());

		if ((key >= 'a') && (key <= 'z'))
		{
			ev.setKey(SoKeyboardEvent::Key(SoKeyboardEvent::A + (key - 'a')));
		}
		else if ((key >= 'A') && (key <= 'Z'))
		{
			ev.setKey(SoKeyboardEvent::Key(SoKeyboardEvent::A + (key - 'Z')));
			ev.setShiftDown(TRUE);
		}
		else switch (key)
		{
		case '\n':
		case '\r':		ev.setKey(SoKeyboardEvent::RETURN); break;
		case '\t':		ev.setKey(SoKeyboardEvent::TAB); break;
		case ' ':		ev.setKey(SoKeyboardEvent::SPACE); break;
		case ',':		ev.setKey(SoKeyboardEvent::COMMA); break;
		case '.':		ev.setKey(SoKeyboardEvent::PERIOD); break;
		case '=':		ev.setKey(SoKeyboardEvent::EQUAL); break;
		case '-':		ev.setKey(SoKeyboardEvent::PAD_SUBTRACT); break;
		case '+':		ev.setKey(SoKeyboardEvent::PAD_ADD); break;
		case '/':		ev.setKey(SoKeyboardEvent::PAD_DIVIDE); break;
		case '*':		ev.setKey(SoKeyboardEvent::PAD_MULTIPLY); break;
		case '\x1b':	ev.setKey(SoKeyboardEvent::ESCAPE); break;
		case '\x08':	ev.setKey(SoKeyboardEvent::BACKSPACE); break;
		default:
			mapped = false;
		}

		if (mapped)
		{
			ev.setState(SoButtonEvent::DOWN);
			SbBool processed = self->sceneManager->processEvent(&ev);
			ev.setState(SoButtonEvent::UP);
			processEvent(self, &ev);
			processed |= self->sceneManager->processEvent(&ev);
			if (processed)
			{
				SoDB::getSensorManager()->processDelayQueue(FALSE);
			}
		}
	}

    Py_INCREF(Py_None);
    return Py_None;
}


PyObject* PySceneManager::view_all(Object *self, PyObject *args)
{
	long ok = 0;

	PyObject *applyTo = NULL;
	if (PyArg_ParseTuple(args, "|O", &applyTo))
	{
		if (!applyTo || PyNode_Check(applyTo))
		{
			SoNode *applyToNode = self->sceneManager->getSceneGraph();

			PySceneObject::Object *sceneObj = (PySceneObject::Object *)	applyTo;
			if (sceneObj && sceneObj->inventorObject)
			{
				applyToNode = (SoNode*) sceneObj->inventorObject;
			}

			SoSearchAction sa;
			sa.setType(SoCamera::getClassTypeId());
			sa.setInterest(SoSearchAction::FIRST);
			sa.apply(applyToNode);
			if (sa.getPath())
			{
				SbViewportRegion vp(512, 512);
				((SoCamera*) sa.getPath()->getTail())->viewAll(applyToNode, vp);
				ok = 1;
			}
		}
	}

	return PyBool_FromLong(ok);
}


PyObject* PySceneManager::interaction(Object *self, PyObject *args)
{
    int mode = 0;
    if (PyArg_ParseTuple(args, "i", &mode))
	{
		switch (mode)
		{
		case Object::CAMERA: self->manipMode = Object::CAMERA; break;
		default:
			self->manipMode = Object::SCENE;
		}
	}

    Py_INCREF(Py_None);
    return Py_None;
}


bool PySceneManager::getScene(PyObject* self, PyObject *&scene_out, int &viewportWidth_out, int &viewportHeight_out)
{
	if (PyObject_TypeCheck(self, PySceneManager::getType()))
	{
		PySceneManager::Object *sm = (PySceneManager::Object *) self;

		if (sm->scene && sm->sceneManager)
		{
			scene_out = sm->scene;
			SbVec2s size = sm->sceneManager->getViewportRegion().getViewportSizePixels();
			viewportWidth_out = size[0];
			viewportHeight_out = size[0];

			return true;
		}
	}

	return false;
}
