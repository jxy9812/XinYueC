#include"XCompare.h"
#include<string.h>
#include"XString.h"
#include"XPoint.h"
int32_t bool_compare(const _Bool* lhs, const _Bool* rhs) {
    if ((*lhs)<(*rhs))return -1; else if ((*lhs)>(*rhs))return 1; return 0;
};
XCompare_ComeTwo(unsigned, char);
XCompare_Come(char);
XCompare_Come(int);
XCompare_Come(long);
XCompare_Come(float);
XCompare_Come(double);
XCompare_Come(uint8_t);
XCompare_Come(uint16_t);
XCompare_Come(uint32_t);
XCompare_Come(uint64_t);
XCompare_Come(int8_t);
XCompare_Come(int16_t);
XCompare_Come(int32_t);
XCompare_Come(int64_t);
XCompare_Come(size_t);
XCompare_Come(uintptr_t);