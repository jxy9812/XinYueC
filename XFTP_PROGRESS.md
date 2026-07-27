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
- The DTP state no longer shares storage with the passive target pointer. The
  former union could interpret state values as heap pointers during teardown.
- Automatic reconnect now uses `XTimer`, restores the saved login sequence,
  avoids blocking the event thread, and fails commands queued while the
  session is unavailable.
- Active FTPS is explicitly rejected with a portable error because the shared
  socket API has no safe descriptor-adoption operation for an accepted
  `XTcpSocket`.
- MODE Z is explicitly rejected until a portable compressed DTP path exists;
  `XFtp_setCompression()` no longer silently advertises unsupported behavior.

Related test corrections:

- `ftp_e2e_test.c` and `Test/XIOTest/XNetworkTest/XFtpTest.c` now read
  `listInfo` through the framework's `(XObject*, XVarList*)` callback ABI.
  Their prior `(XFtp*, XFileInfo*)` callback signatures interpreted an
  `XVarList*` as an `XFileInfo*`, producing invalid size/type values.
- The E2E LIST assertion now requires at least one item.
- `ftp_test_server.py` was adjusted so a duplicate `MKD` uses
  `exist_ok=False`, matching normal FTP failure semantics.

## Historical Windows Verification

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

The recorded Windows run from the earlier handoff reported 16/17 passed. The
Linux runs below are the current verification for the repaired state.

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

The duplicate-MKD association issue was subsequently covered by the Linux
run after the PI/DTP state-machine fixes.

## Current Linux Verification

- `XGuiRegression_Test` builds and passes. It covers uninitialized pixmap
  copy, affine transform dimensions, icon size enumeration, and BMP reader /
  writer device I/O.
- `FtpE2E_Test` builds and exits normally. All 17 real-server cases pass,
  including passive and active transfers, 256 KiB chunked PUT/GET, REST,
  APPE, ABOR recovery, SIZE, MDTM, MLST, and 550 error classification.
- `FtpE2E_Test --ssl` passes all 17 cases against the local certificate-backed
  Explicit FTPS server. This covers TLS-protected control traffic, `PBSZ 0`,
  `PROT P`, passive TLS data channels, REST/APPE, 256 KiB integrity, ABOR,
  metadata commands, and 4xx/5xx classification. The active FTPS case is a
  deliberate pass when the portable API returns `XFtp_Error_ActiveModeFailed`.
  The test certificate is self-signed, so this test selects
  `XSSL_VerifyNone`; the XFtp default remains `XSSL_VerifyPeer`.
- The POSIX io_uring path now preserves completion results, uses the actual
  accepted descriptor from `IORING_OP_ACCEPT`, cancels pending operations
  before close, and keeps TCP-server descriptors separate from `XIODevice`
  descriptors. These changes are shared-contract fixes; no platform API was
  added to XFtp or XGui shared code.
- `XinYueC_Dynamic` startup runs the 15-case `XTcpServer` suite with 15/15
  passing. A menu start/quit smoke test also exits normally.
- `ctest --test-dir build-xalignment` reports no registered CTest cases; the
  executable tests above are the available evidence.

## Remaining Work

The XFTP module is not yet feature-complete. Prioritize these before calling
it complete:

1. Audit every public declaration in `Src/XCode/XNetwork/XFtp/XFtp.h` and
   `XFtpCommand.h`. Several API declarations still lack complete Doxygen
   `@param` and return/error/lifetime documentation.
2. Add certificate-chain and hostname-verification fixtures for FTPS E2E;
   the current self-signed fixture intentionally exercises encrypted I/O with
   peer verification disabled.

## Linux Continuation

Suggested clean build directory and commands:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target FtpE2E_Test XGuiRegression_Test -j
python3 ftp_test_server.py
./bin/Debug/FtpE2E_Test
# For Explicit FTPS, start the server with --tls --cert <cert> --key <key>
# and run: ./bin/Debug/FtpE2E_Test --ssl --port 2122
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
