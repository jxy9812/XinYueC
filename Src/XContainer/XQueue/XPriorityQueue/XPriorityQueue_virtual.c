#include"XPriorityQueue.h"
#if XPriorityQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
//插入到队列的队尾
static void VXPriorityQueue_push(XPriorityQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXPriorityQueue_pop(XPriorityQueue* this_queue);
// 返回优先队列堆顶元素
static void* VXPriorityQueue_top(XPriorityQueue* this_queue);
static bool VXPriorityQueue_receive(XPriorityQueue* this_queue, void* pvBuffer);
static bool VXPriorityQueue_isFull(const XPriorityQueue* this_queue);
XVtable* XPriorityQueue_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT_SIZE(XPRIORITYQUEUE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = { VXPriorityQueue_push,VXPriorityQueue_pop,VXPriorityQueue_top,VXPriorityQueue_receive,VXPriorityQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

#if SHOWCONTAINERSIZE
	printf("XPriorityQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
//插入向上调整
static void AdjustUp(void* LParray, const size_t TypeSize, size_t childNSel, XCompare compare, XSortOrder order)
{
	size_t parentNSel = (childNSel - 1) / 2;//父亲节点,索引下标
	char* LPparent = NULL;//父亲的元素地址
	char* LPchild = NULL;//孩子的元素地址
	int32_t cmp;
	while (true)
	{
		LPparent = (char*)LParray + parentNSel * TypeSize;
		LPchild = (char*)LParray + childNSel * TypeSize;
		cmp = compare(LPchild, LPparent);
		if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
		//if (compare(LPchild, LPparent))
		{
			XSwap(LPchild, LPparent, TypeSize);
		}
		if (parentNSel == 0)//已经调整到顶部了
			return;
		childNSel = parentNSel;
		parentNSel = (childNSel - 1) / 2;
	}
}
//向下调整
static void AdjustDwon(void* LParray, const size_t nSize, const size_t TypeSize, size_t parentNSel, XCompare compare, XSortOrder order)
{
	size_t child = parentNSel * 2 + 1;//默认左孩子
	int32_t cmp;
	while (child < nSize)
	{
		char* LPparent = (char*)LParray + parentNSel * TypeSize;//父亲当前指针
		char* LPchild = (char*)LParray + child * TypeSize;//左孩子指针
		if (child + 1 < nSize)//右孩子存在时
		{
			char* LPRchild = LPchild + TypeSize;//右孩子指针
			cmp = compare(LPchild, LPRchild);
			if (!((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
			//if (!compare(LPchild, LPRchild))//排序比较函数，选出大的那个
			{
				LPchild = LPRchild;//右孩子大，默认孩子指向右孩子
			}
		}
		cmp = compare(LPchild, LPparent);
		if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
		//if (compare(LPchild, LPparent))//排序比较函数
		{
			XSwap(LPchild, LPparent, TypeSize);//交换函数
			parentNSel = child;//父亲节点更新
			child = parentNSel * 2 + 1;//默认孩子更新
		}
		else
		{
			break;
		}
	}
}

void VXPriorityQueue_push(XPriorityQueue* this_queue, void* pvData, XCDataCreatMethod dataCreatMethod)
{
	if (ISNULL(this_queue, "")|| ISNULL(pvData, ""))
		return ;
	XVtableGetFunc(XVector_class_init(), EXVector_Push_Back,void(*)(XVector*,void*, XCDataCreatMethod))(this_queue, pvData,dataCreatMethod);
	size_t size = XContainerSize(this_queue) - 1;
	if (size > 0)//一个元素不用调整
		AdjustUp(XContainerDataPtr(this_queue), XContainerTypeSize(this_queue), size, this_queue->compare, this_queue->m_order);
}

void VXPriorityQueue_pop(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "")|| XContainer_isEmpty_base(this_queue))
		return ;
	char* LParr = XContainerDataPtr(this_queue);//指向数组的开始
	size_t arrSize = XContainer_size_base(this_queue);//数组元素数量
	size_t TypeSize = XContainer_typeSize_base(this_queue);//单个元素大小字节
	//拷贝最后一个元素到第一个
	if (arrSize > 1)
	{
		memcpy(LParr, LParr + (arrSize - 1) * TypeSize, TypeSize);
		AdjustDwon(LParr, arrSize, TypeSize, 0, this_queue->compare,this_queue->m_order);
	}
	--XContainerSize(this_queue);
}

void* VXPriorityQueue_top(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	return XVtableGetFunc(XVector_class_init(), EXVector_Front, void*(*)(XVector*))(this_queue);
	//return  XVector_front_base(this_queue);//指向数组的开始
}
bool VXPriorityQueue_receive(XPriorityQueue* this_queue, void* pvBuffer)
{
	void* data = XPriorityQueue_top_base(this_queue);//指向数组的开始
	if(data==NULL)
		return false;
	memcpy(pvBuffer, data,XContainerTypeSize(this_queue));
	XPriorityQueue_pop_base(this_queue);
	return true;
}
bool VXPriorityQueue_isFull(const XPriorityQueue* this_queue)
{
	return XContainer_size_base(this_queue)== XContainer_capacity_base(this_queue);
}
size_t XPriorityQueue_remove(XPriorityQueue* this_queue, const void* value, size_t n)
{
	if (ISNULL(this_queue, "") || ISNULL(value, "") || n == 0)
		return 0;

	XVector* vector = (XVector*)this_queue;
	XCompare compare = XContainerCompare(this_queue);
	size_t element_size = XContainerTypeSize(this_queue);
	size_t current_size = XContainerSize(this_queue);
	size_t removed_count = 0;

	// 遍历所有元素，查找匹配的元素
	size_t i = 0;
	while (i < current_size && removed_count < n) {
		void* current_element = (char*)XContainerDataPtr(this_queue) + i * element_size;

		// 使用比较函数检查是否匹配（假设比较函数返回0表示相等）
		if (compare(current_element, value) == XCompare_Equality) {
			// 找到匹配的元素，需要移除它

			// 如果不是最后一个元素，将最后一个元素移到当前位置
			if (i < current_size - 1) {
				memcpy((char*)XContainerDataPtr(this_queue) + i * element_size,
					(char*)XContainerDataPtr(this_queue) + (current_size - 1) * element_size,
					element_size);
			}

			// 减少容器大小
			--current_size;
			XContainerSize(this_queue) = current_size;
			removed_count++;

			// 如果队列不为空，需要恢复堆性质
			if (current_size > 0) {
				// 获取当前元素与父元素的比较结果，决定向上还是向下调整
				size_t parent_index = (i == 0) ? 0 : (i - 1) / 2;
				void* current_elem = (char*)XContainerDataPtr(this_queue) + i * element_size;
				void* parent_elem = (char*)XContainerDataPtr(this_queue) + parent_index * element_size;

				int cmp_result = compare(current_elem, parent_elem);
				XSortOrder order = this_queue->m_order;

				// 检查是否需要向上调整
				bool need_adjust_up = false;
				if (i > 0) { // 不是根节点
					if ((order == XSORT_ASC && cmp_result == XCompare_Less) ||
						(order == XSORT_DESC && cmp_result == XCompare_Greater)) {
						need_adjust_up = true;
					}
				}

				if (need_adjust_up) {
					// 向上调整
					AdjustUp(XContainerDataPtr(this_queue), element_size, i, compare, order);
				}
				else {
					// 向下调整
					AdjustDwon(XContainerDataPtr(this_queue), current_size, element_size, i, compare, order);
				}
			}

			// 注意：由于我们将最后一个元素移到了位置 i，
			// 我们需要重新检查位置 i 的元素，所以不增加 i
		}
		else {
			i++;
		}
	}

	return removed_count;
}


#endif