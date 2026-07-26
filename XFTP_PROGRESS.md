# XFTP Progress Handoff

Last updated: 2026-07-27 (Asia/Shanghai)

## Scope

This document records the current state of the untracked XFTP implementation
under `Src/XCode/XNetwork/XFtp/`. The working tree was already dirty before
this work began. Do not reset or discard unrelated changes.

## Implemented and Checked

The following P0 fixes have been applied, primarily in
`Src/XCode/XNetwork/XFtp/XFtp.c`:

- XFtp now registers its deinitializer in the vtable. Creation initializes
  private state, buffers, command queue, mutex, error string, listing storage,
  and the PI socket. Destruction releases those resources.
- The control socket is always created as `XSslSocket`, removing the unsafe
  runtime cast from `XTcpSocket*` to `XSslSocket*` when explicit FTPS starts.
- Explicit FTPS control-channel flow was added: `AUTH TLS`, TLS handshake,
  `PBSZ 0`, `PROT P`, then normal queued commands. Passive FTPS data sockets
  are also created as `XSslSocket` and wait for TLS before transfer starts.
- Transfer completion now waits for both the final PI reply and DTP closure so
  late DTP data is not discarded.
- LIST/MLSD data is parsed before the command object is removed. Previously
  the command was cleared first, making directory parsing unreachable.
- MLSD/MLST filename parsing now follows RFC 3659: facts precede the first
  space and the pathname follows it; filenames are not expected in a `name=`
  fact.
- `rawCommandReply` now emits the complete response line, for example
  `213 44`, so `SIZE` and `MDTM` callers can parse the documented result.
- Zero-length memory uploads close the DTP socket after the `150` reply.
- PI and DTP socket failures now complete the active command instead of
  leaving the command queue permanently blocked.

Related test corrections:

- `ftp_e2e_test.c` and `Test/XIOTest/XNetworkTest/XFtpTest.c` now read
  `listInfo` through the framework's `(XObject*, XVarList*)` callback ABI.
  Their prior `(XFtp*, XFileInfo*)` callback signatures interpreted an
  `XVarList*` as an `XFileInfo*`, producing invalid size/type values.
- The E2E LIST assertion now requires at least one item.
- `ftp_test_server.py` was adjusted so a duplicate `MKD` uses
  `exist_ok=False`, matching normal FTP failure semantics.

## Verification Performed on Windows

Build command:

```powershell
cmake --build build --target FtpE2E_Test --config Debug
```

The build completed successfully. It still produces many pre-existing C type
warnings across the project; no new build errors were introduced.

The local server was started with:

```powershell
python ftp_test_server.py
```

Latest E2E run result: 16/17 passed.

Verified passing E2E coverage:

- connect/login/state machine
- FEAT negotiation
- LIST/MLSD with a real listing item
- CWD/CDUP and MKDIR/RMDIR
- PUT/GET, resumed GET, and APPE
- passive and active FTP transfer paths
- 256 KiB chunked PUT/GET integrity
- ABOR followed by continued connection use
- rename/remove and raw commands
- SIZE, MDTM, and MLST
- 550 error classification for missing SIZE/DELE/RMD targets

The remaining E2E failure is the duplicate-MKD assertion. The test observed
`257` where its second MKD expects `550`, even after the server was changed to
`exist_ok=False`. Treat this as an unresolved test synchronization or response
association issue until it is reproduced with PI command/reply logging. Do
not mark it as a server-only issue without that trace.

## Remaining Work

The XFTP module is not yet feature-complete. Prioritize these before calling
it complete:

1. Fix and fully verify duplicate-MKD E2E response association. Add command
   IDs or PI tracing to prove that `wait_cmd` observes the reply for the
   command it just enqueued.
2. Resolve active-mode FTPS. `XTcpServer_nextPendingConnection_base()` returns
   an `XTcpSocket`; active FTPS must not cast that object to `XSslSocket`.
   Either implement safe descriptor adoption into an SSL socket or reject
   active FTPS explicitly with a clear error until supported.
3. Implement or explicitly reject MODE Z compression. `XFtp_setCompression()`
   exists but the protocol negotiation and compression data path are absent.
4. Complete reconnect behavior: after a reconnect, restore the necessary
   authentication/session state and safely recover or fail queued commands.
5. Audit every public declaration in `Src/XCode/XNetwork/XFtp/XFtp.h` and
   `XFtpCommand.h`. Several API declarations still lack complete Doxygen
   `@param` and return/error/lifetime documentation.
6. Add FTPS E2E coverage with a local certificate-backed server. The current
   E2E suite validates plain FTP only.

## Linux Continuation

Suggested clean build directory and commands:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target FtpE2E_Test -j
python3 ftp_test_server.py
./bin/Debug/FtpE2E_Test
```

The exact executable path may differ by generator. Use `find build-linux -name
'FtpE2E_Test*'` if the last command cannot locate it.

Before running the E2E test, ensure TCP port 2121 is free and that the Python
server is using the repository root as its working directory. The test writes
temporary files under `ftp_test_root/` and may leave generated files such as
`xftp_dl_*.bin` and `xftp_resume_local.bin` in the repository root.

## Files of Interest

- `Src/XCode/XNetwork/XFtp/XFtp.c`
- `Src/XCode/XNetwork/XFtp/XFtp.h`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.c`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.h`
- `ftp_e2e_test.c`
- `ftp_test_server.py`
- `Test/XIOTest/XNetworkTest/XFtpTest.c`
