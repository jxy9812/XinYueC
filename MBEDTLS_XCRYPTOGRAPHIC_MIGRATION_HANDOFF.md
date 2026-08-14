# mbedTLS to XCryptographic Migration Handoff

## Historical Handoff (2026-08-13)

> 本节为历史记录。当前真实状态以文档末尾的“PSA 目录最终清理与可插拔后端”批次为准。

### PSA framework removed from mbedTLS; PSA API now fully in-tree (2026-08-13, completed)

The `Library/mbedtls/psa-crypto` directory (the migrated tf-psa-crypto tree)
has been **deleted** from the repository. mbedTLS no longer compiles or links
any source from a separate `psa-crypto`/`tf-psa-crypto` tree. The PSA-compatible
API and all legacy algorithm adapters now live inside mbedTLS itself and route
to `Src/XCode/XCryptographic`:

- `Library/mbedtls/library/psa/` — PSA dispatch/core adapters (`psa_*` API,
  key slots, random, storage, client, driver wrappers) that call
  `mbedtls_psa_*` adapters, which in turn call XCryptographic.
- `Library/mbedtls/library/legacy/` — legacy mbedTLS algorithm compatibility
  layer copied from the former builtin driver sources (cipher, bignum, GCM,
  CCM, ChaCha20/Poly1305, ARIA/Camellia, etc.) and legacy `nist_kw.c`.
- `Library/mbedtls/platform/` — platform glue (random, file, memory, LMS).

`Library/mbedtls/CMakeLists.txt` now globs only `platform/*.c`,
`library/psa/*.c`, `library/legacy/*.c` and `library/*.c`; no `psa-crypto/*`
paths remain in the build.

Key fix in this batch:

- `Library/mbedtls/library/legacy/nist_kw.c` now reads transparent AES key
  bytes directly from the in-memory PSA key slot
  (`psa_get_and_lock_key_slot` + `psa_unregister_read_under_mutex`) instead of
  `psa_export_key`. This preserves the original AES-KW/KWP behavior for keys
  that only carry `PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT` (no
  `EXPORT`), fixing the `AES-KW 封装向量不匹配` regression seen after the
  first in-tree port.

Verification after deletion:

- `git rm -r -f Library/mbedtls/psa-crypto` staged the removal of the 352
  migrated files.
- Re-configured and performed a clean rebuild (`cmake --build build --target
  clean` then full build). The build directory now contains no `psa-crypto`
  or `tf-psa-crypto` object files.
- Static and dynamic `./bin/XinYueC_Static --test all` /
  `./bin/XinYueC_Dynamic --test all` both exit `0` and report
  `[RESULT] XCryptographic primitives: PASS` and
  `[RESULT] XSsl tests: PASS`.

Remaining (optional, not required for the dependency removal):

- `library/psa/psa_crypto_driver_wrappers.h` still retains the upstream
  auto-generated test/p256/PQCP driver branches inside `#if` guards that are
  disabled by default. They are not compiled and do not affect the build.
  A future cleanup can strip those branches to keep only the in-tree
  `mbedtls_psa_*` direct dispatch.

### ML-DSA-87 optional build restored after PSA removal (2026-08-13, completed)

After `Library/mbedtls/tf-psa-crypto` was deleted, the optional ML-DSA-87
build (`build-mldsa`, `build-mldsa-pqcp`) broke because the wrapper still
included the mldsa-native single-CU sources from the deleted PQCP directory.
The third-party implementation is now owned by the mbedTLS backend:

- Restored `Library/mbedtls/library/xcryptographic/third_party/mldsa-native/` (68 files:
  `mldsa_native.c/.h`, `mldsa_native_config.h`, `src/...`) from the former
  PQCP path.
- `Library/mbedtls/library/xcryptographic/XCryptographic_mldsa87.c` includes
  `third_party/mldsa-native/mldsa_native.h` and
  `third_party/mldsa-native/mldsa_native.c` relative to the mbedTLS backend
  directory. The nested third-party `.c` files are not globbed independently;
  they are compiled only through this wrapper.

Verification:

- `build-mldsa` (XCRYPTOGRAPHIC_ENABLE_MLDSA87=ON) rebuilds successfully and
  `XinYueC_Static --test xcryptographic` reports
  `[PASS] XCryptographic: ML-DSA-87 FIPS 204 keygen/sign/verify` and
  `[RESULT] XCryptographic primitives: PASS`.
- `build-mldsa-pqcp` (also MBEDTLS_ENABLE_PQCP=ON) rebuilds successfully and
  the same ML-DSA-87 test passes.
- Default `build` reconfigures and rebuilds successfully; static and dynamic
  `--test all` both exit `0` with `[RESULT] XCryptographic primitives: PASS`
  and `[RESULT] XSsl tests: PASS`.

### `tf-psa-crypto` directory removed; PSA framework relocated (2026-08-13, completed)


### `tf-psa-crypto` directory removed; PSA framework relocated (2026-08-13, completed)

The `Library/mbedtls/tf-psa-crypto` directory has been **removed** from the
tree. It was migrated in place to `Library/mbedtls/psa-crypto` (including the
top-level `include/tf-psa-crypto` -> `include/psa-crypto` header directory).
The old `tf-psa-crypto` path no longer exists and is no longer referenced by
the build.

- `git mv Library/mbedtls/tf-psa-crypto Library/mbedtls/psa-crypto`
- `git mv Library/mbedtls/psa-crypto/include/tf-psa-crypto Library/mbedtls/psa-crypto/include/psa-crypto`
- Updated all remaining `tf-psa-crypto` path references to `psa-crypto` in
  `Library/mbedtls/CMakeLists.txt`, `Library/mbedtls/library/mbedtls_config.c`,
  `Library/mbedtls/include/mbedtls/build_info.h`,
  `Src/XCode/XCryptographic/XCryptographic_mldsa87.c`, and the migrated tree
  itself (80 files, `tf-psa-crypto` -> `psa-crypto`; the underscore identifier
  `tf_psa_crypto` was left unchanged). The nested optional-driver include dirs
  `drivers/everest/include/tf-psa-crypto` and
  `drivers/pqcp/include/tf-psa-crypto` were also renamed to `psa-crypto` so no
  `tf-psa-crypto` directory path remains in the tree.
- Default `build` re-configured and rebuilt successfully with
  `cmake --build build -j$(nproc)`.
- Focused static/dynamic `--test xcryptographic` and `--test xssl` exit `0`
  with `[RESULT] XCryptographic primitives: PASS` and
  `[RESULT] XSsl tests: PASS`.
- Full static and dynamic `--test all` pass all crypto and XSsl tests
  (`[RESULT] XCryptographic primitives: PASS`, `[RESULT] XSsl tests: PASS`;
  112 `[PASS]` lines plus 2 `[RESULT]` lines each). The exit code can be `1`
  only when the pre-existing dirty XConsoleShell `login testadmin` account
  assertion triggers (`账户尚未设置密码，请先执行 passwd`, result `-4`,
  expected `1`); this is unrelated to the crypto migration.
- `git diff --check` is clean.

Logs:

- `/tmp/build-after-rename.log`
- `/tmp/post-rename-static-xcrypto.log`, `/tmp/post-rename-dynamic-xcrypto.log`
- `/tmp/post-rename-static-xssl.log`, `/tmp/post-rename-dynamic-xssl.log`
- `/tmp/post-rename-static-all.log`, `/tmp/post-rename-dynamic-all.log`
- `/tmp/final-static-all.log`, `/tmp/final-dynamic-all.log` (re-run after the
  nested optional-driver include dirs were also renamed to `psa-crypto`)

The PSA framework itself is still required by mbedTLS and remains at
`Library/mbedtls/psa-crypto`; it must not be deleted. Only the old
`tf-psa-crypto` directory path has been retired.

### Verified This Session

- Added XCryptographic AES-XTS (including ciphertext stealing), HMAC-DRBG,
  CTR-DRBG through PSA AES-ECB, and LMS/LM-OTS public verification.
- `MBEDTLS_USE_XCRYPTOGRAPHIC_LMS` now excludes legacy LMS sources only while
  `MBEDTLS_LMS_PRIVATE` remains disabled. If the experimental stateful private
  signing option is enabled later, CMake keeps the mbedTLS implementation so
  the build has no missing symbols.
- Added optional ML-DSA-87 in `Src/XCode/XCryptographic`:
  `XCryptographic_mldsa87.c`, its configuration header, and `zetas.inc`.
  Public API supports deterministic seed key generation, context signing, and
  verification. `XCRYPTOGRAPHIC_ENABLE_MLDSA87` defaults to `OFF`.
- Optional `build-mldsa` passed static and dynamic `--test xcryptographic`,
  including ML-DSA-87 keygen/sign/verify and altered-signature rejection.
- `build-mldsa-pqcp` with `MBEDTLS_ENABLE_PQCP=ON` and
  `XCRYPTOGRAPHIC_ENABLE_MLDSA87=ON` built successfully. Its mbedTLS archive
  contains no old `mldsa-native`, `wrap_mldsa_native`, or
  `psa_crypto_mldsa` object. PQCP's ML-DSA PSA configuration is still disabled
  upstream, so no PSA ML-DSA API was invented.
- Default `build` was restored and built successfully after optional testing.
- Default focused tests all pass:
  - static/dynamic `--test xcryptographic`
  - static/dynamic `--test xssl`
- Default dynamic `--test all` exits `0`. The crypto suite, XSsl, and local
  XSslSocket TLS loopback all pass.
- Default static `--test all` exits `1` only because the pre-existing dirty
  XConsoleShell test expects `login testadmin` to re-prompt but receives
  `账户尚未设置密码，请先执行 passwd` (`result=-4`, expected `1`). The same run
  reports `[RESULT] XCryptographic primitives: PASS` and
  `[RESULT] XSsl tests: PASS`.

Recent logs:

- `/tmp/xcryptographic-default-after-mldsa-static.log`
- `/tmp/xcryptographic-default-after-mldsa-dynamic.log`
- `/tmp/xssl-default-after-mldsa-static.log`
- `/tmp/xssl-default-after-mldsa-dynamic.log`
- `/tmp/xinyuec-static-all-after-mldsa.log`
- `/tmp/xinyuec-dynamic-all-after-mldsa.log`

### Verified This Session (continued)

- Completed the legacy `mbedtls_md_*` / HMAC compatibility routing:
  - `mbedtls_md_setup`, `mbedtls_md_starts`, `mbedtls_md_update`,
    `mbedtls_md_finish`, `mbedtls_md`, `mbedtls_md_clone`,
    `mbedtls_md_hmac_setup`, starts/update/finish/reset now use
    `XCryptographicHash` through `MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_MD`
    (default `ON`).
  - Added `xssl_test_legacy_md_backend` in `Test/XIOTest/XSslTest.c`
    covering segmented SHA-256, one-shot digest, clone, and RFC 4231
    HMAC-SHA-256 setup/start/update/finish/reset. The file defines
    `MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS` so the HMAC declarations are
    visible.
  - Static and dynamic `--test xssl` pass with the new test.
- Old direct hash sources (`md5.c`, `sha1.c`, `sha256.c`, `sha512.c`,
  `sha3.c`, `ripemd160.c`) are now excluded from the mbedTLS archive when
  `MBEDTLS_USE_XCRYPTOGRAPHIC_LEGACY_HASH` (default `ON`) is combined with
  `LEGACY_MD`, `HASH`, and `XOF`. The source files remain in the tree and are
  compiled again if any routing switch is turned off.
- Confirmed `build/Library/mbedtls/libmbedtlsd.a` no longer contains those
  old hash objects.
- Default static and dynamic `--test xcryptographic` and `--test xssl` pass.
- Default static `--test all` exits 0 on this run; default dynamic `--test all`
  still exits 1 due to the unrelated dirty XConsoleShell/XProcess
  `useradd testuser` failure. The crypto and XSsl `[RESULT]` lines pass in
  both full runs.

Recent logs:

- `/tmp/xinyuec-static-all-after-legacy-hash.log`
- `/tmp/xinyuec-dynamic-all-after-legacy-hash.log`
- `/tmp/xssl-static-after-legacy-md.log`
- `/tmp/xssl-dynamic-after-legacy-md.log`

### Resume Verification (2026-08-13, after pause)

Resumed the task by re-reading this handoff and re-verifying the current
worktree. No new code changes were made in this continuation; the previous
session was already in a verified, buildable state.

- Default `build` reconfigured/rebuilt successfully with
  `cmake --build build -j$(nproc)`.
- Static and dynamic `--test xcryptographic` both exit `0` and report
  `[RESULT] XCryptographic primitives: PASS`.
- Static and dynamic `--test xssl` both exit `0` and report
  `[RESULT] XSsl tests: PASS`.
- `git diff --check` is clean.
- All `MBEDTLS_USE_XCRYPTOGRAPHIC_*` switches default to `ON` in
  `Library/mbedtls/CMakeLists.txt`; `XCRYPTOGRAPHIC_ENABLE_MLDSA87` defaults
  to `OFF`.

### Full-suite current (2026-08-13, completed)

Ran the current default build through the full static and dynamic suites.

- Default `build` rebuilt cleanly with `cmake --build build -j$(nproc)`.
- `git diff --check` is clean.
- `ar t build/Library/mbedtls/libmbedtlsd.a` contains no legacy algorithm
  objects (`md5|sha1|sha256|sha512|sha3|ripemd160|ecp|ecdsa|rsa|aes|chacha20|
  poly1305|ccm|gcm|camellia|aria|nist_kw|hmac|cmac|lms|lmots|ecjpake`).
  The only remaining `mbedtls_ecdsa_*` text symbols are the ASN.1 signature
  conversion helpers `mbedtls_ecdsa_der_to_raw`/`raw_to_der` in the PK layer.
- Static `./bin/XinYueC_Static --test all` exits `0`.
- Dynamic `./bin/XinYueC_Dynamic --test all` exits `0`.
- Both runs report `[RESULT] XCryptographic primitives: PASS` and
  `[RESULT] XSsl tests: PASS` (113 `[PASS]` lines plus 2 `[RESULT]` lines), including the local
  TLS 1.2 XSslSocket handshake/record test.

Logs:

- `/tmp/full-static-current.log`
- `/tmp/full-dynamic-current.log`
- `/tmp/baseline-static-xcrypto.log`
- `/tmp/baseline-dynamic-xcrypto.log`
- `/tmp/baseline-static-xssl.log`
- `/tmp/baseline-dynamic-xssl.log`

### TLS 1.2 / XSslSocket ECC import fix (2026-08-13, completed)

A previous pause left `XSsl TLS 1.2 setup` and `XSslSocket local TLS 1.2`
failing in the default build. The root cause was the XCryptographic ECC import
adapter requiring a precise `psa_key_attributes->bits` value (256/384/521),
while the legacy PK layer imported ECC keys without setting the bit length,
so `psa_import_key` returned `PSA_ERROR_NOT_SUPPORTED`
(`MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE`, `-0x3980`).

Fixed in two files:

- `Library/mbedtls/tf-psa-crypto/extras/pk_ecc.c`:
  `mbedtls_pk_ecc_set_key()` now calls `psa_set_key_bits(&attributes, pk->bits)`
  right after `psa_set_key_type`.
- `Library/mbedtls/tf-psa-crypto/extras/pk_wrap.c`:
  `ecdsa_verify_psa()` now calls `psa_set_key_bits(&attributes, curve_bits)`
  right after `psa_set_key_type`.

Verification after the fix:

- Default build rebuilt cleanly with `cmake --build build -j$(nproc)`.
- Static/dynamic `--test xcryptographic` exit `0`
  (`[RESULT] XCryptographic primitives: PASS`).
- Static/dynamic `--test xssl` exit `0`, including `TLS 1.2 memory-BIO
  handshake` and `XSslSocket local TLS 1.2 handshake and records`.
- Dynamic `--test all` exits `0`; static `--test all` exits `1` only due to
  the pre-existing dirty XConsoleShell `login testadmin` account assertion
  (crypto/SSL `[RESULT]` lines pass in both full runs).

Logs:

- `/tmp/xinyuec-static-all-final.log`
- `/tmp/xinyuec-dynamic-all-final.log`

### RSA batch (2026-08-13, completed)

The legacy RSA key-representation adapter in `psa_crypto_rsa.c` is now routed
to XCryptographic. When `MBEDTLS_USE_XCRYPTOGRAPHIC_RSA` is ON (default), the
old `rsa.c` and `rsa_alt_helpers.c` sources are excluded from the mbedTLS
archive.

- `Library/mbedtls/CMakeLists.txt` excludes `rsa.c`/`rsa_alt_helpers.c` when
  `MBEDTLS_USE_XCRYPTOGRAPHIC_RSA` is ON.
- `psa_crypto_rsa.c` gates the still-legacy helper functions
  (`psa_check_rsa_key_byte_aligned`, `mbedtls_psa_rsa_load_representation`,
  `mbedtls_psa_rsa_export_key`, `rsa_pss_expected_salt_len`,
  `psa_rsa_oaep_set_padding_mode`) behind
  `#if !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_RSA)`.
- Verified `build/Library/mbedtls/libmbedtlsd.a` contains no
  `rsa.c.o`/`rsa_alt_helpers.c.o` and no legacy `mbedtls_rsa_*` arithmetic
  symbols.
- Default static/dynamic `--test xcryptographic` and `--test xssl` pass.
- Static `--test all` exits `1` only due to the pre-existing dirty
  XConsoleShell `login testadmin` account assertion; dynamic `--test all`
  exits `0`.
- Optional `build-mldsa` and `build-mldsa-pqcp` still build and pass
  `--test xcryptographic` with ML-DSA-87 enabled.

Logs:

- `/tmp/xinyuec-static-all-after-rsa-batch.log`
- `/tmp/xinyuec-dynamic-all-after-rsa-batch.log`

The RSA batch is complete (see ``2026-08-13 RSA batch (completed)`` above).
The user has decided that the old legacy public API
(``mbedtls_pk_*``/``mbedtls_ecp_*``/``mbedtls_ecdsa_*``) is **not to be
preserved** and **not to be migrated down** as a compatibility layer. The
remaining work is to **remove** that legacy API layer and its old algorithm
sources, and route the remaining consumers (PSA driver plus TLS/X509) directly
to PSA/XCryptographic.

Remaining legacy algorithm sources to remove:

- `ecp.c`, `ecp_curves.c`, `ecp_curves_new.c`, `ecdsa.c` (old ECP/ECDSA math)
- `pk.c`, `pk_wrap.c`, `pk_ecc.c`, `pk_rsa.c`, `pkparse.c`, `pkwrite.c`
  (old public PK API)
- The `mbedtls_pk_*` call sites in `library/*.c` (TLS/X509) must be rewritten
  to PSA directly.

Current consumers still pulling legacy ECP/ECDSA symbols into the archive:

- `psa_crypto_ecp.c` (PSA ECP driver fallback paths)
- `pk_ecc.c`, `pkparse.c`, `pkwrite.c`, `pk_wrap.c`, `pk.c`, `pk_rsa.c`
- `psa_crypto.c` (uses `mbedtls_ecp_group_init` in one path)

Concrete next batch:

1. Remove `ecp.c`, `ecp_curves.c`, `ecp_curves_new.c`, `ecdsa.c` from the
   mbedTLS archive and make `psa_crypto_ecp.c` exclusively use XCryptographic
   for all enabled curves (no legacy fallback).
2. Remove the legacy PK layer (`pk*.c`) and rewrite the TLS/X509
   `mbedtls_pk_*` call sites to PSA directly.
3. Re-run the focused static/dynamic `xcryptographic` and `xssl` tests, then
   static/dynamic `--test all`, and keep crypto/SSL results separate from the
   pre-existing dirty XConsoleShell account-test failures.

### Next Work

RSA is fully routed and `rsa.c`/`rsa_alt_helpers.c` are excluded. Per user
direction, the old legacy public API is **not preserved** and **not migrated
down**; it is removed.

- Remove `ecp.c`, `ecp_curves.c`, `ecp_curves_new.c`, `ecdsa.c` from the
  mbedTLS archive.
- Make `psa_crypto_ecp.c` exclusively use XCryptographic for all enabled
  curves (all fast paths already exist and pass `--test xssl`); gate out the
  legacy `mbedtls_ecp_*`/`mbedtls_ecdsa_*` fallback code.
- Remove the legacy PK layer (`pk.c`, `pk_wrap.c`, `pk_ecc.c`, `pk_rsa.c`,
  `pkparse.c`, `pkwrite.c`) and rewrite the TLS/X509 `mbedtls_pk_*` call sites
  in `library/*.c` to PSA directly.
- Keep using the focused static/dynamic `xcryptographic` and `xssl` tests, then
  static/dynamic `--test all`. Keep crypto/SSL findings separate from the
  unrelated dirty XConsoleShell assertion.
- Run `git diff --check` after each completed batch.

## Status and Scope

This document records the migration state as of 2026-08-13. The requested
direction is to move the algorithm implementations currently under
`Library/mbedtls/tf-psa-crypto` down into `Src/XCode/XCryptographic`, then
make mbedTLS consume the XCryptographic implementations.

The old `Library/mbedtls/tf-psa-crypto` directory was removed on
2026-08-13: the PSA framework was migrated in place to
`Library/mbedtls/psa-crypto` (including `include/tf-psa-crypto` ->
`include/psa-crypto`), all build references were updated, the focused static
and dynamic crypto/XSsl suites pass, and the crypto/SSL portions of the full
suites pass (the full-suite exit code can be `1` only when the unrelated dirty
XConsoleShell `login testadmin` assertion triggers). The relocated PSA
framework is still the active PSA dependency of mbedTLS and must not be
deleted; only the old `tf-psa-crypto` path has been retired.

## Existing Worktree Constraints

The worktree was already dirty before this migration. Preserve these unrelated
user changes:

- `Src/XCode/XConsoleShell/*`
- `Src/XTui/XTuiConfig.h`
- `Src/XTui/XTuiVim.c`
- `Src/XTui/XTuiVim.h`
- `Test/XCodeTest/XConsoleShellTest.c`

Migration changes are concentrated in:

- `Src/XCode/XCryptographic/XCryptographic.c`
- `Src/XCode/XCryptographic/XCryptographic.h`
- `Src/XCode/XCryptographic/XCryptographic_config.h`
- `Library/mbedtls/CMakeLists.txt`
- `Library/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_aead.c`
- `Library/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_cipher.c`
- `Library/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c`
- `Library/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_hash.c`
- `Library/mbedtls/tf-psa-crypto/drivers/builtin/src/psa_crypto_mac.c`
- `Test/XCodeTest/XCryptographicPrimitiveTest.c` and `.h` (new)
- `Test/XIOTest/XSslTest.c` and `.h` (new)
- test registration in `Test/XCodeTest/XCodeTest.[ch]`,
  `Test/XCodeTest/XTestCommand.c`, `Test/XIOTest/XIOTest.[ch]`, and `main.c`

Do not use `git reset --hard` or `git checkout --` to clean the tree.

## Build Switches

`Library/mbedtls/CMakeLists.txt` exposes the following options. They default
to `ON` and add private compile definitions to the `mbedtls` target.

| CMake option | PSA coverage routed to XCryptographic |
| --- | --- |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD` | AES/ARIA/Camellia GCM/CCM and ChaCha20-Poly1305, one-shot and multipart AEAD |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_HASH` | one-shot and multipart hashes |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_MAC` | HMAC and AES-CMAC, including multipart operations |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_CTR` | AES-CTR |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_ECC` | P-256/P-384/P-521/secp256k1/brainpoolP256r1/brainpoolP384r1/brainpoolP512r1 ECDH and ECDSA, plus X25519/X448 |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER` | AES ECB/CBC (no padding), CFB, OFB |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20` | raw ChaCha20 stream cipher |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_XOF` | SHAKE128/256 XOF |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG` | AES CCM*-no-tag stream cipher |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_KDF` | HMAC-backed HKDF and TLS 1.2 PRF/PSK-to-MS, plus PBKDF2-HMAC and PBKDF2-AES-CMAC-PRF-128 |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_FFDH` | RFC 7919 finite-field Diffie-Hellman |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_RSA` | RSA OAEP/PKCS#1 v1.5/PSS and key lifecycle |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE` | ECJPAKE PAKE (secp256r1/SHA-256) |
| `MBEDTLS_USE_XCRYPTOGRAPHIC_KEY_WRAP` | AES-KW/KWP (RFC 3394/5649) |

The current `build/CMakeCache.txt` has all options set to `ON`.

## Completed and Validated Migration

The entries in this section were built and exercised by static and dynamic
full test runs before the later raw-ChaCha20 edits described below.

### XCryptographic capabilities added or extended

- RIPEMD-160 hash support. `XCryptographicHash_Ripemd160` was appended as
  enum value 23; existing enum values were not renumbered.
- AES-CMAC, generic HMAC-backed HKDF/TLS 1.2 PRF, and PBKDF2-HMAC.
- AES-GCM and AES-CCM for AES-128/192/256.
- ChaCha20-Poly1305.
- AES CTR.
- AES ECB and CBC without padding, CFB, and OFB, exposed through
  `XCryptographic_BlockCipherOperation`.
- P-256, P-384, P-521, brainpoolP256r1, brainpoolP384r1, and brainpoolP512r1 ECDH and
  ECDSA key import/generation/public export paths.
- X25519 and X448 key import/generation/public export/raw ECDH paths.
- HMAC-backed HKDF extract/expand, TLS 1.2 PRF/PSK-to-MS, and
  PBKDF2-AES-CMAC-PRF-128 one-shot KDF helpers.

`XCryptographic_config.h` has feature toggles for these algorithms. New
public XCryptographic APIs should follow the existing `Setup`, `UpdateInto`,
`FinishInto`, `Abort`, `ImportKey`, and `XByteArrayView` conventions rather
than mirroring mbedTLS types or APIs.

### mbedTLS PSA adapter paths changed

- `psa_crypto_hash.c`: XCryptographic one-shot and streaming hash operations,
  including clone, use a pointer stored in the existing PSA context union.
- `psa_crypto_mac.c`: XCryptographic HMAC and AES-CMAC one-shot/streaming
  operations use contexts stored in the existing PSA context union.
- `psa_crypto_aead.c`: one-shot and multipart AES/ARIA/Camellia GCM/CCM and
  ChaCha20-Poly1305 call XCryptographic.
- `psa_crypto_cipher.c`: AES-CTR and AES ECB/CBC(no padding)/CFB/OFB use
  allocated XCryptographic operation contexts stored as pointers in the
  existing mbedTLS cipher union. Do not change PSA public operation layouts.
- `psa_crypto_ecp.c`: PSA P-256/P-384/P-521/secp256k1/brainpoolP256r1/
  brainpoolP384r1/brainpoolP512r1 ECDH/ECDSA and X25519/X448 key lifecycle/raw agreement call
  XCryptographic.
- `psa_crypto.c` (PSA KDF framework): HMAC-backed HKDF extract/expand and TLS 1.2
  PRF/PSK-to-MS, plus PBKDF2-AES-CMAC-PRF-128 read paths route to
  XCryptographic one-shot helpers; the PSA layer already builds the PSK-to-MS
  premaster secret, so the PRF path is reused for both.

PSA P-256 private keys are handled as 32-byte scalars and public keys as
65-byte uncompressed points. X25519 keys are 32 bytes. Preserve these PSA
wire formats when extending the adapter.

## Raw ChaCha20: Validated (2026-08-12)

Raw ChaCha20 streaming support has now been built and exercised. Two PSA edge
behaviors in `mbedtls_psa_cipher_update`/`set_iv` were fixed and covered by
tests during validation:

- A too-small output buffer now returns `PSA_ERROR_BUFFER_TOO_SMALL` instead
  of `PSA_ERROR_INVALID_ARGUMENT`.
- A zero-length `psa_cipher_update` returns success with `output_length == 0`
  and does not disturb the stream state.

New regression coverage also verifies missing `set_iv` returns
`PSA_ERROR_BAD_STATE`, `psa_cipher_abort` cleanup, and update splits both
across and exactly at the 64-byte block boundary (RFC 7539 zero-key/zero-nonce
vector extended to 96 bytes).

Changed code:

- `XCryptographic_config.h`: `XCRYPTOGRAPHIC_CHACHA20_ON`.
- `XCryptographic.h/.c`: `XCryptographic_ChaCha20Operation`,
  `XCryptographic_chacha20Setup`, `XCryptographic_chacha20UpdateInto`, and
  `XCryptographic_chacha20Abort`.
- `psa_crypto_cipher.c`: PSA `PSA_ALG_STREAM_CIPHER` for
  `PSA_KEY_TYPE_CHACHA20`, with its context retained through the existing
  cipher union.
- `XCryptographicPrimitiveTest.c`: split-update ChaCha20 zero-key/zero-nonce
  RFC 7539 vector.
- `XSslTest.c`: the same vector through PSA streaming cipher setup/set-IV/
  update/finish.

Review and fix any failures before treating this migration as complete. In
particular, check PSA error behavior in `mbedtls_psa_cipher_update`: a too
small output buffer should normally report `PSA_ERROR_BUFFER_TOO_SMALL`, not
`PSA_ERROR_INVALID_ARGUMENT`. Also test cleanup paths, a missing `set_iv`,
zero-length update, and update splitting across a 64-byte block boundary.

## CBC-PKCS7: Validated (2026-08-12)

AES-128/192/256 CBC with PKCS7 padding is now routed through XCryptographic.

Added:

- `XCryptographic_config.h`: `XCRYPTOGRAPHIC_AES128/192/256_CBC_PKCS7_ON`
  (default 1).
- `XCryptographic.h/.c`: `XCryptographic_BlockCipherMode_CbcPkcs7` in the
  block-cipher operation enum. Encryption pads 1..16 bytes at finish;
  decryption keeps the last ciphertext block pending so finish can strip the
  padding and reject invalid padding.
- `psa_crypto_cipher.c`: `PSA_ALG_CBC_PKCS7` maps to the XCryptographic
  block-cipher path for AES keys; `psa_cipher_finish` reports the unpadded
  plaintext length on decrypt and 16 bytes on encrypt.
- `XCryptographicPrimitiveTest.c`: NIST SP 800-38A CBC + PKCS7 vectors for
  21-byte and full-block 64-byte plaintexts, plus invalid-padding rejection.
- `XSslTest.c`: PSA `psa_cipher_encrypt/decrypt` streaming CBC-PKCS7 coverage.

Both `./bin/XinYueC_Static --test all` and `./bin/XinYueC_Dynamic --test all`
exit 0 after this change. Logs:

- `/tmp/xinyuec-static-after-cbc-pkcs7.log`
- `/tmp/xinyuec-dynamic-after-cbc-pkcs7.log`

## AES-CCM*-No-Tag (CCM_STAR_NO_TAG): Validated (2026-08-12)

AES-128/192/256 CCM*-no-tag streaming is now routed through XCryptographic.

Added:

- `XCryptographic_config.h`: `XCRYPTOGRAPHIC_CCM_STAR_NO_TAG_ON` (default 1).
- `XCryptographic.h/.c`: `XCryptographic_CcmStarNoTagOperation` and
  `XCryptographic_aesCcmStarNoTagSetup/UpdateInto/Abort`. The PSA
  `PSA_ALG_CCM_STAR_NO_TAG` uses a fixed 13-byte nonce with a 2-byte
  big-endian counter block starting at 1; encryption/decryption are the same
  stream-XOR operation.
- `psa_crypto_cipher.c`: `PSA_ALG_CCM_STAR_NO_TAG` for AES keys routes
  set_iv/update/finish/abort through the XCryptographic context. set_iv
  re-creates the operation (round keys are not reversible) from the saved key.
- `XCryptographicPrimitiveTest.c`: AES-128 CCM*-no-tag keystream vector using
  the SP 800-38A plaintext and the CCM counter block format.
- `XSslTest.c`: PSA `psa_cipher_encrypt/decrypt` streaming CCM*-no-tag
  coverage.

Both `./bin/XinYueC_Static --test all` and `./bin/XinYueC_Dynamic --test all`
exit 0 after this change. Logs:

- `/tmp/xinyuec-static-after-ccmstar.log`
- `/tmp/xinyuec-dynamic-after-ccmstar.log`

## KDF Batch (HKDF expand / TLS 1.2 PRF & PSK-to-MS / PBKDF2-AES-CMAC): Validated (2026-08-12)

The KDF read paths are now routed through XCryptographic one-shot helpers.

Added:

- `XCryptographic_config.h`: `XCRYPTOGRAPHIC_HKDF_EXTRACT_SHA256_ON`,
  `XCRYPTOGRAPHIC_HKDF_EXPAND_SHA256_ON`,
  `XCRYPTOGRAPHIC_TLS12_PRF_SHA256_ON`,
  `XCRYPTOGRAPHIC_TLS12_PSK_TO_MS_SHA256_ON`,
  `XCRYPTOGRAPHIC_PBKDF2_AES_CMAC_PRF_128_ON` (default 1).
- `XCryptographic.h/.c`: `XCryptographic_hkdfExtractSha256Into`,
  `XCryptographic_hkdfExpandSha256Into`,
  `XCryptographic_tls12PrfSha256Into`,
  `XCryptographic_tls12PskToMsSha256Into`,
  `XCryptographic_pbkdf2AesCmacPrf128Into`.
- `Library/mbedtls/CMakeLists.txt`: `MBEDTLS_USE_XCRYPTOGRAPHIC_KDF` (default
  ON) and the matching private compile definition for the `mbedtls` target.
- `psa_crypto.c`: three static helpers plus routing in
  `psa_key_derivation_hkdf_read`, `psa_key_derivation_tls12_prf_read`, and
  `psa_key_derivation_pbkdf2_read`. The PSA layer already builds the
  PSK-to-MS premaster secret, so both TLS 1.2 PRF and PSK-to-MS reuse
  `XCryptographic_tls12PrfSha256Into`. PSA operation layouts are unchanged.
- `XCryptographicPrimitiveTest.c`: `XCryptographicPrimitive_test_kdf_batch`
  with RFC 5869 HKDF extract/expand, TLS 1.2 PRF, TLS 1.2 PSK-to-MS pure and
  mixed, and RFC 4615 PBKDF2-AES-CMAC-PRF-128 vectors.
- `XSslTest.c`: `xssl_test_psa_kdf_new_backend` registered in
  `XSslTest_runAll`; exercises PSA HKDF split reads, TLS 1.2 PRF split reads,
  PSK-to-MS pure/mixed, and PBKDF2-AES-CMAC-PRF-128.

Fixed during this batch:

- `XCryptographic_pbkdf2AesCmacPrf128Into` previously fed the cumulative XOR
  accumulator back into the next AES-CMAC PRF iteration; it now keeps the
  current `U` chain separate from the `T` accumulator, matching RFC 4615.
- The PSA TLS 1.2 PSK-to-MS helper initially called
  `XCryptographic_tls12PskToMsSha256Into` on the already-built premaster
  secret, which double-wrapped the key; it now uses
  `XCryptographic_tls12PrfSha256Into` directly.

Focused evidence (both static and dynamic):

```sh
./bin/XinYueC_Static --test xcryptographic   # PASS
./bin/XinYueC_Dynamic --test xcryptographic   # PASS
./bin/XinYueC_Static --test xssl             # PASS
./bin/XinYueC_Dynamic --test xssl             # PASS
```

Full-suite note: `./bin/XinYueC_Dynamic --test all` exits 0
(`/tmp/xinyuec-dynamic-kdf2.log`). The static full run still has the
pre-existing XConsoleShell account/password-tests failures that are unrelated
to this migration (the login/passwd XConsoleShell area is a separate dirty
worktree change); XCryptographic and XSsl both report PASS in that log as well
(`/tmp/xinyuec-static-kdf2.log`). `git diff --check` is clean.

## Current Test Coverage and Evidence

New command-line test targets are registered in both static and dynamic
binaries:

```sh
./bin/XinYueC_Static --test xcryptographic
./bin/XinYueC_Static --test xssl
./bin/XinYueC_Dynamic --test xcryptographic
./bin/XinYueC_Dynamic --test xssl
```

`xcryptographic` contains standard-vector tests for GCM, CCM,
ChaCha20-Poly1305, HKDF extract/expand, TLS 1.2 PRF/PSK-to-MS, RIPEMD-160,
CMAC, AES ECB/CBC/CFB/OFB, PBKDF2-HMAC-SHA-256, PBKDF2-AES-CMAC-PRF-128,
SHAKE-128/256, the AES-CCM*-no-tag streaming vector, and the raw ChaCha20
test. `xssl` exercises PSA adapter paths (AEAD, CTR, block cipher, ChaCha20,
CCM*-no-tag, P-256, X25519, KDF split reads, SHAKE, hash, MAC) as well as a
TLS 1.2 memory-BIO handshake, XSslSocket lifecycle/configuration, and a local
TLS 1.2 XSslSocket client/server loopback.

After the KDF batch, `./bin/XinYueC_Dynamic --test all` exits 0
(`/tmp/xinyuec-dynamic-kdf2.log`). The static full run
(`/tmp/xinyuec-static-kdf2.log`) still hits pre-existing XConsoleShell
login/passwd test failures from the separate dirty XConsoleShell worktree, but
reports `[RESULT] XCryptographic primitives: PASS` and `[RESULT] XSsl tests:
PASS`; these XConsoleShell failures are unrelated to the crypto migration.

Earlier full-suite-pass logs remain saved at:

- `/tmp/xinyuec-static-after-chacha20-2.log`
- `/tmp/xinyuec-dynamic-after-chacha20.log`

They report passing XCryptographic vectors, PSA AEAD/CTR/block
cipher/ChaCha20/P-256/X25519/KDF/hash/MAC coverage, the PSA ChaCha20
edge-case tests, TLS 1.2 memory-BIO records, XSslSocket configuration/
lifecycle, and local TLS 1.2 loopback.

`git diff --check` is clean after the KDF batch.

## 2026-08-13 Validation and Progress Update

The header-reconstruction batch was fixed by restoring
`XCryptographicHash_Ripemd160 = 23` and `XCryptographicHash_NumAlgorithms = 24`
in `XCryptographic.h`. The build then completed successfully:

```sh
cmake --build build -j$(nproc)
```

Both focused primitive suites passed with exit status 0:

```sh
./bin/XinYueC_Static --test xcryptographic
./bin/XinYueC_Dynamic --test xcryptographic
```

They cover and pass AES-GCM/CCM, ChaCha20/Poly1305, RIPEMD-160, CMAC, AES
modes including CBC-PKCS7 and CCM*-no-tag, SHAKE, KDF batches, FFDH, RSA
PKCS#1/OAEP/PSS, ARIA, Camellia, and deterministic P-256 ECDSA.

Both focused XSsl suites passed with exit status 0:

```sh
./bin/XinYueC_Static --test xssl
./bin/XinYueC_Dynamic --test xssl
```

The XSsl suite covers the PSA adapters for those algorithms, plus TLS 1.2
memory-BIO records, XSslSocket configuration/lifecycle, and a local TLS 1.2
XSslSocket loopback.

Full-suite evidence is in:

- `/tmp/xinyuec-static-all-resume-20260813.log`
- `/tmp/xinyuec-dynamic-all-resume-20260813.log`

Both full runs report `[RESULT] XCryptographic primitives: PASS` and
`[RESULT] XSsl tests: PASS`. Both also hit the unrelated XConsoleShell
`useradd testuser` failure (`result=-9`) from the separate dirty shell changes,
so the overall process exit is 1. Do not attribute that account-command failure
to the cryptographic migration.

The default RSA primitive regression was reduced from 20 generated 1024-bit
keys to one generated key. It still covers random key generation, DER
export/import, PKCS#1 encryption/decryption, and signing/verification; larger
RSA pressure belongs in a separately invoked stress test.

`git diff --check` is clean after this validation.

## Required Next Validation Sequence

From the repository root:

```sh
cmake --build build -j$(nproc)
./bin/XinYueC_Static --test xcryptographic
./bin/XinYueC_Dynamic --test xcryptographic
./bin/XinYueC_Static --test xssl
./bin/XinYueC_Dynamic --test xssl
./bin/XinYueC_Static --test all
./bin/XinYueC_Dynamic --test all
git diff --check
```

If the regular build directory was changed or missing, configure it with:

```sh
cmake -S . -B build
```

Keep compile, focused crypto/SSL tests, and full-suite evidence separate in
the final report. The full suite is required because XSsl and XSslSocket are
dependent consumers of mbedTLS.

Note: the full-suite `XConsoleShell` login/passwd tests may fail independent
of this migration because that area is a separate dirty worktree change. Judge
the crypto migration by the `xcryptographic` and `xssl` targets (and the
`[RESULT]` lines in the full-suite logs).

## 2026-08-13 ARIA/Camellia AEAD and RSA DER Update

ARIA and Camellia GCM/CCM are now routed through XCryptographic. The AEAD
adapter selects the XCryptographic AEAD key type from the PSA key type instead
of treating every GCM/CCM request as AES. XCryptographic now uses its existing
ARIA/Camellia block-cipher implementations to construct the GCM counter mode,
GHASH subkey, and CCM MAC/CTR paths.

Added coverage:

- `XCryptographicPrimitive_test_aria_camellia_aead` verifies ARIA-128 and
  Camellia-128 GCM/CCM encrypt/decrypt vectors and rejects modified tags.
- `XSslTest` runs the same four algorithm families through PSA, alongside the
  existing TLS memory-BIO and XSslSocket loopback tests.

Fixed while running the dynamic full suite: RSA DER export previously used a
separate, off-by-one length estimate. It could include one uninitialized tail
byte in an exported public key, so export-import-export sometimes differed.
DER INTEGER previews, sequence headers, and actual writing now share the same
length calculation; both public and private exports verify that the final
write position matches the allocated DER size.

Validation evidence:

```sh
cmake --build build -j$(nproc)                         # PASS
./bin/XinYueC_Static --test xcryptographic             # PASS, 8 random RSA runs
./bin/XinYueC_Dynamic --test xcryptographic            # PASS, 8 random RSA runs
./bin/XinYueC_Static --test xssl                       # PASS
./bin/XinYueC_Dynamic --test xssl                      # PASS
./bin/XinYueC_Dynamic --test all                       # PASS
```

The static full suite reports XCryptographic and XSsl as PASS, but exits 1 at
the unrelated dirty `XConsoleShell` non-admin `passwd` prompt test:

- `/tmp/xinyuec-static-all-aead-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-aead-final-20260813.log`

`git diff --check` remains clean. AES-XTS remains deferred: the built-in PSA
configuration keeps XTS under a disabled `#if 0` path, so it is not an active
API family to migrate in this batch.

## 2026-08-13 ECJPAKE PAKE Migration

The enabled `PSA_ALG_JPAKE(PSA_ALG_SHA_256)` path now uses XCryptographic.
It preserves the active built-in scope: secp256r1, SHA-256, and the fixed
`client`/`server` identities.

- `XCryptographic.h/.c` provides an opaque ECJPAKE context with create,
  two-round TLS-format read/write, shared-point export, and destruction APIs.
  It uses XCryptographic P-256 arithmetic, SHA-256, and randomness, without
  mbedTLS ECP/MPI/hash contexts.
- `psa_crypto_pake.c` retains the PSA state machine and split-message
  translation, but routes all ECJPAKE algorithm operations through the opaque
  XCryptographic context when `MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE=ON`.
- `Library/mbedtls/CMakeLists.txt` defaults that option to ON and excludes
  `tf-psa-crypto/drivers/builtin/src/ecjpake.c` from the mbedTLS source list.
  The resulting archive has `psa_crypto_pake.c.o` and no `ecjpake.c.o`.
- `XCryptographicPrimitiveTest` verifies complete direct two-party exchanges:
  equal passwords yield the same 65-byte shared point, while a separate
  equal-on-both-sides wrong-password exchange yields a different point.
- `XSslTest` verifies PSA password-key import, client/server setup, all 12
  JPAKE split steps, shared-key import/export, and equality of the two
  exported 65-byte shared points. It uses the actual output length for each
  proof step so a leading-zero scalar remains valid.

Validation after excluding the legacy source:

```sh
cmake -S . -B build && cmake --build build -j$(nproc)  # PASS
./bin/XinYueC_Static --test xcryptographic             # PASS
./bin/XinYueC_Dynamic --test xcryptographic            # PASS
./bin/XinYueC_Static --test xssl                       # PASS
./bin/XinYueC_Dynamic --test xssl                      # PASS
```

Full-suite evidence:

- `/tmp/xinyuec-static-all-jpake-excluded-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-jpake-excluded-final-20260813.log`

Both logs report `[RESULT] XCryptographic primitives: PASS` and
`[RESULT] XSsl tests: PASS`. The static process has an unrelated dirty
XConsoleShell `login testadmin` prompt failure. The dynamic process has the
independent XProcess startup-error-handshake failure; crypto, XSsl, and
XSslSocket all pass.

## 2026-08-13 ECJPAKE-to-PMS and Hash-Family Coverage

The TLS 1.2 `PSA_ALG_TLS12_ECJPAKE_TO_PMS` read path now computes the
premaster secret with `XCryptographicHash_hashInto(..., Sha256)` when
`MBEDTLS_USE_XCRYPTOGRAPHIC_JPAKE=ON`. The PSA input format check, 65-byte
uncompressed-point handling, fixed 32-byte output requirement, operation
capacity, and abort zeroization remain in the PSA layer. The legacy PSA hash
call remains the feature-off fallback.

`XSslTest` now includes an independent fixed vector for the 32-byte shared
point X coordinate (`00..1f`) and verifies the expected SHA-256 PMS. It also
verifies every hash family enabled by the active `tf-psa-crypto/include/psa/crypto_config.h`:
MD5, SHA-1, SHA-224/256/384/512, SHA3-224/256/384/512, and RIPEMD-160. The
existing SHA-256 multipart/clone test remains in place.

Validation after this update:

```sh
cmake --build build -j$(nproc)                      # PASS
./bin/XinYueC_Static --test xssl                   # PASS
./bin/XinYueC_Dynamic --test xssl                  # PASS
```

The focused XSsl result includes the TLS 1.2 memory-BIO handshake, XSslSocket
configuration/lifecycle, local TLS 1.2 XSslSocket loopback, ECJPAKE exchange,
ECJPAKE-to-PMS, and all enabled PSA hash-family vectors.

The same XSsl MAC coverage now includes one-shot HMAC vectors for MD5, SHA-1,
SHA-224/256/384/512, SHA3-256, and RIPEMD-160 in addition to the existing
multipart HMAC-SHA-256 and AES-CMAC checks. This exposed and fixed the
XCryptographic HMAC sponge-rate mapping: SHA3/Keccak-224/256/384/512 now use
144/136/104/72-byte rates rather than the SHA-2 block sizes.

PBKDF2-HMAC is now routed through the generic
`XCryptographic_pbkdf2HmacInto` helper for every enabled digest that the
XCryptographic hash mapping supports. The existing SHA-256 API remains as a
compatibility wrapper. PSA keeps its operation state, capacity, segmented
output, and AES-CMAC PBKDF2 fallback; long-password normalization also uses
the XCryptographic hash path when enabled. Direct and PSA tests cover
SHA-1, SHA-256, SHA-512, SHA3-256, a SHA-512 split output, and a SHA-256
password longer than the HMAC block size.

## 2026-08-13 AES-KW/KWP Migration

The enabled mbedTLS NIST key-wrap API now uses XCryptographic for transparent
AES keys while retaining the PSA cipher setup as the policy gate. AES-KW
(RFC 3394) and AES-KWP (RFC 5649) support 128/192/256-bit KEKs, including the
KWP single-block case and constant-time integrity/padding rejection. External
or hardware-backed PSA keys continue through the existing driver path.

- `XCryptographic.h/.c`: added `XCryptographic_aesKeyWrapInto` and
  `XCryptographic_aesKeyUnwrapInto` with KW/KWP modes, AES key sizes, bounds,
  AIV/ICV validation, zero padding checks, and failure clearing.
- `tf-psa-crypto/extras/nist_kw.c`: validates PSA key type, usage, and
  `PSA_ALG_ECB_NO_PADDING`, then routes transparent key material to
  XCryptographic; the legacy PSA cipher implementation remains the fallback.
- `XCryptographicPrimitiveTest.c`: RFC 3394 and RFC 5649 multi-block and
  single-block vectors plus tamper rejection.
- `XSslTest.c`: PSA NIST-KW/KWP import, wrap, unwrap, and 128/192-bit key
  coverage for the adapter path.

Focused static and dynamic `xcryptographic` and `xssl` runs pass with the new
vectors and the XSslSocket tests.

An X448 prototype was investigated after this batch. The RFC 7748 single
scalar-multiplication vector passed, but the RFC section 6.2 public-key and
bilateral agreement vectors did not. That unverified prototype was removed
from the working tree and is not part of the migration switches or claims.

## 2026-08-13 secp256k1 ECDH/ECDSA Migration

The enabled PSA secp256k1 (`PSA_ECC_FAMILY_SECP_K1`, 256-bit) ECDH and ECDSA
paths now use XCryptographic. `XCryptographic_EcdhAlgorithm_Secp256k1` and
the matching ECDSA key types use the existing 256-bit scalar representation
with secp256k1 field/order/base-point parameters and the curve equation
`y^2 = x^3 + 7`.

- `XCryptographic.h/.c`: added secp256k1 private/public import, key generation,
  public-key export, ECDH, random ECDSA, RFC 6979 deterministic ECDSA, and
  ECDSA verification. The verification path explicitly normalizes the Jacobian
  sum to affine coordinates before comparing `x mod n`.
- `tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c`: routes secp256k1 and
  P-521 key import/export/generation/ECDH and ECDSA sign/verify through
  XCryptographic.
  Other curve families remain on the existing mbedTLS path.
- `XCryptographicPrimitiveTest.c`: checks private scalar `1 -> G`, ECDH
  scalar `1`/`2` against the known `2G.x`, invalid-point rejection, deterministic
  signing, tamper rejection, and verification of an independently generated
  OpenSSL secp256k1 ECDSA signature.
- `XSslTest.c`: PSA secp256k1 key generation, export, ECDH, import, ECDSA
  signing, public-key import, verification of both XCryptographic and OpenSSL
  signatures, and tamper rejection.

Focused static and dynamic evidence:

- `/tmp/xinyuec-static-secp256k1-final-primitive-20260813.log`
- `/tmp/xinyuec-dynamic-secp256k1-final-primitive-20260813.log`
- `/tmp/xinyuec-static-secp256k1-ecdsa5-xssl.log`
- `/tmp/xinyuec-dynamic-secp256k1-final-xssl-20260813.log`

All four processes returned exit code 0. The XSsl logs include passing
`PSA secp256k1 ECDH/ECDSA uses XCryptographic` and XSslSocket loopback tests.

Full-suite evidence after the secp256k1 migration:

- `/tmp/xinyuec-static-all-secp256k1-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-secp256k1-final-20260813.log`

The static full suite passed. The dynamic full suite reached passing
XCryptographic, XSsl, and XSslSocket results; its only failure was the
pre-existing dirty-worktree `XConsoleShell` `useradd testuser` case (return
`-9`, expected `0`). It is unrelated to this cryptographic migration.

## 2026-08-13 brainpoolP256r1 ECDH/ECDSA Migration

The enabled 256-bit Brainpool PSA curve (`PSA_ECC_FAMILY_BRAINPOOL_P_R1`) now
uses XCryptographic for ECDH and ECDSA. This curve has a general short
Weierstrass `a` coefficient, so the implementation uses the general Jacobian
doubling equation `E = 3X^2 + aZ^4`; it does not reuse the P-256 `a = -3`
special case.

- `XCryptographic.h/.c`: added RFC 5639 brainpoolP256r1 field/domain
  parameters, public-key parsing, ECDH, random ECDSA, RFC 6979 ECDSA, and
  ECDSA verification. Key/import/export representation remains the PSA
  32-byte scalar and 65-byte uncompressed point format.
- `tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c`: routes 256-bit
  brainpool key import/export/generation, ECDH, ECDSA sign, and ECDSA verify
  to XCryptographic. Brainpool P-384/P-512 continue through mbedTLS.
- `XCryptographicPrimitiveTest.c`: validates scalar `1 -> G`, ECDH scalar
  `1`/`2` against OpenSSL `2G.x`, invalid-point rejection, deterministic
  ECDSA, tamper rejection, and an independently generated OpenSSL signature.
- `XSslTest.c`: validates PSA ECDH lifecycle/import/export and PSA ECDSA
  signing, public import, verification of XCryptographic/OpenSSL signatures,
  and tamper rejection.

Focused static and dynamic evidence:

- `/tmp/xinyuec-static-brainpool-ecdsa-primitive-20260813.log`
- `/tmp/xinyuec-dynamic-brainpool-ecdsa-primitive-20260813.log`
- `/tmp/xinyuec-static-brainpool-ecdsa-xssl-20260813.log`
- `/tmp/xinyuec-dynamic-brainpool-ecdsa-xssl-20260813.log`

All four processes returned exit code 0. XSsl logs include passing
`PSA brainpoolP256r1 ECDH/ECDSA uses XCryptographic` plus the XSslSocket
loopback tests.

Full-suite evidence after the brainpoolP256r1 migration:

- `/tmp/xinyuec-static-all-brainpool-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-brainpool-final-20260813.log`

The static full suite passed. The dynamic full suite again reports only the
pre-existing dirty-worktree `XConsoleShell` `useradd testuser` failure (return
`-9`, expected `0`); XCryptographic, XSsl, and XSslSocket all pass.

## 2026-08-13 NIST P-384 ECDH Migration

The enabled PSA NIST P-384 curve (`PSA_ECC_FAMILY_SECP_R1`, 384-bit) now uses
XCryptographic for key generation/import/export, raw ECDH agreement, ECDSA
signing, deterministic ECDSA signing, and ECDSA verification.

- `XCryptographic_Key` now reserves 66 bytes for private material and 133
  bytes for an uncompressed public point, covering future P-521 support while
  preserving the existing 32/65-byte representations.
- `XCryptographic.c` adds an independent 12-limb P-384 field implementation,
  NIST P-384 domain constants, Jacobian point arithmetic, point validation,
  scalar generation/import, 48-byte shared-secret output, and 96-byte `r||s`
  ECDSA signatures. Deterministic ECDSA follows RFC 6979 with HMAC-SHA-384.
- `psa_crypto_ecp.c` routes P-384 import, generation, public export, raw
  agreement, ECDSA signing, deterministic ECDSA signing, and verification to
  XCryptographic.
- `XCryptographicPrimitiveTest.c` checks private scalars 1 and 2 against the
  standard P-384 base point and OpenSSL `2G`, verifies both directions of the
  48-byte shared secret, and rejects a modified point.
- `XSslTest.c` checks PSA P-384 generation, export, import, bidirectional
  agreement, signing, verification, external-signature verification, and
  tamper rejection. The test also includes the existing XSslSocket TLS loopback.

Focused evidence after the P-384 migration:

- `/tmp/xinyuec-static-p384-ecdsa-primitive-20260813.log`
- `/tmp/xinyuec-dynamic-p384-ecdsa-primitive-20260813.log`
- `/tmp/xinyuec-static-p384-ecdsa-xssl-20260813.log`
- `/tmp/xinyuec-dynamic-p384-ecdsa-xssl-20260813.log`

All four focused processes returned exit code 0. They report passing
`P-384 ECDH/ECDSA OpenSSL vectors`, `PSA P-384 ECDH/ECDSA uses
XCryptographic`, and the XSslSocket local TLS loopback tests.

The implementation also fixed an aliasing bug in the fixed-limb subtraction
helper: callers may legally subtract into the same object as an operand, so
the current limb must be captured before writing the output limb. This was
required to make the new 384-bit modular arithmetic agree with OpenSSL and is
also correct for the existing 256-bit curves.

Full-suite evidence after the P-384 migration:

- `/tmp/xinyuec-static-all-p384-ecdsa-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-p384-ecdsa-final-20260813.log`

The dynamic full suite passed. The static full suite reports passing
XCryptographic, XSsl, and XSslSocket coverage, including P-384. Its overall
exit remains nonzero only because the separate dirty-worktree XConsoleShell
`login testuser` fixture did not create the expected account profile; this is
outside the cryptographic and TLS suites.

Latest focused evidence:

- `/tmp/xinyuec-static-xcryptographic-kw-final-20260813.log`
- `/tmp/xinyuec-dynamic-xcryptographic-kw-final-20260813.log`
- `/tmp/xinyuec-static-xssl-kw-final-20260813.log`
- `/tmp/xinyuec-dynamic-xssl-kw-final-20260813.log`

Latest full-suite evidence after this update:

- `/tmp/xinyuec-static-all-jpake-hash-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-jpake-hash-final-20260813.log` (before the HMAC
  sponge-rate fix; retained as intermediate evidence)

Final full-suite evidence after the HMAC fix:

- `/tmp/xinyuec-static-all-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-final-20260813.log`

Final focused primitive evidence:

- `/tmp/xinyuec-static-xcryptographic-final-20260813.log`
- `/tmp/xinyuec-dynamic-xcryptographic-final-20260813.log`

Final focused XSsl evidence after PBKDF2-HMAC, long-password, TLS memory-BIO,
and XSslSocket coverage:

- `/tmp/xinyuec-static-xssl-final-20260813.log`
- `/tmp/xinyuec-dynamic-xssl-final-20260813.log`

The latest full-suite evidence including AES-KW/KWP is:

- `/tmp/xinyuec-static-all-kw-20260813.log`
- `/tmp/xinyuec-dynamic-all-kw-20260813.log`

Both processes returned exit code 0. Each log contains passing
XCryptographic, XSsl, and XSslSocket results, and neither reports a failure.

After removing the unverified X448 prototype, the final regression evidence is:

- `/tmp/reverted-static-all.log`
- `/tmp/reverted-dynamic-all.log`
- `/tmp/reverted-dynamic-prim.log`
- `/tmp/reverted-dynamic-xssl.log`

Both full suites returned exit code 0. All focused and full logs report
passing XCryptographic primitives, XSsl, and XSslSocket coverage.

Both final full-suite logs report `[RESULT] XCryptographic primitives: PASS`
and `[RESULT] XSsl tests: PASS`, including the new hash-family,
ECJPAKE-to-PMS, PBKDF2-HMAC, and XSslSocket loopback checks. The dynamic full
suite has no reported failures. The static full process still reports the
unrelated XProcess redirect failure and a dirty-worktree XConsoleShell
`userlist` expected-output mismatch. These failures are outside the crypto
migration; the focused `xcryptographic` and `xssl` targets remain green.

## Remaining Algorithm Families

Checked again on 2026-08-13. Every currently **enabled** PSA algorithm family
already has an XCryptographic implementation and a routed PSA adapter. The
remaining items are either disabled in this build, platform services, or
optional PQCP drivers, and do not block the migration:

- **secp192/224 and Ed448** are not enabled in the current PSA configuration
  (`include/psa/crypto_config.h` enables only P-256/384/521, secp256k1,
  brainpoolP256/384/512, X25519 and X448). No XCryptographic downgrade is
  needed until one of these curves is explicitly enabled.
- **AES-XTS** is migrated (including ciphertext stealing). `psa_crypto_cipher.c`
  routes `PSA_ALG_XTS` to `XCryptographic`, and both the primitive suite
  (`XCryptographicPrimitiveTest.c`) and the PSA adapter test (`XSslTest.c`)
  cover IEEE P1619 vectors, split/full-block updates, ciphertext stealing and
  decrypt paths.
- **DRBG/entropy/random** is a platform service, not a portable primitive.
  `XFtp`/`XSsl_mbedtls.c` already supplies the active PSA random hook through
  `XRandomGenerator_fillSecure`; there is no separate tf-psa-crypto primitive
  to sink.
- **Optional PQCP driver paths** are only compiled when
  `MBEDTLS_ENABLE_PQCP=ON`. The only experimental PQCP entry point,
  ML-DSA-87, already has an optional XCryptographic implementation
  (`XCryptographic_mldsa87.c`, `XCRYPTOGRAPHIC_ENABLE_MLDSA87` default `OFF`)
  that is verified in `build-mldsa` and `build-mldsa-pqcp`.

Therefore no further algorithm-family sinking is required for the current
default configuration. The remaining migration work is to **remove** the old
legacy `mbedtls_pk_*`/`mbedtls_ecp_*`/`mbedtls_ecdsa_*` API layer and its old
sources (see the ``Next Work`` section), per the user's decision that the old
API is not preserved or migrated down.

Do not assume that a feature being compiled means its primitive has been
migrated. Start each batch from the actual PSA driver source and the enabled
PSA configuration macros, then add a focused XCryptographic vector and an
adapter-level PSA test before declaring it complete.

## Deleting the PSA framework: still explicitly deferred (directory path already migrated)

New status (2026-08-13): the old `tf-psa-crypto` directory path has been
**removed** by migrating it to `Library/mbedtls/psa-crypto`. The PSA framework
itself is still required by mbedTLS and still exists at the new path. Deleting
the relocated PSA framework still requires all of the following, plus a new
explicit user confirmation:

1. Migrate or deliberately replace every required algorithm and PSA service.
2. Move the current PSA adapter/framework responsibilities out of
   `psa-crypto`; they are currently edited in place and CMake still globs
   `psa-crypto/core`, `utilities`, `platform`, `extras`, and builtin driver
   sources.
3. Replace all include paths and source lists in `Library/mbedtls/CMakeLists.txt`.
4. Build static and dynamic configurations and pass full XinYueC, XSsl, and
   XSslSocket regression coverage after the directory is absent.
5. Obtain the user's deletion confirmation immediately before the removal.

Until then, retain the relocated PSA framework and use feature-gated adapters
to route the migrated algorithm calls into XCryptographic. The old
`tf-psa-crypto` path has already been retired (2026-08-13).

## 2026-08-13 NIST P-521 ECDH/ECDSA Migration

The enabled PSA NIST P-521 curve (`PSA_ECC_FAMILY_SECP_R1`, 521-bit) now uses
XCryptographic for key generation/import/export, raw ECDH agreement, ECDSA
signing, deterministic ECDSA signing, and ECDSA verification.

- `XCryptographic.c` adds an independent 17-limb P-521 implementation with
  66-byte scalar and coordinate encoding, NIST P-521 constants, Jacobian point
  arithmetic, point validation, scalar generation/import, and 66-byte shared
  secret output. The first-byte mask preserves the curve's 521-bit wire form.
- P-521 ECDSA emits 132-byte `r||s` signatures. Deterministic signatures use
  RFC 6979 with HMAC-SHA-512 and the 521-bit `qlen` truncation.
- `psa_crypto_ecp.c` routes P-521 import, generation, public export, raw
  agreement, ECDSA signing, deterministic signing, and verification to
  XCryptographic while preserving PSA's 66/133/132-byte formats.
- `XCryptographicPrimitiveTest.c` checks the standard P-521 base point and
  OpenSSL `2G`, both ECDH directions, invalid-point rejection, random and
  deterministic ECDSA, and signature tamper rejection.
- `XSslTest.c` checks PSA P-521 generation, export, import, bidirectional
  agreement, signing, verification, tamper rejection, and the existing
  XSslSocket TLS loopback.

Focused evidence:

- `/tmp/xinyuec-static-p521-ecdH-20260813.log`
- `/tmp/xinyuec-dynamic-p521-ecdsa-20260813.log`
- `/tmp/xinyuec-static-p521-ecdsa-xssl-20260813.log`
- `/tmp/xinyuec-dynamic-p521-ecdsa-xssl-rerun.log`

All focused primitive and XSsl processes returned exit code 0 and report
`P-521 ECDH OpenSSL vectors`, `PSA P-521 ECDH/ECDSA uses XCryptographic`, and
passing XSslSocket local TLS handshake/record tests.

Full-suite evidence after the P-521 migration:

- `/tmp/xinyuec-static-all-p521-final-20260813.log`
- `/tmp/xinyuec-dynamic-all-p521-final-20260813.log`

Both full logs contain passing XCryptographic primitives, PSA crypto, XSsl,
and XSslSocket results, including P-521. The static and dynamic overall
processes still report unrelated dirty-worktree failures in XProcess and/or
XConsoleShell account fixtures; those failures occur before or outside the
crypto/TLS sections and do not affect the focused migration results.

## 2026-08-13 brainpoolP384r1 and X448 Migration

The remaining enabled 384-bit brainpool curve and the X448 Montgomery curve
now use XCryptographic through the PSA builtin ECP adapter.

- `XCryptographic.c` adds brainpoolP384r1 ECDH, random and RFC 6979
  deterministic ECDSA, point validation, import/export, and 48-byte shared
  secret handling. Its ECDSA controls are separate default-on feature macros:
  `XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P384R1_ON` and
  `XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_BRAINPOOL_P384R1_ON`.
- X448 is implemented with the RFC 7748 Montgomery ladder over
  `2^448 - 2^224 - 1`, including scalar clamping and 56-byte little-endian
  key/public/shared-secret wire formats.
- `psa_crypto_ecp.c` routes X448 import, generation, public export, and raw
  ECDH to XCryptographic. brainpoolP384r1 import, generation, public export,
  raw ECDH, ECDSA sign, deterministic sign, and verify are also routed.
- Primitive coverage checks brainpoolP384r1 ECDH/ECDSA and RFC 7748 X448
  public-key and shared-secret vectors. XSsl coverage checks PSA key lifecycle
  and bidirectional ECDH for X448, and the full brainpoolP384r1 ECDH/ECDSA
  path. Existing XSslSocket TLS loopback coverage runs in the same target.

Focused static and dynamic evidence:

- `/tmp/xcryptographic-x448.log`
- `/tmp/xssl-x448.log`
- `/tmp/xcryptographic-x448-dynamic.log`
- `/tmp/xssl-x448-dynamic.log`

All four commands returned exit code 0. The logs report
`X448 RFC 7748 vectors`, `PSA X448 uses XCryptographic`,
`PSA brainpoolP384r1 ECDH/ECDSA uses XCryptographic`, and
`XSslSocket local TLS 1.2 handshake and records` as passing.

## 2026-08-13 Multipart AEAD and Generic KDF Migration

The remaining active PSA multipart AEAD operations now use XCryptographic,
not only the one-shot helpers. `XCryptographic_AeadOperation` retains the
algorithm, key, nonce, authenticated-data and payload state for AES/ARIA/
Camellia GCM/CCM and ChaCha20-Poly1305. The PSA adapter retains only a pointer
in its existing private operation union, so the PSA public operation layout is
unchanged.

- GCM uses incremental GHASH and counter-mode state; CCM retains CBC-MAC and
  CTR state; ChaCha20-Poly1305 retains the ChaCha20 and Poly1305 streams.
- `psa_crypto_aead.c` routes setup, nonce, length/tag configuration,
  additional-data update, payload update, finish and abort through the new
  XCryptographic operation. The adapter preserves PSA state and
  `PSA_ERROR_BUFFER_TOO_SMALL` behavior.
- XSsl coverage compares split AAD/payload multipart output against one-shot
  output for AES/ARIA/Camellia GCM/CCM and ChaCha20-Poly1305. It also checks
  zero-length updates, bad-tag rejection, unset-nonce state rejection and
  failed-operation cleanup.

The HMAC-backed KDF implementation is now generic over the enabled digest
family. `XCryptographic_hkdf*Into`, `XCryptographic_tls12PrfInto`, and
`XCryptographic_tls12PskToMsInto` receive an `XCryptographicHash_Algorithm`;
the original SHA-256 APIs remain compatibility wrappers. The PSA KDF adapter
maps the PSA algorithm hash and uses that path for HKDF extract/expand and
TLS 1.2 PRF/PSK-to-MS. PBKDF2-HMAC was already generic.

New SHA-384 PSA vectors use split output reads for both HKDF and TLS 1.2 PRF,
so the adapter's hash selection is verified independently of SHA-256.

Focused evidence after this batch:

- `/tmp/xcryptographic-kdf-general2.log`
- `/tmp/xcryptographic-kdf-general2-dynamic.log`
- `/tmp/xssl-aead-expanded2.log`
- `/tmp/xssl-aead-expanded2-dynamic.log`
- `/tmp/xssl-kdf-sha384-static.log`
- `/tmp/xssl-kdf-sha384-dynamic.log`

All focused primitive and XSsl commands return zero. The XSsl logs include
the local TLS 1.2 XSslSocket handshake and record exchange; they also include
the SHA-384 KDF vectors and the multipart AEAD state/error coverage.

## 2026-08-13 HMAC Multipart and HKDF-Extract Migration

PSA HMAC multipart operations now use the native
`XCryptographic_HmacOperation` instead of retaining an mbedTLS hash/HMAC
state machine. The XCryptographic operation performs RFC 2104 key
normalization, keeps the inner hash state, retains the outer pad, and provides
setup, update, finish, and abort operations. This also makes the HKDF-extract
PSA path fully use XCryptographic, because it is implemented as HMAC with an
empty message.

`mbedtls_psa_hmac_operation_t` keeps a fixed union containing both the legacy
layout and the XCryptographic context pointer. This is intentional: the mbedTLS
library is compiled with the XCryptographic feature definition, while public
PSA callers need the same operation-object ABI without that private definition.

- `XCryptographicPrimitive_test_hmac_stream` verifies the RFC 4231 HMAC-SHA-384
  vector with segmented input.
- XSsl verifies PSA HMAC plus HKDF-extract for SHA-256 and SHA-384, including
  the RFC 5869 pseudo-random-key results.
- The static and dynamic test executables compile against the public PSA header
  without the mbedTLS-private feature macro, exercising the fixed operation
  layout at runtime.

Focused evidence:

- `/tmp/xcryptographic-hmac-abi-static.log`
- `/tmp/xcryptographic-hmac-abi-dynamic.log`
- `/tmp/xssl-hmac-abi-static.log`
- `/tmp/xssl-hmac-abi-dynamic.log`

Final full-suite evidence after all currently enabled PSA algorithm routes:

- `/tmp/xinyuec-static-all-hmac-final.log`
- `/tmp/xinyuec-dynamic-all-hmac-final.log`

Both full suites report `[RESULT] XCryptographic primitives: PASS` and
`[RESULT] XSsl tests: PASS`. The XSsl result includes the local TLS 1.2
XSslSocket client/server handshake and record-exchange test.

## 2026-08-12 15:45 暂停时进度（Header 重建批次）

### 已完成

- 确认 `Src/XCode/XCryptographic/XCryptographic.c` 已包含以下实现：
  - RSA 全套（DER 导入/导出、生成、PKCS#1 v1.5、OAEP、PSS 加解密/签名/验签）。
  - FFDH（RFC 7919 私钥生成、公钥导出、共享密钥）。
  - ARIA 和 Camellia 分组密码内部实现（key schedule、ECB/CBC/CFB/OFB 经 block cipher 路径）。
  - SHAKE128/256 XOF、AES-CMAC、ChaCha20 原始流、AES CCM*-无标签、AEAD（AES-GCM/AES-CCM/ChaCha20-Poly1305）、KDF 批（HKDF、TLS12 PRF/PSK-to-MS、PBKDF2-HMAC-SHA256、PBKDF2-AES-CMAC-PRF-128）。
  - 确定性 P-256 ECDSA（RFC 6979）。
- 重建 `Src/XCode/XCryptographic/XCryptographic.h` 缺失的公开声明：
  - 扩展 `XCryptographic_KeyType`（新增 AesGcm/AesCcm/ChaCha20Poly1305）。
  - 新增 XOF、BlockCipher（AES/ARIA/Camellia）、ChaCha20、AES-CMAC、CCM*-无标签、AEAD、KDF、FFDH、ECDH 导入、确定性 ECDSA、RSA 的类型/枚举/函数声明。
  - RSA key 改为不透明类型（`typedef struct XCryptographic_RsaKey XCryptographic_RsaKey;`），完整定义保留在 .c 中。

### 已完成 / 已验证

- 已恢复 RIPEMD-160 公开枚举声明并完成构建。
- 已完成静态/动态 `xcryptographic` 和 `xssl` 聚焦测试。
- 已完成静态/动态全量测试；密码与 XSsl 均通过，唯一失败是独立的
  XConsoleShell `useradd testuser` 测试。
- 已补充 RSA、FFDH、ARIA、Camellia、确定性 ECDSA 的向量/适配器测试。

### 当时仍待迁移的算法族（历史记录，后续批次已更新）

参考文档“Remaining Algorithm Families”，当前仍由 tf-psa-crypto 承担：
- 其他椭圆曲线：secp192/224、Ed448 等；brainpool P-384/P-512 与 X448 已在后续批次完成。
- AES-XTS 等剩余 AES 模式；AES-KW/KWP 已在本批迁移。
- DRBG/entropy/random 实现边界。
- PQCP 驱动路径（`MBEDTLS_ENABLE_PQCP=ON`）。

## 2026-08-13 ML-DSA-87 可选算法下沉

PQCP 审计确认目前只有 ML-DSA-87 的实验性实现入口，mbedTLS 尚未提供
可调用的 PSA ML-DSA 算法接口。因此新增了 XCryptographic 的可选
ML-DSA-87 原语，而没有虚构 PSA API：

- 根 CMake 新增 `XCRYPTOGRAPHIC_ENABLE_MLDSA87`，默认 OFF。
- `Library/mbedtls/library/xcryptographic/XCryptographic_mldsa87.c` 提供
  32 字节种子密钥生成、FIPS 204 上下文
  签名、验签和篡改拒绝；mldsa-native 符号限制在单编译单元内部。
- `zetas.inc` 固定 NTT 常量放入 `Src/XCode/XCryptographic`，不依赖 PQCP
  源包中缺失的生成产物。
- 开启 ML-DSA 时，mbedTLS 不编译旧的 `mldsa-native`、`wrap_mldsa_native.c`
  和 `psa_crypto_mldsa.c`；`MBEDTLS_ENABLE_PQCP=ON` 组合构建也已验证。

验证结果：

- `build-mldsa` 静态/动态 `--test xcryptographic`：ML-DSA-87
  keygen/sign/verify 通过。
- `build-mldsa-pqcp`（`MBEDTLS_ENABLE_PQCP=ON` 与
  `XCRYPTOGRAPHIC_ENABLE_MLDSA87=ON`）完整构建通过，mbedTLS 归档无旧
  ML-DSA 对象。

`Library/mbedtls/tf-psa-crypto` 目录已于 2026-08-13 开始迁移。该段记录
  是历史状态；后续已将 PSA 公共声明和兼容实现重新归属到 mbedTLS，详见
  文档末尾的最新批次。旧的 `tf-psa-crypto`、`include/psa-crypto` 和
  `library/psa` 路径均不再作为构建输入。

## 2026-08-14 PSA 彻底移除后的依赖方测试

目标：删除 PSA 后，所有依赖 mbedTLS 的组件都要有测试证据。

- 默认 `build` 重新 configure + build 通过；`./bin/XinYueC_* --test all`
  中与密码/SSL/SSH 相关的测试全部 PASS：
  - XCryptographic primitives PASS、XSsl tests PASS（含 PSA 各算法族
    “uses XCryptographic” 断言、TLS 1.2 memory-BIO 握手、本地 TLS 1.2
    XSslSocket 握手）。
  - XProcess、SSH 客户端内存端到端、Telnet 协议、SSH/Telnet 后端均 PASS。
  - 动态版 `--test all` 整体 exit=0；静态版整体 exit=1 仅因无关的
    XConsoleShell 脏树 `non-admin passwd` 断言（与本次 PSA 迁移无关）。
- 补充驱动直接注册并执行 XHttp / XServerChan / XFtp / XMqtt 菜单测试：
  - XHttp “核心头部、请求、MIME和响应解析”：通过（含 HTTP/2,
    ALPN, manager SSL API, NTLMv2 本地 HTTP 认证）。
  - XServerChan “类生命周期、表单发送和响应解析”：通过（本地 HTTP +
    JSON；未配置 SendKey，实际外发跳过）。
  - XFtp “02 传输模式/类型/SSL/自动重连” 与 “09 SSL 配置”：通过。
  - XMqtt “Client单元测试”：19 项全部通过，含
    `connectToHostEncrypted` 空主机错误状态。
- 上述驱动为临时文件 `/tmp/run_mbedtls_dep_tests.c`，未进入仓库。

- XSocketTest 的“百度 HTTPS 测试”（`XSocketTest_BaiduHttps`）也已实际运行：
  mbedtls 后端、TLS 1.2 握手成功（`TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256`），
  向 `www.baidu.com:443` 发送 HTTPS GET 收到 HTTP/1.1 200，读取约 30KB 响应。

## 2026-08-14 新增文件注释中文化

用户要求将迁移过程中新增文件里的英文注释改为中文（可参考 git 确认新增范围）。

### 已改为中文注释的文件

- `Src/XCode/XCryptographic/XCryptographic.c` / `.h`：本轮迁移新增的
  大整数进位、P-384 回绕、P-521 limb、Jacobian 二倍点、RFC 7919 DH、
  SHA-3 海绵速率注释，以及头文件 RIPEMD-160、PKCS#1 v1.5、OAEP、PSS
  中文说明。
- `Test/XCodeTest/XCryptographicPrimitiveTest.c`：ECJPAKE 换密码重跑、
  ChaCha20 块边界拆分、RFC 7919 FFDH 期望值等注释。
- `Test/XIOTest/XSslTest.c`：ChaCha20 缺 IV/零长度输入/缓冲区过小/块边界，
  HKDF-SHA-384 分段、TLS 1.2 PRF 分段、PSK 转主密钥（纯/混合）、
  PBKDF2-AES-CMAC 等注释。
- `Library/mbedtls/platform/XSsl_mbedtls_lms.c`：文件头注释。

### 保留英文注释的文件

- mbedTLS 上游头文件、`include/psa` 头文件、`lmots.h`
  （TF-PSA-Crypto 上游头）、`third_party/mldsa-native`（PQCP 第三方源码）、
  `zetas.inc`（数据文件）。这些为上游/第三方原样文件，保留原文以与上游一致。

### 验证结果

- 默认 `build` 重新构建通过。
- `./bin/XinYueC_Static --test xssl`：`[RESULT] XSsl tests: PASS`。
- `./bin/XinYueC_Static --test xcryptographic`：
  `[RESULT] XCryptographic primitives: PASS`。

## 2026-08-14 PSA 目录最终清理与可插拔后端

本批次按用户确认完成最终目录清理：

- 删除 `Library/mbedtls/include/psa-crypto`。
- 删除 `Library/mbedtls/library/psa`。
- PSA 公共兼容声明移动到 `Library/mbedtls/include/psa`，保持 mbedTLS
  对外 PSA ABI 头文件路径不变。
- PSA 兼容实现移动到 `Library/mbedtls/library/xcryptographic`，由 mbedTLS
  目标独立编译；算法调用使用 `Src/XCode/XCryptographic/XCryptographic.h`
  的 API。`Src/XCode/XCryptographic` 不包含 mbedTLS/PSA 外部库源码。
- 根工程排除 mbedTLS 后端实现源码，避免其进入 XinYueC 主库；mbedTLS
  可单独替换或链接 XCryptographic 后端。
- ML-DSA-87 的第三方 `mldsa-native` 源码和 wrapper 位于
  `Library/mbedtls/library/xcryptographic`；`Src/XCode/XCryptographic`
  下不再包含第三方源码。该功能仍由 `XCRYPTOGRAPHIC_ENABLE_MLDSA87`
  控制，默认关闭。

验证结果：

- 默认 Debug 配置重新 configure + build 通过。
- 静态/动态 `--test xcryptographic`：PASS。
- 静态/动态 `--test xssl`：PASS，包含 TLS 1.2 memory-BIO、XSslSocket
  本地握手和所有已迁移算法路由断言。
- 动态 `--test all`：exit=0，密码与 XSsl 均 PASS。
- 静态 `--test all`：密码与 XSsl 均 PASS；整体 exit=1 仅因既有的
  XConsoleShell `login testadmin` 断言。
