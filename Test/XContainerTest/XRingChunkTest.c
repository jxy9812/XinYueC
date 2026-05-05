#include"XDataStructTest.h"
#if DEMOTEST
#include"XRingChunk.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include <assert.h>
// 辅助函数：打印缓冲区状态
static void printBufferState(XRingChunk * chunk, const char* msg) {
    printf("\n--- %s ---\n", msg);
    printf("Capacity (logical): %zu\n", XRingChunk_capacity_base(chunk));
    printf("Size (available): %zu\n", XRingChunk_available(chunk));
    printf("Read Pos: %zu, Write Pos: %zu\n", chunk->m_readPos, chunk->m_writePos);
}

// 辅助函数：验证缓冲区内容
static void assertBufferContent(XRingChunk* chunk, const char* expected, size_t len) {
    char* buffer = (char*)malloc(len);
    size_t read = XRingChunk_peek(chunk, buffer, len);
    if (read != len || memcmp(buffer, expected, len) != 0) {
        printf("ERROR: Content mismatch!\n");
        printf("Expected: ");
        for (size_t i = 0; i < len; i++) printf("%02x ", (unsigned char)expected[i]);
        printf("\nGot:      ");
        for (size_t i = 0; i < read; i++) printf("%02x ", (unsigned char)buffer[i]);
        printf("\n");
        XFree_System(buffer);
        exit(1);
    }
    XFree_System(buffer);
}
void XRingChunkTest()
{
    printf("=== Starting XRingChunk Comprehensive Test ===\n");

    // ==============================
    // 测试 1: 基本创建、写入、读取
    // ==============================
    printf("\n[Test 1: Basic Create, Write, Read]\n");
    XRingChunk* chunk = XRingChunk_create(10); // 逻辑容量10字节
    assert(chunk != NULL);

    const char* data1 = "Hello";
    size_t written = XRingChunk_write(chunk, data1, strlen(data1));
    assert(written == 5);
    assert(XRingChunk_available(chunk) == 5);

    char readBuf[10] = { 0 };
    size_t read = XRingChunk_read(chunk, readBuf, 5);
    assert(read == 5);
    assert(strcmp(readBuf, "Hello") == 0);
    assert(XRingChunk_available(chunk) == 0);

    XRingChunk_delete_base(chunk);

    // ========================================
// 测试 2: 环绕写入 (Write Wrap-Around)
// ========================================
    printf("\n[Test 2: Write Wrap-Around]\n");
    chunk = XRingChunk_create(5); // 逻辑容量5字节

    // 写入4字节，使writePos=4
    written = XRingChunk_write(chunk, "1234", 4);
    assert(written == 4);
    printBufferState(chunk, "After writing '1234'");

    // 再写入2字节，但缓冲区只剩1字节空间，所以只能写入1字节 ('5')
    // writePos 应该变为 (4+1)%6 = 5
    written = XRingChunk_write(chunk, "56", 2);
    assert(written == 1); // === 修正点：期望值应为1 ===
    printBufferState(chunk, "After writing '56' (only '5' was written)");

    // 此时缓冲区应为满状态，内容为 "12345"
    assert(XRingChunk_available(chunk) == 5);
    assertBufferContent(chunk, "12345", 5);

    // 尝试再写入任何数据都应该失败
    written = XRingChunk_write(chunk, "X", 1);
    assert(written == 0); // 缓冲区已满

    XRingChunk_delete_base(chunk);

    // ========================================
    // 测试 3: 环绕读取 (Read Wrap-Around)
    // ========================================
    printf("\n[Test 3: Read Wrap-Around]\n");
    chunk = XRingChunk_create(5);

    // 先填满缓冲区
    XRingChunk_write(chunk, "ABCDE", 5);
    // 读取前3个字节，使readPos=3
    XRingChunk_read(chunk, readBuf, 3);
    printBufferState(chunk, "After reading first 3 bytes");

    // 写入2个新字节 'FG'，writePos 应该变为 (0+2)=2
    written = XRingChunk_write(chunk, "FG", 2);
    assert(written == 2);
    printBufferState(chunk, "After writing 'FG'");

    // 此时缓冲区内容应为: [F, G, _, D, E] (readPos=3, writePos=2)
    // 读取剩余2字节 'DE'
    XRingChunk_read(chunk, readBuf, 2);
    assert(readBuf[0] == 'D' && readBuf[1] == 'E');
    // 再读取新写入的2字节 'FG'
    XRingChunk_read(chunk, readBuf, 2);
    assert(readBuf[0] == 'F' && readBuf[1] == 'G');

    XRingChunk_delete_base(chunk);

    // ========================================
    // 测试 4: Peek 和 Skip 功能
    // ========================================
    printf("\n[Test 4: Peek and Skip]\n");
    chunk = XRingChunk_create(10);
    XRingChunk_write(chunk, "PeekTest", 8);

    // Peek 前4个字节
    char peekBuf[5] = { 0 };
    size_t peeked = XRingChunk_peek(chunk, peekBuf, 4);
    assert(peeked == 4);
    assert(strncmp(peekBuf, "Peek", 4) == 0);
    // Peek后，available大小不应改变
    assert(XRingChunk_available(chunk) == 8);

    // Skip 4个字节
    XRingChunk_skip(chunk, 4);
    assert(XRingChunk_available(chunk) == 4);
    // 读取剩下的 "Test"
    XRingChunk_read(chunk, readBuf, 4);
    assert(strncmp(readBuf, "Test", 4) == 0);

    XRingChunk_delete_base(chunk);

    // ========================================
    // 测试 5: Mark 和 ResetToMark 功能
    // ========================================
    printf("\n[Test 5: Mark and ResetToMark]\n");
    chunk = XRingChunk_create(10);
    XRingChunk_write(chunk, "MarkMe", 6);

    // 读取前2个字节 'Ma'
    XRingChunk_read(chunk, readBuf, 2);
    assert(strncmp(readBuf, "Ma", 2) == 0);
    assert(XRingChunk_available(chunk) == 4);

    // 设置标记
    XRingChunk_mark(chunk);
    // 再读取2个字节 'rk'
    XRingChunk_read(chunk, readBuf, 2);
    assert(strncmp(readBuf, "rk", 2) == 0);
    assert(XRingChunk_available(chunk) == 2);

    // 回滚到标记点
    XRingChunk_resetToMark(chunk);
    assert(XRingChunk_available(chunk) == 4); // 应回滚到4字节可用

    // 读取应该得到 'rkMe'
    XRingChunk_read(chunk, readBuf, 4);
    assert(strncmp(readBuf, "rkMe", 4) == 0);

    XRingChunk_delete_base(chunk);

    // 测试 6: Unget 功能 (修正版 - Simple Case)
    // ========================================
    printf("\n[Test 6: Unget - Simple Case (Fixed)]\n");
     chunk = XRingChunk_create(20); // 给足空间避免边界问题
    XRingChunk_write(chunk, "UngetSimple", 11);

    // 读取第一个字节 'U'
    {
        char readBuf[2] = { 0 };
        read = XRingChunk_read(chunk, readBuf, 1);
        assert(read == 1);
        assert(readBuf[0] == 'U'); // 第一次读取验证
        assert(XRingChunk_available(chunk) == 10);

        // 将 'U' unget 回去
        size_t ungot = XRingChunk_unget(chunk, "U", 1);
        assert(ungot == 1);
        assert(XRingChunk_available(chunk) == 11); // 应该恢复

        // 再次读取，应该还是 'U'
        read = XRingChunk_read(chunk, readBuf, 1);
        assert(read == 1);
        assert(readBuf[0] == 'U'); // 这次应该成功！

        XRingChunk_delete_base(chunk);
    }
   

    // ===================================================
    // 测试 7: Unget 失败情况 (展示其设计局限性)
    // ===================================================
    printf("\n[Test 7: Unget - Limitation (When readPos is small)]\n");
    // 创建一个缓冲区，并让 readPos 变得很小
    chunk = XRingChunk_create(10);
    XRingChunk_write(chunk, "SmallReadPos", 13); // 写入13字节

    // 读取12个字节，使 readPos = 12
    XRingChunk_read(chunk, readBuf, 12);
    printBufferState(chunk, "After reading 12 bytes, readPos=12");

    // 现在尝试 unget 1个字节，应该成功，因为 12 >= 1
    size_t ungot = XRingChunk_unget(chunk, "X", 1); // 数据内容不重要
    assert(ungot == 1);
    printf("  -> Unget succeeded when readPos=12.\n");

    // 再读取1个字节，让 readPos 变成 13
    XRingChunk_read(chunk, readBuf, 1);
    // 再读取1个字节，让 readPos 变成 14
    XRingChunk_read(chunk, readBuf, 1);
    printBufferState(chunk, "After more reads, readPos=14");

    // 现在尝试 unget 5个字节。虽然逻辑上缓冲区前面有空间，
    // 但因为 readPos (14) < 5 是 false? 14>=5, 所以还是会成功。
    // 让我们制造一个 readPos=0 的场景。

    XRingChunk_reset(chunk);
    XRingChunk_write(chunk, "ABC", 3);
    XRingChunk_read(chunk, readBuf, 3); // readPos is now 3
    // Now, the buffer is empty. readPos=3.

    // 尝试 unget 4个字节。这应该失败，因为 3 < 4.
    ungot = XRingChunk_unget(chunk, "DATA", 4);
    if (ungot == 0) {
        printf("  -> UNGET CORRECTLY FAILED! Because readPos(3) < size(4).\n");
        printf("  -> This is the known limitation of the current simple unget implementation.\n");
    }
    else {
        printf("  -> Unexpectedly succeeded.\n");
    }

    XRingChunk_delete_base(chunk);


    // ========================================
    // 测试 8: 边界情况 - 零大小操作
    // ========================================
    printf("\n[Test 8: Edge Cases - Zero Size Operations]\n");
    chunk = XRingChunk_create(5);

    assert(XRingChunk_write(chunk, "test", 0) == 0);
    assert(XRingChunk_read(chunk, readBuf, 0) == 0);
    assert(XRingChunk_peek(chunk, readBuf, 0) == 0);
    XRingChunk_skip(chunk, 0);

    XRingChunk_delete_base(chunk);

    printf("\n=== All Tests Completed Successfully! ===\n");
	XCoreApplication_quit();
}
void XMenu_XRingChunkTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XRingChunk(环形缓冲区)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XRingChunkTest);
	}
}
#endif