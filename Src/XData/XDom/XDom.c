#include "XDom.h"

#include "XMemory.h"
#include "XXmlStreamReader.h"
#include "XStringUtils.h"   /* XStrlen/XStrchr/XSnprintf：库内字符串原语（外部依赖约束） */

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct XDomContext {
    int m_liveNodes;
} XDomContext;

enum {
    XDom_CollectionNone = 0,
    XDom_CollectionList = 1,
    XDom_CollectionMap = 2
};

enum {
    XDom_MapAttributes = 1,
    XDom_MapEntities = 2,
    XDom_MapNotations = 3
};

enum {
    XDom_ListChildren = 1,
    XDom_ListElements = 2
};

struct XDomNodePrivate {
    int m_refs;
    int m_collectionKind;
    int m_mapKind;
    XDomContext* m_context;
    XDomNodeType m_type;
    XString* m_name;
    XString* m_value;
    XString* m_namespaceUri;
    XString* m_localName;
    XString* m_prefix;
    XString* m_publicId;
    XString* m_systemId;
    XString* m_notationName;
    XString* m_internalSubset;
    XString* m_textCache;
    XString* m_attributeCache;
    XString* m_attributeNSCache;
    XString* m_documentVersion;
    XString* m_documentEncoding;
    bool m_specified;
    bool m_publicIdNull;
    bool m_systemIdNull;
    bool m_createdWithNamespace;
    bool m_hasXmlDeclaration;
    bool m_isStandalone;
    bool m_hasStandalone;
    int64_t m_line;
    int64_t m_column;
    XDomNodePrivate* m_parent;
    XDomNodePrivate* m_ownerDocument;
    XDomNodePrivate** m_children;
    int m_childCount;
    int m_childCapacity;
    XDomNodePrivate** m_attributes;
    int m_attributeCount;
    int m_attributeCapacity;
    XDomNodePrivate** m_entities;
    int m_entityCount;
    int m_entityCapacity;
    XDomNodePrivate** m_notations;
    int m_notationCount;
    int m_notationCapacity;
    XDomNodePrivate** m_items;
    int m_itemCount;
    int m_itemCapacity;
    XDomNodePrivate* m_mapOwner;
    XDomNodePrivate* m_doctype;
    XDomNodePrivate* m_listOwner;
    int m_listQueryKind;
    XString* m_listTagName;
    XString* m_listNamespaceUri;
};

static XDomInvalidDataPolicy g_xxml_dom_invalid_data_policy = XDom_AcceptInvalidChars;

static XString* XDom_emptyString(void)
{
    static XString* empty = NULL;
    if (!empty) empty = XString_create();
    return empty;
}

static bool XDom_stringEqual(const XString* left, const XString* right)
{
    if (!left || !right) return left == right;
    return XString_equals(left, right, XChar_CaseSensitive);
}

static bool XDom_stringEqual_utf8(const XString* left, const char* right)
{
    if (!left || !right) return false;
    return XString_equals_utf8(left, right, XChar_CaseSensitive);
}

static void XDom_stringAssign(XString** target, const XString* source)
{
    if (!target) return;
    if (!*target) *target = XString_create();
    if (!*target) return;
    if (source) XString_assign(*target, source);
    else XString_clear_base(*target);
}

/* [死码清理] xxml_dom_string_assign_utf8 已删除：全仓无调用点（见审计清单）。
 */

static void XDom_stringDelete(XString** value)
{
    if (value && *value) {
        XClassDelete(*value);
        *value = NULL;
    }
}

static XDomContext* XDom_contextCreate(void)
{
    XDomContext* context = (XDomContext*)XMalloc_System(sizeof(XDomContext));
    if (context) context->m_liveNodes = 0;
    return context;
}

static bool XDom_arrayReserve(XDomNodePrivate*** array, int* capacity, int required)
{
    if (!array || !capacity || required < 0) return false;
    if (*capacity >= required) return true;
    int next = *capacity > 0 ? *capacity : 4;
    while (next < required) {
        if (next > 1073741823) return false;
        next *= 2;
    }
    XDomNodePrivate** resized = (XDomNodePrivate**)XRealloc_System(
        *array, (size_t)next * sizeof(XDomNodePrivate*));
    if (!resized) return false;
    *array = resized;
    *capacity = next;
    return true;
}

static bool XDom_arrayAppend(XDomNodePrivate*** array, int* count,
                                  int* capacity, XDomNodePrivate* value)
{
    if (!array || !count || !capacity || !value) return false;
    if (!XDom_arrayReserve(array, capacity, *count + 1)) return false;
    (*array)[(*count)++] = value;
    return true;
}

static int XDom_arrayIndex(XDomNodePrivate* const* array, int count,
                                const XDomNodePrivate* value)
{
    if (!array || !value) return -1;
    for (int i = 0; i < count; ++i)
        if (array[i] == value) return i;
    return -1;
}

static void XDom_arrayRemoveAt(XDomNodePrivate** array, int* count, int index)
{
    if (!array || !count || index < 0 || index >= *count) return;
    for (int i = index + 1; i < *count; ++i) array[i - 1] = array[i];
    --*count;
}

static XDomNodePrivate* XDom_nodeNewInContext(XDomContext* context,
                                                         XDomNodeType type);
static void XDom_nodeRetain(XDomNodePrivate* node);
static void XDom_nodeRelease(XDomNodePrivate* node);
static void XDom_nodeClear(XDomNodePrivate* node);
static int XDom_childIndex(const XDomNodePrivate* parent,
                                const XDomNodePrivate* child);
static bool XDom_appendPrivate(XDomNodePrivate* parent,
                                    XDomNodePrivate* child, int index);
static void XDom_attrSetValuePrivate(XDomNodePrivate* attr,
                                             const XString* value);

static void XDom_clearOwnerDocument(XDomNodePrivate* node)
{
    if (!node) return;
    node->m_ownerDocument = NULL;
    if (node->m_type == XDom_DocumentNode && node->m_doctype)
        XDom_clearOwnerDocument(node->m_doctype);
    for (int i = 0; i < node->m_childCount; ++i)
        XDom_clearOwnerDocument(node->m_children[i]);
    for (int i = 0; i < node->m_attributeCount; ++i)
        XDom_clearOwnerDocument(node->m_attributes[i]);
}

static void XDom_setOwnerDocument(XDomNodePrivate* node,
                                         XDomNodePrivate* document)
{
    if (!node) return;
    node->m_ownerDocument = document;
    if (node->m_type == XDom_DocumentNode && node->m_doctype)
        XDom_setOwnerDocument(node->m_doctype, document);
    for (int i = 0; i < node->m_childCount; ++i)
        XDom_setOwnerDocument(node->m_children[i], document);
    for (int i = 0; i < node->m_attributeCount; ++i)
        XDom_setOwnerDocument(node->m_attributes[i], document);
}

static void XDom_nodeFree(XDomNodePrivate* node)
{
    if (!node) return;
    for (int i = 0; i < node->m_childCount; ++i) {
        node->m_children[i]->m_parent = NULL;
        if (node->m_type == XDom_DocumentNode)
            XDom_clearOwnerDocument(node->m_children[i]);
        XDom_nodeRelease(node->m_children[i]);
    }
    for (int i = 0; i < node->m_attributeCount; ++i) {
        node->m_attributes[i]->m_parent = NULL;
        XDom_nodeRelease(node->m_attributes[i]);
    }
    for (int i = 0; i < node->m_entityCount; ++i)
        XDom_nodeRelease(node->m_entities[i]);
    for (int i = 0; i < node->m_notationCount; ++i)
        XDom_nodeRelease(node->m_notations[i]);
    if (node->m_type == XDom_DocumentNode)
        XDom_nodeRelease(node->m_doctype);
    XFree_System(node->m_children);
    XFree_System(node->m_attributes);
    XFree_System(node->m_entities);
    XFree_System(node->m_notations);
    XDom_stringDelete(&node->m_name);
    XDom_stringDelete(&node->m_value);
    XDom_stringDelete(&node->m_namespaceUri);
    XDom_stringDelete(&node->m_localName);
    XDom_stringDelete(&node->m_prefix);
    XDom_stringDelete(&node->m_publicId);
    XDom_stringDelete(&node->m_systemId);
    XDom_stringDelete(&node->m_notationName);
    XDom_stringDelete(&node->m_internalSubset);
    XDom_stringDelete(&node->m_textCache);
    XDom_stringDelete(&node->m_attributeCache);
    XDom_stringDelete(&node->m_attributeNSCache);
    XDom_stringDelete(&node->m_documentVersion);
    XDom_stringDelete(&node->m_documentEncoding);
    XDomContext* context = node->m_context;
    XFree_System(node);
    if (context && --context->m_liveNodes == 0) XFree_System(context);
}

static void XDom_nodeRetain(XDomNodePrivate* node)
{
    if (node) ++node->m_refs;
}

static void XDom_nodeRelease(XDomNodePrivate* node)
{
    if (!node) return;
    if (node->m_collectionKind != XDom_CollectionNone) {
        if (--node->m_refs > 0) return;
        if (node->m_collectionKind == XDom_CollectionList) {
            for (int i = 0; i < node->m_itemCount; ++i)
                XDom_nodeRelease(node->m_items[i]);
            XFree_System(node->m_items);
            if (node->m_listOwner) XDom_nodeRelease(node->m_listOwner);
            XDom_stringDelete(&node->m_listTagName);
            XDom_stringDelete(&node->m_listNamespaceUri);
        } else {
            if (node->m_mapOwner) XDom_nodeRelease(node->m_mapOwner);
        }
        XFree_System(node);
        return;
    }
    if (--node->m_refs == 0) XDom_nodeFree(node);
}

static XDomNodePrivate* XDom_nodeNewInContext(XDomContext* context,
                                                         XDomNodeType type)
{
    bool ownContext = false;
    if (!context) {
        context = XDom_contextCreate();
        if (!context) return NULL;
        ownContext = true;
    }
    XDomNodePrivate* node = (XDomNodePrivate*)XMalloc_System(sizeof(*node));
    if (!node) {
        if (ownContext) XFree_System(context);
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    /* 新节点先由调用者持有一个引用；挂接到树或包装成句柄时再增加引用。 */
    node->m_refs = 1;
    node->m_context = context;
    node->m_type = type;
    node->m_publicIdNull = true;
    node->m_systemIdNull = true;
    node->m_line = -1;
    node->m_column = -1;
    node->m_name = XString_create();
    node->m_value = XString_create();
    node->m_namespaceUri = XString_create();
    node->m_localName = XString_create();
    node->m_prefix = XString_create();
    node->m_publicId = XString_create();
    node->m_systemId = XString_create();
    node->m_notationName = XString_create();
    node->m_internalSubset = XString_create();
    node->m_textCache = NULL;
    node->m_attributeCache = NULL;
    node->m_attributeNSCache = NULL;
    node->m_documentVersion = XString_create();
    node->m_documentEncoding = XString_create();
    if (!node->m_name || !node->m_value || !node->m_namespaceUri ||
        !node->m_localName || !node->m_prefix || !node->m_publicId ||
        !node->m_systemId || !node->m_notationName || !node->m_internalSubset ||
        !node->m_documentVersion || !node->m_documentEncoding) {
        XDom_stringDelete(&node->m_name);
        XDom_stringDelete(&node->m_value);
        XDom_stringDelete(&node->m_namespaceUri);
        XDom_stringDelete(&node->m_localName);
        XDom_stringDelete(&node->m_prefix);
        XDom_stringDelete(&node->m_publicId);
        XDom_stringDelete(&node->m_systemId);
        XDom_stringDelete(&node->m_notationName);
        XDom_stringDelete(&node->m_internalSubset);
        XDom_stringDelete(&node->m_documentVersion);
        XDom_stringDelete(&node->m_documentEncoding);
        XFree_System(node);
        /* 本失败路径 m_liveNodes 尚未递增（成功路径末尾才 ++），
           须像上方 node 分配失败分支一样直接释放自有 context，
           递减判断 0-1=-1 恒不成立会泄漏 context。 */
        if (ownContext) XFree_System(context);
        return NULL;
    }
    switch (type) {
        case XDom_TextNode: XString_assign_utf8(node->m_name, "#text"); break;
        case XDom_CDATASectionNode: XString_assign_utf8(node->m_name, "#cdata-section"); break;
        case XDom_CommentNode: XString_assign_utf8(node->m_name, "#comment"); break;
        case XDom_DocumentNode: XString_assign_utf8(node->m_name, "#document"); break;
        case XDom_DocumentFragmentNode: XString_assign_utf8(node->m_name, "#document-fragment"); break;
        default: break;
    }
    ++context->m_liveNodes;
    return node;
}

static XDomNodePrivate* XDom_nodeNew(XDomNodeType type)
{
    return XDom_nodeNewInContext(NULL, type);
}

static void XDom_setQualifiedName(XDomNodePrivate* node,
                                         const XString* namespaceURI,
                                         const XString* qualifiedName)
{
    if (!node) return;
    XDom_stringAssign(&node->m_name, qualifiedName);
    XDom_stringAssign(&node->m_namespaceUri, namespaceURI);
    XDom_stringAssign(&node->m_prefix, NULL);
    XDom_stringAssign(&node->m_localName, qualifiedName);
    node->m_createdWithNamespace = true;
    const char* qName = qualifiedName ? XString_toUtf8(qualifiedName) : "";
    const char* colon = qName ? XStrchr(qName, ':') : NULL;
    if (colon) {
        XString* prefix = XString_create_with_length_utf8(qName, (size_t)(colon - qName));
        XString* local = XString_create_utf8(colon + 1);
        if (prefix) {
            XDom_stringAssign(&node->m_prefix, prefix);
            XClassDelete(prefix);
        }
        if (local) {
            XDom_stringAssign(&node->m_localName, local);
            XClassDelete(local);
        }
    }
}

/* DOM Level 1 创建的节点不填充 localName、prefix、namespaceURI。 */
static void XDom_setPlainName(XDomNodePrivate* node, const XString* name)
{
    if (!node) return;
    XDom_stringAssign(&node->m_name, name);
    XDom_stringAssign(&node->m_namespaceUri, NULL);
    XDom_stringAssign(&node->m_localName, NULL);
    XDom_stringAssign(&node->m_prefix, NULL);
    node->m_createdWithNamespace = false;
}

static XDomNodePrivate* XDom_documentForNode(XDomNodePrivate* node)
{
    if (!node) return NULL;
    if (node->m_type == XDom_DocumentNode) return node;
    return node->m_ownerDocument;
}

static bool XDom_nameCharAllowed(XChar ch, bool first)
{
    if (first)
        return XChar_isLetter(ch) || ch == XChar_from('_') || ch == XChar_from(':');
    return XChar_isLetterOrNumber(ch) || XChar_isMark(ch) ||
           ch == XChar_from('_') || ch == XChar_from('-') ||
           ch == XChar_from('.') || ch == XChar_from(':');
}

static void XDom_appendChars(XString* target, const XChar* chars,
                                  size_t begin, size_t end)
{
    if (!target || !chars) return;
    for (size_t i = begin; i < end; ++i) XString_append_char(target, chars[i]);
}

static XString* XDom_fixedName(const XString* name, bool namespaces, bool* ok)
{
    if (ok) *ok = false;
    if (!name || XString_isEmpty_base(name)) return NULL;

    const XChar* chars = XString_unicode(name);
    size_t length = XString_length_base(name);
    size_t localBegin = 0;
    size_t prefixLength = 0;
    if (namespaces) {
        for (size_t i = 0; i < length; ++i) {
            if (chars[i] == XChar_from(':')) {
                /* Qt 允许空前缀，只有本地名为空时才是无效限定名。 */
                if (i + 1 >= length) return NULL;
                prefixLength = i;
                localBegin = i + 1;
                break;
            }
        }
    }

    if (g_xxml_dom_invalid_data_policy == XDom_AcceptInvalidChars) {
        XString* copy = XString_create_copy(name);
        if (copy && ok) *ok = true;
        return copy;
    }

    XString* local = XString_create();
    if (!local) return NULL;
    bool first = true;
    for (size_t i = localBegin; i < length; ++i) {
        XChar ch = chars[i];
        if (XDom_nameCharAllowed(ch, first)) {
            XString_append_char(local, ch);
            first = false;
        } else if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XClassDelete(local);
            return NULL;
        }
    }
    if (XString_isEmpty_base(local)) {
        XClassDelete(local);
        return NULL;
    }

    XString* result = XString_create();
    if (!result) {
        XClassDelete(local);
        return NULL;
    }
    if (namespaces && prefixLength > 0) {
        XDom_appendChars(result, chars, 0, prefixLength);
        XString_append_char(result, XChar_from(':'));
    }
    XString_append(result, local);
    XClassDelete(local);
    if (ok) *ok = !XString_isEmpty_base(result);
    return result;
}

/* [死码清理] xxml_dom_valid_name 已删除：全仓无调用点（见审计清单）。
 */

static XDomNodePrivate* XDom_collectionNew(int kind)
{
    XDomNodePrivate* collection = (XDomNodePrivate*)XMalloc_System(sizeof(*collection));
    if (!collection) return NULL;
    memset(collection, 0, sizeof(*collection));
    collection->m_refs = 1;
    collection->m_collectionKind = kind;
    return collection;
}

static XDomNodePrivate* XDom_listNewForOwner(XDomNodePrivate* owner,
                                                    int queryKind,
                                                    const XString* tagName,
                                                    const XString* namespaceURI)
{
    XDomNodePrivate* list = XDom_collectionNew(XDom_CollectionList);
    if (!list) return NULL;
    list->m_listOwner = owner;
    list->m_listQueryKind = queryKind;
    if (owner) XDom_nodeRetain(owner);
    if (tagName) list->m_listTagName = XString_create_copy(tagName);
    if (namespaceURI) list->m_listNamespaceUri = XString_create_copy(namespaceURI);
    if ((tagName && !list->m_listTagName) ||
        (namespaceURI && !list->m_listNamespaceUri)) {
        XDom_nodeRelease(list);
        return NULL;
    }
    return list;
}

static XDomNodePrivate* XDom_mapNew(XDomNodePrivate* owner, int mapKind)
{
    XDomNodePrivate* map = XDom_collectionNew(XDom_CollectionMap);
    if (!map) return NULL;
    map->m_mapOwner = owner;
    map->m_mapKind = mapKind;
    if (owner) XDom_nodeRetain(owner);
    return map;
}

static XDomNodePrivate** XDom_mapItems(XDomNodePrivate* map, int* count)
{
    if (count) *count = 0;
    if (!map || map->m_collectionKind != XDom_CollectionMap || !map->m_mapOwner)
        return NULL;
    XDomNodePrivate* owner = map->m_mapOwner;
    switch (map->m_mapKind) {
        case XDom_MapAttributes:
            if (count) *count = owner->m_attributeCount;
            return owner->m_attributes;
        case XDom_MapEntities:
            if (count) *count = owner->m_entityCount;
            return owner->m_entities;
        case XDom_MapNotations:
            if (count) *count = owner->m_notationCount;
            return owner->m_notations;
        default:
            return NULL;
    }
}

static XDomNodePrivate* XDom_mapAt(XDomNodePrivate* map, int index)
{
    int count = 0;
    XDomNodePrivate** items = XDom_mapItems(map, &count);
    return items && index >= 0 && index < count ? items[index] : NULL;
}

static int XDom_mapFindName(XDomNodePrivate* map, const XString* name)
{
    int count = 0;
    XDomNodePrivate** items = XDom_mapItems(map, &count);
    for (int i = 0; items && i < count; ++i)
        if (XDom_stringEqual(items[i]->m_name, name)) return i;
    return -1;
}

static int XDom_mapFindNs(XDomNodePrivate* map, const XString* namespaceURI,
                                const XString* localName)
{
    int count = 0;
    XDomNodePrivate** items = XDom_mapItems(map, &count);
    for (int i = 0; items && i < count; ++i) {
        if (XDom_stringEqual(items[i]->m_namespaceUri, namespaceURI) &&
            XDom_stringEqual(items[i]->m_localName, localName)) return i;
    }
    return -1;
}

static bool XDom_nodeIsDescendant(XDomNodePrivate* node,
                                        XDomNodePrivate* possibleParent)
{
    for (XDomNodePrivate* current = possibleParent; current; current = current->m_parent)
        if (current == node) return true;
    return false;
}

static bool XDom_nodeChildAllowed(XDomNodePrivate* parent,
                                        XDomNodePrivate* child)
{
    if (!parent || !child) return false;
    if (parent->m_type == XDom_DocumentTypeNode)
        return child->m_type == XDom_EntityNode || child->m_type == XDom_NotationNode;
    if (parent->m_type == XDom_TextNode ||
        parent->m_type == XDom_CDATASectionNode ||
        parent->m_type == XDom_CommentNode ||
        parent->m_type == XDom_EntityNode ||
        parent->m_type == XDom_NotationNode) return false;
    if (parent->m_type == XDom_AttributeNode)
        return child->m_type == XDom_TextNode;
    /* Qt 历史兼容行为允许属性节点通过 append/insert 接口进入子节点序列。 */
    if (child->m_type == XDom_EntityNode || child->m_type == XDom_NotationNode) return false;
    if (parent->m_type == XDom_DocumentNode &&
        child->m_type != XDom_ElementNode && child->m_type != XDom_DocumentTypeNode &&
        child->m_type != XDom_CommentNode && child->m_type != XDom_ProcessingInstructionNode)
        return false;
    if (parent->m_type == XDom_DocumentNode &&
        (child->m_type == XDom_ElementNode || child->m_type == XDom_DocumentTypeNode)) {
        for (int i = 0; i < parent->m_childCount; ++i) {
            XDomNodePrivate* existing = parent->m_children[i];
            if (existing != child && existing->m_type == child->m_type) return false;
        }
    }
    if (parent->m_type == XDom_ElementNode && child->m_type == XDom_DocumentNode)
        return false;
    return true;
}

static bool XDom_detachFromParent(XDomNodePrivate* node)
{
    if (!node || !node->m_parent) return true;
    XDomNodePrivate* parent = node->m_parent;
    if (parent->m_type == XDom_DocumentNode && parent->m_doctype == node) {
        parent->m_doctype = NULL;
        node->m_parent = NULL;
        XDom_nodeRelease(node);
        return true;
    }
    int index = XDom_arrayIndex(parent->m_children, parent->m_childCount, node);
    if (index >= 0) {
        XDom_arrayRemoveAt(parent->m_children, &parent->m_childCount, index);
        node->m_parent = NULL;
        if (parent->m_type == XDom_DocumentNode) XDom_clearOwnerDocument(node);
        XDom_nodeRelease(node);
        return true;
    }
    index = XDom_arrayIndex(parent->m_attributes, parent->m_attributeCount, node);
    if (index >= 0) {
        XDom_arrayRemoveAt(parent->m_attributes, &parent->m_attributeCount, index);
        node->m_parent = NULL;
        node->m_specified = true;
        XDom_nodeRelease(node);
        return true;
    }
    node->m_parent = NULL;
    return true;
}

static bool XDom_appendPrivate(XDomNodePrivate* parent,
                                    XDomNodePrivate* child, int index)
{
    if (!XDom_nodeChildAllowed(parent, child) || parent == child ||
        XDom_nodeIsDescendant(child, parent)) return false;
    /* Qt 将文档类型保存在 QDomDocumentPrivate::type 中，不放入文档子链表。 */
    if (parent->m_type == XDom_DocumentNode && child->m_type == XDom_DocumentTypeNode) {
        if (parent->m_doctype && parent->m_doctype != child) return false;
        if (!XDom_detachFromParent(child)) return false;
        parent->m_doctype = child;
        XDom_nodeRetain(child);
        child->m_parent = parent;
        XDom_setOwnerDocument(child, parent);
        return true;
    }
    XDomNodePrivate* oldParent = child->m_parent;
    int oldIndex = oldParent == parent ?
                   XDom_childIndex(parent, child) : -1;
    if (!XDom_detachFromParent(child)) return false;
    if (index < 0 || index > parent->m_childCount) index = parent->m_childCount;
    if (oldIndex >= 0 && oldIndex < index) --index;
    if (!XDom_arrayReserve(&parent->m_children, &parent->m_childCapacity,
                                parent->m_childCount + 1)) return false;
    for (int i = parent->m_childCount; i > index; --i)
        parent->m_children[i] = parent->m_children[i - 1];
    parent->m_children[index] = child;
    ++parent->m_childCount;
    XDom_nodeRetain(child);
    child->m_parent = parent;
    XDom_setOwnerDocument(child, XDom_documentForNode(parent));
    return true;
}

/* QDomAttr::setValue() 通过一个直接子 Text 节点保存属性值。 */
static void XDom_attrSetValuePrivate(XDomNodePrivate* attr,
                                             const XString* value)
{
    if (!attr || attr->m_type != XDom_AttributeNode) return;
    XDom_stringAssign(&attr->m_value, value);
    XDom_nodeClear(attr);
    XDomNodePrivate* text = XDom_nodeNewInContext(attr->m_context, XDom_TextNode);
    if (!text) return;
    XDom_stringAssign(&text->m_value, value);
    text->m_ownerDocument = attr->m_ownerDocument;
    if (!XDom_appendPrivate(attr, text, -1)) {
        XDom_nodeRelease(text);
        return;
    }
    XDom_nodeRelease(text);
}

static XDomNodePrivate* XDom_nodeClone(XDomNodePrivate* source,
                                               XDomContext* context, bool deep)
{
    if (!source || source->m_collectionKind != XDom_CollectionNone) return NULL;
    XDomNodePrivate* clone = XDom_nodeNewInContext(context, source->m_type);
    if (!clone) return NULL;
    XDom_stringAssign(&clone->m_name, source->m_name);
    XDom_stringAssign(&clone->m_value, source->m_value);
    XDom_stringAssign(&clone->m_namespaceUri, source->m_namespaceUri);
    XDom_stringAssign(&clone->m_localName, source->m_localName);
    XDom_stringAssign(&clone->m_prefix, source->m_prefix);
    XDom_stringAssign(&clone->m_publicId, source->m_publicId);
    XDom_stringAssign(&clone->m_systemId, source->m_systemId);
    XDom_stringAssign(&clone->m_notationName, source->m_notationName);
    XDom_stringAssign(&clone->m_internalSubset, source->m_internalSubset);
    XDom_stringAssign(&clone->m_documentVersion, source->m_documentVersion);
    XDom_stringAssign(&clone->m_documentEncoding, source->m_documentEncoding);
    clone->m_specified = source->m_specified;
    clone->m_publicIdNull = source->m_publicIdNull;
    clone->m_systemIdNull = source->m_systemIdNull;
    clone->m_createdWithNamespace = source->m_createdWithNamespace;
    clone->m_hasXmlDeclaration = source->m_hasXmlDeclaration;
    clone->m_isStandalone = source->m_isStandalone;
    clone->m_hasStandalone = source->m_hasStandalone;
    clone->m_line = source->m_line;
    clone->m_column = source->m_column;
    if (source->m_type == XDom_ElementNode || source->m_type == XDom_DocumentNode) {
        for (int i = 0; i < source->m_attributeCount; ++i) {
            XDomNodePrivate* attr = XDom_nodeClone(source->m_attributes[i], context, true);
            if (!attr || !XDom_arrayAppend(&clone->m_attributes,
                    &clone->m_attributeCount, &clone->m_attributeCapacity, attr)) {
                if (attr) XDom_nodeRelease(attr);
                XDom_nodeRelease(clone);
                return NULL;
            }
            XDom_nodeRetain(attr);
            attr->m_parent = clone;
            XDom_nodeRelease(attr);
        }
    }
    if (deep) {
        for (int i = 0; i < source->m_childCount; ++i) {
            XDomNodePrivate* child = XDom_nodeClone(source->m_children[i], context, true);
            if (!child || !XDom_appendPrivate(clone, child, -1)) {
                if (child) XDom_nodeRelease(child);
                XDom_nodeRelease(clone);
                return NULL;
            }
            XDom_nodeRelease(child);
        }
    }
    if (clone->m_type == XDom_DocumentTypeNode) {
        /* DTD 声明同时存在于 childNodes 和两个只读映射中，映射只增加引用。 */
        for (int i = 0; i < clone->m_childCount; ++i) {
            XDomNodePrivate* declaration = clone->m_children[i];
            if (declaration->m_type == XDom_EntityNode) {
                if (XDom_arrayAppend(&clone->m_entities, &clone->m_entityCount,
                                          &clone->m_entityCapacity, declaration))
                    XDom_nodeRetain(declaration);
            } else if (declaration->m_type == XDom_NotationNode) {
                if (XDom_arrayAppend(&clone->m_notations, &clone->m_notationCount,
                                          &clone->m_notationCapacity, declaration))
                    XDom_nodeRetain(declaration);
            }
        }
    }
    if (source->m_type == XDom_DocumentNode && source->m_doctype) {
        XDomNodePrivate* type = XDom_nodeClone(source->m_doctype, context, true);
        if (!type) {
            XDom_nodeRelease(clone);
            return NULL;
        }
        clone->m_doctype = type;
        type->m_parent = clone;
        XDom_setOwnerDocument(type, clone);
    }
    return clone;
}

static void XDom_nodeClear(XDomNodePrivate* node)
{
    if (!node) return;
    while (node->m_childCount > 0) {
        XDomNodePrivate* child = node->m_children[node->m_childCount - 1];
        --node->m_childCount;
        child->m_parent = NULL;
        if (node->m_type == XDom_DocumentNode) XDom_clearOwnerDocument(child);
        XDom_nodeRelease(child);
    }
    while (node->m_attributeCount > 0) {
        XDomNodePrivate* attr = node->m_attributes[node->m_attributeCount - 1];
        --node->m_attributeCount;
        attr->m_parent = NULL;
        XDom_nodeRelease(attr);
    }
    if (node->m_type == XDom_DocumentTypeNode) {
        while (node->m_entityCount > 0)
            XDom_nodeRelease(node->m_entities[--node->m_entityCount]);
        while (node->m_notationCount > 0)
            XDom_nodeRelease(node->m_notations[--node->m_notationCount]);
    }
    if (node->m_type == XDom_DocumentNode && node->m_doctype) {
        XDom_nodeRelease(node->m_doctype);
        node->m_doctype = NULL;
    }
}

static void XDom_handleDeinit(XDomNodePrivate** impl)
{
    if (!impl || !*impl) return;
    XDom_nodeRelease(*impl);
    *impl = NULL;
}

static void XDom_handleCopy(XDomNodePrivate** dest, const XDomNodePrivate* src)
{
    if (!dest || !src || dest == &src) return;
    if (*dest == src) return;
    XDom_handleDeinit(dest);
    *dest = (XDomNodePrivate*)src;
    XDom_nodeRetain(*dest);
}

static void XDom_handleMove(XDomNodePrivate** dest, XDomNodePrivate** src)
{
    if (!dest || !src || dest == src) return;
    if (*dest == *src) return;
    XDom_handleDeinit(dest);
    *dest = *src;
    *src = NULL;
}

static XVtable* XDom_makeVtable(void* copy, void* move, void* deinit)
{
    XVtable* table = XVtable_create();
    if (!table) return NULL;
    XVtable_append_vtable(table, XClass_class_init());
    XVtable_At(table, EXClass_Copy) = copy;
    XVtable_At(table, EXClass_Move) = move;
    XVtable_At(table, EXClass_Deinit) = deinit;
    return table;
}

#define XDOM_DEFINE_VTABLE(Type) \
    static void V##Type##_copy(XClass* dest, const XClass* src) \
    { \
        Type* destination = (Type*)dest; \
        const Type* source = (const Type*)src; \
        if (!destination || !source) return; \
        if ((const void*)destination == (const void*)source) return; \
        if (XClassIsVtableNull((XClass*)source)) return; \
        if (XClassIsVtableNull((XClass*)dest)) Type##_init(destination); \
        XDom_handleCopy(&destination->m_impl, source->m_impl); \
    } \
    static void V##Type##_move(XClass* dest, XClass* src) \
    { \
        Type* destination = (Type*)dest; \
        Type* source = (Type*)src; \
        if (!destination || !source) return; \
        if (destination == source) return; \
        if (XClassIsVtableNull((XClass*)source)) return; \
        if (XClassIsVtableNull((XClass*)dest)) Type##_init(destination); \
        XDom_handleMove(&destination->m_impl, &source->m_impl); \
    } \
    static void V##Type##_deinit(XClass* object) \
    { \
        Type* self = (Type*)object; \
        if (self) XDom_handleDeinit(&self->m_impl); \
    } \
    XVtable* Type##_class_init(void) \
    { \
        static XVtable* table = NULL; \
        if (table) return table; \
        table = XDom_makeVtable(V##Type##_copy, V##Type##_move, V##Type##_deinit); \
        XVTABLE_SET_NAME(table, #Type); \
        return table; \
    }

XDOM_DEFINE_VTABLE(XDomNode)
XDOM_DEFINE_VTABLE(XDomNodeList)
XDOM_DEFINE_VTABLE(XDomNamedNodeMap)
XDOM_DEFINE_VTABLE(XDomDocument)
XDOM_DEFINE_VTABLE(XDomDocumentType)
XDOM_DEFINE_VTABLE(XDomDocumentFragment)
XDOM_DEFINE_VTABLE(XDomCharacterData)
XDOM_DEFINE_VTABLE(XDomAttr)
XDOM_DEFINE_VTABLE(XDomElement)
XDOM_DEFINE_VTABLE(XDomText)
XDOM_DEFINE_VTABLE(XDomComment)
XDOM_DEFINE_VTABLE(XDomCDATASection)
XDOM_DEFINE_VTABLE(XDomEntityReference)
XDOM_DEFINE_VTABLE(XDomProcessingInstruction)
XDOM_DEFINE_VTABLE(XDomEntity)
XDOM_DEFINE_VTABLE(XDomNotation)

static void VXDomImplementation_copy(XClass* dest, const XClass* src)
{
    XDomImplementation* destination = (XDomImplementation*)dest;
    const XDomImplementation* source = (const XDomImplementation*)src;
    if (!destination || !source) return;
    if (destination == source) return;
    if (XClassIsVtableNull((XClass*)source)) return;
    if (XClassIsVtableNull(dest)) XDomImplementation_init(destination);
    XDom_nodeRelease(destination->m_document);
    destination->m_isNull = source->m_isNull;
    destination->m_document = source->m_document;
    XDom_nodeRetain(destination->m_document);
}

static void VXDomImplementation_move(XClass* dest, XClass* src)
{
    XDomImplementation* destination = (XDomImplementation*)dest;
    XDomImplementation* source = (XDomImplementation*)src;
    if (!destination || !source) return;
    if (destination == source) return;
    if (XClassIsVtableNull((XClass*)source)) return;
    if (XClassIsVtableNull(dest)) XDomImplementation_init(destination);
    XDom_nodeRelease(destination->m_document);
    destination->m_isNull = source->m_isNull;
    destination->m_document = source->m_document;
    source->m_document = NULL;
    source->m_isNull = true;
}

static void VXDomImplementation_deinit(XClass* object)
{
    XDomImplementation* self = (XDomImplementation*)object;
    if (self) {
        XDom_nodeRelease(self->m_document);
        self->m_document = NULL;
        self->m_isNull = true;
    }
}

XVtable* XDomImplementation_class_init(void)
{
    static XVtable* table = NULL;
    if (table) return table;
    table = XDom_makeVtable(VXDomImplementation_copy,
                                 VXDomImplementation_move,
                                 VXDomImplementation_deinit);
    XVTABLE_SET_NAME(table, "XDomImplementation");
    return table;
}

#define XDOM_DEFINE_LIFECYCLE(Type) \
    void Type##_init(Type* self) \
    { \
        if (!self) return; \
        memset(self, 0, sizeof(*self)); \
        XClass_init(&self->m_class); \
        XClassSetVtable(self, Type); \
    } \
    Type* Type##_create(void) \
    { \
        Type* self = (Type*)XClass_Malloc(Type); \
        if (!self) return NULL; \
        Type##_init(self); \
        Set_Class_IsHeap(self, true); \
        return self; \
    } \
    Type* Type##_create_copy(const Type* other) \
    { \
        if (!other) return NULL; \
        Type* self = Type##_create(); \
        if (!self) return NULL; \
        XClassCopy(self, other); \
        return self; \
    } \
    Type* Type##_create_move(Type* other) \
    { \
        if (!other) return NULL; \
        Type* self = Type##_create(); \
        if (!self) return NULL; \
        XClassMove(self, other); \
        return self; \
    }

XDOM_DEFINE_LIFECYCLE(XDomNode)
XDOM_DEFINE_LIFECYCLE(XDomNodeList)
XDOM_DEFINE_LIFECYCLE(XDomNamedNodeMap)
/* 文档需要默认创建一个非空 Document 节点，生命周期函数在下方单独实现。 */
XDOM_DEFINE_LIFECYCLE(XDomDocumentType)
XDOM_DEFINE_LIFECYCLE(XDomDocumentFragment)
XDOM_DEFINE_LIFECYCLE(XDomCharacterData)
XDOM_DEFINE_LIFECYCLE(XDomAttr)
XDOM_DEFINE_LIFECYCLE(XDomElement)
XDOM_DEFINE_LIFECYCLE(XDomText)
XDOM_DEFINE_LIFECYCLE(XDomComment)
XDOM_DEFINE_LIFECYCLE(XDomCDATASection)
XDOM_DEFINE_LIFECYCLE(XDomEntityReference)
XDOM_DEFINE_LIFECYCLE(XDomProcessingInstruction)
XDOM_DEFINE_LIFECYCLE(XDomEntity)
XDOM_DEFINE_LIFECYCLE(XDomNotation)

void XDomImplementation_init(XDomImplementation* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init(&self->m_class);
    XClassSetVtable(self, XDomImplementation);
    self->m_isNull = true;
}

XDomImplementation* XDomImplementation_create_ex(XMemoryType memory)
{
    XDomImplementation* self = (XDomImplementation*)XMemory_malloc(sizeof(XDomImplementation), memory);
    if (!self) return NULL;
    XDomImplementation_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XDomImplementation* XDomImplementation_create_copy(const XDomImplementation* other)
{
    if (!other || XClassIsVtableNull((const XClass*)other)) return NULL;
    XDomImplementation* self = XDomImplementation_create();
    if (!self) return NULL;
    XClassCopy(self, other);
    return self;
}

XDomImplementation* XDomImplementation_create_move(XDomImplementation* other)
{
    if (!other || XClassIsVtableNull((XClass*)other)) return NULL;
    XDomImplementation* self = XDomImplementation_create();
    if (!self) return NULL;
    XClassMove(self, other);
    return self;
}

/* QDomDocument 默认构造后是 null；首次工厂调用或 setContent 时再分配实现。 */
void XDomDocument_init(XDomDocument* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init(&self->m_class);
    XClassSetVtable(self, XDomDocument);
}

XDomDocument* XDomDocument_create_ex(XMemoryType memory)
{
    XDomDocument* self = (XDomDocument*)XMemory_malloc(sizeof(XDomDocument), memory);
    if (!self) return NULL;
    XDomDocument_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XDomDocument* XDomDocument_create_copy(const XDomDocument* other)
{
    if (!other || XClassIsVtableNull((const XClass*)other)) return NULL;
    XDomDocument* self = XDomDocument_create();
    if (!self) return NULL;
    XClassCopy(self, other);
    return self;
}

XDomDocument* XDomDocument_create_move(XDomDocument* other)
{
    if (!other || XClassIsVtableNull((XClass*)other)) return NULL;
    XDomDocument* self = XDomDocument_create();
    if (!self) return NULL;
    XClassMove(self, other);
    return self;
}

static void XDom_assignNewHandle(XDomNodePrivate** target,
                                        XDomNodePrivate* source)
{
    if (!target) return;
    XDom_handleDeinit(target);
    *target = source;
    XDom_nodeRetain(source);
}

static XDomNode* XDom_wrapNode(XDomNodePrivate* node)
{
    if (!node) return XDomNode_create();
    XDomNode* result = XDomNode_create();
    if (result) XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

/* [死码清理] xxml_dom_wrap_node_as_node 已删除：全仓无调用点（见审计清单）。
 */

static XDomElement* XDom_wrapElement(XDomNodePrivate* node)
{
    XDomElement* result = XDomElement_create();
    if (result && node && node->m_type == XDom_ElementNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomAttr* XDom_wrapAttr(XDomNodePrivate* node)
{
    XDomAttr* result = XDomAttr_create();
    if (result && node && node->m_type == XDom_AttributeNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomText* XDom_wrapText(XDomNodePrivate* node)
{
    XDomText* result = XDomText_create();
    if (result && node && node->m_type == XDom_TextNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomCDATASection* XDom_wrapCdata(XDomNodePrivate* node)
{
    XDomCDATASection* result = XDomCDATASection_create();
    if (result && node && node->m_type == XDom_CDATASectionNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomComment* XDom_wrapComment(XDomNodePrivate* node)
{
    XDomComment* result = XDomComment_create();
    if (result && node && node->m_type == XDom_CommentNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomCharacterData* XDom_wrapCharacterData(XDomNodePrivate* node)
{
    XDomCharacterData* result = XDomCharacterData_create();
    if (result && node && (node->m_type == XDom_TextNode ||
                           node->m_type == XDom_CommentNode))
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomDocument* XDom_wrapDocument(XDomNodePrivate* node)
{
    XDomDocument* result = XDomDocument_create();
    if (result && node && node->m_type == XDom_DocumentNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomDocumentType* XDom_wrapDoctype(XDomNodePrivate* node)
{
    XDomDocumentType* result = XDomDocumentType_create();
    if (result && node && node->m_type == XDom_DocumentTypeNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomDocumentFragment* XDom_wrapFragment(XDomNodePrivate* node)
{
    XDomDocumentFragment* result = XDomDocumentFragment_create();
    if (result && node && node->m_type == XDom_DocumentFragmentNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomEntityReference* XDom_wrapEntityReference(XDomNodePrivate* node)
{
    XDomEntityReference* result = XDomEntityReference_create();
    if (result && node && node->m_type == XDom_EntityReferenceNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomProcessingInstruction* XDom_wrapPi(XDomNodePrivate* node)
{
    XDomProcessingInstruction* result = XDomProcessingInstruction_create();
    if (result && node && node->m_type == XDom_ProcessingInstructionNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomEntity* XDom_wrapEntity(XDomNodePrivate* node)
{
    XDomEntity* result = XDomEntity_create();
    if (result && node && node->m_type == XDom_EntityNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomNotation* XDom_wrapNotation(XDomNodePrivate* node)
{
    XDomNotation* result = XDomNotation_create();
    if (result && node && node->m_type == XDom_NotationNode)
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}

static XDomNodeList* XDom_wrapList(XDomNodePrivate* list)
{
    XDomNodeList* result = XDomNodeList_create();
    if (result) XDom_assignNewHandle(&result->m_impl, list);
    return result;
}

static XDomNamedNodeMap* XDom_wrapMap(XDomNodePrivate* map)
{
    XDomNamedNodeMap* result = XDomNamedNodeMap_create();
    if (result) XDom_assignNewHandle(&result->m_impl, map);
    return result;
}

static void XDom_setDefaultCreationValue(XDomNodePrivate* node,
                                                 const XString* value)
{
    if (node && value) XDom_stringAssign(&node->m_value, value);
}

static bool XDom_xmlCharAllowed(XChar ch)
{
    if (XChar_isNonCharacter(ch) || XChar_isControl(ch))
        return ch == XChar_from(0x09) || ch == XChar_from(0x0a) || ch == XChar_from(0x0d);
    return (ch >= XChar_from(0x20) && ch <= XChar_from(0xd7ff)) ||
           (ch >= XChar_from(0xe000) && ch <= XChar_from(0xfffd));
}

static XString* XDom_fixedValue(const XString* value, XDomNodeType type, bool* ok)
{
    if (ok) *ok = false;
    XString* result = XString_create();
    if (!result) return NULL;
    if (g_xxml_dom_invalid_data_policy == XDom_AcceptInvalidChars) {
        if (value) XString_assign(result, value);
        if (ok) *ok = true;
        return result;
    }

    const XChar* chars = value ? XString_unicode(value) : NULL;
    size_t length = value ? XString_length_base(value) : 0;
    for (size_t i = 0; i < length; ++i) {
        XChar ch = chars[i];
        bool valid = XDom_xmlCharAllowed(ch);
        if (XChar_isHighSurrogate(ch)) {
            valid = i + 1 < length && XChar_isLowSurrogate(chars[i + 1]);
            if (valid) {
                XString_append_char(result, ch);
                XString_append_char(result, chars[++i]);
                continue;
            }
        } else if (XChar_isLowSurrogate(ch)) {
            valid = false;
        }
        if (valid) XString_append_char(result, ch);
        else if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XClassDelete(result);
            return NULL;
        }
    }

    const char* forbidden = NULL;
    if (type == XDom_CommentNode) forbidden = "--";
    else if (type == XDom_ProcessingInstructionNode) forbidden = "?>";
    if (type == XDom_CDATASectionNode) forbidden = "]]>";
    if (forbidden) {
        size_t forbiddenLength = XStrlen(forbidden);
        for (;;) {
            int64_t position = XString_indexOf_utf8(result, forbidden, 0,
                                                      XChar_CaseSensitive);
            if (position < 0) break;
            if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
                XClassDelete(result);
                return NULL;
            }
            XString_remove_base(result, (size_t)position, forbiddenLength);
        }
    }
    if (ok) *ok = true;
    return result;
}

/* ==================== 公共句柄与节点树辅助函数 ==================== */

static XDomNodePrivate* XDom_handleImpl(const void* handle)
{
    const XClass* base = (const XClass*)handle;
    if (!base || !base->m_vtable) return NULL;
    return ((const XDomNode*)handle)->m_impl;
}

static bool XDom_nodeIsCharacterData(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CommentNode);
}

/* 字符数据 API 仍可直接作用于 CDATA；Qt 的 isCharacterData 查询则排除 CDATA。 */
static bool XDom_nodeIsCharacterDataPayload(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode ||
                    node->m_type == XDom_CommentNode);
}

/* [死码清理] xxml_dom_context_for_node 已删除：全仓无调用点（见审计清单）。
 */

static int XDom_childIndex(const XDomNodePrivate* parent,
                                const XDomNodePrivate* child)
{
    if (!parent || !child) return -1;
    return XDom_arrayIndex(parent->m_children, parent->m_childCount, child);
}

static bool XDom_stringMatches(const XString* value, const XString* filter)
{
    if (!filter) return true;
    if (XDom_stringEqual_utf8(filter, "*")) return true;
    return XDom_stringEqual(value, filter);
}

static bool XDom_elementMatches(const XDomNodePrivate* node,
                                     const XString* tagName,
                                     const XString* namespaceURI)
{
    if (!node || node->m_type != XDom_ElementNode) return false;
    return XDom_stringMatches(node->m_name, tagName) &&
           XDom_stringMatches(node->m_namespaceUri, namespaceURI);
}

static void XDom_nodeInvalidateTextCache(XDomNodePrivate* node)
{
    for (XDomNodePrivate* current = node; current; current = current->m_parent)
        XDom_stringDelete(&current->m_textCache);
}

static bool XDom_appendFragment(XDomNodePrivate* parent,
                                     XDomNodePrivate* fragment, int index)
{
    if (!parent || !fragment || fragment->m_type != XDom_DocumentFragmentNode)
        return false;
    int insertIndex = index;
    while (fragment->m_childCount > 0) {
        XDomNodePrivate* child = fragment->m_children[0];
        if (!XDom_appendPrivate(parent, child, insertIndex)) return false;
        if (insertIndex >= 0) ++insertIndex;
    }
    return true;
}

static bool XDom_insertChild(XDomNodePrivate* parent,
                                  XDomNodePrivate* child, int index)
{
    if (!parent || !child) return false;
    if (child->m_type == XDom_DocumentFragmentNode)
        return XDom_appendFragment(parent, child, index);
    if (!XDom_appendPrivate(parent, child, index)) return false;
    XDom_nodeInvalidateTextCache(parent);
    return true;
}

static XDomNodePrivate* XDom_createNodeForDocument(XDomNodePrivate* document,
                                                              XDomNodeType type)
{
    return XDom_nodeNewInContext(document ? document->m_context : NULL, type);
}

static bool XDom_implementationIsValid(const XDomImplementation* self)
{
    return self && !XClassIsVtableNull((const XClass*)self) && !self->m_isNull;
}

/* 与 Qt fixedPubidLiteral/fixedSystemLiteral 对齐的 DTD 标识符清理。 */
static bool XDom_publicIdChar(XChar ch)
{
    if (ch == XChar_from(' ') || ch == XChar_from('\t') ||
        ch == XChar_from('\n') || ch == XChar_from('\r')) return true;
    if ((ch >= XChar_from('a') && ch <= XChar_from('z')) ||
        (ch >= XChar_from('A') && ch <= XChar_from('Z')) ||
        (ch >= XChar_from('0') && ch <= XChar_from('9'))) return true;
    return XStrchr("-'()+,./:=?;!*#@$_%", (int)ch) != NULL;
}

static XString* XDom_fixedPublicId(const XString* value, bool* ok)
{
    if (ok) *ok = false;
    XString* result = XString_create();
    if (!result) return NULL;
    if (g_xxml_dom_invalid_data_policy == XDom_AcceptInvalidChars) {
        if (value) XString_assign(result, value);
        if (ok) *ok = true;
        return result;
    }
    const XChar* chars = value ? XString_unicode(value) : NULL;
    size_t length = value ? XString_length_base(value) : 0;
    for (size_t i = 0; i < length; ++i) {
        if (XDom_publicIdChar(chars[i])) XString_append_char(result, chars[i]);
        else if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XClassDelete(result);
            return NULL;
        }
    }
    if (XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0) {
        if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XClassDelete(result);
            return NULL;
        }
        XString_replace_utf8(result, "'", "", XChar_CaseSensitive);
    }
    if (ok) *ok = true;
    return result;
}

static XString* XDom_fixedSystemId(const XString* value, bool* ok)
{
    if (ok) *ok = false;
    XString* result = value ? XString_create_copy(value) : XString_create();
    if (!result) return NULL;
    if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode &&
        XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0) {
        XClassDelete(result);
        return NULL;
    }
    if (g_xxml_dom_invalid_data_policy == XDom_DropInvalidChars &&
        XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0)
        XString_replace_utf8(result, "'", "", XChar_CaseSensitive);
    if (ok) *ok = true;
    return result;
}

static XString* XDom_serialize(XDomNodePrivate* node, int indent);
static XString* XDom_serializeForPolicy(XDomNodePrivate* node, int indent,
                                               XDomEncodingPolicy policy);

/* ==================== QDomNode ==================== */

XDomNode* XDomNode_insertBefore(XDomNode* self, const XDomNode* newChild,
                                      const XDomNode* refChild)
{
    XDomNodePrivate* parent = XDom_handleImpl(self);
    XDomNodePrivate* child = XDom_handleImpl(newChild);
    XDomNodePrivate* reference = XDom_handleImpl(refChild);
    /* Qt 兼容：空参考节点表示插入到第一个子节点之前。 */
    int index = reference ? XDom_childIndex(parent, reference) : 0;
    if (!parent || !child || child == reference || (reference && index < 0) ||
        !XDom_insertChild(parent, child, index))
        return XDom_wrapNode(NULL);
    return XDom_wrapNode(child);
}

XDomNode* XDomNode_insertAfter(XDomNode* self, const XDomNode* newChild,
                                     const XDomNode* refChild)
{
    XDomNodePrivate* parent = XDom_handleImpl(self);
    XDomNodePrivate* child = XDom_handleImpl(newChild);
    XDomNodePrivate* reference = XDom_handleImpl(refChild);
    int index = reference ? XDom_childIndex(parent, reference) : -1;
    if (reference && index >= 0) ++index;
    if (!parent || !child || child == reference || (reference && index < 0) ||
        !XDom_insertChild(parent, child, index))
        return XDom_wrapNode(NULL);
    return XDom_wrapNode(child);
}

XDomNode* XDomNode_replaceChild(XDomNode* self, const XDomNode* newChild,
                                      const XDomNode* oldChild)
{
    XDomNodePrivate* parent = XDom_handleImpl(self);
    XDomNodePrivate* child = XDom_handleImpl(newChild);
    XDomNodePrivate* old = XDom_handleImpl(oldChild);
    int index = XDom_childIndex(parent, old);
    if (!parent || !child || !old || child == old || index < 0)
        return XDom_wrapNode(NULL);

    XDomNode* result = XDom_wrapNode(old);
    if (!XDom_detachFromParent(old) || !XDom_insertChild(parent, child, index)) {
        if (!old->m_parent) XDom_insertChild(parent, old, index);
        XClassDelete(result);
        return XDom_wrapNode(NULL);
    }
    return result;
}

XDomNode* XDomNode_removeChild(XDomNode* self, const XDomNode* oldChild)
{
    XDomNodePrivate* parent = XDom_handleImpl(self);
    XDomNodePrivate* child = XDom_handleImpl(oldChild);
    if (!parent || !child || XDom_childIndex(parent, child) < 0)
        return XDom_wrapNode(NULL);
    XDomNode* result = XDom_wrapNode(child);
    if (!XDom_detachFromParent(child)) {
        XClassDelete(result);
        return XDom_wrapNode(NULL);
    }
    XDom_nodeInvalidateTextCache(parent);
    return result;
}

XDomNode* XDomNode_appendChild(XDomNode* self, const XDomNode* newChild)
{
    /* insertAfter(..., NULL) 的既有语义是追加到末尾。 */
    return XDomNode_insertAfter(self, newChild, NULL);
}

bool XDomNode_hasChildNodes(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_childCount > 0;
}

XDomNode* XDomNode_cloneNode(const XDomNode* self, bool deep)
{
    XDomNodePrivate* source = XDom_handleImpl(self);
    if (!source) return XDom_wrapNode(NULL);
    XDomNodePrivate* clone = XDom_nodeClone(source, source->m_context, deep);
    if (!clone) return XDom_wrapNode(NULL);
    /* 克隆节点仍归属原文档；文档自身的克隆则成为新树的归属文档。 */
    XDom_setOwnerDocument(clone,
                                source->m_type == XDom_DocumentNode ? clone :
                                XDom_documentForNode(source));
    XDomNode* result = XDom_wrapNode(clone);
    XDom_nodeRelease(clone);
    return result;
}

static void XDom_normalizePrivate(XDomNodePrivate* node)
{
    if (!node) return;
    /* 对齐 Qt 6.8 qNormalizeNode：只处理当前节点的直接子节点，不递归进入后代。 */
    XDomNodePrivate* text = NULL;
    for (int i = 0; i < node->m_childCount; ++i) {
        XDomNodePrivate* child = node->m_children[i];
        bool isText = child->m_type == XDom_TextNode ||
                      child->m_type == XDom_CDATASectionNode;
        if (!isText) {
            text = NULL;
            continue;
        }
        if (!text) {
            text = child;
            continue;
        }
        XString_append(text->m_value, child->m_value);
        XDom_arrayRemoveAt(node->m_children, &node->m_childCount, i);
        child->m_parent = NULL;
        XDom_nodeRelease(child);
        --i;
    }
    XDom_nodeInvalidateTextCache(node);
}

void XDomNode_normalize(XDomNode* self)
{
    XDom_normalizePrivate(XDom_handleImpl(self));
}

bool XDomNode_isSupported(const XDomNode* self, const XString* feature,
                             const XString* version)
{
    (void)self;
    return feature && XDom_stringEqual_utf8(feature, "XML") &&
           (!version || XString_isEmpty_base(version) ||
            XDom_stringEqual_utf8(version, "1.0"));
}

bool XDomNode_isSupported_utf8(const XDomNode* self, const char* feature,
                                  const char* version)
{
    XString* featureString = XString_create_utf8(feature);
    XString* versionString = XString_create_utf8(version);
    bool result = XDomNode_isSupported(self, featureString, versionString);
    XClassDelete(featureString);
    XClassDelete(versionString);
    return result;
}

const XString* XDomNode_nodeName(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_name ? node->m_name : XDom_emptyString();
}

XDomNodeType XDomNode_nodeType(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node ? node->m_type : XDom_BaseNode;
}

XDomNode* XDomNode_parentNode(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return XDom_wrapNode(node ? node->m_parent : NULL);
}

XDomNodeList* XDomNode_childNodes(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    XDomNodePrivate* list = XDom_listNewForOwner(node, XDom_ListChildren, NULL, NULL);
    XDomNodeList* result = XDom_wrapList(list);
    XDom_nodeRelease(list);
    return result;
}

XDomNode* XDomNode_firstChild(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return XDom_wrapNode(node && node->m_childCount ? node->m_children[0] : NULL);
}

XDomNode* XDomNode_lastChild(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return XDom_wrapNode(node && node->m_childCount ?
                              node->m_children[node->m_childCount - 1] : NULL);
}

XDomNode* XDomNode_previousSibling(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    int index = node ? XDom_childIndex(node->m_parent, node) : -1;
    return XDom_wrapNode(index > 0 ? node->m_parent->m_children[index - 1] : NULL);
}

XDomNode* XDomNode_nextSibling(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    int index = node ? XDom_childIndex(node->m_parent, node) : -1;
    return XDom_wrapNode(index >= 0 && index + 1 < node->m_parent->m_childCount ?
                              node->m_parent->m_children[index + 1] : NULL);
}

XDomNamedNodeMap* XDomNode_attributes(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    XDomNodePrivate* map = XDom_mapNew(node && node->m_type == XDom_ElementNode ?
                                                node : NULL, XDom_MapAttributes);
    XDomNamedNodeMap* result = XDom_wrapMap(map);
    XDom_nodeRelease(map);
    return result;
}

XDomDocument* XDomNode_ownerDocument(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return XDom_wrapDocument(node ? XDom_documentForNode(node) : NULL);
}

const XString* XDomNode_namespaceURI(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_createdWithNamespace && node->m_namespaceUri ?
        node->m_namespaceUri : XDom_emptyString();
}

const XString* XDomNode_localName(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_createdWithNamespace && node->m_localName ?
        node->m_localName : XDom_emptyString();
}

bool XDomNode_hasAttributes(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_attributeCount > 0;
}

const XString* XDomNode_nodeValue(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node || node->m_type == XDom_ElementNode || node->m_type == XDom_DocumentNode ||
        node->m_type == XDom_DocumentTypeNode || node->m_type == XDom_DocumentFragmentNode)
        return XDom_emptyString();
    return node->m_value ? node->m_value : XDom_emptyString();
}

void XDomNode_setNodeValue(XDomNode* self, const XString* value)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node || node->m_type == XDom_ElementNode || node->m_type == XDom_DocumentNode ||
        node->m_type == XDom_DocumentTypeNode || node->m_type == XDom_DocumentFragmentNode)
        return;
    if (node->m_type == XDom_AttributeNode) {
        XDom_attrSetValuePrivate(node, value);
        node->m_specified = true;
        return;
    }
    XDom_stringAssign(&node->m_value, value);
    XDom_nodeInvalidateTextCache(node);
}

void XDomNode_setNodeValue_utf8(XDomNode* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomNode_setNodeValue(self, text);
    XClassDelete(text);
}

const XString* XDomNode_prefix(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_createdWithNamespace && node->m_prefix ?
        node->m_prefix : XDom_emptyString();
}

void XDomNode_setPrefix(XDomNode* self, const XString* prefix)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node || !node->m_createdWithNamespace || !node->m_prefix ||
        (node->m_type != XDom_ElementNode && node->m_type != XDom_AttributeNode)) return;
    /* QDomNode::setPrefix 只修改 prefix，不重建 nodeName。 */
    XDom_stringAssign(&node->m_prefix, prefix);
}

void XDomNode_setPrefix_utf8(XDomNode* self, const char* prefix)
{
    XString* text = XString_create_utf8(prefix);
    XDomNode_setPrefix(self, text);
    XClassDelete(text);
}

XDomNode* XDomNode_namedItem(const XDomNode* self, const XString* name)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node) return XDom_wrapNode(NULL);
    for (int i = 0; i < node->m_childCount; ++i)
        if (XDom_stringEqual(node->m_children[i]->m_name, name))
            return XDom_wrapNode(node->m_children[i]);
    return XDom_wrapNode(NULL);
}

XDomNode* XDomNode_namedItem_utf8(const XDomNode* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNode_namedItem(self, text);
    XClassDelete(text);
    return result;
}

bool XDomNode_isNull(const XDomNode* self)
{
    return XDom_handleImpl(self) == NULL;
}

bool XDomNode_equals(const XDomNode* left, const XDomNode* right)
{
    return XDom_handleImpl(left) == XDom_handleImpl(right);
}

void XDomNode_clear(XDomNode* self)
{
    if (!self) return;
    /* Qt 兼容：clear 只清空当前隐式共享句柄，不能修改共享的树。 */
    XDom_handleDeinit(&self->m_impl);
}

#define XDOM_DEFINE_NODE_TYPE_CHECK(Name, Value) \
    bool XDomNode_##Name(const XDomNode* self) \
    { \
        XDomNodePrivate* node = XDom_handleImpl(self); \
        return node && node->m_type == (Value); \
    }

XDOM_DEFINE_NODE_TYPE_CHECK(isAttr, XDom_AttributeNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isCDATASection, XDom_CDATASectionNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isDocumentFragment, XDom_DocumentFragmentNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isDocument, XDom_DocumentNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isDocumentType, XDom_DocumentTypeNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isElement, XDom_ElementNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isEntityReference, XDom_EntityReferenceNode)
/* Qt 的 QDomNode::isText() 对普通 Text 和 CDATA 都返回 true。 */
bool XDomNode_isText(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode);
}
XDOM_DEFINE_NODE_TYPE_CHECK(isEntity, XDom_EntityNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isNotation, XDom_NotationNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isProcessingInstruction, XDom_ProcessingInstructionNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isComment, XDom_CommentNode)

bool XDomNode_isCharacterData(const XDomNode* self)
{
    return XDom_nodeIsCharacterData(XDom_handleImpl(self));
}

XDomElement* XDomNode_toElement(const XDomNode* self) { return XDom_wrapElement(XDom_handleImpl(self)); }
XDomAttr* XDomNode_toAttr(const XDomNode* self) { return XDom_wrapAttr(XDom_handleImpl(self)); }
XDomText* XDomNode_toText(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    XDomText* result = XDomText_create();
    if (result && node && (node->m_type == XDom_TextNode ||
                           node->m_type == XDom_CDATASectionNode))
        XDom_assignNewHandle(&result->m_impl, node);
    return result;
}
XDomCDATASection* XDomNode_toCDATASection(const XDomNode* self) { return XDom_wrapCdata(XDom_handleImpl(self)); }
XDomComment* XDomNode_toComment(const XDomNode* self) { return XDom_wrapComment(XDom_handleImpl(self)); }
XDomCharacterData* XDomNode_toCharacterData(const XDomNode* self) { return XDom_wrapCharacterData(XDom_handleImpl(self)); }
XDomDocument* XDomNode_toDocument(const XDomNode* self) { return XDom_wrapDocument(XDom_handleImpl(self)); }
XDomDocumentType* XDomNode_toDocumentType(const XDomNode* self) { return XDom_wrapDoctype(XDom_handleImpl(self)); }
XDomDocumentFragment* XDomNode_toDocumentFragment(const XDomNode* self) { return XDom_wrapFragment(XDom_handleImpl(self)); }
XDomEntityReference* XDomNode_toEntityReference(const XDomNode* self) { return XDom_wrapEntityReference(XDom_handleImpl(self)); }
XDomEntity* XDomNode_toEntity(const XDomNode* self) { return XDom_wrapEntity(XDom_handleImpl(self)); }
XDomNotation* XDomNode_toNotation(const XDomNode* self) { return XDom_wrapNotation(XDom_handleImpl(self)); }
XDomProcessingInstruction* XDomNode_toProcessingInstruction(const XDomNode* self) { return XDom_wrapPi(XDom_handleImpl(self)); }

static XDomElement* XDom_findElement(const XDomNodePrivate* start, int direction,
                                              const XString* tagName, const XString* namespaceURI)
{
    if (!start) return XDom_wrapElement(NULL);
    int index = direction > 0 ? 0 : start->m_childCount - 1;
    for (; index >= 0 && index < start->m_childCount; index += direction) {
        XDomNodePrivate* child = start->m_children[index];
        if (XDom_elementMatches(child, tagName, namespaceURI)) return XDom_wrapElement(child);
    }
    return XDom_wrapElement(NULL);
}

XDomElement* XDomNode_firstChildElement(const XDomNode* self, const XString* tagName,
                                              const XString* namespaceURI)
{
    return XDom_findElement(XDom_handleImpl(self), 1, tagName, namespaceURI);
}

XDomElement* XDomNode_firstChildElement_utf8(const XDomNode* self, const char* tagName,
                                                   const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_firstChildElement(self, tag, ns);
    XClassDelete(tag); XClassDelete(ns);
    return result;
}

XDomElement* XDomNode_lastChildElement(const XDomNode* self, const XString* tagName,
                                             const XString* namespaceURI)
{
    return XDom_findElement(XDom_handleImpl(self), -1, tagName, namespaceURI);
}

XDomElement* XDomNode_lastChildElement_utf8(const XDomNode* self, const char* tagName,
                                                  const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_lastChildElement(self, tag, ns);
    XClassDelete(tag); XClassDelete(ns);
    return result;
}

XDomElement* XDomNode_previousSiblingElement_utf8(const XDomNode* self,
                                                        const char* tagName,
                                                        const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_previousSiblingElement(self, tag, ns);
    XClassDelete(tag); XClassDelete(ns);
    return result;
}

static XDomElement* XDom_findSiblingElement(const XDomNodePrivate* node, int direction,
                                                      const XString* tagName, const XString* namespaceURI)
{
    if (!node || !node->m_parent) return XDom_wrapElement(NULL);
    int index = XDom_childIndex(node->m_parent, node) + direction;
    for (; index >= 0 && index < node->m_parent->m_childCount; index += direction) {
        XDomNodePrivate* sibling = node->m_parent->m_children[index];
        if (XDom_elementMatches(sibling, tagName, namespaceURI)) return XDom_wrapElement(sibling);
    }
    return XDom_wrapElement(NULL);
}

XDomElement* XDomNode_previousSiblingElement(const XDomNode* self, const XString* tagName,
                                                    const XString* namespaceURI)
{
    return XDom_findSiblingElement(XDom_handleImpl(self), -1, tagName, namespaceURI);
}

XDomElement* XDomNode_nextSiblingElement(const XDomNode* self, const XString* tagName,
                                                const XString* namespaceURI)
{
    return XDom_findSiblingElement(XDom_handleImpl(self), 1, tagName, namespaceURI);
}

XDomElement* XDomNode_nextSiblingElement_utf8(const XDomNode* self,
                                                    const char* tagName,
                                                    const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_nextSiblingElement(self, tag, ns);
    XClassDelete(tag); XClassDelete(ns);
    return result;
}

int64_t XDomNode_lineNumber(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node ? node->m_line : -1;
}

int64_t XDomNode_columnNumber(const XDomNode* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node ? node->m_column : -1;
}

XString* XDomNode_toString(const XDomNode* self, int indent)
{
    return XDom_serialize(XDom_handleImpl(self), indent);
}

bool XDomNode_save(const XDomNode* self, XIODevice* device, int indent,
                   XDomEncodingPolicy encodingPolicy)
{
    if (!self || !device || XDomNode_isNull(self)) return false;
    XString* text = XDom_serializeForPolicy(XDom_handleImpl(self), indent,
                                                   encodingPolicy);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;
    size_t length = text ? XString_toUtf8_length(text) : 0;
    int64_t written = (utf8 || length == 0) ? XIODevice_write_1(
        device, utf8 ? utf8 : "", (int64_t)length) : -1;
    XClassDelete(text);
    return written == (int64_t)length;
}

/* ==================== QDomNodeList ==================== */

static bool XDom_listMatches(const XDomNodePrivate* list,
                                  const XDomNodePrivate* node)
{
    return list && node && XDom_elementMatches(node, list->m_listTagName,
                                                     list->m_listNamespaceUri);
}

static int XDom_listCountDescendants(const XDomNodePrivate* list,
                                           const XDomNodePrivate* parent)
{
    if (!list || !parent) return 0;
    int count = 0;
    for (int i = 0; i < parent->m_childCount; ++i) {
        const XDomNodePrivate* child = parent->m_children[i];
        if (XDom_listMatches(list, child) && count < INT_MAX) ++count;
        int descendants = XDom_listCountDescendants(list, child);
        if (descendants > INT_MAX - count) count = INT_MAX;
        else count += descendants;
    }
    return count;
}

static XDomNodePrivate* XDom_listAtDescendant(const XDomNodePrivate* list,
                                                    const XDomNodePrivate* parent,
                                                    int* index)
{
    if (!list || !parent || !index || *index < 0) return NULL;
    for (int i = 0; i < parent->m_childCount; ++i) {
        XDomNodePrivate* child = parent->m_children[i];
        if (XDom_listMatches(list, child)) {
            if (*index == 0) return child;
            --*index;
        }
        XDomNodePrivate* result = XDom_listAtDescendant(list, child, index);
        if (result) return result;
    }
    return NULL;
}

XDomNode* XDomNodeList_item(const XDomNodeList* self, int index)
{
    XDomNodePrivate* list = XDom_handleImpl(self);
    if (!list || list->m_collectionKind != XDom_CollectionList || index < 0)
        return XDom_wrapNode(NULL);
    if (list->m_listQueryKind == XDom_ListChildren)
        return XDom_wrapNode(list->m_listOwner && index < list->m_listOwner->m_childCount ?
                                  list->m_listOwner->m_children[index] : NULL);
    if (list->m_listQueryKind == XDom_ListElements) {
        XDomNodePrivate* result = XDom_listAtDescendant(list, list->m_listOwner, &index);
        return XDom_wrapNode(result);
    }
    return XDom_wrapNode(NULL);
}

XDomNode* XDomNodeList_at(const XDomNodeList* self, int index)
{
    return XDomNodeList_item(self, index);
}

int XDomNodeList_length(const XDomNodeList* self)
{
    XDomNodePrivate* list = XDom_handleImpl(self);
    if (!list || list->m_collectionKind != XDom_CollectionList || !list->m_listOwner) return 0;
    if (list->m_listQueryKind == XDom_ListChildren) return list->m_listOwner->m_childCount;
    return list->m_listQueryKind == XDom_ListElements ?
        XDom_listCountDescendants(list, list->m_listOwner) : 0;
}

int XDomNodeList_count(const XDomNodeList* self) { return XDomNodeList_length(self); }
int XDomNodeList_size(const XDomNodeList* self) { return XDomNodeList_length(self); }
bool XDomNodeList_isEmpty(const XDomNodeList* self) { return XDomNodeList_length(self) == 0; }
bool XDomNodeList_equals(const XDomNodeList* left, const XDomNodeList* right)
{
    XDomNodePrivate* leftList = XDom_handleImpl(left);
    XDomNodePrivate* rightList = XDom_handleImpl(right);
    if (leftList == rightList) return true;
    if (!leftList || !rightList ||
        leftList->m_collectionKind != XDom_CollectionList ||
        rightList->m_collectionKind != XDom_CollectionList)
        return false;
    /* Qt 的 QDomNodeList::operator== 不比较 namespaceURI，只比较节点和 tagName。 */
    return leftList->m_listOwner == rightList->m_listOwner &&
           leftList->m_listQueryKind == rightList->m_listQueryKind &&
           XDom_stringEqual(leftList->m_listTagName, rightList->m_listTagName);
}

/* ==================== QDomNamedNodeMap ==================== */

XDomNode* XDomNamedNodeMap_namedItem(const XDomNamedNodeMap* self, const XString* name)
{
    XDomNodePrivate* map = XDom_handleImpl(self);
    int index = XDom_mapFindName(map, name);
    return XDom_wrapNode(index >= 0 ? XDom_mapAt(map, index) : NULL);
}

XDomNode* XDomNamedNodeMap_namedItem_utf8(const XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNamedNodeMap_namedItem(self, text);
    XClassDelete(text);
    return result;
}

static XDomNodePrivate** XDom_mapStorage(XDomNodePrivate* map, int** count,
                                                  int** capacity)
{
    if (!map || map->m_collectionKind != XDom_CollectionMap || !map->m_mapOwner) return NULL;
    switch (map->m_mapKind) {
        case XDom_MapAttributes:
            if (count) *count = &map->m_mapOwner->m_attributeCount;
            if (capacity) *capacity = &map->m_mapOwner->m_attributeCapacity;
            return map->m_mapOwner->m_attributes;
        case XDom_MapEntities:
            if (count) *count = &map->m_mapOwner->m_entityCount;
            if (capacity) *capacity = &map->m_mapOwner->m_entityCapacity;
            return map->m_mapOwner->m_entities;
        case XDom_MapNotations:
            if (count) *count = &map->m_mapOwner->m_notationCount;
            if (capacity) *capacity = &map->m_mapOwner->m_notationCapacity;
            return map->m_mapOwner->m_notations;
        default:
            return NULL;
    }
}

static XDomNode* XDom_mapSetItem(XDomNamedNodeMap* self,
                                          const XDomNode* newNode, bool byNamespace)
{
    XDomNodePrivate* map = XDom_handleImpl(self);
    XDomNodePrivate* node = XDom_handleImpl(newNode);
    /* Qt 的实体/notation 映射是只读；属性映射由 QDomElement 专门维护 parent。 */
    if (!map || !node || map->m_mapKind != XDom_MapAttributes ||
        node->m_type != XDom_AttributeNode) return XDom_wrapNode(NULL);
    if (node->m_parent && node->m_parent != map->m_mapOwner)
        return XDom_wrapNode(NULL);
    int index = byNamespace ? XDom_mapFindNs(map, node->m_namespaceUri, node->m_localName) :
                              XDom_mapFindName(map, node->m_name);
    XDomNode* replaced = XDom_wrapNode(index >= 0 ? XDom_mapAt(map, index) : NULL);
    int* count = NULL;
    int* capacity = NULL;
    XDomNodePrivate** storage = XDom_mapStorage(map, &count, &capacity);
    if (!storage || !count || !capacity) return replaced;
    if (index >= 0) {
        XDomNodePrivate* old = storage[index];
        storage[index] = node;
        node->m_parent = map->m_mapOwner;
        XDom_setOwnerDocument(node, XDom_documentForNode(map->m_mapOwner));
        XDom_nodeRetain(node);
        old->m_parent = NULL;
        XDom_nodeRelease(old);
        return replaced;
    }
    XDomNodePrivate*** array = NULL;
    if (map->m_mapKind == XDom_MapAttributes) array = &map->m_mapOwner->m_attributes;
    else if (map->m_mapKind == XDom_MapEntities) array = &map->m_mapOwner->m_entities;
    else array = &map->m_mapOwner->m_notations;
    if (XDom_arrayAppend(array, count, capacity, node)) {
        node->m_parent = map->m_mapOwner;
        XDom_setOwnerDocument(node, XDom_documentForNode(map->m_mapOwner));
        XDom_nodeRetain(node);
    }
    return replaced;
}

XDomNode* XDomNamedNodeMap_setNamedItem(XDomNamedNodeMap* self, const XDomNode* newNode)
{
    return XDom_mapSetItem(self, newNode, false);
}

XDomNode* XDomNamedNodeMap_setNamedItemNS(XDomNamedNodeMap* self, const XDomNode* newNode)
{
    return XDom_mapSetItem(self, newNode, true);
}

XDomNode* XDomNamedNodeMap_removeNamedItem(XDomNamedNodeMap* self, const XString* name)
{
    XDomNodePrivate* map = XDom_handleImpl(self);
    if (!map || map->m_mapKind != XDom_MapAttributes)
        return XDom_wrapNode(NULL);
    int index = XDom_mapFindName(map, name);
    if (index < 0) return XDom_wrapNode(NULL);
    XDomNodePrivate* node = XDom_mapAt(map, index);
    XDomNode* result = XDom_wrapNode(node);
    int* count = NULL; XDomNodePrivate** storage = XDom_mapStorage(map, &count, NULL);
    XDom_arrayRemoveAt(storage, count, index);
    node->m_parent = NULL;
    node->m_specified = true;
    XDom_nodeRelease(node);
    return result;
}

XDomNode* XDomNamedNodeMap_removeNamedItem_utf8(XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNamedNodeMap_removeNamedItem(self, text);
    XClassDelete(text);
    return result;
}

XDomNode* XDomNamedNodeMap_item(const XDomNamedNodeMap* self, int index)
{
    return XDom_wrapNode(XDom_mapAt(XDom_handleImpl(self), index));
}

XDomNode* XDomNamedNodeMap_namedItemNS(const XDomNamedNodeMap* self,
                                              const XString* namespaceURI, const XString* localName)
{
    XDomNodePrivate* map = XDom_handleImpl(self);
    int index = XDom_mapFindNs(map, namespaceURI, localName);
    return XDom_wrapNode(index >= 0 ? XDom_mapAt(map, index) : NULL);
}

XDomNode* XDomNamedNodeMap_namedItemNS_utf8(const XDomNamedNodeMap* self,
                                                   const char* namespaceURI, const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomNode* result = XDomNamedNodeMap_namedItemNS(self, ns, local);
    XClassDelete(ns); XClassDelete(local);
    return result;
}

XDomNode* XDomNamedNodeMap_removeNamedItemNS(XDomNamedNodeMap* self,
                                                    const XString* namespaceURI, const XString* localName)
{
    XDomNodePrivate* map = XDom_handleImpl(self);
    if (!map || map->m_mapKind != XDom_MapAttributes)
        return XDom_wrapNode(NULL);
    int index = XDom_mapFindNs(map, namespaceURI, localName);
    if (index < 0) return XDom_wrapNode(NULL);
    XDomNodePrivate* node = XDom_mapAt(map, index);
    XDomNode* result = XDom_wrapNode(node);
    int* count = NULL; XDomNodePrivate** storage = XDom_mapStorage(map, &count, NULL);
    XDom_arrayRemoveAt(storage, count, index);
    node->m_parent = NULL;
    node->m_specified = true;
    XDom_nodeRelease(node);
    return result;
}

int XDomNamedNodeMap_length(const XDomNamedNodeMap* self)
{
    int count = 0;
    XDom_mapItems(XDom_handleImpl(self), &count);
    return count;
}

int XDomNamedNodeMap_count(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self); }
int XDomNamedNodeMap_size(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self); }
bool XDomNamedNodeMap_isEmpty(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self) == 0; }
bool XDomNamedNodeMap_equals(const XDomNamedNodeMap* left, const XDomNamedNodeMap* right)
{
    XDomNodePrivate* leftMap = XDom_handleImpl(left);
    XDomNodePrivate* rightMap = XDom_handleImpl(right);
    if (leftMap == rightMap) return true;
    if (!leftMap || !rightMap ||
        leftMap->m_collectionKind != XDom_CollectionMap ||
        rightMap->m_collectionKind != XDom_CollectionMap)
        return false;
    return leftMap->m_mapOwner == rightMap->m_mapOwner &&
           leftMap->m_mapKind == rightMap->m_mapKind;
}
bool XDomNamedNodeMap_contains(const XDomNamedNodeMap* self, const XString* name)
{ return XDom_mapFindName(XDom_handleImpl(self), name) >= 0; }
bool XDomNamedNodeMap_contains_utf8(const XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    bool result = XDom_mapFindName(XDom_handleImpl(self), text) >= 0;
    XClassDelete(text);
    return result;
}

/* ==================== QDomDocument 与节点工厂 ==================== */

static XDomNodePrivate* XDom_documentImpl(const XDomDocument* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_DocumentNode ? node : NULL;
}

static bool XDom_documentCreateDefaultDoctype(XDomNodePrivate* document)
{
    if (!document || document->m_type != XDom_DocumentNode) return false;
    if (document->m_doctype) return true;
    XDomNodePrivate* doctype = XDom_nodeNewInContext(document->m_context,
                                                             XDom_DocumentTypeNode);
    if (!doctype) return false;
    doctype->m_parent = document;
    doctype->m_ownerDocument = document;
    document->m_doctype = doctype;
    return true;
}

static XDomNodePrivate* XDom_documentEnsureImpl(XDomDocument* self)
{
    if (!self || XClassIsVtableNull((XClass*)self)) return NULL;
    XDomNodePrivate* document = XDom_documentImpl(self);
    if (document) return document;
    document = XDom_nodeNew(XDom_DocumentNode);
    if (!document) return NULL;
    self->m_impl = document;
    if (!XDom_documentCreateDefaultDoctype(document)) {
        XDom_handleDeinit(&self->m_impl);
        return NULL;
    }
    return document;
}

static XDomElement* XDom_documentNewElement(XDomNodePrivate* document,
                                                      const XString* namespaceURI,
                                                      const XString* qualifiedName)
{
    bool ok = false;
    XString* fixedName = XDom_fixedName(qualifiedName, namespaceURI != NULL, &ok);
    if (!document || !ok) {
        XClassDelete(fixedName);
        return XDom_wrapElement(NULL);
    }
    XDomNodePrivate* node = XDom_createNodeForDocument(document, XDom_ElementNode);
    if (!node) {
        XClassDelete(fixedName);
        return XDom_wrapElement(NULL);
    }
    if (namespaceURI) XDom_setQualifiedName(node, namespaceURI, fixedName);
    else XDom_setPlainName(node, fixedName);
    node->m_ownerDocument = document;
    XDomElement* result = XDom_wrapElement(node);
    XDom_nodeRelease(node);
    XClassDelete(fixedName);
    return result;
}

static XDomAttr* XDom_documentNewAttribute(XDomNodePrivate* document,
                                                     const XString* namespaceURI,
                                                     const XString* qualifiedName)
{
    bool ok = false;
    XString* fixedName = XDom_fixedName(qualifiedName, namespaceURI != NULL, &ok);
    if (!document || !ok) {
        XClassDelete(fixedName);
        return XDom_wrapAttr(NULL);
    }
    XDomNodePrivate* node = XDom_createNodeForDocument(document, XDom_AttributeNode);
    if (!node) {
        XClassDelete(fixedName);
        return XDom_wrapAttr(NULL);
    }
    if (namespaceURI) XDom_setQualifiedName(node, namespaceURI, fixedName);
    else XDom_setPlainName(node, fixedName);
    node->m_ownerDocument = document;
    XDomAttr* result = XDom_wrapAttr(node);
    XDom_nodeRelease(node);
    XClassDelete(fixedName);
    return result;
}

static XDomNodePrivate* XDom_documentNewValueNode(XDomNodePrivate* document,
                                                             XDomNodeType type,
                                                             const XString* value)
{
    if (!document) return NULL;
    bool ok = false;
    XString* fixedValue = XDom_fixedValue(value, type, &ok);
    if (!ok) {
        XClassDelete(fixedValue);
        return NULL;
    }
    XDomNodePrivate* node = XDom_createNodeForDocument(document, type);
    if (!node) {
        XClassDelete(fixedValue);
        return NULL;
    }
    XDom_setDefaultCreationValue(node, fixedValue);
    node->m_ownerDocument = document;
    XClassDelete(fixedValue);
    return node;
}

XDomDocument* XDomDocument_createName(const XString* name)
{
    XDomDocument* document = XDomDocument_create();
    XDomNodePrivate* impl = XDom_documentEnsureImpl(document);
    if (!document || !impl) return document;
    XDom_setPlainName(impl->m_doctype, name);
    return document;
}

XDomDocument* XDomDocument_createName_utf8(const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomDocument* result = XDomDocument_createName(text);
    XClassDelete(text);
    return result;
}

XDomDocument* XDomDocument_createDoctype(const XDomDocumentType* doctype)
{
    XDomDocument* result = XDomDocument_create();
    XDomNodePrivate* document = XDom_documentEnsureImpl(result);
    XDomNodePrivate* source = XDom_handleImpl(doctype);
    if (!document) return result;
    if (source && source->m_type == XDom_DocumentTypeNode) {
        XDomNodePrivate* clone = XDom_nodeClone(source, document->m_context, true);
        if (clone) {
            if (document->m_doctype) {
                XDom_nodeRelease(document->m_doctype);
                document->m_doctype = NULL;
            }
            if (!XDom_appendPrivate(document, clone, -1))
                XDom_nodeRelease(clone);
            else
                XDom_nodeRelease(clone);
        }
    }
    return result;
}

XDomDocumentType* XDomDocument_doctype(const XDomDocument* self)
{
    XDomNodePrivate* document = XDom_documentImpl(self);
    return XDom_wrapDoctype(document ? document->m_doctype : NULL);
}

XDomImplementation* XDomDocument_implementation(const XDomDocument* self)
{
    XDomImplementation* result = XDomImplementation_create();
    XDomNodePrivate* document = XDom_documentImpl(self);
    if (!document && result) result->m_isNull = true;
    if (result && document) {
        result->m_isNull = false;
        result->m_document = document;
        XDom_nodeRetain(document);
    }
    return result;
}

XDomElement* XDomDocument_documentElement(const XDomDocument* self)
{
    XDomNodePrivate* document = XDom_documentImpl(self);
    if (document) {
        for (int i = 0; i < document->m_childCount; ++i)
            if (document->m_children[i]->m_type == XDom_ElementNode)
                return XDom_wrapElement(document->m_children[i]);
    }
    return XDom_wrapElement(NULL);
}

XDomElement* XDomDocument_createElement(XDomDocument* self, const XString* tagName)
{
    return XDom_documentNewElement(XDom_documentEnsureImpl(self), NULL, tagName);
}

XDomElement* XDomDocument_createElement_utf8(XDomDocument* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomElement* result = XDomDocument_createElement(self, text);
    XClassDelete(text);
    return result;
}

XDomDocumentFragment* XDomDocument_createDocumentFragment(XDomDocument* self)
{
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    XDomNodePrivate* node = XDom_documentNewValueNode(document,
                                                                 XDom_DocumentFragmentNode, NULL);
    XDomDocumentFragment* result = XDom_wrapFragment(node);
    XDom_nodeRelease(node);
    return result;
}

XDomText* XDomDocument_createTextNode(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = XDom_documentNewValueNode(XDom_documentEnsureImpl(self),
                                                                 XDom_TextNode, data);
    XDomText* result = XDom_wrapText(node);
    XDom_nodeRelease(node);
    return result;
}

XDomText* XDomDocument_createTextNode_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomText* result = XDomDocument_createTextNode(self, text);
    XClassDelete(text);
    return result;
}

XDomComment* XDomDocument_createComment(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = XDom_documentNewValueNode(XDom_documentEnsureImpl(self),
                                                                 XDom_CommentNode, data);
    XDomComment* result = XDom_wrapComment(node);
    XDom_nodeRelease(node);
    return result;
}

XDomComment* XDomDocument_createComment_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomComment* result = XDomDocument_createComment(self, text);
    XClassDelete(text);
    return result;
}

XDomCDATASection* XDomDocument_createCDATASection(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = XDom_documentNewValueNode(XDom_documentEnsureImpl(self),
                                                                 XDom_CDATASectionNode, data);
    XDomCDATASection* result = XDom_wrapCdata(node);
    XDom_nodeRelease(node);
    return result;
}

XDomCDATASection* XDomDocument_createCDATASection_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomCDATASection* result = XDomDocument_createCDATASection(self, text);
    XClassDelete(text);
    return result;
}

XDomProcessingInstruction* XDomDocument_createProcessingInstruction(XDomDocument* self,
                                                                            const XString* target,
                                                                            const XString* data)
{
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    bool ok = false;
    XString* fixedTarget = XDom_fixedName(target, false, &ok);
    if (!document || !ok) {
        XClassDelete(fixedTarget);
        return XDom_wrapPi(NULL);
    }
    XDomNodePrivate* node = XDom_documentNewValueNode(document,
                                                                 XDom_ProcessingInstructionNode, data);
    if (!node) {
        XClassDelete(fixedTarget);
        return XDom_wrapPi(NULL);
    }
    XDom_stringAssign(&node->m_name, fixedTarget);
    XDomProcessingInstruction* result = XDom_wrapPi(node);
    XDom_nodeRelease(node);
    XClassDelete(fixedTarget);
    return result;
}

XDomProcessingInstruction* XDomDocument_createProcessingInstruction_utf8(XDomDocument* self,
                                                                                 const char* target,
                                                                                 const char* data)
{
    XString* targetString = XString_create_utf8(target);
    XString* dataString = XString_create_utf8(data);
    XDomProcessingInstruction* result = XDomDocument_createProcessingInstruction(self, targetString, dataString);
    XClassDelete(targetString); XClassDelete(dataString);
    return result;
}

XDomAttr* XDomDocument_createAttribute(XDomDocument* self, const XString* name)
{
    return XDom_documentNewAttribute(XDom_documentEnsureImpl(self), NULL, name);
}

XDomAttr* XDomDocument_createAttribute_utf8(XDomDocument* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomAttr* result = XDomDocument_createAttribute(self, text);
    XClassDelete(text);
    return result;
}

XDomEntityReference* XDomDocument_createEntityReference(XDomDocument* self, const XString* name)
{
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    bool ok = false;
    XString* fixedName = XDom_fixedName(name, false, &ok);
    if (!document || !ok) {
        XClassDelete(fixedName);
        return XDom_wrapEntityReference(NULL);
    }
    XDomNodePrivate* node = XDom_documentNewValueNode(document,
                                                                 XDom_EntityReferenceNode, NULL);
    if (!node) {
        XClassDelete(fixedName);
        return XDom_wrapEntityReference(NULL);
    }
    XDom_stringAssign(&node->m_name, fixedName);
    XDomEntityReference* result = XDom_wrapEntityReference(node);
    XDom_nodeRelease(node);
    XClassDelete(fixedName);
    return result;
}

XDomEntityReference* XDomDocument_createEntityReference_utf8(XDomDocument* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomEntityReference* result = XDomDocument_createEntityReference(self, text);
    XClassDelete(text);
    return result;
}

static XDomNodeList* XDom_elementsByName(XDomNodePrivate* parent, const XString* tagName,
                                                   const XString* namespaceURI)
{
    XDomNodePrivate* list = XDom_listNewForOwner(parent, XDom_ListElements,
                                                        tagName, namespaceURI);
    XDomNodeList* result = XDom_wrapList(list);
    XDom_nodeRelease(list);
    return result;
}

XDomNodeList* XDomDocument_elementsByTagName(const XDomDocument* self, const XString* tagName)
{
    return XDom_elementsByName(XDom_documentImpl(self), tagName, NULL);
}

XDomNodeList* XDomDocument_elementsByTagName_utf8(const XDomDocument* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomNodeList* result = XDomDocument_elementsByTagName(self, text);
    XClassDelete(text);
    return result;
}

XDomNode* XDomDocument_importNode(XDomDocument* self, const XDomNode* importedNode, bool deep)
{
    XDomNodePrivate* source = XDom_handleImpl(importedNode);
    if (!source) return XDom_wrapNode(NULL);
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    if (!document) return XDom_wrapNode(NULL);
    /* QDomDocument 和 QDomDocumentType 不允许通过 importNode 导入。 */
    if (source->m_type == XDom_DocumentNode ||
        source->m_type == XDom_DocumentTypeNode)
        return XDom_wrapNode(NULL);
    /* 实体引用的后代永不导入，deep 对其没有影响。属性始终导入后代。 */
    bool copyDeep = source->m_type == XDom_AttributeNode ||
                    (source->m_type != XDom_EntityReferenceNode && deep);
    XDomNodePrivate* copy = XDom_nodeClone(source, document->m_context, copyDeep);
    if (!copy) return XDom_wrapNode(NULL);
    XDom_setOwnerDocument(copy, document);
    XDomNode* result = XDom_wrapNode(copy);
    XDom_nodeRelease(copy);
    return result;
}

XDomElement* XDomDocument_createElementNS(XDomDocument* self, const XString* namespaceURI,
                                                const XString* qualifiedName)
{
    return XDom_documentNewElement(XDom_documentEnsureImpl(self), namespaceURI, qualifiedName);
}

XDomElement* XDomDocument_createElementNS_utf8(XDomDocument* self, const char* namespaceURI,
                                                     const char* qualifiedName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XDomElement* result = XDomDocument_createElementNS(self, ns, name);
    XClassDelete(ns); XClassDelete(name);
    return result;
}

XDomAttr* XDomDocument_createAttributeNS(XDomDocument* self, const XString* namespaceURI,
                                               const XString* qualifiedName)
{
    return XDom_documentNewAttribute(XDom_documentEnsureImpl(self), namespaceURI, qualifiedName);
}

XDomAttr* XDomDocument_createAttributeNS_utf8(XDomDocument* self, const char* namespaceURI,
                                                    const char* qualifiedName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XDomAttr* result = XDomDocument_createAttributeNS(self, ns, name);
    XClassDelete(ns); XClassDelete(name);
    return result;
}

XDomNodeList* XDomDocument_elementsByTagNameNS(const XDomDocument* self, const XString* namespaceURI,
                                                     const XString* localName)
{
    return XDom_elementsByName(XDom_documentImpl(self), localName, namespaceURI);
}

XDomNodeList* XDomDocument_elementsByTagNameNS_utf8(const XDomDocument* self, const char* namespaceURI,
                                                          const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomNodeList* result = XDomDocument_elementsByTagNameNS(self, ns, local);
    XClassDelete(ns); XClassDelete(local);
    return result;
}

XDomElement* XDomDocument_elementById(const XDomDocument* self, const XString* id)
{
    (void)self;
    (void)id;
    /* Qt 6 的 QDomDocument 目前也没有基于 DTD/XML Schema 自动识别 ID 属性。 */
    return XDom_wrapElement(NULL);
}

XDomElement* XDomDocument_elementById_utf8(const XDomDocument* self, const char* id)
{
    XString* text = XString_create_utf8(id);
    XDomElement* result = XDomDocument_elementById(self, text);
    XClassDelete(text);
    return result;
}

/* ==================== QDomElement 与 QDomAttr ==================== */

static XDomNodePrivate* XDom_elementImpl(const XDomElement* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_ElementNode ? node : NULL;
}

static XDomNodePrivate* XDom_attrImpl(const XDomAttr* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_AttributeNode ? node : NULL;
}

static int XDom_attributeIndex(const XDomNodePrivate* element, const XString* name)
{
    if (!element || element->m_type != XDom_ElementNode) return -1;
    for (int i = 0; i < element->m_attributeCount; ++i)
        if (XDom_stringEqual(element->m_attributes[i]->m_name, name)) return i;
    return -1;
}

static int XDom_attributeNsIndex(const XDomNodePrivate* element,
                                       const XString* namespaceURI, const XString* localName)
{
    if (!element || element->m_type != XDom_ElementNode) return -1;
    for (int i = 0; i < element->m_attributeCount; ++i) {
        XDomNodePrivate* attr = element->m_attributes[i];
        if (XDom_stringEqual(attr->m_namespaceUri, namespaceURI) &&
            XDom_stringEqual(attr->m_localName, localName)) return i;
    }
    return -1;
}

/* 挂接属性前保留一个临时引用，避免从唯一父节点移除时提前释放属性。 */
static bool XDom_attachAttribute(XDomNodePrivate* element, XDomNodePrivate* attr,
                                      bool byNamespace)
{
    if (!element || !attr || element->m_type != XDom_ElementNode ||
        attr->m_type != XDom_AttributeNode) return false;
    XDom_nodeRetain(attr);
    XDom_detachFromParent(attr);
    int index = byNamespace ? XDom_attributeNsIndex(element, attr->m_namespaceUri, attr->m_localName) :
                              XDom_attributeIndex(element, attr->m_name);
    if (index >= 0) {
        XDomNodePrivate* old = element->m_attributes[index];
        element->m_attributes[index] = attr;
        attr->m_parent = element;
        XDom_setOwnerDocument(attr, XDom_documentForNode(element));
        old->m_parent = NULL;
        old->m_specified = true;
        XDom_nodeRelease(old);
    } else if (!XDom_arrayAppend(&element->m_attributes, &element->m_attributeCount,
                                      &element->m_attributeCapacity, attr)) {
        XDom_nodeRelease(attr);
        return false;
    } else {
        attr->m_parent = element;
        XDom_setOwnerDocument(attr, XDom_documentForNode(element));
    }
    XDom_nodeInvalidateTextCache(element);
    return true;
}

const XString* XDomElement_tagName(const XDomElement* self)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    return element ? element->m_name : XDom_emptyString();
}

void XDomElement_setTagName(XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    /* Qt 6.8 直接修改 name，不重新执行 createElement 的名称校验。 */
    if (!element) return;
    XDom_stringAssign(&element->m_name, name);
}

void XDomElement_setTagName_utf8(XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomElement_setTagName(self, text);
    XClassDelete(text);
}

const XString* XDomElement_attribute(const XDomElement* self, const XString* name,
                                        const XString* defaultValue)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeIndex(element, name);
    return index >= 0 ? element->m_attributes[index]->m_value :
                        (defaultValue ? defaultValue : XDom_emptyString());
}

const XString* XDomElement_attribute_utf8(const XDomElement* self, const char* name,
                                             const char* defaultValue)
{
    XString* nameString = XString_create_utf8(name);
    XString* defaultString = XString_create_utf8(defaultValue);
    const XString* result = XDomElement_attribute(self, nameString, defaultString);
    XDomNodePrivate* element = XDom_elementImpl(self);
    /* UTF-8 包装函数的临时参数不能直接返回，未命中时复制到元素缓存。 */
    const XString* stable = result;
    if (result == defaultString) {
        if (element) {
            if (!element->m_attributeCache) element->m_attributeCache = XString_create();
            if (element->m_attributeCache) {
                XString_assign(element->m_attributeCache, defaultString);
                stable = element->m_attributeCache;
            }
        }
        if (stable == defaultString || !stable) stable = XDom_emptyString();
    }
    XClassDelete(nameString); XClassDelete(defaultString);
    return stable;
}

static void XDom_elementSetAttribute(XDomElement* self, const XString* namespaceURI,
                                           const XString* qualifiedName, const XString* value,
                                           bool byNamespace)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    bool ok = false;
    XString* fixedName = XDom_fixedName(qualifiedName, byNamespace, &ok);
    if (!element || !ok) {
        XClassDelete(fixedName);
        return;
    }
    XDomNodePrivate* document = XDom_documentForNode(element);
    XDomNodePrivate* attr = XDom_createNodeForDocument(document ? document : element,
                                                                  XDom_AttributeNode);
    if (!attr) {
        XClassDelete(fixedName);
        return;
    }
    if (byNamespace) XDom_setQualifiedName(attr, namespaceURI, fixedName);
    else XDom_setPlainName(attr, fixedName);
    XDom_attrSetValuePrivate(attr, value);
    attr->m_specified = true;
    XDom_attachAttribute(element, attr, byNamespace);
    XDom_nodeRelease(attr);
    XClassDelete(fixedName);
}

void XDomElement_setAttribute(XDomElement* self, const XString* name, const XString* value)
{
    XDom_elementSetAttribute(self, NULL, name, value, false);
}

void XDomElement_setAttribute_utf8(XDomElement* self, const char* name, const char* value)
{
    XString* nameString = XString_create_utf8(name);
    XString* valueString = XString_create_utf8(value);
    XDomElement_setAttribute(self, nameString, valueString);
    XClassDelete(nameString); XClassDelete(valueString);
}

static void XDom_elementSetAttributeText(XDomElement* self, const XString* name, const char* text)
{
    XString* value = XString_create_utf8(text);
    XDomElement_setAttribute(self, name, value);
    XClassDelete(value);
}

void XDomElement_setAttribute_int(XDomElement* self, const XString* name, int value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%d", value);   /* 数字转字符串经库内 XSnprintf（外部依赖约束） */
    XDom_elementSetAttributeText(self, name, buffer);
}
void XDomElement_setAttribute_uint(XDomElement* self, const XString* name, unsigned int value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%u", value);
    XDom_elementSetAttributeText(self, name, buffer);
}
void XDomElement_setAttribute_int64(XDomElement* self, const XString* name, int64_t value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    XDom_elementSetAttributeText(self, name, buffer);
}
void XDomElement_setAttribute_uint64(XDomElement* self, const XString* name, uint64_t value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    XDom_elementSetAttributeText(self, name, buffer);
}
void XDomElement_setAttribute_double(XDomElement* self, const XString* name, double value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%.17g", value);
    XDom_elementSetAttributeText(self, name, buffer);
}
void XDomElement_setAttribute_float(XDomElement* self, const XString* name, float value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%.8g", (double)value);
    XDom_elementSetAttributeText(self, name, buffer);
}

void XDomElement_removeAttribute(XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeIndex(element, name);
    if (index < 0) return;
    XDomNodePrivate* attr = element->m_attributes[index];
    XDom_arrayRemoveAt(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    XDom_nodeRelease(attr);
}

void XDomElement_removeAttribute_utf8(XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomElement_removeAttribute(self, text);
    XClassDelete(text);
}

XDomAttr* XDomElement_attributeNode(const XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeIndex(element, name);
    return XDom_wrapAttr(index >= 0 ? element->m_attributes[index] : NULL);
}

XDomAttr* XDomElement_attributeNode_utf8(const XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomAttr* result = XDomElement_attributeNode(self, text);
    XClassDelete(text);
    return result;
}

static XDomAttr* XDom_elementSetAttributeNode(XDomElement* self,
                                                         const XDomAttr* newAttr, bool byNamespace)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    XDomNodePrivate* attr = XDom_attrImpl(newAttr);
    if (!element || !attr) return XDom_wrapAttr(NULL);
    int index = byNamespace ? XDom_attributeNsIndex(element, attr->m_namespaceUri, attr->m_localName) :
                              XDom_attributeIndex(element, attr->m_name);
    XDomAttr* replaced = XDom_wrapAttr(index >= 0 ? element->m_attributes[index] : NULL);
    if (!XDom_attachAttribute(element, attr, byNamespace)) {
        XClassDelete(replaced);
        return XDom_wrapAttr(NULL);
    }
    return replaced;
}

XDomAttr* XDomElement_setAttributeNode(XDomElement* self, const XDomAttr* newAttr)
{ return XDom_elementSetAttributeNode(self, newAttr, false); }

XDomAttr* XDomElement_removeAttributeNode(XDomElement* self, const XDomAttr* oldAttr)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    XDomNodePrivate* attr = XDom_attrImpl(oldAttr);
    int index = XDom_arrayIndex(element ? element->m_attributes : NULL,
                                     element ? element->m_attributeCount : 0, attr);
    if (index < 0) return XDom_wrapAttr(NULL);
    XDomAttr* result = XDom_wrapAttr(attr);
    XDom_arrayRemoveAt(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    XDom_nodeRelease(attr);
    return result;
}

bool XDomElement_hasAttribute(const XDomElement* self, const XString* name)
{ return XDom_attributeIndex(XDom_elementImpl(self), name) >= 0; }

bool XDomElement_hasAttribute_utf8(const XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    bool result = XDomElement_hasAttribute(self, text);
    XClassDelete(text);
    return result;
}

const XString* XDomElement_attributeNS(const XDomElement* self, const XString* namespaceURI,
                                          const XString* localName, const XString* defaultValue)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeNsIndex(element, namespaceURI, localName);
    return index >= 0 ? element->m_attributes[index]->m_value :
                        (defaultValue ? defaultValue : XDom_emptyString());
}

const XString* XDomElement_attributeNS_utf8(const XDomElement* self, const char* namespaceURI,
                                               const char* localName, const char* defaultValue)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XString* defaultText = XString_create_utf8(defaultValue);
    const XString* result = XDomElement_attributeNS(self, ns, local, defaultText);
    XDomNodePrivate* element = XDom_elementImpl(self);
    const XString* stable = result;
    if (result == defaultText) {
        if (element) {
            if (!element->m_attributeNSCache) element->m_attributeNSCache = XString_create();
            if (element->m_attributeNSCache) {
                XString_assign(element->m_attributeNSCache, defaultText);
                stable = element->m_attributeNSCache;
            }
        }
        if (stable == defaultText || !stable) stable = XDom_emptyString();
    }
    XClassDelete(ns); XClassDelete(local); XClassDelete(defaultText);
    return stable;
}

void XDomElement_setAttributeNS(XDomElement* self, const XString* namespaceURI,
                                   const XString* qualifiedName, const XString* value)
{ XDom_elementSetAttribute(self, namespaceURI, qualifiedName, value, true); }

void XDomElement_setAttributeNS_utf8(XDomElement* self, const char* namespaceURI,
                                        const char* qualifiedName, const char* value)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XString* text = XString_create_utf8(value);
    XDomElement_setAttributeNS(self, ns, name, text);
    XClassDelete(ns); XClassDelete(name); XClassDelete(text);
}

static void XDom_elementSetAttributeNsText(XDomElement* self, const XString* namespaceURI,
                                           const XString* name, const char* text)
{
    XString* value = XString_create_utf8(text);
    XDomElement_setAttributeNS(self, namespaceURI, name, value);
    XClassDelete(value);
}

void XDomElement_setAttributeNS_int(XDomElement* self, const XString* namespaceURI,
                                      const XString* name, int value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%d", value);
    XDom_elementSetAttributeNsText(self, namespaceURI, name, buffer);
}
void XDomElement_setAttributeNS_uint(XDomElement* self, const XString* namespaceURI,
                                       const XString* name, unsigned int value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%u", value);
    XDom_elementSetAttributeNsText(self, namespaceURI, name, buffer);
}
void XDomElement_setAttributeNS_int64(XDomElement* self, const XString* namespaceURI,
                                        const XString* name, int64_t value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    XDom_elementSetAttributeNsText(self, namespaceURI, name, buffer);
}
void XDomElement_setAttributeNS_uint64(XDomElement* self, const XString* namespaceURI,
                                         const XString* name, uint64_t value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    XDom_elementSetAttributeNsText(self, namespaceURI, name, buffer);
}
void XDomElement_setAttributeNS_double(XDomElement* self, const XString* namespaceURI,
                                         const XString* name, double value)
{
    char buffer[96];
    XSnprintf(buffer, sizeof(buffer), "%.17g", value);
    XDom_elementSetAttributeNsText(self, namespaceURI, name, buffer);
}

void XDomElement_removeAttributeNS(XDomElement* self, const XString* namespaceURI,
                                      const XString* localName)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeNsIndex(element, namespaceURI, localName);
    if (index < 0) return;
    XDomNodePrivate* attr = element->m_attributes[index];
    XDom_arrayRemoveAt(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    XDom_nodeRelease(attr);
}

void XDomElement_removeAttributeNS_utf8(XDomElement* self, const char* namespaceURI,
                                           const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomElement_removeAttributeNS(self, ns, local);
    XClassDelete(ns); XClassDelete(local);
}

XDomAttr* XDomElement_attributeNodeNS(const XDomElement* self, const XString* namespaceURI,
                                            const XString* localName)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    int index = XDom_attributeNsIndex(element, namespaceURI, localName);
    return XDom_wrapAttr(index >= 0 ? element->m_attributes[index] : NULL);
}

XDomAttr* XDomElement_setAttributeNodeNS(XDomElement* self, const XDomAttr* newAttr)
{ return XDom_elementSetAttributeNode(self, newAttr, true); }

bool XDomElement_hasAttributeNS(const XDomElement* self, const XString* namespaceURI,
                                   const XString* localName)
{ return XDom_attributeNsIndex(XDom_elementImpl(self), namespaceURI, localName) >= 0; }

XDomNodeList* XDomElement_elementsByTagName(const XDomElement* self, const XString* tagName)
{ return XDom_elementsByName(XDom_elementImpl(self), tagName, NULL); }

XDomNodeList* XDomElement_elementsByTagName_utf8(const XDomElement* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomNodeList* result = XDomElement_elementsByTagName(self, text);
    XClassDelete(text);
    return result;
}

XDomNodeList* XDomElement_elementsByTagNameNS(const XDomElement* self,
                                                     const XString* namespaceURI, const XString* localName)
{ return XDom_elementsByName(XDom_elementImpl(self), localName, namespaceURI); }

static void XDom_collectText(XDomNodePrivate* node, XString* output)
{
    if (!node || !output) return;
    for (int i = 0; i < node->m_childCount; ++i) {
        XDomNodePrivate* child = node->m_children[i];
        if (child->m_type == XDom_TextNode || child->m_type == XDom_CDATASectionNode)
            XString_append(output, child->m_value);
        else if (child->m_type == XDom_ElementNode)
            XDom_collectText(child, output);
    }
}

const XString* XDomElement_text(const XDomElement* self)
{
    XDomNodePrivate* element = XDom_elementImpl(self);
    if (!element) return XDom_emptyString();
    XDom_stringDelete(&element->m_textCache);
    element->m_textCache = XString_create();
    if (!element->m_textCache) return XDom_emptyString();
    XDom_collectText(element, element->m_textCache);
    return element->m_textCache;
}

XDomNamedNodeMap* XDomElement_attributes(const XDomElement* self)
{
    return XDomNode_attributes((const XDomNode*)self);
}

const XString* XDomAttr_name(const XDomAttr* self)
{
    XDomNodePrivate* attr = XDom_attrImpl(self);
    return attr ? attr->m_name : XDom_emptyString();
}

bool XDomAttr_specified(const XDomAttr* self)
{
    XDomNodePrivate* attr = XDom_attrImpl(self);
    return attr && attr->m_specified;
}

XDomElement* XDomAttr_ownerElement(const XDomAttr* self)
{
    XDomNodePrivate* attr = XDom_attrImpl(self);
    return XDom_wrapElement(attr ? attr->m_parent : NULL);
}

const XString* XDomAttr_value(const XDomAttr* self)
{
    XDomNodePrivate* attr = XDom_attrImpl(self);
    return attr ? attr->m_value : XDom_emptyString();
}

void XDomAttr_setValue(XDomAttr* self, const XString* value)
{
    XDomNodePrivate* attr = XDom_attrImpl(self);
    if (!attr) return;
    XDom_attrSetValuePrivate(attr, value);
    attr->m_specified = true;
    XDom_nodeInvalidateTextCache(attr);
}

void XDomAttr_setValue_utf8(XDomAttr* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomAttr_setValue(self, text);
    XClassDelete(text);
}

/* ==================== XML 序列化 ==================== */

/* XML 内部使用 UTF-16，逐个 XChar 处理可避免把 UTF-8 多字节字符拆开。 */
static void XDom_appendEscaped(XString* output, const XString* value, bool attribute)
{
    if (!output || !value) return;
    size_t length = XString_length_base(value);
    for (size_t i = 0; i < length; ++i) {
        XChar character = XString_at(value, i);
        switch (character) {
            case '&': XString_append_utf8(output, "&amp;"); break;
            case '<': XString_append_utf8(output, "&lt;"); break;
            case '>': XString_append_utf8(output, "&gt;"); break;
            case '"':
                if (attribute) XString_append_utf8(output, "&quot;");
                else XString_append_char(output, character);
                break;
            case '\'':
                if (attribute) XString_append_utf8(output, "&apos;");
                else XString_append_char(output, character);
                break;
            default: XString_append_char(output, character); break;
        }
    }
}

static void XDom_appendCdata(XString* output, const XString* value)
{
    if (!output || !value) return;
    size_t length = XString_length_base(value);
    for (size_t i = 0; i < length; ++i) {
        if (i + 2 < length && XString_at(value, i) == ']' &&
            XString_at(value, i + 1) == ']' && XString_at(value, i + 2) == '>') {
            XString_append_utf8(output, "]]]]><![CDATA[>");
            i += 2;
        } else {
            XString_append_char(output, XString_at(value, i));
        }
    }
}

static bool XDom_elementHasNamespaceDecl(const XDomNodePrivate* element,
                                                 const XString* prefix)
{
    if (!element || element->m_type != XDom_ElementNode) return false;
    for (int i = 0; i < element->m_attributeCount; ++i) {
        const XString* name = element->m_attributes[i]->m_name;
        if (!prefix || XString_isEmpty_base(prefix)) {
            if (XString_equals_utf8(name, "xmlns", XChar_CaseSensitive)) return true;
        }
        if (prefix && !XString_isEmpty_base(prefix)) {
            XString* qualified = XString_create_copy(prefix);
            if (qualified) {
                XString_insert_utf8(qualified, 0, "xmlns:");
                bool equal = XString_equals(name, qualified, XChar_CaseSensitive);
                XClassDelete(qualified);
                if (equal) return true;
            }
        }
    }
    return false;
}

static bool XDom_elementHasNamespacePrefixBefore(const XDomNodePrivate* element,
                                                         const XString* prefix, int end)
{
    if (!element || !prefix) return false;
    for (int i = 0; i < end && i < element->m_attributeCount; ++i) {
        XDomNodePrivate* attr = element->m_attributes[i];
        if (attr->m_createdWithNamespace &&
            XDom_stringEqual(attr->m_prefix, prefix)) return true;
    }
    return false;
}

static void XDom_appendNamespaceDecl(XString* output, const XString* prefix,
                                           const XString* namespaceURI)
{
    if (!output) return;
    XString_append_utf8(output, " xmlns");
    if (prefix && !XString_isEmpty_base(prefix)) {
        XString_append_char(output, ':');
        XString_append(output, prefix);
    }
    XString_append_utf8(output, "=\"");
    XDom_appendEscaped(output, namespaceURI, true);
    XString_append_char(output, '"');
}

static void XDom_appendIndent(XString* output, int indent, int level)
{
    if (!output || indent <= 0 || level <= 0) return;
    for (int i = 0; i < indent * level; ++i) XString_append_char(output, ' ');
}

static bool XDom_isTextualNode(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode ||
                    node->m_type == XDom_EntityReferenceNode);
}

static void XDom_serializeNode(XDomNodePrivate* node, XString* output,
                                    int indent, int level);

static void XDom_serializeDoctype(XDomNodePrivate* node, XString* output)
{
    if (!node || XString_isEmpty_base(node->m_name)) return;
    /* Qt 在写 DTD 标识符时优先使用单引号，避免无意义的转义。 */
    #define XDOM_APPEND_QUOTED(value) do { \
        char quote = XString_indexOf_utf8((value), "'", 0, XChar_CaseSensitive) < 0 ? '\'' : '"'; \
        XString_append_char(output, quote); \
        XString_append(output, (value)); \
        XString_append_char(output, quote); \
    } while (0)
    XString_append_utf8(output, "<!DOCTYPE ");
    XString_append(output, node->m_name);
    if (!node->m_publicIdNull) {
        XString_append_utf8(output, " PUBLIC ");
        XDOM_APPEND_QUOTED(node->m_publicId);
        if (!node->m_systemIdNull) {
            XString_append_char(output, ' ');
            XDOM_APPEND_QUOTED(node->m_systemId);
        }
    } else if (!node->m_systemIdNull) {
        XString_append_utf8(output, " SYSTEM ");
        XDOM_APPEND_QUOTED(node->m_systemId);
    }

    if (node->m_entityCount > 0 || node->m_notationCount > 0) {
        XString_append_utf8(output, " [");
        for (int i = 0; i < node->m_notationCount; ++i) {
            XDomNodePrivate* notation = node->m_notations[i];
            XString_append_utf8(output, "<!NOTATION ");
            XString_append(output, notation->m_name);
            XString_append_char(output, ' ');
            if (!notation->m_publicIdNull) {
                XString_append_utf8(output, "PUBLIC ");
                XDOM_APPEND_QUOTED(notation->m_publicId);
                if (!notation->m_systemIdNull) {
                    XString_append_char(output, ' ');
                    XDOM_APPEND_QUOTED(notation->m_systemId);
                }
            } else {
                XString_append_utf8(output, "SYSTEM ");
                XDOM_APPEND_QUOTED(notation->m_systemId);
            }
            XString_append_utf8(output, ">\n");
        }
        for (int i = 0; i < node->m_entityCount; ++i) {
            XDomNodePrivate* entity = node->m_entities[i];
            XString_append_utf8(output, "<!ENTITY ");
            XString_append(output, entity->m_name);
            XString_append_char(output, ' ');
            if (entity->m_publicIdNull && entity->m_systemIdNull) {
                XDOM_APPEND_QUOTED(entity->m_value);
            } else if (entity->m_publicIdNull) {
                XString_append_utf8(output, "SYSTEM ");
                XDOM_APPEND_QUOTED(entity->m_systemId);
                if (!XString_isEmpty_base(entity->m_notationName)) {
                    XString_append_utf8(output, " NDATA ");
                    XString_append(output, entity->m_notationName);
                }
            } else {
                XString_append_utf8(output, "PUBLIC ");
                XDOM_APPEND_QUOTED(entity->m_publicId);
                XString_append_char(output, ' ');
                XDOM_APPEND_QUOTED(entity->m_systemId);
                if (!XString_isEmpty_base(entity->m_notationName)) {
                    XString_append_utf8(output, " NDATA ");
                    XString_append(output, entity->m_notationName);
                }
            }
            XString_append_utf8(output, ">\n");
        }
        XString_append_utf8(output, "]");
    }
    XString_append_char(output, '>');
    #undef XDOM_APPEND_QUOTED
}

static void XDom_serializeElement(XDomNodePrivate* node, XString* output,
                                       int indent, int level)
{
    bool pretty = indent >= 0;
    if (pretty) XDom_appendIndent(output, indent, level);
    XString_append_char(output, '<');
    XString_append(output, node->m_name);
    if (node->m_createdWithNamespace &&
        ((!node->m_prefix || !XString_isEmpty_base(node->m_prefix)) ||
         !XString_isEmpty_base(node->m_namespaceUri)) &&
        !XDom_elementHasNamespaceDecl(node, node->m_prefix))
        XDom_appendNamespaceDecl(output, node->m_prefix, node->m_namespaceUri);
    for (int i = 0; i < node->m_attributeCount; ++i) {
        XDomNodePrivate* attribute = node->m_attributes[i];
        if (!attribute->m_createdWithNamespace ||
            XDom_stringEqual(attribute->m_prefix, node->m_prefix) ||
            XDom_elementHasNamespaceDecl(node, attribute->m_prefix) ||
            XDom_elementHasNamespacePrefixBefore(node, attribute->m_prefix, i))
            continue;
        XDom_appendNamespaceDecl(output, attribute->m_prefix,
                                       attribute->m_namespaceUri);
    }
    for (int emitted = 0; emitted < node->m_attributeCount; ++emitted) {
        int selected = -1;
        for (int i = 0; i < node->m_attributeCount; ++i) {
            XDomNodePrivate* candidate = node->m_attributes[i];
            if (candidate->m_line == INT64_MIN) continue;
            const XString* candidatePrefix = candidate->m_createdWithNamespace ?
                                              candidate->m_prefix : XDom_emptyString();
            if (selected < 0) {
                selected = i;
                continue;
            }
            XDomNodePrivate* current = node->m_attributes[selected];
            const XString* currentPrefix = current->m_createdWithNamespace ?
                                            current->m_prefix : XDom_emptyString();
            int prefixOrder = XString_compare(candidatePrefix, currentPrefix);
            if (prefixOrder < 0 ||
                (prefixOrder == 0 && XString_compare(candidate->m_name, current->m_name) < 0))
                selected = i;
        }
        XDomNodePrivate* attribute = node->m_attributes[selected];
        XString_append_char(output, ' ');
        XString_append(output, attribute->m_name);
        XString_append_utf8(output, "=\"");
        XDom_appendEscaped(output, attribute->m_value, true);
        XString_append_char(output, '"');
        /* 选中的节点标记为空，后续循环按原数组寻找未输出项。 */
        node->m_attributes[selected]->m_line = INT64_MIN;
    }
    for (int i = 0; i < node->m_attributeCount; ++i)
        if (node->m_attributes[i]->m_line == INT64_MIN) node->m_attributes[i]->m_line = -1;
    if (node->m_childCount == 0) {
        XString_append_utf8(output, "/>");
        return;
    }
    XString_append_char(output, '>');
    bool inlineChildren = node->m_childCount == 1 && XDom_isTextualNode(node->m_children[0]);
    for (int i = 0; i < node->m_childCount; ++i) {
        if (pretty && !inlineChildren) XString_append_char(output, '\n');
        XDom_serializeNode(node->m_children[i], output, indent,
                                inlineChildren ? level : level + 1);
    }
    if (pretty && !inlineChildren) {
        XString_append_char(output, '\n');
        XDom_appendIndent(output, indent, level);
    }
    XString_append_utf8(output, "</");
    XString_append(output, node->m_name);
    XString_append_char(output, '>');
}

static void XDom_serializeNode(XDomNodePrivate* node, XString* output,
                                    int indent, int level)
{
    if (!node || !output) return;
    bool pretty = indent >= 0;
    switch (node->m_type) {
        case XDom_DocumentNode:
        {
            bool wroteHeader = false;
            int firstChild = 0;
            /* Qt 把 XML 声明保存为第一个 target 为 xml 的处理指令。 */
            if (node->m_childCount > 0 &&
                node->m_children[0]->m_type == XDom_ProcessingInstructionNode &&
                XDom_stringEqual_utf8(node->m_children[0]->m_name, "xml")) {
                XDom_serializeNode(node->m_children[0], output, indent, 0);
                wroteHeader = true;
                firstChild = 1;
            }
            if (node->m_doctype && !XString_isEmpty_base(node->m_doctype->m_name)) {
                if (wroteHeader && pretty) XString_append_char(output, '\n');
                XDom_serializeDoctype(node->m_doctype, output);
                wroteHeader = true;
            }
            for (int i = firstChild; i < node->m_childCount; ++i) {
                if (pretty && (wroteHeader || i > firstChild))
                    XString_append_char(output, '\n');
                XDom_serializeNode(node->m_children[i], output, indent, 0);
                wroteHeader = true;
            }
            break;
        }
        case XDom_ElementNode:
            XDom_serializeElement(node, output, indent, level);
            break;
        case XDom_TextNode:
            XDom_appendEscaped(output, node->m_value, false);
            break;
        case XDom_CDATASectionNode:
            if (pretty) XDom_appendIndent(output, indent, level);
            XString_append_utf8(output, "<![CDATA[");
            XDom_appendCdata(output, node->m_value);
            XString_append_utf8(output, "]]>");
            break;
        case XDom_CommentNode:
            if (pretty) XDom_appendIndent(output, indent, level);
            XString_append_utf8(output, "<!--");
            XString_append(output, node->m_value);
            XString_append_utf8(output, "-->");
            break;
        case XDom_ProcessingInstructionNode:
            if (pretty) XDom_appendIndent(output, indent, level);
            XString_append_utf8(output, "<?");
            XString_append(output, node->m_name);
            if (!XString_isEmpty_base(node->m_value)) {
                XString_append_char(output, ' ');
                XString_append(output, node->m_value);
            }
            XString_append_utf8(output, "?>");
            break;
        case XDom_DocumentTypeNode:
            if (pretty) XDom_appendIndent(output, indent, level);
            XDom_serializeDoctype(node, output);
            break;
        case XDom_EntityReferenceNode:
            XString_append_char(output, '&');
            XString_append(output, node->m_name);
            XString_append_char(output, ';');
            break;
        default:
            break;
    }
}

static XString* XDom_serialize(XDomNodePrivate* node, int indent)
{
    XString* result = XString_create();
    if (result && node) XDom_serializeNode(node, result, indent, 0);
    return result;
}

static XString* XDom_serializeForPolicy(XDomNodePrivate* node, int indent,
                                               XDomEncodingPolicy policy)
{
    if (!node || node->m_type != XDom_DocumentNode ||
        policy == XDom_EncodingFromDocument)
        return XDom_serialize(node, indent);

    /* XinYueC 的 XIODevice 没有 QTextStream 编码状态，FromTextStream 以 UTF-8 约定输出。 */
    XString* result = XString_create();
    if (!result) return NULL;
    XString_append_utf8(result, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    bool wroteContent = true;
    if (node->m_doctype && !XString_isEmpty_base(node->m_doctype->m_name)) {
        if (indent >= 0 && wroteContent) XString_append_char(result, '\n');
        XDom_serializeDoctype(node->m_doctype, result);
        wroteContent = true;
    }
    for (int i = 0; i < node->m_childCount; ++i) {
        XDomNodePrivate* child = node->m_children[i];
        /* EncodingFromTextStream 已经生成新的声明，跳过文档中的 xml PI。 */
        if (child->m_type == XDom_ProcessingInstructionNode &&
            XDom_stringEqual_utf8(child->m_name, "xml")) continue;
        if (indent >= 0 && wroteContent) XString_append_char(result, '\n');
        XDom_serializeNode(child, result, indent, 0);
        wroteContent = true;
    }
    return result;
}

/* ==================== 字符数据、DTD 与实现对象 ==================== */

static XDomNodePrivate* XDom_characterDataImpl(const XDomCharacterData* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return XDom_nodeIsCharacterDataPayload(node) ? node : NULL;
}

XString* XDomCharacterData_substringData(const XDomCharacterData* self,
                                            uint64_t offset, uint64_t count)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node) return XString_create();
    size_t length = XString_length_base(node->m_value);
    if (offset >= length) return XString_create();
    size_t start = (size_t)offset;
    size_t available = length - start;
    size_t requested = count > (uint64_t)available ? available : (size_t)count;
    return XString_mid(node->m_value, start, requested);
}

void XDomCharacterData_appendData(XDomCharacterData* self, const XString* value)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node || !value) return;
    XString_append(node->m_value, value);
    XDom_nodeInvalidateTextCache(node);
}

void XDomCharacterData_appendData_utf8(XDomCharacterData* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomCharacterData_appendData(self, text);
    XClassDelete(text);
}

void XDomCharacterData_insertData(XDomCharacterData* self, uint64_t offset,
                                     const XString* value)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node || !value) return;
    size_t length = XString_length_base(node->m_value);
    /* QString::insert 对超出范围的 offset 不补齐，也不改变原字符串。 */
    if (offset > (uint64_t)length || offset > (uint64_t)SIZE_MAX) return;
    XString_insert(node->m_value, (size_t)offset, value);
    XDom_nodeInvalidateTextCache(node);
}

void XDomCharacterData_deleteData(XDomCharacterData* self, uint64_t offset,
                                     uint64_t count)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node) return;
    size_t length = XString_length_base(node->m_value);
    if (offset >= length) return;
    size_t start = (size_t)offset;
    size_t amount = count > (uint64_t)(length - start) ? length - start : (size_t)count;
    XString_remove(node->m_value, start, amount);
    XDom_nodeInvalidateTextCache(node);
}

void XDomCharacterData_replaceData(XDomCharacterData* self, uint64_t offset,
                                      uint64_t count, const XString* value)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node || !value) return;
    size_t length = XString_length_base(node->m_value);
    if (offset > length) return;
    size_t start = (size_t)offset;
    size_t amount = count > (uint64_t)(length - start) ? length - start : (size_t)count;
    XString_remove(node->m_value, start, amount);
    XString_insert(node->m_value, start, value);
    XDom_nodeInvalidateTextCache(node);
}

int XDomCharacterData_length(const XDomCharacterData* self)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    size_t length = node ? XString_length_base(node->m_value) : 0;
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

const XString* XDomCharacterData_data(const XDomCharacterData* self)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    return node ? node->m_value : XDom_emptyString();
}

void XDomCharacterData_setData(XDomCharacterData* self, const XString* value)
{
    XDomNodePrivate* node = XDom_characterDataImpl(self);
    if (!node) return;
    XDom_stringAssign(&node->m_value, value);
    XDom_nodeInvalidateTextCache(node);
}

void XDomCharacterData_setData_utf8(XDomCharacterData* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomCharacterData_setData(self, text);
    XClassDelete(text);
}

XDomText* XDomText_splitText(XDomText* self, int offset)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node || (node->m_type != XDom_TextNode &&
                  node->m_type != XDom_CDATASectionNode) || !node->m_parent)
        return XDom_wrapText(NULL);
    size_t length = XString_length_base(node->m_value);
    size_t split = offset <= 0 ? 0 : (size_t)offset;
    if (split > length) split = length;
    XString* suffix = XString_mid(node->m_value, split, length - split);
    if (!suffix) return XDom_wrapText(NULL);
    XString_remove(node->m_value, split, length - split);
    XDomNodePrivate* newNode = XDom_createNodeForDocument(
        XDom_documentForNode(node), XDom_TextNode);
    if (!newNode) {
        XClassDelete(suffix);
        return XDom_wrapText(NULL);
    }
    XDom_stringAssign(&newNode->m_value, suffix);
    XClassDelete(suffix);
    int index = XDom_childIndex(node->m_parent, node);
    if (!XDom_insertChild(node->m_parent, newNode, index + 1)) {
        XDom_nodeRelease(newNode);
        return XDom_wrapText(NULL);
    }
    XDomText* result = XDom_wrapText(newNode);
    XDom_nodeRelease(newNode);
    XDom_nodeInvalidateTextCache(node);
    return result;
}

const XString* XDomDocumentType_name(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_name : XDom_emptyString();
}

XDomNamedNodeMap* XDomDocumentType_entities(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    XDomNodePrivate* map = XDom_mapNew(node && node->m_type == XDom_DocumentTypeNode ? node : NULL,
                                                XDom_MapEntities);
    XDomNamedNodeMap* result = XDom_wrapMap(map);
    XDom_nodeRelease(map);
    return result;
}

XDomNamedNodeMap* XDomDocumentType_notations(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    XDomNodePrivate* map = XDom_mapNew(node && node->m_type == XDom_DocumentTypeNode ? node : NULL,
                                                XDom_MapNotations);
    XDomNamedNodeMap* result = XDom_wrapMap(map);
    XDom_nodeRelease(map);
    return result;
}

const XString* XDomDocumentType_publicId(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_publicId : XDom_emptyString();
}

const XString* XDomDocumentType_systemId(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_systemId : XDom_emptyString();
}

const XString* XDomDocumentType_internalSubset(const XDomDocumentType* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_internalSubset : XDom_emptyString();
}

const XString* XDomEntity_publicId(const XDomEntity* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_publicId : XDom_emptyString();
}

const XString* XDomEntity_systemId(const XDomEntity* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_systemId : XDom_emptyString();
}

const XString* XDomEntity_notationName(const XDomEntity* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_notationName : XDom_emptyString();
}

const XString* XDomNotation_publicId(const XDomNotation* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_NotationNode ? node->m_publicId : XDom_emptyString();
}

const XString* XDomNotation_systemId(const XDomNotation* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_NotationNode ? node->m_systemId : XDom_emptyString();
}

const XString* XDomProcessingInstruction_target(const XDomProcessingInstruction* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_ProcessingInstructionNode ? node->m_name : XDom_emptyString();
}

const XString* XDomProcessingInstruction_data(const XDomProcessingInstruction* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && node->m_type == XDom_ProcessingInstructionNode ? node->m_value : XDom_emptyString();
}

void XDomProcessingInstruction_setData(XDomProcessingInstruction* self, const XString* value)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    if (!node || node->m_type != XDom_ProcessingInstructionNode) return;
    XDom_stringAssign(&node->m_value, value);
}

void XDomProcessingInstruction_setData_utf8(XDomProcessingInstruction* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomProcessingInstruction_setData(self, text);
    XClassDelete(text);
}

/* ==================== QDomImplementation 与类型转换 ==================== */

bool XDomImplementation_hasFeature(const XDomImplementation* self,
                                    const XString* feature, const XString* version)
{
    if (!XDom_implementationIsValid(self) || !feature) return false;
    return XDom_stringEqual_utf8(feature, "XML") &&
           (!version || XString_isEmpty_base(version) ||
            XDom_stringEqual_utf8(version, "1.0"));
}

bool XDomImplementation_hasFeature_utf8(const XDomImplementation* self,
                                         const char* feature, const char* version)
{
    XString* featureString = XString_create_utf8(feature);
    XString* versionString = XString_create_utf8(version);
    bool result = XDomImplementation_hasFeature(self, featureString, versionString);
    XClassDelete(featureString);
    XClassDelete(versionString);
    return result;
}

XDomDocumentType* XDomImplementation_createDocumentType(const XDomImplementation* self,
                                                        const XString* qualifiedName,
                                                        const XString* publicId,
                                                        const XString* systemId)
{
    bool ok = false;
    XString* fixedName = XDom_fixedName(qualifiedName, true, &ok);
    if (!XDom_implementationIsValid(self) || !ok) {
        XClassDelete(fixedName);
        return XDom_wrapDoctype(NULL);
    }
    bool publicOk = false;
    bool systemOk = false;
    XString* fixedPublicId = XDom_fixedPublicId(publicId, &publicOk);
    XString* fixedSystemId = XDom_fixedSystemId(systemId, &systemOk);
    if (!publicOk || !systemOk) {
        XClassDelete(fixedName);
        XClassDelete(fixedPublicId);
        XClassDelete(fixedSystemId);
        return XDom_wrapDoctype(NULL);
    }
    XDomNodePrivate* doctype = XDom_nodeNew(XDom_DocumentTypeNode);
    if (!doctype) {
        XClassDelete(fixedName);
        XClassDelete(fixedPublicId);
        XClassDelete(fixedSystemId);
        return XDom_wrapDoctype(NULL);
    }
    XDom_setPlainName(doctype, fixedName);
    XDom_stringAssign(&doctype->m_publicId, fixedPublicId);
    XDom_stringAssign(&doctype->m_systemId, fixedSystemId);
    /* XString 空串对应 Qt 的非 null QString；只有 NULL 才表示 null 标识符。 */
    doctype->m_publicIdNull = publicId == NULL;
    doctype->m_systemIdNull = systemId == NULL;
    if (doctype->m_systemIdNull) {
        doctype->m_publicIdNull = true;
        XString_clear_base(doctype->m_publicId);
    }
    XDomDocumentType* result = XDom_wrapDoctype(doctype);
    XDom_nodeRelease(doctype);
    XClassDelete(fixedName);
    XClassDelete(fixedPublicId);
    XClassDelete(fixedSystemId);
    return result;
}

XDomDocumentType* XDomImplementation_createDocumentType_utf8(const XDomImplementation* self,
                                                             const char* qualifiedName,
                                                             const char* publicId,
                                                             const char* systemId)
{
    XString* name = XString_create_utf8(qualifiedName);
    XString* publicText = XString_create_utf8(publicId);
    XString* systemText = systemId ? XString_create_utf8(systemId) : NULL;
    XDomDocumentType* result = XDomImplementation_createDocumentType(self, name, publicText,
                                                                       systemText);
    XClassDelete(name);
    XClassDelete(publicText);
    XClassDelete(systemText);
    return result;
}

XDomDocument* XDomImplementation_createDocument(const XDomImplementation* self,
                                                const XString* namespaceURI,
                                                const XString* qualifiedName,
                                                const XDomDocumentType* doctype)
{
    if (!XDom_implementationIsValid(self)) return NULL;
    bool nameOk = false;
    XString* fixedName = XDom_fixedName(qualifiedName, namespaceURI != NULL, &nameOk);
    if (!nameOk) {
        XClassDelete(fixedName);
        return NULL;
    }
    XDomDocument* result = XDomDocument_create();
    XDomNodePrivate* document = XDom_documentEnsureImpl(result);
    if (!document) {
        XClassDelete(fixedName);
        XClassDelete(result);
        return NULL;
    }

    XDomNodePrivate* sourceType = XDom_handleImpl(doctype);
    if (sourceType && sourceType->m_type == XDom_DocumentTypeNode) {
        XDomNodePrivate* type = XDom_nodeClone(sourceType, document->m_context, true);
        if (document->m_doctype) {
            XDom_nodeRelease(document->m_doctype);
            document->m_doctype = NULL;
        }
        if (!type || !XDom_insertChild(document, type, -1)) {
            if (type) XDom_nodeRelease(type);
            XClassDelete(fixedName);
            XClassDelete(result);
            return NULL;
        }
        XDom_nodeRelease(type);
    }

    XDomNodePrivate* root = XDom_createNodeForDocument(document, XDom_ElementNode);
    if (!root) {
        if (root) XDom_nodeRelease(root);
        XClassDelete(fixedName);
        XClassDelete(result);
        return NULL;
    }
    if (namespaceURI) XDom_setQualifiedName(root, namespaceURI, fixedName);
    else XDom_setPlainName(root, fixedName);
    if (!XDom_insertChild(document, root, -1)) {
        XDom_nodeRelease(root);
        XClassDelete(fixedName);
        XClassDelete(result);
        return NULL;
    }
    XDom_nodeRelease(root);
    XClassDelete(fixedName);
    return result;
}

XDomDocument* XDomImplementation_createDocument_utf8(const XDomImplementation* self,
                                                     const char* namespaceURI,
                                                     const char* qualifiedName,
                                                     const XDomDocumentType* doctype)
{
    XString* namespaceText = namespaceURI ? XString_create_utf8(namespaceURI) : NULL;
    XString* name = XString_create_utf8(qualifiedName);
    XDomDocument* result = XDomImplementation_createDocument(self, namespaceText, name, doctype);
    XClassDelete(namespaceText);
    XClassDelete(name);
    return result;
}

bool XDomImplementation_isNull(const XDomImplementation* self)
{
    return !XDom_implementationIsValid(self);
}

bool XDomImplementation_equals(const XDomImplementation* left,
                               const XDomImplementation* right)
{
    if (left == right) return true;
    if (!left || !right || XDomImplementation_isNull(left) != XDomImplementation_isNull(right))
        return false;
    return left->m_document == right->m_document;
}

XDomInvalidDataPolicy XDomImplementation_invalidDataPolicy(void)
{
    return g_xxml_dom_invalid_data_policy;
}

void XDomImplementation_setInvalidDataPolicy(XDomInvalidDataPolicy policy)
{
    if (policy < XDom_AcceptInvalidChars || policy > XDom_ReturnNullNode) return;
    g_xxml_dom_invalid_data_policy = policy;
}

static XDomNode* XDom_handleToNode(const void* handle, XDomNodeType type)
{
    XDomNodePrivate* node = XDom_handleImpl(handle);
    return node && node->m_type == type ? XDom_wrapNode(node) : XDom_wrapNode(NULL);
}

XDomNode* XDomElement_toNode(const XDomElement* self)
{ return XDom_handleToNode(self, XDom_ElementNode); }

XDomNode* XDomAttr_toNode(const XDomAttr* self)
{ return XDom_handleToNode(self, XDom_AttributeNode); }

XDomNode* XDomText_toNode(const XDomText* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && (node->m_type == XDom_TextNode || node->m_type == XDom_CDATASectionNode) ?
        XDom_wrapNode(node) : XDom_wrapNode(NULL);
}

XDomNode* XDomCDATASection_toNode(const XDomCDATASection* self)
{ return XDom_handleToNode(self, XDom_CDATASectionNode); }

XDomNode* XDomComment_toNode(const XDomComment* self)
{ return XDom_handleToNode(self, XDom_CommentNode); }

XDomNode* XDomDocument_toNode(const XDomDocument* self)
{ return XDom_handleToNode(self, XDom_DocumentNode); }

XDomNode* XDomDocumentType_toNode(const XDomDocumentType* self)
{ return XDom_handleToNode(self, XDom_DocumentTypeNode); }

XDomNode* XDomDocumentFragment_toNode(const XDomDocumentFragment* self)
{ return XDom_handleToNode(self, XDom_DocumentFragmentNode); }

XDomNode* XDomCharacterData_toNode(const XDomCharacterData* self)
{
    XDomNodePrivate* node = XDom_handleImpl(self);
    return node && XDom_nodeIsCharacterData(node) ?
        XDom_wrapNode(node) : XDom_wrapNode(NULL);
}

XDomNode* XDomEntity_toNode(const XDomEntity* self)
{ return XDom_handleToNode(self, XDom_EntityNode); }

XDomNode* XDomNotation_toNode(const XDomNotation* self)
{ return XDom_handleToNode(self, XDom_NotationNode); }

XDomNode* XDomEntityReference_toNode(const XDomEntityReference* self)
{ return XDom_handleToNode(self, XDom_EntityReferenceNode); }

XDomNode* XDomProcessingInstruction_toNode(const XDomProcessingInstruction* self)
{ return XDom_handleToNode(self, XDom_ProcessingInstructionNode); }

/* ==================== 文档解析与输出 ==================== */

void XDomParseResult_init(XDomParseResult* result)
{
    if (!result) return;
    result->m_errorMessage = NULL;
    result->m_errorLine = 0;
    result->m_errorColumn = 0;
}

void XDomParseResult_deinit(XDomParseResult* result)
{
    if (!result) return;
    XDom_stringDelete(&result->m_errorMessage);
    result->m_errorLine = 0;
    result->m_errorColumn = 0;
}

bool XDomParseResult_isSuccess(const XDomParseResult* result)
{
    return result && result->m_errorMessage == NULL;
}

static void XDom_parseResultError_utf8(XDomParseResult* result, const char* message,
                                              int64_t line, int64_t column)
{
    if (!result || result->m_errorMessage) return;
    result->m_errorMessage = XString_create_utf8(message ? message : "XML 解析失败");
    result->m_errorLine = line;
    result->m_errorColumn = column;
}

static void XDom_extractInternalSubset(XDomNodePrivate* doctype, const XString* rawDtd)
{
    if (!doctype || !rawDtd) return;
    const char* data = XString_toUtf8(rawDtd);
    if (!data) return;
    size_t length = XStrlen(data);
    const char* begin = NULL;
    const char* end = NULL;
    int bracketDepth = 0;
    char quote = '\0';
    bool comment = false;
    for (size_t i = 0; i < length; ++i) {
        const char* p = data + i;
        if (comment) {
            if (i + 2 < length && p[0] == '-' && p[1] == '-' && p[2] == '>') {
                comment = false;
                i += 2;
            }
            continue;
        }
        if (quote) {
            if (*p == quote) quote = '\0';
            continue;
        }
        if (i + 3 < length && p[0] == '<' && p[1] == '!' &&
            p[2] == '-' && p[3] == '-') {
            comment = true;
            i += 3;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }
        if (*p == '[') {
            if (!begin) begin = p;
            ++bracketDepth;
        } else if (*p == ']' && bracketDepth > 0) {
            if (--bracketDepth == 0) {
                end = p;
                break;
            }
        }
    }
    if (!begin || !end || end <= begin) return;
    XString* subset = XString_create_with_length_utf8(begin + 1,
                                                       (size_t)(end - begin - 1));
    if (subset) {
        XDom_stringAssign(&doctype->m_internalSubset, subset);
        XClassDelete(subset);
    }
}

static void XDom_appendDtdEntity(XDomNodePrivate* document,
                                       XDomNodePrivate* doctype,
                                       const XXmlStreamEntityDeclaration* declaration)
{
    if (!document || !doctype || !declaration) return;
    XDomNodePrivate* entity = XDom_createNodeForDocument(document, XDom_EntityNode);
    if (!entity) return;
    XDom_setPlainName(entity, XXmlStreamEntityDeclaration_name(declaration));
    XDom_stringAssign(&entity->m_value, XXmlStreamEntityDeclaration_value(declaration));
    XDom_stringAssign(&entity->m_publicId, XXmlStreamEntityDeclaration_publicId(declaration));
    XDom_stringAssign(&entity->m_systemId, XXmlStreamEntityDeclaration_systemId(declaration));
    XDom_stringAssign(&entity->m_notationName, XXmlStreamEntityDeclaration_notationName(declaration));
    entity->m_ownerDocument = document;
    if (XDom_appendPrivate(doctype, entity, -1) &&
        XDom_arrayAppend(&doctype->m_entities, &doctype->m_entityCount,
                              &doctype->m_entityCapacity, entity))
        XDom_nodeRetain(entity);
    XDom_nodeRelease(entity);
}

static void XDom_appendDtdNotation(XDomNodePrivate* document,
                                         XDomNodePrivate* doctype,
                                         const XXmlStreamNotationDeclaration* declaration)
{
    if (!document || !doctype || !declaration) return;
    XDomNodePrivate* notation = XDom_createNodeForDocument(document, XDom_NotationNode);
    if (!notation) return;
    XDom_setPlainName(notation, XXmlStreamNotationDeclaration_name(declaration));
    XDom_stringAssign(&notation->m_publicId, XXmlStreamNotationDeclaration_publicId(declaration));
    XDom_stringAssign(&notation->m_systemId, XXmlStreamNotationDeclaration_systemId(declaration));
    notation->m_ownerDocument = document;
    if (XDom_appendPrivate(doctype, notation, -1) &&
        XDom_arrayAppend(&doctype->m_notations, &doctype->m_notationCount,
                              &doctype->m_notationCapacity, notation))
        XDom_nodeRetain(notation);
    XDom_nodeRelease(notation);
}

static void XDom_parseDtd(XDomNodePrivate* document, XXmlStreamReader* reader)
{
    if (!document || !reader) return;
    XDomNodePrivate* doctype = XDom_createNodeForDocument(document, XDom_DocumentTypeNode);
    if (!doctype) return;
    XDom_setPlainName(doctype, XXmlStreamReader_dtdName(reader));
    XDom_stringAssign(&doctype->m_publicId, XXmlStreamReader_dtdPublicId(reader));
    XDom_stringAssign(&doctype->m_systemId, XXmlStreamReader_dtdSystemId(reader));
    doctype->m_publicIdNull = XString_isEmpty_base(doctype->m_publicId);
    doctype->m_systemIdNull = XString_isEmpty_base(doctype->m_systemId);
    XDom_extractInternalSubset(doctype, XXmlStreamReader_text(reader));
    doctype->m_ownerDocument = document;
    XXmlStreamEntityDeclarations* entities = XXmlStreamReader_entityDeclarations(reader);
    for (size_t i = 0; entities && i < XXmlStreamEntityDeclarations_size(entities); ++i) {
        const XXmlStreamEntityDeclaration* declaration =
            XXmlStreamEntityDeclarations_at(entities, i);
        const XString* publicId = XXmlStreamEntityDeclaration_publicId(declaration);
        const XString* systemId = XXmlStreamEntityDeclaration_systemId(declaration);
        /* Qt 的 QDomBuilder 只把外部/未解析实体放入 entities；内部实体由读取器展开。 */
        if ((publicId && !XString_isEmpty_base(publicId)) ||
            (systemId && !XString_isEmpty_base(systemId)))
            XDom_appendDtdEntity(document, doctype, declaration);
    }
    XXmlStreamNotationDeclarations* notations = XXmlStreamReader_notationDeclarations(reader);
    for (size_t i = 0; notations && i < XXmlStreamNotationDeclarations_size(notations); ++i)
        XDom_appendDtdNotation(document, doctype, XXmlStreamNotationDeclarations_at(notations, i));
    if (document->m_doctype) {
        XDom_nodeRelease(document->m_doctype);
        document->m_doctype = NULL;
    }
    if (!XDom_insertChild(document, doctype, -1))
        XDom_nodeRelease(doctype);
    else
        XDom_nodeRelease(doctype);
}

static XDomNodePrivate* XDom_parseElement(XDomNodePrivate* document,
                                                   XDomNodePrivate* parent,
                                                   XXmlStreamReader* reader, bool useNamespace)
{
    XDomNodePrivate* element = XDom_createNodeForDocument(document, XDom_ElementNode);
    if (!element) return NULL;
    if (useNamespace) XDom_setQualifiedName(element, XXmlStreamReader_namespaceUri(reader),
                                                   XXmlStreamReader_qualifiedName(reader));
    else XDom_setPlainName(element, XXmlStreamReader_qualifiedName(reader));
    element->m_ownerDocument = document;
    element->m_line = XXmlStreamReader_lineNumber(reader);
    element->m_column = XXmlStreamReader_columnNumber(reader);
    const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
    for (int i = 0; attributes && i < XXmlStreamAttributes_size(attributes); ++i) {
        const XXmlStreamAttribute* source = XXmlStreamAttributes_at(attributes, i);
        XDomNodePrivate* attribute = XDom_createNodeForDocument(document, XDom_AttributeNode);
        if (!attribute) continue;
        if (useNamespace) XDom_setQualifiedName(attribute, XXmlStreamAttribute_namespaceUri(source),
                                                       XXmlStreamAttribute_qualifiedName(source));
        else XDom_setPlainName(attribute, XXmlStreamAttribute_qualifiedName(source));
        XDom_attrSetValuePrivate(attribute, XXmlStreamAttribute_value(source));
        attribute->m_ownerDocument = document;
        attribute->m_specified = true;
        XDom_attachAttribute(element, attribute, useNamespace);
        XDom_nodeRelease(attribute);
    }
    if (useNamespace) {
        const XXmlStreamNamespaceDeclarations* declarations =
            XXmlStreamReader_namespaceDeclarations(reader);
        for (int i = 0; declarations && i < XXmlStreamNamespaceDeclarations_size(declarations); ++i) {
            const XXmlStreamNamespaceDeclaration* declaration =
                XXmlStreamNamespaceDeclarations_at(declarations, i);
            const XString* prefix = XXmlStreamNamespaceDeclaration_prefix(declaration);
            const XString* namespaceURI = XXmlStreamNamespaceDeclaration_namespaceUri(declaration);
            if (!declaration || !prefix || !namespaceURI) continue;
            XString* name = XString_create();
            if (!name) continue;
            if (XString_isEmpty_base(prefix)) XString_assign_utf8(name, "xmlns");
            else {
                XString_assign_utf8(name, "xmlns:");
                XString_append(name, prefix);
            }
            XDomNodePrivate* attribute = XDom_createNodeForDocument(document,
                                                                              XDom_AttributeNode);
            if (attribute) {
                XDom_setPlainName(attribute, name);
                XDom_attrSetValuePrivate(attribute, namespaceURI);
                attribute->m_ownerDocument = document;
                attribute->m_specified = true;
                XDom_attachAttribute(element, attribute, false);
                XDom_nodeRelease(attribute);
            }
            XClassDelete(name);
        }
    }
    if (!XDom_insertChild(parent, element, -1)) {
        XDom_nodeRelease(element);
        return NULL;
    }
    /* 树已经持有元素引用；current 只借用该指针，不应额外持有初始引用。 */
    XDom_nodeRelease(element);
    return element;
}

static void XDom_parseValueNode(XDomNodePrivate* document, XDomNodePrivate* parent,
                                      XDomNodeType type, const XString* value)
{
    if (!document || !parent) return;
    XDomNodePrivate* node = XDom_documentNewValueNode(document, type, value);
    if (!node) return;
    XDom_insertChild(parent, node, -1);
    XDom_nodeRelease(node);
}

static void XDom_captureDocumentInfo(XDomNodePrivate* document,
                                            const XXmlStreamReader* reader)
{
    if (!document || !reader || !XXmlStreamReader_hasXmlDeclaration(reader)) return;
    XDom_stringAssign(&document->m_documentVersion,
                           XXmlStreamReader_documentVersion(reader));
    XDom_stringAssign(&document->m_documentEncoding,
                           XXmlStreamReader_documentEncoding(reader));
    document->m_hasXmlDeclaration = true;
    document->m_hasStandalone = XXmlStreamReader_hasStandaloneDeclaration(reader);
    document->m_isStandalone = document->m_hasStandalone &&
                               XXmlStreamReader_isStandaloneDocument(reader);
}

/**
 * @brief      构造 Qt QDomDocument 使用的 XML 声明处理指令数据。
 * @param      reader 已定位在 StartDocument 的 XML 读取器；只借用。
 * @return     新分配的处理指令数据；调用者使用 XClassDelete 释放，失败返回 NULL。
 * @note       属性使用单引号，且仅在输入声明中出现 standalone 时写出 standalone='no'。
 */
static XString* XDom_xmlDeclarationData(const XXmlStreamReader* reader)
{
    if (!reader) return NULL;
    const XString* version = XXmlStreamReader_documentVersion(reader);
    if (!version || XString_isEmpty_base(version)) return NULL;
    XString* data = XString_create();
    if (!data) return NULL;
    XString_append_utf8(data, "version='");
    XString_append(data, version);
    XString_append_char(data, '\'');
    const XString* encoding = XXmlStreamReader_documentEncoding(reader);
    if (encoding && !XString_isEmpty_base(encoding)) {
        XString_append_utf8(data, " encoding='");
        XString_append(data, encoding);
        XString_append_char(data, '\'');
    }
    if (XXmlStreamReader_isStandaloneDocument(reader)) {
        XString_append_utf8(data, " standalone='yes'");
    } else if (XXmlStreamReader_hasStandaloneDeclaration(reader)) {
        XString_append_utf8(data, " standalone='no'");
    }
    return data;
}

static XDomParseResult XDom_setContentFromReader(XDomNodePrivate* document,
                                                         XXmlStreamReader* reader,
                                                         unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    if (!document || !reader) {
        XDom_parseResultError_utf8(&result, "XML 文档或读取器为空", -1, -1);
        return result;
    }

    XDom_nodeClear(document);
    if (!XDom_documentCreateDefaultDoctype(document)) {
        XDom_parseResultError_utf8(&result, "XML 文档类型创建失败", -1, -1);
        return result;
    }
    XDom_stringAssign(&document->m_documentVersion, NULL);
    XDom_stringAssign(&document->m_documentEncoding, NULL);
    document->m_hasXmlDeclaration = false;
    document->m_isStandalone = false;
    document->m_hasStandalone = false;

    bool useNamespace = (options & XDom_UseNamespaceProcessing) != 0;
    bool foundDtd = false;
    XXmlStreamReader_setNamespaceProcessing(reader, useNamespace);
    XDom_captureDocumentInfo(document, reader);
    XDomNodePrivate* current = document;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token == XXmlStream_Invalid || XXmlStreamReader_hasError(reader)) break;
        if (token == XXmlStream_StartDocument) {
            XDom_captureDocumentInfo(document, reader);
            XString* declarationData = XDom_xmlDeclarationData(reader);
            XDomNodePrivate* declaration = XDom_documentNewValueNode(
                document, XDom_ProcessingInstructionNode, declarationData);
            if (declaration) {
                XString* target = XString_create_utf8("xml");
                XDom_setPlainName(declaration, target);
                XClassDelete(target);
                declaration->m_line = XXmlStreamReader_lineNumber(reader);
                declaration->m_column = XXmlStreamReader_columnNumber(reader);
                if (!XDom_insertChild(document, declaration, 0))
                    XDom_nodeRelease(declaration);
                else
                    XDom_nodeRelease(declaration);
            }
            XClassDelete(declarationData);
        } else if (token == XXmlStream_DTD) {
            if (foundDtd) {
                /* Qt 的 QDomParser 只允许一个文档类型声明。 */
                XDom_parseResultError_utf8(&result, "XML 文档不允许包含多个 DTD 声明",
                                                  XXmlStreamReader_lineNumber(reader),
                                                  XXmlStreamReader_columnNumber(reader));
                break;
            }
            foundDtd = true;
            XDom_parseDtd(document, reader);
        } else if (token == XXmlStream_StartElement) {
            XDomNodePrivate* element = XDom_parseElement(document, current, reader, useNamespace);
            if (!element) {
                XDom_parseResultError_utf8(&result, "XML 元素创建失败",
                                                  XXmlStreamReader_lineNumber(reader),
                                                  XXmlStreamReader_columnNumber(reader));
                break;
            }
            current = element;
        } else if (token == XXmlStream_EndElement) {
            if (current && current != document)
                current = current->m_parent ? current->m_parent : document;
        } else if (token == XXmlStream_Characters) {
            if (XXmlStreamReader_isWhitespace(reader) &&
                !(options & XDom_PreserveSpacingOnlyNodes)) continue;
            XDom_parseValueNode(document, current,
                XXmlStreamReader_isCDATA(reader) ? XDom_CDATASectionNode : XDom_TextNode,
                XXmlStreamReader_text(reader));
        } else if (token == XXmlStream_Comment) {
            XDom_parseValueNode(document, current, XDom_CommentNode,
                                      XXmlStreamReader_text(reader));
        } else if (token == XXmlStream_ProcessingInstruction) {
            XDomNodePrivate* pi = XDom_documentNewValueNode(document,
                XDom_ProcessingInstructionNode, XXmlStreamReader_processingInstructionData(reader));
            if (pi) {
                XDom_setPlainName(pi, XXmlStreamReader_processingInstructionTarget(reader));
                XDom_insertChild(current, pi, -1);
                XDom_nodeRelease(pi);
            }
        } else if (token == XXmlStream_EntityReference) {
            XDomNodePrivate* reference = XDom_documentNewValueNode(document,
                XDom_EntityReferenceNode, NULL);
            if (reference) {
                XDom_setPlainName(reference, XXmlStreamReader_name(reader));
                XDom_insertChild(current, reference, -1);
                XDom_nodeRelease(reference);
            }
        }
    }
    if (XXmlStreamReader_hasError(reader)) {
        const XString* message = XXmlStreamReader_errorString(reader);
        result.m_errorMessage = message ? XString_create_copy(message) :
                                         XString_create_utf8("XML 解析失败");
        result.m_errorLine = XXmlStreamReader_lineNumber(reader);
        result.m_errorColumn = XXmlStreamReader_columnNumber(reader);
    }
    return result;
}

XDomParseResult XDomDocument_setContent_reader_result(XDomDocument* self,
                                                       XXmlStreamReader* reader,
                                                       unsigned int options)
{
    return XDom_setContentFromReader(XDom_documentEnsureImpl(self), reader, options);
}

XDomParseResult XDomDocument_setContent_result(XDomDocument* self,
                                                const XByteArray* data, unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    if (!document || !data) {
        XDom_parseResultError_utf8(&result, "XML 文档或输入数据为空", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        XDom_parseResultError_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    XXmlStreamReader_addData(reader, data);
    result = XDom_setContentFromReader(document, reader, options);
    XClassDelete(reader);
    return result;
}

XDomParseResult XDomDocument_setContent_utf8_result(XDomDocument* self,
                                                     const char* data, unsigned int options)
{
    XByteArray* bytes = XByteArray_create_utf8(data ? data : "");
    XDomParseResult result = XDomDocument_setContent_result(self, bytes, options);
    XClassDelete(bytes);
    return result;
}

XDomParseResult XDomDocument_setContent_string_result(XDomDocument* self,
                                                        const XString* data,
                                                        unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    if (!document || !data) {
        XDom_parseResultError_utf8(&result, "XML 文档或输入数据为空", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        XDom_parseResultError_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    /* 直接使用 UTF-16 输入重载，匹配 Qt QString setContent 路径。 */
    XXmlStreamReader_addData_string(reader, data);
    result = XDom_setContentFromReader(document, reader, options);
    XClassDelete(reader);
    return result;
}

XDomParseResult XDomDocument_setContent_device_result(XDomDocument* self,
                                                       XIODevice* device,
                                                       unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = XDom_documentEnsureImpl(self);
    if (!document || !device) {
        XDom_parseResultError_utf8(&result, "XML 文档或输入设备为空", -1, -1);
        return result;
    }
    /* Qt 6.8 会为未打开设备尝试只读打开；设备仍由调用者负责关闭。 */
    if (!XIODevice_isOpen(device) && !XIODevice_open_base(device, XIODevice_ReadOnly)) {
        XDom_parseResultError_utf8(&result, "XML 输入设备打开失败", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        XDom_parseResultError_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    XXmlStreamReader_setDevice(reader, device);
    result = XDom_setContentFromReader(document, reader, options);
    XClassDelete(reader);
    return result;
}

static bool XDom_unpackParseResult(XDomParseResult* result,
                                         XString** errorMessage,
                                         int64_t* errorLine, int64_t* errorColumn)
{
    if (!result) return false;
    bool success = XDomParseResult_isSuccess(result);
    if (errorMessage) {
        *errorMessage = result->m_errorMessage;
        result->m_errorMessage = NULL;
    }
    if (errorLine) *errorLine = result->m_errorLine;
    if (errorColumn) *errorColumn = result->m_errorColumn;
    XDomParseResult_deinit(result);
    return success;
}

bool XDomDocument_setContent(XDomDocument* self, const XByteArray* data, unsigned int options,
                             XString** errorMessage, int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_result(self, data, options);
    return XDom_unpackParseResult(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_utf8(XDomDocument* self, const char* data, unsigned int options,
                                  XString** errorMessage, int64_t* errorLine, int64_t* errorColumn)
{
    XByteArray* bytes = XByteArray_create_utf8(data ? data : "");
    bool success = XDomDocument_setContent(self, bytes, options, errorMessage,
                                           errorLine, errorColumn);
    XClassDelete(bytes);
    return success;
}

bool XDomDocument_setContent_string(XDomDocument* self, const XString* data,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_string_result(self, data, options);
    return XDom_unpackParseResult(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_reader(XDomDocument* self, XXmlStreamReader* reader,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_reader_result(self, reader, options);
    return XDom_unpackParseResult(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_device(XDomDocument* self, XIODevice* device,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_device_result(self, device, options);
    return XDom_unpackParseResult(&result, errorMessage, errorLine, errorColumn);
}

XString* XDomDocument_toString(const XDomDocument* self, int indent)
{
    return XDom_serialize(XDom_documentImpl(self), indent);
}

XByteArray* XDomDocument_toByteArray(const XDomDocument* self, int indent)
{
    XString* text = XDomDocument_toString(self, indent);
    XByteArray* result = XByteArray_create_utf8(text ? XString_toUtf8(text) : "");
    XClassDelete(text);
    return result;
}
