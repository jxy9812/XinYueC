#include "XCircularQueue.h"
#include<string.h>
#if XCircularQueue_ON
void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize_base(this_queue,count+1);
	this_queue->m_autoExpansion = false;
	this_queue->m_head = 0;
	this_queue->m_tail = 0;
	XClassGetVtable(this_queue) = XCircularQueue_class_init();
}
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueue* this_queue = XMalloc_System(sizeof(XCircularQueue));
	XCircularQueue_init(this_queue,typeSize,count);
	Set_Class_MemoryFree(this_queue, XFree_System);
	return this_queue;
}

size_t XCircularQueue_remove(XCircularQueue* this_queue, const void* value, size_t n)
{
    if (!this_queue || !value ) {
        return 0;
    }
    XCompare compare = XContainerCompare(this_queue);
    if (!compare)return false;
    // 检查队列是否为空
    if (this_queue->m_head == this_queue->m_tail) {
        return 0;
    }

    uint8_t* buffer = (uint8_t*)XContainerDataPtr(this_queue);
    size_t elem_size = XContainerTypeSize(this_queue);
    size_t capacity = XContainerSize(this_queue); // 实际分配的容量

    // 计算当前元素数量
    size_t count;
    if (this_queue->m_tail >= this_queue->m_head) {
        count = this_queue->m_tail - this_queue->m_head;
    }
    else {
        count = this_queue->m_tail + capacity - this_queue->m_head;
    }

    if (count == 0) {
        return 0;
    }

    // 如果 n 为 0，表示删除所有匹配元素
    size_t max_to_remove = (n == 0) ? count : n;
    size_t removed_count = 0;

    // 使用数组记录要删除的索引，避免在遍历时修改队列结构
    size_t* indices_to_remove = XMalloc_System(count * sizeof(size_t));
    if (!indices_to_remove) {
        return 0; // 内存分配失败
    }

    size_t found_count = 0;
    size_t current = this_queue->m_head;

    // 第一次遍历：找到所有要删除的元素索引
    for (size_t i = 0; i < count && found_count < max_to_remove; i++) {
        void* current_element = buffer + (current * elem_size);
        if (compare(current_element, value) == 0) {
            indices_to_remove[found_count] = current;
            found_count++;
        }
        current = (current + 1) % capacity;
    }

    if (found_count == 0) {
        XFree_System(indices_to_remove);
        return 0; // 未找到任何匹配元素
    }

    // 按索引从大到小排序（从后往前删除，避免索引偏移问题）
    // 由于我们是从头到尾遍历的，所以索引已经是按顺序的
    // 但为了安全起见，我们需要从后往前处理

    // 重新计算当前的 count，因为可能有变化
    if (this_queue->m_tail >= this_queue->m_head) {
        count = this_queue->m_tail - this_queue->m_head;
    }
    else {
        count = this_queue->m_tail + capacity - this_queue->m_head;
    }

    // 从后往前删除元素（按索引降序）
    for (size_t i = found_count; i > 0; i--) {
        size_t target_index = indices_to_remove[i - 1];

        // 找到要删除的元素
        void* target_element = buffer + (target_index * elem_size);

        // 如果容器设置了数据释放方法，先释放该元素
        if (XContainerDataDeinitMethod(this_queue)) {
            XContainerDataDeinitMethod(this_queue)(target_element);
        }

        // 重新计算当前元素数量（因为之前的删除可能影响了队列状态）
        if (this_queue->m_tail >= this_queue->m_head) {
            count = this_queue->m_tail - this_queue->m_head;
        }
        else {
            count = this_queue->m_tail + capacity - this_queue->m_head;
        }

        if (count == 1) {
            // 只有一个元素，直接清空
            this_queue->m_head = 0;
            this_queue->m_tail = 0;
            buffer = (uint8_t*)XContainerDataPtr(this_queue); // 重新获取buffer
            capacity = XContainerSize(this_queue);
        }
        else if (target_index == this_queue->m_head) {
            // 删除队头元素
            this_queue->m_head = (this_queue->m_head + 1) % capacity;
        }
        else if ((this_queue->m_tail == 0 ? capacity : this_queue->m_tail) - 1 == target_index) {
            // 删除队尾元素（考虑环绕情况）
            this_queue->m_tail = target_index;
        }
        else {
            // 删除中间元素，需要批量移动

            // 确定数据布局：是否环绕
            bool is_wrapped = this_queue->m_tail < this_queue->m_head;

            if (!is_wrapped) {
                // 数据连续存储 [head, tail)
                size_t elements_after = this_queue->m_tail - target_index - 1;
                if (elements_after > 0) {
                    // 批量前移后面的元素
                    memmove(buffer + (target_index * elem_size),
                        buffer + ((target_index + 1) * elem_size),
                        elements_after * elem_size);
                }
                this_queue->m_tail--;
            }
            else {
                // 数据环绕存储 [head, capacity) + [0, tail)
                if (target_index >= this_queue->m_head) {
                    // 目标在前半段 [head, capacity)
                    size_t elements_after_in_first = capacity - target_index - 1;
                    if (elements_after_in_first > 0) {
                        memmove(buffer + (target_index * elem_size),
                            buffer + ((target_index + 1) * elem_size),
                            elements_after_in_first * elem_size);
                    }
                    // 处理环绕部分
                    if (this_queue->m_tail > 0) {
                        memcpy(buffer + ((capacity - 1) * elem_size),
                            buffer,
                            elem_size);
                        if (this_queue->m_tail > 1) {
                            memmove(buffer,
                                buffer + elem_size,
                                (this_queue->m_tail - 1) * elem_size);
                        }
                        this_queue->m_tail--;
                    }
                    else {
                        this_queue->m_tail = capacity - 1;
                    }
                }
                else {
                    // 目标在后半段 [0, tail)
                    size_t elements_after_in_second = this_queue->m_tail - target_index - 1;
                    if (elements_after_in_second > 0) {
                        memmove(buffer + (target_index * elem_size),
                            buffer + ((target_index + 1) * elem_size),
                            elements_after_in_second * elem_size);
                    }
                    this_queue->m_tail--;
                }
            }
        }

        removed_count++;
        buffer = (uint8_t*)XContainerDataPtr(this_queue); // 重新获取buffer，因为可能扩容/缩容
        capacity = XContainerSize(this_queue);
    }

    XFree_System(indices_to_remove);
    return removed_count;
}

void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion)
{
	if (this_queue)
	{
		this_queue->m_autoExpansion = autoExpansion;
	}
}


#endif