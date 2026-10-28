#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package the FLTK adapter and remove optional Qt bindings from matching Fedora VTK Python RPMs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import py_compile
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--python-rpm',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--build',type=Path,default=ROOT/'build-native')
a=p.parse_args();work=a.work.resolve();source=a.python_rpm.resolve();build=a.build.resolve()
if work.exists():p.error('Choose a new work directory')
subprocess.run(['rpmkeys','--checksig',str(source)],check=True)
identity=subprocess.check_output(['rpm','-qp','--qf','%{NAME} %{VERSION} %{RELEASE} %{ARCH}',str(source)],text=True)
if identity!='python3-vtk 9.5.2 12.fc44 x86_64':p.error('This recipe targets the installed Fedora 44 VTK 9.5.2-12 ABI')
if 'CMAKE_BUILD_TYPE:STRING=Release' not in (build/'CMakeCache.txt').read_text():p.error('Build the adapter in Release mode')
work.mkdir(parents=True)
python=work/'python-payload';python.mkdir()
stream=subprocess.Popen(['rpm2cpio',str(source)],stdout=subprocess.PIPE)
try:subprocess.run(['cpio','-idm','--quiet','--no-absolute-filenames'],stdin=stream.stdout,cwd=python,check=True)
finally:stream.stdout.close()
if stream.wait()!=0:raise SystemExit('RPM extraction failed')
site=python/'usr/lib64/python3.14/site-packages';modules=site/'vtkmodules'
removed=[]
for f in modules.glob('*Qt*'):
 removed.append(str(f.relative_to(python)));f.unlink()
removed.extend(str(f.relative_to(python)) for f in (modules/'qt').rglob('*') if f.is_file())
shutil.rmtree(modules/'qt')
for f in (site/'vtk.py',modules/'all.py',modules/'__init__.py'):
 content=f.read_text();assert 'vtkRenderingQt' in content,f
 f.write_text('\n'.join(line for line in content.split('\n') if 'vtkRenderingQt' not in line))
 for optimize in (0,1):py_compile.compile(str(f),dfile='/'+str(f.relative_to(python)),optimize=optimize,doraise=True)
shutil.rmtree(python/'usr/lib/.build-id')
# The native binary payload is unchanged. Keep the same core ABI for OpenCV,
# OpenCascade and all non-Qt Python modules; do not pretend to provide Qt bindings.
(work/'python-changes.json').write_text(json.dumps({'original_rpm_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'removed':removed,'edited':['vtk.py','vtkmodules/all.py','vtkmodules/__init__.py'],'retained_core_abi':'vtk-9.5.2-12.fc44.x86_64'},indent=2)+'\n')
adapter=work/'adapter-payload'
subprocess.run(['cmake','--install',str(build)],env={**os.environ,'DESTDIR':str(adapter)},check=True)
wrapper=adapter/'usr/bin/vtk-fltk';wrapper.parent.mkdir(parents=True,exist_ok=True)
wrapper.write_text('''#!/bin/sh
set -eu
vtk_native_path=""
vtk_saved_ifs=$IFS
IFS=:
for vtk_dir in ${LD_LIBRARY_PATH-}; do
  case "$vtk_dir" in /opt/fltk-vr/toolkit/lib*|"$HOME/.local/lib/domed/fltk"*) ;;
    *) vtk_native_path=${vtk_native_path:+$vtk_native_path:}$vtk_dir ;; esac
done
IFS=$vtk_saved_ifs
if [ -n "$vtk_native_path" ]; then export LD_LIBRARY_PATH="$vtk_native_path"; else unset LD_LIBRARY_PATH; fi
export FLTK_BACKEND="${FLTK_BACKEND:-wayland}"
exec /usr/libexec/vtk-fltk/vtk-fltk "$@"
''');wrapper.chmod(0o755)
entry=adapter/'usr/share/applications/org.domed.VTKFLTK.desktop';entry.parent.mkdir(parents=True,exist_ok=True)
entry.write_text('''[Desktop Entry]
Type=Application
Name=VTK FLTK
Comment=View VTK, STL, OBJ and PLY geometry using system FLTK
Exec=/usr/local/bin/dome-link --app-id vtk-fltk -- /usr/bin/vtk-fltk %f
TryExec=/usr/bin/vtk-fltk
Icon=applications-science
Terminal=false
Categories=Science;Graphics;3DGraphics;
MimeType=application/vnd.vtk;model/stl;model/obj;model/ply;
StartupWMClass=vtk-fltk
''')
subprocess.run(['desktop-file-validate',str(entry)],check=True)
for tree in (python,adapter):
 for f in tree.rglob('*'):
  if f.is_symlink():continue
  f.chmod(0o755 if f.is_dir() or f.stat().st_mode&0o111 else 0o644)
# Repacking optimized, signed Fedora binaries does not require root or a core
# rebuild. Preserve ELF bytes/build IDs and the upstream license files.
common='''%global debug_package %{nil}
%global _build_id_links none
%global __os_install_post %{nil}
License: BSD-3-Clause
URL: https://vtk.org/
BuildArch: x86_64
'''
for name,payload,version,release,summary,extra,license in [
 ('python3-vtk',python,'9.5.2','12.fc44.domed1','VTK Python bindings without optional Qt frontends',
  'Requires: vtk(x86-64) = 9.5.2-12.fc44\nRequires: python(abi) = 3.14\nProvides: python-vtk = %{version}-%{release}\nProvides: python3.14-vtk = %{version}-%{release}\nProvides: python3dist(vtk) = %{version}\nProvides: python3.14dist(vtk) = %{version}\n','BSD-3-Clause'),
 ('vtk-fltk',adapter,'0.2.0','1.fc44.domed1','VTK render widget and geometry viewer using system FLTK','Requires: vtk(x86-64) = 9.5.2-12.fc44\n','MIT')]:
 top=work/name
 for directory in ('SPECS','BUILD','BUILDROOT','RPMS','SRPMS','SOURCES'):(top/directory).mkdir(parents=True)
 files=[]
 for f in sorted(payload.rglob('*')):
  rel=f.relative_to(payload);path='/'+str(rel)
  if f.is_dir() and not f.is_symlink():
   own=(len(rel.parts)>=5 and rel.parts[:4]==('usr','lib64','python3.14','site-packages')) or any(path.startswith(s) for s in ('/usr/share/doc/'+name,'/usr/share/licenses/'+name,'/usr/libexec/vtk-fltk','/usr/include/vtk-fltk','/usr/lib64/cmake/VTKFLTK'))
   if own:files.append('%dir '+path)
  else:files.append(path)
 filelist=top/'files.list';filelist.write_text('\n'.join(files)+'\n')
 spec=top/'SPECS'/f'{name}.spec'
 spec.write_text(f'''Name: {name}
Version: {version}
Release: {release}
Summary: {summary}
'''+common.replace('License: BSD-3-Clause','License: '+license)+extra+f'''
%description
{summary}. Native non-Qt VTK ABI is retained.

%install
mkdir -p %{{buildroot}}
cp -a %{{native_payload}}/. %{{buildroot}}/

%files -f {filelist}
''')
 subprocess.run(['rpmbuild','-bb','--define',f'_topdir {top}','--define',f'native_payload {payload}',str(spec)],check=True)
 rpms=list((top/'RPMS').rglob('*.rpm'));assert len(rpms)==1,rpms
 requires=subprocess.check_output(['rpm','-qp','--requires',str(rpms[0])],text=True)
 if 'libQt' in requires or 'vtk-qt' in requires:raise SystemExit('Unexpected Qt RPM dependency: '+requires)
 (work/(name+'-requires.txt')).write_text(requires)
 print('Verified no Qt dependency:',rpms[0],flush=True)
