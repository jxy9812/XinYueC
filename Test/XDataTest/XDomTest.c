/**
 * @file       XDomTest.c
 * @brief      XDom XML DOM 模块回归测试实现。
 * @details    测试按节点树、实现对象、属性/列表、字符数据、序列化和解析顺序执行。
 */
#include "XDomTest.h"
#include "XDom.h"
#include "XString.h"
#include "XXmlStreamReader.h"
#include "XFile.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XVariant.h"

#include <stdint.h>
#include <string.h>

#define TEST_PASS(name) XPrintf("[通过] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[失败] %s: %s\n", name, reason)
#define TEST_INFO(...) XPrintf_line("[信息] ", __VA_ARGS__)

static bool xdom_node_is_null(XDomNode* node)
{
    bool result = !node || XDomNode_isNull(node);
    if (node) XClassDelete(node);
    return result;
}

static bool xdom_node_is_valid(const XDomNode* node)
{
    return node && !XDomNode_isNull(node);
}

static bool xdom_string_equals(const XString* value, const char* expected)
{
    if (expected && expected[0] == '\0') return !value || XString_isEmpty_base(value);
    return value && XString_equals_utf8(value, expected ? expected : "", XChar_CaseSensitive);
}

static XDomNode* xdom_append(XDomNode* parent, XDomNode* child)
{
    XDomNode* result = XDomNode_appendChild(parent, child);
    return result;
}

static bool test_implementation_and_conversions(void)
{
    TEST_INFO("===== DOM 实现与类型转换 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomImplementation* emptyImplementation = XDomImplementation_create();
    XDomImplementation* implementation = XDomDocument_implementation(document);
    XDomNode* emptyDocumentNode = XDomDocument_toNode(document);
    XDomImplementation* sameDocumentImplementation = XDomDocument_implementation(document);
    XDomDocument* otherDocument = XDomDocument_create();
    XDomImplementation* otherImplementation = XDomDocument_implementation(otherDocument);
    bool defaultImplementationNull = implementation && XDomImplementation_isNull(implementation);
    bool defaultSameImplementationNull = sameDocumentImplementation &&
                                         XDomImplementation_isNull(sameDocumentImplementation);
    XDomElement* materializer = XDomDocument_createElement_utf8(document, "materializer");
    XDomElement* otherMaterializer = XDomDocument_createElement_utf8(otherDocument, "other");
    XDomDocumentType* nullDocumentType = XDomDocument_doctype(document);
    XDomNode* nullDocumentTypeNode = XDomDocumentType_toNode(nullDocumentType);
    XClassDelete(implementation);
    implementation = XDomDocument_implementation(document);
    XClassDelete(sameDocumentImplementation);
    sameDocumentImplementation = XDomDocument_implementation(document);
    XClassDelete(otherImplementation);
    otherImplementation = XDomDocument_implementation(otherDocument);
    if (emptyImplementation && XDomImplementation_isNull(emptyImplementation) &&
        emptyDocumentNode && XDomNode_isNull(emptyDocumentNode) &&
        defaultImplementationNull && defaultSameImplementationNull &&
        nullDocumentType && nullDocumentTypeNode && !XDomNode_isNull(nullDocumentTypeNode) &&
        xdom_string_equals(XDomDocumentType_name(nullDocumentType), "") &&
        materializer && otherMaterializer && implementation &&
        !XDomImplementation_isNull(implementation) && sameDocumentImplementation &&
        otherImplementation && !XDomImplementation_isNull(otherImplementation) &&
        XDomImplementation_equals(implementation, sameDocumentImplementation) &&
        !XDomImplementation_equals(implementation, otherImplementation) &&
        XDomImplementation_hasFeature_utf8(implementation, "XML", "") &&
        XDomImplementation_hasFeature_utf8(implementation, "XML", "1.0") &&
        !XDomImplementation_hasFeature_utf8(implementation, "HTML", "1.0"))
        TEST_PASS("实现能力查询");
    else { TEST_FAIL("实现能力查询", "XML 能力查询结果错误"); all_pass = false; }

    XDomDocumentType* type = XDomImplementation_createDocumentType_utf8(
        implementation, "root", "public-id", "system-id");
    if (type && xdom_string_equals(XDomDocumentType_name(type), "root") &&
        xdom_string_equals(XDomDocumentType_publicId(type), "public-id") &&
        xdom_string_equals(XDomDocumentType_systemId(type), "system-id"))
        TEST_PASS("创建文档类型");
    else { TEST_FAIL("创建文档类型", "文档类型字段错误"); all_pass = false; }

    XDomDocument* created = XDomImplementation_createDocument_utf8(
        implementation, "urn:test", "p:root", type);
    XDomDocument* doctypeConstructed = XDomDocument_createDoctype(type);
    XDomDocumentType* doctypeConstructedType = doctypeConstructed ?
        XDomDocument_doctype(doctypeConstructed) : NULL;
    XDomElement* root = created ? XDomDocument_documentElement(created) : NULL;
    XDomDocumentType* createdType = created ? XDomDocument_doctype(created) : NULL;
    XDomNode* createdRootNode = root ? XDomElement_toNode(root) : NULL;
    if (created && root && createdRootNode && !XDomNode_isNull(createdRootNode) &&
        xdom_string_equals(XDomElement_tagName(root), "p:root") &&
        xdom_string_equals(XDomNode_localName(createdRootNode), "root") &&
        xdom_string_equals(XDomNode_namespaceURI(createdRootNode), "urn:test") &&
        createdType && xdom_string_equals(XDomDocumentType_name(createdType), "root"))
        TEST_PASS("创建文档并挂接文档类型");
    else { TEST_FAIL("创建文档", "根元素、命名空间或文档类型错误"); all_pass = false; }
    XClassDelete(createdRootNode);
    if (doctypeConstructed && doctypeConstructedType &&
        xdom_string_equals(XDomDocumentType_name(doctypeConstructedType), "root"))
        TEST_PASS("按文档类型构造文档");
    else { TEST_FAIL("文档类型构造", "QDomDocument(doctype) 的 C 映射未复制文档类型"); all_pass = false; }

    XDomNode* documentNode = XDomDocument_toNode(document);
    XDomNode* typeNode = XDomDocumentType_toNode(type);
    XDomAttr* attr = XDomDocument_createAttribute_utf8(document, "attribute");
    XDomText* text = XDomDocument_createTextNode_utf8(document, "text");
    XDomCDATASection* cdata = XDomDocument_createCDATASection_utf8(document, "cdata");
    XDomComment* comment = XDomDocument_createComment_utf8(document, "comment");
    XDomEntityReference* entityReference = XDomDocument_createEntityReference_utf8(document, "entity");
    XDomProcessingInstruction* processingInstruction =
        XDomDocument_createProcessingInstruction_utf8(document, "target", "data");
    XDomNode* attrNode = XDomAttr_toNode(attr);
    XDomNode* textNode = XDomText_toNode(text);
    XDomNode* cdataNode = XDomCDATASection_toNode(cdata);
    XDomNode* commentNode = XDomComment_toNode(comment);
    XDomNode* characterDataNode = XDomCharacterData_toNode((XDomCharacterData*)text);
    XDomNode* entityReferenceNode = XDomEntityReference_toNode(entityReference);
    XDomNode* processingInstructionNode = XDomProcessingInstruction_toNode(processingInstruction);
    if (xdom_node_is_valid(documentNode) && xdom_node_is_valid(typeNode) &&
        xdom_node_is_valid(attrNode) && xdom_node_is_valid(textNode) &&
        xdom_node_is_valid(cdataNode) && xdom_node_is_valid(commentNode) &&
        xdom_node_is_valid(characterDataNode) && xdom_node_is_valid(entityReferenceNode) &&
        xdom_node_is_valid(processingInstructionNode))
        TEST_PASS("具体节点转换保持共享引用");
    else { TEST_FAIL("具体节点转换", "转换后节点为空"); all_pass = false; }

    XClassDelete(processingInstructionNode);
    XClassDelete(entityReferenceNode);
    XClassDelete(characterDataNode);
    XClassDelete(commentNode);
    XClassDelete(cdataNode);
    XClassDelete(textNode);
    XClassDelete(attrNode);
    XClassDelete(processingInstruction);
    XClassDelete(entityReference);
    XClassDelete(comment);
    XClassDelete(cdata);
    XClassDelete(text);
    XClassDelete(attr);
    XClassDelete(typeNode);
    XClassDelete(documentNode);
    XClassDelete(createdType);
    XClassDelete(doctypeConstructedType);
    XClassDelete(nullDocumentType);
    XClassDelete(nullDocumentTypeNode);
    XClassDelete(otherMaterializer);
    XClassDelete(materializer);
    XClassDelete(emptyDocumentNode);
    XClassDelete(root);
    XClassDelete(created);
    XClassDelete(doctypeConstructed);
    XClassDelete(type);
    XClassDelete(implementation);
    XClassDelete(emptyImplementation);
    XClassDelete(otherImplementation);
    XClassDelete(otherDocument);
    XClassDelete(sameDocumentImplementation);
    XClassDelete(document);
    return all_pass;
}

static bool test_tree_and_live_lists(void)
{
    TEST_INFO("===== 节点树、根约束和实时节点列表 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* root = XDomDocument_createElement_utf8(document, "root");
    XDomElement* first = XDomDocument_createElement_utf8(document, "item");
    XDomElement* second = XDomDocument_createElement_utf8(document, "item");
    XDomElement* nested = XDomDocument_createElement_utf8(document, "nested");
    XDomNode* documentNode = XDomDocument_toNode(document);
    XDomNode* rootNode = XDomElement_toNode(root);
    XDomNode* firstNode = XDomElement_toNode(first);
    XDomNode* secondNode = XDomElement_toNode(second);
    XDomNode* nestedNode = XDomElement_toNode(nested);

    XDomNode* appended = xdom_append(documentNode, rootNode);
    if (!xdom_node_is_null(appended)) TEST_PASS("挂接根元素");
    else { TEST_FAIL("挂接根元素", "根元素挂接失败"); all_pass = false; }

    XDomNodeList* children = XDomNode_childNodes(rootNode);
    XDomNodeList* items = XDomElement_elementsByTagName_utf8(root, "item");
    appended = xdom_append(rootNode, firstNode);
    if (!xdom_node_is_null(appended) && XDomNodeList_length(children) == 1 &&
        XDomNodeList_length(items) == 1)
        TEST_PASS("节点列表初始实时查询");
    else { TEST_FAIL("节点列表初始查询", "第一次子节点未反映到列表"); all_pass = false; }

    appended = xdom_append(rootNode, nestedNode);
    XDomNode* nestedChild = XDomElement_toNode(nested);
    bool secondAppendOk = !xdom_node_is_null(xdom_append(nestedChild, secondNode));
    XClassDelete(nestedChild);
    XDomNode* secondItem = XDomNodeList_item(items, 1);
    if (!xdom_node_is_null(appended) && secondAppendOk && XDomNodeList_length(children) == 2 &&
        XDomNodeList_length(items) == 2 &&
        xdom_string_equals(XDomNode_nodeName(secondItem), "item"))
        TEST_PASS("节点列表变更后保持实时");
    else { TEST_FAIL("节点列表实时性", "后续树变化未反映到列表"); all_pass = false; }
    XClassDelete(secondItem);

    XDomNode* movedBefore = XDomNode_insertBefore(rootNode, firstNode, nestedNode);
    XDomNode* movedAfter = XDomNode_insertAfter(rootNode, nestedNode, firstNode);
    XDomNode* firstAfterMove = XDomNodeList_item(children, 0);
    XDomNode* secondAfterMove = XDomNodeList_item(children, 1);
    if (movedBefore && movedAfter && firstAfterMove && secondAfterMove &&
        xdom_string_equals(XDomNode_nodeName(firstAfterMove), "item") &&
        xdom_string_equals(XDomNode_nodeName(secondAfterMove), "nested"))
        TEST_PASS("同父节点移动保持 Qt 插入位置");
    else { TEST_FAIL("同父节点移动", "已有子节点移动后顺序错误"); all_pass = false; }
    XClassDelete(secondAfterMove);
    XClassDelete(firstAfterMove);
    XClassDelete(movedAfter);
    XClassDelete(movedBefore);

    XDomElement* duplicateRoot = XDomDocument_createElement_utf8(document, "other");
    XDomNode* duplicateRootNode = XDomElement_toNode(duplicateRoot);
    appended = xdom_append(documentNode, duplicateRootNode);
    if (xdom_node_is_null(appended) && root &&
        xdom_string_equals(XDomElement_tagName(root), "root"))
        TEST_PASS("文档拒绝第二个根元素且保持原树");
    else { TEST_FAIL("根元素约束", "第二个根元素被错误挂接"); all_pass = false; }

    XDomNode* rootClone = XDomNode_cloneNode(rootNode, true);
    XDomElement* cloneElement = XDomNode_toElement(rootClone);
    XDomElement_setAttribute_utf8(cloneElement, "copy", "yes");
    if (!XDomElement_hasAttribute_utf8(root, "copy") &&
        XDomElement_hasAttribute_utf8(cloneElement, "copy"))
        TEST_PASS("深拷贝保持独立");
    else { TEST_FAIL("深拷贝", "深拷贝与源节点发生错误共享"); all_pass = false; }

    XClassDelete(rootClone);
    XClassDelete(cloneElement);
    XClassDelete(duplicateRoot);
    XClassDelete(duplicateRootNode);
    XClassDelete(nestedNode);
    XClassDelete(nested);
    XClassDelete(second);
    XClassDelete(secondNode);
    XClassDelete(firstNode);
    XClassDelete(first);
    XClassDelete(root);
    XClassDelete(documentNode);
    XClassDelete(rootNode);
    XClassDelete(children);
    XClassDelete(items);
    XClassDelete(document);
    return all_pass;
}

static bool test_qt_tree_and_import_semantics(void)
{
    TEST_INFO("===== Qt 树操作、clear、克隆和导入语义 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* root = XDomDocument_createElement_utf8(document, "root");
    XDomElement* child = XDomDocument_createElement_utf8(document, "child");
    XDomNode* documentNode = XDomDocument_toNode(document);
    XDomNode* rootNode = XDomElement_toNode(root);
    XDomNode* childNode = XDomElement_toNode(child);
    XDomNode* appended = xdom_append(documentNode, rootNode);
    XClassDelete(appended);
    XClassDelete(xdom_append(rootNode, childNode));

    XDomNode* clone = XDomNode_cloneNode(rootNode, true);
    XDomDocument* cloneOwner = XDomNode_ownerDocument(clone);
    XDomNode* cloneOwnerNode = XDomDocument_toNode(cloneOwner);
    if (clone && cloneOwner && XDomNode_equals(cloneOwnerNode, documentNode))
        TEST_PASS("深克隆保留 ownerDocument");
    else { TEST_FAIL("深克隆 ownerDocument", "克隆节点未归属原文档"); all_pass = false; }

    XDomNode* shared = XDomNode_create_copy(rootNode);
    XDomNode_clear(shared);
    XDomNodeList* sourceChildren = XDomNode_childNodes(rootNode);
    if (shared && XDomNode_isNull(shared) && XDomNodeList_length(sourceChildren) == 1)
        TEST_PASS("clear 只清空当前句柄");
    else { TEST_FAIL("clear 共享句柄", "clear 错误修改了共享树或未清空目标句柄"); all_pass = false; }

    XDomElement* first = XDomDocument_createElement_utf8(document, "first");
    XDomElement* second = XDomDocument_createElement_utf8(document, "second");
    XDomElement* before = XDomDocument_createElement_utf8(document, "before");
    XDomElement* after = XDomDocument_createElement_utf8(document, "after");
    XDomElement* replacement = XDomDocument_createElement_utf8(document, "replacement");
    XDomNode* firstNode = XDomElement_toNode(first);
    XDomNode* secondNode = XDomElement_toNode(second);
    XDomNode* beforeNode = XDomElement_toNode(before);
    XDomNode* afterNode = XDomElement_toNode(after);
    XDomNode* replacementNode = XDomElement_toNode(replacement);
    XClassDelete(xdom_append(rootNode, firstNode));
    XClassDelete(xdom_append(rootNode, secondNode));
    XDomNode* inserted = XDomNode_insertBefore(rootNode, beforeNode, NULL);
    XDomNode* selfInsert = XDomNode_insertBefore(rootNode, beforeNode, beforeNode);
    XDomNode* firstChild = XDomNode_firstChild(rootNode);
    XDomNode* appendedAfter = XDomNode_insertAfter(rootNode, afterNode, NULL);
    XDomNode* lastChild = XDomNode_lastChild(rootNode);
    XDomNode* replaced = XDomNode_replaceChild(rootNode, replacementNode, secondNode);
    XDomNode* selfReplace = XDomNode_replaceChild(rootNode, replacementNode, replacementNode);
    XDomNode* removed = XDomNode_removeChild(rootNode, firstNode);
    bool orderOk = inserted && XDomNode_isNull(selfInsert) && appendedAfter && replaced &&
                   XDomNode_isNull(selfReplace) && removed &&
                   xdom_string_equals(XDomNode_nodeName(firstChild), "before") &&
                   xdom_string_equals(XDomNode_nodeName(lastChild), "after") &&
                   xdom_string_equals(XDomNode_nodeName(replaced), "second") &&
                   xdom_string_equals(XDomNode_nodeName(removed), "first");
    if (orderOk) TEST_PASS("insertBefore/After、replaceChild、removeChild 顺序");
    else { TEST_FAIL("树操作顺序", "空参考节点或替换移除顺序不符合 Qt"); all_pass = false; }

    XDomDocument* targetDocument = XDomDocument_create();
    XDomNode* imported = XDomDocument_importNode(targetDocument, rootNode, true);
    XDomDocument* importedOwner = XDomNode_ownerDocument(imported);
    XDomNode* importedOwnerNode = XDomDocument_toNode(importedOwner);
    XDomNode* targetDocumentNode = XDomDocument_toNode(targetDocument);
    XDomNode* documentImport = XDomDocument_importNode(targetDocument, documentNode, true);
    XDomImplementation* implementation = XDomDocument_implementation(document);
    XDomDocumentType* doctype = XDomImplementation_createDocumentType_utf8(
        implementation, "root", NULL, NULL);
    XDomNode* doctypeNode = XDomDocumentType_toNode(doctype);
    XDomNode* doctypeImport = XDomDocument_importNode(targetDocument, doctypeNode, true);
    bool documentImportNull = documentImport && XDomNode_isNull(documentImport);
    bool doctypeImportNull = doctypeImport && XDomNode_isNull(doctypeImport);
    if (imported && importedOwner && importedOwnerNode && targetDocumentNode &&
        XDomNode_equals(importedOwnerNode, targetDocumentNode) &&
        documentImportNull && doctypeImportNull)
        TEST_PASS("importNode 的归属文档和禁止类型");
    else { TEST_FAIL("importNode", "导入归属或 Document/DocumentType 限制错误"); all_pass = false; }

    XDomEntityReference* reference = XDomDocument_createEntityReference_utf8(document, "entity");
    XDomText* referenceText = XDomDocument_createTextNode_utf8(document, "expanded");
    XDomNode* referenceNode = XDomEntityReference_toNode(reference);
    XDomNode* referenceTextNode = XDomText_toNode(referenceText);
    XClassDelete(xdom_append(referenceNode, referenceTextNode));
    XDomNode* importedReference = XDomDocument_importNode(targetDocument, referenceNode, true);
    XDomNodeList* importedReferenceChildren = XDomNode_childNodes(importedReference);
    if (importedReference && XDomNodeList_length(importedReferenceChildren) == 0)
        TEST_PASS("实体引用导入不复制后代");
    else { TEST_FAIL("实体引用导入", "deep=true 错误复制了实体引用后代"); all_pass = false; }

    XString* savePath = XString_create_utf8("xdom_node_save_test.xml");
    XFile_remove_static(savePath);
    XFile* saveFile = XFile_create_2(savePath);
    bool saveOpened = saveFile && XFile_open_2(saveFile,
        XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate, 0);
    bool saveOk = saveOpened && XDomNode_save(rootNode, (XIODevice*)saveFile, -1,
                                               XDom_EncodingFromDocument);
    if (saveFile) {
        XIODevice_close_base((XIODevice*)saveFile);
        XFile_deleteLater(saveFile);
    }
    XFile* readSavedFile = XFile_create_2(savePath);
    XByteArray* savedBytes = NULL;
    bool readSaved = readSavedFile && XFile_open_2(readSavedFile,
        XIODevice_ReadOnly | XIODevice_Existing, 0);
    if (readSaved) savedBytes = XIODevice_readAll_3((XIODevice*)readSavedFile);
    if (savedBytes) XByteArray_append_1(savedBytes, 0);
    const char* savedText = savedBytes ? (const char*)XByteArray_data(savedBytes) : NULL;
    if (saveOk && savedText && strstr(savedText, "<root"))
        TEST_PASS("节点 save 写入 XinYueC 设备");
    else { TEST_FAIL("节点 save", "节点序列化未完整写入设备"); all_pass = false; }
    XClassDelete(savedBytes);
    if (readSavedFile) {
        XIODevice_close_base((XIODevice*)readSavedFile);
        XFile_deleteLater(readSavedFile);
    }
    XFile_remove_static(savePath);
    XClassDelete(savePath);

    XDomNodeList* sourceChildrenCopy = XDomNodeList_create_copy(sourceChildren);
    XDomNodeList* sourceChildrenFresh = XDomNode_childNodes(rootNode);
    XDomNamedNodeMap* attributes = XDomElement_attributes(root);
    XDomNamedNodeMap* attributesCopy = XDomNamedNodeMap_create_copy(attributes);
    if (XDomNodeList_equals(sourceChildren, sourceChildrenCopy) &&
        XDomNodeList_equals(sourceChildren, sourceChildrenFresh) &&
        XDomNamedNodeMap_equals(attributes, attributesCopy))
        TEST_PASS("列表和命名映射句柄相等性");
    else { TEST_FAIL("句柄相等性", "复制句柄未保持底层对象相等"); all_pass = false; }

    XClassDelete(importedReference);
    XClassDelete(importedReferenceChildren);
    XClassDelete(referenceTextNode);
    XClassDelete(referenceText);
    XClassDelete(referenceNode);
    XClassDelete(reference);
    XClassDelete(documentImport);
    XClassDelete(attributesCopy);
    XClassDelete(attributes);
    XClassDelete(sourceChildrenCopy);
    XClassDelete(sourceChildrenFresh);
    XClassDelete(doctypeImport);
    XClassDelete(doctypeNode);
    XClassDelete(doctype);
    XClassDelete(implementation);
    XClassDelete(targetDocumentNode);
    XClassDelete(importedOwnerNode);
    XClassDelete(importedOwner);
    XClassDelete(imported);
    XClassDelete(targetDocument);
    XClassDelete(removed);
    XClassDelete(replaced);
    XClassDelete(lastChild);
    XClassDelete(appendedAfter);
    XClassDelete(firstChild);
    XClassDelete(inserted);
    XClassDelete(selfInsert);
    XClassDelete(selfReplace);
    XClassDelete(replacementNode);
    XClassDelete(afterNode);
    XClassDelete(beforeNode);
    XClassDelete(secondNode);
    XClassDelete(firstNode);
    XClassDelete(replacement);
    XClassDelete(after);
    XClassDelete(before);
    XClassDelete(second);
    XClassDelete(first);
    XClassDelete(sourceChildren);
    XClassDelete(cloneOwner);
    XClassDelete(cloneOwnerNode);
    XClassDelete(clone);
    XClassDelete(shared);
    XClassDelete(childNode);
    XClassDelete(child);
    XClassDelete(rootNode);
    XClassDelete(root);
    XClassDelete(documentNode);
    XClassDelete(document);
    return all_pass;
}

static bool test_qt_attribute_and_policy_semantics(void)
{
    TEST_INFO("===== Qt 属性重载和 InvalidDataPolicy =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* element = XDomDocument_createElement_utf8(document, "root");
    XString* name = XString_create_utf8("count");
    XString* namespaceURI = XString_create_utf8("urn:test");
    XString* namespacedName = XString_create_utf8("p:value");
    XString* ratioName = XString_create_utf8("p:ratio");
    XString* localName = XString_create_utf8("value");
    XDomElement_setAttribute_float(element, name, 1.25f);
    XDomElement_setAttribute_float(element, ratioName, 1.23456789f);
    XDomElement_setAttributeNS_int(element, namespaceURI, namespacedName, 7);
    XDomElement_setAttributeNS_double(element, namespaceURI, ratioName, 2.5);
    const XString* missing = XDomElement_attribute_utf8(element, "missing", "默认值");
    const XString* missingNS = XDomElement_attributeNS_utf8(element, "urn:none", "none", "命名空间默认值");
    XDomNamedNodeMap* attributes = XDomElement_attributes(element);
    XDomNamedNodeMap* attributesAgain = XDomElement_attributes(element);
    if (xdom_string_equals(XDomElement_attribute(element, name, NULL), "1.25") &&
        xdom_string_equals(XDomElement_attribute(element, ratioName, NULL), "1.2345679") &&
        xdom_string_equals(XDomElement_attributeNS(element, namespaceURI, localName, NULL), "7") &&
        xdom_string_equals(missing, "默认值") && xdom_string_equals(missingNS, "命名空间默认值") &&
        XDomNamedNodeMap_equals(attributes, attributesAgain))
        TEST_PASS("数值属性和 UTF-8 默认值");
    else { TEST_FAIL("属性重载", "数值格式或 UTF-8 默认值生命周期错误"); all_pass = false; }

    XDomImplementation_setInvalidDataPolicy(XDom_DropInvalidChars);
    XDomElement* dropped = XDomDocument_createElement_utf8(document, "~f~o~o~");
    XDomElement* droppedNS = XDomDocument_createElementNS_utf8(document, "urn:test", "foo:...:.");
    XDomElement* droppedOnlyInvalid = XDomDocument_createElement_utf8(document, "~");
    bool dropOk = dropped && droppedNS && droppedOnlyInvalid &&
                  xdom_string_equals(XDomElement_tagName(dropped), "foo") &&
                  xdom_string_equals(XDomElement_tagName(droppedNS), "foo::.") &&
                  xdom_string_equals(XDomElement_tagName(droppedOnlyInvalid), "");
    if (dropOk) TEST_PASS("DropInvalidChars 名称过滤");
    else { TEST_FAIL("DropInvalidChars", "非法名称字符未按 Qt 规则过滤"); all_pass = false; }

    XString* invalidTextData = XString_create_utf8("a");
    XString_append_char(invalidTextData, XChar_from(0x01));
    XString_append_utf8(invalidTextData, "b");
    XDomText* droppedText = XDomDocument_createTextNode(document, invalidTextData);
    XDomComment* droppedComment = XDomDocument_createComment_utf8(document, "a--b");
    XDomCDATASection* droppedCData = XDomDocument_createCDATASection_utf8(document, "a]]>b");
    XDomProcessingInstruction* droppedPI = XDomDocument_createProcessingInstruction_utf8(
        document, "target", "a?>b");
    if (droppedText && droppedComment && droppedCData && droppedPI &&
        xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)droppedText), "ab") &&
        xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)droppedComment), "ab") &&
        xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)droppedCData), "ab") &&
        xdom_string_equals(XDomProcessingInstruction_data(droppedPI), "ab"))
        TEST_PASS("DropInvalidChars 字符数据过滤");
    else { TEST_FAIL("DropInvalidChars 字符数据", "非法字符或保留分隔符未被过滤"); all_pass = false; }

    XDomImplementation_setInvalidDataPolicy(XDom_ReturnNullNode);
    XDomText* returnedText = XDomDocument_createTextNode(document, invalidTextData);
    XDomComment* returnedComment = XDomDocument_createComment_utf8(document, "a--b");
    XDomCDATASection* returnedCData = XDomDocument_createCDATASection_utf8(document, "a]]>b");
    XDomProcessingInstruction* returnedPI = XDomDocument_createProcessingInstruction_utf8(
        document, "target", "a?>b");
    XDomNode* returnedTextNode = XDomText_toNode(returnedText);
    XDomNode* returnedCommentNode = XDomComment_toNode(returnedComment);
    XDomNode* returnedCDataNode = XDomCDATASection_toNode(returnedCData);
    XDomNode* returnedPINode = XDomProcessingInstruction_toNode(returnedPI);
    if (returnedTextNode && XDomNode_isNull(returnedTextNode) &&
        returnedCommentNode && XDomNode_isNull(returnedCommentNode) &&
        returnedCDataNode && XDomNode_isNull(returnedCDataNode) &&
        returnedPINode && XDomNode_isNull(returnedPINode))
        TEST_PASS("ReturnNullNode 字符数据");
    else { TEST_FAIL("ReturnNullNode 字符数据", "非法字符数据未返回空节点"); all_pass = false; }
    XDomElement* invalidName = XDomDocument_createElement_utf8(document, "~f~o~o~");
    XDomElement* invalidQualified = XDomDocument_createElementNS_utf8(document, "urn:test", "foo:~");
    XDomNode* droppedOnlyInvalidNode = droppedOnlyInvalid ? XDomElement_toNode(droppedOnlyInvalid) : NULL;
    XDomNode* invalidNameNode = invalidName ? XDomElement_toNode(invalidName) : NULL;
    XDomNode* invalidQualifiedNode = invalidQualified ? XDomElement_toNode(invalidQualified) : NULL;
    if (xdom_node_is_valid(droppedOnlyInvalidNode) == false &&
        xdom_node_is_valid(invalidNameNode) == false &&
        xdom_node_is_valid(invalidQualifiedNode) == false)
        TEST_PASS("ReturnNullNode 非法名称");
    else { TEST_FAIL("ReturnNullNode", "非法名称未返回空节点"); all_pass = false; }
    XDomImplementation_setInvalidDataPolicy(XDom_AcceptInvalidChars);

    XClassDelete(returnedTextNode);
    XClassDelete(returnedCommentNode);
    XClassDelete(returnedCDataNode);
    XClassDelete(returnedPINode);
    XClassDelete(returnedPI);
    XClassDelete(returnedCData);
    XClassDelete(returnedComment);
    XClassDelete(returnedText);
    XClassDelete(droppedPI);
    XClassDelete(droppedCData);
    XClassDelete(droppedComment);
    XClassDelete(droppedText);
    XClassDelete(invalidTextData);
    XClassDelete(droppedOnlyInvalidNode);
    XClassDelete(invalidQualified);
    XClassDelete(invalidName);
    XClassDelete(droppedOnlyInvalid);
    XClassDelete(droppedNS);
    XClassDelete(dropped);
    XClassDelete(localName);
    XClassDelete(ratioName);
    XClassDelete(namespacedName);
    XClassDelete(namespaceURI);
    XClassDelete(name);
    XClassDelete(attributesAgain);
    XClassDelete(attributes);
    XClassDelete(invalidQualifiedNode);
    XClassDelete(invalidNameNode);
    XClassDelete(element);
    XClassDelete(document);
    return all_pass;
}

static bool test_handle_semantics(void)
{
    TEST_INFO("===== 句柄浅拷贝、移动和浅克隆 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* original = XDomDocument_createElement_utf8(document, "original");
    XDomNode* originalNode = XDomElement_toNode(original);
    XDomElement* child = XDomDocument_createElement_utf8(document, "child");
    XDomNode* childNode = XDomElement_toNode(child);
    XDomNode* appended = XDomNode_appendChild(originalNode, childNode);
    XClassDelete(appended);

    XDomElement copiedTarget = {0};
    XClassCopy(&copiedTarget, original);
    XDomElement_setAttribute_utf8(&copiedTarget, "shared", "yes");
    XDomNode* copiedNode = XDomElement_toNode(&copiedTarget);
    XDomNodeList* copiedChildren = XDomNode_childNodes(copiedNode);
    if (XDomElement_hasAttribute_utf8(original, "shared") &&
        copiedNode && !XDomNode_isNull(copiedNode) &&
        XDomNodeList_length(copiedChildren) == 1)
        TEST_PASS("浅拷贝共享树和属性修改");
    else { TEST_FAIL("浅拷贝", "复制句柄没有共享底层 DOM 数据"); all_pass = false; }

    XDomElement movedTarget = {0};
    XClassMove(&movedTarget, &copiedTarget);
    XDomNode* movedNode = XDomElement_toNode(&movedTarget);
    XDomNode* movedSourceNode = XDomElement_toNode(&copiedTarget);
    if (movedNode && !XDomNode_isNull(movedNode) &&
        xdom_node_is_null(movedSourceNode))
        TEST_PASS("移动后源句柄为空且可析构");
    else { TEST_FAIL("移动句柄", "移动后的源对象状态错误"); all_pass = false; }

    XDomElement uninitialized = {0};
    XDomElement* emptyCopy = XDomElement_create_copy(&uninitialized);
    XDomNode* emptyCopyNode = XDomElement_toNode(emptyCopy);
    if (emptyCopy && xdom_node_is_null(emptyCopyNode))
        TEST_PASS("未初始化源对象安全复制");
    else { TEST_FAIL("未初始化复制", "未初始化源对象导致非空或崩溃"); all_pass = false; }

    XDomNode* shallowClone = XDomNode_cloneNode(originalNode, false);
    XDomNodeList* shallowChildren = XDomNode_childNodes(shallowClone);
    if (shallowClone && !XDomNode_isNull(shallowClone) &&
        XDomNodeList_length(shallowChildren) == 0)
        TEST_PASS("浅克隆不复制子节点");
    else { TEST_FAIL("浅克隆", "浅克隆错误复制了子树"); all_pass = false; }

    XClassDelete(shallowChildren);
    XClassDelete(shallowClone);
    XClassDelete(emptyCopy);
    XClassDelete(movedNode);
    XClassDelete(copiedNode);
    XClassDelete(copiedChildren);
    XClassDeinit(&movedTarget);
    XClassDeinit(&copiedTarget);
    XClassDelete(childNode);
    XClassDelete(child);
    XClassDelete(originalNode);
    XClassDelete(original);
    XClassDelete(document);
    return all_pass;
}

static bool test_normalize_behavior(void)
{
    TEST_INFO("===== normalize 直接子节点行为 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* outer = XDomDocument_createElement_utf8(document, "outer");
    XDomElement* nested = XDomDocument_createElement_utf8(document, "nested");
    XDomText* firstText = XDomDocument_createTextNode_utf8(document, "a");
    XDomCDATASection* secondText = XDomDocument_createCDATASection_utf8(document, "b");
    XDomText* nestedFirst = XDomDocument_createTextNode_utf8(document, "x");
    XDomText* nestedSecond = XDomDocument_createTextNode_utf8(document, "y");
    XDomNode* outerNode = XDomElement_toNode(outer);
    XDomNode* nestedNode = XDomElement_toNode(nested);
    XDomNode* firstTextNode = XDomText_toNode(firstText);
    XDomNode* secondTextNode = XDomCDATASection_toNode(secondText);
    XDomNode* nestedFirstNode = XDomText_toNode(nestedFirst);
    XDomNode* nestedSecondNode = XDomText_toNode(nestedSecond);
    XDomNode* appended = XDomNode_appendChild(outerNode, firstTextNode);
    XClassDelete(appended);
    appended = XDomNode_appendChild(outerNode, secondTextNode);
    XClassDelete(appended);
    appended = XDomNode_appendChild(outerNode, nestedNode);
    XClassDelete(appended);
    appended = XDomNode_appendChild(nestedNode, nestedFirstNode);
    XClassDelete(appended);
    appended = XDomNode_appendChild(nestedNode, nestedSecondNode);
    XClassDelete(appended);

    XDomNode_normalize(outerNode);
    XDomNodeList* outerChildren = XDomNode_childNodes(outerNode);
    XDomNodeList* nestedChildren = XDomNode_childNodes(nestedNode);
    XDomNode* mergedNode = XDomNode_firstChild(outerNode);
    XDomCharacterData* mergedData = XDomNode_toCharacterData(mergedNode);
    if (XDomNodeList_length(outerChildren) == 2 &&
        XDomNodeList_length(nestedChildren) == 2 &&
        xdom_string_equals(XDomCharacterData_data(mergedData), "ab"))
        TEST_PASS("只合并当前层文本和字符数据区段");
    else { TEST_FAIL("normalize", "递归层级或文本合并结果不符合 Qt 行为"); all_pass = false; }

    XClassDelete(mergedData);
    XClassDelete(mergedNode);
    XClassDelete(nestedChildren);
    XClassDelete(outerChildren);
    XClassDelete(nestedSecondNode);
    XClassDelete(nestedSecond);
    XClassDelete(nestedFirstNode);
    XClassDelete(nestedFirst);
    XClassDelete(secondTextNode);
    XClassDelete(secondText);
    XClassDelete(firstTextNode);
    XClassDelete(firstText);
    XClassDelete(nestedNode);
    XClassDelete(nested);
    XClassDelete(outerNode);
    XClassDelete(outer);
    XClassDelete(document);
    return all_pass;
}

static bool test_attributes_and_character_data(void)
{
    TEST_INFO("===== 属性、命名空间和字符数据 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomElement* element = XDomDocument_createElement_utf8(document, "root");
    XDomNode* elementNode = XDomElement_toNode(element);
    XString* countName = XString_create_utf8("count");
    XString* namespaceName = XString_create_utf8("urn:test");
    XString* localName = XString_create_utf8("flag");
    XDomElement_setAttribute_utf8(element, "id", "42");
    XDomElement_setAttribute_int(element, countName, 7);
    XDomElement_setAttributeNS_utf8(element, "urn:test", "p:flag", "on");
    XDomNamedNodeMap* attributes = XDomElement_attributes(element);
    XDomNode* idNode = XDomNamedNodeMap_namedItem_utf8(attributes, "id");
    if (XDomNamedNodeMap_length(attributes) == 3 &&
        XDomNamedNodeMap_contains_utf8(attributes, "id") &&
        idNode && xdom_string_equals(XDomNode_nodeValue(idNode), "42") &&
        XDomElement_hasAttributeNS(element, namespaceName, localName))
        TEST_PASS("属性和命名节点映射");
    else { TEST_FAIL("属性和命名节点映射", "属性查询结果错误"); all_pass = false; }
    XClassDelete(idNode);
    XClassDelete(countName);
    XClassDelete(namespaceName);
    XClassDelete(localName);

    XDomText* text = XDomDocument_createTextNode_utf8(document, "ab");
    XDomNode* textNode = XDomText_toNode(text);
    bool textAppendOk = !xdom_node_is_null(xdom_append(elementNode, textNode));
    XDomText* suffix = XDomText_splitText(text, 1);
    XDomNodeList* textChildren = XDomNode_childNodes(elementNode);
    if (textAppendOk && suffix && xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)text), "a") &&
        xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)suffix), "b") &&
        XDomNodeList_length(textChildren) == 2)
        TEST_PASS("文本分割与 UTF-16 字符数据");
    else { TEST_FAIL("文本分割", "分割结果或子节点数量错误"); all_pass = false; }
    XDomCharacterData_appendData_utf8((XDomCharacterData*)suffix, "c");
    XString* inserted = XString_create_utf8("z");
    XDomCharacterData_insertData((XDomCharacterData*)suffix, 8, inserted);
    if (xdom_string_equals(XDomCharacterData_data((XDomCharacterData*)suffix), "bc"))
        TEST_PASS("字符数据超范围插入保持原值");
    else { TEST_FAIL("字符数据", "超范围插入未按 Qt 保持原值"); all_pass = false; }
    XClassDelete(inserted);

    XDomCDATASection* cdata = XDomDocument_createCDATASection_utf8(document, "a&<]]>b");
    XDomNode* cdataNode = XDomCDATASection_toNode(cdata);
    XString* cdataXml = XDomNode_toString(cdataNode, -1);
    if (cdataXml && xdom_string_equals(cdataXml, "<![CDATA[a&<]]]]><![CDATA[>b]]>"))
        TEST_PASS("字符数据区段原样序列化并拆分终止符");
    else { TEST_FAIL("字符数据区段序列化", cdataXml ? XString_toUtf8(cdataXml) : "序列化失败"); all_pass = false; }

    XClassDelete(cdataXml);
    XClassDelete(cdataNode);
    XClassDelete(cdata);
    XClassDelete(textChildren);
    XClassDelete(suffix);
    XClassDelete(textNode);
    XClassDelete(text);
    XClassDelete(attributes);
    XClassDelete(elementNode);
    XClassDelete(element);
    XClassDelete(document);
    return all_pass;
}

static bool test_alternate_input_sources(void)
{
    TEST_INFO("===== XString、Reader 和设备输入 =====");
    bool all_pass = true;
    XString* xmlText = XString_create_utf8(
        "<?xml version=\"1.0\" standalone=\"no\"?><root/>");
    XDomDocument* stringDocument = XDomDocument_create();
    XDomParseResult stringResult = XDomDocument_setContent_string_result(
        stringDocument, xmlText, XDom_ParseDefault);
    XString* stringSerialized = XDomDocument_toString(stringDocument, -1);
    if (XDomParseResult_isSuccess(&stringResult) && stringSerialized &&
        xdom_string_equals(stringSerialized,
                           "<?xml version='1.0' standalone='no'?><root/>"))
        TEST_PASS("XString 输入和 standalone=no 保留");
    else { TEST_FAIL("XString 输入", "XML 声明或 standalone=no 序列化错误"); all_pass = false; }

    XString* legacyError = NULL;
    int64_t legacyLine = 0;
    int64_t legacyColumn = 0;
    bool legacyStringOk = XDomDocument_setContent_string(
        stringDocument, xmlText, XDom_ParseDefault, &legacyError,
        &legacyLine, &legacyColumn);
    if (legacyStringOk && !legacyError)
        TEST_PASS("XString 旧式错误参数接口");
    else { TEST_FAIL("XString 旧式接口", "成功解析不应返回错误消息"); all_pass = false; }
    XClassDelete(legacyError);

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XDomDocument* readerDocument = XDomDocument_create();
    XXmlStreamReader_addData_utf8(reader, "<?xml version=\"1.0\"?><reader-root/>");
    XDomParseResult readerResult = XDomDocument_setContent_reader_result(
        readerDocument, reader, XDom_ParseDefault);
    XDomElement* readerRoot = XDomDocument_documentElement(readerDocument);
    if (XDomParseResult_isSuccess(&readerResult) && readerRoot &&
        XXmlStreamReader_hasXmlDeclaration(reader) &&
        xdom_string_equals(XDomElement_tagName(readerRoot), "reader-root"))
        TEST_PASS("已有 Reader 输入和声明查询");
    else { TEST_FAIL("Reader 输入", "Reader 解析或声明状态错误"); all_pass = false; }
    XClassDelete(readerRoot);

    XXmlStreamReader* legacyReader = XXmlStreamReader_create();
    XDomDocument* legacyReaderDocument = XDomDocument_create();
    XXmlStreamReader_addData_utf8(legacyReader, "<legacy-reader/>");
    XString* readerError = NULL;
    bool legacyReaderOk = XDomDocument_setContent_reader(
        legacyReaderDocument, legacyReader, XDom_ParseDefault, &readerError,
        NULL, NULL);
    if (legacyReaderOk && !readerError &&
        !XXmlStreamReader_hasXmlDeclaration(legacyReader))
        TEST_PASS("Reader 旧式接口和无声明状态");
    else { TEST_FAIL("Reader 旧式接口", "成功解析不应返回错误消息"); all_pass = false; }
    XClassDelete(readerError);

    XString* path = XString_create_utf8("xdom_device_input_test.xml");
    XFile_remove_static(path);
    XFile* writer = XFile_create_2(path);
    bool wrote = writer && XFile_open_2(writer,
        XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate, 0);
    if (wrote) {
        XIODevice_write_3((XIODevice*)writer, "<device-root/>");
        XIODevice_close_base((XIODevice*)writer);
    }
    if (writer) XFile_deleteLater(writer);
    XFile* input = wrote ? XFile_create_2(path) : NULL;
    bool opened = input && XFile_open_2(input, XIODevice_ReadOnly | XIODevice_Existing, 0);
    if (opened) XIODevice_close_base((XIODevice*)input);
    XDomDocument* deviceDocument = XDomDocument_create();
    XDomParseResult deviceResult = XDomDocument_setContent_device_result(
        deviceDocument, (XIODevice*)input, XDom_ParseDefault);
    XDomElement* deviceRoot = XDomDocument_documentElement(deviceDocument);
    if (opened && XIODevice_isOpen((XIODevice*)input) &&
        XDomParseResult_isSuccess(&deviceResult) && deviceRoot &&
        xdom_string_equals(XDomElement_tagName(deviceRoot), "device-root"))
        TEST_PASS("XIODevice 输入");
    else { TEST_FAIL("XIODevice 输入", "设备内容解析失败"); all_pass = false; }
    XClassDelete(deviceRoot);

    XString* deviceError = NULL;
    bool nullDeviceOk = XDomDocument_setContent_device(
        deviceDocument, NULL, XDom_ParseDefault, &deviceError, NULL, NULL);
    if (!nullDeviceOk && deviceError)
        TEST_PASS("空设备失败结果和错误消息");
    else { TEST_FAIL("空设备", "空设备应返回失败和错误消息"); all_pass = false; }
    XClassDelete(deviceError);

    XDomParseResult_deinit(&deviceResult);
    if (input) {
        XIODevice_close_base((XIODevice*)input);
        XFile_deleteLater(input);
    }
    XFile_remove_static(path);
    XClassDelete(path);
    XDomParseResult_deinit(&readerResult);
    XClassDelete(reader);
    XClassDelete(readerDocument);
    XClassDelete(legacyReader);
    XClassDelete(legacyReaderDocument);
    XClassDelete(deviceDocument);
    XDomParseResult_deinit(&stringResult);
    XClassDelete(stringSerialized);
    XClassDelete(xmlText);
    XClassDelete(stringDocument);
    return all_pass;
}

static bool test_qt_remaining_semantics(void)
{
    TEST_INFO("===== Qt 剩余行为差异回归 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    XDomAttr* attr = XDomDocument_createAttribute_utf8(document, "value");
    XDomNode* attrNode = XDomAttr_toNode(attr);
    XDomNodeList* attrChildren = XDomNode_childNodes(attrNode);
    if (attr && !XDomAttr_specified(attr) && XDomNodeList_length(attrChildren) == 0)
        TEST_PASS("新属性默认 specified=false 且没有子文本");
    else { TEST_FAIL("新属性默认状态", "属性初始状态未对齐 Qt"); all_pass = false; }

    XDomAttr_setValue_utf8(attr, "属性值");
    XDomNode* attrText = XDomNode_firstChild(attrNode);
    XDomNodeList* attrChildrenAfter = XDomNode_childNodes(attrNode);
    XDomNode* attrClone = XDomNode_cloneNode(attrNode, true);
    XDomNodeList* attrCloneChildren = XDomNode_childNodes(attrClone);
    if (XDomAttr_specified(attr) && XDomNodeList_length(attrChildrenAfter) == 1 &&
        attrText && XDomNode_isText(attrText) &&
        xdom_string_equals(XDomNode_nodeValue(attrText), "属性值") &&
        XDomNodeList_length(attrCloneChildren) == 1)
        TEST_PASS("属性值同步子文本并支持深克隆");
    else { TEST_FAIL("属性子文本", "setValue 未维护 Qt 属性子节点"); all_pass = false; }

    XDomElement* root = XDomDocument_createElement_utf8(document, "root");
    XDomElement* child = XDomDocument_createElement_utf8(document, "child");
    XDomNode* rootNode = XDomElement_toNode(root);
    XDomNode* childNode = XDomElement_toNode(child);
    XDomElement_setAttribute_utf8(root, "id", "1");
    XClassDelete(xdom_append(rootNode, childNode));
    XDomAttr* historicalAttr = XDomDocument_createAttribute_utf8(document, "historical");
    XDomNode* historicalAttrNode = XDomAttr_toNode(historicalAttr);
    XDomNode* historicalAttrResult = xdom_append(rootNode, historicalAttrNode);
    XDomNode* historicalAttrParent = XDomNode_parentNode(historicalAttrNode);
    if (historicalAttrResult && historicalAttrParent &&
        XDomNode_equals(historicalAttrParent, rootNode))
        TEST_PASS("Qt 历史兼容属性节点插入");
    else { TEST_FAIL("属性节点插入", "appendChild 未接受属性节点"); all_pass = false; }
    XClassDelete(historicalAttrResult);
    XClassDelete(historicalAttrParent);
    XDomNode* childByName = XDomNode_namedItem_utf8(rootNode, "child");
    XDomNode* attrByName = XDomNode_namedItem_utf8(rootNode, "id");
    XDomElement_setTagName_utf8(root, "~原样标签");
    if (childByName && !XDomNode_isNull(childByName) && attrByName &&
        XDomNode_isNull(attrByName) && xdom_string_equals(XDomElement_tagName(root), "~原样标签"))
        TEST_PASS("namedItem 查询直接子节点和 setTagName 原样赋值");
    else { TEST_FAIL("节点查询和改名", "namedItem 或 setTagName 行为未对齐 Qt"); all_pass = false; }

    XDomElement* namespacedElement = XDomDocument_createElementNS_utf8(
        document, "urn:namespace", "p:item");
    XDomNode* modifiedNamespaceNode = XDomElement_toNode(namespacedElement);
    XString* changedPrefix = XString_create_utf8("q");
    XString* changedTagName = XString_create_utf8("renamed");
    XDomNode_setPrefix(modifiedNamespaceNode, changedPrefix);
    XDomElement_setTagName(namespacedElement, changedTagName);
    if (xdom_string_equals(XDomNode_prefix(modifiedNamespaceNode), "q") &&
        xdom_string_equals(XDomNode_nodeName(modifiedNamespaceNode), "renamed") &&
        xdom_string_equals(XDomNode_localName(modifiedNamespaceNode), "item"))
        TEST_PASS("命名空间 setPrefix/setTagName 原样语义");
    else { TEST_FAIL("命名空间修改", "prefix 或 name/localName 不符合 Qt"); all_pass = false; }
    XClassDelete(changedTagName);
    XClassDelete(changedPrefix);

    XDomImplementation* implementation = XDomDocument_implementation(document);
    XDomDocumentType* type = XDomImplementation_createDocumentType_utf8(
        implementation, "root", "公共", NULL);
    if (type && xdom_string_equals(XDomDocumentType_publicId(type), "") &&
        xdom_string_equals(XDomDocumentType_systemId(type), ""))
        TEST_PASS("无 systemId 时清空 publicId");
    else { TEST_FAIL("文档类型标识符", "createDocumentType 的 null systemId 规则错误"); all_pass = false; }
    XDomDocumentType* emptySystemType = XDomImplementation_createDocumentType_utf8(
        implementation, "root", "公共", "");
    if (emptySystemType && xdom_string_equals(XDomDocumentType_publicId(emptySystemType), "公共") &&
        xdom_string_equals(XDomDocumentType_systemId(emptySystemType), ""))
        TEST_PASS("空 systemId 与 null systemId 区分");
    else { TEST_FAIL("空 systemId", "Qt 的空字符串标识符语义未保留"); all_pass = false; }

    XDomDocument* parsedDocument = XDomDocument_create();
    XDomParseResult parsed = XDomDocument_setContent_utf8_result(
        parsedDocument, "<!DOCTYPE root [<!ENTITY ext SYSTEM \"system-id\">]><root/>", XDom_ParseDefault);
    XDomDocumentType* parsedType = XDomDocument_doctype(parsedDocument);
    XDomNode* parsedTypeNode = XDomDocumentType_toNode(parsedType);
    XDomNodeList* parsedTypeChildren = XDomNode_childNodes(parsedTypeNode);
    XDomNode* parsedEntity = XDomNode_namedItem_utf8(parsedTypeNode, "ext");
    XDomNode* parsedDocumentNode = XDomDocument_toNode(parsedDocument);
    XDomNodeList* parsedDocumentChildren = XDomNode_childNodes(parsedDocumentNode);
    XString* parsedSerialized = XDomDocument_toString(parsedDocument, -1);
    if (XDomParseResult_isSuccess(&parsed) && parsedType && parsedEntity &&
        XDomNodeList_length(parsedTypeChildren) == 1 &&
        XDomNodeList_length(parsedDocumentChildren) == 2 && parsedSerialized &&
        xdom_string_equals(XDomNode_nodeName(parsedEntity), "ext") &&
        strstr(XString_toUtf8(parsedSerialized), "<!ENTITY ext"))
        TEST_PASS("doctype 独立保存、外部实体映射和序列化");
    else { TEST_FAIL("doctype 结构", "doctype 未按 Qt 私有 type 和子节点模型保存"); all_pass = false; }

    XDomDocument* internalDocument = XDomDocument_create();
    XDomParseResult internalParsed = XDomDocument_setContent_utf8_result(
        internalDocument, "<!DOCTYPE root [<!ENTITY hello \"world\">]><root>&hello;</root>",
        XDom_ParseDefault);
    XDomElement* internalRoot = XDomDocument_documentElement(internalDocument);
    XDomNode* internalRootNode = XDomElement_toNode(internalRoot);
    XDomDocumentType* internalType = XDomDocument_doctype(internalDocument);
    XDomNamedNodeMap* internalEntities = XDomDocumentType_entities(internalType);
    if (XDomParseResult_isSuccess(&internalParsed) && internalRoot && internalEntities &&
        XDomNamedNodeMap_length(internalEntities) == 0 &&
        xdom_string_equals(XDomElement_text(internalRoot), "world"))
        TEST_PASS("内部实体按 Qt 展开且不预先加入映射");
    else { TEST_FAIL("内部实体", "内部实体展开或 entities 映射语义未对齐 Qt"); all_pass = false; }

    XDomDocument* multipleDtdDocument = XDomDocument_create();
    XDomParseResult multipleDtd = XDomDocument_setContent_utf8_result(
        multipleDtdDocument, "<!DOCTYPE one><!DOCTYPE two><one/>", XDom_ParseDefault);
    if (!XDomParseResult_isSuccess(&multipleDtd) && multipleDtd.m_errorMessage &&
        strstr(XString_toUtf8(multipleDtd.m_errorMessage), "DTD"))
        TEST_PASS("重复 DTD 按 Qt 规则拒绝");
    else { TEST_FAIL("重复 DTD", "同一文档中的多个 DTD 未返回解析错误"); all_pass = false; }

    XDomElement* ordered = XDomDocument_createElement_utf8(document, "ordered");
    XDomElement_setAttribute_utf8(ordered, "z", "1");
    XDomElement_setAttribute_utf8(ordered, "a", "2");
    XDomElement_setAttributeNS_utf8(ordered, "urn:p", "p:b", "3");
    XDomNode* orderedNode = XDomElement_toNode(ordered);
    XString* orderedText = XDomNode_toString(orderedNode, -1);
    const char* orderedUtf8 = orderedText ? XString_toUtf8(orderedText) : "";
    const char* aPosition = strstr(orderedUtf8, " a=\"2\"");
    const char* zPosition = strstr(orderedUtf8, " z=\"1\"");
    const char* pPosition = strstr(orderedUtf8, " p:b=\"3\"");
    if (aPosition && zPosition && pPosition && aPosition < zPosition && zPosition < pPosition)
        TEST_PASS("属性按 prefix 和 name 稳定排序");
    else { TEST_FAIL("属性序列化顺序", orderedUtf8); all_pass = false; }

    XDomElement* namespaced = XDomDocument_createElementNS_utf8(
        document, "urn:root", "p:root");
    XDomElement_setAttributeNS_utf8(namespaced, "urn:attr", "q:id", "7");
    XDomNode* namespacedNode = XDomElement_toNode(namespaced);
    XString* namespacedText = XDomNode_toString(namespacedNode, -1);
    const char* namespacedUtf8 = namespacedText ? XString_toUtf8(namespacedText) : "";
    if (strstr(namespacedUtf8, "xmlns:p=\"urn:root\"") &&
        strstr(namespacedUtf8, "xmlns:q=\"urn:attr\"") &&
        strstr(namespacedUtf8, "q:id=\"7\""))
        TEST_PASS("命名空间元素和属性自动补全 xmlns");
    else { TEST_FAIL("命名空间序列化", namespacedUtf8); all_pass = false; }

    XString* savePath = XString_create_utf8("xdom_policy_test.xml");
    XFile_remove_static(savePath);
    XFile* saveFile = XFile_create_2(savePath);
    bool saveOpened = saveFile && XFile_open_2(saveFile,
        XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate, 0);
    XDomNode* documentNode = XDomDocument_toNode(document);
    bool policySaved = saveOpened && XDomNode_save(documentNode, (XIODevice*)saveFile, -1,
                                                   XDom_EncodingFromTextStream);
    if (saveFile) {
        XIODevice_close_base((XIODevice*)saveFile);
        XFile_deleteLater(saveFile);
    }
    XFile* policyInput = XFile_create_2(savePath);
    bool policyReadOpened = policyInput && XFile_open_2(policyInput,
        XIODevice_ReadOnly | XIODevice_Existing, 0);
    XByteArray* policyBytes = policyReadOpened ? XIODevice_readAll_3((XIODevice*)policyInput) : NULL;
    if (policyBytes) XByteArray_append_1(policyBytes, 0);
    const char* policyText = policyBytes ? (const char*)XByteArray_data(policyBytes) : NULL;
    if (policySaved && policyText && strstr(policyText, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"))
        TEST_PASS("EncodingFromTextStream 输出 UTF-8 XML 声明");
    else { TEST_FAIL("save 编码策略", "EncodingFromTextStream 未输出 Qt 风格声明"); all_pass = false; }
    XClassDelete(policyBytes);
    if (policyInput) {
        XIODevice_close_base((XIODevice*)policyInput);
        XFile_deleteLater(policyInput);
    }
    XFile_remove_static(savePath);

    XClassDelete(documentNode);
    XClassDelete(savePath);
    XClassDelete(orderedText);
    XClassDelete(orderedNode);
    XClassDelete(ordered);
    XClassDelete(namespacedText);
    XClassDelete(modifiedNamespaceNode);
    XClassDelete(namespaced);
    XClassDelete(parsedSerialized);
    XClassDelete(parsedDocumentChildren ? parsedDocumentNode : NULL);
    XClassDelete(parsedDocumentChildren);
    XClassDelete(parsedEntity);
    XClassDelete(parsedTypeChildren);
    XClassDelete(parsedTypeNode);
    XClassDelete(parsedType);
    XDomParseResult_deinit(&parsed);
    XClassDelete(parsedDocument);
    XClassDelete(internalEntities);
    XClassDelete(internalType);
    XClassDelete(internalRootNode);
    XClassDelete(internalRoot);
    XDomParseResult_deinit(&internalParsed);
    XClassDelete(internalDocument);
    XDomParseResult_deinit(&multipleDtd);
    XClassDelete(multipleDtdDocument);
    XClassDelete(type);
    XClassDelete(emptySystemType);
    XClassDelete(implementation);
    XClassDelete(attrClone);
    XClassDelete(attrCloneChildren);
    XClassDelete(attrText);
    XClassDelete(attrChildrenAfter);
    XClassDelete(attrChildren);
    XClassDelete(attrNode);
    XClassDelete(attr);
    XClassDelete(namespacedNode);
    XClassDelete(namespacedElement);
    XClassDelete(historicalAttrNode);
    XClassDelete(historicalAttr);
    XClassDelete(attrByName);
    XClassDelete(childByName);
    XClassDelete(childNode);
    XClassDelete(child);
    XClassDelete(rootNode);
    XClassDelete(root);
    XClassDelete(document);
    return all_pass;
}

static bool test_parse_and_serialize(void)
{
    TEST_INFO("===== 内容设置、DTD 和序列化回读 =====");
    bool all_pass = true;
    XDomDocument* document = XDomDocument_create();
    const char* xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<!DOCTYPE root [<!-- [comment] --> <!ENTITY hello \"world\">"
                      "<!ENTITY ext SYSTEM \"system-id\">]>"
                      "<root xmlns:p=\"urn:p\" p:id=\"7\"><p:item><![CDATA[a&<]]></p:item>&hello;</root>";
    XDomParseResult parsed = XDomDocument_setContent_utf8_result(
        document, xml, XDom_UseNamespaceProcessing | XDom_PreserveSpacingOnlyNodes);
    XDomElement* root = XDomDocument_documentElement(document);
    XDomDocumentType* type = XDomDocument_doctype(document);
    XDomNamedNodeMap* entities = XDomDocumentType_entities(type);
    XDomNode* declarationDocumentRoot = XDomDocument_toNode(document);
    XDomNode* declarationNode = XDomNode_firstChild(declarationDocumentRoot);
    XDomProcessingInstruction* declaration =
        XDomNode_toProcessingInstruction(declarationNode);
    if (XDomParseResult_isSuccess(&parsed) && root &&
        xdom_string_equals(XDomElement_tagName(root), "root") &&
        type && xdom_string_equals(XDomDocumentType_internalSubset(type),
                                   "<!-- [comment] --> <!ENTITY hello \"world\"><!ENTITY ext SYSTEM \"system-id\">") &&
        entities && XDomNamedNodeMap_contains_utf8(entities, "ext"))
        TEST_PASS("内容解析命名空间和 DTD");
    else { TEST_FAIL("内容解析", parsed.m_errorMessage ? XString_toUtf8(parsed.m_errorMessage) : "文档内容不完整"); all_pass = false; }
    if (declaration &&
        xdom_string_equals(XDomProcessingInstruction_target(declaration), "xml") &&
        XString_toUtf8(XDomProcessingInstruction_data(declaration)) &&
        strstr(XString_toUtf8(XDomProcessingInstruction_data(declaration)),
               "version='1.0'") != NULL)
        TEST_PASS("XML 声明映射为首个 xml 处理指令");
    else { TEST_FAIL("XML 声明节点", "未按 QDomParser 创建 xml 处理指令"); all_pass = false; }
    XDomNode* entityRemoval = XDomNamedNodeMap_removeNamedItem_utf8(entities, "ext");
    if (xdom_node_is_null(entityRemoval) && entities &&
        XDomNamedNodeMap_contains_utf8(entities, "ext"))
        TEST_PASS("实体声明映射只读");
    else { TEST_FAIL("实体声明映射", "只读实体映射允许删除节点"); all_pass = false; }

    XDomDocument* noNamespaceDocument = XDomDocument_create();
    XDomParseResult noNamespaceResult = XDomDocument_setContent_utf8_result(
        noNamespaceDocument, xml, XDom_ParseDefault);
    XDomElement* noNamespaceRoot = XDomDocument_documentElement(noNamespaceDocument);
    XDomNode* noNamespaceRootNode = XDomElement_toNode(noNamespaceRoot);
    XDomElement* noNamespaceChild = XDomNode_firstChildElement(
        noNamespaceRootNode, NULL, NULL);
    XDomNode* noNamespaceChildNode = XDomElement_toNode(noNamespaceChild);
    XString* attemptedPrefix = XString_create_utf8("attempted");
    XDomNode_setPrefix(noNamespaceRootNode, attemptedPrefix);
    if (XDomParseResult_isSuccess(&noNamespaceResult) && noNamespaceChildNode &&
        xdom_string_equals(XDomNode_prefix(noNamespaceRootNode), "") &&
        xdom_string_equals(XDomNode_localName(noNamespaceRootNode), "") &&
        xdom_string_equals(XDomNode_namespaceURI(noNamespaceRootNode), "") &&
        xdom_string_equals(XDomNode_nodeName(noNamespaceChildNode), "p:item") &&
        xdom_string_equals(XDomNode_localName(noNamespaceChildNode), ""))
        TEST_PASS("关闭命名空间后的节点字段");
    else { TEST_FAIL("关闭命名空间", "prefix、localName 或 namespaceURI 未清空"); all_pass = false; }
    XClassDelete(attemptedPrefix);
    XClassDelete(noNamespaceChildNode);
    XClassDelete(noNamespaceChild);
    XClassDelete(noNamespaceRootNode);
    XClassDelete(noNamespaceRoot);
    XDomParseResult_deinit(&noNamespaceResult);
    XClassDelete(noNamespaceDocument);

    XString* serialized = XDomDocument_toString(document, -1);
    XDomDocument* roundTrip = XDomDocument_create();
    XDomParseResult reparsed = XDomDocument_setContent_utf8_result(
        roundTrip, serialized ? XString_toUtf8(serialized) : "", XDom_UseNamespaceProcessing);
    XDomElement* roundRoot = XDomDocument_documentElement(roundTrip);
    if (XDomParseResult_isSuccess(&reparsed) && roundRoot)
        TEST_PASS("文档序列化后可再次解析");
    else {
        TEST_FAIL("序列化回读", reparsed.m_errorMessage ? XString_toUtf8(reparsed.m_errorMessage) : "回读失败");
        TEST_INFO("序列化内容: %s", serialized ? XString_toUtf8(serialized) : "(空)");
        all_pass = false;
    }

    XDomDocument* invalid = XDomDocument_create();
    XDomParseResult error = XDomDocument_setContent_utf8_result(invalid, "<root>", XDom_ParseDefault);
    if (!XDomParseResult_isSuccess(&error) && error.m_errorLine > 0 && error.m_errorColumn > 0)
        TEST_PASS("内容解析错误消息和行列");
    else {
        TEST_FAIL("内容解析错误", "不完整 XML 未提供错误位置");
        TEST_INFO("错误行列: %lld,%lld", (long long)error.m_errorLine,
                  (long long)error.m_errorColumn);
        all_pass = false;
    }

    XDomParseResult_deinit(&error);
    XClassDelete(invalid);
    XDomDocument* declarationDocument = XDomDocument_create();
    XDomParseResult declarationParsed = XDomDocument_setContent_utf8_result(
        declarationDocument, "<?xml version=\"1.0\"?><root/>", XDom_ParseDefault);
    XDomNode* declarationDocumentNode = XDomDocument_toNode(declarationDocument);
    XDomNode* declarationToRemove = XDomNode_firstChild(declarationDocumentNode);
    XDomNode* removedDeclaration = XDomNode_removeChild(
        declarationDocumentNode, declarationToRemove);
    XString* withoutDeclaration = XDomDocument_toString(declarationDocument, -1);
    if (XDomParseResult_isSuccess(&declarationParsed) && removedDeclaration &&
        withoutDeclaration &&
        strstr(XString_toUtf8(withoutDeclaration), "<?xml") == NULL)
        TEST_PASS("移除 xml 处理指令后不再合成 XML 声明");
    else { TEST_FAIL("移除 XML 声明", "文档序列化仍错误保留 XML 声明"); all_pass = false; }
    XClassDelete(withoutDeclaration);
    XClassDelete(removedDeclaration);
    XClassDelete(declarationToRemove);
    XClassDelete(declarationDocumentNode);
    XDomParseResult_deinit(&declarationParsed);
    XClassDelete(declarationDocument);
    XDomParseResult_deinit(&reparsed);
    XClassDelete(roundRoot);
    XClassDelete(roundTrip);
    XClassDelete(serialized);
    XClassDelete(entities);
    XClassDelete(declaration);
    XClassDelete(declarationNode);
    XClassDelete(declarationDocumentRoot);
    XClassDelete(type);
    XClassDelete(root);
    XDomParseResult_deinit(&parsed);
    XClassDelete(document);
    return all_pass;
}

bool XDomTest_runAll(void)
{
    bool all_pass = true;
    all_pass = test_implementation_and_conversions() && all_pass;
    all_pass = test_tree_and_live_lists() && all_pass;
    all_pass = test_qt_tree_and_import_semantics() && all_pass;
    all_pass = test_qt_attribute_and_policy_semantics() && all_pass;
    all_pass = test_handle_semantics() && all_pass;
    all_pass = test_normalize_behavior() && all_pass;
    all_pass = test_attributes_and_character_data() && all_pass;
    all_pass = test_alternate_input_sources() && all_pass;
    all_pass = test_qt_remaining_semantics() && all_pass;
    all_pass = test_parse_and_serialize() && all_pass;
    TEST_INFO("XDom 测试总结果: %s", all_pass ? "通过" : "失败");
    return all_pass;
}

#if DEMOTEST
static void xdom_run_all_wrapper(XVariant* data)
{
    (void)data;
    XDomTest_runAll();
}

void XTestMenu_XDomTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XDomTest");
    XTestMenu_addMenu(root, menu);
    XAction* action = XTestMenu_addAction(menu, "全部测试");
    XTestMenu_setActionFunction(action, xdom_run_all_wrapper);
}
#endif
