import io

p = 'Src/XData/XExcel/XSharedStrings.c'
b = open(p, 'rb').read()

helper = '''/* [2026-10-31 UAF 根修] 按身份删除表条目。
 * 表的键是 XRichString 内部的 XString（XRichString_text），而 map 的比较
 * 函数比较的是**文本内容**（不是指针）。按内容相等删除（XMapBase_remove_base）
 * 可能命中「文本相同但指针不同」的另一条目——被删 rich 的键指针就此悬垂在
 * 表中，下次查表即比较已释放的 XString（UAF：Excel 往返用例实测
 * XString_compare -> XString_unicode 读已释放堆块）。
 * 本辅助按**指针身份**删除指定条目，并让幸存的同文本条目补登记，避免
 * 制造重复文本条目（重复正是歧义之源）。 */
static bool XSharedStrings_eraseEntryByKey(XSharedStrings* self, XString* ownedKey)
{
    XMap_iterator it;
    bool erased = false;
    if (!self || !self->m_stringTable || !ownedKey) return false;
    it = XMap_begin(self->m_stringTable);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString** kp = pair ? (XString**)XPair_first(pair) : NULL;
        if (kp && *kp == ownedKey) {
            XMapBase_iterator next;
            XMapBase_erase_base((XMapBase*)self->m_stringTable, &it, &next);
            erased = true;
            break;
        }
        XMap_iterator_add(self->m_stringTable, &it);
    }
    return erased;
}

'''.replace('
', '
').encode('utf-8')

anchor = b'static XlsxSharedStringInfo* XSharedStrings_shared_string_info(XSharedStrings* self,'
assert b.count(anchor) == 1
b = b.replace(anchor, helper + anchor, 1)

old = '''    if (--info->m_count <= 0) {
        int idx = info->m_index;
        XString* key = (XString*)string;
        XMap_remove_base((XMapBase*)self->m_stringTable, &key);
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
        if (rich) XRichString_delete(rich);
        XVector_removeAt_base(self->m_stringList, (size_t)idx);
        XMap_iterator it = XMap_begin(self->m_stringTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_stringTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XlsxSharedStringInfo* current =
                pair ? (XlsxSharedStringInfo*)XPair_second(pair) : NULL;
            if (current && current->m_index > idx) current->m_index--;
        }
        for (size_t i = XVector_size_base(self->m_stringList); i > 0; --i) {
            XRichString* candidate = *(XRichString**)XVector_at_base(
                self->m_stringList, i - 1);
            const XString* candidateText = candidate && !XRichString_isRichString(candidate)
                ? XRichString_text(candidate) : NULL;
            if (candidateText &&
                XString_compare(candidateText, string) == XCompare_Equality) {
                XString* candidateKey = (XString*)candidateText;
                XlsxSharedStringInfo replacement = {(int)(i - 1), 0};
                XMap_insert_base(self->m_stringTable, &candidateKey, &replacement);
                break;
            }
        }
    }
}'''.replace('
', '
').encode('utf-8')

new = '''    if (--info->m_count <= 0) {
        int idx = info->m_index;
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
        /* 按身份删除（键属于被删 rich），不按内容相等——见
           XSharedStrings_eraseEntryByKey。 */
        if (rich) {
            XString* ownedKey = (XString*)XRichString_text(rich);
            XSharedStrings_eraseEntryByKey(self, ownedKey);
            XRichString_delete(rich);
        }
        XVector_removeAt_base(self->m_stringList, (size_t)idx);
        /* 幸存条目重排索引（删除位之后的整体前移）。 */
        {
            XMap_iterator it = XMap_begin(self->m_stringTable);
            for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_stringTable, &it)) {
                XPair* pair = XMap_iterator_data(&it);
                XlsxSharedStringInfo* current =
                    pair ? (XlsxSharedStringInfo*)XPair_second(pair) : NULL;
                if (current && current->m_index > idx) current->m_index--;
            }
        }
        /* 幸存的同文本 rich（同一文本从文件加载两次时可能存在）需补登记，
           否则后续查表找不到——但按身份登记：按内容插入会造出重复条目
           （map 无法同时容纳两个同文本键），重复正是歧义与 UAF 的源头。 */
        for (size_t i = XVector_size_base(self->m_stringList); i > 0; --i) {
            XRichString* candidate = *(XRichString**)XVector_at_base(
                self->m_stringList, i - 1);
            const XString* candidateText = candidate && !XRichString_isRichString(candidate)
                ? XRichString_text(candidate) : NULL;
            if (candidateText &&
                XString_compare(candidateText, string) == XCompare_Equality) {
                XString* candidateKey = (XString*)candidateText;
                XlsxSharedStringInfo* current =
                    XSharedStrings_shared_string_info(self, candidateKey);
                if (current) {
                    current->m_index = (int)(i - 1);
                    current->m_count = 0;
                } else {
                    XlsxSharedStringInfo replacement = {(int)(i - 1), 0};
                    XMap_insert_base(self->m_stringTable, &candidateKey, &replacement);
                }
                break;
            }
        }
    }
}'''.replace('
', '
').encode('utf-8')

print('body found:', b.count(old))
assert b.count(old) == 1
b = b.replace(old, new, 1)
open(p, 'wb').write(b)
print('patched')
