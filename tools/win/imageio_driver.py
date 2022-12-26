#!/usr/bin/env python

"""
windows driver to build a stand-alone python executable for the
ART-imageio plugins with PyInstaller
"""

# import all the required modules
import Imath
import OpenEXR
import PIL
import argparse
import contextlib
import numpy
import os
import pillow_heif
import re
import struct
import subprocess
import sys
import tempfile
import tifffile
import time
import webp


if len(sys.argv) < 2:
    print('no script specified')
else:
    script = sys.argv[1]
    sys.argv = sys.argv[1:]
    with open(script) as f:
        code = f.read()
    exec(code)
