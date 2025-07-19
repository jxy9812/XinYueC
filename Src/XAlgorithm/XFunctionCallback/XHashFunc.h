//哈希函数
#ifndef XHASH_H
#define XHASH_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
size_t XHashMap_murmur3_32(const void* key, size_t len);
size_t XHashMap_fnv1a_64(const void* key, size_t len);
size_t XHashMap_cityhash64(const void* key, size_t len);
#endif // !XFUNCTIONPOINTER_H