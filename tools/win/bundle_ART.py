#!/usr/bin/env python3

"""
Helper script to generate an ART self-contained "bundle" directory,
with all the required dependencies (windows version)
"""

import os, sys
import shutil
import subprocess
import argparse
import tarfile, zipfile
import tempfile
import io
from urllib.request import urlopen


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--outdir', required=True,
                   help='output directory for the bundle')
    p.add_argument('-m', '--msys', default='c:/msys64',
                   help='msys installation directory (default: c:/msys64)')
    p.add_argument('-e', '--exiftool',
                   help='path to exiftool.exe (default: search in PATH)')
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-E', '--exiftool-download', action='store_true')
    p.add_argument('-i', '--imageio', help='path to imageio plugins')
    p.add_argument('-b', '--imageio-bin', help='path to imageio binaries')
    p.add_argument('-I', '--imageio-download', action='store_true')
    ret = p.parse_args()
    if ret.exiftool is None and not ret.exiftool_download:
        ret.exiftool = shutil.which('exiftool')
    if ret.msys.endswith('/'):
        ret.msys = ret.msys[:-1]
    return ret


def getdlls(opts):
    res = []
    d = os.getcwd()
    p = subprocess.Popen(['ldd', os.path.join(d, 'ART.exe')],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines():
        if ' => ' in line:
            bits = line.split()
            lib = bits[2]
            if not lib.lower().startswith('/c/windows/'):
                res.append(opts.msys + lib)
    return res


def extra_files(opts):
    def D(s): return opts.msys + '/' + s
    exiftool = []
    extra = []
    if not opts.exiftool and opts.exiftool_download:
        with urlopen('https://exiftool.org/ver.txt') as f:
            ver = f.read().strip().decode('utf-8')
        name = 'exiftool-%s_64' % ver
        with urlopen('https://exiftool.org/' + name + '.zip') as f:
            if opts.verbose:
                print('downloading %s.zip from https://exiftool.org ...' % name)
            zf = zipfile.ZipFile(io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking %s.zip ...' % name)
            zf.extractall(opts.tempdir)
            opts.exiftool = os.path.join(opts.tempdir, name, 'exiftool(-k).exe')
    if opts.exiftool:
        exiftool.append((opts.exiftool, 'exiftool.exe'))
        exiftool_files = os.path.join(os.path.dirname(opts.exiftool),
                                      'exiftool_files')
        if os.path.isdir(exiftool_files):
            exiftool.append((exiftool_files, 'exiftool_files'))
    if opts.imageio:
        extra.append(('.', [(opts.imageio, 'imageio')]))
    elif opts.imageio_download:
        with urlopen('https://bitbucket.org/agriggio/art-imageio/'
                     'downloads/ART-imageio.tar.gz') as f:
            if opts.verbose:
                print('downloading ART-imageio.tar.gz '
                      'from https://bitbucket.org ...')
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking ART-imageio.tar.gz ...')
            tf.extractall(opts.tempdir)
        extra.append(('.', [(os.path.join(opts.tempdir, 'ART-imageio'),
                             'imageio')]))
    if opts.imageio_bin:
        extra.append(('imageio', [(opts.imageio_bin, 'bin')]))            
    elif opts.imageio_download:
        with urlopen('https://bitbucket.org/agriggio/art-imageio/'
                     'downloads/ART-imageio-bin-win64.tar.gz') as f:
            if opts.verbose:
                print('downloading ART-imageio-bin-win64.tar.gz '
                      'from http://bitbucket.org ...')
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking ART-imageio-bin-win64.tar.gz ...')
            tf.extractall(opts.tempdir)
        extra.append(('imageio',
                      [(os.path.join(opts.tempdir, 'ART-imageio-bin-win64'),
                        'bin')]))
    return [
        ('.', [
            D('mingw64/bin/gdbus.exe'),
            D('mingw64/bin/gspawn-win64-helper.exe'),
            D('mingw64/bin/gspawn-win64-helper-console.exe')
        ] + exiftool),
        ('share/icons/Adwaita', [
            D('mingw64/share/icons/Adwaita/scalable'),
            D('mingw64/share/icons/Adwaita/index.theme'), 
            D('mingw64/share/icons/Adwaita/cursors'),
        ]),
        ('lib', [
            D('mingw64/lib/gdk-pixbuf-2.0'),
        ]),
        ('share/glib-2.0/schemas', [
            D('mingw64/share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('share', [
            (D('mingw64/var/lib/lensfun-updates/version_1'), 'lensfun'),
        ]),
    ] + extra


def main():
    opts = getopts()
    d = os.getcwd()
    if not os.path.exists('ART.exe'):
        sys.stderr.write('ERROR: ART.exe not found! Please run this script '
                         'from the build directory of ART\n')
        sys.exit(1)
    if opts.verbose:
        print('copying %s to %s' % (os.getcwd(), opts.outdir))
    shutil.copytree(d, opts.outdir)
    for lib in getdlls(opts):
        if opts.verbose:
            print('copying: %s' % lib)
        shutil.copy2(lib,
                     os.path.join(opts.outdir, os.path.basename(lib)))
    with tempfile.TemporaryDirectory() as d:
        opts.tempdir = d
        for key, elems in extra_files(opts):
            for elem in elems:
                name = None
                if isinstance(elem, tuple):
                    elem, name = elem
                else:
                    name = os.path.basename(elem)
                if opts.verbose:
                    print('copying: %s' % elem)
                if os.path.isdir(elem):
                    shutil.copytree(elem, os.path.join(opts.outdir, key, name))
                else:
                    dest = os.path.join(opts.outdir, key, name)
                    destdir = os.path.dirname(dest)
                    if not os.path.exists(destdir):
                        os.makedirs(destdir)
                    shutil.copy2(elem, dest)
    os.makedirs(os.path.join(opts.outdir, 'share/gtk-3.0'))
    with open(os.path.join(opts.outdir, 'share/gtk-3.0/settings.ini'), 'w') \
         as out:
        out.write('[Settings]\ngtk-button-images=1\n')


if __name__ == '__main__':
    main()
