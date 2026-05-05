#include"XAtomic.h"
size_t XAtomic_index_bits(size_t max_value)
{
    if (max_value == 0) return 1;
    size_t bits = 0;
    while (max_value > 0) {
        bits++;
        max_value >>= 1;
    }
    return bits;
}

size_t XAtomic_pack_index_version(size_t index, size_t version, size_t index_bits, uintptr_t version_mask)
{
    size_t index_part = index;
    size_t version_part = (version  & version_mask) << index_bits;
    return index_part | version_part;
}

size_t XAtomic_unpack_index(size_t packed, size_t index_mask)
{
    return packed & index_mask;
}

size_t XAtomic_unpack_version(size_t packed, size_t index_bits, size_t version_mask)
{
    return (packed >> index_bits) & version_mask;
}
