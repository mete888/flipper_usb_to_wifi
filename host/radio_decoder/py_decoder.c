#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "decoder.h"

static void release_decoder(PyObject* capsule) {
    fib_radio_decoder_free(PyCapsule_GetPointer(capsule, "fib.radio"));
}
static PyObject* allocate(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    FibRadioDecoder* decoder = fib_radio_decoder_alloc();
    if(!decoder) return PyErr_NoMemory();
    PyObject* capsule = PyCapsule_New(decoder, "fib.radio", release_decoder);
    if(!capsule) fib_radio_decoder_free(decoder);
    return capsule;
}
typedef struct { PyObject* bytes; int failed; } Output;
static void append_pcm(const uint8_t* bytes, size_t count, void* context) {
    Output* out = context;
    if(out->failed) return;
    const Py_ssize_t offset = PyByteArray_Size(out->bytes);
    if(PyByteArray_Resize(out->bytes, offset + (Py_ssize_t)count) < 0) { out->failed = 1; return; }
    memcpy(PyByteArray_AsString(out->bytes) + offset, bytes, count);
}
static PyObject* feed(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* capsule;
    Py_buffer input;
    if(!PyArg_ParseTuple(args, "Oy*", &capsule, &input)) return NULL;
    FibRadioDecoder* decoder = PyCapsule_GetPointer(capsule, "fib.radio");
    if(!decoder) { PyBuffer_Release(&input); return NULL; }
    if(input.len > 4096) {
        PyBuffer_Release(&input);
        return PyErr_Format(PyExc_ValueError, "MP3 input block exceeds 4096 bytes");
    }
    Output out = {PyByteArray_FromStringAndSize(NULL, 0), 0};
    if(out.bytes) fib_radio_decoder_feed(decoder, input.buf, (size_t)input.len, append_pcm, &out);
    PyBuffer_Release(&input);
    if(!out.bytes) return NULL;
    PyObject* result = out.failed ? NULL : PyBytes_FromStringAndSize(PyByteArray_AsString(out.bytes), PyByteArray_Size(out.bytes));
    Py_DECREF(out.bytes);
    return result;
}
static PyMethodDef methods[] = {
    {"allocate", allocate, METH_NOARGS, "Create a private streaming MP3 decoder."},
    {"feed", feed, METH_VARARGS, "Decode to mono 14493 Hz s16le."},
    {NULL, NULL, 0, NULL},
};
static struct PyModuleDef module = {PyModuleDef_HEAD_INIT, "_radio_decoder", NULL, -1, methods, NULL, NULL, NULL, NULL};
PyMODINIT_FUNC PyInit__radio_decoder(void) { return PyModule_Create(&module); }
