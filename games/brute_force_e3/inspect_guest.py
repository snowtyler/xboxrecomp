"""Read-only symbol/TLS snapshot of a selected workspace performance test."""
import argparse, ctypes as c, json
from ctypes import wintypes as w
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True);p.add_argument('--tid',type=int,help='Defaults to the oldest thread owned by this process')
p.add_argument('--output',type=Path,required=True)
p.add_argument('--buffer',type=lambda x:int(x,0),action='append',default=[],help='Guest DirectSound interface address to inspect')
a=p.parse_args()
k=c.WinDLL('kernel32',use_last_error=True);d=c.WinDLL('dbghelp',use_last_error=True);n=c.WinDLL('ntdll')
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise OSError(c.get_last_error(),'OpenProcess')
path=c.create_unicode_buffer(32768);length=w.DWORD(len(path))
if not k.QueryFullProcessImageNameW(h,0,path,c.byref(length)):raise OSError(c.get_last_error(),'Query image')
exe=Path(path.value).resolve();root=Path(__file__).resolve().parent/'build'/'RelWithDebInfo'
if exe.parent!=root.resolve() or not (exe.name.startswith('brute_force_e3_perf') or exe.name=='brute_force_e3_prototype.exe'):raise RuntimeError('Not a workspace performance test')
if a.tid is None:
    class ThreadEntry(c.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('pid',w.DWORD),('priority',w.LONG),('delta',w.LONG),('flags',w.DWORD)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Thread32First.argtypes=[w.HANDLE,c.POINTER(ThreadEntry)];k.Thread32Next.argtypes=k.Thread32First.argtypes
    k.GetThreadTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
    snapshot=k.CreateToolhelp32Snapshot(4,0);entry=ThreadEntry();entry.size=c.sizeof(entry);candidates=[]
    try:
        found=k.Thread32First(snapshot,c.byref(entry))
        while found:
            if entry.pid==a.pid:
                thread=k.OpenThread(0x40,False,entry.tid)
                if thread:
                    times=[w.FILETIME() for _ in range(4)]
                    try:
                        if k.GetThreadTimes(thread,*[c.byref(x) for x in times]):
                            candidates.append(((times[0].dwHighDateTime<<32)|times[0].dwLowDateTime,entry.tid))
                    finally:k.CloseHandle(thread)
            entry.size=c.sizeof(entry);found=k.Thread32Next(snapshot,c.byref(entry))
    finally:k.CloseHandle(snapshot)
    if not candidates:raise RuntimeError('No queryable process thread')
    a.tid=min(candidates)[1]
def read(addr,size):
    buf=c.create_string_buffer(size);got=c.c_size_t()
    if not k.ReadProcessMemory(h,addr,buf,size,c.byref(got)) or got.value!=size:raise OSError(c.get_last_error(),f'Read {addr:X}')
    return buf.raw
def uint(addr,size=4):return int.from_bytes(read(addr,size),'little')
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL];d.SymFromName.argtypes=[w.HANDLE,c.c_char_p,c.c_void_p];d.SymCleanup.argtypes=[w.HANDLE]
if not d.SymInitializeW(h,str(root),True):raise OSError(c.get_last_error(),'Symbols')
def sym(name):
    b=c.create_string_buffer(1120);c.c_uint32.from_buffer(b,0).value=88;c.c_uint32.from_buffer(b,80).value=1023
    if not d.SymFromName(h,name.encode(),b):raise OSError(c.get_last_error(),name)
    return {'type':int.from_bytes(b[4:8],'little'),'base':int.from_bytes(b[32:40],'little'),'flags':int.from_bytes(b[40:44],'little'),'address':int.from_bytes(b[56:64],'little')}

d.SymGetTypeInfo.argtypes=[w.HANDLE,c.c_ulonglong,w.ULONG,c.c_int,c.c_void_p]
k.LocalFree.argtypes=[c.c_void_p]
def type_uint(base,index,query):
    value=w.ULONG()
    if not d.SymGetTypeInfo(h,base,index,query,c.byref(value)):raise OSError(c.get_last_error(),'Type info')
    return value.value
def members(base,index):
    count=type_uint(base,index,13) # TI_GET_CHILDRENCOUNT
    children=(w.ULONG*(count+2))();children[0]=count
    if not d.SymGetTypeInfo(h,base,index,7,children):raise OSError(c.get_last_error(),'Type children')
    fields={}
    for child in children[2:]:
        name=c.c_void_p()
        if not d.SymGetTypeInfo(h,base,child,1,c.byref(name)):continue
        try:label=c.wstring_at(name)
        finally:k.LocalFree(name)
        try:fields[label]=type_uint(base,child,10) # TI_GET_OFFSET
        except OSError:pass
    return fields
result={'pid':a.pid,'tid':a.tid,'image':str(exe),'symbols':{},'guest':{}}
t=None
try:
    for name in ['g_memory_offset','g_state','g_nv2a_notify_pending','g_nv2a_memory',
                 'g_fence_batch_depth','g_pending_fence_count','g_fence_batch_failed','g_pending_fences',
                 's_irq_line','g_irql_raised_count','_tls_index','g_eax','g_ecx','g_edx','g_esp','g_esi','g_edi']:
        try:result['symbols'][name]=sym(name)
        except OSError as e:result['symbols'][name]={'error':str(e)}
    offset=uint(result['symbols']['g_memory_offset']['address'],8)
    dev=uint(offset+0x003AB378);result['guest']['device']=hex(dev)
    if dev:
        result['guest']['device_fields']={hex(x):hex(uint(offset+dev+x)) for x in [0x2c,0x30,0x196c,0x1970]}
        fence=uint(offset+dev+0x30)
        if fence:result['guest']['fence']=hex(uint(offset+fence))
    t=k.OpenThread(0x40,False,a.tid)
    basic=c.create_string_buffer(48);n.NtQueryInformationThread.argtypes=[w.HANDLE,c.c_int,c.c_void_p,w.ULONG,c.c_void_p]
    if not t or n.NtQueryInformationThread(t,0,basic,48,None)!=0:raise RuntimeError('Thread info unavailable')
    teb=int.from_bytes(basic.raw[8:16],'little');slots=uint(teb+0x58,8)
    index=uint(result['symbols']['_tls_index']['address']);tls=uint(slots+index*8,8)
    def variable_address(name):
        value=result['symbols'][name]
        return tls+value['address'] if value.get('flags',0)&0x4000 else value['address']
    gpu={'fence_thread_id':a.tid}
    try:
        base=uint(variable_address('g_nv2a_memory'),8)
        gpu['registers']={name:hex(uint(base+at)) for name,at in {
            'DMA_PUT':0x800040,'DMA_GET':0x800044,'PMC_INTR':0x100,'PGRAPH_INTR':0x400100,
            'PGRAPH_NSOURCE':0x400108,'PGRAPH_TRAPPED_ADDR':0x400704,'PGRAPH_TRAPPED_DATA':0x400708}.items()}
        gpu['notify_pending']=uint(variable_address('g_nv2a_notify_pending'))
        gpu['fence_batch']={name:uint(variable_address(name)) for name in
            ['g_fence_batch_depth','g_pending_fence_count','g_fence_batch_failed']}
        count=gpu['fence_batch']['g_pending_fence_count']
        if count>64:raise RuntimeError('Invalid pending fence count')
        pending=variable_address('g_pending_fences')
        gpu['pending_fences']=[{'target':hex(uint(pending+i*8)), 'value':hex(uint(pending+i*8+4))}
                               for i in range(count)]
    except Exception as error:
        gpu['inspection_error']=str(error)
    result['gpu']=gpu
    regs={}
    for name in ['g_eax','g_ecx','g_edx','g_esp','g_esi','g_edi']:
        s=result['symbols'][name]
        if s.get('flags',0)&0x4000:regs[name]=uint(tls+s['address'])
    result['guest']['registers']={key:hex(value) for key,value in regs.items()}
    if 'g_esp' in regs:result['guest']['stack']=[hex(uint(offset+regs['g_esp']+i*4)) for i in range(32)]
    state_symbol=result['symbols']['g_state']
    state=uint(state_symbol['address'],8)
    state_type=type_uint(state_symbol['base'],state_symbol['type'],4) # pointer target
    fields=members(state_symbol['base'],state_type)
    result['apu']={'state':hex(state),'field_offsets':fields}
    result['apu']['host_state']={name:uint(result['symbols'][name]['address']) for name in ['s_irq_line','g_irql_raised_count'] if 'address' in result['symbols'][name]}
    if state:
        result['apu']['flags']={name:uint(state+fields[name],1) for name in ['set_irq','pause_requested','is_idle','exiting'] if name in fields}
    if state and 'regs' in fields:
        apu_regs=state+fields['regs']
        result['apu']['registers']={hex(x):hex(uint(apu_regs+x*4)) for x in [0x1000,0x1004,0x1100,0x1110,0x115c,0x202c]}
    # SDK buffer pointers still on the Bink GetStatus caller's stack are
    # candidates only; retain the full stack to validate the call chain.
    buffers=[]
    addresses=list(a.buffer)
    if 'g_esp' in regs:addresses.extend(uint(offset+regs['g_esp']+i*4) for i in range(32))
    for buffer in dict.fromkeys(addresses):
            if not 0x01000000<=buffer<0x04000000:continue
            try:
                voice=uint(offset+buffer-0x1c+0x20)
                if not 0x01000000<=voice<0x04000000:continue
                count=uint(offset+voice+0x64,1)
                if not 1<=count<=8:continue
                handles=[uint(offset+voice+0x0c+j*2,2) for j in range(count)]
                notifier=uint(offset+voice+0x68)
                entry={'interface':hex(buffer),'voice_object':hex(voice),'flags':hex(uint(offset+voice+0x12,2)),
                    'voice_handles':handles,'notifier_pointer':hex(notifier),
                    'sdk_notifiers':read(offset+notifier,min(count*64,512)).hex() if notifier else None}
                if state and 'regs' in fields:
                    vpaddr=uint(apu_regs+0x202c*4);fenaddr=uint(apu_regs+0x115c*4)
                    entry['hardware_voices']=[]
                    for handle in handles:
                        if handle>=256:continue
                        physical=vpaddr+handle*0x80
                        notify=fenaddr+16*(2+handle*4)
                        entry['hardware_voices'].append({'handle':handle,'physical':hex(physical),
                            'low_ram':read(offset+physical,128).hex(),
                            'contiguous_ram':read(offset+0x80000000+physical,128).hex(),
                            'notifier_physical':hex(notify),
                            'notifier_low_ram':read(offset+notify,64).hex(),
                            'notifier_contiguous_ram':read(offset+0x80000000+notify,64).hex()})
                buffers.append(entry)
            except OSError:continue
    result['guest']['buffer_candidates']=buffers
except Exception as e:result['inspection_error']=str(e)
finally:
    if t:k.CloseHandle(t)
    d.SymCleanup(h);k.CloseHandle(h)
a.output.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
