#include "XHuffmanTree.h"
#if XMap_ON

// 初始化哈夫曼树
XHuffmanTree* XHfmTree_init() {
    XHuffmanTree* tree = XMemory_malloc(sizeof(XHuffmanTree));
    if (ISNULL(tree, "初始化哈夫曼树失败")) {
        return NULL;
    }
    tree->root = NULL;
    // 使用unsigned char作为key类型，解决符号问题
    tree->dictionaries = XMap_Create(unsigned char, DictionaryValue, XCompare_unsigned_char);
    if (ISNULL(tree->dictionaries, "申请哈夫曼树字典失败")) {
        XMemory_free(tree);  // 修复内存泄漏：失败时释放已分配的tree
        return NULL;
    }
    return tree;
}

// 创建哈夫曼节点
XHfmNode* XHfmTree_createNode(unsigned char ch, size_t count, XVector* code) {
    XTreeNode* node = XMemory_malloc(sizeof(XTreeNode));
    if (ISNULL(node, "创建哈夫曼节点失败")) {
        return NULL;
    }
    XTreeNode_init(node, 3, NULL, sizeof(XHfmNodeData));
    XHfmNodeData* data = XHfmTree_GetNodeData(node);
    data->ch = ch;
    data->count = count;
    data->code = code;
    return node;
}

// 优先队列比较函数（升序）
static bool Less(XHfmNode** nodeOne, XHfmNode** nodeTwo) {
    return XHfmTree_GetNodeData(*nodeOne)->count < XHfmTree_GetNodeData(*nodeTwo)->count;
}

// 字典数据插入优先队列
static void installQueue(XPair** pair, XPriorityQueue* queue) {
    unsigned char ch = XPair_First(*pair, unsigned char);
    DictionaryValue dv = XPair_Second(*pair, DictionaryValue);
    XHfmNode* node = XHfmTree_createNode(ch, dv.count, dv.code);
    if (node != NULL) {
        XPriorityQueue_push_base(queue, &node);
    }
}

// 根据字典创建哈夫曼树
XHfmNode* XHfmTree_DictionariesToCreationTree(XMap* dictionaries) {
    if (!XPriorityQueue_ON) {
        IS_ON_DEBUG(XPriorityQueue_ON);
        return NULL;
    }
    XPriorityQueue* queue = XPriorityQueue_Create(XHfmNode*, Less, XSORT_ASC);
    if (ISNULL(queue, "创建优先队列失败")) {
        return NULL;
    }
    XMap_iterator_for_each(dictionaries, installQueue, queue);

    XHfmNode* parent = NULL;
    XHfmNode* left = NULL;
    XHfmNode* right = NULL;

    while (!XPriorityQueue_isEmpty_base(queue)) {
        XHfmNode* node = XPriorityQueue_Top_Base(queue, XHfmNode*);
        XPriorityQueue_pop_base(queue);

        if (left == NULL) {
            left = node;
        }
        else {
            right = node;
            // 创建父节点
            parent = XHfmTree_createNode(0,
                XHfmTree_GetNodeData(left)->count + XHfmTree_GetNodeData(right)->count,
                NULL);
            if (ISNULL(parent, "创建父节点失败")) {
                // 释放已分配节点避免泄漏
                XTree_delete(left, NULL, NULL);
                XTree_delete(right, NULL, NULL);
                left = right = NULL;
                break;
            }
            XBTreeNode_SetLChild(parent, left);
            XBTreeNode_SetRChild(parent, right);
            XBTreeNode_SetParent(left, parent);
            XBTreeNode_SetParent(right, parent);
            XPriorityQueue_push_base(queue, &parent);
            left = right = NULL;
        }
    }
    XPriorityQueue_delete_base(queue);

    // 修复单一节点场景：若只有一个节点，根节点为left
    return parent != NULL ? parent : left;
}

// 逆序编码（辅助函数）
static void reverseCode(XVector* code) {
    size_t size = XVector_size_base(code);
    char* data = (char*)XContainerDataPtr(code);
    for (size_t i = 0; i < size / 2; i++) {
        char temp = data[i];
        data[i] = data[size - 1 - i];
        data[size - 1 - i] = temp;
    }
}

// 为叶子节点设置编码
static void setCode(XHfmNode* node, XVector* code) {
    if (!XVector_ON) return;
    XHfmNode* current = node;
    XHfmNode* parent = XBTreeNode_GetParent(current);
    while (parent != NULL) {
        char bit = (current == XBTreeNode_GetLChild(parent)) ? 0 : 1;
        XVector_push_back_base(code, &bit);
        current = parent;
        parent = XBTreeNode_GetParent(current);
    }
    reverseCode(code);  // 逆序得到正确编码
}

// 遍历节点设置编码
static void getCode(XHfmNode** node, void* args) {
    XHfmNodeData* data = XHfmTree_GetNodeData(*node);
    if (data->code != NULL) {  // 叶子节点
        setCode(*node, data->code);
    }
}

// 设置哈夫曼编码
void XHfmTree_setCode(XHfmNode* root) {
    if (ISNULL(root, "根节点为空")) return;
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return;
    }
    XVector* nodeList = XBTree_TraversingToXVector(root, XBTreeInorder);
    if (nodeList != NULL) {
        XVector_iterator_for_each(nodeList, getCode, NULL);
        XVector_delete_base(nodeList);
    }
}

// 构建字典（统计字符出现次数）
void XHfmTree_setDictionaries(XMap* dictionaries, const char* data, const size_t size) {
    if (ISNULL(dictionaries, "字典为空") || ISNULL(data, "数据为空") || size == 0) {
        return;
    }
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return;
    }
    for (size_t i = 0; i < size; i++) {
        unsigned char ch = (unsigned char)data[i];  // 强制转为无符号
        DictionaryValue* dv = XMap_value_base(dictionaries, &ch);
        if (dv->count == 0) {  // 首次出现，初始化编码向量
            dv->code = XVector_Create(char);
            if (ISNULL(dv->code, "创建编码向量失败")) {
                continue;
            }
        }
        dv->count++;
    }
}

// 写入字典数据到压缩数组
static void writeDictionaryData(XPair** pair, XVector* gzipData) {
    if (!XVector_ON) return;
    unsigned char ch = XPair_First(*pair, unsigned char);
    DictionaryValue dv = XPair_Second(*pair, DictionaryValue);
    size_t codeSize = XVector_size_base(dv.code);

    DictionaryData data = { ch, dv.count, codeSize };
    size_t currentSize = XVector_size_base(gzipData);
    // 扩容以容纳字典数据和编码
    XVector_resize_base(gzipData, currentSize + sizeof(DictionaryData) + codeSize);
    char* ptr = (char*)XContainerDataPtr(gzipData) + currentSize;
    memcpy(ptr, &data, sizeof(DictionaryData));
    ptr += sizeof(DictionaryData);
    memcpy(ptr, XContainerDataPtr(dv.code), codeSize);
}

// 写入压缩字典
int XHfmTree_writeCompressDictionaries(XVector* retData, XMap* dictionaries) {
    if (ISNULL(retData, "返回数据为空") || ISNULL(dictionaries, "字典为空")) {
        return 0;
    }
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return 0;
    }
    size_t count = XMap_size_base(dictionaries);
    // 先写入字典条目数量
    XVector_resize_base(retData, sizeof(size_t));
    memcpy(XContainerDataPtr(retData), &count, sizeof(size_t));
    // 写入每个条目
    XMap_iterator_for_each(dictionaries, writeDictionaryData, retData);
    return (int)XVector_size_base(retData);
}

// 读取数据构建哈夫曼树
const bool XHfmTree_readData(XHuffmanTree* tree, const char* data, const size_t size) {
    if (ISNULL(tree, "哈夫曼树为空") || ISNULL(data, "数据为空") || size == 0) {
        return false;
    }
    XHfmTree_setDictionaries(tree->dictionaries, data, size);
    tree->root = XHfmTree_DictionariesToCreationTree(tree->dictionaries);
    if (tree->root != NULL) {
        XHfmTree_setCode(tree->root);
        return true;
    }
    return false;
}

// 压缩数据写入（带有效比特数记录）
static void writeData(XVector* gzipData, XMap* dictionaries, const char* data, const size_t size) {
    if (!XVector_ON) return;
    char byte = 0;
    char bitIdx = 0;
    // 预留1字节存储最后一个字节的有效比特数
    char lastBits = 0;
    XVector_push_back_base(gzipData, &lastBits);  // 占位

    for (size_t i = 0; i < size; i++) {
        unsigned char ch = (unsigned char)data[i];
        DictionaryValue* dv = XMap_value_base(dictionaries, &ch);
        if (ISNULL(dv->code, "编码为空")) continue;

        // 遍历编码位
        for (size_t j = 0; j < XVector_size_base(dv->code); j++) {
            char bit = *(char*)XVector_at_base(dv->code, j);
            if (bit) {
                byte |= (1 << (7 - bitIdx));
            }
            else {
                byte &= ~(1 << (7 - bitIdx));
            }
            bitIdx++;
            if (bitIdx == 8) {  // 满1字节
                XVector_push_back_base(gzipData, &byte);
                byte = 0;
                bitIdx = 0;
            }
        }
    }

    // 处理剩余比特
    if (bitIdx != 0) {
        XVector_push_back_base(gzipData, &byte);
        lastBits = bitIdx;  // 记录有效比特数
    }
    // 更新预留的有效比特数
    *(char*)XVector_at_base(gzipData, 0) = lastBits;
}

// 压缩函数
XVector* XHfmTree_gzip(XHuffmanTree* tree, const char* data, const size_t size) {
    if (ISNULL(tree, "哈夫曼树为空") || ISNULL(data, "数据为空") || size == 0) {
        return NULL;
    }
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return NULL;
    }
    XHfmTree_clear(tree);
    if (!XHfmTree_readData(tree, data, size)) {
        return NULL;  // 构建树失败
    }
    XVector* gzipData = XVector_Create(char);
    if (ISNULL(gzipData, "创建压缩数据向量失败")) {
        return NULL;
    }
    // 写入字典
    XHfmTree_writeCompressDictionaries(gzipData, tree->dictionaries);
    // 写入压缩数据（包含有效比特数）
    writeData(gzipData, tree->dictionaries, data, size);
    return gzipData;
}

// 读取压缩字典
static size_t readDictionaries(XHuffmanTree* tree, const char* data) {
    if (ISNULL(tree, "哈夫曼树为空") || ISNULL(data, "数据为空")) {
        return 0;
    }
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return 0;
    }
    size_t offset = 0;
    size_t count = *(size_t*)data;
    offset += sizeof(size_t);
    const DictionaryData* dictData = (const DictionaryData*)(data + offset);

    for (size_t i = 0; i < count; i++) {
        unsigned char ch = dictData->ch;
        DictionaryValue* dv = XMap_value_base(tree->dictionaries, &ch);
        dv->count = dictData->count;
        dv->code = XVector_Create(char);
        if (ISNULL(dv->code, "创建编码向量失败")) {
            offset += sizeof(DictionaryData) + dictData->codeSize;
            dictData = (const DictionaryData*)(data + offset);
            continue;
        }
        XVector_resize_base(dv->code, dictData->codeSize);
        offset += sizeof(DictionaryData);
        memcpy(XContainerDataPtr(dv->code), data + offset, dictData->codeSize);
        offset += dictData->codeSize;
        dictData = (const DictionaryData*)(data + offset);
    }
    return offset;
}

// 解压时写入单个字符
static bool writeUnZip(XVector* unzipData, XHfmNode* node) {
    XHfmNodeData* data = XHfmTree_GetNodeData(node);
    if (data->code != NULL) {  // 叶子节点
        XVector_push_back_base(unzipData, &data->ch);
        return true;
    }
    return false;
}

// 累加字符总数量
static void addCount(XPair** pair, size_t* total) {
    *total += XPair_Second(*pair, DictionaryValue).count;
}

// 解压数据
static XVector* writeUnZipData(XHuffmanTree* tree, const char* data, const size_t size) {
    if (ISNULL(tree, "哈夫曼树为空") || ISNULL(data, "数据为空") || size == 0) {
        return NULL;
    }
    if (!XVector_ON) {
        IS_ON_DEBUG(XVector_ON);
        return NULL;
    }
    size_t totalCount = 0;
    XMap_iterator_for_each(tree->dictionaries, addCount, &totalCount);
    if (totalCount == 0) {
        return XVector_Create(char);
    }

    XVector* unzipData = XVector_Create(char);
    XHfmNode* root = tree->root;
    XHfmNode* current = root;
    size_t count = 0;
    // 读取最后一个字节的有效比特数
    char lastBits = data[0];
    const char* bitData = data + 1;  // 实际比特数据从第2字节开始
    size_t bitDataSize = size - 1;

    for (size_t i = 0; i < bitDataSize; i++) {
        char byte = bitData[i];
        // 计算当前字节需要处理的比特数（最后一个字节特殊处理）
        char bitsToProcess = (i == bitDataSize - 1) ? lastBits : 8;
        if (bitsToProcess == 0) bitsToProcess = 8;  // 兼容无剩余比特的情况

        for (char j = 0; j < bitsToProcess; j++) {
            char bit = (byte >> (7 - j)) & 0x01;
            // 移动到子节点
            current = (bit == 0) ? XBTreeNode_GetLChild(current) : XBTreeNode_GetRChild(current);
            if (ISNULL(current, "无效编码（子节点为空）")) {
                XVector_delete_base(unzipData);
                return NULL;
            }
            // 检查是否到达叶子节点
            if (writeUnZip(unzipData, current)) {
                current = root;
                if (++count == totalCount) {
                    return unzipData;  // 解压完成
                }
            }
        }
    }
    return unzipData;
}

// 解压函数
XVector* XHfmTree_unzip(XHuffmanTree* tree, const char* data, const size_t size) {
    if (ISNULL(tree, "哈夫曼树为空") || ISNULL(data, "数据为空") || size == 0) {
        return NULL;
    }
    XHfmTree_clear(tree);
    size_t offset = readDictionaries(tree, data);
    if (offset == 0) {
        return NULL;
    }
    tree->root = XHfmTree_DictionariesToCreationTree(tree->dictionaries);
    if (ISNULL(tree->root, "构建解压树失败")) {
        return NULL;
    }
    // 解压数据（包含有效比特数的1字节 + 实际数据）
    return writeUnZipData(tree, data + offset, size - offset);
}

// 释放编码向量（字典迭代回调）
static void freeCode(XPair** pair, void* args) {
    if (XVector_ON) {
        DictionaryValue dv = XPair_Second(*pair, DictionaryValue);
        if (dv.code != NULL) {
            XVector_delete_base(dv.code);
        }
    }
}

// 清空哈夫曼树
void XHfmTree_clear(XHuffmanTree* tree) {
    if (ISNULL(tree, "哈夫曼树为空")) return;
    // 释放树节点
    if (tree->root != NULL) {
        XTree_delete(tree->root, NULL, NULL);
        tree->root = NULL;
    }
    // 释放字典中的编码向量
    XMap_iterator_for_each(tree->dictionaries, freeCode, NULL);
    XMap_clear_base(tree->dictionaries);
}

// 释放哈夫曼树
void XHfmTree_delete(XHuffmanTree* tree) {
    if (ISNULL(tree, "哈夫曼树为空")) return;
    XHfmTree_clear(tree);
    XMap_delete_base(tree->dictionaries);
    XMemory_free(tree);
}

#endif // XMap_ON