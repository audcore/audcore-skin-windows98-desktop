"""Run the serial MSVC build below normal priority in a capped Windows job."""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import sys

kernel = c.WinDLL("kernel32", use_last_error=True)


class BasicLimits(c.Structure):
    _fields_ = [
        ("process_time", c.c_int64), ("job_time", c.c_int64),
        ("flags", w.DWORD), ("min_working_set", c.c_size_t),
        ("max_working_set", c.c_size_t), ("active_processes", w.DWORD),
        ("affinity", c.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD),
    ]


class ExtendedLimits(c.Structure):
    _fields_ = [
        ("basic", BasicLimits), ("io", c.c_uint64 * 6),
        ("process_memory", c.c_size_t), ("job_memory", c.c_size_t),
        ("peak_process_memory", c.c_size_t), ("peak_job_memory", c.c_size_t),
    ]


class Startup(c.Structure):
    _fields_ = [
        ("size", w.DWORD), ("reserved", w.LPWSTR), ("desktop", w.LPWSTR), ("title", w.LPWSTR),
        ("dimensions", w.DWORD * 8), ("show", w.WORD), ("reserved_size", w.WORD),
        ("reserved_bytes", c.c_void_p), ("stdin", w.HANDLE), ("stdout", w.HANDLE), ("stderr", w.HANDLE),
    ]


class Process(c.Structure):
    _fields_ = [("handle", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]


def api(name, result, *args):
    function = getattr(kernel, name)
    function.restype = result
    function.argtypes = args
    return function


def check(result):
    if not result:
        raise c.WinError(c.get_last_error())
    return result


def main(command):
    create_job = api("CreateJobObjectW", w.HANDLE, c.c_void_p, w.LPCWSTR)
    configure = api("SetInformationJobObject", w.BOOL, w.HANDLE, c.c_int, c.c_void_p, w.DWORD)
    create_process = api("CreateProcessW", w.BOOL, w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p,
                         w.BOOL, w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(Startup), c.POINTER(Process))
    assign = api("AssignProcessToJobObject", w.BOOL, w.HANDLE, w.HANDLE)
    resume = api("ResumeThread", w.DWORD, w.HANDLE)
    wait = api("WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)
    exit_code = api("GetExitCodeProcess", w.BOOL, w.HANDLE, c.POINTER(w.DWORD))
    terminate = api("TerminateProcess", w.BOOL, w.HANDLE, w.UINT)
    close = api("CloseHandle", w.BOOL, w.HANDLE)

    job = check(create_job(None, None))
    process = Process()
    try:
        limits = ExtendedLimits()
        limits.basic.flags = 0x200 | 0x2000  # JOB_MEMORY | KILL_ON_JOB_CLOSE
        limits.job_memory = 1024 * 1024 * 1024
        check(configure(job, 9, c.byref(limits), c.sizeof(limits)))
        startup = Startup()
        startup.size = c.sizeof(startup)
        line = c.create_unicode_buffer(subprocess.list2cmdline(command))
        check(create_process(None, line, None, None, True, 0x4 | 0x4000,
                             None, None, c.byref(startup), c.byref(process)))  # suspended | below normal
        try:
            check(assign(job, process.handle))
        except OSError:
            terminate(process.handle, 1)
            raise
        if resume(process.thread) == 0xFFFFFFFF or wait(process.handle, 0xFFFFFFFF) != 0:
            raise c.WinError(c.get_last_error())
        status = w.DWORD()
        check(exit_code(process.handle, c.byref(status)))
        return status.value
    finally:
        close(job)
        if process.handle:
            close(process.thread)
            close(process.handle)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
