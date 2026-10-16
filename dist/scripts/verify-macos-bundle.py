#!/usr/bin/env python3
"""Verify an extracted Qt bundle has no developer-machine dependencies."""
from pathlib import Path
import argparse
import subprocess
import sys

def verify(app, sanitize=False):
    app=Path(app).resolve()
    main=app/'Contents/MacOS/Fovelle'
    frameworks=app/'Contents/Frameworks'
    plugins=app/'Contents/PlugIns/platforms/libqcocoa.dylib'
    if not main.is_file() or not plugins.is_file():
        raise ValueError('Missing executable or Cocoa platform plugin')
    for framework in ('QtCore','QtGui','QtWidgets','QtNetwork','QtSvg','QtConcurrent'):
        if not (frameworks/(framework+'.framework')).is_dir(): raise ValueError('Missing '+framework)
    macho_magic={bytes.fromhex(x) for x in ('feedface','cefaedfe','feedfacf','cffaedfe','cafebabe','bebafeca','cafebabf','bfbafeca')}
    for path in app.rglob('*'):
        if not path.is_file() or path.is_symlink(): continue
        with path.open('rb') as binary:
            if binary.read(4) not in macho_magic: continue
        kind=subprocess.run(['file','-b',str(path)],capture_output=True,text=True,check=True).stdout
        if 'Mach-O' not in kind: continue
        executable_base=path.parent if 'executable' in kind else main.parent
        if path.is_relative_to(app/'Contents/Resources/ghostscript'):
            executable_base=app/'Contents/Resources/ghostscript/bin'
        linked=subprocess.run(['otool','-L',str(path)],capture_output=True,text=True,check=True).stdout
        identity=subprocess.run(['otool','-D',str(path)],capture_output=True,text=True,check=True).stdout.splitlines()[1:]
        identities={line.strip() for line in identity}
        if sanitize:
            for own_id in identities:
                if own_id.startswith('/') and not own_id.startswith(('/System/Library/','/usr/lib/')):
                    subprocess.run(['install_name_tool','-id','@rpath/'+path.name,str(path)],check=True)
        dependencies=[]
        for line in linked.splitlines():
            if not line[:1].isspace(): continue
            dependency=line.strip().split(' (')[0]
            if dependency in identities: continue  # LC_ID_DYLIB names this image; it is not a load command.
            dependencies.append(dependency)
            if dependency.startswith('/') and not dependency.startswith(('/System/Library/','/usr/lib/')):
                raise ValueError(f'External dependency: {path.name}: {dependency}')
        # LC_RPATH must also not point to the build machine.
        load=subprocess.run(['otool','-l',str(path)],capture_output=True,text=True,check=True).stdout
        lines=load.splitlines()
        rpaths=[str(frameworks), '/usr/lib/swift']
        removed_rpaths=set()
        for i,line in enumerate(lines):
            if line.strip()=='cmd LC_RPATH':
                rpath=lines[i+2].strip().removeprefix('path ').split(' (offset ',1)[0]
                rpaths.append(rpath.replace('@loader_path',str(path.parent)).replace('@executable_path',str(executable_base)))
                if rpath.startswith('/') and not rpath.startswith(('/System/Library/','/usr/lib/')):
                    if sanitize:
                        if rpath not in removed_rpaths:
                            subprocess.run(['install_name_tool','-delete_rpath',rpath,str(path)],check=True)
                            removed_rpaths.add(rpath)
                    else:
                        raise ValueError(f'External rpath: {path.name}: {rpath}')
        for dependency in dependencies:
            if dependency.startswith('@rpath/'):
                tail=dependency[len('@rpath/'):]
                if not any((Path(root)/tail).exists() for root in rpaths):
                    raise ValueError(f'Unresolved dependency: {path.name}: {dependency}')
            elif dependency.startswith(('@loader_path/','@executable_path/')):
                resolved=dependency.replace('@loader_path',str(path.parent)).replace('@executable_path',str(executable_base))
                if not Path(resolved).exists(): raise ValueError('Unresolved dependency: '+dependency)
            elif not dependency.startswith('/'):
                raise ValueError('Unsupported relative dependency: '+dependency)
    return True

def sign_adhoc(app):
    """Resign code outside Apple's recognized nested-code locations as well."""
    app=Path(app).resolve()
    magic={bytes.fromhex(x) for x in ('feedface','cefaedfe','feedfacf','cffaedfe','cafebabe','bebafeca','cafebabf','bfbafeca')}
    for path in (app/'Contents/Resources').rglob('*'):
        if not path.is_file() or path.is_symlink(): continue
        with path.open('rb') as binary:
            if binary.read(4) not in magic: continue
        kind=subprocess.run(['file','-b',str(path)],capture_output=True,text=True,check=True).stdout
        if 'Mach-O' in kind:
            subprocess.run(['codesign','--force','--sign','-',str(path)],check=True,timeout=30)
    subprocess.run(['codesign','--force','--deep','--sign','-',str(app)],check=True,timeout=60)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('app'); parser.add_argument('--sanitize',action='store_true'); parser.add_argument('--sign-adhoc',action='store_true'); args=parser.parse_args()
    try:
        verify(args.app,args.sanitize)
        if args.sign_adhoc: sign_adhoc(args.app)
    except (ValueError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);raise SystemExit(1)
    print('Bundle dependencies and Cocoa plugin verified')
