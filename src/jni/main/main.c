#include "SDL.h"
#include "Python.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <jni.h>
#include "android/log.h"
#include "jniwrapperstuff.h"

SDL_Window *window = NULL;


#define LOG(x) __android_log_write(ANDROID_LOG_INFO, "python", (x))

static PyObject *androidembed_log(PyObject *self, PyObject *args) {
    char *logstr = NULL;
    if (!PyArg_ParseTuple(args, "s", &logstr)) {
        return NULL;
    }
    LOG(logstr);
    Py_RETURN_NONE;
}

static PyObject *androidembed_close_window(PyObject *self, PyObject *args) {
    char *logstr = NULL;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    if (window) {
		SDL_DestroyWindow(window);
		window = NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef AndroidEmbedMethods[] = {
	    {"log", androidembed_log, METH_VARARGS, "Log on android platform."},
	    {"close_window", androidembed_close_window, METH_VARARGS, "Close the initial window."},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initandroidembed(void) {
    (void) Py_InitModule("androidembed", AndroidEmbedMethods);
}

int file_exists(const char * filename) {
	FILE *file;
    if (file = fopen(filename, "r")) {
        fclose(file);
        return 1;
    }
    return 0;
}


int start_python(void) {
    char *env_argument = NULL;
    int ret = 0;
    FILE *fd;
    char *args[] = { "python", NULL };

    LOG("Initialize Python for Android");

    env_argument = getenv("ANDROID_ARGUMENT");
    setenv("ANDROID_APP_PATH", env_argument, 1);

    //setenv("PYTHONVERBOSE", "2", 1);
    Py_SetProgramName(args[0]);
    Py_Initialize();
    PySys_SetArgvEx(1, args, 0);

    /* ensure threads will work.
     */
    PyEval_InitThreads();

    /* our logging module for android
     */
    initandroidembed();

    /* inject our bootstrap code to redirect python stdin/stdout
     * replace sys.path with our path
     */
    PyRun_SimpleString(
        "import sys, posix\n" \
        "private = posix.environ['ANDROID_PRIVATE']\n" \
        "argument = posix.environ['ANDROID_ARGUMENT']\n" \
        "sys.path[:] = [ \n" \
		"    argument, \n" \
		"    private + '/lib/python27.zip', \n" \
		"    private + '/lib/python2.7/', \n" \
		"    private + '/lib/python2.7/lib-dynload/', \n" \
		"    private + '/lib/python2.7/site-packages/', \n" \
		"    ]\n" \
        "import androidembed\n" \
        "class LogFile(object):\n" \
        "    def __init__(self):\n" \
        "        self.buffer = ''\n" \
        "    def write(self, s):\n" \
        "        s = self.buffer + s\n" \
        "        lines = s.split(\"\\n\")\n" \
        "        for l in lines[:-1]:\n" \
        "            androidembed.log(l)\n" \
        "        self.buffer = lines[-1]\n" \
        "    def flush(self):\n" \
        "        return\n" \
        "sys.stdout = sys.stderr = LogFile()\n" \
		"import site; print site.getsitepackages()\n"\
		"print '3...'\n"\
		"print '2...'\n"\
		"print '1...'\n"\
		"print 'Android path', sys.path\n" \
        "print 'Android bootstrap done. __name__ is', __name__\n"\
		"import pygame_sdl2\n"\
		"pygame_sdl2.import_as_pygame()\n"\
    	"");

    /* run it !
     */
    LOG("Run user program, change dir and execute main.py");
    chdir(env_argument);

	/* search the initial main.py
	 */
	char *main_py = "main.pyo";
	if ( file_exists(main_py) == 0 ) {
		if ( file_exists("main.py") )
			main_py = "main.py";
		else
			main_py = NULL;
	}

	if ( main_py == NULL ) {
		LOG("No main.pyo / main.py found.");
		return 1;
	}

    fd = fopen(main_py, "r");
    if ( fd == NULL ) {
        LOG("Open the main.py(o) failed");
        return 1;
    }

    /* run python !
     */
    ret = PyRun_SimpleFile(fd, main_py);

    if (PyErr_Occurred() != NULL) {
        ret = 1;
        PyErr_Print(); /* This exits with the right code if SystemExit. */
        if (Py_FlushLine())
			PyErr_Clear();
    }

    /* close everything
     */
	Py_Finalize();
    fclose(fd);

    LOG("Python for android ended.");
    return ret;
}


JNIEXPORT void JNICALL JAVA_EXPORT_NAME(PythonSDLActivity_nativeSetEnv) (
		JNIEnv*  env, jobject thiz,
		jstring variable,
		jstring value) {

	jboolean iscopy;
    const char *c_variable = (*env)->GetStringUTFChars(env, variable, &iscopy);
    const char *c_value  = (*env)->GetStringUTFChars(env, value, &iscopy);
    setenv(c_variable, c_value, 1);
}

void call_prepare_python(void) {
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jobject activity = (jobject) SDL_AndroidGetActivity();
	jclass clazz = (*env)->GetObjectClass(env, activity);
	jmethodID method_id = (*env)->GetMethodID(env, clazz, "preparePython", "()V");
	(*env)->CallVoidMethod(env, activity, method_id);
	(*env)->DeleteLocalRef(env, activity);
	(*env)->DeleteLocalRef(env, clazz);
}

int SDL_main(int argc, char **argv) {
	SDL_Surface *surface;
	SDL_Event event;

	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		return 1;
	}

	window = SDL_CreateWindow("pygame_sdl2 starting...", 0, 0, 800, 600, SDL_WINDOW_SHOWN);
	surface = SDL_GetWindowSurface(window);

	SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 255));
	SDL_UpdateWindowSurface(window);

	call_prepare_python();

	return start_python();
}