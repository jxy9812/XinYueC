// Minimal XTest compatibility shim for XFtpTest
// Maps XASSERT_* macros to standard assert
#ifndef XTEST_H
#define XTEST_H

#include <assert.h>
#include <string.h>

#define XASSERT(cond)                 assert(cond)
#define XASSERT_TRUE(cond)            assert(cond)
#define XASSERT_FALSE(cond)           assert(!(cond))
#define XASSERT_NOT_NULL(p)           assert((p) != NULL)
#define XASSERT_NULL(p)               assert((p) == NULL)
#define XASSERT_EQ(a, b)              assert((a) == (b))
#define XASSERT_NE(a, b)              assert((a) != (b))
#define XASSERT_STR_EQ(a, b)          assert(strcmp((a), (b)) == 0)

#endif // XTEST_H
