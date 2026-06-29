#include"XDataStructTest.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<string.h>
#include<math.h>
#include"XCoreApplication.h"
#include <stdarg.h>
#include"XVarList.h"
#include <stdint.h>

/* ========================================================================== */
/*                   XCHAR.TXT 转换表生成器                                     */
/* ========================================================================== */

/**
 * @brief 生成GBK-Unicode转换表的静态数组代码
 *
 * 从 XCHAR.TXT 文件读取数据，生成以下输出：
 *   1. 精简的二进制文件（XCHAR_COMPACT.BIN）- 可被文件模式使用
 *   2. C语言静态数组代码（打印到控制台）- 可复制到 XChar_code.c
 *
 * 使用方法：
 *   1. 运行程序，调用 XChar_generateCodeTable()
 *   2. 将控制台输出的数组内容复制到 XChar_code.c 的 s_gbk_to_uni_table 和 s_uni_to_gbk_table
 *   3. 重新编译即可使用代码模式
 */

#define XCHAR_GEN_MAX_ENTRIES  25000  /* 最大条目数 */

typedef struct {
    uint16_t gbk;
    uint16_t unicode;
} GbkUniEntry;

/* 比较函数：按GBK排序 */
static int compare_by_gbk(const void* a, const void* b)
{
    const GbkUniEntry* ea = (const GbkUniEntry*)a;
    const GbkUniEntry* eb = (const GbkUniEntry*)b;
    return (int)ea->gbk - (int)eb->gbk;
}

/* 比较函数：按Unicode排序 */
static int compare_by_unicode(const void* a, const void* b)
{
    const GbkUniEntry* ea = (const GbkUniEntry*)a;
    const GbkUniEntry* eb = (const GbkUniEntry*)b;
    return (int)ea->unicode - (int)eb->unicode;
}

/* 解析十六进制字符串 */
static uint32_t parse_hex(const char* str, size_t len)
{
    uint32_t val = 0;
    size_t i;
    for (i = 0; i < len && str[i]; i++) {
        char c = str[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        val = (val << 4) | d;
    }
    return val;
}

/* 跳过空白字符 */
static const char* skip_whitespace(const char* p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

void XChar_generateCodeTable(void)
{
    FILE* fin;
    FILE* fbin;
    char line[256];
    GbkUniEntry* entries;
    GbkUniEntry* gbk_sorted;
    GbkUniEntry* uni_sorted;
    size_t entry_count = 0;
    size_t i;
    int in_gbk_table = 0;
    int in_uni_table = 0;

    /* 分配内存 */
    entries = (GbkUniEntry*)malloc(sizeof(GbkUniEntry) * XCHAR_GEN_MAX_ENTRIES);
    if (!entries) {
        printf("Error: Failed to allocate memory for entries\n");
        return;
    }

    /* 打开输入文件 */
    fin = fopen("../Library/XCHAR.TXT", "r");
    if (!fin) {
        printf("Error: Cannot open Library/XCHAR.TXT\n");
        free(entries);
        return;
    }

    printf("=== XCHAR.TXT Parser ===\n\n");

    /* 读取并解析文件 - 只读取GBK表部分 */
    /* XCHAR.TXT 格式：
     * GBKTBL:XXXXXXXX  <- GBK表偏移（32字节头部的第一行）
     * UNITBL:XXXXXXXX  <- Unicode表偏移（32字节头部的第二行）
     * GBK表数据：0xGBK\t0xUnicode   <- 按GBK排序
     * Unicode表数据：0xUnicode\t0xGBK <- 按Unicode排序（需要跳过！）
     */
    int64_t gbk_table_offset = 0;
    int64_t uni_table_offset = 0;
    
    /* 首先读取头部获取两个表的偏移 */
    if (fgets(line, sizeof(line), fin)) {
        if (line[0] == 'G' && line[6] == ':') {
            gbk_table_offset = (int64_t)parse_hex(line + 7, 8);
        }
    }
    if (fgets(line, sizeof(line), fin)) {
        if (line[0] == 'U' && line[6] == ':') {
            uni_table_offset = (int64_t)parse_hex(line + 7, 8);
        }
    }
    
    printf("GBK table offset: 0x%llX\n", (unsigned long long)gbk_table_offset);
    printf("Unicode table offset: 0x%llX\n", (unsigned long long)uni_table_offset);
    
    /* 计算GBK表的条目数：两个表之间的字节数 / 14字节每行 */
    int64_t gbk_table_size = uni_table_offset - gbk_table_offset;
    size_t max_gbk_entries = (size_t)(gbk_table_size / 14);  /* 每行约14字节 */
    printf("Estimated GBK entries: %zu\n\n", max_gbk_entries);
    
    /* 只读取GBK表部分，直到到达Unicode表偏移 */
    while (fgets(line, sizeof(line), fin) && entry_count < XCHAR_GEN_MAX_ENTRIES) {
        /* 检查是否到达Unicode表 */
        long current_pos = ftell(fin);
        if (current_pos >= uni_table_offset) {
            printf("Reached Unicode table at entry %zu, stopping.\n", entry_count);
            break;
        }

        /* 解析数据行：格式 "0xGBK\t0xUnicode" */
        const char* p = line;
        if (p[0] == '0' && p[1] == 'x') {
            uint16_t val1, val2;
            p += 2;
            val1 = (uint16_t)parse_hex(p, 4);
            
            /* 跳过制表符 */
            while (*p && *p != '\t') p++;
            if (*p == '\t') p++;
            
            if (p[0] == '0' && p[1] == 'x') {
                p += 2;
                val2 = (uint16_t)parse_hex(p, 4);
                
                /* 只存储GBK双字节字符（0x8140-0xFEFE） */
                if (val1 >= 0x8140 && val1 <= 0xFEFE) {
                    entries[entry_count].gbk = val1;
                    entries[entry_count].unicode = val2;
                    entry_count++;
                }
            }
        }
    }
    fclose(fin);

    printf("Total entries loaded: %zu\n\n", entry_count);

    if (entry_count == 0) {
        printf("Error: No entries found\n");
        free(entries);
        return;
    }

    /* 创建排序后的副本 */
    gbk_sorted = (GbkUniEntry*)malloc(sizeof(GbkUniEntry) * entry_count);
    uni_sorted = (GbkUniEntry*)malloc(sizeof(GbkUniEntry) * entry_count);
    if (!gbk_sorted || !uni_sorted) {
        printf("Error: Failed to allocate memory for sorted arrays\n");
        free(entries);
        if (gbk_sorted) free(gbk_sorted);
        if (uni_sorted) free(uni_sorted);
        return;
    }

    memcpy(gbk_sorted, entries, sizeof(GbkUniEntry) * entry_count);
    memcpy(uni_sorted, entries, sizeof(GbkUniEntry) * entry_count);

    qsort(gbk_sorted, entry_count, sizeof(GbkUniEntry), compare_by_gbk);
    qsort(uni_sorted, entry_count, sizeof(GbkUniEntry), compare_by_unicode);

    printf("Tables sorted successfully.\n\n");

    /* 生成精简的二进制文件 */
    fbin = fopen("../Library/XCHAR_COMPACT.BIN", "wb");
    if (fbin) {
        /* 写入GBK表 */
        for (i = 0; i < entry_count; i++) {
            uint8_t buf[4];
            buf[0] = (gbk_sorted[i].gbk >> 8) & 0xFF;
            buf[1] = gbk_sorted[i].gbk & 0xFF;
            buf[2] = (gbk_sorted[i].unicode >> 8) & 0xFF;
            buf[3] = gbk_sorted[i].unicode & 0xFF;
            fwrite(buf, 1, 4, fbin);
        }
        /* 写入Unicode表 */
        for (i = 0; i < entry_count; i++) {
            uint8_t buf[4];
            buf[0] = (uni_sorted[i].unicode >> 8) & 0xFF;
            buf[1] = uni_sorted[i].unicode & 0xFF;
            buf[2] = (uni_sorted[i].gbk >> 8) & 0xFF;
            buf[3] = uni_sorted[i].gbk & 0xFF;
            fwrite(buf, 1, 4, fbin);
        }
        fclose(fbin);
        printf("Binary file saved: Library/XCHAR_COMPACT.BIN\n");
        printf("  - GBK table: %zu entries * 4 bytes = %zu bytes\n", entry_count, entry_count * 4);
        printf("  - Unicode table: %zu entries * 4 bytes = %zu bytes\n", entry_count, entry_count * 4);
        printf("  - Total size: %zu bytes (%.1f KB)\n\n", entry_count * 8, entry_count * 8 / 1024.0);
    }

    /* 生成C语言静态数组代码到txt文件 */
    {
        FILE* fcode = fopen("../Library/XCHAR_CODE_TABLE.txt", "w");
        if (fcode) {
            fprintf(fcode, "/**\n");
            fprintf(fcode, " * XChar GBK-Unicode 转换表静态数组\n");
            fprintf(fcode, " * 条目数: %zu\n", entry_count);
            fprintf(fcode, " * 生成时间: 自动生成\n");
            fprintf(fcode, " * 使用方法: 将此文件内容复制到 XChar_code.c 中对应位置\n");
            fprintf(fcode, " */\n\n");
            
            /* GBK表 */
            fprintf(fcode, "/* GBK->Unicode map (sorted by GBK, %zu entries) */\n", entry_count);
            fprintf(fcode, "static const uint8_t s_gbk_to_uni_table[] = {\n");
            for (i = 0; i < entry_count; i++) {
                if (i % 8 == 0) fprintf(fcode, "    ");
                fprintf(fcode, "0x%02X,0x%02X,0x%02X,0x%02X",
                       (gbk_sorted[i].gbk >> 8) & 0xFF, gbk_sorted[i].gbk & 0xFF,
                       (gbk_sorted[i].unicode >> 8) & 0xFF, gbk_sorted[i].unicode & 0xFF);
                if (i < entry_count - 1) fprintf(fcode, ",");
                if ((i + 1) % 8 == 0 || i == entry_count - 1) fprintf(fcode, "\n");
                else fprintf(fcode, " ");
            }
            fprintf(fcode, "};\n\n");

            /* Unicode表 */
            fprintf(fcode, "/* Unicode->GBK map (sorted by Unicode, %zu entries) */\n", entry_count);
            fprintf(fcode, "static const uint8_t s_uni_to_gbk_table[] = {\n");
            for (i = 0; i < entry_count; i++) {
                if (i % 8 == 0) fprintf(fcode, "    ");
                fprintf(fcode, "0x%02X,0x%02X,0x%02X,0x%02X",
                       (uni_sorted[i].unicode >> 8) & 0xFF, uni_sorted[i].unicode & 0xFF,
                       (uni_sorted[i].gbk >> 8) & 0xFF, uni_sorted[i].gbk & 0xFF);
                if (i < entry_count - 1) fprintf(fcode, ",");
                if ((i + 1) % 8 == 0 || i == entry_count - 1) fprintf(fcode, "\n");
                else fprintf(fcode, " ");
            }
            fprintf(fcode, "};\n");
            fclose(fcode);
            printf("Code saved to: Library/XCHAR_CODE_TABLE.txt\n");
        }
    }
    
    /* 同时生成到 XChar_code.c 文件 */
    {
        /* 先读取 XChar_code.c 的头部（前39行，到数组定义之前） */
        FILE* fin = fopen("../Src/XData/XChar/XChar_code.c", "r");
        FILE* fout = fopen("../Src/XData/XChar/XChar_code.c.new", "w");
        if (fin && fout) {
            char linebuf[512];
            int line_num = 0;
            /* 复制头部（包括注释、#include、#define等，直到数组定义） */
            while (fgets(linebuf, sizeof(linebuf), fin) && line_num < 39) {
                fputs(linebuf, fout);
                line_num++;
            }
            fclose(fin);
            
            /* 写入新的 GBK 表 */
            fprintf(fout, "static const uint8_t s_gbk_to_uni_table[] = {\n");
            for (i = 0; i < entry_count; i++) {
                if (i % 8 == 0) fprintf(fout, "    ");
                fprintf(fout, "0x%02X,0x%02X,0x%02X,0x%02X",
                       (gbk_sorted[i].gbk >> 8) & 0xFF, gbk_sorted[i].gbk & 0xFF,
                       (gbk_sorted[i].unicode >> 8) & 0xFF, gbk_sorted[i].unicode & 0xFF);
                if (i < entry_count - 1) fprintf(fout, ",");
                if ((i + 1) % 8 == 0 || i == entry_count - 1) fprintf(fout, "\n");
                else fprintf(fout, " ");
            }
            fprintf(fout, "};\n\n");
            
            /* 写入 Unicode 表注释 */
            fprintf(fout, "/* \n");
            fprintf(fout, " * Unicode->GBK 映射表（按Unicode排序）\n");
            fprintf(fout, " * 格式：每4字节为一项，大端序存储\n");
            fprintf(fout, " * [Unicode高字节][Unicode低字节][GBK高字节][GBK低字节]\n");
            fprintf(fout, " */\n");
            
            /* 写入新的 Unicode 表 */
            fprintf(fout, "static const uint8_t s_uni_to_gbk_table[] = {\n");
            for (i = 0; i < entry_count; i++) {
                if (i % 8 == 0) fprintf(fout, "    ");
                fprintf(fout, "0x%02X,0x%02X,0x%02X,0x%02X",
                       (uni_sorted[i].unicode >> 8) & 0xFF, uni_sorted[i].unicode & 0xFF,
                       (uni_sorted[i].gbk >> 8) & 0xFF, uni_sorted[i].gbk & 0xFF);
                if (i < entry_count - 1) fprintf(fout, ",");
                if ((i + 1) % 8 == 0 || i == entry_count - 1) fprintf(fout, "\n");
                else fprintf(fout, " ");
            }
            fprintf(fout, "};\n\n");
            
            /* 添加分隔注释 */
            fprintf(fout, "/* ========================================================================== */\n");
            fprintf(fout, "/*                        内部辅助函数                                         */\n");
            fprintf(fout, "/* ========================================================================== */\n\n");
            
            /* 读取原文件的剩余部分（从第65行开始，即辅助函数部分） */
            fin = fopen("../Src/XData/XChar/XChar_code.c", "r");
            if (fin) {
                line_num = 0;
                while (fgets(linebuf, sizeof(linebuf), fin)) {
                    line_num++;
                    if (line_num >= 65) {
                        fputs(linebuf, fout);
                    }
                }
                fclose(fin);
            }
            
            fclose(fout);
            
            /* 替换原文件 */
            remove("../Src/XData/XChar/XChar_code.c");
            rename("../Src/XData/XChar/XChar_code.c.new", "../Src/XData/XChar/XChar_code.c");
            printf("Code also saved to: Src/XData/XChar/XChar_code.c\n");
        } else {
            if (fin) fclose(fin);
            if (fout) fclose(fout);
            printf("Warning: Could not update XChar_code.c directly\n");
        }
    }
    
    printf("\nGeneration complete!\n");

    /* 清理 */
    free(entries);
    free(gbk_sorted);
    free(uni_sorted);
}

int main(int argc, char* args[])
{
    // 取消注释以生成转换表
    /* XChar_generateCodeTable();
     return 0;*/
    
	//XVectorTest();
	int n = 8,n1=666,sum=n+n1;
	char* str = "dadasdsad";
	XVarList* list=XVarList_Create(XVar(int,n), XVar(int, n1), XVar(char*, str));
	//XVarList_start(list);
	XVarList_args_3(list,int,a,int,b, char*,c);
	printf("%d %d %s\n", a,b,c);
	
	XVarList_delete(list);

	XCoreApplication* app = XCoreApplication_create(argc,args);
	//XThreadTest();
	//XCoreApplication_setApplicationDescription
	//XAtomic_bool b;
	//XAtomic_init(b,false);
	//XAtomic_store_bool(&b,false);
	//printf("%d\n",XAtomic_load_bool(&b));
#if DEMOTEST
	
	//XStringVectorTest();
	//XStringTest();
	//XBase64Test();
	//return;
	//XTimerTimeWheelTest();
	//XListDLinkedIterator();
	//XHashMapTest();
	//XMapTest();
	//XLockFreeListTest();
	//XLockFreeListSwapTest();
	//XLockFreeListSortTest();
	//XLockFreeListIterator();
	//XListDLinkedTest();
	//
	//XListDLinkedSortTest();
	//XListDLinkedIterator();
	//XListDLinkedSwapTest();
	//XListSLinkedTest();
	//XListSLinkedSwapTest();
	//XListSLinkedIterator();
	//XListSLinkedSortTest();
	//XPriority_QueueTest();
	//XPWMDeviceTest();
	//XVectorTest();
	//TJCHMICommTest();
	//XDataFrameCommTest();
	//XSocketTest();
	//XHashSetTest();
	//XSetTest();
	//XBinaryTreeTest();
	//XRedBlackTreeTest();
	//XMapTest();
	//return;
	//XVariantListTest();
	//XStringListTest();
	//XStringTest();
	//XJsonArrayTest();
	//XJsonObjectTest();
	//return;
	//XStateMachineSignalTest();
	//XHistoryState_Test();
	return XMenuTest_run();
	//XStateMachineEventTest();
	return XCoreApplication_exec();
	//cJsonTest();
	XRedBlackTreeTest();
	//XMapAndXVectorFindTest();
	//XBinarySearchTest();
	SortTest();
	//XMazeGeneratedTest();
	//XMazePathfinding();
	//XBalancedBinaryTreeTest();
	
#endif
#ifdef _WIN32
	//XHuffmanTreeTest();
#else
	//XRedBlackTreeTest();
#endif // _Win32
	return XCoreApplication_exec();
}