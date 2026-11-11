"""Names for "module+0xOFFSET" addresses of a crash log line, from the build's .pdb (dbghelp, no debugger needed).

    python tools/symaddr.py <path to cdmp.asi or coopplus.asi> 0x91013 0x164CDD ...
    python tools/symaddr.py <asi> --log <a coopplus.log>      every "<asi name>+0x..." in the log, each once

The .pdb must lie next to the .asi (the build folder) and belong to that exact build.
"""
import ctypes
import os
import re
import sys
from ctypes import wintypes

dbghelp = ctypes.WinDLL("dbghelp.dll")
kernel32 = ctypes.WinDLL("kernel32.dll")


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG), ("Reserved", ctypes.c_ulonglong * 2),
                ("Index", wintypes.ULONG), ("Size", wintypes.ULONG), ("ModBase", ctypes.c_ulonglong),
                ("Flags", wintypes.ULONG), ("Value", ctypes.c_ulonglong), ("Address", ctypes.c_ulonglong),
                ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG), ("Tag", wintypes.ULONG),
                ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG), ("Name", ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wintypes.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wintypes.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_ulonglong)]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image = os.path.abspath(sys.argv[1])
    name = os.path.basename(image)
    if sys.argv[2] == "--log":
        text = open(sys.argv[3], encoding="utf-8", errors="replace").read()
        offs = sorted({int(m, 16) for m in re.findall(re.escape(name) + r"\+0x([0-9A-Fa-f]+)", text)})
    else:
        offs = [int(a, 16) for a in sys.argv[2:]]
    proc = wintypes.HANDLE(kernel32.GetCurrentProcess())
    dbghelp.SymSetOptions(0x00000010 | 0x00000002)  # LOAD_LINES | UNDNAME
    dbghelp.SymInitialize.argtypes = [wintypes.HANDLE, ctypes.c_char_p, wintypes.BOOL]
    if not dbghelp.SymInitialize(proc, os.path.dirname(image).encode(), False):
        print("SymInitialize failed")
        return 1
    base = 0x10000000
    dbghelp.SymLoadModuleEx.restype = ctypes.c_ulonglong
    dbghelp.SymLoadModuleEx.argtypes = [wintypes.HANDLE, wintypes.HANDLE, ctypes.c_char_p, ctypes.c_char_p,
                                        ctypes.c_ulonglong, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    if not dbghelp.SymLoadModuleEx(proc, None, image.encode(), None, base, 0, None, 0):
        print("SymLoadModuleEx failed", ctypes.GetLastError())
        return 1
    dbghelp.SymFromAddr.argtypes = [wintypes.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(ctypes.c_ulonglong),
                                    ctypes.POINTER(SYMBOL_INFO)]
    dbghelp.SymGetLineFromAddr64.argtypes = [wintypes.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(wintypes.DWORD),
                                             ctypes.POINTER(IMAGEHLP_LINE64)]
    for off in offs:
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 1024
        sym.MaxNameLen = 1000
        disp = ctypes.c_ulonglong(0)
        fn = "?"
        if dbghelp.SymFromAddr(proc, base + off, ctypes.byref(disp), ctypes.byref(sym)):
            fn = f"{sym.Name.decode(errors='replace')}+0x{disp.value:X}"
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        d2 = wintypes.DWORD(0)
        where = ""
        if dbghelp.SymGetLineFromAddr64(proc, base + off, ctypes.byref(d2), ctypes.byref(line)):
            f = line.FileName.decode(errors="replace")
            where = f"  [{f[f.lower().find('src'):] if 'src' in f.lower() else os.path.basename(f)}:{line.LineNumber}]"
        print(f"{name}+0x{off:X}  {fn}{where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
