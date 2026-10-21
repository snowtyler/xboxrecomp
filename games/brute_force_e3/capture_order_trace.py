"""Read a workspace candidate's diagnostic ring without suspending any thread."""
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--seconds',type=float,default=300)
a=p.parse_args()
if a.output.exists():p.error('Trace output exists; use a fresh name')
k=c.WinDLL('kernel32',use_last_error=True);d=c.WinDLL('dbghelp',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
k.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)];k.CloseHandle.argtypes=[w.HANDLE]
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL];d.SymFromName.argtypes=[w.HANDLE,c.c_char_p,c.c_void_p];d.SymCleanup.argtypes=[w.HANDLE]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise OSError(c.get_last_error(),'OpenProcess')
image=c.create_unicode_buffer(32768);length=w.DWORD(len(image))
if not k.QueryFullProcessImageNameW(h,0,image,c.byref(length)):raise OSError(c.get_last_error(),'Query image')
exe=Path(image.value).resolve();root=Path(__file__).resolve().parent/'build'/'RelWithDebInfo'
if exe.parent!=root.resolve() or not exe.name.startswith(('brute_force_e3_perf','brute_force_e3_candidate')):raise RuntimeError('Not a workspace candidate')
if not d.SymInitializeW(h,str(root),True):raise OSError(c.get_last_error(),'Symbols')
def symbol(name):
    b=c.create_string_buffer(1120);c.c_uint32.from_buffer(b,0).value=88;c.c_uint32.from_buffer(b,80).value=1023
    if not d.SymFromName(h,name.encode(),b):raise OSError(c.get_last_error(),name)
    return int.from_bytes(b[56:64],'little')
def read(address,size):
    b=c.create_string_buffer(size);got=c.c_size_t()
    if not k.ReadProcessMemory(h,address,b,size,c.byref(got)) or got.value!=size:raise OSError(c.get_last_error(),'Read ring')
    return b.raw
meta=dict(pid=a.pid,exe=str(exe),sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
          observer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          capacity=65536,record_size=48,discarded=0,overwritten=0,records=0)
last=0;deadline=time.monotonic()+a.seconds
event_waits={};executor_tid=None;stall_captured=False;latest_qpc=0
try:
    ring=symbol('g_nv2a_order_ring');sequence=symbol('g_nv2a_order_sequence');frequency=symbol('g_nv2a_order_frequency')
    with a.output.open('x',newline='') as stream:
        writer=csv.writer(stream);writer.writerow(['sequence','qpc','qpc_seconds','tid','event','a','b','c','d'])
        while time.monotonic()<deadline:
            status=w.DWORD();k.GetExitCodeProcess(h,c.byref(status))
            if status.value!=259:break
            head=int.from_bytes(read(sequence,8),'little');freq=int.from_bytes(read(frequency,8),'little')
            if head>last and freq:
                payload=read(ring,65536*48);start=max(last+1,head-65535)
                meta['overwritten']+=max(0,start-last-1)
                for number in range(start,head+1):
                    begin,qpc,tid,event,x,y,z,t,end=struct.unpack_from('<QQ6IQ',payload,((number-1)%65536)*48)
                    if begin!=number or end!=number:
                        meta['discarded']+=1;continue
                    writer.writerow([number,qpc,qpc/freq,tid,event,*[f'0x{v:08X}' for v in (x,y,z,t)]])
                    meta['records']+=1
                    latest_qpc=qpc/freq
                    if event==1 and x==0x003AAC9C:
                        if y==2:event_waits[tid]=latest_qpc
                        elif y==3:event_waits.pop(tid,None)
                    if event in (7,16):executor_tid=tid
                last=head;stream.flush()
                # A late stall can occur after the probe's scheduled snapshot.
                # Read only after the event has stayed blocked for five seconds;
                # never suspend threads or alter the guest to unstick it.
                if event_waits and not stall_captured and latest_qpc-min(event_waits.values())>5:
                    stall_captured=True;meta['stall_snapshots']=[]
                    prefix=a.output.stem.removesuffix('-order')
                    for label,thread in [('guest',None),('executor',executor_tid)]:
                        if label=='executor' and thread is None:continue
                        output=a.output.with_name(f'{prefix}-stall-{label}-inspection.json')
                        command=[sys.executable,str(Path(__file__).with_name('inspect_guest.py')),
                                 '--pid',str(a.pid),'--output',str(output)]
                        if thread is not None:command+=['--tid',str(thread)]
                        try:
                            result=subprocess.run(command,timeout=4,capture_output=True,text=True)
                            meta['stall_snapshots'].append(dict(path=output.name,exit_code=result.returncode,
                                                               stderr=result.stderr[-1000:]))
                        except subprocess.TimeoutExpired:
                            meta['stall_snapshots'].append(dict(path=output.name,error='snapshot timeout'))
            time.sleep(.5)
except OSError as error:
    meta['read_error']=str(error)
    # Process exit can race with the final read. A live read failure remains
    # explicit in metadata; it is never treated as a complete event timeline.
finally:
    meta['last_sequence']=last
    a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
    d.SymCleanup(h);k.CloseHandle(h)
