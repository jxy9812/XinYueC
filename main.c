#include"XDataStructTest.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<string.h>
#include<math.h>
#include"XCoreApplication.h"
#include <stdarg.h>
#include"XVarList.h"
#include<stdlib.h>
typedef struct { unsigned int gbk; unsigned int unicode; } _GenEntry;
static int _cmp_entry_uni(const void* a, const void* b) {
	unsigned int ua = ((const _GenEntry*)a)->unicode, ub = ((const _GenEntry*)b)->unicode;
	return (ua > ub) - (ua < ub);
}
static void _generate_tables(const char* input, const char* out_combined) {
	_GenEntry* e; FILE* fin; FILE* fout; int count, idx; char line[512];
	/* header: "GBKTBL:NNNNNNNN\n" = 16字节, "UNITBL:NNNNNNNN\n" = 16字节 */
	/* 总头 = 32字节, 每条目行 = 14字节 */
	int header_size = 32;
	int line_bytes = 14;
	int gbk_offset, uni_offset;

	fin = fopen(input, "r"); if (!fin) { printf("Cannot open %s\n", input); return; }
	count = 0;
	while (fgets(line, sizeof(line), fin)) {
		unsigned int g = 0, u = 0;
		if (line[0] == '0' && line[1] == 'x' && sscanf(line, "0x%X\t0x%X", &g, &u) == 2) count++;
	}
	printf("Valid entries: %d\n", count);
	e = (_GenEntry*)malloc(count * sizeof(_GenEntry)); if (!e) { fclose(fin); return; }
	fseek(fin, 0, SEEK_SET); idx = 0;
	while (fgets(line, sizeof(line), fin)) {
		unsigned int g = 0, u = 0;
		if (line[0] == '0' && line[1] == 'x' && sscanf(line, "0x%X\t0x%X", &g, &u) == 2) {
			e[idx].gbk = g; e[idx].unicode = u; idx++;
		}
	}
	fclose(fin);

	gbk_offset = header_size;
	uni_offset = header_size + idx * line_bytes;

	fout = fopen(out_combined, "wb"); if (!fout) { free(e); return; }
	/* 写头部 */
	fprintf(fout, "GBKTBL:%08X\n", gbk_offset);
	fprintf(fout, "UNITBL:%08X\n", uni_offset);
	/* 写 GBK 表（按 GBK 排序，原文件已是排序的） */
	{ int i; for (i = 0; i < idx; i++) fprintf(fout, "0x%04X\t0x%04X\n", e[i].gbk, e[i].unicode); }
	/* 写 Unicode 表（按 Unicode 排序） */
	qsort(e, idx, sizeof(_GenEntry), _cmp_entry_uni);
	{ int i; for (i = 0; i < idx; i++) fprintf(fout, "0x%04X\t0x%04X\n", e[i].unicode, e[i].gbk); }
	fclose(fout); free(e);
	printf("Generated %s (%d entries per table, file size=%d)\n", out_combined, idx,
	       header_size + 2 * idx * line_bytes);
}
int main(int argc, char* args[])
{
	/* 临时：生成合并映射文件（运行一次后注释掉） */
	/* _generate_tables("d:/code/CMake/XinYueC/Library/CP936.TXT",
	                  "d:/code/CMake/XinYueC/Library/XCHAR.TXT"); */
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