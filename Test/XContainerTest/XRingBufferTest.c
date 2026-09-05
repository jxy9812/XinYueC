#include"XDataStructTest.h"
#if DEMOTEST
#include"XRingBuffer.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include <assert.h>
static void print_buffer_status(const char* msg, const XRingBuffer* buffer) {
	XPrintf("\n--- %s ---\n", msg);
	XPrintf("Total Size (available): %zu\n", XRingBuffer_available(buffer));
	XPrintf("Current Read Chunk Index: %zu\n", buffer->m_currentReadChunk);
	XPrintf("Current Write Chunk Index: %zu\n", buffer->m_currentWriteChunk);
}

void XRingBufferTest()
{
	{
        XPrintf("=== Starting XRingBuffer Comprehensive Test ===\n");

        // ========================================
        // Test 1: Basic Create, Write, Read
        // ========================================
        XPrintf("\n[Test 1: Basic Create, Write, Read]\n");
        XRingBuffer* buffer = XRingBuffer_create(10); // Small chunk size to force wrap-around quickly
        assert(buffer != NULL);

        const char* writeData = "Hello";
        size_t written = XRingBuffer_write(buffer, writeData, strlen(writeData));
        assert(written == 5);
        assert(XRingBuffer_available(buffer) == 5);

        char readBuf[10] = { 0 };
        size_t read = XRingBuffer_read(buffer, readBuf, 5);
        assert(read == 5);
        assert(strcmp(readBuf, "Hello") == 0);
        assert(XRingBuffer_available(buffer) == 0);

        XRingBuffer_delete_base(buffer);

        // ========================================
     // Test 2: Write Across Multiple Chunks
     // ========================================
        XPrintf("\n[Test 2: Write Across Multiple Chunks]\n");
        buffer = XRingBuffer_create(5); // Each chunk has a LOGICAL capacity of 4 bytes
        assert(buffer != NULL);

        // Write 10 bytes, which should span 3 chunks (4 + 4 + 2)
        const char* longData = "0123456789";
        written = XRingBuffer_write(buffer, longData, 10);
        assert(written == 10);
        assert(XRingBuffer_available(buffer) == 10);

        // Read all back to verify correctness
        memset(readBuf, 0, sizeof(readBuf));
        read = XRingBuffer_read(buffer, readBuf, 10);
        assert(read == 10);
        assert(strncmp(readBuf, longData, 10) == 0);
        assert(XRingBuffer_available(buffer) == 0);

        XRingBuffer_delete_base(buffer);

        // ========================================
        // Test 3: Read/Write Wrap-Around with Partial Operations
        // ========================================
        XPrintf("\n[Test 3: Read/Write Wrap-Around with Partial Operations]\n");
        buffer = XRingBuffer_create(4); // Logical cap 3 per chunk
        assert(buffer != NULL);

        // Fill first chunk
        written = XRingBuffer_write(buffer, "ABC", 3);
        assert(written == 3);
        assert(XRingBuffer_available(buffer) == 3);

        // This should trigger creation of a new chunk and write 'D'
        written = XRingBuffer_write(buffer, "D", 1);
        assert(written == 1);
        assert(XRingBuffer_available(buffer) == 4);
        // --- 移除对 m_currentWriteChunk 的直接断言 ---

        // Read first 2 bytes ("AB")
        read = XRingBuffer_read(buffer, readBuf, 2);
        assert(read == 2);
        assert(strncmp(readBuf, "AB", 2) == 0);
        assert(XRingBuffer_available(buffer) == 2);

        // Write "EF" -> should go into second chunk
        written = XRingBuffer_write(buffer, "EF", 2);
        assert(written == 2);
        assert(XRingBuffer_available(buffer) == 4); // 1 (C) + 1 (D) + 2 (EF) = 4

        // Read remaining 4 bytes: "C", "D", "E", "F"
        read = XRingBuffer_read(buffer, readBuf, 4);
        assert(read == 4);
        assert(strncmp(readBuf, "CDEF", 4) == 0);
        assert(XRingBuffer_available(buffer) == 0);

        XRingBuffer_delete_base(buffer);

        // ========================================
        // Test 4: Peek and Skip
        // ========================================
        XPrintf("\n[Test 4: Peek and Skip]\n");
        buffer = XRingBuffer_create(10);
        assert(buffer != NULL);

        written = XRingBuffer_write(buffer, "PeekTest", 8);
        assert(written == 8);

        // Peek first 4 bytes
        memset(readBuf, 0, sizeof(readBuf));
        size_t peeked = XRingBuffer_peek(buffer, readBuf, 4);
        assert(peeked == 4);
        assert(strncmp(readBuf, "Peek", 4) == 0);
        assert(XRingBuffer_available(buffer) == 8); // Should not change

        // Skip 4 bytes
        XRingBuffer_skip(buffer, 4);
        assert(XRingBuffer_available(buffer) == 4);

        // Read the rest
        memset(readBuf, 0, sizeof(readBuf));
        read = XRingBuffer_read(buffer, readBuf, 10);
        assert(read == 4);
        assert(strcmp(readBuf, "Test") == 0);

        XRingBuffer_delete_base(buffer);

        // ========================================
        // Test 5: Global Mark and ResetToMark (Across Chunks)
        // ========================================
        XPrintf("\n[Test 5: Global Mark and ResetToMark (Across Chunks)]\n");
        buffer = XRingBuffer_create(5); // Logical cap 4 per chunk
        assert(buffer != NULL);

        // Write enough data to span 2 chunks: "0123" + "4567"
        written = XRingBuffer_write(buffer, "01234567", 8);
        assert(written == 8);
        assert(buffer->m_currentWriteChunk == 1);

        // Read first 3 bytes: "012"
        read = XRingBuffer_read(buffer, readBuf, 3);
        assert(read == 3);
        assert(buffer->m_currentReadChunk == 0); // Still in first chunk

        // Set global mark HERE
        XRingBuffer_mark(buffer);

        // Read next 3 bytes: "345"
        read = XRingBuffer_read(buffer, readBuf, 3);
        assert(read == 3);
        assert(buffer->m_currentReadChunk == 1); // Now in second chunk
        assert(XRingBuffer_available(buffer) == 2); // "67" left

        // Reset to mark! Should go back to having "34567" available
        XRingBuffer_resetToMark(buffer);
        assert(XRingBuffer_available(buffer) == 5);
        assert(buffer->m_currentReadChunk == 0); // Back to first chunk

        // Read from mark point
        memset(readBuf, 0, sizeof(readBuf));
        read = XRingBuffer_read(buffer, readBuf, 10);
        assert(read == 5);
        assert(strncmp(readBuf, "34567", 5) == 0);

        XRingBuffer_delete_base(buffer);

        // ========================================
        // Test 6: Edge Cases - Zero Size Operations
        // ========================================
        XPrintf("\n[Test 6: Edge Cases - Zero Size Operations]\n");
        buffer = XRingBuffer_create(10);
        assert(buffer != NULL);

        // All these should be safe and return 0 or do nothing
        assert(XRingBuffer_write(buffer, "test", 0) == 0);
        assert(XRingBuffer_read(buffer, readBuf, 0) == 0);
        assert(XRingBuffer_peek(buffer, readBuf, 0) == 0);
        XRingBuffer_skip(buffer, 0);

        // Write some data and then try to skip more than available
        XRingBuffer_write(buffer, "Edge", 4);
        XRingBuffer_skip(buffer, 10); // Should reset the buffer
        assert(XRingBuffer_available(buffer) == 0);

        XRingBuffer_delete_base(buffer);

        XPrintf("\n=== All XRingBuffer Tests Completed Successfully! ===\n");
	}
}
void XTestMenu_XRingBufferTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XRingBuffer(环形缓冲区)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XRingBufferTest);
	}
}
#endif
