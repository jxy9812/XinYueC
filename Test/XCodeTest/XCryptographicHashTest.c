#include "XCodeTest.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XByteArray.h"
#include "XCryptographicHash.h"
#include <stdio.h>
#include <string.h>

// 辅助函数：打印十六进制哈希值
static void print_hash_hex(const char* label, const uint8_t* hash, int len)
{
    XPrintf("%s: ", label);
    for (int i = 0; i < len; i++) {
        XPrintf("%02x", hash[i]);
    }
    XPrintf("\n");
}

// 辅助函数：比较哈希值
static bool compare_hash(const uint8_t* hash1, const uint8_t* hash2, int len)
{
    return memcmp(hash1, hash2, len) == 0;
}

// 测试向量
static void test_md5(void)
{
    XPrintf("\n========== MD5 Tests ==========\n");
    
    // 测试向量来自 RFC 1321
    const char* inputs[] = {
        "",           // 空字符串
        "a",          // 单字符
        "abc",        // 简短字符串
        "message digest",
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
    };
    
    // 预期 MD5 值
    const char* expected[] = {
        "d41d8cd98f00b204e9800998ecf8427e",
        "0cc175b9c0f1b6a831c399e269772661",
        "900150983cd24fb0d6963f7d28e17f72",
        "f96b697d7cb7938d525a2f31aaf161d0",
        "c3fcd3d76192e4007dfb496cca67e13b",
        "d174ab98d277d9f5a5611c2c9f419d9f",
        "57edf4a22be3c955ac49da2e2107b67a"
    };
    
    for (int i = 0; i < 7; i++) {
        XByteArray* result = XCryptographicHash_hash(inputs[i], strlen(inputs[i]), XCryptographicHash_Md5);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            XPrintf("Test %d (\"%s\"): ", i + 1, inputs[i][0] ? inputs[i] : "(empty)");
            print_hash_hex("", hash, 16);
            XPrintf("Expected: %s\n", expected[i]);
            XByteArray_delete_base(result);
        }
    }
}

static void test_sha1(void)
{
    XPrintf("\n========== SHA-1 Tests ==========\n");
    
    // 测试向量
    const char* inputs[] = {
        "abc",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        ""
    };
    
    const char* expected[] = {
        "a9993e364706816aba3e25717850c26c9cd0d89d",
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
        "da39a3ee5e6b4b0d3255bfef95601890afd80709"
    };
    
    for (int i = 0; i < 3; i++) {
        XByteArray* result = XCryptographicHash_hash(inputs[i], strlen(inputs[i]), XCryptographicHash_Sha1);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            XPrintf("Test %d: ", i + 1);
            print_hash_hex("SHA1", hash, 20);
            XPrintf("Expected: %s\n", expected[i]);
            XByteArray_delete_base(result);
        }
    }
}

static void test_sha256(void)
{
    XPrintf("\n========== SHA-256 Tests ==========\n");
    
    const char* inputs[] = {
        "abc",
        "",
        "hello world"
    };
    
    const char* expected[] = {
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9"
    };
    
    for (int i = 0; i < 3; i++) {
        XByteArray* result = XCryptographicHash_hash(inputs[i], strlen(inputs[i]), XCryptographicHash_Sha256);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            XPrintf("Test %d (\"%s\"):\n", i + 1, inputs[i][0] ? inputs[i] : "(empty)");
            print_hash_hex("SHA256", hash, 32);
            XPrintf("Expected: %s\n", expected[i]);
            XByteArray_delete_base(result);
        }
    }
}

static void test_sha512(void)
{
    XPrintf("\n========== SHA-512 Tests ==========\n");
    
    const char* inputs[] = {
        "abc",
        "",
        "The quick brown fox jumps over the lazy dog"
    };
    
    const char* expected[] = {
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
        "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6"
    };
    
    for (int i = 0; i < 3; i++) {
        XByteArray* result = XCryptographicHash_hash(inputs[i], strlen(inputs[i]), XCryptographicHash_Sha512);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            XPrintf("Test %d (\"%s\"):\n", i + 1, inputs[i][0] ? inputs[i] : "(empty)");
            print_hash_hex("SHA512", hash, 64);
            XPrintf("Expected: %s\n", expected[i]);
            XByteArray_delete_base(result);
        }
    }
}

static void test_sha3(void)
{
    XPrintf("\n========== SHA-3 Tests ==========\n");
    
    // SHA3-256 测试
    const char* input = "abc";
    XByteArray* result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_RealSha3_256);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("SHA3-256(\"abc\")", hash, 32);
        XPrintf("Expected: 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532\n");
        XByteArray_delete_base(result);
    }
    
    // SHA3-512 测试
    result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_RealSha3_512);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("SHA3-512(\"abc\")", hash, 64);
        XByteArray_delete_base(result);
    }
    
    // 空字符串测试
    result = XCryptographicHash_hash("", 0, XCryptographicHash_RealSha3_256);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("SHA3-256(\"\")", hash, 32);
        XPrintf("Expected: a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a\n");
        XByteArray_delete_base(result);
    }
}

static void test_keccak(void)
{
    XPrintf("\n========== Keccak Tests ==========\n");
    
    // Keccak-256 测试（以太坊使用）
    const char* input = "abc";
    XByteArray* result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Keccak_256);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Keccak-256(\"abc\")", hash, 32);
        XPrintf("Expected: 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45\n");
        XByteArray_delete_base(result);
    }
    
    // Keccak-512 测试
    result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Keccak_512);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Keccak-512(\"abc\")", hash, 64);
        XByteArray_delete_base(result);
    }
}

static void test_blake2b(void)
{
    XPrintf("\n========== Blake2b Tests ==========\n");
    
    const char* input = "abc";
    
    // Blake2b-256
    XByteArray* result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Blake2b_256);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Blake2b-256(\"abc\")", hash, 32);
        XPrintf("Expected: bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319\n");
        XByteArray_delete_base(result);
    }
    
    // Blake2b-512
    result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Blake2b_512);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Blake2b-512(\"abc\")", hash, 64);
        XByteArray_delete_base(result);
    }
}

static void test_blake2s(void)
{
    XPrintf("\n========== Blake2s Tests ==========\n");
    
    const char* input = "abc";
    
    // Blake2s-256
    XByteArray* result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Blake2s_256);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Blake2s-256(\"abc\")", hash, 32);
        XByteArray_delete_base(result);
    }
    
    // Blake2s-128
    result = XCryptographicHash_hash(input, strlen(input), XCryptographicHash_Blake2s_128);
    if (result) {
        const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
        print_hash_hex("Blake2s-128(\"abc\")", hash, 16);
        XByteArray_delete_base(result);
    }
}

static void test_incremental_hashing(void)
{
    XPrintf("\n========== Incremental Hashing Test ==========\n");
    
    // 测试增量哈希
    XCryptographicHash* ctx = XCryptographicHash_create(XCryptographicHash_Sha256);
    if (ctx) {
        XCryptographicHash_addData(ctx, "Hello, ", 7);
        XCryptographicHash_addData(ctx, "World!", 6);
        
        XByteArray* result = XCryptographicHash_result(ctx);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            print_hash_hex("SHA256(\"Hello, World!\")", hash, 32);
            XPrintf("Expected: dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f\n");
            XByteArray_delete_base(result);
        }
        
        // 测试 reset
        XCryptographicHash_reset(ctx);
        XCryptographicHash_addData(ctx, "New data", 8);
        result = XCryptographicHash_result(ctx);
        if (result) {
            const uint8_t* hash = (const uint8_t*)XByteArray_data(result);
            print_hash_hex("SHA256(\"New data\")", hash, 32);
            XByteArray_delete_base(result);
        }
        
        XCryptographicHash_delete_base(ctx);
    }
}

static void test_hash_into(void)
{
    XPrintf("\n========== hashInto Test ==========\n");
    
    char buffer[64];
    XByteArrayView view = XCryptographicHash_hashInto(buffer, sizeof(buffer), "test", 4, XCryptographicHash_Sha256);
    
    if (view.data) {
        print_hash_hex("hashInto SHA256(\"test\")", (const uint8_t*)view.data, (int)view.size);
        XPrintf("Expected: 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n");
    }
}

static void test_algorithm_info(void)
{
    XPrintf("\n========== Algorithm Info Test ==========\n");
    
    XPrintf("Supported algorithms:\n");
    for (int i = 0; i < XCryptographicHash_NumAlgorithms; i++) {
        XPrintf("  %s: %d bytes, supported: %s\n",
               XCryptographicHash_algorithmName(i),
               XCryptographicHash_hashLength(i),
               XCryptographicHash_supportsAlgorithm(i) ? "yes" : "no");
    }
}

static void test_copy_move(void)
{
    XPrintf("\n========== Copy/Move Test ==========\n");
    
    // 创建原始对象
    XCryptographicHash* original = XCryptographicHash_create(XCryptographicHash_Sha256);
    XCryptographicHash_addData(original, "test data", 9);
    
    // 测试拷贝 - 使用栈对象
    XCryptographicHash copied;
    XCryptographicHash_init(&copied, XCryptographicHash_Sha256);
    XCryptographicHash_copy_base(&copied, original);
    
    XByteArray* result1 = XCryptographicHash_result(original);
    XByteArray* result2 = XCryptographicHash_result(&copied);
    
    if (result1 && result2) {
        const uint8_t* hash1 = (const uint8_t*)XByteArray_data(result1);
        const uint8_t* hash2 = (const uint8_t*)XByteArray_data(result2);
        
        print_hash_hex("Original SHA256", hash1, 32);
        print_hash_hex("Copied SHA256", hash2, 32);
        
        if (compare_hash(hash1, hash2, 32)) {
            XPrintf("Copy test: PASSED\n");
        } else {
            XPrintf("Copy test: FAILED\n");
        }
    }
    
    if (result1) XByteArray_delete_base(result1);
    if (result2) XByteArray_delete_base(result2);
    
    // 测试移动 - 使用栈对象
    XCryptographicHash moved;
    XCryptographicHash_init(&moved, XCryptographicHash_Sha256);
    XCryptographicHash_move_base(&moved, original);
    
    XPrintf("Move test: completed\n");
    
    XCryptographicHash_delete_base(original);
    XCryptographicHash_deinit_base(&copied);
    XCryptographicHash_deinit_base(&moved);
}

void XCryptographicHashTest(void)
{
    XPrintf("=== XCryptographicHash Comprehensive Test ===\n");
    
    test_md5();
    test_sha1();
    test_sha256();
    test_sha512();
    test_sha3();
    test_keccak();
    test_blake2b();
    test_blake2s();
    test_incremental_hashing();
    test_hash_into();
    test_algorithm_info();
    test_copy_move();
    
    XPrintf("\n=== All XCryptographicHash tests completed ===\n");
}

void XMenu_XCryptographicHashTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCryptographicHash(加密哈希)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XCryptographicHashTest);
    }
}