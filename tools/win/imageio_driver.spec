# -*- mode: python ; coding: utf-8 -*-


block_cipher = None


a = Analysis(
    ['imageio_driver.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[
        'Imath',
        'OpenEXR',
        'PIL',
        'argparse',
        'contextlib',
        'numpy',
        'os',
        'pillow_heif',
        're',
        'struct',
        'subprocess',
        'sys',
        'tempfile',
        'tifffile',
        'time',
        'webp',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='imageio_driver',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
