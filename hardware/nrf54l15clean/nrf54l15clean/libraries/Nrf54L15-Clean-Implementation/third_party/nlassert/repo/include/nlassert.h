#pragma once
#include <stdlib.h>
#define NL_ASSERT(x) do { if (!(x)) abort(); } while(0)
#define NL_TEST_ASSERT(a, x) do { if (!(x)) abort(); } while(0)
#define NL_TEST_ASSERT_FMT(a, x, ...) do { if (!(x)) abort(); } while(0)
#define nlASSERT(x) NL_ASSERT(x)
#define nlTestAssert(a, x) NL_TEST_ASSERT(a, x)
#define nlTestAssertFmt(a, x, ...) NL_TEST_ASSERT_FMT(a, x, __VA_ARGS__)
