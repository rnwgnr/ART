#!/usr/bin/env python3

import os, sys
import shutil
import subprocess
import argparse

def getdlls(opts):
    blacklist = {
	'linux-vdso.so.1',
	'libm.so.6',
	'libpthread.so.0',
	'libc.so.6',
	## 'libX11.so.6',
	## 'libXi.so.6',
	## 'libXfixes.so.3',
	'libfontconfig.so.1',
	## 'libXinerama.so.1',
	## 'libXrandr.so.2',
	## 'libXcursor.so.1',
	## 'libXcomposite.so.1',
	## 'libXdamage.so.1',
	## 'libxkbcommon.so.0',
	## 'libwayland-cursor.so.0',
	## 'libwayland-egl.so.1',
	## 'libwayland-client.so.0',
	## 'libXext.so.6',
	'ld-linux-x86-64.so.2',
	'libdl.so.2',
	'libfreetype.so.6',
	## 'libXrender.so.1',
	## 'libdbus-1.so.3',
	## 'libselinux.so.1',
	## 'libmount.so.1',
	## 'libXau.so.6',
	## 'libXdmcp.so.6',
	## 'libsystemd.so.0',
        'librt.so.1',
        }
    res = []
    d = os.getcwd()
    p = subprocess.Popen(['ldd', os.path.join(d, 'ART')],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines():
        if ' => ' in line:
            bits = line.split()
            lib = bits[2]
            bn = os.path.basename(lib)
            if bn not in blacklist:
                res.append(lib)
    return res

def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--outdir', required=True,
                   help='output directory for the bundle')
    p.add_argument('-e', '--exiftool',
                   help='path to exiftool.exe (default: search in PATH)')
    p.add_argument('-v', '--verbose', action='store_true')
    ret = p.parse_args()
    return ret

def extra_files(opts):
    def D(s): return os.path.expanduser(s)
    return [
        ('share/icons/Adwaita', [
            D('/usr/share/icons/Adwaita/scalable'),
            D('/usr/share/icons/Adwaita/index.theme'), 
            D('/usr/share/icons/Adwaita/cursors'),
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0'),
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/gio'),
        ]),
        ('share/glib-2.0/schemas', [
            D('/usr/share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('share', [
            (D('~/.local/share/lensfun/updates/version_2'), 'lensfun'),
        ]),
    ]


def main():
    opts = getopts()
    d = os.getcwd()
    if not os.path.exists('ART'):
        sys.stderr.write('ERROR: ART not found! Please run this script '
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
    with open(os.path.join(opts.outdir, 'options'), 'a') as out:
        out.write('\n[Lensfun]\nDBDirectory=share/lensfun\n')
    for name in ('ART', 'ART-cli'):
        shutil.move(os.path.join(opts.outdir, name),
                    os.path.join(opts.outdir, name + '.bin'))
        with open(os.path.join(opts.outdir, name), 'w') as out:
            out.write("""#!/bin/bash
    export GTK_CSD=0
    d=$(dirname $0)
    export GDK_PIXBUF_MODULEDIR="$d/lib/gdk-pixbuf-2.0"
    export GIO_MODULE_DIR="$d/lib/gio/modules"
    export LD_LIBRARY_PATH="$d"
    exec "$d/%s.bin" "$@"
    """ %  name)
        os.chmod(os.path.join(opts.outdir, name), 0o755)

if __name__ == '__main__':
    main()
