#!/usr/bin/env python3

import os, sys
import subprocess
import argparse
import json
import numpy
import re
from collections import OrderedDict as odict
import tempfile
import shutil


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('--dcamprof-path', default='.')
    p.add_argument('--use-camconst', help='if set, use the D65 color matrix in camconst.json (via Bradford adaptation)')
    p.add_argument('--argyll-path', default='/usr/share/color/argyll')
    p.add_argument('--argyll-prefix', default='')
    p.add_argument('-t', '--tungsten', help='tungsten shot')
    p.add_argument('-d', '--daylight', help='daylight shot')
    p.add_argument('-1', '--target-1', help='target 1 shot')
    p.add_argument('-2', '--target-2', help='target 2 shot')
    p.add_argument('--illuminant-1', default='D50')
    p.add_argument('--illuminant-2', default='D50')
    p.add_argument('-n', '--name', help='camera name to embed')
    p.add_argument('-o', '--output', help='output file')
    p.add_argument('-O', '--outdir', help='output dir')
    p.add_argument('-T', '--tone', default='standard', help='tone curve')
    p.add_argument('-c', '--curve', default='linear', help='tone curve type')
    p.add_argument('-m', '--matrix', action='store_true')
    p.add_argument('--limit-blue', type=float)
    p.add_argument('-M', '--update-matrix',
                   help='store the automatically computed dcraw matrix '
                   'in the given json file')
    p.add_argument('-C', '--constraint',
                   help='extra constraint for optimization (e.g. "-l 0.1")')
    p.add_argument('--make-dcp-flags')
    p.add_argument('--tempdir')
    p.add_argument('-s', '--skin-priority', action='store_true', default=False)
    p.add_argument('-S', '--no-skin-priority', action='store_false',
                   dest='skin_priority')
    p.add_argument('--profile-color-model',
                   choices=['relighting', 'CAT'], default='CAT')
    p.add_argument('-R', '--relighting', action='store_true')
    p.add_argument('--cie', help='path to the .cie file to use ' \
                            '(default: cc24_ref-new.cie provided by dcamprof)')
    opts = p.parse_args()
    if opts.tungsten and opts.daylight:
        opts.target_1 = opts.tungsten
        opts.target_2 = opts.daylight
        opts.illuminant_1 = 'StdA'
        opts.illuminant_2 = 'D65'
    elif opts.tungsten:
        opts.target_1 = opts.tungsten
        opts.target_2 = None
        opts.illuminant_1 = 'StdA'
    elif opts.daylight:
        opts.target_1 = opts.daylight
        opts.target_2 = None
        opts.illuminant_1 = 'D65'
    if opts.target_2 and not opts.target_1:
        raise ValueError('--target-2 specified, but no --target-1 given')
    if opts.relighting:
        opts.profile_color_model = 'relighting'
    return opts


# see http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
bradford_MA = numpy.array([
    [0.8951000,  0.2664000, -0.1614000],
    [-0.7502000,  1.7135000,  0.0367000],
    [0.0389000, -0.0685000,  1.0296000]
    ])

bradford_MA_inv = numpy.array([
    [0.9869929, -0.1470543,  0.1599627],
    [0.4323053,  0.5183603,  0.0492912],
    [-0.0085287,  0.0400428,  0.9684867]
    ])

def generate_adaptation_matrix(white1, white2):
    src = bradford_MA.dot(white1)
    dst = bradford_MA.dot(white2)
    m = numpy.array([
        [dst[0]/src[0], 0, 0],
        [0, dst[1]/src[1], 0],
        [0, 0, dst[2]/src[2]]
        ])
    res = bradford_MA_inv.dot(m).dot(bradford_MA)
    return res


def adapt_matrix(matrix, srcwhite, dstwhite):
    m = generate_adaptation_matrix(srcwhite, dstwhite)
    res = matrix.dot(m)
    return res

d50 = numpy.array([0.96422, 1.00000, 0.82521])
d55 = numpy.array([0.95682, 1.00000, 0.92149])
d60 = numpy.array([0.95265198, 1.00000000, 1.00881958])
d65 = numpy.array([0.95047, 1.00000, 1.08883])
stdA = numpy.array([1.09850, 1.00000, 0.35585])

bradford_adapt_D65_StdA = generate_adaptation_matrix(d65, stdA)
bradford_adapt_D65_D50 = generate_adaptation_matrix(d65, d50)
bradford_adapt_D50_D65 = generate_adaptation_matrix(d50, d65)


tempdir = None
metadata = None

def tmpout(name):
    return os.path.join(tempdir, name)


def get_metadata(opts):
    global metadata
    if metadata is None:
        p = subprocess.Popen(['exiftool', '-json', opts.target_1],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out, err = p.communicate()
        if err:
            raise Exception(err)
        data = json.loads(out)[0]
        try:
            make = data['Make']
            model = data['Model']
            for corp in [
                "Canon",
                "NIKON",
                "EPSON",
                "KODAK",
                "Kodak",
                "OLYMPUS",
                "PENTAX",
                "RICOH",
                "MINOLTA",
                "Minolta",
                "Konica",
                "CASIO",
                "Sinar",
                "Phase One",
                "SAMSUNG",
                "Mamiya",
                "MOTOROLA",
                "Leaf",
                "Panasonic"
                ]:
                if corp in make:
                    make = corp
                    break
            if model.upper().startswith(make.upper() + ' '):
                model = model[len(make)+1:]
            make_model = '%s %s' % (make, model)
        except:
            make_model = "UNKNOWN"
        try:
            dimensions = (data['RawImageFullWidth'], data['RawImageFullHeight'])
        except:
            dimensions = (0, 0)
        metadata = {
            'make_model' : make_model.upper(),
            'dimensions' : dimensions
            }
        opts.name = make_model.upper()

def load_camconst(f):
    jsondata = []
    ok = True
    for line in f:
        l = line.strip()
        if l.startswith('/*'):
            ok = False
        elif l.endswith('*/'):
            ok = True
        elif ok:
            jsondata.append(re.sub('//.*$', '', line))
    return json.loads("\n".join(jsondata))


def get_matrices(name, camconst):
    name = name.upper()
    print('Searching color matrix for %s in %s' % (name, camconst))
    m = None
    with open(camconst) as f:
        data = load_camconst(f)
    for entry in data['camera_constants']:
        model = entry.get('make_model', '')
        if isinstance(model, list):
            found = name in [m.upper() for m in model]
        else:
            found = model.upper() == name
        if found:
            if 'dcraw_matrix' in entry:
                m = entry['dcraw_matrix']
                print('Found: %s' % m)
                m = [float(e)/10000.0 for e in m]
                m = numpy.array(m)
                m = m.reshape(3, 3)
                break

    if m is not None:
        stda = numpy.dot(bradford_adapt_D65_StdA, m)
        return list(map(list, stda)), list(map(list, m))#d50))
    else:
        print('Not found')
        return None


def scan_targets(opts):
    scanin = opts.argyll_prefix + 'scanin'
    if shutil.which(scanin) is None and opts.argyll_path:
        scanin = os.path.join(opts.argyll_path, 'bin', scanin)
    cht = os.path.join(opts.argyll_path, 'ref', 'ColorChecker.cht')
    if opts.cie:
        cie = opts.cie
    else:
        cie = os.path.join(opts.dcamprof_path, 'data-examples',
                           'cc24_ref-new.cie')
    scaninopts = "-v -G 0.454545 -p -digIcrpn".split()
    r1 = subprocess.call([scanin] + scaninopts +
                         ['-O', tmpout('target-1.ti3'),
                          opts.target_1, cht, cie,
                          tmpout('target-1-diag.tif')])
    if opts.target_2:
        r2 = subprocess.call([scanin] + scaninopts +
                             ['-O', tmpout('target-2.ti3'),
                              opts.target_2, cht, cie,
                              tmpout('target-2-diag.tif')])
    else:
        r2 = 0
    return r1 == 0 and r2 == 0


def make_profile_json(opts):
    dcamprof = os.path.join(opts.dcamprof_path, 'dcamprof')
    extra = []
    if opts.limit_blue is not None:
        l = ['-y', str(opts.limit_blue)]
        extra += l
    if opts.constraint:
        l = opts.constraint.split()
        extra += l
    if opts.skin_priority:
        if not opts.matrix:
            l = ['-l', 'A02', '-0.1,0.1', '-l', 'A01', '-0.1,0.1']
        else:
            l = ['-w', 'A02', '2', '-w', 'A01', '2']
        extra += l
    if opts.profile_color_model == 'relighting':
        extra.append('-C')
    r1 = subprocess.call([dcamprof, 'make-profile', '-i', opts.illuminant_1,
                          '-I', 'D50', '-S'] + extra +
                         [tmpout('target-1.ti3'), tmpout('target-1.json')])
    if r1 == 0 and opts.target_2:
        extra.append('-C')
        r2 = subprocess.call([dcamprof, 'make-profile', '-i', opts.illuminant_2,
                              '-I', 'D50', '-S'] + extra +
                             [tmpout('target-2.ti3'), tmpout('target-2.json')])
    else:
        r2 = 0
    return r1 == 0 and r2 == 0


def dump_camconst(out, camconst):
    indent = ' ' * 4
    pr = out.write
    pr('{"camera_constants": [\n')
    keysep = indent
    for entry in sorted(camconst['camera_constants'],
                        key=lambda e: e['make_model']):
        pr(keysep)
        pr('{\n%s%s"make_model" : %s' % (indent, indent,
                                         json.dumps(entry['make_model'])))
        sep = ',\n%s%s' % (indent, indent)
        for key in sorted(entry.keys()):
            if key != 'make_model':
                pr('%s"%s" : %s' % (sep, key, json.dumps(entry[key])))
        pr('\n%s}' % indent)
        keysep = ',\n%s' % indent
    pr('\n]}\n')


def replace_color_matrices(opts):
    found = False
    if opts.use_camconst:
        matrices = get_matrices(opts.name, opts.use_camconst)
        if matrices is not None:
            found = True
            f = ['target-1.json', 'target-2.json'] \
                if opts.target_2 else ['target-1.json']
            for i in range(2):
                with open(tmpout(f[i])) as src:
                    data = json.load(src)
                if 'ColorMatrix1' in data:
                    old = data['ColorMatrix1']
                    print('Replacing color matrix %s in %s with %s' % \
                          (old, f[i], matrices[i]))
                    data['ColorMatrix1'] = matrices[i]
                    with open(f[i], 'w') as out:
                        json.dump(data, out)
    elif opts.update_matrix:
        with open(opts.update_matrix) as src:
            camconst = load_camconst(src)
        with open(tmpout('daylight.json')) as src:
            data = json.load(src)
            if 'ColorMatrix1' in data:
                out_m = numpy.array(data['ColorMatrix1'])
                out_m = adapt_matrix(out_m, d65, d50)
                out_m = [int(x * 10000) for x in out_m.reshape(9)]
                for entry in camconst['camera_constants']:
                    model = entry.get('make_model', '')
                    if isinstance(model, list):
                        found = opts.name in [m.upper() for m in model]
                    else:
                        found = model.upper() == opts.name
                    if found:
                        entry['dcraw_matrix'] = out_m
                        break
                if not found:
                    d = odict()
                    d['make_model'] = opts.name
                    d['dcraw_matrix'] = out_m
                    camconst['camera_constants'].append(d)
                    found = True
        if found:
            with open(opts.update_matrix, 'w') as out:
                dump_camconst(out, camconst)
            print('Updating dcraw matrix for %s: %s' % (opts.name, out_m))
    if not found:
        print('No action needed')
    return True


def make_dcp(opts):
    dcamprof = os.path.join(opts.dcamprof_path, 'dcamprof')
    if not opts.name:
        get_metadata(opts)
        name = metadata['make_model']
    else:
        name = opts.name.upper()
    if opts.output is not None:
        outname = opts.output
    else:
        outname = name + '.dcp'
        if opts.outdir is not None:
            outname = os.path.join(opts.outdir, outname)
    profile_kind = ['-t', opts.curve, '-o', opts.tone]
    if opts.matrix:
        profile_kind = ['-L']
    if opts.make_dcp_flags:
        profile_kind += opts.make_dcp_flags.split()
    if opts.target_2:
        return subprocess.call([dcamprof, 'make-dcp', '-n', name, '-d', name,
                                '-c', 'public domain'] +
                               profile_kind +
                               [tmpout('target-1.json'),
                                tmpout('target-2.json'), outname]) == 0
    else:
        return subprocess.call([dcamprof, 'make-dcp', '-n', name, '-d', name,
                                '-c', 'public domain'] +
                               profile_kind +
                               [tmpout('target-1.json'), outname]) == 0


def stage(name, func, opts):
    print('-' * 80)
    print(name)
    print('-' * 80)
    if not func(opts):
        print('Error(s) detected, aborting')
        exit(1)


def main():
    global tempdir
    opts = getopts()
    if opts.tempdir:
        tempdir = opts.tempdir
    else:
        tempdir = tempfile.mkdtemp()
    try:
        ## stage('Generating targets', generate_dpreview_targets, opts)
        stage('Scanning targets', scan_targets, opts)
        stage('Making json profiles', make_profile_json, opts)
        stage('Updating colour matrices (if needed)',
              replace_color_matrices, opts)
        stage('Generating DCP profile', make_dcp, opts)
    finally:
        if not opts.tempdir:
            shutil.rmtree(tempdir)

if __name__ == '__main__':
    main()
