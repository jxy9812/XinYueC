# XFTP Progress Handoff

Last updated: 2026-07-28 (Asia/Shanghai)
The active FTP GET completion issue, FTPS data-handshake re-entry issue, and
IPv6 active-mode endpoint issue have been fixed. The complete local IPv4/IPv6
FTP/FTPS end-to-end matrix has passed on the POSIX backend.

## Scope

This document records the current state of the XFTP implementation under
`Src/XCode/XNetwork/XFtp/` and its required POSIX/Windows socket backends.
Preserve unrelated working-tree changes when continuing this work.

## 2026-07-28 lwIP TAP/DHCP Follow-up

The lwIP backend is selected with `-DXNETWORK_USE_LWIP`; its POSIX transport
uses the existing `lwip0` TAP device and DHCP. In an isolated TAP namespace
with the host-side address `192.168.200.1/24`, the client acquired
`192.168.200.2` through DHCP and completed these full 17-case matrices:

- FTP over DHCP/TAP, including passive EPSV and active PORT/EPRT-style
  endpoint handling;
- FTP + MODE Z over DHCP/TAP;
- FTP against a fixture which advertises EPSV then rejects it with `502`,
  proving the IPv4 PASV retry path.

The lwIP implementation now coalesces chained pbufs into one TAP Ethernet
frame, leaves lwIP's built-in loopback as the only loopback interface, and
returns TAP as the default external netif. The generic socket read event also
releases the backend read buffer before emitting `readyRead`, preventing a
consumer from receiving the same control reply twice. XFtp counts queued and
acknowledged PUT bytes separately and accepts only `225`/`226` as a transfer's
final PI reply, so a delayed non-transfer `250` cannot complete a later PUT.

The repeated lwIP explicit-FTPS failure is resolved. The apparent fifth TLS
channel failure was a late close/error event from the completed DTP socket:
because its FTP signals remained connected until the next passive setup, that
old event failed the new PUT before its `229` response was processed. Completed
DTP sockets now detach all FTP-facing signals immediately while preserving the
existing deferred object destruction. A second large-upload issue was fixed by
mapping lwIP's full send window and `ERR_MEM` result to a retryable zero-byte
write, allowing the TLS BIO to return `WANT_WRITE` instead of mbedTLS internal
error `-0x6c00`.

The public error contract is now explicit for local precondition failures:
operations on an unconnected client return `XFtp_Error_NotConnected`, commands
issued on a connected but unauthenticated client return `XFtp_Error_NotLoggedIn`,
and duplicate queued login/close requests return `XFtp_Error_OperationInProgress`.
Directory-list parsing no longer silently drops malformed MLSD/MLST entries:
the command completes with `XFtp_Error_DirectoryListingFailed`. Traditional
Unix `LIST` summary lines such as `total 8` remain accepted.

`ftp_e2e_test.c` now verifies all four local error states as part of normal
execution: unconnected list/login/close, connected-but-unauthed list, duplicate
login, and duplicate close. The fixture accepts `--malformed-listing`, and
`FtpE2E_Test --expect-list-failure` proves that a malformed MLSD data line
produces `XFtp_Error_DirectoryListingFailed` rather than a silent omission.

The lwIP Raw API teardown now unregisters every TCP callback before closing a
PCB. An established `tcp_close()` can defer final PCB release until a later ACK;
without this, an ACK could call into an already freed socket-private object.

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
- Passive FTPS starts its data-channel TLS handshake exactly once when the
  socket changes from connecting to connected. `XSslSocket_startClientEncryption`
  is also idempotent while a handshake is pending or encryption is complete.
  This prevents repeated connected notifications from re-entering the same
  mbedTLS context and producing spurious `-0x6c00` internal errors.
- Transfer completion now waits for both the final PI reply and DTP closure so
  late DTP data is not discarded.
- Passive DTP connection startup now consumes `m_waitForDtpToConnect` before
  it emits the transfer command. Repeated connected notifications can no
  longer send a duplicate `STOR` or `RETR` after the server has closed its
  data channel.
- The common error path no longer assigns `m_errorString` from its own UTF-8
  cache. This preserves the public error text for synchronous data-channel
  failures, including failed active FTP/FTPS connection attempts.
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
- Active FTPS now uses an `XTcpServer` incoming-socket factory. The accepted
  descriptor is bound to an `XSslSocket` on its first adoption, then the FTP
  client performs the PROT P handshake as TLS client. This avoids unsafe
  descriptor transfer between independently owning socket objects.
- Active data commands now wait for both the PORT/EPRT `200` response and a
  ready TCP/TLS channel before sending LIST, RETR, or STOR. This prevents a
  fast lwIP TLS receive event from reaching the DTP completion state before
  the control channel has entered its transfer phase.
- MODE Z is negotiated after FEAT and uses the portable compressed DTP path
  for uploads, downloads, and directory listings.
- Active FTP listeners are detached from accepted DTP sockets and released at
  transfer completion, abort, and object teardown. Repeated active transfers
  no longer leave an `XTcpServer` alive for each command.
- FEAT state is reset for every new control-session connection. EPSV and EPRT
  now have distinct feature bits; an IPv6 active transfer requires EPRT rather
  than assuming that EPSV implies it.
- When an IPv4 server advertises EPSV but rejects its use, XFtp discards the
  EPSV capability for that session and retries the same passive setup with
  PASV. Explicit IPv6 literals do not take this IPv4-only fallback.
- Outbound socket connections now synchronize their local and peer endpoints
  into `XAbstractSocket` once connection succeeds. The POSIX and Windows
  backends both do this for their asynchronous connect completions; without
  it, XFtp could not see an IPv6 PI local address and active mode fell back to
  an IPv4 `PORT` listener.
- POSIX `XTcpServer` now selects `AF_INET` or `AF_INET6` from the requested
  listen address and reports a bound IPv6 port correctly. XFtp binds its
  active DTP listener to the PI socket's local address, so IPv6 active FTP
  negotiates and exercises EPRT end to end.

Related test corrections:

- `ftp_e2e_test.c` and `Test/XIOTest/XNetworkTest/XFtpTest.c` now read
  `listInfo` through the framework's `(XObject*, XVarList*)` callback ABI.
  Their prior `(XFtp*, XFileInfo*)` callback signatures interpreted an
  `XVarList*` as an `XFileInfo*`, producing invalid size/type values.
- The E2E LIST assertion now requires at least one item.
- `ftp_test_server.py` was adjusted so a duplicate `MKD` uses
  `exist_ok=False`, matching normal FTP failure semantics.
- `ftp_test_server.py --reject-epsv` deliberately advertises EPSV then returns
  `502` for the command. Its passing E2E run covers the client-side PASV
  fallback rather than merely the nominal EPSV path.
- The active-FTPS E2E case now performs protected active LIST, GET, and PUT;
  the uploaded payload is downloaded over a passive channel for byte-for-byte
  verification.
- The E2E client and local test server accept `--host <address>`. The fixture
  supports IPv6 control/passive sockets and EPRT data connections; IPv6 PASV
  deliberately returns `522`, requiring EPSV as FTP specifies.

## Verification

Build command:

```bash
cmake --build build --target FtpE2E_Test -j2
```

The build completed with exit code 0. It still emits existing project-wide C
type warnings; no new build error was introduced.

The following local, real-server configurations each completed all 17 E2E
cases with exit code 0:

- FTP: `./bin/FtpE2E_Test`
- FTP + MODE Z: `./bin/FtpE2E_Test --mode-z`
- Explicit FTPS: `./bin/FtpE2E_Test --ssl --port 2122`
- Explicit FTPS + MODE Z: `./bin/FtpE2E_Test --ssl --mode-z --port 2122`
- EPSV-rejecting FTP fixture: `python3 ftp_test_server.py --reject-epsv`, then
  `./bin/FtpE2E_Test`
- IPv6 FTP: `python3 ftp_test_server.py --host ::1`, then
  `./bin/FtpE2E_Test --host ::1`
- IPv6 FTP + MODE Z: `./bin/FtpE2E_Test --host ::1 --mode-z`
- IPv6 Explicit FTPS: start the TLS fixture with `--host ::1 --port 2122`,
  then `./bin/FtpE2E_Test --ssl --host ::1 --port 2122`
- IPv6 Explicit FTPS + MODE Z:
  `./bin/FtpE2E_Test --ssl --host ::1 --mode-z --port 2122`

Each run covers login and FEAT negotiation; MLSD; CWD/CDUP; MKDIR/RMDIR;
PUT/GET including empty upload; REST resume; APPE; active FTP/FTPS LIST, GET,
and PUT;
256 KiB upload/download integrity; ABOR recovery; rename/remove; raw commands;
SIZE, MDTM, MLST; and 550 error classification. The IPv6 explicit-FTPS run
specifically proves EPRT LIST, GET, and PUT. FTPS additionally verifies
`AUTH TLS`, `PBSZ 0`, `PROT P`, and protected passive and active data
channels.

The EPSV-rejecting fixture also completed 17/17. It exercised passive
LIST/GET/PUT and the normal active FTP cases after an advertised EPSV command
was rejected with `502`, proving that the next passive setup uses PASV.

The local FTPS fixture used a temporary self-signed certificate and
`XSSL_VerifyNone`; the XFtp default remains `XSSL_VerifyPeer`. The temporary
FTP/FTPS server processes and generated test files were removed after testing.
Both current FTPS runs completed without the former mbedTLS `-0x6c00` output.
An additional three consecutive Explicit FTPS runs each completed 17/17 with
no `mbedtls err`, failed-case, or fatal marker in their logs.

The IPv4 and IPv6 FTP/FTPS/FTP+MODE Z/FTPS+MODE Z E2E variants all completed
17/17 on Linux. The Windows endpoint-synchronization code follows the existing
Windows socket APIs, but it has not been runtime-tested in this Linux-only
environment.

The current backend-specific regression evidence is:

- POSIX platform backend: FTP and FTP + MODE Z both completed 17/17 after the
  shared read and transfer-state changes.
- lwIP TAP/DHCP backend: FTP, FTP + MODE Z, and EPSV-reject/PASV-fallback each
  completed 17/17 after a three-second DHCP settle period.
- lwIP explicit FTPS and explicit FTPS + MODE Z both completed 17/17. The
  clean verification logs contain no mbedTLS read/write/internal errors.

On 2026-07-28, after the local-error/listing and lwIP TCP-teardown changes,
the POSIX platform backend again completed FTP, FTP + MODE Z, Explicit FTPS,
and Explicit FTPS + MODE Z at 17/17 each. The malformed-listing negative E2E
case also completed with `XFtp_Error_DirectoryListingFailed`.

The lwIP runtime matrix was then re-run from scratch through TAP/DHCP. The
temporary host-side TAP endpoint was `192.168.200.1/24`; the embedded lwIP
client acquired `192.168.200.2` through DHCP. FTP, FTP + MODE Z, Explicit
FTPS, and Explicit FTPS + MODE Z each completed 17/17, including active FTP
PORT transfers that advertised the DHCP lease address. The malformed-listing
negative case also produced `XFtp_Error_DirectoryListingFailed`. These runs
contained no segfault, lwIP callback-state assertion, or mbedTLS internal
read/write error. The original crash was a real lwIP lifecycle bug: an ACK
could invoke a Raw API callback after its socket-private object was released;
TCP callbacks are now detached before close and the PCB is aborted when a
graceful close cannot be accepted.

## 2026-07-28 Active FTPS Completion

Active FTPS is implemented without descriptor hand-off: `XTcpServer` now
accepts an optional socket factory, and XFtp configures it to create an
`XSslSocket` before the accepted descriptor is adopted. Active data-command
dispatch is two-phase: TYPE must succeed before PORT/EPRT is sent, and the
transfer command waits until both PORT/EPRT and the client-side data TLS
handshake have completed. This removes the lwIP re-entrancy window where a
TLS data record could arrive before the control-plane `150` state existed.

Final runtime evidence, using the generated FTP fixture and a real TAP/DHCP
lease of `192.168.200.2`, is:

- POSIX IPv4: FTP, FTP + MODE Z, Explicit FTPS, and Explicit FTPS + MODE Z
  each completed 17/17. The active case performed LIST, GET, and PUT.
- POSIX IPv6: Explicit FTPS completed 17/17, including EPRT protected
  LIST, GET, and PUT.
- lwIP TAP/DHCP: FTP, FTP + MODE Z, Explicit FTPS, and Explicit FTPS + MODE Z
  each completed 17/17 after DHCP. The active FTPS case performed protected
  PORT LIST, GET, and PUT with no mbedTLS internal error or lwIP assertion.

The Windows backend compiles through the shared API change but has not been
runtime-tested in this Linux environment.

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
  `XByteArray_toDecompress()`. The FTP/FTPS and MODE Z E2E matrix covers this
  behavior end to end.
- Active FTPS is implemented and tested on POSIX IPv4/IPv6 and lwIP TAP/DHCP.
  `XFtp_Error_ActiveModeFailed` now denotes a real PORT/EPRT listener,
  reverse-connection, or protected active-data-channel failure.
- Desktop FTPS now has a CA-backed `XSSL_VerifyPeer` positive case and a
  peer-name mismatch negative case. The self-signed `XSSL_VerifyNone` fixture
  remains useful for encrypted-I/O coverage, while production callers should
  retain the default `XSSL_VerifyPeer`.

## Future Work

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

1. Run the endpoint-synchronization and FTP matrix on Windows.

## Files of Interest

- `Src/XCode/XNetwork/XFtp/XFtp.c`
- `Src/XCode/XNetwork/XFtp/XFtp.h`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.c`
- `Src/XCode/XNetwork/XFtp/XFtpCommand.h`
- `Src/XCode/XNetwork/XSsl/XSslSocket.c`
- `Drive/Posix/XNetwork/XNetwork_posix.c`
- `Drive/windows/XNetwork/XNetwork_win32.c`
- `Drive/Posix/XNetwork/XNetwork_lwip_posix.c`
- `Library/lwip/platform/XNetwork_lwip.c`
- `ftp_e2e_test.c`
- `ftp_test_server.py`
- `Test/XIOTest/XNetworkTest/XFtpTest.c`
