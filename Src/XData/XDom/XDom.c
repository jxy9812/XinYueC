#include "XDom.h"

#include "XMemory.h"
#include "XXmlStreamReader.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
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

static XString* xxml_dom_empty_string(void)
{
    static XString* empty = NULL;
    if (!empty) empty = XString_create();
    return empty;
}

static bool xxml_dom_string_equal(const XString* left, const XString* right)
{
    if (!left || !right) return left == right;
    return XString_equals(left, right, XChar_CaseSensitive);
}

static bool xxml_dom_string_equal_utf8(const XString* left, const char* right)
{
    if (!left || !right) return false;
    return XString_equals_utf8(left, right, XChar_CaseSensitive);
}

static void xxml_dom_string_assign(XString** target, const XString* source)
{
    if (!target) return;
    if (!*target) *target = XString_create();
    if (!*target) return;
    if (source) XString_assign(*target, source);
    else XString_clear_base(*target);
}

static void xxml_dom_string_assign_utf8(XString** target, const char* source)
{
    if (!target) return;
    if (!*target) *target = XString_create();
    if (!*target) return;
    XString_assign_utf8(*target, source ? source : "");
}

static void xxml_dom_string_delete(XString** value)
{
    if (value && *value) {
        XString_delete_base(*value);
        *value = NULL;
    }
}

static XDomContext* xxml_dom_context_create(void)
{
    XDomContext* context = (XDomContext*)XMalloc_System(sizeof(XDomContext));
    if (context) context->m_liveNodes = 0;
    return context;
}

static bool xxml_dom_array_reserve(XDomNodePrivate*** array, int* capacity, int required)
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

static bool xxml_dom_array_append(XDomNodePrivate*** array, int* count,
                                  int* capacity, XDomNodePrivate* value)
{
    if (!array || !count || !capacity || !value) return false;
    if (!xxml_dom_array_reserve(array, capacity, *count + 1)) return false;
    (*array)[(*count)++] = value;
    return true;
}

static int xxml_dom_array_index(XDomNodePrivate* const* array, int count,
                                const XDomNodePrivate* value)
{
    if (!array || !value) return -1;
    for (int i = 0; i < count; ++i)
        if (array[i] == value) return i;
    return -1;
}

static void xxml_dom_array_remove_at(XDomNodePrivate** array, int* count, int index)
{
    if (!array || !count || index < 0 || index >= *count) return;
    for (int i = index + 1; i < *count; ++i) array[i - 1] = array[i];
    --*count;
}

static XDomNodePrivate* xxml_dom_node_new_in_context(XDomContext* context,
                                                         XDomNodeType type);
static void xxml_dom_node_retain(XDomNodePrivate* node);
static void xxml_dom_node_release(XDomNodePrivate* node);
static void xxml_dom_node_clear(XDomNodePrivate* node);
static int xxml_dom_child_index(const XDomNodePrivate* parent,
                                const XDomNodePrivate* child);
static bool xxml_dom_append_private(XDomNodePrivate* parent,
                                    XDomNodePrivate* child, int index);
static void xxml_dom_attr_set_value_private(XDomNodePrivate* attr,
                                             const XString* value);

static void xxml_dom_clear_owner_document(XDomNodePrivate* node)
{
    if (!node) return;
    node->m_ownerDocument = NULL;
    if (node->m_type == XDom_DocumentNode && node->m_doctype)
        xxml_dom_clear_owner_document(node->m_doctype);
    for (int i = 0; i < node->m_childCount; ++i)
        xxml_dom_clear_owner_document(node->m_children[i]);
    for (int i = 0; i < node->m_attributeCount; ++i)
        xxml_dom_clear_owner_document(node->m_attributes[i]);
}

static void xxml_dom_set_owner_document(XDomNodePrivate* node,
                                         XDomNodePrivate* document)
{
    if (!node) return;
    node->m_ownerDocument = document;
    if (node->m_type == XDom_DocumentNode && node->m_doctype)
        xxml_dom_set_owner_document(node->m_doctype, document);
    for (int i = 0; i < node->m_childCount; ++i)
        xxml_dom_set_owner_document(node->m_children[i], document);
    for (int i = 0; i < node->m_attributeCount; ++i)
        xxml_dom_set_owner_document(node->m_attributes[i], document);
}

static void xxml_dom_node_free(XDomNodePrivate* node)
{
    if (!node) return;
    for (int i = 0; i < node->m_childCount; ++i) {
        node->m_children[i]->m_parent = NULL;
        if (node->m_type == XDom_DocumentNode)
            xxml_dom_clear_owner_document(node->m_children[i]);
        xxml_dom_node_release(node->m_children[i]);
    }
    for (int i = 0; i < node->m_attributeCount; ++i) {
        node->m_attributes[i]->m_parent = NULL;
        xxml_dom_node_release(node->m_attributes[i]);
    }
    for (int i = 0; i < node->m_entityCount; ++i)
        xxml_dom_node_release(node->m_entities[i]);
    for (int i = 0; i < node->m_notationCount; ++i)
        xxml_dom_node_release(node->m_notations[i]);
    if (node->m_type == XDom_DocumentNode)
        xxml_dom_node_release(node->m_doctype);
    XFree_System(node->m_children);
    XFree_System(node->m_attributes);
    XFree_System(node->m_entities);
    XFree_System(node->m_notations);
    xxml_dom_string_delete(&node->m_name);
    xxml_dom_string_delete(&node->m_value);
    xxml_dom_string_delete(&node->m_namespaceUri);
    xxml_dom_string_delete(&node->m_localName);
    xxml_dom_string_delete(&node->m_prefix);
    xxml_dom_string_delete(&node->m_publicId);
    xxml_dom_string_delete(&node->m_systemId);
    xxml_dom_string_delete(&node->m_notationName);
    xxml_dom_string_delete(&node->m_internalSubset);
    xxml_dom_string_delete(&node->m_textCache);
    xxml_dom_string_delete(&node->m_attributeCache);
    xxml_dom_string_delete(&node->m_attributeNSCache);
    xxml_dom_string_delete(&node->m_documentVersion);
    xxml_dom_string_delete(&node->m_documentEncoding);
    XDomContext* context = node->m_context;
    XFree_System(node);
    if (context && --context->m_liveNodes == 0) XFree_System(context);
}

static void xxml_dom_node_retain(XDomNodePrivate* node)
{
    if (node) ++node->m_refs;
}

static void xxml_dom_node_release(XDomNodePrivate* node)
{
    if (!node) return;
    if (node->m_collectionKind != XDom_CollectionNone) {
        if (--node->m_refs > 0) return;
        if (node->m_collectionKind == XDom_CollectionList) {
            for (int i = 0; i < node->m_itemCount; ++i)
                xxml_dom_node_release(node->m_items[i]);
            XFree_System(node->m_items);
            if (node->m_listOwner) xxml_dom_node_release(node->m_listOwner);
            xxml_dom_string_delete(&node->m_listTagName);
            xxml_dom_string_delete(&node->m_listNamespaceUri);
        } else {
            if (node->m_mapOwner) xxml_dom_node_release(node->m_mapOwner);
        }
        XFree_System(node);
        return;
    }
    if (--node->m_refs == 0) xxml_dom_node_free(node);
}

static XDomNodePrivate* xxml_dom_node_new_in_context(XDomContext* context,
                                                         XDomNodeType type)
{
    bool ownContext = false;
    if (!context) {
        context = xxml_dom_context_create();
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
        xxml_dom_string_delete(&node->m_name);
        xxml_dom_string_delete(&node->m_value);
        xxml_dom_string_delete(&node->m_namespaceUri);
        xxml_dom_string_delete(&node->m_localName);
        xxml_dom_string_delete(&node->m_prefix);
        xxml_dom_string_delete(&node->m_publicId);
        xxml_dom_string_delete(&node->m_systemId);
        xxml_dom_string_delete(&node->m_notationName);
        xxml_dom_string_delete(&node->m_internalSubset);
        xxml_dom_string_delete(&node->m_documentVersion);
        xxml_dom_string_delete(&node->m_documentEncoding);
        XFree_System(node);
        if (ownContext && --context->m_liveNodes == 0) XFree_System(context);
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

static XDomNodePrivate* xxml_dom_node_new(XDomNodeType type)
{
    return xxml_dom_node_new_in_context(NULL, type);
}

static void xxml_dom_set_qualified_name(XDomNodePrivate* node,
                                         const XString* namespaceURI,
                                         const XString* qualifiedName)
{
    if (!node) return;
    xxml_dom_string_assign(&node->m_name, qualifiedName);
    xxml_dom_string_assign(&node->m_namespaceUri, namespaceURI);
    xxml_dom_string_assign(&node->m_prefix, NULL);
    xxml_dom_string_assign(&node->m_localName, qualifiedName);
    node->m_createdWithNamespace = true;
    const char* qName = qualifiedName ? XString_toUtf8(qualifiedName) : "";
    const char* colon = qName ? strchr(qName, ':') : NULL;
    if (colon) {
        XString* prefix = XString_create_with_length_utf8(qName, (size_t)(colon - qName));
        XString* local = XString_create_utf8(colon + 1);
        if (prefix) {
            xxml_dom_string_assign(&node->m_prefix, prefix);
            XString_delete_base(prefix);
        }
        if (local) {
            xxml_dom_string_assign(&node->m_localName, local);
            XString_delete_base(local);
        }
    }
}

/* DOM Level 1 创建的节点不填充 localName、prefix、namespaceURI。 */
static void xxml_dom_set_plain_name(XDomNodePrivate* node, const XString* name)
{
    if (!node) return;
    xxml_dom_string_assign(&node->m_name, name);
    xxml_dom_string_assign(&node->m_namespaceUri, NULL);
    xxml_dom_string_assign(&node->m_localName, NULL);
    xxml_dom_string_assign(&node->m_prefix, NULL);
    node->m_createdWithNamespace = false;
}

static XDomNodePrivate* xxml_dom_document_for_node(XDomNodePrivate* node)
{
    if (!node) return NULL;
    if (node->m_type == XDom_DocumentNode) return node;
    return node->m_ownerDocument;
}

static bool xxml_dom_name_char_allowed(XChar ch, bool first)
{
    if (first)
        return XChar_isLetter(ch) || ch == XChar_from('_') || ch == XChar_from(':');
    return XChar_isLetterOrNumber(ch) || XChar_isMark(ch) ||
           ch == XChar_from('_') || ch == XChar_from('-') ||
           ch == XChar_from('.') || ch == XChar_from(':');
}

static void xxml_dom_append_chars(XString* target, const XChar* chars,
                                  size_t begin, size_t end)
{
    if (!target || !chars) return;
    for (size_t i = begin; i < end; ++i) XString_append_char(target, chars[i]);
}

static XString* xxml_dom_fixed_name(const XString* name, bool namespaces, bool* ok)
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
        if (xxml_dom_name_char_allowed(ch, first)) {
            XString_append_char(local, ch);
            first = false;
        } else if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XString_delete_base(local);
            return NULL;
        }
    }
    if (XString_isEmpty_base(local)) {
        XString_delete_base(local);
        return NULL;
    }

    XString* result = XString_create();
    if (!result) {
        XString_delete_base(local);
        return NULL;
    }
    if (namespaces && prefixLength > 0) {
        xxml_dom_append_chars(result, chars, 0, prefixLength);
        XString_append_char(result, XChar_from(':'));
    }
    XString_append(result, local);
    XString_delete_base(local);
    if (ok) *ok = !XString_isEmpty_base(result);
    return result;
}

static bool xxml_dom_valid_name(const XString* name)
{
    bool ok = false;
    XString* fixed = xxml_dom_fixed_name(name, false, &ok);
    XString_delete_base(fixed);
    return ok;
}

static XDomNodePrivate* xxml_dom_collection_new(int kind)
{
    XDomNodePrivate* collection = (XDomNodePrivate*)XMalloc_System(sizeof(*collection));
    if (!collection) return NULL;
    memset(collection, 0, sizeof(*collection));
    collection->m_refs = 1;
    collection->m_collectionKind = kind;
    return collection;
}

static XDomNodePrivate* xxml_dom_list_new_for_owner(XDomNodePrivate* owner,
                                                    int queryKind,
                                                    const XString* tagName,
                                                    const XString* namespaceURI)
{
    XDomNodePrivate* list = xxml_dom_collection_new(XDom_CollectionList);
    if (!list) return NULL;
    list->m_listOwner = owner;
    list->m_listQueryKind = queryKind;
    if (owner) xxml_dom_node_retain(owner);
    if (tagName) list->m_listTagName = XString_create_copy(tagName);
    if (namespaceURI) list->m_listNamespaceUri = XString_create_copy(namespaceURI);
    if ((tagName && !list->m_listTagName) ||
        (namespaceURI && !list->m_listNamespaceUri)) {
        xxml_dom_node_release(list);
        return NULL;
    }
    return list;
}

static XDomNodePrivate* xxml_dom_map_new(XDomNodePrivate* owner, int mapKind)
{
    XDomNodePrivate* map = xxml_dom_collection_new(XDom_CollectionMap);
    if (!map) return NULL;
    map->m_mapOwner = owner;
    map->m_mapKind = mapKind;
    if (owner) xxml_dom_node_retain(owner);
    return map;
}

static XDomNodePrivate** xxml_dom_map_items(XDomNodePrivate* map, int* count)
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

static XDomNodePrivate* xxml_dom_map_at(XDomNodePrivate* map, int index)
{
    int count = 0;
    XDomNodePrivate** items = xxml_dom_map_items(map, &count);
    return items && index >= 0 && index < count ? items[index] : NULL;
}

static int xxml_dom_map_find_name(XDomNodePrivate* map, const XString* name)
{
    int count = 0;
    XDomNodePrivate** items = xxml_dom_map_items(map, &count);
    for (int i = 0; items && i < count; ++i)
        if (xxml_dom_string_equal(items[i]->m_name, name)) return i;
    return -1;
}

static int xxml_dom_map_find_ns(XDomNodePrivate* map, const XString* namespaceURI,
                                const XString* localName)
{
    int count = 0;
    XDomNodePrivate** items = xxml_dom_map_items(map, &count);
    for (int i = 0; items && i < count; ++i) {
        if (xxml_dom_string_equal(items[i]->m_namespaceUri, namespaceURI) &&
            xxml_dom_string_equal(items[i]->m_localName, localName)) return i;
    }
    return -1;
}

static bool xxml_dom_node_is_descendant(XDomNodePrivate* node,
                                        XDomNodePrivate* possibleParent)
{
    for (XDomNodePrivate* current = possibleParent; current; current = current->m_parent)
        if (current == node) return true;
    return false;
}

static bool xxml_dom_node_child_allowed(XDomNodePrivate* parent,
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

static bool xxml_dom_detach_from_parent(XDomNodePrivate* node)
{
    if (!node || !node->m_parent) return true;
    XDomNodePrivate* parent = node->m_parent;
    if (parent->m_type == XDom_DocumentNode && parent->m_doctype == node) {
        parent->m_doctype = NULL;
        node->m_parent = NULL;
        xxml_dom_node_release(node);
        return true;
    }
    int index = xxml_dom_array_index(parent->m_children, parent->m_childCount, node);
    if (index >= 0) {
        xxml_dom_array_remove_at(parent->m_children, &parent->m_childCount, index);
        node->m_parent = NULL;
        if (parent->m_type == XDom_DocumentNode) xxml_dom_clear_owner_document(node);
        xxml_dom_node_release(node);
        return true;
    }
    index = xxml_dom_array_index(parent->m_attributes, parent->m_attributeCount, node);
    if (index >= 0) {
        xxml_dom_array_remove_at(parent->m_attributes, &parent->m_attributeCount, index);
        node->m_parent = NULL;
        node->m_specified = true;
        xxml_dom_node_release(node);
        return true;
    }
    node->m_parent = NULL;
    return true;
}

static bool xxml_dom_append_private(XDomNodePrivate* parent,
                                    XDomNodePrivate* child, int index)
{
    if (!xxml_dom_node_child_allowed(parent, child) || parent == child ||
        xxml_dom_node_is_descendant(child, parent)) return false;
    /* Qt 将文档类型保存在 QDomDocumentPrivate::type 中，不放入文档子链表。 */
    if (parent->m_type == XDom_DocumentNode && child->m_type == XDom_DocumentTypeNode) {
        if (parent->m_doctype && parent->m_doctype != child) return false;
        if (!xxml_dom_detach_from_parent(child)) return false;
        parent->m_doctype = child;
        xxml_dom_node_retain(child);
        child->m_parent = parent;
        xxml_dom_set_owner_document(child, parent);
        return true;
    }
    XDomNodePrivate* oldParent = child->m_parent;
    int oldIndex = oldParent == parent ?
                   xxml_dom_child_index(parent, child) : -1;
    if (!xxml_dom_detach_from_parent(child)) return false;
    if (index < 0 || index > parent->m_childCount) index = parent->m_childCount;
    if (oldIndex >= 0 && oldIndex < index) --index;
    if (!xxml_dom_array_reserve(&parent->m_children, &parent->m_childCapacity,
                                parent->m_childCount + 1)) return false;
    for (int i = parent->m_childCount; i > index; --i)
        parent->m_children[i] = parent->m_children[i - 1];
    parent->m_children[index] = child;
    ++parent->m_childCount;
    xxml_dom_node_retain(child);
    child->m_parent = parent;
    xxml_dom_set_owner_document(child, xxml_dom_document_for_node(parent));
    return true;
}

/* QDomAttr::setValue() 通过一个直接子 Text 节点保存属性值。 */
static void xxml_dom_attr_set_value_private(XDomNodePrivate* attr,
                                             const XString* value)
{
    if (!attr || attr->m_type != XDom_AttributeNode) return;
    xxml_dom_string_assign(&attr->m_value, value);
    xxml_dom_node_clear(attr);
    XDomNodePrivate* text = xxml_dom_node_new_in_context(attr->m_context, XDom_TextNode);
    if (!text) return;
    xxml_dom_string_assign(&text->m_value, value);
    text->m_ownerDocument = attr->m_ownerDocument;
    if (!xxml_dom_append_private(attr, text, -1)) {
        xxml_dom_node_release(text);
        return;
    }
    xxml_dom_node_release(text);
}

static XDomNodePrivate* xxml_dom_node_clone(XDomNodePrivate* source,
                                               XDomContext* context, bool deep)
{
    if (!source || source->m_collectionKind != XDom_CollectionNone) return NULL;
    XDomNodePrivate* clone = xxml_dom_node_new_in_context(context, source->m_type);
    if (!clone) return NULL;
    xxml_dom_string_assign(&clone->m_name, source->m_name);
    xxml_dom_string_assign(&clone->m_value, source->m_value);
    xxml_dom_string_assign(&clone->m_namespaceUri, source->m_namespaceUri);
    xxml_dom_string_assign(&clone->m_localName, source->m_localName);
    xxml_dom_string_assign(&clone->m_prefix, source->m_prefix);
    xxml_dom_string_assign(&clone->m_publicId, source->m_publicId);
    xxml_dom_string_assign(&clone->m_systemId, source->m_systemId);
    xxml_dom_string_assign(&clone->m_notationName, source->m_notationName);
    xxml_dom_string_assign(&clone->m_internalSubset, source->m_internalSubset);
    xxml_dom_string_assign(&clone->m_documentVersion, source->m_documentVersion);
    xxml_dom_string_assign(&clone->m_documentEncoding, source->m_documentEncoding);
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
            XDomNodePrivate* attr = xxml_dom_node_clone(source->m_attributes[i], context, true);
            if (!attr || !xxml_dom_array_append(&clone->m_attributes,
                    &clone->m_attributeCount, &clone->m_attributeCapacity, attr)) {
                if (attr) xxml_dom_node_release(attr);
                xxml_dom_node_release(clone);
                return NULL;
            }
            xxml_dom_node_retain(attr);
            attr->m_parent = clone;
            xxml_dom_node_release(attr);
        }
    }
    if (deep) {
        for (int i = 0; i < source->m_childCount; ++i) {
            XDomNodePrivate* child = xxml_dom_node_clone(source->m_children[i], context, true);
            if (!child || !xxml_dom_append_private(clone, child, -1)) {
                if (child) xxml_dom_node_release(child);
                xxml_dom_node_release(clone);
                return NULL;
            }
            xxml_dom_node_release(child);
        }
    }
    if (clone->m_type == XDom_DocumentTypeNode) {
        /* DTD 声明同时存在于 childNodes 和两个只读映射中，映射只增加引用。 */
        for (int i = 0; i < clone->m_childCount; ++i) {
            XDomNodePrivate* declaration = clone->m_children[i];
            if (declaration->m_type == XDom_EntityNode) {
                if (xxml_dom_array_append(&clone->m_entities, &clone->m_entityCount,
                                          &clone->m_entityCapacity, declaration))
                    xxml_dom_node_retain(declaration);
            } else if (declaration->m_type == XDom_NotationNode) {
                if (xxml_dom_array_append(&clone->m_notations, &clone->m_notationCount,
                                          &clone->m_notationCapacity, declaration))
                    xxml_dom_node_retain(declaration);
            }
        }
    }
    if (source->m_type == XDom_DocumentNode && source->m_doctype) {
        XDomNodePrivate* type = xxml_dom_node_clone(source->m_doctype, context, true);
        if (!type) {
            xxml_dom_node_release(clone);
            return NULL;
        }
        clone->m_doctype = type;
        type->m_parent = clone;
        xxml_dom_set_owner_document(type, clone);
    }
    return clone;
}

static void xxml_dom_node_clear(XDomNodePrivate* node)
{
    if (!node) return;
    while (node->m_childCount > 0) {
        XDomNodePrivate* child = node->m_children[node->m_childCount - 1];
        --node->m_childCount;
        child->m_parent = NULL;
        if (node->m_type == XDom_DocumentNode) xxml_dom_clear_owner_document(child);
        xxml_dom_node_release(child);
    }
    while (node->m_attributeCount > 0) {
        XDomNodePrivate* attr = node->m_attributes[node->m_attributeCount - 1];
        --node->m_attributeCount;
        attr->m_parent = NULL;
        xxml_dom_node_release(attr);
    }
    if (node->m_type == XDom_DocumentTypeNode) {
        while (node->m_entityCount > 0)
            xxml_dom_node_release(node->m_entities[--node->m_entityCount]);
        while (node->m_notationCount > 0)
            xxml_dom_node_release(node->m_notations[--node->m_notationCount]);
    }
    if (node->m_type == XDom_DocumentNode && node->m_doctype) {
        xxml_dom_node_release(node->m_doctype);
        node->m_doctype = NULL;
    }
}

static void xxml_dom_handle_deinit(XDomNodePrivate** impl)
{
    if (!impl || !*impl) return;
    xxml_dom_node_release(*impl);
    *impl = NULL;
}

static void xxml_dom_handle_copy(XDomNodePrivate** dest, const XDomNodePrivate* src)
{
    if (!dest || !src || dest == &src) return;
    if (*dest == src) return;
    xxml_dom_handle_deinit(dest);
    *dest = (XDomNodePrivate*)src;
    xxml_dom_node_retain(*dest);
}

static void xxml_dom_handle_move(XDomNodePrivate** dest, XDomNodePrivate** src)
{
    if (!dest || !src || dest == src) return;
    if (*dest == *src) return;
    xxml_dom_handle_deinit(dest);
    *dest = *src;
    *src = NULL;
}

static XVtable* xxml_dom_make_vtable(void* copy, void* move, void* deinit)
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
        xxml_dom_handle_copy(&destination->m_impl, source->m_impl); \
    } \
    static void V##Type##_move(XClass* dest, XClass* src) \
    { \
        Type* destination = (Type*)dest; \
        Type* source = (Type*)src; \
        if (!destination || !source) return; \
        if (destination == source) return; \
        if (XClassIsVtableNull((XClass*)source)) return; \
        if (XClassIsVtableNull((XClass*)dest)) Type##_init(destination); \
        xxml_dom_handle_move(&destination->m_impl, &source->m_impl); \
    } \
    static void V##Type##_deinit(XClass* object) \
    { \
        Type* self = (Type*)object; \
        if (self) xxml_dom_handle_deinit(&self->m_impl); \
    } \
    XVtable* Type##_class_init(void) \
    { \
        static XVtable* table = NULL; \
        if (table) return table; \
        table = xxml_dom_make_vtable(V##Type##_copy, V##Type##_move, V##Type##_deinit); \
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
    xxml_dom_node_release(destination->m_document);
    destination->m_isNull = source->m_isNull;
    destination->m_document = source->m_document;
    xxml_dom_node_retain(destination->m_document);
}

static void VXDomImplementation_move(XClass* dest, XClass* src)
{
    XDomImplementation* destination = (XDomImplementation*)dest;
    XDomImplementation* source = (XDomImplementation*)src;
    if (!destination || !source) return;
    if (destination == source) return;
    if (XClassIsVtableNull((XClass*)source)) return;
    if (XClassIsVtableNull(dest)) XDomImplementation_init(destination);
    xxml_dom_node_release(destination->m_document);
    destination->m_isNull = source->m_isNull;
    destination->m_document = source->m_document;
    source->m_document = NULL;
    source->m_isNull = true;
}

static void VXDomImplementation_deinit(XClass* object)
{
    XDomImplementation* self = (XDomImplementation*)object;
    if (self) {
        xxml_dom_node_release(self->m_document);
        self->m_document = NULL;
        self->m_isNull = true;
    }
}

XVtable* XDomImplementation_class_init(void)
{
    static XVtable* table = NULL;
    if (table) return table;
    table = xxml_dom_make_vtable(VXDomImplementation_copy,
                                 VXDomImplementation_move,
                                 VXDomImplementation_deinit);
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
        Type* self = (Type*)XMalloc_System(sizeof(Type)); \
        if (!self) return NULL; \
        Type##_init(self); \
        Set_Class_MemoryFree(self, XFree_System); \
        return self; \
    } \
    Type* Type##_create_copy(const Type* other) \
    { \
        if (!other) return NULL; \
        Type* self = Type##_create(); \
        if (!self) return NULL; \
        Type##_copy_base(self, other); \
        return self; \
    } \
    Type* Type##_create_move(Type* other) \
    { \
        if (!other) return NULL; \
        Type* self = Type##_create(); \
        if (!self) return NULL; \
        Type##_move_base(self, other); \
        return self; \
    } \
    void Type##_deinit_base(Type* self) \
    { \
        if (self) XClass_deinit_base((XClass*)self); \
    } \
    void Type##_delete_base(Type* self) \
    { \
        if (self) XClass_delete_base((XClass*)self); \
    } \
    void Type##_copy_base(Type* dest, const Type* src) \
    { \
        if (dest && src) XClass_copy_base((XClass*)dest, (const XClass*)src); \
    } \
    void Type##_move_base(Type* dest, Type* src) \
    { \
        if (dest && src) XClass_move_base((XClass*)dest, (XClass*)src); \
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

XDomImplementation* XDomImplementation_create(void)
{
    XDomImplementation* self = (XDomImplementation*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XDomImplementation_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XDomImplementation* XDomImplementation_create_copy(const XDomImplementation* other)
{
    if (!other || XClassIsVtableNull((const XClass*)other)) return NULL;
    XDomImplementation* self = XDomImplementation_create();
    if (!self) return NULL;
    XDomImplementation_copy_base(self, other);
    return self;
}

XDomImplementation* XDomImplementation_create_move(XDomImplementation* other)
{
    if (!other || XClassIsVtableNull((XClass*)other)) return NULL;
    XDomImplementation* self = XDomImplementation_create();
    if (!self) return NULL;
    XDomImplementation_move_base(self, other);
    return self;
}

void XDomImplementation_deinit_base(XDomImplementation* self)
{
    if (self) XClass_deinit_base((XClass*)self);
}

void XDomImplementation_delete_base(XDomImplementation* self)
{
    if (self) XClass_delete_base((XClass*)self);
}

void XDomImplementation_copy_base(XDomImplementation* dest, const XDomImplementation* src)
{
    if (dest && src) XClass_copy_base((XClass*)dest, (const XClass*)src);
}

void XDomImplementation_move_base(XDomImplementation* dest, XDomImplementation* src)
{
    if (dest && src) XClass_move_base((XClass*)dest, (XClass*)src);
}

/* QDomDocument 默认构造后是 null；首次工厂调用或 setContent 时再分配实现。 */
void XDomDocument_init(XDomDocument* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init(&self->m_class);
    XClassSetVtable(self, XDomDocument);
}

XDomDocument* XDomDocument_create(void)
{
    XDomDocument* self = (XDomDocument*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XDomDocument_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XDomDocument* XDomDocument_create_copy(const XDomDocument* other)
{
    if (!other || XClassIsVtableNull((const XClass*)other)) return NULL;
    XDomDocument* self = XDomDocument_create();
    if (!self) return NULL;
    XDomDocument_copy_base(self, other);
    return self;
}

XDomDocument* XDomDocument_create_move(XDomDocument* other)
{
    if (!other || XClassIsVtableNull((XClass*)other)) return NULL;
    XDomDocument* self = XDomDocument_create();
    if (!self) return NULL;
    XDomDocument_move_base(self, other);
    return self;
}

void XDomDocument_deinit_base(XDomDocument* self)
{
    if (self) XClass_deinit_base((XClass*)self);
}

void XDomDocument_delete_base(XDomDocument* self)
{
    if (self) XClass_delete_base((XClass*)self);
}

void XDomDocument_copy_base(XDomDocument* dest, const XDomDocument* src)
{
    if (dest && src) XClass_copy_base((XClass*)dest, (const XClass*)src);
}

void XDomDocument_move_base(XDomDocument* dest, XDomDocument* src)
{
    if (dest && src) XClass_move_base((XClass*)dest, (XClass*)src);
}

static void xxml_dom_assign_new_handle(XDomNodePrivate** target,
                                        XDomNodePrivate* source)
{
    if (!target) return;
    xxml_dom_handle_deinit(target);
    *target = source;
    xxml_dom_node_retain(source);
}

static XDomNode* xxml_dom_wrap_node(XDomNodePrivate* node)
{
    if (!node) return XDomNode_create();
    XDomNode* result = XDomNode_create();
    if (result) xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomNode* xxml_dom_wrap_node_as_node(XDomNodePrivate* node)
{
    return xxml_dom_wrap_node(node);
}

static XDomElement* xxml_dom_wrap_element(XDomNodePrivate* node)
{
    XDomElement* result = XDomElement_create();
    if (result && node && node->m_type == XDom_ElementNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomAttr* xxml_dom_wrap_attr(XDomNodePrivate* node)
{
    XDomAttr* result = XDomAttr_create();
    if (result && node && node->m_type == XDom_AttributeNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomText* xxml_dom_wrap_text(XDomNodePrivate* node)
{
    XDomText* result = XDomText_create();
    if (result && node && node->m_type == XDom_TextNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomCDATASection* xxml_dom_wrap_cdata(XDomNodePrivate* node)
{
    XDomCDATASection* result = XDomCDATASection_create();
    if (result && node && node->m_type == XDom_CDATASectionNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomComment* xxml_dom_wrap_comment(XDomNodePrivate* node)
{
    XDomComment* result = XDomComment_create();
    if (result && node && node->m_type == XDom_CommentNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomCharacterData* xxml_dom_wrap_character_data(XDomNodePrivate* node)
{
    XDomCharacterData* result = XDomCharacterData_create();
    if (result && node && (node->m_type == XDom_TextNode ||
                           node->m_type == XDom_CommentNode))
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomDocument* xxml_dom_wrap_document(XDomNodePrivate* node)
{
    XDomDocument* result = XDomDocument_create();
    if (result && node && node->m_type == XDom_DocumentNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomDocumentType* xxml_dom_wrap_doctype(XDomNodePrivate* node)
{
    XDomDocumentType* result = XDomDocumentType_create();
    if (result && node && node->m_type == XDom_DocumentTypeNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomDocumentFragment* xxml_dom_wrap_fragment(XDomNodePrivate* node)
{
    XDomDocumentFragment* result = XDomDocumentFragment_create();
    if (result && node && node->m_type == XDom_DocumentFragmentNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomEntityReference* xxml_dom_wrap_entity_reference(XDomNodePrivate* node)
{
    XDomEntityReference* result = XDomEntityReference_create();
    if (result && node && node->m_type == XDom_EntityReferenceNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomProcessingInstruction* xxml_dom_wrap_pi(XDomNodePrivate* node)
{
    XDomProcessingInstruction* result = XDomProcessingInstruction_create();
    if (result && node && node->m_type == XDom_ProcessingInstructionNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomEntity* xxml_dom_wrap_entity(XDomNodePrivate* node)
{
    XDomEntity* result = XDomEntity_create();
    if (result && node && node->m_type == XDom_EntityNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomNotation* xxml_dom_wrap_notation(XDomNodePrivate* node)
{
    XDomNotation* result = XDomNotation_create();
    if (result && node && node->m_type == XDom_NotationNode)
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}

static XDomNodeList* xxml_dom_wrap_list(XDomNodePrivate* list)
{
    XDomNodeList* result = XDomNodeList_create();
    if (result) xxml_dom_assign_new_handle(&result->m_impl, list);
    return result;
}

static XDomNamedNodeMap* xxml_dom_wrap_map(XDomNodePrivate* map)
{
    XDomNamedNodeMap* result = XDomNamedNodeMap_create();
    if (result) xxml_dom_assign_new_handle(&result->m_impl, map);
    return result;
}

static void xxml_dom_set_default_creation_value(XDomNodePrivate* node,
                                                 const XString* value)
{
    if (node && value) xxml_dom_string_assign(&node->m_value, value);
}

static bool xxml_dom_xml_char_allowed(XChar ch)
{
    if (XChar_isNonCharacter(ch) || XChar_isControl(ch))
        return ch == XChar_from(0x09) || ch == XChar_from(0x0a) || ch == XChar_from(0x0d);
    return (ch >= XChar_from(0x20) && ch <= XChar_from(0xd7ff)) ||
           (ch >= XChar_from(0xe000) && ch <= XChar_from(0xfffd));
}

static XString* xxml_dom_fixed_value(const XString* value, XDomNodeType type, bool* ok)
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
        bool valid = xxml_dom_xml_char_allowed(ch);
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
            XString_delete_base(result);
            return NULL;
        }
    }

    const char* forbidden = NULL;
    if (type == XDom_CommentNode) forbidden = "--";
    else if (type == XDom_ProcessingInstructionNode) forbidden = "?>";
    if (type == XDom_CDATASectionNode) forbidden = "]]>";
    if (forbidden) {
        size_t forbiddenLength = strlen(forbidden);
        for (;;) {
            int64_t position = XString_indexOf_utf8(result, forbidden, 0,
                                                      XChar_CaseSensitive);
            if (position < 0) break;
            if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
                XString_delete_base(result);
                return NULL;
            }
            XString_remove_base(result, (size_t)position, forbiddenLength);
        }
    }
    if (ok) *ok = true;
    return result;
}

/* ==================== 公共句柄与节点树辅助函数 ==================== */

static XDomNodePrivate* xxml_dom_handle_impl(const void* handle)
{
    const XClass* base = (const XClass*)handle;
    if (!base || !base->m_vtable) return NULL;
    return ((const XDomNode*)handle)->m_impl;
}

static bool xxml_dom_node_is_character_data(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CommentNode);
}

/* 字符数据 API 仍可直接作用于 CDATA；Qt 的 isCharacterData 查询则排除 CDATA。 */
static bool xxml_dom_node_is_character_data_payload(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode ||
                    node->m_type == XDom_CommentNode);
}

static XDomContext* xxml_dom_context_for_node(XDomNodePrivate* node)
{
    return node ? node->m_context : NULL;
}

static int xxml_dom_child_index(const XDomNodePrivate* parent,
                                const XDomNodePrivate* child)
{
    if (!parent || !child) return -1;
    return xxml_dom_array_index(parent->m_children, parent->m_childCount, child);
}

static bool xxml_dom_string_matches(const XString* value, const XString* filter)
{
    if (!filter) return true;
    if (xxml_dom_string_equal_utf8(filter, "*")) return true;
    return xxml_dom_string_equal(value, filter);
}

static bool xxml_dom_element_matches(const XDomNodePrivate* node,
                                     const XString* tagName,
                                     const XString* namespaceURI)
{
    if (!node || node->m_type != XDom_ElementNode) return false;
    return xxml_dom_string_matches(node->m_name, tagName) &&
           xxml_dom_string_matches(node->m_namespaceUri, namespaceURI);
}

static void xxml_dom_node_invalidate_text_cache(XDomNodePrivate* node)
{
    for (XDomNodePrivate* current = node; current; current = current->m_parent)
        xxml_dom_string_delete(&current->m_textCache);
}

static bool xxml_dom_append_fragment(XDomNodePrivate* parent,
                                     XDomNodePrivate* fragment, int index)
{
    if (!parent || !fragment || fragment->m_type != XDom_DocumentFragmentNode)
        return false;
    int insertIndex = index;
    while (fragment->m_childCount > 0) {
        XDomNodePrivate* child = fragment->m_children[0];
        if (!xxml_dom_append_private(parent, child, insertIndex)) return false;
        if (insertIndex >= 0) ++insertIndex;
    }
    return true;
}

static bool xxml_dom_insert_child(XDomNodePrivate* parent,
                                  XDomNodePrivate* child, int index)
{
    if (!parent || !child) return false;
    if (child->m_type == XDom_DocumentFragmentNode)
        return xxml_dom_append_fragment(parent, child, index);
    if (!xxml_dom_append_private(parent, child, index)) return false;
    xxml_dom_node_invalidate_text_cache(parent);
    return true;
}

static XDomNodePrivate* xxml_dom_create_node_for_document(XDomNodePrivate* document,
                                                              XDomNodeType type)
{
    return xxml_dom_node_new_in_context(document ? document->m_context : NULL, type);
}

static bool xxml_dom_implementation_is_valid(const XDomImplementation* self)
{
    return self && !XClassIsVtableNull((const XClass*)self) && !self->m_isNull;
}

/* 与 Qt fixedPubidLiteral/fixedSystemLiteral 对齐的 DTD 标识符清理。 */
static bool xxml_dom_public_id_char(XChar ch)
{
    if (ch == XChar_from(' ') || ch == XChar_from('\t') ||
        ch == XChar_from('\n') || ch == XChar_from('\r')) return true;
    if ((ch >= XChar_from('a') && ch <= XChar_from('z')) ||
        (ch >= XChar_from('A') && ch <= XChar_from('Z')) ||
        (ch >= XChar_from('0') && ch <= XChar_from('9'))) return true;
    return strchr("-'()+,./:=?;!*#@$_%", (int)ch) != NULL;
}

static XString* xxml_dom_fixed_public_id(const XString* value, bool* ok)
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
        if (xxml_dom_public_id_char(chars[i])) XString_append_char(result, chars[i]);
        else if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XString_delete_base(result);
            return NULL;
        }
    }
    if (XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0) {
        if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode) {
            XString_delete_base(result);
            return NULL;
        }
        XString_replace_utf8(result, "'", "", XChar_CaseSensitive);
    }
    if (ok) *ok = true;
    return result;
}

static XString* xxml_dom_fixed_system_id(const XString* value, bool* ok)
{
    if (ok) *ok = false;
    XString* result = value ? XString_create_copy(value) : XString_create();
    if (!result) return NULL;
    if (g_xxml_dom_invalid_data_policy == XDom_ReturnNullNode &&
        XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0) {
        XString_delete_base(result);
        return NULL;
    }
    if (g_xxml_dom_invalid_data_policy == XDom_DropInvalidChars &&
        XString_indexOf_utf8(result, "'", 0, XChar_CaseSensitive) >= 0 &&
        XString_indexOf_utf8(result, "\"", 0, XChar_CaseSensitive) >= 0)
        XString_replace_utf8(result, "'", "", XChar_CaseSensitive);
    if (ok) *ok = true;
    return result;
}

static XString* xxml_dom_serialize(XDomNodePrivate* node, int indent);
static XString* xxml_dom_serialize_for_policy(XDomNodePrivate* node, int indent,
                                               XDomEncodingPolicy policy);

/* ==================== QDomNode ==================== */

XDomNode* XDomNode_insertBefore(XDomNode* self, const XDomNode* newChild,
                                      const XDomNode* refChild)
{
    XDomNodePrivate* parent = xxml_dom_handle_impl(self);
    XDomNodePrivate* child = xxml_dom_handle_impl(newChild);
    XDomNodePrivate* reference = xxml_dom_handle_impl(refChild);
    /* Qt 兼容：空参考节点表示插入到第一个子节点之前。 */
    int index = reference ? xxml_dom_child_index(parent, reference) : 0;
    if (!parent || !child || child == reference || (reference && index < 0) ||
        !xxml_dom_insert_child(parent, child, index))
        return xxml_dom_wrap_node(NULL);
    return xxml_dom_wrap_node(child);
}

XDomNode* XDomNode_insertAfter(XDomNode* self, const XDomNode* newChild,
                                     const XDomNode* refChild)
{
    XDomNodePrivate* parent = xxml_dom_handle_impl(self);
    XDomNodePrivate* child = xxml_dom_handle_impl(newChild);
    XDomNodePrivate* reference = xxml_dom_handle_impl(refChild);
    int index = reference ? xxml_dom_child_index(parent, reference) : -1;
    if (reference && index >= 0) ++index;
    if (!parent || !child || child == reference || (reference && index < 0) ||
        !xxml_dom_insert_child(parent, child, index))
        return xxml_dom_wrap_node(NULL);
    return xxml_dom_wrap_node(child);
}

XDomNode* XDomNode_replaceChild(XDomNode* self, const XDomNode* newChild,
                                      const XDomNode* oldChild)
{
    XDomNodePrivate* parent = xxml_dom_handle_impl(self);
    XDomNodePrivate* child = xxml_dom_handle_impl(newChild);
    XDomNodePrivate* old = xxml_dom_handle_impl(oldChild);
    int index = xxml_dom_child_index(parent, old);
    if (!parent || !child || !old || child == old || index < 0)
        return xxml_dom_wrap_node(NULL);

    XDomNode* result = xxml_dom_wrap_node(old);
    if (!xxml_dom_detach_from_parent(old) || !xxml_dom_insert_child(parent, child, index)) {
        if (!old->m_parent) xxml_dom_insert_child(parent, old, index);
        XDomNode_delete_base(result);
        return xxml_dom_wrap_node(NULL);
    }
    return result;
}

XDomNode* XDomNode_removeChild(XDomNode* self, const XDomNode* oldChild)
{
    XDomNodePrivate* parent = xxml_dom_handle_impl(self);
    XDomNodePrivate* child = xxml_dom_handle_impl(oldChild);
    if (!parent || !child || xxml_dom_child_index(parent, child) < 0)
        return xxml_dom_wrap_node(NULL);
    XDomNode* result = xxml_dom_wrap_node(child);
    if (!xxml_dom_detach_from_parent(child)) {
        XDomNode_delete_base(result);
        return xxml_dom_wrap_node(NULL);
    }
    xxml_dom_node_invalidate_text_cache(parent);
    return result;
}

XDomNode* XDomNode_appendChild(XDomNode* self, const XDomNode* newChild)
{
    /* insertAfter(..., NULL) 的既有语义是追加到末尾。 */
    return XDomNode_insertAfter(self, newChild, NULL);
}

bool XDomNode_hasChildNodes(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_childCount > 0;
}

XDomNode* XDomNode_cloneNode(const XDomNode* self, bool deep)
{
    XDomNodePrivate* source = xxml_dom_handle_impl(self);
    if (!source) return xxml_dom_wrap_node(NULL);
    XDomNodePrivate* clone = xxml_dom_node_clone(source, source->m_context, deep);
    if (!clone) return xxml_dom_wrap_node(NULL);
    /* 克隆节点仍归属原文档；文档自身的克隆则成为新树的归属文档。 */
    xxml_dom_set_owner_document(clone,
                                source->m_type == XDom_DocumentNode ? clone :
                                xxml_dom_document_for_node(source));
    XDomNode* result = xxml_dom_wrap_node(clone);
    xxml_dom_node_release(clone);
    return result;
}

static void xxml_dom_normalize_private(XDomNodePrivate* node)
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
        xxml_dom_array_remove_at(node->m_children, &node->m_childCount, i);
        child->m_parent = NULL;
        xxml_dom_node_release(child);
        --i;
    }
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomNode_normalize(XDomNode* self)
{
    xxml_dom_normalize_private(xxml_dom_handle_impl(self));
}

bool XDomNode_isSupported(const XDomNode* self, const XString* feature,
                             const XString* version)
{
    (void)self;
    return feature && xxml_dom_string_equal_utf8(feature, "XML") &&
           (!version || XString_isEmpty_base(version) ||
            xxml_dom_string_equal_utf8(version, "1.0"));
}

bool XDomNode_isSupported_utf8(const XDomNode* self, const char* feature,
                                  const char* version)
{
    XString* featureString = XString_create_utf8(feature);
    XString* versionString = XString_create_utf8(version);
    bool result = XDomNode_isSupported(self, featureString, versionString);
    XString_delete_base(featureString);
    XString_delete_base(versionString);
    return result;
}

const XString* XDomNode_nodeName(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_name ? node->m_name : xxml_dom_empty_string();
}

XDomNodeType XDomNode_nodeType(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node ? node->m_type : XDom_BaseNode;
}

XDomNode* XDomNode_parentNode(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return xxml_dom_wrap_node(node ? node->m_parent : NULL);
}

XDomNodeList* XDomNode_childNodes(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    XDomNodePrivate* list = xxml_dom_list_new_for_owner(node, XDom_ListChildren, NULL, NULL);
    XDomNodeList* result = xxml_dom_wrap_list(list);
    xxml_dom_node_release(list);
    return result;
}

XDomNode* XDomNode_firstChild(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return xxml_dom_wrap_node(node && node->m_childCount ? node->m_children[0] : NULL);
}

XDomNode* XDomNode_lastChild(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return xxml_dom_wrap_node(node && node->m_childCount ?
                              node->m_children[node->m_childCount - 1] : NULL);
}

XDomNode* XDomNode_previousSibling(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    int index = node ? xxml_dom_child_index(node->m_parent, node) : -1;
    return xxml_dom_wrap_node(index > 0 ? node->m_parent->m_children[index - 1] : NULL);
}

XDomNode* XDomNode_nextSibling(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    int index = node ? xxml_dom_child_index(node->m_parent, node) : -1;
    return xxml_dom_wrap_node(index >= 0 && index + 1 < node->m_parent->m_childCount ?
                              node->m_parent->m_children[index + 1] : NULL);
}

XDomNamedNodeMap* XDomNode_attributes(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    XDomNodePrivate* map = xxml_dom_map_new(node && node->m_type == XDom_ElementNode ?
                                                node : NULL, XDom_MapAttributes);
    XDomNamedNodeMap* result = xxml_dom_wrap_map(map);
    xxml_dom_node_release(map);
    return result;
}

XDomDocument* XDomNode_ownerDocument(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return xxml_dom_wrap_document(node ? xxml_dom_document_for_node(node) : NULL);
}

const XString* XDomNode_namespaceURI(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_createdWithNamespace && node->m_namespaceUri ?
        node->m_namespaceUri : xxml_dom_empty_string();
}

const XString* XDomNode_localName(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_createdWithNamespace && node->m_localName ?
        node->m_localName : xxml_dom_empty_string();
}

bool XDomNode_hasAttributes(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_attributeCount > 0;
}

const XString* XDomNode_nodeValue(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node || node->m_type == XDom_ElementNode || node->m_type == XDom_DocumentNode ||
        node->m_type == XDom_DocumentTypeNode || node->m_type == XDom_DocumentFragmentNode)
        return xxml_dom_empty_string();
    return node->m_value ? node->m_value : xxml_dom_empty_string();
}

void XDomNode_setNodeValue(XDomNode* self, const XString* value)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node || node->m_type == XDom_ElementNode || node->m_type == XDom_DocumentNode ||
        node->m_type == XDom_DocumentTypeNode || node->m_type == XDom_DocumentFragmentNode)
        return;
    if (node->m_type == XDom_AttributeNode) {
        xxml_dom_attr_set_value_private(node, value);
        node->m_specified = true;
        return;
    }
    xxml_dom_string_assign(&node->m_value, value);
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomNode_setNodeValue_utf8(XDomNode* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomNode_setNodeValue(self, text);
    XString_delete_base(text);
}

const XString* XDomNode_prefix(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_createdWithNamespace && node->m_prefix ?
        node->m_prefix : xxml_dom_empty_string();
}

void XDomNode_setPrefix(XDomNode* self, const XString* prefix)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node || !node->m_createdWithNamespace || !node->m_prefix ||
        (node->m_type != XDom_ElementNode && node->m_type != XDom_AttributeNode)) return;
    /* QDomNode::setPrefix 只修改 prefix，不重建 nodeName。 */
    xxml_dom_string_assign(&node->m_prefix, prefix);
}

void XDomNode_setPrefix_utf8(XDomNode* self, const char* prefix)
{
    XString* text = XString_create_utf8(prefix);
    XDomNode_setPrefix(self, text);
    XString_delete_base(text);
}

XDomNode* XDomNode_namedItem(const XDomNode* self, const XString* name)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node) return xxml_dom_wrap_node(NULL);
    for (int i = 0; i < node->m_childCount; ++i)
        if (xxml_dom_string_equal(node->m_children[i]->m_name, name))
            return xxml_dom_wrap_node(node->m_children[i]);
    return xxml_dom_wrap_node(NULL);
}

XDomNode* XDomNode_namedItem_utf8(const XDomNode* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNode_namedItem(self, text);
    XString_delete_base(text);
    return result;
}

bool XDomNode_isNull(const XDomNode* self)
{
    return xxml_dom_handle_impl(self) == NULL;
}

bool XDomNode_equals(const XDomNode* left, const XDomNode* right)
{
    return xxml_dom_handle_impl(left) == xxml_dom_handle_impl(right);
}

void XDomNode_clear(XDomNode* self)
{
    if (!self) return;
    /* Qt 兼容：clear 只清空当前隐式共享句柄，不能修改共享的树。 */
    xxml_dom_handle_deinit(&self->m_impl);
}

#define XDOM_DEFINE_NODE_TYPE_CHECK(Name, Value) \
    bool XDomNode_##Name(const XDomNode* self) \
    { \
        XDomNodePrivate* node = xxml_dom_handle_impl(self); \
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
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode);
}
XDOM_DEFINE_NODE_TYPE_CHECK(isEntity, XDom_EntityNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isNotation, XDom_NotationNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isProcessingInstruction, XDom_ProcessingInstructionNode)
XDOM_DEFINE_NODE_TYPE_CHECK(isComment, XDom_CommentNode)

bool XDomNode_isCharacterData(const XDomNode* self)
{
    return xxml_dom_node_is_character_data(xxml_dom_handle_impl(self));
}

XDomElement* XDomNode_toElement(const XDomNode* self) { return xxml_dom_wrap_element(xxml_dom_handle_impl(self)); }
XDomAttr* XDomNode_toAttr(const XDomNode* self) { return xxml_dom_wrap_attr(xxml_dom_handle_impl(self)); }
XDomText* XDomNode_toText(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    XDomText* result = XDomText_create();
    if (result && node && (node->m_type == XDom_TextNode ||
                           node->m_type == XDom_CDATASectionNode))
        xxml_dom_assign_new_handle(&result->m_impl, node);
    return result;
}
XDomCDATASection* XDomNode_toCDATASection(const XDomNode* self) { return xxml_dom_wrap_cdata(xxml_dom_handle_impl(self)); }
XDomComment* XDomNode_toComment(const XDomNode* self) { return xxml_dom_wrap_comment(xxml_dom_handle_impl(self)); }
XDomCharacterData* XDomNode_toCharacterData(const XDomNode* self) { return xxml_dom_wrap_character_data(xxml_dom_handle_impl(self)); }
XDomDocument* XDomNode_toDocument(const XDomNode* self) { return xxml_dom_wrap_document(xxml_dom_handle_impl(self)); }
XDomDocumentType* XDomNode_toDocumentType(const XDomNode* self) { return xxml_dom_wrap_doctype(xxml_dom_handle_impl(self)); }
XDomDocumentFragment* XDomNode_toDocumentFragment(const XDomNode* self) { return xxml_dom_wrap_fragment(xxml_dom_handle_impl(self)); }
XDomEntityReference* XDomNode_toEntityReference(const XDomNode* self) { return xxml_dom_wrap_entity_reference(xxml_dom_handle_impl(self)); }
XDomEntity* XDomNode_toEntity(const XDomNode* self) { return xxml_dom_wrap_entity(xxml_dom_handle_impl(self)); }
XDomNotation* XDomNode_toNotation(const XDomNode* self) { return xxml_dom_wrap_notation(xxml_dom_handle_impl(self)); }
XDomProcessingInstruction* XDomNode_toProcessingInstruction(const XDomNode* self) { return xxml_dom_wrap_pi(xxml_dom_handle_impl(self)); }

static XDomElement* xxml_dom_find_element(const XDomNodePrivate* start, int direction,
                                              const XString* tagName, const XString* namespaceURI)
{
    if (!start) return xxml_dom_wrap_element(NULL);
    int index = direction > 0 ? 0 : start->m_childCount - 1;
    for (; index >= 0 && index < start->m_childCount; index += direction) {
        XDomNodePrivate* child = start->m_children[index];
        if (xxml_dom_element_matches(child, tagName, namespaceURI)) return xxml_dom_wrap_element(child);
    }
    return xxml_dom_wrap_element(NULL);
}

XDomElement* XDomNode_firstChildElement(const XDomNode* self, const XString* tagName,
                                              const XString* namespaceURI)
{
    return xxml_dom_find_element(xxml_dom_handle_impl(self), 1, tagName, namespaceURI);
}

XDomElement* XDomNode_firstChildElement_utf8(const XDomNode* self, const char* tagName,
                                                   const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_firstChildElement(self, tag, ns);
    XString_delete_base(tag); XString_delete_base(ns);
    return result;
}

XDomElement* XDomNode_lastChildElement(const XDomNode* self, const XString* tagName,
                                             const XString* namespaceURI)
{
    return xxml_dom_find_element(xxml_dom_handle_impl(self), -1, tagName, namespaceURI);
}

XDomElement* XDomNode_lastChildElement_utf8(const XDomNode* self, const char* tagName,
                                                  const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_lastChildElement(self, tag, ns);
    XString_delete_base(tag); XString_delete_base(ns);
    return result;
}

XDomElement* XDomNode_previousSiblingElement_utf8(const XDomNode* self,
                                                        const char* tagName,
                                                        const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_previousSiblingElement(self, tag, ns);
    XString_delete_base(tag); XString_delete_base(ns);
    return result;
}

static XDomElement* xxml_dom_find_sibling_element(const XDomNodePrivate* node, int direction,
                                                      const XString* tagName, const XString* namespaceURI)
{
    if (!node || !node->m_parent) return xxml_dom_wrap_element(NULL);
    int index = xxml_dom_child_index(node->m_parent, node) + direction;
    for (; index >= 0 && index < node->m_parent->m_childCount; index += direction) {
        XDomNodePrivate* sibling = node->m_parent->m_children[index];
        if (xxml_dom_element_matches(sibling, tagName, namespaceURI)) return xxml_dom_wrap_element(sibling);
    }
    return xxml_dom_wrap_element(NULL);
}

XDomElement* XDomNode_previousSiblingElement(const XDomNode* self, const XString* tagName,
                                                    const XString* namespaceURI)
{
    return xxml_dom_find_sibling_element(xxml_dom_handle_impl(self), -1, tagName, namespaceURI);
}

XDomElement* XDomNode_nextSiblingElement(const XDomNode* self, const XString* tagName,
                                                const XString* namespaceURI)
{
    return xxml_dom_find_sibling_element(xxml_dom_handle_impl(self), 1, tagName, namespaceURI);
}

XDomElement* XDomNode_nextSiblingElement_utf8(const XDomNode* self,
                                                    const char* tagName,
                                                    const char* namespaceURI)
{
    XString* tag = XString_create_utf8(tagName);
    XString* ns = XString_create_utf8(namespaceURI);
    XDomElement* result = XDomNode_nextSiblingElement(self, tag, ns);
    XString_delete_base(tag); XString_delete_base(ns);
    return result;
}

int64_t XDomNode_lineNumber(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node ? node->m_line : -1;
}

int64_t XDomNode_columnNumber(const XDomNode* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node ? node->m_column : -1;
}

XString* XDomNode_toString(const XDomNode* self, int indent)
{
    return xxml_dom_serialize(xxml_dom_handle_impl(self), indent);
}

bool XDomNode_save(const XDomNode* self, XIODevice* device, int indent,
                   XDomEncodingPolicy encodingPolicy)
{
    if (!self || !device || XDomNode_isNull(self)) return false;
    XString* text = xxml_dom_serialize_for_policy(xxml_dom_handle_impl(self), indent,
                                                   encodingPolicy);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;
    size_t length = text ? XString_toUtf8_length(text) : 0;
    int64_t written = (utf8 || length == 0) ? XIODevice_write_1(
        device, utf8 ? utf8 : "", (int64_t)length) : -1;
    XString_delete_base(text);
    return written == (int64_t)length;
}

/* ==================== QDomNodeList ==================== */

static bool xxml_dom_list_matches(const XDomNodePrivate* list,
                                  const XDomNodePrivate* node)
{
    return list && node && xxml_dom_element_matches(node, list->m_listTagName,
                                                     list->m_listNamespaceUri);
}

static int xxml_dom_list_count_descendants(const XDomNodePrivate* list,
                                           const XDomNodePrivate* parent)
{
    if (!list || !parent) return 0;
    int count = 0;
    for (int i = 0; i < parent->m_childCount; ++i) {
        const XDomNodePrivate* child = parent->m_children[i];
        if (xxml_dom_list_matches(list, child) && count < INT_MAX) ++count;
        int descendants = xxml_dom_list_count_descendants(list, child);
        if (descendants > INT_MAX - count) count = INT_MAX;
        else count += descendants;
    }
    return count;
}

static XDomNodePrivate* xxml_dom_list_at_descendant(const XDomNodePrivate* list,
                                                    const XDomNodePrivate* parent,
                                                    int* index)
{
    if (!list || !parent || !index || *index < 0) return NULL;
    for (int i = 0; i < parent->m_childCount; ++i) {
        XDomNodePrivate* child = parent->m_children[i];
        if (xxml_dom_list_matches(list, child)) {
            if (*index == 0) return child;
            --*index;
        }
        XDomNodePrivate* result = xxml_dom_list_at_descendant(list, child, index);
        if (result) return result;
    }
    return NULL;
}

XDomNode* XDomNodeList_item(const XDomNodeList* self, int index)
{
    XDomNodePrivate* list = xxml_dom_handle_impl(self);
    if (!list || list->m_collectionKind != XDom_CollectionList || index < 0)
        return xxml_dom_wrap_node(NULL);
    if (list->m_listQueryKind == XDom_ListChildren)
        return xxml_dom_wrap_node(list->m_listOwner && index < list->m_listOwner->m_childCount ?
                                  list->m_listOwner->m_children[index] : NULL);
    if (list->m_listQueryKind == XDom_ListElements) {
        XDomNodePrivate* result = xxml_dom_list_at_descendant(list, list->m_listOwner, &index);
        return xxml_dom_wrap_node(result);
    }
    return xxml_dom_wrap_node(NULL);
}

XDomNode* XDomNodeList_at(const XDomNodeList* self, int index)
{
    return XDomNodeList_item(self, index);
}

int XDomNodeList_length(const XDomNodeList* self)
{
    XDomNodePrivate* list = xxml_dom_handle_impl(self);
    if (!list || list->m_collectionKind != XDom_CollectionList || !list->m_listOwner) return 0;
    if (list->m_listQueryKind == XDom_ListChildren) return list->m_listOwner->m_childCount;
    return list->m_listQueryKind == XDom_ListElements ?
        xxml_dom_list_count_descendants(list, list->m_listOwner) : 0;
}

int XDomNodeList_count(const XDomNodeList* self) { return XDomNodeList_length(self); }
int XDomNodeList_size(const XDomNodeList* self) { return XDomNodeList_length(self); }
bool XDomNodeList_isEmpty(const XDomNodeList* self) { return XDomNodeList_length(self) == 0; }
bool XDomNodeList_equals(const XDomNodeList* left, const XDomNodeList* right)
{
    XDomNodePrivate* leftList = xxml_dom_handle_impl(left);
    XDomNodePrivate* rightList = xxml_dom_handle_impl(right);
    if (leftList == rightList) return true;
    if (!leftList || !rightList ||
        leftList->m_collectionKind != XDom_CollectionList ||
        rightList->m_collectionKind != XDom_CollectionList)
        return false;
    /* Qt 的 QDomNodeList::operator== 不比较 namespaceURI，只比较节点和 tagName。 */
    return leftList->m_listOwner == rightList->m_listOwner &&
           leftList->m_listQueryKind == rightList->m_listQueryKind &&
           xxml_dom_string_equal(leftList->m_listTagName, rightList->m_listTagName);
}

/* ==================== QDomNamedNodeMap ==================== */

XDomNode* XDomNamedNodeMap_namedItem(const XDomNamedNodeMap* self, const XString* name)
{
    XDomNodePrivate* map = xxml_dom_handle_impl(self);
    int index = xxml_dom_map_find_name(map, name);
    return xxml_dom_wrap_node(index >= 0 ? xxml_dom_map_at(map, index) : NULL);
}

XDomNode* XDomNamedNodeMap_namedItem_utf8(const XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNamedNodeMap_namedItem(self, text);
    XString_delete_base(text);
    return result;
}

static XDomNodePrivate** xxml_dom_map_storage(XDomNodePrivate* map, int** count,
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

static XDomNode* xxml_dom_map_set_item(XDomNamedNodeMap* self,
                                          const XDomNode* newNode, bool byNamespace)
{
    XDomNodePrivate* map = xxml_dom_handle_impl(self);
    XDomNodePrivate* node = xxml_dom_handle_impl(newNode);
    /* Qt 的实体/notation 映射是只读；属性映射由 QDomElement 专门维护 parent。 */
    if (!map || !node || map->m_mapKind != XDom_MapAttributes ||
        node->m_type != XDom_AttributeNode) return xxml_dom_wrap_node(NULL);
    if (node->m_parent && node->m_parent != map->m_mapOwner)
        return xxml_dom_wrap_node(NULL);
    int index = byNamespace ? xxml_dom_map_find_ns(map, node->m_namespaceUri, node->m_localName) :
                              xxml_dom_map_find_name(map, node->m_name);
    XDomNode* replaced = xxml_dom_wrap_node(index >= 0 ? xxml_dom_map_at(map, index) : NULL);
    int* count = NULL;
    int* capacity = NULL;
    XDomNodePrivate** storage = xxml_dom_map_storage(map, &count, &capacity);
    if (!storage || !count || !capacity) return replaced;
    if (index >= 0) {
        XDomNodePrivate* old = storage[index];
        storage[index] = node;
        node->m_parent = map->m_mapOwner;
        xxml_dom_set_owner_document(node, xxml_dom_document_for_node(map->m_mapOwner));
        xxml_dom_node_retain(node);
        old->m_parent = NULL;
        xxml_dom_node_release(old);
        return replaced;
    }
    XDomNodePrivate*** array = NULL;
    if (map->m_mapKind == XDom_MapAttributes) array = &map->m_mapOwner->m_attributes;
    else if (map->m_mapKind == XDom_MapEntities) array = &map->m_mapOwner->m_entities;
    else array = &map->m_mapOwner->m_notations;
    if (xxml_dom_array_append(array, count, capacity, node)) {
        node->m_parent = map->m_mapOwner;
        xxml_dom_set_owner_document(node, xxml_dom_document_for_node(map->m_mapOwner));
        xxml_dom_node_retain(node);
    }
    return replaced;
}

XDomNode* XDomNamedNodeMap_setNamedItem(XDomNamedNodeMap* self, const XDomNode* newNode)
{
    return xxml_dom_map_set_item(self, newNode, false);
}

XDomNode* XDomNamedNodeMap_setNamedItemNS(XDomNamedNodeMap* self, const XDomNode* newNode)
{
    return xxml_dom_map_set_item(self, newNode, true);
}

XDomNode* XDomNamedNodeMap_removeNamedItem(XDomNamedNodeMap* self, const XString* name)
{
    XDomNodePrivate* map = xxml_dom_handle_impl(self);
    if (!map || map->m_mapKind != XDom_MapAttributes)
        return xxml_dom_wrap_node(NULL);
    int index = xxml_dom_map_find_name(map, name);
    if (index < 0) return xxml_dom_wrap_node(NULL);
    XDomNodePrivate* node = xxml_dom_map_at(map, index);
    XDomNode* result = xxml_dom_wrap_node(node);
    int* count = NULL; XDomNodePrivate** storage = xxml_dom_map_storage(map, &count, NULL);
    xxml_dom_array_remove_at(storage, count, index);
    node->m_parent = NULL;
    node->m_specified = true;
    xxml_dom_node_release(node);
    return result;
}

XDomNode* XDomNamedNodeMap_removeNamedItem_utf8(XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomNode* result = XDomNamedNodeMap_removeNamedItem(self, text);
    XString_delete_base(text);
    return result;
}

XDomNode* XDomNamedNodeMap_item(const XDomNamedNodeMap* self, int index)
{
    return xxml_dom_wrap_node(xxml_dom_map_at(xxml_dom_handle_impl(self), index));
}

XDomNode* XDomNamedNodeMap_namedItemNS(const XDomNamedNodeMap* self,
                                              const XString* namespaceURI, const XString* localName)
{
    XDomNodePrivate* map = xxml_dom_handle_impl(self);
    int index = xxml_dom_map_find_ns(map, namespaceURI, localName);
    return xxml_dom_wrap_node(index >= 0 ? xxml_dom_map_at(map, index) : NULL);
}

XDomNode* XDomNamedNodeMap_namedItemNS_utf8(const XDomNamedNodeMap* self,
                                                   const char* namespaceURI, const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomNode* result = XDomNamedNodeMap_namedItemNS(self, ns, local);
    XString_delete_base(ns); XString_delete_base(local);
    return result;
}

XDomNode* XDomNamedNodeMap_removeNamedItemNS(XDomNamedNodeMap* self,
                                                    const XString* namespaceURI, const XString* localName)
{
    XDomNodePrivate* map = xxml_dom_handle_impl(self);
    if (!map || map->m_mapKind != XDom_MapAttributes)
        return xxml_dom_wrap_node(NULL);
    int index = xxml_dom_map_find_ns(map, namespaceURI, localName);
    if (index < 0) return xxml_dom_wrap_node(NULL);
    XDomNodePrivate* node = xxml_dom_map_at(map, index);
    XDomNode* result = xxml_dom_wrap_node(node);
    int* count = NULL; XDomNodePrivate** storage = xxml_dom_map_storage(map, &count, NULL);
    xxml_dom_array_remove_at(storage, count, index);
    node->m_parent = NULL;
    node->m_specified = true;
    xxml_dom_node_release(node);
    return result;
}

int XDomNamedNodeMap_length(const XDomNamedNodeMap* self)
{
    int count = 0;
    xxml_dom_map_items(xxml_dom_handle_impl(self), &count);
    return count;
}

int XDomNamedNodeMap_count(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self); }
int XDomNamedNodeMap_size(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self); }
bool XDomNamedNodeMap_isEmpty(const XDomNamedNodeMap* self) { return XDomNamedNodeMap_length(self) == 0; }
bool XDomNamedNodeMap_equals(const XDomNamedNodeMap* left, const XDomNamedNodeMap* right)
{
    XDomNodePrivate* leftMap = xxml_dom_handle_impl(left);
    XDomNodePrivate* rightMap = xxml_dom_handle_impl(right);
    if (leftMap == rightMap) return true;
    if (!leftMap || !rightMap ||
        leftMap->m_collectionKind != XDom_CollectionMap ||
        rightMap->m_collectionKind != XDom_CollectionMap)
        return false;
    return leftMap->m_mapOwner == rightMap->m_mapOwner &&
           leftMap->m_mapKind == rightMap->m_mapKind;
}
bool XDomNamedNodeMap_contains(const XDomNamedNodeMap* self, const XString* name)
{ return xxml_dom_map_find_name(xxml_dom_handle_impl(self), name) >= 0; }
bool XDomNamedNodeMap_contains_utf8(const XDomNamedNodeMap* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    bool result = xxml_dom_map_find_name(xxml_dom_handle_impl(self), text) >= 0;
    XString_delete_base(text);
    return result;
}

/* ==================== QDomDocument 与节点工厂 ==================== */

static XDomNodePrivate* xxml_dom_document_impl(const XDomDocument* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_DocumentNode ? node : NULL;
}

static bool xxml_dom_document_create_default_doctype(XDomNodePrivate* document)
{
    if (!document || document->m_type != XDom_DocumentNode) return false;
    if (document->m_doctype) return true;
    XDomNodePrivate* doctype = xxml_dom_node_new_in_context(document->m_context,
                                                             XDom_DocumentTypeNode);
    if (!doctype) return false;
    doctype->m_parent = document;
    doctype->m_ownerDocument = document;
    document->m_doctype = doctype;
    return true;
}

static XDomNodePrivate* xxml_dom_document_ensure_impl(XDomDocument* self)
{
    if (!self || XClassIsVtableNull((XClass*)self)) return NULL;
    XDomNodePrivate* document = xxml_dom_document_impl(self);
    if (document) return document;
    document = xxml_dom_node_new(XDom_DocumentNode);
    if (!document) return NULL;
    self->m_impl = document;
    if (!xxml_dom_document_create_default_doctype(document)) {
        xxml_dom_handle_deinit(&self->m_impl);
        return NULL;
    }
    return document;
}

static XDomElement* xxml_dom_document_new_element(XDomNodePrivate* document,
                                                      const XString* namespaceURI,
                                                      const XString* qualifiedName)
{
    bool ok = false;
    XString* fixedName = xxml_dom_fixed_name(qualifiedName, namespaceURI != NULL, &ok);
    if (!document || !ok) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_element(NULL);
    }
    XDomNodePrivate* node = xxml_dom_create_node_for_document(document, XDom_ElementNode);
    if (!node) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_element(NULL);
    }
    if (namespaceURI) xxml_dom_set_qualified_name(node, namespaceURI, fixedName);
    else xxml_dom_set_plain_name(node, fixedName);
    node->m_ownerDocument = document;
    XDomElement* result = xxml_dom_wrap_element(node);
    xxml_dom_node_release(node);
    XString_delete_base(fixedName);
    return result;
}

static XDomAttr* xxml_dom_document_new_attribute(XDomNodePrivate* document,
                                                     const XString* namespaceURI,
                                                     const XString* qualifiedName)
{
    bool ok = false;
    XString* fixedName = xxml_dom_fixed_name(qualifiedName, namespaceURI != NULL, &ok);
    if (!document || !ok) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_attr(NULL);
    }
    XDomNodePrivate* node = xxml_dom_create_node_for_document(document, XDom_AttributeNode);
    if (!node) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_attr(NULL);
    }
    if (namespaceURI) xxml_dom_set_qualified_name(node, namespaceURI, fixedName);
    else xxml_dom_set_plain_name(node, fixedName);
    node->m_ownerDocument = document;
    XDomAttr* result = xxml_dom_wrap_attr(node);
    xxml_dom_node_release(node);
    XString_delete_base(fixedName);
    return result;
}

static XDomNodePrivate* xxml_dom_document_new_value_node(XDomNodePrivate* document,
                                                             XDomNodeType type,
                                                             const XString* value)
{
    if (!document) return NULL;
    bool ok = false;
    XString* fixedValue = xxml_dom_fixed_value(value, type, &ok);
    if (!ok) {
        XString_delete_base(fixedValue);
        return NULL;
    }
    XDomNodePrivate* node = xxml_dom_create_node_for_document(document, type);
    if (!node) {
        XString_delete_base(fixedValue);
        return NULL;
    }
    xxml_dom_set_default_creation_value(node, fixedValue);
    node->m_ownerDocument = document;
    XString_delete_base(fixedValue);
    return node;
}

XDomDocument* XDomDocument_createName(const XString* name)
{
    XDomDocument* document = XDomDocument_create();
    XDomNodePrivate* impl = xxml_dom_document_ensure_impl(document);
    if (!document || !impl) return document;
    xxml_dom_set_plain_name(impl->m_doctype, name);
    return document;
}

XDomDocument* XDomDocument_createName_utf8(const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomDocument* result = XDomDocument_createName(text);
    XString_delete_base(text);
    return result;
}

XDomDocument* XDomDocument_createDoctype(const XDomDocumentType* doctype)
{
    XDomDocument* result = XDomDocument_create();
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(result);
    XDomNodePrivate* source = xxml_dom_handle_impl(doctype);
    if (!document) return result;
    if (source && source->m_type == XDom_DocumentTypeNode) {
        XDomNodePrivate* clone = xxml_dom_node_clone(source, document->m_context, true);
        if (clone) {
            if (document->m_doctype) {
                xxml_dom_node_release(document->m_doctype);
                document->m_doctype = NULL;
            }
            if (!xxml_dom_append_private(document, clone, -1))
                xxml_dom_node_release(clone);
            else
                xxml_dom_node_release(clone);
        }
    }
    return result;
}

XDomDocumentType* XDomDocument_doctype(const XDomDocument* self)
{
    XDomNodePrivate* document = xxml_dom_document_impl(self);
    return xxml_dom_wrap_doctype(document ? document->m_doctype : NULL);
}

XDomImplementation* XDomDocument_implementation(const XDomDocument* self)
{
    XDomImplementation* result = XDomImplementation_create();
    XDomNodePrivate* document = xxml_dom_document_impl(self);
    if (!document && result) result->m_isNull = true;
    if (result && document) {
        result->m_isNull = false;
        result->m_document = document;
        xxml_dom_node_retain(document);
    }
    return result;
}

XDomElement* XDomDocument_documentElement(const XDomDocument* self)
{
    XDomNodePrivate* document = xxml_dom_document_impl(self);
    if (document) {
        for (int i = 0; i < document->m_childCount; ++i)
            if (document->m_children[i]->m_type == XDom_ElementNode)
                return xxml_dom_wrap_element(document->m_children[i]);
    }
    return xxml_dom_wrap_element(NULL);
}

XDomElement* XDomDocument_createElement(XDomDocument* self, const XString* tagName)
{
    return xxml_dom_document_new_element(xxml_dom_document_ensure_impl(self), NULL, tagName);
}

XDomElement* XDomDocument_createElement_utf8(XDomDocument* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomElement* result = XDomDocument_createElement(self, text);
    XString_delete_base(text);
    return result;
}

XDomDocumentFragment* XDomDocument_createDocumentFragment(XDomDocument* self)
{
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    XDomNodePrivate* node = xxml_dom_document_new_value_node(document,
                                                                 XDom_DocumentFragmentNode, NULL);
    XDomDocumentFragment* result = xxml_dom_wrap_fragment(node);
    xxml_dom_node_release(node);
    return result;
}

XDomText* XDomDocument_createTextNode(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = xxml_dom_document_new_value_node(xxml_dom_document_ensure_impl(self),
                                                                 XDom_TextNode, data);
    XDomText* result = xxml_dom_wrap_text(node);
    xxml_dom_node_release(node);
    return result;
}

XDomText* XDomDocument_createTextNode_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomText* result = XDomDocument_createTextNode(self, text);
    XString_delete_base(text);
    return result;
}

XDomComment* XDomDocument_createComment(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = xxml_dom_document_new_value_node(xxml_dom_document_ensure_impl(self),
                                                                 XDom_CommentNode, data);
    XDomComment* result = xxml_dom_wrap_comment(node);
    xxml_dom_node_release(node);
    return result;
}

XDomComment* XDomDocument_createComment_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomComment* result = XDomDocument_createComment(self, text);
    XString_delete_base(text);
    return result;
}

XDomCDATASection* XDomDocument_createCDATASection(XDomDocument* self, const XString* data)
{
    XDomNodePrivate* node = xxml_dom_document_new_value_node(xxml_dom_document_ensure_impl(self),
                                                                 XDom_CDATASectionNode, data);
    XDomCDATASection* result = xxml_dom_wrap_cdata(node);
    xxml_dom_node_release(node);
    return result;
}

XDomCDATASection* XDomDocument_createCDATASection_utf8(XDomDocument* self, const char* data)
{
    XString* text = XString_create_utf8(data);
    XDomCDATASection* result = XDomDocument_createCDATASection(self, text);
    XString_delete_base(text);
    return result;
}

XDomProcessingInstruction* XDomDocument_createProcessingInstruction(XDomDocument* self,
                                                                            const XString* target,
                                                                            const XString* data)
{
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    bool ok = false;
    XString* fixedTarget = xxml_dom_fixed_name(target, false, &ok);
    if (!document || !ok) {
        XString_delete_base(fixedTarget);
        return xxml_dom_wrap_pi(NULL);
    }
    XDomNodePrivate* node = xxml_dom_document_new_value_node(document,
                                                                 XDom_ProcessingInstructionNode, data);
    if (!node) {
        XString_delete_base(fixedTarget);
        return xxml_dom_wrap_pi(NULL);
    }
    xxml_dom_string_assign(&node->m_name, fixedTarget);
    XDomProcessingInstruction* result = xxml_dom_wrap_pi(node);
    xxml_dom_node_release(node);
    XString_delete_base(fixedTarget);
    return result;
}

XDomProcessingInstruction* XDomDocument_createProcessingInstruction_utf8(XDomDocument* self,
                                                                                 const char* target,
                                                                                 const char* data)
{
    XString* targetString = XString_create_utf8(target);
    XString* dataString = XString_create_utf8(data);
    XDomProcessingInstruction* result = XDomDocument_createProcessingInstruction(self, targetString, dataString);
    XString_delete_base(targetString); XString_delete_base(dataString);
    return result;
}

XDomAttr* XDomDocument_createAttribute(XDomDocument* self, const XString* name)
{
    return xxml_dom_document_new_attribute(xxml_dom_document_ensure_impl(self), NULL, name);
}

XDomAttr* XDomDocument_createAttribute_utf8(XDomDocument* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomAttr* result = XDomDocument_createAttribute(self, text);
    XString_delete_base(text);
    return result;
}

XDomEntityReference* XDomDocument_createEntityReference(XDomDocument* self, const XString* name)
{
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    bool ok = false;
    XString* fixedName = xxml_dom_fixed_name(name, false, &ok);
    if (!document || !ok) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_entity_reference(NULL);
    }
    XDomNodePrivate* node = xxml_dom_document_new_value_node(document,
                                                                 XDom_EntityReferenceNode, NULL);
    if (!node) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_entity_reference(NULL);
    }
    xxml_dom_string_assign(&node->m_name, fixedName);
    XDomEntityReference* result = xxml_dom_wrap_entity_reference(node);
    xxml_dom_node_release(node);
    XString_delete_base(fixedName);
    return result;
}

XDomEntityReference* XDomDocument_createEntityReference_utf8(XDomDocument* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomEntityReference* result = XDomDocument_createEntityReference(self, text);
    XString_delete_base(text);
    return result;
}

static XDomNodeList* xxml_dom_elements_by_name(XDomNodePrivate* parent, const XString* tagName,
                                                   const XString* namespaceURI)
{
    XDomNodePrivate* list = xxml_dom_list_new_for_owner(parent, XDom_ListElements,
                                                        tagName, namespaceURI);
    XDomNodeList* result = xxml_dom_wrap_list(list);
    xxml_dom_node_release(list);
    return result;
}

XDomNodeList* XDomDocument_elementsByTagName(const XDomDocument* self, const XString* tagName)
{
    return xxml_dom_elements_by_name(xxml_dom_document_impl(self), tagName, NULL);
}

XDomNodeList* XDomDocument_elementsByTagName_utf8(const XDomDocument* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomNodeList* result = XDomDocument_elementsByTagName(self, text);
    XString_delete_base(text);
    return result;
}

XDomNode* XDomDocument_importNode(XDomDocument* self, const XDomNode* importedNode, bool deep)
{
    XDomNodePrivate* source = xxml_dom_handle_impl(importedNode);
    if (!source) return xxml_dom_wrap_node(NULL);
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    if (!document) return xxml_dom_wrap_node(NULL);
    /* QDomDocument 和 QDomDocumentType 不允许通过 importNode 导入。 */
    if (source->m_type == XDom_DocumentNode ||
        source->m_type == XDom_DocumentTypeNode)
        return xxml_dom_wrap_node(NULL);
    /* 实体引用的后代永不导入，deep 对其没有影响。属性始终导入后代。 */
    bool copyDeep = source->m_type == XDom_AttributeNode ||
                    (source->m_type != XDom_EntityReferenceNode && deep);
    XDomNodePrivate* copy = xxml_dom_node_clone(source, document->m_context, copyDeep);
    if (!copy) return xxml_dom_wrap_node(NULL);
    xxml_dom_set_owner_document(copy, document);
    XDomNode* result = xxml_dom_wrap_node(copy);
    xxml_dom_node_release(copy);
    return result;
}

XDomElement* XDomDocument_createElementNS(XDomDocument* self, const XString* namespaceURI,
                                                const XString* qualifiedName)
{
    return xxml_dom_document_new_element(xxml_dom_document_ensure_impl(self), namespaceURI, qualifiedName);
}

XDomElement* XDomDocument_createElementNS_utf8(XDomDocument* self, const char* namespaceURI,
                                                     const char* qualifiedName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XDomElement* result = XDomDocument_createElementNS(self, ns, name);
    XString_delete_base(ns); XString_delete_base(name);
    return result;
}

XDomAttr* XDomDocument_createAttributeNS(XDomDocument* self, const XString* namespaceURI,
                                               const XString* qualifiedName)
{
    return xxml_dom_document_new_attribute(xxml_dom_document_ensure_impl(self), namespaceURI, qualifiedName);
}

XDomAttr* XDomDocument_createAttributeNS_utf8(XDomDocument* self, const char* namespaceURI,
                                                    const char* qualifiedName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XDomAttr* result = XDomDocument_createAttributeNS(self, ns, name);
    XString_delete_base(ns); XString_delete_base(name);
    return result;
}

XDomNodeList* XDomDocument_elementsByTagNameNS(const XDomDocument* self, const XString* namespaceURI,
                                                     const XString* localName)
{
    return xxml_dom_elements_by_name(xxml_dom_document_impl(self), localName, namespaceURI);
}

XDomNodeList* XDomDocument_elementsByTagNameNS_utf8(const XDomDocument* self, const char* namespaceURI,
                                                          const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomNodeList* result = XDomDocument_elementsByTagNameNS(self, ns, local);
    XString_delete_base(ns); XString_delete_base(local);
    return result;
}

XDomElement* XDomDocument_elementById(const XDomDocument* self, const XString* id)
{
    (void)self;
    (void)id;
    /* Qt 6 的 QDomDocument 目前也没有基于 DTD/XML Schema 自动识别 ID 属性。 */
    return xxml_dom_wrap_element(NULL);
}

XDomElement* XDomDocument_elementById_utf8(const XDomDocument* self, const char* id)
{
    XString* text = XString_create_utf8(id);
    XDomElement* result = XDomDocument_elementById(self, text);
    XString_delete_base(text);
    return result;
}

/* ==================== QDomElement 与 QDomAttr ==================== */

static XDomNodePrivate* xxml_dom_element_impl(const XDomElement* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_ElementNode ? node : NULL;
}

static XDomNodePrivate* xxml_dom_attr_impl(const XDomAttr* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_AttributeNode ? node : NULL;
}

static int xxml_dom_attribute_index(const XDomNodePrivate* element, const XString* name)
{
    if (!element || element->m_type != XDom_ElementNode) return -1;
    for (int i = 0; i < element->m_attributeCount; ++i)
        if (xxml_dom_string_equal(element->m_attributes[i]->m_name, name)) return i;
    return -1;
}

static int xxml_dom_attribute_ns_index(const XDomNodePrivate* element,
                                       const XString* namespaceURI, const XString* localName)
{
    if (!element || element->m_type != XDom_ElementNode) return -1;
    for (int i = 0; i < element->m_attributeCount; ++i) {
        XDomNodePrivate* attr = element->m_attributes[i];
        if (xxml_dom_string_equal(attr->m_namespaceUri, namespaceURI) &&
            xxml_dom_string_equal(attr->m_localName, localName)) return i;
    }
    return -1;
}

/* 挂接属性前保留一个临时引用，避免从唯一父节点移除时提前释放属性。 */
static bool xxml_dom_attach_attribute(XDomNodePrivate* element, XDomNodePrivate* attr,
                                      bool byNamespace)
{
    if (!element || !attr || element->m_type != XDom_ElementNode ||
        attr->m_type != XDom_AttributeNode) return false;
    xxml_dom_node_retain(attr);
    xxml_dom_detach_from_parent(attr);
    int index = byNamespace ? xxml_dom_attribute_ns_index(element, attr->m_namespaceUri, attr->m_localName) :
                              xxml_dom_attribute_index(element, attr->m_name);
    if (index >= 0) {
        XDomNodePrivate* old = element->m_attributes[index];
        element->m_attributes[index] = attr;
        attr->m_parent = element;
        xxml_dom_set_owner_document(attr, xxml_dom_document_for_node(element));
        old->m_parent = NULL;
        old->m_specified = true;
        xxml_dom_node_release(old);
    } else if (!xxml_dom_array_append(&element->m_attributes, &element->m_attributeCount,
                                      &element->m_attributeCapacity, attr)) {
        xxml_dom_node_release(attr);
        return false;
    } else {
        attr->m_parent = element;
        xxml_dom_set_owner_document(attr, xxml_dom_document_for_node(element));
    }
    xxml_dom_node_invalidate_text_cache(element);
    return true;
}

const XString* XDomElement_tagName(const XDomElement* self)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    return element ? element->m_name : xxml_dom_empty_string();
}

void XDomElement_setTagName(XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    /* Qt 6.8 直接修改 name，不重新执行 createElement 的名称校验。 */
    if (!element) return;
    xxml_dom_string_assign(&element->m_name, name);
}

void XDomElement_setTagName_utf8(XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomElement_setTagName(self, text);
    XString_delete_base(text);
}

const XString* XDomElement_attribute(const XDomElement* self, const XString* name,
                                        const XString* defaultValue)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_index(element, name);
    return index >= 0 ? element->m_attributes[index]->m_value :
                        (defaultValue ? defaultValue : xxml_dom_empty_string());
}

const XString* XDomElement_attribute_utf8(const XDomElement* self, const char* name,
                                             const char* defaultValue)
{
    XString* nameString = XString_create_utf8(name);
    XString* defaultString = XString_create_utf8(defaultValue);
    const XString* result = XDomElement_attribute(self, nameString, defaultString);
    XDomNodePrivate* element = xxml_dom_element_impl(self);
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
        if (stable == defaultString || !stable) stable = xxml_dom_empty_string();
    }
    XString_delete_base(nameString); XString_delete_base(defaultString);
    return stable;
}

static void xxml_dom_element_set_attribute(XDomElement* self, const XString* namespaceURI,
                                           const XString* qualifiedName, const XString* value,
                                           bool byNamespace)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    bool ok = false;
    XString* fixedName = xxml_dom_fixed_name(qualifiedName, byNamespace, &ok);
    if (!element || !ok) {
        XString_delete_base(fixedName);
        return;
    }
    XDomNodePrivate* document = xxml_dom_document_for_node(element);
    XDomNodePrivate* attr = xxml_dom_create_node_for_document(document ? document : element,
                                                                  XDom_AttributeNode);
    if (!attr) {
        XString_delete_base(fixedName);
        return;
    }
    if (byNamespace) xxml_dom_set_qualified_name(attr, namespaceURI, fixedName);
    else xxml_dom_set_plain_name(attr, fixedName);
    xxml_dom_attr_set_value_private(attr, value);
    attr->m_specified = true;
    xxml_dom_attach_attribute(element, attr, byNamespace);
    xxml_dom_node_release(attr);
    XString_delete_base(fixedName);
}

void XDomElement_setAttribute(XDomElement* self, const XString* name, const XString* value)
{
    xxml_dom_element_set_attribute(self, NULL, name, value, false);
}

void XDomElement_setAttribute_utf8(XDomElement* self, const char* name, const char* value)
{
    XString* nameString = XString_create_utf8(name);
    XString* valueString = XString_create_utf8(value);
    XDomElement_setAttribute(self, nameString, valueString);
    XString_delete_base(nameString); XString_delete_base(valueString);
}

static void xxml_dom_element_set_attribute_number(XDomElement* self, const XString* name,
                                                   const char* format, ...)
{
    char buffer[96];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    XString* value = XString_create_utf8(buffer);
    XDomElement_setAttribute(self, name, value);
    XString_delete_base(value);
}

void XDomElement_setAttribute_int(XDomElement* self, const XString* name, int value)
{ xxml_dom_element_set_attribute_number(self, name, "%d", value); }
void XDomElement_setAttribute_uint(XDomElement* self, const XString* name, unsigned int value)
{ xxml_dom_element_set_attribute_number(self, name, "%u", value); }
void XDomElement_setAttribute_int64(XDomElement* self, const XString* name, int64_t value)
{ xxml_dom_element_set_attribute_number(self, name, "%lld", (long long)value); }
void XDomElement_setAttribute_uint64(XDomElement* self, const XString* name, uint64_t value)
{ xxml_dom_element_set_attribute_number(self, name, "%llu", (unsigned long long)value); }
void XDomElement_setAttribute_double(XDomElement* self, const XString* name, double value)
{ xxml_dom_element_set_attribute_number(self, name, "%.17g", value); }
void XDomElement_setAttribute_float(XDomElement* self, const XString* name, float value)
{ xxml_dom_element_set_attribute_number(self, name, "%.8g", (double)value); }

void XDomElement_removeAttribute(XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_index(element, name);
    if (index < 0) return;
    XDomNodePrivate* attr = element->m_attributes[index];
    xxml_dom_array_remove_at(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    xxml_dom_node_release(attr);
}

void XDomElement_removeAttribute_utf8(XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomElement_removeAttribute(self, text);
    XString_delete_base(text);
}

XDomAttr* XDomElement_attributeNode(const XDomElement* self, const XString* name)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_index(element, name);
    return xxml_dom_wrap_attr(index >= 0 ? element->m_attributes[index] : NULL);
}

XDomAttr* XDomElement_attributeNode_utf8(const XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    XDomAttr* result = XDomElement_attributeNode(self, text);
    XString_delete_base(text);
    return result;
}

static XDomAttr* xxml_dom_element_set_attribute_node(XDomElement* self,
                                                         const XDomAttr* newAttr, bool byNamespace)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    XDomNodePrivate* attr = xxml_dom_attr_impl(newAttr);
    if (!element || !attr) return xxml_dom_wrap_attr(NULL);
    int index = byNamespace ? xxml_dom_attribute_ns_index(element, attr->m_namespaceUri, attr->m_localName) :
                              xxml_dom_attribute_index(element, attr->m_name);
    XDomAttr* replaced = xxml_dom_wrap_attr(index >= 0 ? element->m_attributes[index] : NULL);
    if (!xxml_dom_attach_attribute(element, attr, byNamespace)) {
        XDomAttr_delete_base(replaced);
        return xxml_dom_wrap_attr(NULL);
    }
    return replaced;
}

XDomAttr* XDomElement_setAttributeNode(XDomElement* self, const XDomAttr* newAttr)
{ return xxml_dom_element_set_attribute_node(self, newAttr, false); }

XDomAttr* XDomElement_removeAttributeNode(XDomElement* self, const XDomAttr* oldAttr)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    XDomNodePrivate* attr = xxml_dom_attr_impl(oldAttr);
    int index = xxml_dom_array_index(element ? element->m_attributes : NULL,
                                     element ? element->m_attributeCount : 0, attr);
    if (index < 0) return xxml_dom_wrap_attr(NULL);
    XDomAttr* result = xxml_dom_wrap_attr(attr);
    xxml_dom_array_remove_at(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    xxml_dom_node_release(attr);
    return result;
}

bool XDomElement_hasAttribute(const XDomElement* self, const XString* name)
{ return xxml_dom_attribute_index(xxml_dom_element_impl(self), name) >= 0; }

bool XDomElement_hasAttribute_utf8(const XDomElement* self, const char* name)
{
    XString* text = XString_create_utf8(name);
    bool result = XDomElement_hasAttribute(self, text);
    XString_delete_base(text);
    return result;
}

const XString* XDomElement_attributeNS(const XDomElement* self, const XString* namespaceURI,
                                          const XString* localName, const XString* defaultValue)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_ns_index(element, namespaceURI, localName);
    return index >= 0 ? element->m_attributes[index]->m_value :
                        (defaultValue ? defaultValue : xxml_dom_empty_string());
}

const XString* XDomElement_attributeNS_utf8(const XDomElement* self, const char* namespaceURI,
                                               const char* localName, const char* defaultValue)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XString* defaultText = XString_create_utf8(defaultValue);
    const XString* result = XDomElement_attributeNS(self, ns, local, defaultText);
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    const XString* stable = result;
    if (result == defaultText) {
        if (element) {
            if (!element->m_attributeNSCache) element->m_attributeNSCache = XString_create();
            if (element->m_attributeNSCache) {
                XString_assign(element->m_attributeNSCache, defaultText);
                stable = element->m_attributeNSCache;
            }
        }
        if (stable == defaultText || !stable) stable = xxml_dom_empty_string();
    }
    XString_delete_base(ns); XString_delete_base(local); XString_delete_base(defaultText);
    return stable;
}

void XDomElement_setAttributeNS(XDomElement* self, const XString* namespaceURI,
                                   const XString* qualifiedName, const XString* value)
{ xxml_dom_element_set_attribute(self, namespaceURI, qualifiedName, value, true); }

void XDomElement_setAttributeNS_utf8(XDomElement* self, const char* namespaceURI,
                                        const char* qualifiedName, const char* value)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* name = XString_create_utf8(qualifiedName);
    XString* text = XString_create_utf8(value);
    XDomElement_setAttributeNS(self, ns, name, text);
    XString_delete_base(ns); XString_delete_base(name); XString_delete_base(text);
}

static void xxml_dom_element_set_attribute_ns_number(XDomElement* self,
                                                       const XString* namespaceURI,
                                                       const XString* name,
                                                       const char* format, ...)
{
    char buffer[96];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    XString* value = XString_create_utf8(buffer);
    XDomElement_setAttributeNS(self, namespaceURI, name, value);
    XString_delete_base(value);
}

void XDomElement_setAttributeNS_int(XDomElement* self, const XString* namespaceURI,
                                      const XString* name, int value)
{ xxml_dom_element_set_attribute_ns_number(self, namespaceURI, name, "%d", value); }
void XDomElement_setAttributeNS_uint(XDomElement* self, const XString* namespaceURI,
                                       const XString* name, unsigned int value)
{ xxml_dom_element_set_attribute_ns_number(self, namespaceURI, name, "%u", value); }
void XDomElement_setAttributeNS_int64(XDomElement* self, const XString* namespaceURI,
                                        const XString* name, int64_t value)
{ xxml_dom_element_set_attribute_ns_number(self, namespaceURI, name, "%lld", (long long)value); }
void XDomElement_setAttributeNS_uint64(XDomElement* self, const XString* namespaceURI,
                                         const XString* name, uint64_t value)
{ xxml_dom_element_set_attribute_ns_number(self, namespaceURI, name, "%llu", (unsigned long long)value); }
void XDomElement_setAttributeNS_double(XDomElement* self, const XString* namespaceURI,
                                         const XString* name, double value)
{ xxml_dom_element_set_attribute_ns_number(self, namespaceURI, name, "%.17g", value); }

void XDomElement_removeAttributeNS(XDomElement* self, const XString* namespaceURI,
                                      const XString* localName)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_ns_index(element, namespaceURI, localName);
    if (index < 0) return;
    XDomNodePrivate* attr = element->m_attributes[index];
    xxml_dom_array_remove_at(element->m_attributes, &element->m_attributeCount, index);
    attr->m_parent = NULL;
    attr->m_specified = true;
    xxml_dom_node_release(attr);
}

void XDomElement_removeAttributeNS_utf8(XDomElement* self, const char* namespaceURI,
                                           const char* localName)
{
    XString* ns = XString_create_utf8(namespaceURI);
    XString* local = XString_create_utf8(localName);
    XDomElement_removeAttributeNS(self, ns, local);
    XString_delete_base(ns); XString_delete_base(local);
}

XDomAttr* XDomElement_attributeNodeNS(const XDomElement* self, const XString* namespaceURI,
                                            const XString* localName)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    int index = xxml_dom_attribute_ns_index(element, namespaceURI, localName);
    return xxml_dom_wrap_attr(index >= 0 ? element->m_attributes[index] : NULL);
}

XDomAttr* XDomElement_setAttributeNodeNS(XDomElement* self, const XDomAttr* newAttr)
{ return xxml_dom_element_set_attribute_node(self, newAttr, true); }

bool XDomElement_hasAttributeNS(const XDomElement* self, const XString* namespaceURI,
                                   const XString* localName)
{ return xxml_dom_attribute_ns_index(xxml_dom_element_impl(self), namespaceURI, localName) >= 0; }

XDomNodeList* XDomElement_elementsByTagName(const XDomElement* self, const XString* tagName)
{ return xxml_dom_elements_by_name(xxml_dom_element_impl(self), tagName, NULL); }

XDomNodeList* XDomElement_elementsByTagName_utf8(const XDomElement* self, const char* tagName)
{
    XString* text = XString_create_utf8(tagName);
    XDomNodeList* result = XDomElement_elementsByTagName(self, text);
    XString_delete_base(text);
    return result;
}

XDomNodeList* XDomElement_elementsByTagNameNS(const XDomElement* self,
                                                     const XString* namespaceURI, const XString* localName)
{ return xxml_dom_elements_by_name(xxml_dom_element_impl(self), localName, namespaceURI); }

static void xxml_dom_collect_text(XDomNodePrivate* node, XString* output)
{
    if (!node || !output) return;
    for (int i = 0; i < node->m_childCount; ++i) {
        XDomNodePrivate* child = node->m_children[i];
        if (child->m_type == XDom_TextNode || child->m_type == XDom_CDATASectionNode)
            XString_append(output, child->m_value);
        else if (child->m_type == XDom_ElementNode)
            xxml_dom_collect_text(child, output);
    }
}

const XString* XDomElement_text(const XDomElement* self)
{
    XDomNodePrivate* element = xxml_dom_element_impl(self);
    if (!element) return xxml_dom_empty_string();
    xxml_dom_string_delete(&element->m_textCache);
    element->m_textCache = XString_create();
    if (!element->m_textCache) return xxml_dom_empty_string();
    xxml_dom_collect_text(element, element->m_textCache);
    return element->m_textCache;
}

XDomNamedNodeMap* XDomElement_attributes(const XDomElement* self)
{
    return XDomNode_attributes((const XDomNode*)self);
}

const XString* XDomAttr_name(const XDomAttr* self)
{
    XDomNodePrivate* attr = xxml_dom_attr_impl(self);
    return attr ? attr->m_name : xxml_dom_empty_string();
}

bool XDomAttr_specified(const XDomAttr* self)
{
    XDomNodePrivate* attr = xxml_dom_attr_impl(self);
    return attr && attr->m_specified;
}

XDomElement* XDomAttr_ownerElement(const XDomAttr* self)
{
    XDomNodePrivate* attr = xxml_dom_attr_impl(self);
    return xxml_dom_wrap_element(attr ? attr->m_parent : NULL);
}

const XString* XDomAttr_value(const XDomAttr* self)
{
    XDomNodePrivate* attr = xxml_dom_attr_impl(self);
    return attr ? attr->m_value : xxml_dom_empty_string();
}

void XDomAttr_setValue(XDomAttr* self, const XString* value)
{
    XDomNodePrivate* attr = xxml_dom_attr_impl(self);
    if (!attr) return;
    xxml_dom_attr_set_value_private(attr, value);
    attr->m_specified = true;
    xxml_dom_node_invalidate_text_cache(attr);
}

void XDomAttr_setValue_utf8(XDomAttr* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomAttr_setValue(self, text);
    XString_delete_base(text);
}

/* ==================== XML 序列化 ==================== */

/* XML 内部使用 UTF-16，逐个 XChar 处理可避免把 UTF-8 多字节字符拆开。 */
static void xxml_dom_append_escaped(XString* output, const XString* value, bool attribute)
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

static void xxml_dom_append_cdata(XString* output, const XString* value)
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

static bool xxml_dom_element_has_namespace_decl(const XDomNodePrivate* element,
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
                XString_delete_base(qualified);
                if (equal) return true;
            }
        }
    }
    return false;
}

static bool xxml_dom_element_has_namespace_prefix_before(const XDomNodePrivate* element,
                                                         const XString* prefix, int end)
{
    if (!element || !prefix) return false;
    for (int i = 0; i < end && i < element->m_attributeCount; ++i) {
        XDomNodePrivate* attr = element->m_attributes[i];
        if (attr->m_createdWithNamespace &&
            xxml_dom_string_equal(attr->m_prefix, prefix)) return true;
    }
    return false;
}

static void xxml_dom_append_namespace_decl(XString* output, const XString* prefix,
                                           const XString* namespaceURI)
{
    if (!output) return;
    XString_append_utf8(output, " xmlns");
    if (prefix && !XString_isEmpty_base(prefix)) {
        XString_append_char(output, ':');
        XString_append(output, prefix);
    }
    XString_append_utf8(output, "=\"");
    xxml_dom_append_escaped(output, namespaceURI, true);
    XString_append_char(output, '"');
}

static void xxml_dom_append_indent(XString* output, int indent, int level)
{
    if (!output || indent <= 0 || level <= 0) return;
    for (int i = 0; i < indent * level; ++i) XString_append_char(output, ' ');
}

static bool xxml_dom_is_textual_node(const XDomNodePrivate* node)
{
    return node && (node->m_type == XDom_TextNode ||
                    node->m_type == XDom_CDATASectionNode ||
                    node->m_type == XDom_EntityReferenceNode);
}

static void xxml_dom_serialize_node(XDomNodePrivate* node, XString* output,
                                    int indent, int level);

static void xxml_dom_serialize_doctype(XDomNodePrivate* node, XString* output)
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

static void xxml_dom_serialize_element(XDomNodePrivate* node, XString* output,
                                       int indent, int level)
{
    bool pretty = indent >= 0;
    if (pretty) xxml_dom_append_indent(output, indent, level);
    XString_append_char(output, '<');
    XString_append(output, node->m_name);
    if (node->m_createdWithNamespace &&
        ((!node->m_prefix || !XString_isEmpty_base(node->m_prefix)) ||
         !XString_isEmpty_base(node->m_namespaceUri)) &&
        !xxml_dom_element_has_namespace_decl(node, node->m_prefix))
        xxml_dom_append_namespace_decl(output, node->m_prefix, node->m_namespaceUri);
    for (int i = 0; i < node->m_attributeCount; ++i) {
        XDomNodePrivate* attribute = node->m_attributes[i];
        if (!attribute->m_createdWithNamespace ||
            xxml_dom_string_equal(attribute->m_prefix, node->m_prefix) ||
            xxml_dom_element_has_namespace_decl(node, attribute->m_prefix) ||
            xxml_dom_element_has_namespace_prefix_before(node, attribute->m_prefix, i))
            continue;
        xxml_dom_append_namespace_decl(output, attribute->m_prefix,
                                       attribute->m_namespaceUri);
    }
    for (int emitted = 0; emitted < node->m_attributeCount; ++emitted) {
        int selected = -1;
        for (int i = 0; i < node->m_attributeCount; ++i) {
            XDomNodePrivate* candidate = node->m_attributes[i];
            if (candidate->m_line == INT64_MIN) continue;
            const XString* candidatePrefix = candidate->m_createdWithNamespace ?
                                              candidate->m_prefix : xxml_dom_empty_string();
            if (selected < 0) {
                selected = i;
                continue;
            }
            XDomNodePrivate* current = node->m_attributes[selected];
            const XString* currentPrefix = current->m_createdWithNamespace ?
                                            current->m_prefix : xxml_dom_empty_string();
            int prefixOrder = XString_compare(candidatePrefix, currentPrefix);
            if (prefixOrder < 0 ||
                (prefixOrder == 0 && XString_compare(candidate->m_name, current->m_name) < 0))
                selected = i;
        }
        XDomNodePrivate* attribute = node->m_attributes[selected];
        XString_append_char(output, ' ');
        XString_append(output, attribute->m_name);
        XString_append_utf8(output, "=\"");
        xxml_dom_append_escaped(output, attribute->m_value, true);
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
    bool inlineChildren = node->m_childCount == 1 && xxml_dom_is_textual_node(node->m_children[0]);
    for (int i = 0; i < node->m_childCount; ++i) {
        if (pretty && !inlineChildren) XString_append_char(output, '\n');
        xxml_dom_serialize_node(node->m_children[i], output, indent,
                                inlineChildren ? level : level + 1);
    }
    if (pretty && !inlineChildren) {
        XString_append_char(output, '\n');
        xxml_dom_append_indent(output, indent, level);
    }
    XString_append_utf8(output, "</");
    XString_append(output, node->m_name);
    XString_append_char(output, '>');
}

static void xxml_dom_serialize_node(XDomNodePrivate* node, XString* output,
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
                xxml_dom_string_equal_utf8(node->m_children[0]->m_name, "xml")) {
                xxml_dom_serialize_node(node->m_children[0], output, indent, 0);
                wroteHeader = true;
                firstChild = 1;
            }
            if (node->m_doctype && !XString_isEmpty_base(node->m_doctype->m_name)) {
                if (wroteHeader && pretty) XString_append_char(output, '\n');
                xxml_dom_serialize_doctype(node->m_doctype, output);
                wroteHeader = true;
            }
            for (int i = firstChild; i < node->m_childCount; ++i) {
                if (pretty && (wroteHeader || i > firstChild))
                    XString_append_char(output, '\n');
                xxml_dom_serialize_node(node->m_children[i], output, indent, 0);
                wroteHeader = true;
            }
            break;
        }
        case XDom_ElementNode:
            xxml_dom_serialize_element(node, output, indent, level);
            break;
        case XDom_TextNode:
            xxml_dom_append_escaped(output, node->m_value, false);
            break;
        case XDom_CDATASectionNode:
            if (pretty) xxml_dom_append_indent(output, indent, level);
            XString_append_utf8(output, "<![CDATA[");
            xxml_dom_append_cdata(output, node->m_value);
            XString_append_utf8(output, "]]>");
            break;
        case XDom_CommentNode:
            if (pretty) xxml_dom_append_indent(output, indent, level);
            XString_append_utf8(output, "<!--");
            XString_append(output, node->m_value);
            XString_append_utf8(output, "-->");
            break;
        case XDom_ProcessingInstructionNode:
            if (pretty) xxml_dom_append_indent(output, indent, level);
            XString_append_utf8(output, "<?");
            XString_append(output, node->m_name);
            if (!XString_isEmpty_base(node->m_value)) {
                XString_append_char(output, ' ');
                XString_append(output, node->m_value);
            }
            XString_append_utf8(output, "?>");
            break;
        case XDom_DocumentTypeNode:
            if (pretty) xxml_dom_append_indent(output, indent, level);
            xxml_dom_serialize_doctype(node, output);
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

static XString* xxml_dom_serialize(XDomNodePrivate* node, int indent)
{
    XString* result = XString_create();
    if (result && node) xxml_dom_serialize_node(node, result, indent, 0);
    return result;
}

static XString* xxml_dom_serialize_for_policy(XDomNodePrivate* node, int indent,
                                               XDomEncodingPolicy policy)
{
    if (!node || node->m_type != XDom_DocumentNode ||
        policy == XDom_EncodingFromDocument)
        return xxml_dom_serialize(node, indent);

    /* XinYueC 的 XIODevice 没有 QTextStream 编码状态，FromTextStream 以 UTF-8 约定输出。 */
    XString* result = XString_create();
    if (!result) return NULL;
    XString_append_utf8(result, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    bool wroteContent = true;
    if (node->m_doctype && !XString_isEmpty_base(node->m_doctype->m_name)) {
        if (indent >= 0 && wroteContent) XString_append_char(result, '\n');
        xxml_dom_serialize_doctype(node->m_doctype, result);
        wroteContent = true;
    }
    for (int i = 0; i < node->m_childCount; ++i) {
        XDomNodePrivate* child = node->m_children[i];
        /* EncodingFromTextStream 已经生成新的声明，跳过文档中的 xml PI。 */
        if (child->m_type == XDom_ProcessingInstructionNode &&
            xxml_dom_string_equal_utf8(child->m_name, "xml")) continue;
        if (indent >= 0 && wroteContent) XString_append_char(result, '\n');
        xxml_dom_serialize_node(child, result, indent, 0);
        wroteContent = true;
    }
    return result;
}

/* ==================== 字符数据、DTD 与实现对象 ==================== */

static XDomNodePrivate* xxml_dom_character_data_impl(const XDomCharacterData* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return xxml_dom_node_is_character_data_payload(node) ? node : NULL;
}

XString* XDomCharacterData_substringData(const XDomCharacterData* self,
                                            uint64_t offset, uint64_t count)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
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
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    if (!node || !value) return;
    XString_append(node->m_value, value);
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomCharacterData_appendData_utf8(XDomCharacterData* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomCharacterData_appendData(self, text);
    XString_delete_base(text);
}

void XDomCharacterData_insertData(XDomCharacterData* self, uint64_t offset,
                                     const XString* value)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    if (!node || !value) return;
    size_t length = XString_length_base(node->m_value);
    /* QString::insert 对超出范围的 offset 不补齐，也不改变原字符串。 */
    if (offset > (uint64_t)length || offset > (uint64_t)SIZE_MAX) return;
    XString_insert(node->m_value, (size_t)offset, value);
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomCharacterData_deleteData(XDomCharacterData* self, uint64_t offset,
                                     uint64_t count)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    if (!node) return;
    size_t length = XString_length_base(node->m_value);
    if (offset >= length) return;
    size_t start = (size_t)offset;
    size_t amount = count > (uint64_t)(length - start) ? length - start : (size_t)count;
    XString_remove(node->m_value, start, amount);
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomCharacterData_replaceData(XDomCharacterData* self, uint64_t offset,
                                      uint64_t count, const XString* value)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    if (!node || !value) return;
    size_t length = XString_length_base(node->m_value);
    if (offset > length) return;
    size_t start = (size_t)offset;
    size_t amount = count > (uint64_t)(length - start) ? length - start : (size_t)count;
    XString_remove(node->m_value, start, amount);
    XString_insert(node->m_value, start, value);
    xxml_dom_node_invalidate_text_cache(node);
}

int XDomCharacterData_length(const XDomCharacterData* self)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    size_t length = node ? XString_length_base(node->m_value) : 0;
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

const XString* XDomCharacterData_data(const XDomCharacterData* self)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    return node ? node->m_value : xxml_dom_empty_string();
}

void XDomCharacterData_setData(XDomCharacterData* self, const XString* value)
{
    XDomNodePrivate* node = xxml_dom_character_data_impl(self);
    if (!node) return;
    xxml_dom_string_assign(&node->m_value, value);
    xxml_dom_node_invalidate_text_cache(node);
}

void XDomCharacterData_setData_utf8(XDomCharacterData* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomCharacterData_setData(self, text);
    XString_delete_base(text);
}

XDomText* XDomText_splitText(XDomText* self, int offset)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node || (node->m_type != XDom_TextNode &&
                  node->m_type != XDom_CDATASectionNode) || !node->m_parent)
        return xxml_dom_wrap_text(NULL);
    size_t length = XString_length_base(node->m_value);
    size_t split = offset <= 0 ? 0 : (size_t)offset;
    if (split > length) split = length;
    XString* suffix = XString_mid(node->m_value, split, length - split);
    if (!suffix) return xxml_dom_wrap_text(NULL);
    XString_remove(node->m_value, split, length - split);
    XDomNodePrivate* newNode = xxml_dom_create_node_for_document(
        xxml_dom_document_for_node(node), XDom_TextNode);
    if (!newNode) {
        XString_delete_base(suffix);
        return xxml_dom_wrap_text(NULL);
    }
    xxml_dom_string_assign(&newNode->m_value, suffix);
    XString_delete_base(suffix);
    int index = xxml_dom_child_index(node->m_parent, node);
    if (!xxml_dom_insert_child(node->m_parent, newNode, index + 1)) {
        xxml_dom_node_release(newNode);
        return xxml_dom_wrap_text(NULL);
    }
    XDomText* result = xxml_dom_wrap_text(newNode);
    xxml_dom_node_release(newNode);
    xxml_dom_node_invalidate_text_cache(node);
    return result;
}

const XString* XDomDocumentType_name(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_name : xxml_dom_empty_string();
}

XDomNamedNodeMap* XDomDocumentType_entities(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    XDomNodePrivate* map = xxml_dom_map_new(node && node->m_type == XDom_DocumentTypeNode ? node : NULL,
                                                XDom_MapEntities);
    XDomNamedNodeMap* result = xxml_dom_wrap_map(map);
    xxml_dom_node_release(map);
    return result;
}

XDomNamedNodeMap* XDomDocumentType_notations(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    XDomNodePrivate* map = xxml_dom_map_new(node && node->m_type == XDom_DocumentTypeNode ? node : NULL,
                                                XDom_MapNotations);
    XDomNamedNodeMap* result = xxml_dom_wrap_map(map);
    xxml_dom_node_release(map);
    return result;
}

const XString* XDomDocumentType_publicId(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_publicId : xxml_dom_empty_string();
}

const XString* XDomDocumentType_systemId(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_systemId : xxml_dom_empty_string();
}

const XString* XDomDocumentType_internalSubset(const XDomDocumentType* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_DocumentTypeNode ? node->m_internalSubset : xxml_dom_empty_string();
}

const XString* XDomEntity_publicId(const XDomEntity* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_publicId : xxml_dom_empty_string();
}

const XString* XDomEntity_systemId(const XDomEntity* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_systemId : xxml_dom_empty_string();
}

const XString* XDomEntity_notationName(const XDomEntity* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_EntityNode ? node->m_notationName : xxml_dom_empty_string();
}

const XString* XDomNotation_publicId(const XDomNotation* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_NotationNode ? node->m_publicId : xxml_dom_empty_string();
}

const XString* XDomNotation_systemId(const XDomNotation* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_NotationNode ? node->m_systemId : xxml_dom_empty_string();
}

const XString* XDomProcessingInstruction_target(const XDomProcessingInstruction* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_ProcessingInstructionNode ? node->m_name : xxml_dom_empty_string();
}

const XString* XDomProcessingInstruction_data(const XDomProcessingInstruction* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && node->m_type == XDom_ProcessingInstructionNode ? node->m_value : xxml_dom_empty_string();
}

void XDomProcessingInstruction_setData(XDomProcessingInstruction* self, const XString* value)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    if (!node || node->m_type != XDom_ProcessingInstructionNode) return;
    xxml_dom_string_assign(&node->m_value, value);
}

void XDomProcessingInstruction_setData_utf8(XDomProcessingInstruction* self, const char* value)
{
    XString* text = XString_create_utf8(value);
    XDomProcessingInstruction_setData(self, text);
    XString_delete_base(text);
}

/* ==================== QDomImplementation 与类型转换 ==================== */

bool XDomImplementation_hasFeature(const XDomImplementation* self,
                                    const XString* feature, const XString* version)
{
    if (!xxml_dom_implementation_is_valid(self) || !feature) return false;
    return xxml_dom_string_equal_utf8(feature, "XML") &&
           (!version || XString_isEmpty_base(version) ||
            xxml_dom_string_equal_utf8(version, "1.0"));
}

bool XDomImplementation_hasFeature_utf8(const XDomImplementation* self,
                                         const char* feature, const char* version)
{
    XString* featureString = XString_create_utf8(feature);
    XString* versionString = XString_create_utf8(version);
    bool result = XDomImplementation_hasFeature(self, featureString, versionString);
    XString_delete_base(featureString);
    XString_delete_base(versionString);
    return result;
}

XDomDocumentType* XDomImplementation_createDocumentType(const XDomImplementation* self,
                                                        const XString* qualifiedName,
                                                        const XString* publicId,
                                                        const XString* systemId)
{
    bool ok = false;
    XString* fixedName = xxml_dom_fixed_name(qualifiedName, true, &ok);
    if (!xxml_dom_implementation_is_valid(self) || !ok) {
        XString_delete_base(fixedName);
        return xxml_dom_wrap_doctype(NULL);
    }
    bool publicOk = false;
    bool systemOk = false;
    XString* fixedPublicId = xxml_dom_fixed_public_id(publicId, &publicOk);
    XString* fixedSystemId = xxml_dom_fixed_system_id(systemId, &systemOk);
    if (!publicOk || !systemOk) {
        XString_delete_base(fixedName);
        XString_delete_base(fixedPublicId);
        XString_delete_base(fixedSystemId);
        return xxml_dom_wrap_doctype(NULL);
    }
    XDomNodePrivate* doctype = xxml_dom_node_new(XDom_DocumentTypeNode);
    if (!doctype) {
        XString_delete_base(fixedName);
        XString_delete_base(fixedPublicId);
        XString_delete_base(fixedSystemId);
        return xxml_dom_wrap_doctype(NULL);
    }
    xxml_dom_set_plain_name(doctype, fixedName);
    xxml_dom_string_assign(&doctype->m_publicId, fixedPublicId);
    xxml_dom_string_assign(&doctype->m_systemId, fixedSystemId);
    /* XString 空串对应 Qt 的非 null QString；只有 NULL 才表示 null 标识符。 */
    doctype->m_publicIdNull = publicId == NULL;
    doctype->m_systemIdNull = systemId == NULL;
    if (doctype->m_systemIdNull) {
        doctype->m_publicIdNull = true;
        XString_clear_base(doctype->m_publicId);
    }
    XDomDocumentType* result = xxml_dom_wrap_doctype(doctype);
    xxml_dom_node_release(doctype);
    XString_delete_base(fixedName);
    XString_delete_base(fixedPublicId);
    XString_delete_base(fixedSystemId);
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
    XString_delete_base(name);
    XString_delete_base(publicText);
    XString_delete_base(systemText);
    return result;
}

XDomDocument* XDomImplementation_createDocument(const XDomImplementation* self,
                                                const XString* namespaceURI,
                                                const XString* qualifiedName,
                                                const XDomDocumentType* doctype)
{
    if (!xxml_dom_implementation_is_valid(self)) return NULL;
    bool nameOk = false;
    XString* fixedName = xxml_dom_fixed_name(qualifiedName, namespaceURI != NULL, &nameOk);
    if (!nameOk) {
        XString_delete_base(fixedName);
        return NULL;
    }
    XDomDocument* result = XDomDocument_create();
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(result);
    if (!document) {
        XString_delete_base(fixedName);
        XDomDocument_delete_base(result);
        return NULL;
    }

    XDomNodePrivate* sourceType = xxml_dom_handle_impl(doctype);
    if (sourceType && sourceType->m_type == XDom_DocumentTypeNode) {
        XDomNodePrivate* type = xxml_dom_node_clone(sourceType, document->m_context, true);
        if (document->m_doctype) {
            xxml_dom_node_release(document->m_doctype);
            document->m_doctype = NULL;
        }
        if (!type || !xxml_dom_insert_child(document, type, -1)) {
            if (type) xxml_dom_node_release(type);
            XString_delete_base(fixedName);
            XDomDocument_delete_base(result);
            return NULL;
        }
        xxml_dom_node_release(type);
    }

    XDomNodePrivate* root = xxml_dom_create_node_for_document(document, XDom_ElementNode);
    if (!root) {
        if (root) xxml_dom_node_release(root);
        XString_delete_base(fixedName);
        XDomDocument_delete_base(result);
        return NULL;
    }
    if (namespaceURI) xxml_dom_set_qualified_name(root, namespaceURI, fixedName);
    else xxml_dom_set_plain_name(root, fixedName);
    if (!xxml_dom_insert_child(document, root, -1)) {
        xxml_dom_node_release(root);
        XString_delete_base(fixedName);
        XDomDocument_delete_base(result);
        return NULL;
    }
    xxml_dom_node_release(root);
    XString_delete_base(fixedName);
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
    XString_delete_base(namespaceText);
    XString_delete_base(name);
    return result;
}

bool XDomImplementation_isNull(const XDomImplementation* self)
{
    return !xxml_dom_implementation_is_valid(self);
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

static XDomNode* xxml_dom_handle_to_node(const void* handle, XDomNodeType type)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(handle);
    return node && node->m_type == type ? xxml_dom_wrap_node(node) : xxml_dom_wrap_node(NULL);
}

XDomNode* XDomElement_toNode(const XDomElement* self)
{ return xxml_dom_handle_to_node(self, XDom_ElementNode); }

XDomNode* XDomAttr_toNode(const XDomAttr* self)
{ return xxml_dom_handle_to_node(self, XDom_AttributeNode); }

XDomNode* XDomText_toNode(const XDomText* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && (node->m_type == XDom_TextNode || node->m_type == XDom_CDATASectionNode) ?
        xxml_dom_wrap_node(node) : xxml_dom_wrap_node(NULL);
}

XDomNode* XDomCDATASection_toNode(const XDomCDATASection* self)
{ return xxml_dom_handle_to_node(self, XDom_CDATASectionNode); }

XDomNode* XDomComment_toNode(const XDomComment* self)
{ return xxml_dom_handle_to_node(self, XDom_CommentNode); }

XDomNode* XDomDocument_toNode(const XDomDocument* self)
{ return xxml_dom_handle_to_node(self, XDom_DocumentNode); }

XDomNode* XDomDocumentType_toNode(const XDomDocumentType* self)
{ return xxml_dom_handle_to_node(self, XDom_DocumentTypeNode); }

XDomNode* XDomDocumentFragment_toNode(const XDomDocumentFragment* self)
{ return xxml_dom_handle_to_node(self, XDom_DocumentFragmentNode); }

XDomNode* XDomCharacterData_toNode(const XDomCharacterData* self)
{
    XDomNodePrivate* node = xxml_dom_handle_impl(self);
    return node && xxml_dom_node_is_character_data(node) ?
        xxml_dom_wrap_node(node) : xxml_dom_wrap_node(NULL);
}

XDomNode* XDomEntity_toNode(const XDomEntity* self)
{ return xxml_dom_handle_to_node(self, XDom_EntityNode); }

XDomNode* XDomNotation_toNode(const XDomNotation* self)
{ return xxml_dom_handle_to_node(self, XDom_NotationNode); }

XDomNode* XDomEntityReference_toNode(const XDomEntityReference* self)
{ return xxml_dom_handle_to_node(self, XDom_EntityReferenceNode); }

XDomNode* XDomProcessingInstruction_toNode(const XDomProcessingInstruction* self)
{ return xxml_dom_handle_to_node(self, XDom_ProcessingInstructionNode); }

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
    xxml_dom_string_delete(&result->m_errorMessage);
    result->m_errorLine = 0;
    result->m_errorColumn = 0;
}

bool XDomParseResult_isSuccess(const XDomParseResult* result)
{
    return result && result->m_errorMessage == NULL;
}

static void xxml_dom_parse_result_error_utf8(XDomParseResult* result, const char* message,
                                              int64_t line, int64_t column)
{
    if (!result || result->m_errorMessage) return;
    result->m_errorMessage = XString_create_utf8(message ? message : "XML 解析失败");
    result->m_errorLine = line;
    result->m_errorColumn = column;
}

static void xxml_dom_extract_internal_subset(XDomNodePrivate* doctype, const XString* rawDtd)
{
    if (!doctype || !rawDtd) return;
    const char* data = XString_toUtf8(rawDtd);
    if (!data) return;
    size_t length = strlen(data);
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
        xxml_dom_string_assign(&doctype->m_internalSubset, subset);
        XString_delete_base(subset);
    }
}

static void xxml_dom_append_dtd_entity(XDomNodePrivate* document,
                                       XDomNodePrivate* doctype,
                                       const XXmlStreamEntityDeclaration* declaration)
{
    if (!document || !doctype || !declaration) return;
    XDomNodePrivate* entity = xxml_dom_create_node_for_document(document, XDom_EntityNode);
    if (!entity) return;
    xxml_dom_set_plain_name(entity, XXmlStreamEntityDeclaration_name(declaration));
    xxml_dom_string_assign(&entity->m_value, XXmlStreamEntityDeclaration_value(declaration));
    xxml_dom_string_assign(&entity->m_publicId, XXmlStreamEntityDeclaration_publicId(declaration));
    xxml_dom_string_assign(&entity->m_systemId, XXmlStreamEntityDeclaration_systemId(declaration));
    xxml_dom_string_assign(&entity->m_notationName, XXmlStreamEntityDeclaration_notationName(declaration));
    entity->m_ownerDocument = document;
    if (xxml_dom_append_private(doctype, entity, -1) &&
        xxml_dom_array_append(&doctype->m_entities, &doctype->m_entityCount,
                              &doctype->m_entityCapacity, entity))
        xxml_dom_node_retain(entity);
    xxml_dom_node_release(entity);
}

static void xxml_dom_append_dtd_notation(XDomNodePrivate* document,
                                         XDomNodePrivate* doctype,
                                         const XXmlStreamNotationDeclaration* declaration)
{
    if (!document || !doctype || !declaration) return;
    XDomNodePrivate* notation = xxml_dom_create_node_for_document(document, XDom_NotationNode);
    if (!notation) return;
    xxml_dom_set_plain_name(notation, XXmlStreamNotationDeclaration_name(declaration));
    xxml_dom_string_assign(&notation->m_publicId, XXmlStreamNotationDeclaration_publicId(declaration));
    xxml_dom_string_assign(&notation->m_systemId, XXmlStreamNotationDeclaration_systemId(declaration));
    notation->m_ownerDocument = document;
    if (xxml_dom_append_private(doctype, notation, -1) &&
        xxml_dom_array_append(&doctype->m_notations, &doctype->m_notationCount,
                              &doctype->m_notationCapacity, notation))
        xxml_dom_node_retain(notation);
    xxml_dom_node_release(notation);
}

static void xxml_dom_parse_dtd(XDomNodePrivate* document, XXmlStreamReader* reader)
{
    if (!document || !reader) return;
    XDomNodePrivate* doctype = xxml_dom_create_node_for_document(document, XDom_DocumentTypeNode);
    if (!doctype) return;
    xxml_dom_set_plain_name(doctype, XXmlStreamReader_dtdName(reader));
    xxml_dom_string_assign(&doctype->m_publicId, XXmlStreamReader_dtdPublicId(reader));
    xxml_dom_string_assign(&doctype->m_systemId, XXmlStreamReader_dtdSystemId(reader));
    doctype->m_publicIdNull = XString_isEmpty_base(doctype->m_publicId);
    doctype->m_systemIdNull = XString_isEmpty_base(doctype->m_systemId);
    xxml_dom_extract_internal_subset(doctype, XXmlStreamReader_text(reader));
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
            xxml_dom_append_dtd_entity(document, doctype, declaration);
    }
    XXmlStreamNotationDeclarations* notations = XXmlStreamReader_notationDeclarations(reader);
    for (size_t i = 0; notations && i < XXmlStreamNotationDeclarations_size(notations); ++i)
        xxml_dom_append_dtd_notation(document, doctype, XXmlStreamNotationDeclarations_at(notations, i));
    if (document->m_doctype) {
        xxml_dom_node_release(document->m_doctype);
        document->m_doctype = NULL;
    }
    if (!xxml_dom_insert_child(document, doctype, -1))
        xxml_dom_node_release(doctype);
    else
        xxml_dom_node_release(doctype);
}

static XDomNodePrivate* xxml_dom_parse_element(XDomNodePrivate* document,
                                                   XDomNodePrivate* parent,
                                                   XXmlStreamReader* reader, bool useNamespace)
{
    XDomNodePrivate* element = xxml_dom_create_node_for_document(document, XDom_ElementNode);
    if (!element) return NULL;
    if (useNamespace) xxml_dom_set_qualified_name(element, XXmlStreamReader_namespaceUri(reader),
                                                   XXmlStreamReader_qualifiedName(reader));
    else xxml_dom_set_plain_name(element, XXmlStreamReader_qualifiedName(reader));
    element->m_ownerDocument = document;
    element->m_line = XXmlStreamReader_lineNumber(reader);
    element->m_column = XXmlStreamReader_columnNumber(reader);
    const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
    for (int i = 0; attributes && i < XXmlStreamAttributes_size(attributes); ++i) {
        const XXmlStreamAttribute* source = XXmlStreamAttributes_at(attributes, i);
        XDomNodePrivate* attribute = xxml_dom_create_node_for_document(document, XDom_AttributeNode);
        if (!attribute) continue;
        if (useNamespace) xxml_dom_set_qualified_name(attribute, XXmlStreamAttribute_namespaceUri(source),
                                                       XXmlStreamAttribute_qualifiedName(source));
        else xxml_dom_set_plain_name(attribute, XXmlStreamAttribute_qualifiedName(source));
        xxml_dom_attr_set_value_private(attribute, XXmlStreamAttribute_value(source));
        attribute->m_ownerDocument = document;
        attribute->m_specified = true;
        xxml_dom_attach_attribute(element, attribute, useNamespace);
        xxml_dom_node_release(attribute);
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
            XDomNodePrivate* attribute = xxml_dom_create_node_for_document(document,
                                                                              XDom_AttributeNode);
            if (attribute) {
                xxml_dom_set_plain_name(attribute, name);
                xxml_dom_attr_set_value_private(attribute, namespaceURI);
                attribute->m_ownerDocument = document;
                attribute->m_specified = true;
                xxml_dom_attach_attribute(element, attribute, false);
                xxml_dom_node_release(attribute);
            }
            XString_delete_base(name);
        }
    }
    if (!xxml_dom_insert_child(parent, element, -1)) {
        xxml_dom_node_release(element);
        return NULL;
    }
    /* 树已经持有元素引用；current 只借用该指针，不应额外持有初始引用。 */
    xxml_dom_node_release(element);
    return element;
}

static void xxml_dom_parse_value_node(XDomNodePrivate* document, XDomNodePrivate* parent,
                                      XDomNodeType type, const XString* value)
{
    if (!document || !parent) return;
    XDomNodePrivate* node = xxml_dom_document_new_value_node(document, type, value);
    if (!node) return;
    xxml_dom_insert_child(parent, node, -1);
    xxml_dom_node_release(node);
}

static void xxml_dom_capture_document_info(XDomNodePrivate* document,
                                            const XXmlStreamReader* reader)
{
    if (!document || !reader || !XXmlStreamReader_hasXmlDeclaration(reader)) return;
    xxml_dom_string_assign(&document->m_documentVersion,
                           XXmlStreamReader_documentVersion(reader));
    xxml_dom_string_assign(&document->m_documentEncoding,
                           XXmlStreamReader_documentEncoding(reader));
    document->m_hasXmlDeclaration = true;
    document->m_hasStandalone = XXmlStreamReader_hasStandaloneDeclaration(reader);
    document->m_isStandalone = document->m_hasStandalone &&
                               XXmlStreamReader_isStandaloneDocument(reader);
}

/**
 * @brief      构造 Qt QDomDocument 使用的 XML 声明处理指令数据。
 * @param      reader 已定位在 StartDocument 的 XML 读取器；只借用。
 * @return     新分配的处理指令数据；调用者使用 XString_delete_base 释放，失败返回 NULL。
 * @note       属性使用单引号，且仅在输入声明中出现 standalone 时写出 standalone='no'。
 */
static XString* xxml_dom_xml_declaration_data(const XXmlStreamReader* reader)
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

static XDomParseResult xxml_dom_set_content_from_reader(XDomNodePrivate* document,
                                                         XXmlStreamReader* reader,
                                                         unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    if (!document || !reader) {
        xxml_dom_parse_result_error_utf8(&result, "XML 文档或读取器为空", -1, -1);
        return result;
    }

    xxml_dom_node_clear(document);
    if (!xxml_dom_document_create_default_doctype(document)) {
        xxml_dom_parse_result_error_utf8(&result, "XML 文档类型创建失败", -1, -1);
        return result;
    }
    xxml_dom_string_assign(&document->m_documentVersion, NULL);
    xxml_dom_string_assign(&document->m_documentEncoding, NULL);
    document->m_hasXmlDeclaration = false;
    document->m_isStandalone = false;
    document->m_hasStandalone = false;

    bool useNamespace = (options & XDom_UseNamespaceProcessing) != 0;
    bool foundDtd = false;
    XXmlStreamReader_setNamespaceProcessing(reader, useNamespace);
    xxml_dom_capture_document_info(document, reader);
    XDomNodePrivate* current = document;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token == XXmlStream_Invalid || XXmlStreamReader_hasError(reader)) break;
        if (token == XXmlStream_StartDocument) {
            xxml_dom_capture_document_info(document, reader);
            XString* declarationData = xxml_dom_xml_declaration_data(reader);
            XDomNodePrivate* declaration = xxml_dom_document_new_value_node(
                document, XDom_ProcessingInstructionNode, declarationData);
            if (declaration) {
                XString* target = XString_create_utf8("xml");
                xxml_dom_set_plain_name(declaration, target);
                XString_delete_base(target);
                declaration->m_line = XXmlStreamReader_lineNumber(reader);
                declaration->m_column = XXmlStreamReader_columnNumber(reader);
                if (!xxml_dom_insert_child(document, declaration, 0))
                    xxml_dom_node_release(declaration);
                else
                    xxml_dom_node_release(declaration);
            }
            XString_delete_base(declarationData);
        } else if (token == XXmlStream_DTD) {
            if (foundDtd) {
                /* Qt 的 QDomParser 只允许一个文档类型声明。 */
                xxml_dom_parse_result_error_utf8(&result, "XML 文档不允许包含多个 DTD 声明",
                                                  XXmlStreamReader_lineNumber(reader),
                                                  XXmlStreamReader_columnNumber(reader));
                break;
            }
            foundDtd = true;
            xxml_dom_parse_dtd(document, reader);
        } else if (token == XXmlStream_StartElement) {
            XDomNodePrivate* element = xxml_dom_parse_element(document, current, reader, useNamespace);
            if (!element) {
                xxml_dom_parse_result_error_utf8(&result, "XML 元素创建失败",
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
            xxml_dom_parse_value_node(document, current,
                XXmlStreamReader_isCDATA(reader) ? XDom_CDATASectionNode : XDom_TextNode,
                XXmlStreamReader_text(reader));
        } else if (token == XXmlStream_Comment) {
            xxml_dom_parse_value_node(document, current, XDom_CommentNode,
                                      XXmlStreamReader_text(reader));
        } else if (token == XXmlStream_ProcessingInstruction) {
            XDomNodePrivate* pi = xxml_dom_document_new_value_node(document,
                XDom_ProcessingInstructionNode, XXmlStreamReader_processingInstructionData(reader));
            if (pi) {
                xxml_dom_set_plain_name(pi, XXmlStreamReader_processingInstructionTarget(reader));
                xxml_dom_insert_child(current, pi, -1);
                xxml_dom_node_release(pi);
            }
        } else if (token == XXmlStream_EntityReference) {
            XDomNodePrivate* reference = xxml_dom_document_new_value_node(document,
                XDom_EntityReferenceNode, NULL);
            if (reference) {
                xxml_dom_set_plain_name(reference, XXmlStreamReader_name(reader));
                xxml_dom_insert_child(current, reference, -1);
                xxml_dom_node_release(reference);
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
    return xxml_dom_set_content_from_reader(xxml_dom_document_ensure_impl(self), reader, options);
}

XDomParseResult XDomDocument_setContent_result(XDomDocument* self,
                                                const XByteArray* data, unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    if (!document || !data) {
        xxml_dom_parse_result_error_utf8(&result, "XML 文档或输入数据为空", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        xxml_dom_parse_result_error_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    XXmlStreamReader_addData(reader, data);
    result = xxml_dom_set_content_from_reader(document, reader, options);
    XXmlStreamReader_delete_base(reader);
    return result;
}

XDomParseResult XDomDocument_setContent_utf8_result(XDomDocument* self,
                                                     const char* data, unsigned int options)
{
    XByteArray* bytes = XByteArray_create_utf8(data ? data : "");
    XDomParseResult result = XDomDocument_setContent_result(self, bytes, options);
    XByteArray_delete_base(bytes);
    return result;
}

XDomParseResult XDomDocument_setContent_string_result(XDomDocument* self,
                                                        const XString* data,
                                                        unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    if (!document || !data) {
        xxml_dom_parse_result_error_utf8(&result, "XML 文档或输入数据为空", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        xxml_dom_parse_result_error_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    /* 直接使用 UTF-16 输入重载，匹配 Qt QString setContent 路径。 */
    XXmlStreamReader_addData_string(reader, data);
    result = xxml_dom_set_content_from_reader(document, reader, options);
    XXmlStreamReader_delete_base(reader);
    return result;
}

XDomParseResult XDomDocument_setContent_device_result(XDomDocument* self,
                                                       XIODevice* device,
                                                       unsigned int options)
{
    XDomParseResult result;
    XDomParseResult_init(&result);
    XDomNodePrivate* document = xxml_dom_document_ensure_impl(self);
    if (!document || !device) {
        xxml_dom_parse_result_error_utf8(&result, "XML 文档或输入设备为空", -1, -1);
        return result;
    }
    /* Qt 6.8 会为未打开设备尝试只读打开；设备仍由调用者负责关闭。 */
    if (!XIODevice_isOpen(device) && !XIODevice_open_base(device, XIODevice_ReadOnly)) {
        xxml_dom_parse_result_error_utf8(&result, "XML 输入设备打开失败", -1, -1);
        return result;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        xxml_dom_parse_result_error_utf8(&result, "XML 读取器创建失败", -1, -1);
        return result;
    }
    XXmlStreamReader_setDevice(reader, device);
    result = xxml_dom_set_content_from_reader(document, reader, options);
    XXmlStreamReader_delete_base(reader);
    return result;
}

static bool xxml_dom_unpack_parse_result(XDomParseResult* result,
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
    return xxml_dom_unpack_parse_result(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_utf8(XDomDocument* self, const char* data, unsigned int options,
                                  XString** errorMessage, int64_t* errorLine, int64_t* errorColumn)
{
    XByteArray* bytes = XByteArray_create_utf8(data ? data : "");
    bool success = XDomDocument_setContent(self, bytes, options, errorMessage,
                                           errorLine, errorColumn);
    XByteArray_delete_base(bytes);
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
    return xxml_dom_unpack_parse_result(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_reader(XDomDocument* self, XXmlStreamReader* reader,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_reader_result(self, reader, options);
    return xxml_dom_unpack_parse_result(&result, errorMessage, errorLine, errorColumn);
}

bool XDomDocument_setContent_device(XDomDocument* self, XIODevice* device,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn)
{
    if (errorMessage) *errorMessage = NULL;
    if (errorLine) *errorLine = 0;
    if (errorColumn) *errorColumn = 0;
    XDomParseResult result = XDomDocument_setContent_device_result(self, device, options);
    return xxml_dom_unpack_parse_result(&result, errorMessage, errorLine, errorColumn);
}

XString* XDomDocument_toString(const XDomDocument* self, int indent)
{
    return xxml_dom_serialize(xxml_dom_document_impl(self), indent);
}

XByteArray* XDomDocument_toByteArray(const XDomDocument* self, int indent)
{
    XString* text = XDomDocument_toString(self, indent);
    XByteArray* result = XByteArray_create_utf8(text ? XString_toUtf8(text) : "");
    XString_delete_base(text);
    return result;
}
