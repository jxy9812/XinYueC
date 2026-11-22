#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Probe the hsb_bridge native library (WiFi + screen power flat C ABI).

[What it does]
  1. locate the shared library (env HSF_BRIDGE_LIB, then the client bin dir,
     then the repo bin dir);
  2. load it with ctypes and print the exported symbols that are present;
  3. call hsb_power_capabilities() and hsb_wifi_adapters();
  4. report failures through hsb_last_error() instead of raising.

[Contract] exit code is always 0 (this is a diagnostic probe, not a gate).
All console output is ASCII so it survives any console code page.
"""

import ctypes
import os
import sys

REQUIRED = (
    "hsb_wifi_open", "hsb_wifi_close", "hsb_last_error", "hsb_free",
    "hsb_wifi_adapters", "hsb_wifi_start_scan", "hsb_wifi_scan_state",
    "hsb_wifi_take_scan_results", "hsb_wifi_connect", "hsb_wifi_disconnect",
    "hsb_wifi_connection", "hsb_power_capabilities",
    "hsb_power_is_screen_off", "hsb_power_set_screen_off",
    "hsb_power_get_policy", "hsb_power_set_policy",
    "hsb_power_request_permission", "hsb_power_tick",
    "hsb_power_note_activity",
)


def candidates():
    """Candidate library paths, most specific first."""
    here = os.path.dirname(os.path.abspath(__file__))
    client_bin = os.path.join(here, os.pardir, "hsfsrv_client", "bin")
    repo_root = os.path.abspath(os.path.join(here, os.pardir, os.pardir,
                                             os.pardir))
    name = "hsb_bridge.dll" if os.name == "nt" else "hsb_bridge.so"
    out = []
    env = os.environ.get("HSF_BRIDGE_LIB")
    if env:
        out.append(env)
    out.append(os.path.join(client_bin, name))
    out.append(os.path.join(repo_root, "bin", name))
    return out


def load():
    """Load the library; returns (dll, path) or (None, tried_paths)."""
    tried = candidates()
    for path in tried:
        if os.path.isfile(path):
            try:
                return ctypes.CDLL(path), path
            except OSError as exc:
                print("LOAD_FAILED %s (%s)" % (path, exc))
                return None, tried
    return None, tried


def decode(value):
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return str(value)


def main():
    dll, path = load()
    if dll is None:
        print("LIB_NOT_FOUND")
        for item in path:
            print("  tried: %s" % item)
        print("  hint: build it with .zcode/build_hsf_bridge.bat")
        return 0

    print("LIB %s" % path)

    present = []
    missing = []
    for name in REQUIRED:
        if hasattr(dll, name):
            present.append(name)
        else:
            missing.append(name)
    print("EXPORTS_PRESENT %d" % len(present))
    for name in present:
        print("  + %s" % name)
    for name in missing:
        print("  - %s (missing)" % name)

    dll.hsb_last_error.argtypes = []
    dll.hsb_last_error.restype = ctypes.c_char_p

    # ---- power ----
    dll.hsb_power_capabilities.argtypes = []
    dll.hsb_power_capabilities.restype = ctypes.c_uint
    caps = int(dll.hsb_power_capabilities())
    print("POWER_CAPABILITIES %d" % caps)

    dll.hsb_power_is_screen_off.argtypes = []
    dll.hsb_power_is_screen_off.restype = ctypes.c_int
    print("POWER_SCREEN_OFF %d" % int(dll.hsb_power_is_screen_off()))

    dll.hsb_power_get_policy.argtypes = [ctypes.c_char_p, ctypes.c_int]
    dll.hsb_power_get_policy.restype = ctypes.c_int
    buf = ctypes.create_string_buffer(512)
    rc = int(dll.hsb_power_get_policy(buf, ctypes.sizeof(buf)))
    if rc == 1:
        print("POWER_POLICY %s" % decode(buf.value))
    else:
        print("POWER_POLICY_FAILED %s"
              % decode(dll.hsb_last_error()))

    # ---- power drive (idle tick + activity report) ----
    dll.hsb_power_tick.argtypes = []
    dll.hsb_power_tick.restype = ctypes.c_int
    dll.hsb_power_note_activity.argtypes = [ctypes.c_uint]
    dll.hsb_power_note_activity.restype = ctypes.c_int
    tick_rc = int(dll.hsb_power_tick())
    if tick_rc == 1:
        print("POWER_TICK ok")
    else:
        print("POWER_TICK_FAILED %s" % decode(dll.hsb_last_error()))
    act_rc = int(dll.hsb_power_note_activity(1))
    if act_rc == 1:
        print("POWER_NOTE_ACTIVITY ok")
    else:
        print("POWER_NOTE_ACTIVITY_FAILED %s" % decode(dll.hsb_last_error()))

    # ---- wifi ----
    dll.hsb_wifi_open.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    dll.hsb_wifi_open.restype = ctypes.c_int
    dll.hsb_wifi_adapters.argtypes = [ctypes.c_void_p]
    dll.hsb_wifi_adapters.restype = ctypes.c_void_p
    dll.hsb_free.argtypes = [ctypes.c_void_p]
    dll.hsb_free.restype = None
    dll.hsb_wifi_close.argtypes = [ctypes.c_void_p]
    dll.hsb_wifi_close.restype = None

    handle = ctypes.c_void_p()
    rc = int(dll.hsb_wifi_open(ctypes.byref(handle)))
    if rc != 1 or not handle.value:
        print("WIFI_OPEN_FAILED %s" % decode(dll.hsb_last_error()))
        return 0

    print("WIFI_OPEN_OK")
    try:
        ptr = dll.hsb_wifi_adapters(handle)
        if not ptr:
            print("WIFI_ADAPTERS_FAILED %s" % decode(dll.hsb_last_error()))
        else:
            try:
                text = ctypes.string_at(ptr).decode("utf-8", "replace")
            finally:
                dll.hsb_free(ctypes.c_void_p(ptr))
            print("WIFI_ADAPTERS %s" % text)
    finally:
        dll.hsb_wifi_close(handle)
        print("WIFI_CLOSED")

    # ---- wrapper module (hsf_native) if importable ----
    here = os.path.dirname(os.path.abspath(__file__))
    client_root = os.path.abspath(os.path.join(here, os.pardir))
    if client_root not in sys.path:
        sys.path.insert(0, client_root)
    try:
        from hsfsrv_client import hsf_native
    except Exception as exc:                             # pragma: no cover
        print("WRAPPER_IMPORT_FAILED %s" % exc)
        return 0
    try:
        hsf_native.require()
        print("WRAPPER_AVAILABLE True")
        print("WRAPPER_CAPS %d" % hsf_native.bridge().power_capabilities())
    except Exception as exc:
        print("WRAPPER_AVAILABLE False (%s)" % exc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
