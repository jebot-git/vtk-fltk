#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real geometry import, rendering, interaction and universal-menu checks in private Xvfb/D-Bus."""
import argparse,json,os,subprocess,sys,tempfile,time
from pathlib import Path
from PIL import Image
from vtkmodules.vtkFiltersSources import vtkConeSource
from vtkmodules.vtkIOXML import vtkXMLPolyDataWriter
from vtkmodules.vtkIOLegacy import vtkPolyDataWriter
from vtkmodules.vtkIOGeometry import vtkSTLWriter,vtkOBJWriter
from vtkmodules.vtkIOPLY import vtkPLYWriter
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--artifacts',type=Path,required=True)
a=parser.parse_args()
if not os.environ.get('VTK_FLTK_TEST_BUS'):
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'VTK_FLTK_TEST_BUS':'1'})
a.binary=a.binary.resolve();a.artifacts=a.artifacts.resolve();a.artifacts.mkdir(parents=True,exist_ok=True)
def wait(test,timeout=20):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  try:
   value=test()
   if value:return value
  except (OSError,ValueError,KeyError):pass
  time.sleep(.05)
 raise AssertionError('Timeout in VTK FLTK test')
with tempfile.TemporaryDirectory(prefix='vtk-fltk-test-') as temp:
 base=Path(temp);rd,wr=os.pipe()
 x=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1200x900x24','-nolisten','tcp'],pass_fds=(wr,),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);os.close(wr)
 with os.fdopen(rd) as f:display=':'+f.readline().strip()
 env={**os.environ,'DISPLAY':display,'FLTK_BACKEND':'x11','XDG_CONFIG_HOME':temp+'/config','XDG_CACHE_HOME':temp+'/cache','XDG_STATE_HOME':temp+'/state','XDG_DATA_HOME':temp+'/data','NO_AT_BRIDGE':'1'}
 for key in ('WAYLAND_DISPLAY','WAYLAND_SOCKET'):env.pop(key,None)
 process=None
 try:
  cone=vtkConeSource();cone.SetResolution(32);cone.Update()
  for extension,writer_type in [('vtp',vtkXMLPolyDataWriter),('vtk',vtkPolyDataWriter),('stl',vtkSTLWriter),('obj',vtkOBJWriter),('ply',vtkPLYWriter)]:
   source=base/('Český 日本語.'+extension);writer=writer_type();writer.SetFileName(str(source));writer.SetInputData(cone.GetOutput());assert writer.Write()==1
   report=a.artifacts/(extension+'.json');capture=a.artifacts/(extension+'.ppm')
   report.unlink(missing_ok=True);capture.unlink(missing_ok=True)
   with (a.artifacts/(extension+'.log')).open('w') as log:
    process=subprocess.Popen([str(a.binary),str(source),'--capture',str(capture),'--report',str(report)],env=env,stdout=log,stderr=subprocess.STDOUT)
    def state():return json.loads(report.read_text())
    wait(lambda:report.is_file() and state()['capture_complete'])
    assert state()['points']>0 and state()['cells']>0
    maps=Path(f'/proc/{process.pid}/maps').read_text();assert 'libQt' not in maps
    assert '/usr/lib64/libfltk.so.1.4.5' in maps and '/opt/fltk-vr/toolkit/lib/libfltk' not in maps
    image=Image.open(capture);assert max(high-low for low,high in image.getextrema())>100
    image.save(a.artifacts/(extension+'.png'))
    address=state()['desktop_menu']
    layout=subprocess.check_output(['gdbus','call','--session','--dest',address['service'],'--object-path',address['path'],'--method','com.canonical.dbusmenu.GetLayout','0','2','[]'],env=env,text=True)
    assert 'Reset camera' in layout and 'Wireframe' in layout
    windows=subprocess.check_output(['xdotool','search','--onlyvisible','--pid',str(process.pid)],env=env,text=True).splitlines()
    host=windows[0]
    subprocess.run(['xdotool','windowfocus','--sync',host],env=env,check=True)
    subprocess.run(['xdotool','key','w'],env=env,check=True);wait(lambda:state()['wireframe'])
    before=state()['camera']
    subprocess.run(['xdotool','mousemove','--window',host,'400','320','mousedown','1','mousemove','--window',host,'520','390','mouseup','1'],env=env,check=True)
    wait(lambda:state()['camera']!=before)
    subprocess.run(['xdotool','key','r'],env=env,check=True)
    subprocess.run(['xdotool','key','ctrl+w'],env=env,check=True)
    assert process.wait(timeout=10)==0
    process=None
   print('PASS',extension,'Unicode file import, nonempty VTK frame, system FLTK/no Qt, menu, wireframe, camera drag and close',flush=True)
  invalid=base/'invalid.stl';invalid.write_text('not a mesh')
  result=subprocess.run([str(a.binary),str(invalid)],env=env,capture_output=True,text=True,timeout=15)
  assert result.returncode==1 and 'No readable dataset geometry' in result.stderr,result
  print('PASS invalid geometry rejected with exit status 1',flush=True)
 finally:
  if process and process.poll() is None:process.terminate();process.wait(timeout=10)
  x.terminate();x.wait(timeout=10)
