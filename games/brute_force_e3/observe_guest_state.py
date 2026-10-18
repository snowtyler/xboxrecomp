"""Read-only guest route state series for one workspace qualification process."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import time


def observe(pid, output, seconds, interval):
    from guest_state import snapshot_game_state
    kernel = c.WinDLL("kernel32", use_last_error=True)
    dbg = c.WinDLL("dbghelp", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    dbg.SymInitializeW.argtypes = [w.HANDLE, w.LPCWSTR, w.BOOL]
    dbg.SymFromName.argtypes = [w.HANDLE, c.c_char_p, c.c_void_p]
    dbg.SymCleanup.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise OSError(c.get_last_error(), "OpenProcess")
    symbols = False
    try:
        image = c.create_unicode_buffer(32768)
        length = w.DWORD(len(image))
        if not kernel.QueryFullProcessImageNameW(handle, 0, image, c.byref(length)):
            raise OSError(c.get_last_error(), "Query image")
        root = Path(__file__).resolve().parent / "build/RelWithDebInfo"
        exe = Path(image.value).resolve()
        if exe.parent != root.resolve() or not exe.name.startswith("brute_force_e3_perf"):
            raise RuntimeError("Not a workspace qualification candidate")
        if not dbg.SymInitializeW(handle, str(root), True):
            raise OSError(c.get_last_error(), "Symbols")
        symbols = True
        symbol = c.create_string_buffer(1120)
        c.c_uint32.from_buffer(symbol, 0).value = 88
        c.c_uint32.from_buffer(symbol, 80).value = 1023
        if not dbg.SymFromName(handle, b"g_memory_offset", symbol):
            raise OSError(c.get_last_error(), "g_memory_offset")
        address = int.from_bytes(symbol.raw[56:64], "little")

        def read_host(address, size):
            data = c.create_string_buffer(size)
            got = c.c_size_t()
            if not kernel.ReadProcessMemory(handle, address, data, size, c.byref(got)) or got.value != size:
                raise OSError(c.get_last_error(), f"Read {address:X}")
            return data.raw

        base = int.from_bytes(read_host(address, 8), "little")
        deadline = time.monotonic() + 30
        while not base and time.monotonic() < deadline:
            time.sleep(0.05)
            base = int.from_bytes(read_host(address, 8), "little")
        if not base:
            raise RuntimeError("Guest RAM did not initialize")

        def read_guest(address, size):
            if not (0 < size <= 4096 and (0x10000 <= address < address + size <= 0x08000000
                                        or 0x80000000 <= address < address + size <= 0x88000000)):
                raise ValueError("Guest state read outside mapped RAM bounds")
            return read_host(base + address, size)

        start = time.monotonic()
        samples = errors = 0
        with output.open("x", encoding="utf-8") as stream:
            while time.monotonic() - start < seconds:
                code = w.DWORD()
                if not kernel.GetExitCodeProcess(handle, c.byref(code)) or code.value != 259:
                    break
                now = time.monotonic()
                try:
                    state = snapshot_game_state(read_guest)
                    record = dict(monotonic_seconds=now, pid=pid, state=state)
                except (OSError, ValueError) as error:
                    errors += 1
                    record = dict(monotonic_seconds=now, pid=pid, error=str(error))
                stream.write(json.dumps(record) + "\n")
                stream.flush()
                samples += 1
                time.sleep(interval)
        output.with_suffix(".json").write_text(json.dumps(dict(pid=pid, image=str(exe),
            interval=interval, samples=samples, errors=errors, read_only=True), indent=2) + "\n")
    finally:
        if symbols:
            dbg.SymCleanup(handle)
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()
    if not 0.1 <= args.interval <= 60 or args.seconds <= 0:
        parser.error("Use a positive duration and interval 0.1..60 seconds")
    observe(args.pid, args.output, args.seconds, args.interval)
