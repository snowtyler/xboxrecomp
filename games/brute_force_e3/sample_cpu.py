"""Sample instruction addresses of an owned recomp process on Windows."""
import argparse
import collections
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--pid", type=int, required=True)
p.add_argument("--seconds", type=float, default=10)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
k = c.WinDLL("kernel32", use_last_error=True)
d = c.WinDLL("dbghelp", use_last_error=True)
k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
k.CreateToolhelp32Snapshot.restype = w.HANDLE
k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenThread.restype = w.HANDLE
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
for name in ["CloseHandle", "SuspendThread", "ResumeThread"]:
    getattr(k, name).argtypes = [w.HANDLE]
k.SuspendThread.restype = k.ResumeThread.restype = w.DWORD
k.GetThreadContext.argtypes = [w.HANDLE, c.c_void_p]
k.GetThreadTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]

class Thread(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                ("pid", w.DWORD), ("base", w.LONG), ("delta", w.LONG), ("flags", w.DWORD)]

k.Thread32First.argtypes = k.Thread32Next.argtypes = [w.HANDLE, c.POINTER(Thread)]
snapshot = k.CreateToolhelp32Snapshot(4, 0)
entry = Thread(size=c.sizeof(Thread))
threads = []
try:
    more = k.Thread32First(snapshot, c.byref(entry))
    while more:
        if entry.pid == a.pid:
            handle = k.OpenThread(0x4A, False, entry.tid)
            if handle:
                times = [w.FILETIME() for _ in range(4)]
                if k.GetThreadTimes(handle, *map(c.byref, times)):
                    cpu = sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:])
                    threads.append((cpu, entry.tid, handle))
                else:
                    k.CloseHandle(handle)
        more = k.Thread32Next(snapshot, c.byref(entry))
finally:
    k.CloseHandle(snapshot)
if not threads:
    raise SystemExit("No accessible threads in the supplied process")
threads.sort(reverse=True)
_, tid, handle = threads[0]
buffer = c.create_string_buffer(1232 + 16)
aligned = (c.addressof(buffer) + 15) & ~15
counts = collections.Counter()
thread_counts = {thread_id: collections.Counter() for _, thread_id, _ in threads}
stack_words = {}
process = k.OpenProcess(0x410, False, a.pid)
deadline = time.monotonic() + a.seconds
try:
    while time.monotonic() < deadline:
        c.c_uint32.from_address(aligned + 48).value = 0x100003
        for _, thread_id, thread_handle in threads:
            if k.SuspendThread(thread_handle) != 0xFFFFFFFF:
                try:
                    if k.GetThreadContext(thread_handle, aligned):
                        address = c.c_uint64.from_address(aligned + 248).value
                        thread_counts[thread_id][address] += 1
                        if thread_id not in stack_words:
                            rsp = c.c_uint64.from_address(aligned + 152).value
                            stack = c.create_string_buffer(4096)
                            received = c.c_size_t()
                            readable = k.ReadProcessMemory(process, rsp, stack, len(stack), c.byref(received))
                            if not readable:
                                readable = k.ReadProcessMemory(process, rsp, stack, 512, c.byref(received))
                            if readable:
                                stack_words[thread_id] = [int.from_bytes(stack.raw[i:i+8], "little")
                                                         for i in range(0, received.value-7, 8)]
                        if thread_id == tid:
                            counts[address] += 1
                finally:
                    k.ResumeThread(thread_handle)
        time.sleep(0.007)
finally:
    for _, _, h in threads:
        k.CloseHandle(h)
d.SymInitializeW.argtypes = [w.HANDLE, w.LPCWSTR, w.BOOL]
d.SymFromAddr.argtypes = [w.HANDLE, c.c_uint64, c.POINTER(c.c_uint64), c.c_void_p]
d.SymCleanup.argtypes = [w.HANDLE]
symbolized = collections.Counter()
addresses = []
thread_results = []
try:
    if not d.SymInitializeW(process, str(Path(__file__).resolve().parent / "build/RelWithDebInfo"), True):
        raise OSError(c.get_last_error(), "SymInitialize")
    for address, count in counts.most_common():
        info = c.create_string_buffer(1120)
        c.c_uint32.from_buffer(info, 0).value = 88
        c.c_uint32.from_buffer(info, 80).value = 1023
        displacement = c.c_uint64()
        name = f"0x{address:016X}"
        if d.SymFromAddr(process, address, c.byref(displacement), c.addressof(info)):
            name = bytes(info[84:]).split(b"\0", 1)[0].decode(errors="replace")
        symbolized[name] += count
        addresses.append({"address": f"0x{address:016X}", "symbol": name,
                          "offset": displacement.value, "samples": count})
    for cpu, thread_id, _ in threads:
        symbols = collections.Counter()
        for address, count in thread_counts[thread_id].items():
            info = c.create_string_buffer(1120)
            c.c_uint32.from_buffer(info, 0).value = 88
            c.c_uint32.from_buffer(info, 80).value = 1023
            displacement = c.c_uint64()
            name = f"0x{address:016X}"
            if d.SymFromAddr(process, address, c.byref(displacement), c.addressof(info)):
                name = bytes(info[84:]).split(b"\0", 1)[0].decode(errors="replace")
            symbols[name] += count
        candidates = []
        if cpu:
            for word in stack_words.get(thread_id, []):
                if word < 0x100000000:
                    continue
                info = c.create_string_buffer(1120)
                c.c_uint32.from_buffer(info, 0).value = 88
                c.c_uint32.from_buffer(info, 80).value = 1023
                displacement = c.c_uint64()
                if d.SymFromAddr(process, word, c.byref(displacement), c.addressof(info)):
                    name = bytes(info[84:]).split(b"\0", 1)[0].decode(errors="replace")
                    if name.startswith(("sub_", "bridge_", "kernel_", "xbox_", "nv2a_")) and name not in candidates:
                        candidates.append(name)
        thread_results.append({"tid": thread_id, "cpu_seconds": cpu / 10000000,
                               "symbols": symbols.most_common(20),
                               "stack_symbol_candidates": candidates[:30]})
finally:
    d.SymCleanup(process)
    k.CloseHandle(process)
result = {"pid": a.pid, "tid": tid, "samples": sum(counts.values()),
          "symbols": symbolized.most_common(40), "addresses": addresses,
          "threads": thread_results}
a.output.write_text(json.dumps(result, indent=2))
print(json.dumps({key: result[key] for key in ["pid", "tid", "samples", "symbols"]}, indent=2))
