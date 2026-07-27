# XFTP Progress Handoff

Last updated: 2026-07-27 (Asia/Shanghai)
The work was paused after the build verification below; the remaining
active-mode issue is intentionally recorded instead of being reported as
complete.

## Scope

This document records the current state of the untracked XFTP implementation
under `Src/XCode/XNetwork/XFtp/`. The working tree was already dirty before
this work began. Do not reset or discard unrelated changes.

## Implemented and Checked

The following P0 fixes have been applied, primarily in
`Src/XCode/XNetwork/XFtp/XFtp.c`:

- XFtp now registers its deinitializer in the vtable. Creation initializes
  embedded state, buffers, command queue, mutex, error string, listing storage,
  and the PI socket. Destruction releases those resources.
- `XFtpPrivate` has been removed. Its live state is embedded directly in
  `XFtp`, eliminating one heap allocation and one pointer indirection. The
  former `m_transferDevice` mirror was also removed; transfer code uses the
  current command's device directly.
- Mutually exclusive storage is shared with unions: `m_dtpBuffer` and
  `m_readBuffer` are aliases, and the compatibility `m_restOffset` slot
  shares storage with the internal PUT write offset. These fields are not
  accessed concurrently by their respective state machines.
- Boolean state is packed into two `uint8_t` bitfield groups, and the finite
  SSL/REST/MLST state values share one additional byte. The DTP state remains
  separate from socket pointers, and the state-machine values remain separate
  from one another because their lifetimes can overlap. The reconnect timer is
  lazy created only when an automatic reconnect is scheduled.
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

## Verification

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

The latest Windows build completed successfully. The latest full rerun did not
complete all cases: passive FTP cases through APPE passed, active LIST passed,
but active GET timed out while the server had already sent `150` and `226`.
The test was stopped after the queue became blocked.

Verified passing in the latest rerun:

- connect/login/state machine
- FEAT negotiation
- LIST/MLSD with a real listing item
- CWD/CDUP and MKDIR/RMDIR
- PUT/GET, resumed GET, and APPE
- passive FTP transfer paths
- active FTP LIST path

Not yet verified in the latest rerun:

- active FTP GET completion and subsequent queue recovery
- 256 KiB chunked PUT/GET after the active GET timeout
- ABOR, rename/remove, raw commands, SIZE, MDTM, MLST, and 550 classification

The duplicate-MKD association issue was subsequently covered by the Linux
run after the PI/DTP state-machine fixes.

## Current Verification Details

- `cmake --build build --target FtpE2E_Test --config Debug` completed with exit
  code 0 and produced `bin/Debug/FtpE2E_Test.exe`. The build still emits many
  existing project-wide C type warnings.
- The latest plain FTP run reached active `GET` with `current=6` and one
  pending command. The Python server log showed `RETR`, `150`, data send, and
  `226`; the client did not complete the DTP transfer. This is the next fix.
- MODE Z support is present in the source and uses
  `XByteArray_toCompress()`/`XByteArray_toDecompress()` for transfer payloads,
  but a complete MODE Z regression run must be repeated after the active-mode
  fix.
- `FtpE2E_Test --ssl --port 2122` is also 17/17 against a local
  certificate-backed Explicit FTPS server. It covers TLS-protected control
  traffic, `PBSZ 0`, `PROT P`, passive TLS data channels, REST/APPE, 256 KiB
  integrity, ABOR, metadata commands, and 4xx/5xx classification. The active
  FTPS case deliberately passes when the API returns
  `XFtp_Error_ActiveModeFailed`. The temporary certificate was self-signed and
  the test used `XSSL_VerifyNone`; the XFtp default remains
  `XSSL_VerifyPeer`.
- The temporary FTP/FTPS server processes were stopped after verification.
- Earlier Linux verification recorded in this handoff is historical only; re-run
  the FTP, MODE Z, Explicit FTPS, io_uring, and XTcpServer checks after the
  active-mode fix.
- The POSIX io_uring path now preserves completion results, uses the actual
  accepted descriptor from `IORING_OP_ACCEPT`, cancels pending operations
  before close, and keeps TCP-server descriptors separate from `XIODevice`
  descriptors. These changes are shared-contract fixes; no platform API was
  added to XFtp or XGui shared code.
- `XinYueC_Dynamic` startup runs the 15-case `XTcpServer` suite with 15/15
  passing. A menu start/quit smoke test also exits normally.
- `ctest --test-dir build-xalignment` reports no registered CTest cases; the
  executable tests above are the available evidence.

## API Audit and Explicit Limitations

- Every public function declared in `XFtp.h` has a matching implementation in
  `XFtp.c`, including `XFtp_currentCommand`; no empty public function or
  placeholder return was found in the final scan.
- `XFtpCommand_class_init()` was implemented but missing from its header; its
  public declaration and lifetime documentation were added to
  `XFtpCommand.h`. The other `XFtpCommand` APIs document parameters, return
  values, allocation failure, and external-device ownership.
- MODE Z negotiation and payload handling are implemented. FEAT detection and
  `MODE Z` negotiation are handled out of band; PUT/APPE payloads are compressed
  with `XByteArray_toCompress()`, and GET/LIST payloads are decompressed with
  `XByteArray_toDecompress()`. Full end-to-end coverage remains pending.
- Active FTPS is intentionally unsupported because the portable socket API has
  no safe descriptor-adoption path for wrapping an accepted socket in TLS.
  Explicit FTPS passive mode is implemented and tested; active FTPS fails
  explicitly with `XFtp_Error_ActiveModeFailed`.
- Certificate-chain and hostname-verification fixtures remain a test-system
  enhancement. The current FTPS fixture verifies encrypted I/O with a
  self-signed certificate and `XSSL_VerifyNone`; production callers should use
  the default `XSSL_VerifyPeer`.

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

Continuation priority:

1. Fix active FTP GET DTP completion. Inspect the ordering of the accepted
   socket's `readyRead`/`disconnected` events and `m_waitForDtpToClose`,
   `m_piAckedTransfer`, and `m_dtpState` in `XFtp.c`.
2. Rebuild and run plain FTP, `FtpE2E_Test --mode-z`, and Explicit FTPS + MODE Z.
3. Only then update the pass counts and remove this active-mode limitation.

## Files of Interest

- `Src/XCode/XNetwork/XFtp/XFtp.c`
- `Src/XCode/XNetwork/XFtp/XFtp.h`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.c`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.h`
- `ftp_e2e_test.c`
- `ftp_test_server.py`
- `Test/XIOTest/XNetworkTest/XFtpTest.c`
