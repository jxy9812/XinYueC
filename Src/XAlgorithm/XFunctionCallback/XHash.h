//哈希函数
#ifndef XHASH_H
#define XHASH_H
#include <stdbool.h>
#include <stdint.h>
size_t XHash_murmur3_32(const void* key, size_t len);
#endif // !XFUNCTIONPOINTER_H