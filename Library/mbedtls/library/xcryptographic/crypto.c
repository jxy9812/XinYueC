/*
 * Minimal PSA compatibility layer backed entirely by XCryptographic.
 */
#include "tf_psa_crypto_common.h"
#include <psa/crypto.h>
#include "psa_util_internal.h"
#include "md_psa.h"
#include "XCryptographic.h"
