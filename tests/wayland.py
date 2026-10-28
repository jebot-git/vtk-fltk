#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import os,sys,subprocess,tempfile,socket,json,re,time,urllib.request,argparse
parser=argparse.ArgumentParser()
parser.add_argument("--binary",type=Path,required=True)
parser.add_argument("--artifacts",type=Path,required=True)
args=parser.parse_args();args.binary=args.binary.resolve();args.artifacts=args.artifacts.resolve();args.artifacts.mkdir(parents=True,exist_ok=True)
root=args.artifacts
if os.environ.get('NATIVE_OFFICE_TEST_BUS')!='1':
 os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'NATIVE_OFFICE_TEST_BUS':'1'})
processes=[];logs=[]
def start(command,env,name,**kw):
 log=(root/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen([str(c) for c in command],env=env,stdout=log,stderr=subprocess.STDOUT,**kw);processes.append(p);return p

def wait(fn):
 end=time.monotonic()+25
 while time.monotonic()<end:
  try:
   result=fn()
   if result:return result
  except (OSError,KeyError,ValueError):pass
  time.sleep(.03)
 raise AssertionError('Timeout in private Wayland test')
try:
 with tempfile.TemporaryDirectory(prefix='native-office-wayland-') as temp:
  base=Path(temp);(base/'home').mkdir()
  rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1280x900x24','-nolisten','tcp'],os.environ.copy(),'native-xvfb',pass_fds=(wr,));os.close(wr)
  with os.fdopen(rd) as stream:display=':'+stream.readline().strip()
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  env={**os.environ,'HOME':str(base/'home'),'DISPLAY':display,'XDG_RUNTIME_DIR':temp,'XDG_CONFIG_HOME':temp+'/config','XDG_DATA_HOME':temp+'/data','XDG_CACHE_HOME':temp+'/cache','XDG_STATE_HOME':temp+'/state','DOME_PREVIEW_PORT':str(port),'DOME_RENDERER':'gles','DOME_START_IME':'0','DOME_LOGIN_SESSION':'0','DOME_START_XR':'0','DOME_START_POLKIT':'0','DOME_START_KDECONNECT':'0','DOME_START_TRAY':'0','LIBGL_ALWAYS_SOFTWARE':'1','RUST_LOG':'warn'}
  for key in ('WAYLAND_DISPLAY','WAYLAND_SOCKET'):env.pop(key,None)
  start(['/usr/local/bin/dome-compositor','--backend','nested'],env,'native-compositor')
  def scene():
   with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/state',timeout=1) as res:return json.load(res)['scene']
  wait(lambda:scene()['source']=='wayland')
  socket_name=wait(lambda:re.search(r'Dome Wayland socket: (\S+)',(root/'native-compositor.log').read_text()))[1]
  client={**env,'WAYLAND_DISPLAY':socket_name,'FLTK_BACKEND':'wayland','LD_LIBRARY_PATH':os.environ.get('LD_LIBRARY_PATH','')};client.pop('DISPLAY',None)
  results={}
  commands={'vtk':[args.binary,'--capture',root/'wayland-vtk.ppm','--quit-after-ms','2200']}
  for name,command in commands.items():
   p=start(command,client,'wayland-'+name)
   wait(lambda:scene()['windows'])
   maps=Path(f'/proc/{p.pid}/maps').read_text();libs=sorted(set(line.split()[-1] for line in maps.splitlines() if 'libfltk' in line or 'libQt' in line or 'libgtk-' in line))
   assert not any('libQt' in lib for lib in libs),libs
   assert any(lib.startswith(('/usr/lib64/libfltk','/lib64/libfltk')) for lib in libs),libs
   assert not any('/opt/fltk-vr/toolkit/' in lib for lib in libs),libs
   assert p.wait(timeout=10)==0,(root/('wayland-'+name+'.log')).read_text()
   assert (root/('wayland-'+name+'.ppm')).stat().st_size>1000
   results[name]={'native_wayland':True,'system_fltk':True,'qt_loaded':False,'libraries':libs}
   print('PASS',name,'native Wayland rendering, system FLTK, no Qt',flush=True)
  (root/'native-wayland-results.json').write_text(json.dumps(results,indent=2)+'\n')
finally:
 for p in reversed(processes):
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 for log in logs:log.close()
