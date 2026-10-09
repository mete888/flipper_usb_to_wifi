from setuptools import Extension, setup

# Optional at install time: all text/Bluetooth features remain usable if a C
# compiler is absent. Release binaries include the extension; no ffmpeg needed.
setup(ext_modules=[Extension("host.fibp_host._radio_decoder",
    sources=["host/radio_decoder/decoder.c", "host/radio_decoder/py_decoder.c"],
    optional=True)])
