#include "XExcelExtendedTest.h"

#include "XAbstractSheet.h"
#include "XByteArray.h"
#include "XCell.h"
#include "XCellFormula.h"
#include "XChart.h"
#include "XChartsheet.h"
#include "XClass.h"
#include "XConditionalFormatting.h"
#include "XContentTypes.h"
#include "XDataValidation.h"
#include "XDocPropsApp.h"
#include "XDocPropsCore.h"
#include "XDocument.h"
#include "XDrawing.h"
#include "XDrawingAnchor.h"
#include "XFile.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XMediaFile.h"
#include "XNumFormatParser.h"
#include "XPrintf.h"
#include "XReadSax.h"
#include "XRelationships.h"
#include "XSharedStrings.h"
#include "XSimpleOOXmlFile.h"
#include "XString.h"
#include "XStringList.h"
#include "XStyles.h"
#include "XTheme.h"
#include "XUtility.h"
#include "XVector.h"
#include "XWorkbook.h"
#include "XWorksheet.h"
#include "XXmlStreamReader.h"
#include "XXmlStreamWriter.h"
#include "XZipReader.h"
#include "XZipWriter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* xexcel_asset_path(const char* name)
{
    static char path[512];
    XString candidate;
    if (!name) return "";
    snprintf(path, sizeof(path), "assets/%s", name);
    XString_init(&candidate);
    XString_assign_utf8(&candidate, path);
    if (XFile_exists_static(&candidate)) {
        XString_deinit_base(&candidate);
        return path;
    }
    snprintf(path, sizeof(path), "../assets/%s", name);
    XString_assign_utf8(&candidate, path);
    bool exists = XFile_exists_static(&candidate);
    XString_deinit_base(&candidate);
    return exists ? path : "";
}

static int g_checks;
static int g_failures;

#define CHECK(condition, message) do { \
    ++g_checks; \
    if (condition) XPrintf("[PASS] %s\n", (message)); \
    else { XPrintf("[FAIL] %s\n", (message)); ++g_failures; groupOk = false; } \
} while (0)

typedef struct SaxResult {
    int count;
    int row[8];
    int column[8];
    char value[8][1024];
    char type[8][32];
} SaxResult;

static bool collect_cell(int row, int column, const XString* value,
                         const XString* type, void* userData)
{
    SaxResult* result = (SaxResult*)userData;
    if (!result || result->count >= 8) return false;
    int index = result->count++;
    result->row[index] = row;
    result->column[index] = column;
    snprintf(result->value[index], sizeof(result->value[index]), "%s",
             value ? XString_toUtf8(value) : "");
    snprintf(result->type[index], sizeof(result->type[index]), "%s",
             type ? XString_toUtf8(type) : "");
    return true;
}

static bool test_sax_boundaries(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：SAX 边界、共享字符串与长文本\n");
    XString_Init_Utf8(maxRef, "$XFD$1048576");
    XString_Init_Utf8(badColumn, "XFE1");
    XString_Init_Utf8(badRow, "A1048577");
    int row = 0;
    int column = 0;
    CHECK(XReadSax_parseCellRef(maxRef, &row, &column) && row == 1048576 && column == 16384,
          "XReadSax 接受 Excel 最大单元格引用");
    CHECK(!XReadSax_parseCellRef(badColumn, &row, &column), "XReadSax 拒绝越界列 XFE");
    CHECK(!XReadSax_parseCellRef(badRow, &row, &column), "XReadSax 拒绝越界行");
    XCellReference maxCell = XCellReference_create_str(maxRef);
    XCellReference tooLargeCell = XCellReference_create_ex(1048577, 16385);
    XCellRange maxRange = XCellRange_create_ex(1, 1, 1048576, 16384);
    XCellRange tooLargeRange = XCellRange_create_ex(1, 1, 1048577, 16385);
    CHECK(XCellReference_isValid(&maxCell) && !XCellReference_isValid(&tooLargeCell),
          "CellReference 与 SAX 使用相同的 Excel 行列上界");
    CHECK(XCellRange_isValid(&maxRange) && !XCellRange_isValid(&tooLargeRange),
          "CellRange 接受最大范围并拒绝越界范围");
    XString_Init_Utf8(sharedFormula, "A1+$XFD$1048576");
    XCellReference sourceCell = XCellReference_create_ex(1, 1);
    XCellReference targetCell = XCellReference_create_ex(1048576, 16384);
    XString translatedFormula = XUtility_convertSharedFormula(sharedFormula, &sourceCell, &targetCell);
    CHECK(XString_equals_utf8(&translatedFormula, "XFD1048576+$XFD$1048576",
              XChar_CaseSensitive),
          "共享公式偏移结果限制在 Excel 最大行列边界");
    XString_deinit_base(&translatedFormula);
    XString_deinit_base(sharedFormula);
    XString_deinit_base(maxRef);
    XString_deinit_base(badColumn);
    XString_deinit_base(badRow);

    const char* sharedXml =
        "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<si><t>alpha &amp; beta</t></si>"
        "<si><r><t>rich</t></r><r><t> text</t></r></si></sst>";
    XStringList* shared = XStringList_create();
    CHECK(shared && XReadSax_loadSharedStringsXml((const uint8_t*)sharedXml,
          strlen(sharedXml), shared), "解析 sharedStrings.xml");
    CHECK(shared && XStringList_size_base((XContainer*)shared) == 2,
          "共享字符串数量包含普通文本和富文本");
    XString* rich = shared ? (XString*)XStringList_at_base((XVector*)shared, 1) : NULL;
    CHECK(rich && XString_equals_utf8(rich, "rich text", XChar_CaseSensitive),
          "富文本片段按顺序合并");

    XString sheetXml;
    XString_init(&sheetXml);
    XString_append_utf8(&sheetXml,
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData><row r=\"1\">"
        "<c r=\"A1\" t=\"s\"><v>1</v></c><c r=\"B1\" t=\"inlineStr\"><is><t>");
    for (int i = 0; i < 700; ++i) XString_push_back_base(&sheetXml, (XChar)'x');
    XString_append_utf8(&sheetXml,
        "</t></is></c><c r=\"C1\"><f>1+1</f><v>2</v></c></row></sheetData></worksheet>");
    SaxResult result;
    memset(&result, 0, sizeof(result));
    CHECK(XReadSax_readSheetXml((const uint8_t*)XString_toUtf8(&sheetXml),
          strlen(XString_toUtf8(&sheetXml)), shared, NULL, collect_cell, &result),
          "SAX 解析工作表 XML");
    CHECK(result.count == 3, "SAX 回调覆盖共享、内联和公式结果单元格");
    CHECK(strcmp(result.value[0], "rich text") == 0, "共享字符串索引解析正确");
    CHECK(strlen(result.value[1]) == 700, "内联字符串不再截断到固定缓冲区");
    CHECK(strcmp(result.value[2], "2") == 0, "公式文本不会与缓存结果拼接");
    XString_deinit_base(&sheetXml);
    if (shared) XStringList_delete_base(shared);
    return groupOk;
}

static bool test_xmlstream_qt_behaviour(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：XXmlStream Qt 6.8 命名空间与增量追加行为\n");
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(reader, "<abc:root r:id=\"9\"");
    CHECK(XXmlStreamReader_readNext(reader) == XXmlStream_Invalid &&
          XXmlStreamReader_error(reader) == XXmlStream_PrematureEndOfDocumentError,
          "不完整分块报告 PrematureEndOfDocumentError");
    XXmlStreamReader_addData_utf8(reader,
        " xmlns:r=\"urn:rel\" xmlns:abc=\"urn:root\"><abc:child r:id=\"10\"/></abc:root>");
    CHECK(!XXmlStreamReader_hasError(reader), "追加数据后可从 premature error 恢复");
    CHECK(XXmlStreamReader_readNext(reader) == XXmlStream_StartElement,
          "追加后重新得到根开始元素");
    CHECK(XString_equals_utf8(XXmlStreamReader_namespaceUri_const(reader), "urn:root",
          XChar_CaseSensitive), "元素使用同一开始标签中后置声明的命名空间");
    CHECK(XXmlStreamReader_namespaceDeclarationsCount(reader) == 2,
          "根元素公开当前命名空间声明");
    const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
    XString_Init_Utf8(qualifiedId, "r:id");
    XString_Init_Utf8(namespaceUri, "urn:rel");
    XString_Init_Utf8(localId, "id");
    const XString* idByQualifiedName = XXmlStreamAttributes_value(attributes, qualifiedId);
    const XString* idByNamespace = XXmlStreamAttributes_value_ex(attributes, namespaceUri, localId);
    CHECK(idByQualifiedName && XString_equals_utf8(idByQualifiedName, "9", XChar_CaseSensitive),
          "属性保留限定名 r:id");
    CHECK(idByNamespace && XString_equals_utf8(idByNamespace, "9", XChar_CaseSensitive),
          "属性可按命名空间 URI 和本地名查询");
    const XXmlStreamAttribute* idAttribute = XXmlStreamAttributes_at(attributes, 0);
    CHECK(idAttribute && XString_equals_utf8(XXmlStreamAttribute_prefix(idAttribute), "r",
          XChar_CaseSensitive), "属性前缀为对象内稳定数据");
    XString_deinit_base(qualifiedId);
    XString_deinit_base(namespaceUri);
    XString_deinit_base(localId);

    XXmlStreamReader* copy = XXmlStreamReader_create_copy(reader);
    CHECK(copy && XXmlStreamReader_readNext(copy) == XXmlStream_StartElement &&
          XString_equals_utf8(XXmlStreamReader_namespaceUri_const(copy), "urn:root", XChar_CaseSensitive),
          "Reader 深拷贝保留读取位置和命名空间作用域");
    CHECK(XXmlStreamReader_readNext(reader) == XXmlStream_StartElement &&
          XString_equals_utf8(XXmlStreamReader_namespaceUri_const(reader), "urn:root", XChar_CaseSensitive),
          "子元素继承父元素命名空间绑定");
    CHECK(XXmlStreamReader_namespaceDeclarationsCount(reader) == 0,
          "子元素不重复报告父元素声明");
    if (copy) XXmlStreamReader_delete_base(copy);
    XXmlStreamReader_delete_base(reader);

    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_ex_utf8(writer, "urn:element", "root");
    XXmlStreamWriter_writeAttribute_ex_utf8(writer, "urn:attribute", "id", "7");
    XXmlStreamWriter_writeStartElement_utf8(writer, "child");
    XXmlStreamWriter_writeCharacters_utf8(writer, "text");
    XXmlStreamWriter_writeEndDocument(writer);
    const char* writtenXml = XXmlStreamWriter_toString(writer);
    CHECK(writtenXml && strstr(writtenXml, "<root xmlns=\"urn:element\"") &&
          strstr(writtenXml, "</child></root>"),
          "Writer 命名空间 URI 不再被当作前缀且 EndDocument 自动闭合元素");

    XXmlStreamReader* writtenReader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(writtenReader, writtenXml);
    while (!XXmlStreamReader_atEnd(writtenReader) &&
           !XXmlStreamReader_isStartElement(writtenReader)) {
        XXmlStreamReader_readNext(writtenReader);
    }
    XString_Init_Utf8(attributeNamespace, "urn:attribute");
    XString_Init_Utf8(attributeName, "id");
    const XString* generatedAttribute = XXmlStreamAttributes_value_ex(
        XXmlStreamReader_attributes(writtenReader), attributeNamespace, attributeName);
    CHECK(XString_equals_utf8(XXmlStreamReader_namespaceUri_const(writtenReader),
          "urn:element", XChar_CaseSensitive),
          "Writer 生成元素可由 Reader 解析为正确命名空间 URI");
    CHECK(generatedAttribute && XString_equals_utf8(generatedAttribute, "7", XChar_CaseSensitive),
          "Writer 自动声明命名空间属性前缀并可按 URI 查询");
    XString_deinit_base(attributeNamespace);
    XString_deinit_base(attributeName);
    XXmlStreamReader_delete_base(writtenReader);
    XXmlStreamWriter_delete_base(writer);

    XXmlStreamReader* textReader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(textReader, "<g>before<x>inside</x>after<!--ignored--></g>");
    while (!XXmlStreamReader_atEnd(textReader) && !XXmlStreamReader_isStartElement(textReader)) {
        XXmlStreamReader_readNext(textReader);
    }
    const XString* includedText = XXmlStreamReader_readElementText_const(textReader,
        XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
    CHECK(includedText && XString_equals_utf8(includedText, "beforeinsideafter", XChar_CaseSensitive),
          "readElementText 合并多段文本和子元素文本");
    CHECK(XXmlStreamReader_isEndElement(textReader),
          "readElementText 返回时定位在对应结束元素");
    XXmlStreamReader_delete_base(textReader);

    textReader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(textReader, "<g>before<x>inside</x>after</g>");
    while (!XXmlStreamReader_atEnd(textReader) && !XXmlStreamReader_isStartElement(textReader)) {
        XXmlStreamReader_readNext(textReader);
    }
    const XString* skippedText = XXmlStreamReader_readElementText_const(textReader,
        XXmlStream_ReadElementTextBehaviour_SkipChildElements);
    CHECK(skippedText && XString_equals_utf8(skippedText, "beforeafter", XChar_CaseSensitive),
          "readElementText SkipChildElements 跳过子元素内容但保留前后文本");
    XXmlStreamReader_delete_base(textReader);
    return groupOk;
}

static bool read_file_bytes(const XString* path, XByteArray** output)
{
    *output = NULL;
    XFile* file = XFile_create_2((XString*)path);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    *output = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    return *output != NULL;
}

static bool test_zip_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：ZIP 文件、内存读取与 CRC\n");
    XString_Init_Utf8(path, "/tmp/xinyue_excel_extended.zip");
    XString_Init_Utf8(directory, "folder/");
    XString_Init_Utf8(firstName, "first.txt");
    XString_Init_Utf8(secondName, "folder/second.bin");
    XString_Init_Utf8(emptyName, "empty.dat");
    const uint8_t firstData[] = "first-entry-at-offset-zero";
    const uint8_t secondData[] = {0, 1, 2, 3, 0xfe, 0xff};
    XZipWriter* writer = XZipWriter_create(path);
    CHECK(writer != NULL, "创建 ZIP 写入器");
    CHECK(writer && XZipWriter_addFile(writer, firstName, firstData, sizeof(firstData) - 1),
          "写入首个本地头偏移为 0 的条目");
    CHECK(writer && XZipWriter_addDirectory(writer, directory), "写入目录条目");
    CHECK(writer && XZipWriter_addFile(writer, secondName, secondData, sizeof(secondData)),
          "写入含 NUL 的二进制条目");
    CHECK(writer && XZipWriter_addFile(writer, emptyName, NULL, 0),
          "写入存储模式的空文件条目");
    CHECK(writer && !XZipWriter_addFile(writer, firstName, firstData, sizeof(firstData) - 1),
          "拒绝 ZIP 内重复路径");
    CHECK(writer && XZipWriter_close(writer), "关闭 ZIP 并写入中央目录");
    CHECK(writer && XZipWriter_close(writer) &&
          !XZipWriter_addFile(writer, firstName, firstData, sizeof(firstData) - 1),
          "ZIP close 幂等且关闭后拒绝追加条目");
    if (writer) XZipWriter_delete(writer);

    XZipReader* fileReader = XZipReader_create(path);
    CHECK(fileReader && XZipReader_exists(fileReader), "从文件识别有效 ZIP");
    XByteArray* first = fileReader ? XZipReader_fileData(fileReader, firstName) : NULL;
    CHECK(first && XByteArray_size_base((XContainer*)first) == sizeof(firstData) - 1 &&
          memcmp(XByteArray_data(first), firstData, sizeof(firstData) - 1) == 0,
          "读取首条目并通过 CRC 校验");
    XByteArray* empty = fileReader ? XZipReader_fileData(fileReader, emptyName) : NULL;
    CHECK(empty && XByteArray_size_base((XContainer*)empty) == 0,
          "读取存储模式空文件");
    if (empty) XByteArray_delete_base(empty);
    if (first) XByteArray_delete_base(first);
    if (fileReader) XZipReader_delete(fileReader);

    XByteArray* archive = NULL;
    CHECK(read_file_bytes(path, &archive), "读取 ZIP 原始字节");
    XZipReader* memoryReader = archive ? XZipReader_createFromData(
        XByteArray_data(archive), XByteArray_size_base((XContainer*)archive)) : NULL;
    CHECK(memoryReader && XZipReader_exists(memoryReader), "从内存创建 ZIP 读取器");
    XByteArray* second = memoryReader ? XZipReader_fileData(memoryReader, secondName) : NULL;
    CHECK(second && XByteArray_size_base((XContainer*)second) == sizeof(secondData) &&
          memcmp(XByteArray_data(second), secondData, sizeof(secondData)) == 0,
          "内存 ZIP 正确读取二进制条目");
    if (second) XByteArray_delete_base(second);
    if (memoryReader) XZipReader_delete(memoryReader);
    if (archive) XByteArray_delete_base(archive);

    XString_Init_Utf8(autoPath, "/tmp/xinyue_excel_autoclose.zip");
    XZipWriter* autoWriter = XZipWriter_create(autoPath);
    CHECK(autoWriter && XZipWriter_addFile(autoWriter, firstName,
          firstData, sizeof(firstData) - 1), "创建自动关闭 ZIP");
    if (autoWriter) XZipWriter_delete(autoWriter);
    XZipReader* autoReader = XZipReader_create(autoPath);
    CHECK(autoReader && XZipReader_exists(autoReader), "销毁未关闭写入器时自动写出中央目录");
    if (autoReader) XZipReader_delete(autoReader);
    remove(XString_toUtf8(autoPath));
    XString_deinit_base(autoPath);
    remove(XString_toUtf8(path));
    XString_deinit_base(path);
    XString_deinit_base(directory);
    XString_deinit_base(firstName);
    XString_deinit_base(secondName);
    XString_deinit_base(emptyName);
    return groupOk;
}

static bool test_workbook_worksheet_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：Workbook/Worksheet XML 往返\n");
    XWorkbook* workbook = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XAbstractSheet* firstSheet = XWorkbook_addSheet_utf8(workbook, "Data & Raw", XAbstractSheet_ST_WorkSheet);
    XAbstractSheet* secondSheet = XWorkbook_addSheet_utf8(workbook, "Chart", XAbstractSheet_ST_ChartSheet);
    XAbstractSheet_setSheetState(secondSheet, XAbstractSheet_SS_VeryHidden);
    XWorkbook_setDate1904(workbook, true);
    XWorkbook_setActiveSheet(workbook, 1);
    XString_Init_Utf8(definedName, "SalesTotal");
    XString_Init_Utf8(definedFormula, "'Data & Raw'!$A$1");
    XString_Init_Utf8(comment, "A&B <total>");
    XString_Init_Utf8(scope, "Data & Raw");
    CHECK(firstSheet && secondSheet && XWorkbook_defineName(workbook, definedName,
          definedFormula, comment, scope), "创建工作表和定义名称");
    uint8_t* workbookXml = NULL;
    size_t workbookXmlLength = 0;
    CHECK(XWorkbook_saveToXmlData(workbook, &workbookXml, &workbookXmlLength),
          "保存 workbook.xml 数据");
    XWorkbook* loadedWorkbook = XWorkbook_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loadedWorkbook && XWorkbook_loadFromXmlData(loadedWorkbook, workbookXml,
          workbookXmlLength), "加载 workbook.xml 数据");
    CHECK(XWorkbook_sheetCount(loadedWorkbook) == 2 && XWorkbook_isDate1904(loadedWorkbook),
          "工作表数量与 date1904 往返");
    XAbstractSheet* loadedFirst = XWorkbook_sheet(loadedWorkbook, 0);
    XAbstractSheet* loadedSecond = XWorkbook_sheet(loadedWorkbook, 1);
    CHECK(loadedFirst && XString_equals_utf8(XAbstractSheet_sheetName(loadedFirst),
          "Data & Raw", XChar_CaseSensitive), "工作表名称 XML 转义往返");
    CHECK(loadedSecond && XAbstractSheet_sheetState(loadedSecond) == XAbstractSheet_SS_VeryHidden,
          "工作表 veryHidden 状态往返");
    CHECK(XVector_size_base((XContainer*)loadedWorkbook->m_defineNames) == 1,
          "定义名称、注释和作用域被加载");
    if (workbookXml) XFree_System(workbookXml);
    XString_deinit_base(definedName);
    XString_deinit_base(definedFormula);
    XString_deinit_base(comment);
    XString_deinit_base(scope);
    XWorkbook_delete(workbook);
    XWorkbook_delete(loadedWorkbook);

    XWorkbook* managedWorkbook = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XAbstractSheet* one = XWorkbook_addSheet_utf8(managedWorkbook, "One", XAbstractSheet_ST_WorkSheet);
    XAbstractSheet* two = XWorkbook_addSheet_utf8(managedWorkbook, "Two", XAbstractSheet_ST_WorkSheet);
    XString_Init_Utf8(duplicateName, "oNe");
    XString_Init_Utf8(invalidName, "bad/name");
    CHECK(one && two && !XWorkbook_addSheet(managedWorkbook, duplicateName,
          XAbstractSheet_ST_WorkSheet) && !XWorkbook_addSheet(managedWorkbook, invalidName,
          XAbstractSheet_ST_WorkSheet), "工作簿拒绝大小写无关的重复表名和非法表名");
    XString chineseName;
    XString_init(&chineseName);
    for (int i = 0; i < 31; ++i) XString_push_back_base(&chineseName, (XChar)0x4e2d);
    CHECK(XUtility_isValidSheetName(&chineseName) &&
          XWorkbook_addSheet(managedWorkbook, &chineseName, XAbstractSheet_ST_WorkSheet),
          "工作表名称按 31 个 Unicode 字符而不是 UTF-8 字节限制");
    XString_push_back_base(&chineseName, (XChar)0x4e2d);
    CHECK(!XUtility_isValidSheetName(&chineseName), "工作表名称拒绝超过 31 个字符");
    XString_deinit_base(&chineseName);
    CHECK(!XWorkbook_insertSheet(managedWorkbook, XWorkbook_sheetCount(managedWorkbook) + 1,
          NULL, XAbstractSheet_ST_WorkSheet), "插入工作表拒绝越界索引");
    XWorkbook_setActiveSheet(managedWorkbook, 1);
    XAbstractSheet* activeBeforeInsert = XWorkbook_activeSheet(managedWorkbook);
    XString_Init_Utf8(frontName, "Front");
    CHECK(XWorkbook_insertSheet(managedWorkbook, 0, frontName, XAbstractSheet_ST_WorkSheet) &&
          XWorkbook_activeSheet(managedWorkbook) == activeBeforeInsert,
          "插入工作表后活动页仍指向原对象");
    int activeIndex = managedWorkbook->m_activeSheetIndex;
    CHECK(XWorkbook_moveSheet(managedWorkbook, activeIndex, 0) &&
          managedWorkbook->m_activeSheetIndex == 0 &&
          XWorkbook_activeSheet(managedWorkbook) == activeBeforeInsert,
          "移动工作表同步维护活动页索引");
    CHECK(XWorkbook_deleteSheet(managedWorkbook, 0) &&
          XWorkbook_sheetCount(managedWorkbook) == 3 &&
          managedWorkbook->m_activeSheetIndex >= 0 &&
          managedWorkbook->m_activeSheetIndex < XWorkbook_sheetCount(managedWorkbook),
          "删除活动工作表后索引保持有效");
    XWorksheet* copySource = (XWorksheet*)XWorkbook_sheet(managedWorkbook, 0);
    XWorksheet_writeString_utf8(copySource, 9, 4, "copied", NULL);
    CHECK(XWorkbook_copySheet(managedWorkbook, 0, NULL), "深拷贝工作表并自动生成唯一名称");
    XWorksheet* copiedSheet = (XWorksheet*)XWorkbook_sheet(managedWorkbook,
        XWorkbook_sheetCount(managedWorkbook) - 1);
    XCell* copiedCell = copiedSheet ? XWorksheet_cellAt(copiedSheet, 9, 4) : NULL;
    CHECK(copiedCell && XString_equals_utf8(XCell_value(copiedCell), "copied",
          XChar_CaseSensitive), "工作表副本保留单元格数据");
    CHECK(!XWorksheet_setColumnWidth(copySource, 0, 1, 10.0) &&
          !XWorksheet_setColumnWidth(copySource, 1, 16385, 10.0) &&
          !XWorksheet_setRowHeight(copySource, 2, 1, 20.0) &&
          !XWorksheet_setRowHeight(copySource, 1, 1048577, 20.0),
          "行列属性 API 拒绝反向和 Excel 边界外范围");
    XString_deinit_base(duplicateName);
    XString_deinit_base(invalidName);
    XString_deinit_base(frontName);
    XWorkbook_delete(managedWorkbook);

    XDocument* emptyDocument = XDocument_create();
    XString_Init_Utf8(emptyDocumentPath, "/tmp/xinyue_empty_workbook.xlsx");
    CHECK(emptyDocument && XWorkbook_deleteSheet(emptyDocument->m_workbook, 0) &&
          !XDocument_saveAs(emptyDocument, emptyDocumentPath),
          "Document 拒绝保存没有工作表的无效 XLSX");
    remove(XString_toUtf8(emptyDocumentPath));
    XString_deinit_base(emptyDocumentPath);
    if (emptyDocument) XDocument_delete(emptyDocument);

    XWorkbook* owner = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XString_Init_Utf8(sheetName, "Roundtrip");
    XWorksheet* worksheet = XWorksheet_create(sheetName, 1, owner,
        XAbstractOOXmlFile_F_NewFromScratch);
    XString* longText = XString_create_utf8("special & < > ");
    for (int i = 0; i < 700; ++i) XString_push_back_base(longText, (XChar)'z');
    CHECK(XWorksheet_writeString(worksheet, 1, 1, longText, NULL), "写入长字符串");
    CHECK(XWorksheet_writeNumeric(worksheet, 2, 2, 12.5, NULL), "写入数值");
    CHECK(XWorksheet_writeBool(worksheet, 3, 3, true, NULL), "写入布尔值");
    XCellFormula* formula = XCellFormula_create_ex_utf8("SUM(B2,1)");
    CHECK(formula && XWorksheet_writeFormula(worksheet, 4, 4, formula, NULL, 13.5),
          "写入公式和缓存结果");
    if (formula) XCellFormula_delete(formula);
    XWorksheet_setColumnWidth(worksheet, 2, 3, 22.5);
    XWorksheet_setColumnHidden(worksheet, 3, 3, true);
    XWorksheet_groupRows(worksheet, 2, 4, true);
    XWorksheet_mergeCells(worksheet, 5, 1, 5, 3, NULL);
    XWorksheet_setGridLinesVisible(worksheet, false);
    XWorksheet_setRightToLeft(worksheet, true);
    uint8_t* sheetXml = NULL;
    size_t sheetXmlLength = 0;
    CHECK(XWorksheet_saveToXmlData(worksheet, &sheetXml, &sheetXmlLength),
          "保存 worksheet.xml 数据");
    XWorksheet* loadedWorksheet = XWorksheet_create(sheetName, 1, owner,
        XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loadedWorksheet && XWorksheet_loadFromXmlData(loadedWorksheet, sheetXml, sheetXmlLength),
          "加载 worksheet.xml 数据");
    XCell* loadedText = XWorksheet_cellAt(loadedWorksheet, 1, 1);
    CHECK(loadedText && XString_equals(XCell_value(loadedText), longText, XChar_CaseSensitive),
          "长字符串及 XML 特殊字符完整往返");
    CHECK(XWorksheet_isColumnHidden(loadedWorksheet, 3) &&
          XWorksheet_columnWidth(loadedWorksheet, 2) == 22.5,
          "列宽与隐藏状态往返");
    CHECK(!XWorksheet_isGridLinesVisible(loadedWorksheet) &&
          XWorksheet_isRightToLeft(loadedWorksheet), "工作表视图标志往返");
    int mergedCount = 0;
    XWorksheet_mergedCells(loadedWorksheet, &mergedCount);
    CHECK(mergedCount == 1, "合并单元格范围往返");
    XCell* loadedFormula = XWorksheet_cellAt(loadedWorksheet, 4, 4);
    CHECK(loadedFormula && XCell_formula(loadedFormula) &&
          XString_equals_utf8(XCellFormula_formulaText(XCell_formula(loadedFormula)),
          "SUM(B2,1)", XChar_CaseSensitive), "公式文本往返");
    if (sheetXml) XFree_System(sheetXml);
    XString_delete_base(longText);
    XWorksheet_delete(worksheet);
    XWorksheet_delete(loadedWorksheet);
    XString_deinit_base(sheetName);
    XWorkbook_delete(owner);
    return groupOk;
}

static bool test_chart_drawing_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：Chart 与 DrawingAnchor 往返\n");
    XString_Init_Utf8(chartPath, "/tmp/xinyue_chart_extended.xml");
    XChart* chart = XChart_create(NULL, XAbstractOOXmlFile_F_NewFromScratch);
    XChart_setChartType(chart, XChart_BarChart);
    XChart_setChartStyle(chart, 7);
    XChart_setChartTitle_utf8(chart, "Revenue & Cost");
    XString_Init_Utf8(axisTitle, "Amount <USD>");
    XChart_setAxisTitle(chart, XChart_AxisPosLeft, axisTitle);
    XChart_setChartLegend(chart, XChart_AxisPosBottom, true);
    XChart_setGridlinesEnable(chart, true, true);
    XChart_setPosition(chart, 4, 5, 6, 7);
    XChart_setSize(chart, 640, 360);
    XCellRange range = XCellRange_create_ex(1, 1, 10, 2);
    XChart_addSeries(chart, &range, true, false, true);
    CHECK(XChart_saveToXmlFile(chart, chartPath), "保存标准图表 XML");
    XChart* loaded = XChart_create(NULL, XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loaded && XChart_loadFromXmlFile(loaded, chartPath), "加载图表 XML");
    CHECK(loaded && loaded->m_chartType == XChart_BarChart && loaded->m_chartStyle == 7,
          "图表类型与样式往返");
    CHECK(loaded && loaded->m_chartTitle && XString_equals_utf8(loaded->m_chartTitle,
          "Revenue & Cost", XChar_CaseSensitive), "图表标题 XML 转义往返");
    CHECK(loaded && XVector_size_base((XContainer*)loaded->m_series) == 1,
          "图表系列及范围往返");
    CHECK(loaded && loaded->m_width == 640 && loaded->m_height == 360 &&
          loaded->m_row == 4 && loaded->m_col == 5, "图表尺寸与位置往返");
    XChart_delete(chart);
    XChart_delete(loaded);
    XString_deinit_base(axisTitle);
    remove(XString_toUtf8(chartPath));
    XString_deinit_base(chartPath);

    const uint8_t picture[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3, 4};
    XDrawingAnchor* anchor = XDrawingAnchor_create(NULL, XDAnchor_Picture);
    anchor->m_anchorType = XDAnchor_OneCell;
    anchor->m_row = 8;
    anchor->m_col = 3;
    anchor->m_rowOffset = 120;
    anchor->m_colOffset = 340;
    anchor->m_width = 609600;
    anchor->m_height = 304800;
    XString_Init_Utf8(pngMime, "image/png");
    CHECK(XDrawingAnchor_setPictureFromData(anchor, picture, sizeof(picture), pngMime),
          "锚点从图片字节创建媒体对象");
    XByteArray* extracted = XByteArray_create();
    CHECK(XDrawingAnchor_getPicture(anchor, extracted) &&
          XByteArray_size_base((XContainer*)extracted) == sizeof(picture) &&
          memcmp(XByteArray_data(extracted), picture, sizeof(picture)) == 0,
          "锚点返回图片字节副本");
    XByteArray_delete_base(extracted);

    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:wsDr");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:xdr",
        "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a",
        "http://schemas.openxmlformats.org/drawingml/2006/main");
    CHECK(XDrawingAnchor_saveToXml(anchor, writer), "序列化单单元格图片锚点");
    XXmlStreamWriter_writeEndElement(writer);
    const char* anchorXml = XXmlStreamWriter_toString(writer);
    CHECK(anchorXml && strstr(anchorXml, "<xdr:col>3</xdr:col>") &&
          strstr(anchorXml, "<xdr:row>8</xdr:row>") && strstr(anchorXml, "r:embed=\"rId1\""),
          "锚点 XML 使用真实行列和关系 ID");
    XXmlStreamReader* anchorReader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(anchorReader, anchorXml);
    XDrawingAnchor* loadedAnchor = XDrawingAnchor_create(NULL, XDAnchor_Unknown);
    while (!XXmlStreamReader_atEnd(anchorReader)) {
        if (XXmlStreamReader_readNext(anchorReader) == XXmlStream_StartElement &&
            XString_equals_utf8(XXmlStreamReader_name_const(anchorReader), "oneCellAnchor",
            XChar_CaseSensitive)) {
            CHECK(XDrawingAnchor_loadFromXml(loadedAnchor, anchorReader), "加载锚点 XML");
            break;
        }
    }
    CHECK(loadedAnchor->m_row == 8 && loadedAnchor->m_col == 3 &&
          loadedAnchor->m_rowOffset == 120 && loadedAnchor->m_colOffset == 340,
          "锚点位置和偏移往返");
    XDrawingAnchor_delete(loadedAnchor);
    XXmlStreamReader_delete_base(anchorReader);
    XXmlStreamWriter_delete_base(writer);

    XDrawing* drawing = XDrawing_create(NULL, XAbstractOOXmlFile_F_NewFromScratch);
    anchor->m_drawing = drawing;
    CHECK(drawing && XVector_push_back_2(drawing->m_anchors, &anchor, 1),
          "绘图容器接管锚点");
    uint8_t* drawingXml = NULL;
    size_t drawingXmlLength = 0;
    CHECK(drawing && XDrawing_saveToXmlData(drawing, &drawingXml, &drawingXmlLength) &&
          drawingXmlLength > 0, "Drawing 保存为内存 XML 数据");
    const char* drawingText = (const char*)drawingXml;
    const char* firstNamespace = drawingText ? strstr(drawingText, "xmlns:xdr=") : NULL;
    CHECK(firstNamespace && !strstr(firstNamespace + 1, "xmlns:xdr="),
          "Drawing 根元素不会重复写入同一命名空间声明");
    XDrawing* loadedDrawing = XDrawing_create(NULL, XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loadedDrawing && XDrawing_loadFromXmlData(loadedDrawing, drawingXml, drawingXmlLength) &&
          XVector_size_base((XContainer*)loadedDrawing->m_anchors) == 1,
          "Drawing 从定长内存 XML 恢复锚点");
    XDrawingAnchor* drawingAnchor = loadedDrawing &&
        XVector_size_base((XContainer*)loadedDrawing->m_anchors) == 1
        ? *(XDrawingAnchor**)XVector_at_base(loadedDrawing->m_anchors, 0) : NULL;
    CHECK(drawingAnchor && drawingAnchor->m_anchorType == XDAnchor_OneCell &&
          drawingAnchor->m_row == 8 && drawingAnchor->m_col == 3 &&
          drawingAnchor->m_width == 609600 && drawingAnchor->m_height == 304800,
          "Drawing 数据往返保留锚点位置和尺寸");
    XString_Init_Utf8(drawingPath, "/tmp/xinyue_drawing_extended.xml");
    XDrawing* fileDrawing = XDrawing_create(NULL, XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(XDrawing_saveToXmlFile(drawing, drawingPath) &&
          XDrawing_loadFromXmlFile(fileDrawing, drawingPath) &&
          XVector_size_base((XContainer*)fileDrawing->m_anchors) == 1,
          "Drawing 文件接口完整往返");
    remove(XString_toUtf8(drawingPath));
    XString_deinit_base(drawingPath);
    if (drawingXml) XFree_System(drawingXml);
    if (drawing) XDrawing_delete(drawing);
    if (loadedDrawing) XDrawing_delete(loadedDrawing);
    if (fileDrawing) XDrawing_delete(fileDrawing);
    XString_deinit_base(pngMime);
    return groupOk;
}

static bool test_support_modules(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：数据辅助模块、所有权与定长 XML 数据\n");
    XString_Init_Utf8(formula1, "1");
    XString_Init_Utf8(formula2, "10");
    XDataValidation* validation = XDataValidation_create_ex(XDataValidation_Whole,
        XDataValidation_Between, formula1, formula2, true);
    XDataValidation_addCellRc(validation, 1, 1);
    XDataValidation_addRange(validation, 2, 2, 4, 4);
    XDataValidation_addCellRc(validation, 0, 1);
    XDataValidation_addRange(validation, 1, 1, 1048577, 1);
    XDataValidation* validationCopy = XDataValidation_copy(validation);
    CHECK(validationCopy && XDataValidation_rangesCount(validationCopy) == 2 &&
          XDataValidation_allowBlank(validationCopy), "数据验证深拷贝范围和属性");
    XString_Init_Utf8(message, "message");
    XString_Init_Utf8(title, "title");
    XDataValidation_setPromptMessage(validationCopy, message, title);
    XDataValidation_setErrorMessage(validationCopy, message, title);
    XDataValidation_setFormula1(validationCopy, NULL);
    XDataValidation_setPromptMessage(validationCopy, NULL, NULL);
    XDataValidation_setErrorMessage(validationCopy, NULL, NULL);
    CHECK(!XDataValidation_formula1(validationCopy) &&
          !XDataValidation_promptMessage(validationCopy) &&
          !XDataValidation_promptMessageTitle(validationCopy) &&
          !XDataValidation_errorMessage(validationCopy) &&
          !XDataValidation_errorMessageTitle(validationCopy),
          "数据验证 setter 接受 NULL 并释放旧字符串");
    XDataValidation_setValidationType(validationCopy,
        (XDataValidation_ValidationType)999);
    XDataValidation_setValidationOperator(validationCopy,
        (XDataValidation_ValidationOperator)999);
    XDataValidation_setErrorStyle(validationCopy, (XDataValidation_ErrorStyle)999);
    CHECK(XDataValidation_validationType(validationCopy) == XDataValidation_Whole &&
          XDataValidation_validationOperator(validationCopy) == XDataValidation_Between &&
          XDataValidation_errorStyle(validationCopy) == XDataValidation_Stop &&
          !XDataValidation_create_ex((XDataValidation_ValidationType)999,
              XDataValidation_Between, NULL, NULL, false),
          "数据验证拒绝无效枚举值");
    XString_deinit_base(message);
    XString_deinit_base(title);
    XDataValidation_delete(validation);
    XDataValidation_delete(validationCopy);
    XString_deinit_base(formula1);
    XString_deinit_base(formula2);

    XSharedStrings* strings = XSharedStrings_create(XAbstractOOXmlFile_F_NewFromScratch);
    XString_Init_Utf8(sharedValue, "A & B <C>");
    int firstIndex = XSharedStrings_addSharedString(strings, sharedValue);
    int duplicateIndex = XSharedStrings_addSharedString(strings, sharedValue);
    CHECK(firstIndex == 0 && duplicateIndex == 0 && XSharedStrings_count(strings) == 2 &&
          XVector_size_base(XSharedStrings_getSharedStrings(strings)) == 1,
          "共享字符串去重并复用索引");
    CHECK(strings && strings->m_stringCount == 2,
          "共享字符串分别维护总引用数和唯一条目数");
    XSharedStrings_removeSharedString(strings, sharedValue);
    CHECK(XSharedStrings_count(strings) == 1 && strings->m_stringCount == 1 &&
          XSharedStrings_getSharedStringIndex(strings, sharedValue) == 0,
          "共享字符串移除一次引用时保留仍被引用的条目");
    CHECK(XSharedStrings_addSharedString(strings, sharedValue) == 0,
          "共享字符串减少引用后可再次复用原索引");

    XRichString* richValue = XRichString_create();
    XFormat* richFormat = XFormat_create();
    XFormat_setFontBold(richFormat, true);
    XFormat_setFontItalic(richFormat, true);
    XFormat_setFontStrikeOut(richFormat, true);
    XFormat_setFontOutline(richFormat, true);
    XFormat_setFontUnderline(richFormat, XFormat_FontUnderlineDouble);
    XFormat_setFontScript(richFormat, XFormat_FontScriptSuper);
    XFormat_setFontSize(richFormat, 14);
    XFormat_setFontName_utf8(richFormat, "A&B Font");
    XColor richColor = XColor_create_rgb(1, 2, 3, 255);
    XFormat_setFontColor(richFormat, &richColor);
    int richFamily = 2;
    int richShadow = 1;
    XFormat_setProperty(richFormat, XFormat_P_Font_Family, &richFamily);
    XFormat_setProperty(richFormat, XFormat_P_Font_Shadow, &richShadow);
    XString_Init_Utf8(richFirst, "rich & <one>");
    XString_Init_Utf8(richSecond, " tail");
    XRichString_addFragment(richValue, richFirst, richFormat);
    XRichString_addFragment(richValue, richSecond, NULL);
    int richIndex = XSharedStrings_addSharedRichString(strings, richValue);
    int duplicateRichIndex = XSharedStrings_addSharedRichString(strings, richValue);
    CHECK(richIndex == 1 && duplicateRichIndex == 1 &&
          XVector_size_base(XSharedStrings_getSharedStrings(strings)) == 2 &&
          XRichString_isRichString(richValue) && strings->m_stringCount == 4,
          "共享字符串按片段文本和格式去重富文本并累计引用数");
    CHECK(!XRichString_fragmentText(richValue, -1) &&
          !XRichString_fragmentFormat(richValue, 99), "富文本片段访问拒绝越界索引");
    uint8_t* xml = NULL;
    size_t xmlLength = 0;
    CHECK(XSharedStrings_saveToXmlData(strings, &xml, &xmlLength), "共享字符串保存 XML");
    CHECK(xml && strstr((const char*)xml, "count=\"4\" uniqueCount=\"2\"") &&
          strstr((const char*)xml, "<rPr><b/><i/><strike/><outline/><shadow/><u val=\"double\"/>") &&
          strstr((const char*)xml, "<vertAlign val=\"superscript\"/><sz val=\"14\"/><color rgb=\"FF010203\"/>") &&
          strstr((const char*)xml, "<rFont val=\"A&amp;B Font\"/><family val=\"2\"/>") &&
          strstr((const char*)xml, "rich &amp; &lt;one&gt;"),
          "共享字符串 XML 写出准确计数、富文本格式和转义文本");
    XSharedStrings* loadedStrings = XSharedStrings_create(XAbstractOOXmlFile_F_LoadFromExists);
    uint8_t* exactSharedXml = (uint8_t*)XMalloc_System(xmlLength);
    if (exactSharedXml) memcpy(exactSharedXml, xml, xmlLength);
    CHECK(loadedStrings && exactSharedXml &&
          XSharedStrings_loadFromXmlData(loadedStrings, exactSharedXml, xmlLength) &&
          XSharedStrings_count(loadedStrings) == 4 &&
          XVector_size_base(XSharedStrings_getSharedStrings(loadedStrings)) == 2,
          "共享字符串 XML 往返保留总引用数和唯一条目数");
    XRichString* loadedRich = XSharedStrings_getSharedString(loadedStrings, 1);
    const XFormat* loadedRichFormat = loadedRich
        ? XRichString_fragmentFormat(loadedRich, 0) : NULL;
    XColor loadedRichColor = loadedRichFormat
        ? XFormat_fontColor(loadedRichFormat) : XColor_create_rgb(0, 0, 0, 0);
    CHECK(loadedRich && XRichString_isRichString(loadedRich) &&
          XString_equals_utf8(XRichString_text(loadedRich), "rich & <one> tail",
              XChar_CaseSensitive) && loadedRichFormat &&
          XFormat_fontBold(loadedRichFormat) && XFormat_fontItalic(loadedRichFormat) &&
          XFormat_fontStrikeOut(loadedRichFormat) && XFormat_fontOutline(loadedRichFormat) &&
          XFormat_fontUnderline(loadedRichFormat) == XFormat_FontUnderlineDouble &&
          XFormat_fontScript(loadedRichFormat) == XFormat_FontScriptSuper &&
          XFormat_fontSize(loadedRichFormat) == 14 &&
          XFormat_intProperty(loadedRichFormat, XFormat_P_Font_Family, 0) == 2 &&
          XFormat_boolProperty(loadedRichFormat, XFormat_P_Font_Shadow, false) &&
          XColor_red(&loadedRichColor) == 1 && XColor_green(&loadedRichColor) == 2 &&
          XColor_blue(&loadedRichColor) == 3 && XColor_alpha(&loadedRichColor) == 255 &&
          XString_equals_utf8(XFormat_fontName(loadedRichFormat), "A&B Font",
              XChar_CaseSensitive), "共享字符串 XML 读回富文本片段格式");
    CHECK(loadedStrings && XSharedStrings_loadFromXmlData(loadedStrings,
          exactSharedXml, xmlLength) && XSharedStrings_count(loadedStrings) == 4,
          "共享字符串重复加载会清空旧内容且不依赖 NUL 终止符");
    const char* duplicateSharedXml =
        "<sst xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main' count='2' uniqueCount='2'>"
        "<si><t>duplicate</t></si><si><t>duplicate</t></si></sst>";
    XString_Init_Utf8(duplicateText, "duplicate");
    CHECK(XSharedStrings_loadFromXmlData(loadedStrings,
              (const uint8_t*)duplicateSharedXml, strlen(duplicateSharedXml)) &&
          XSharedStrings_count(loadedStrings) == 2 &&
          XVector_size_base(XSharedStrings_getSharedStrings(loadedStrings)) == 2 &&
          XSharedStrings_getSharedStringIndex(loadedStrings, duplicateText) == 1,
          "加载重复共享字符串时保留 OOXML 索引位置");
    XSharedStrings_removeSharedString(loadedStrings, duplicateText);
    CHECK(XSharedStrings_count(loadedStrings) == 1 &&
          XVector_size_base(XSharedStrings_getSharedStrings(loadedStrings)) == 1 &&
          XSharedStrings_getSharedStringIndex(loadedStrings, duplicateText) == 0,
          "移除重复加载字符串后映射回退到剩余索引");
    const char* invalidSharedXml =
        "<sst xmlns='http://schemas.openxmlformats.org/spreadsheetml/2006/main' count='2' uniqueCount='1'>"
        "<si><t>one</t></si><si><t>two</t></si></sst>";
    CHECK(!XSharedStrings_loadFromXmlData(loadedStrings,
              (const uint8_t*)invalidSharedXml, strlen(invalidSharedXml)) &&
          XSharedStrings_isEmpty(loadedStrings),
          "共享字符串加载拒绝与实际条目数不符的 uniqueCount");
    XString_deinit_base(duplicateText);
    if (exactSharedXml) XFree_System(exactSharedXml);
    if (xml) XFree_System(xml);
    XSharedStrings_delete(strings);
    XSharedStrings_delete(loadedStrings);
    XRichString_delete(richValue);
    XFormat_delete(richFormat);
    XString_deinit_base(richFirst);
    XString_deinit_base(richSecond);
    XString_deinit_base(sharedValue);

    XString_Init_Utf8(plainRichText, "plain");
    XRichString* plainRich = XRichString_create_utf8(plainRichText);
    CHECK(plainRich && !XRichString_isRichString(plainRich),
          "单个无格式片段按普通字符串处理");
    XString_Init_Utf8(htmlText,
        "<span style='font-weight:bold;font-style:italic;font-size:14pt;font-family:A&amp;B Font;'>A &amp; B</span><br/>tail");
    XRichString_setHtml(plainRich, htmlText);
    XString htmlRoundTrip = XRichString_toHtml(plainRich);
    CHECK(XString_equals_utf8(XRichString_text(plainRich), "A & B\ntail",
              XChar_CaseSensitive) && XRichString_isRichString(plainRich) &&
          strstr(XString_toUtf8(&htmlRoundTrip), "font-family:A&amp;B Font;") &&
          strstr(XString_toUtf8(&htmlRoundTrip), "A &amp; B"),
          "富文本 HTML 解析并安全转义正文和字体名");
    XRichString* copiedRich = XRichString_create_utf8(plainRichText);
    XRichString_copy(copiedRich, plainRich);
    CHECK(XString_equals_utf8(XRichString_text(copiedRich), "A & B\ntail",
              XChar_CaseSensitive) &&
          XRichString_fragmentCount(copiedRich) == XRichString_fragmentCount(plainRich),
          "富文本复制替换目标旧内容而不是追加");
    XRichString_setText_utf8(plainRich, "replacement");
    CHECK(!XRichString_isRichString(plainRich) &&
          XString_equals_utf8(XRichString_text(plainRich), "replacement",
              XChar_CaseSensitive), "富文本 setText 清除旧格式片段");
    XString_deinit_base(&htmlRoundTrip);
    XRichString_delete(copiedRich);
    XRichString_delete(plainRich);
    XString_deinit_base(htmlText);
    XString_deinit_base(plainRichText);

    const uint8_t mediaBytes[] = {0, 1, 2, 0xff};
    XString_Init_Utf8(mediaSuffix, "png");
    XString_Init_Utf8(mediaMime, "image/png");
    XString_Init_Utf8(mediaName, "image1.png");
    XMediaFile* media = XMediaFile_create_data(mediaBytes, sizeof(mediaBytes),
        mediaSuffix, mediaMime);
    XMediaFile_setFileName(media, mediaName);
    XMediaFile_setIndex(media, 7);
    uint8_t* mediaKey = NULL;
    size_t mediaKeyLength = 0;
    XMediaFile_hashKey(media, &mediaKey, &mediaKeyLength);
    const uint8_t expectedMediaMd5[16] = {
        0x04, 0x16, 0xda, 0xb8, 0x19, 0x88, 0x73, 0x33,
        0xaf, 0x83, 0x1f, 0x8c, 0x76, 0x5a, 0xc2, 0xae
    };
    CHECK(media && XMediaFile_contentsSize(media) == sizeof(mediaBytes) &&
          memcmp(XMediaFile_contents(media), mediaBytes, sizeof(mediaBytes)) == 0 &&
          XString_compare(XMediaFile_suffix(media), mediaSuffix) == XCompare_Equality &&
          XString_compare(XMediaFile_mimeType(media), mediaMime) == XCompare_Equality &&
          XString_compare(XMediaFile_fileName(media), mediaName) == XCompare_Equality &&
          XMediaFile_isIndexValid(media) && XMediaFile_index(media) == 7 &&
          mediaKeyLength == sizeof(expectedMediaMd5) &&
          memcmp(mediaKey, expectedMediaMd5, sizeof(expectedMediaMd5)) == 0,
          "媒体文件完整保存二进制、元数据、索引和内容键");
    XMediaFile_set(media, NULL, 1, NULL, NULL);
    CHECK(XMediaFile_contentsSize(media) == sizeof(mediaBytes),
          "媒体文件拒绝空指针配合非零长度且保留原内容");
    XMediaFile_set(media, mediaBytes, 0, NULL, NULL);
    XMediaFile_setFileName(media, NULL);
    uint8_t* emptyMediaKey = NULL;
    size_t emptyMediaKeyLength = 0;
    XMediaFile_hashKey(media, &emptyMediaKey, &emptyMediaKeyLength);
    CHECK(XMediaFile_contentsSize(media) == 0 && !XMediaFile_suffix(media) &&
          !XMediaFile_mimeType(media) && !XMediaFile_fileName(media) &&
          !XMediaFile_isIndexValid(media) && emptyMediaKeyLength == 16 &&
          !XMediaFile_create_data(NULL, 1, mediaSuffix, mediaMime),
          "媒体文件空参数清理元数据、重置索引并生成空内容 MD5");
    if (emptyMediaKey) XFree_System(emptyMediaKey);
    XMediaFile* mediaDuplicate1 = XMediaFile_create_data(mediaBytes,
        sizeof(mediaBytes), mediaSuffix, mediaMime);
    XMediaFile* mediaDuplicate2 = XMediaFile_create_data(mediaBytes,
        sizeof(mediaBytes), mediaSuffix, mediaMime);
    XWorkbook* mediaWorkbook = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XWorkbook_addMediaFile(mediaWorkbook, mediaDuplicate1, false);
    XWorkbook_addMediaFile(mediaWorkbook, mediaDuplicate2, false);
    int workbookMediaCount = -1;
    XWorkbook_mediaFiles(mediaWorkbook, &workbookMediaCount);
    bool mediaDeduplicated = workbookMediaCount == 1 &&
        XMediaFile_isIndexValid(mediaDuplicate1) && XMediaFile_index(mediaDuplicate1) == 0 &&
        XMediaFile_isIndexValid(mediaDuplicate2) && XMediaFile_index(mediaDuplicate2) == 0;
    XWorkbook_addMediaFile(mediaWorkbook, mediaDuplicate2, true);
    XWorkbook_mediaFiles(mediaWorkbook, &workbookMediaCount);
    CHECK(mediaDeduplicated && workbookMediaCount == 2 &&
          XMediaFile_index(mediaDuplicate2) == 1,
          "工作簿媒体表按内容去重、回填索引且 force 参数允许强制重复");
    XChart* registeredChart = XChart_create(NULL, XAbstractOOXmlFile_F_NewFromScratch);
    XWorkbook_addChartFile(mediaWorkbook, registeredChart);
    XWorkbook_addChartFile(mediaWorkbook, registeredChart);
    int registeredChartCount = -1;
    XWorkbook_chartFiles(mediaWorkbook, &registeredChartCount);
    CHECK(registeredChartCount == 1, "工作簿图表注册表拒绝重复对象指针");
    XWorkbook_delete(mediaWorkbook);
    XChart_delete(registeredChart);
    XMediaFile_delete(mediaDuplicate1);
    XMediaFile_delete(mediaDuplicate2);
    if (mediaKey) XFree_System(mediaKey);
    XMediaFile_delete(media);
    XString_deinit_base(mediaSuffix);
    XString_deinit_base(mediaMime);
    XString_deinit_base(mediaName);

    double serial = XUtility_epochToExcel(2024, 2, 29, 12, 34, 56);
    int year, month, day, hour, minute, second;
    XUtility_excelToEpoch(serial, &year, &month, &day, &hour, &minute, &second);
    CHECK(year == 2024 && month == 2 && day == 29 && hour == 12 && minute == 34 && second == 56,
          "Excel 日期序列闰日往返");
    XString_Init_Utf8(invalidSheetName, "bad/name*with:chars");
    XString safeName = XUtility_safeSheetName(invalidSheetName);
    CHECK(XUtility_isValidSheetName(&safeName), "非法工作表名称清洗为有效名称");
    XString_deinit_base(&safeName);
    XString_deinit_base(invalidSheetName);

    XCell* ownershipCell = XCell_create();
    XCellFormula* oldFormula = XCellFormula_create_ex_utf8("A1+1");
    XCellFormula* newFormula = XCellFormula_create_ex_utf8("B1+1");
    XCell_setFormula(ownershipCell, oldFormula);
    XCell_setFormula(ownershipCell, newFormula);
    CHECK(XCell_formula(ownershipCell) == newFormula &&
          XString_equals_utf8(XCellFormula_formulaText(newFormula), "B1+1", XChar_CaseSensitive),
          "重复设置公式会释放旧公式并接管新公式所有权");
    XString_Init_Utf8(firstRichText, "first");
    XString_Init_Utf8(secondRichText, "second");
    XRichString* firstRich = XRichString_create_utf8(firstRichText);
    XRichString* secondRich = XRichString_create_utf8(secondRichText);
    XCell_setRichString(ownershipCell, firstRich);
    XCell_setRichString(ownershipCell, secondRich);
    CHECK(XCell_richString(ownershipCell) == secondRich,
          "重复设置富文本会释放旧富文本并接管新对象所有权");
    XString_Init_Utf8(replacementValue, "plain");
    XCell_setValue(ownershipCell, replacementValue);
    CHECK(!XCell_hasFormula(ownershipCell) && !XCell_isRichString(ownershipCell),
          "普通值覆盖会清除旧公式和富文本负载");
    XCell_delete(ownershipCell);
    XString_deinit_base(firstRichText);
    XString_deinit_base(secondRichText);
    XString_deinit_base(replacementValue);

    XString_Init_Utf8(dateCode, "yyyy-mm-dd hh:mm:ss");
    XString_Init_Utf8(elapsedCode, "[h]:mm:ss");
    XString_Init_Utf8(literalCode, "0.00 \"meters\"");
    XString_Init_Utf8(colorCode, "[Red]0.00");
    XString_Init_Utf8(customId, "164");
    XString_Init_Utf8(builtinId, "14");
    CHECK(XNumFormatParser_isDateTime(dateCode) && XNumFormatParser_isDateTime(elapsedCode) &&
          XNumFormatParser_isDateTime(builtinId), "识别日期、累计时间和内置日期格式编号");
    CHECK(!XNumFormatParser_isDateTime(literalCode) && !XNumFormatParser_isDateTime(colorCode) &&
          !XNumFormatParser_isDateTime(customId), "忽略引号文字、颜色段且不误判自定义格式编号");
    XFormat* dateFormat = XFormat_create();
    XFormat_setNumberFormat(dateFormat, literalCode);
    CHECK(!XFormat_isDateTimeFormat(dateFormat), "XFormat 复用数字格式解析器避免文字 m 误判");
    XFormat_delete(dateFormat);

    XFont* sourceFont = XFont_create_ex("Noto Sans CJK SC", 13, XFont_Bold, true);
    XFont_setUnderline(sourceFont, true);
    XFont_setStrikeOut(sourceFont, true);
    XFormat* fontFormat = XFormat_create();
    XFormat_setFont(fontFormat, sourceFont);
    XFont* returnedFont = XFormat_font(fontFormat);
    CHECK(returnedFont && strcmp(XFont_family(returnedFont), "Noto Sans CJK SC") == 0 &&
          XFont_pointSize(returnedFont) == 13 && XFont_bold(returnedFont) &&
          XFont_italic(returnedFont) && XFont_underline(returnedFont) &&
          XFont_strikeOut(returnedFont), "XFormat 字体对象 API 完整复制支持的 XFont 属性");
    if (sourceFont) XFont_delete(sourceFont);
    if (returnedFont) XFont_delete(returnedFont);
    XFormat_delete(fontFormat);
    XString_deinit_base(dateCode);
    XString_deinit_base(elapsedCode);
    XString_deinit_base(literalCode);
    XString_deinit_base(colorCode);
    XString_deinit_base(customId);
    XString_deinit_base(builtinId);

    const char* unicodeXml = "<root><name>中文 &amp; UTF-8</name></root>";
    size_t unicodeLength = strlen(unicodeXml);
    uint8_t* exactXml = (uint8_t*)XMalloc_System(unicodeLength);
    memcpy(exactXml, unicodeXml, unicodeLength);
    XSimpleOOXmlFile* simple = XSimpleOOXmlFile_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(simple && XSimpleOOXmlFile_loadFromXmlData(simple, exactXml, unicodeLength),
          "SimpleOOXmlFile 从无 NUL 的定长数据加载");
    uint8_t* simpleData = NULL;
    size_t simpleLength = 0;
    CHECK(simple && XSimpleOOXmlFile_saveToXmlData(simple, &simpleData, &simpleLength) &&
          simpleLength == unicodeLength && memcmp(simpleData, unicodeXml, unicodeLength) == 0,
          "SimpleOOXmlFile 按 UTF-8 字节数完整保存中文 XML");
    XString_Init_Utf8(simplePath, "/tmp/xinyue_simple_ooxml.xml");
    XSimpleOOXmlFile* simpleFromFile = XSimpleOOXmlFile_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(XSimpleOOXmlFile_saveToXmlFile(simple, simplePath) &&
          XSimpleOOXmlFile_loadFromXmlFile(simpleFromFile, simplePath) &&
          XString_equals(XSimpleOOXmlFile_xmlData(simple),
              XSimpleOOXmlFile_xmlData(simpleFromFile), XChar_CaseSensitive),
          "SimpleOOXmlFile 中文文件接口按实际 UTF-8 字节往返");
    remove(XString_toUtf8(simplePath));
    XString_deinit_base(simplePath);
    if (simpleData) XFree_System(simpleData);
    if (simple) XSimpleOOXmlFile_delete(simple);
    if (simpleFromFile) XSimpleOOXmlFile_delete(simpleFromFile);

    XTheme* theme = XTheme_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(theme && XTheme_loadFromXmlData(theme, exactXml, unicodeLength),
          "Theme 从无 NUL 的定长数据加载");
    uint8_t* themeData = NULL;
    size_t themeLength = 0;
    CHECK(theme && XTheme_saveToXmlData(theme, &themeData, &themeLength) &&
          themeLength == unicodeLength && memcmp(themeData, unicodeXml, unicodeLength) == 0,
          "Theme 按 UTF-8 字节数完整保存中文 XML");
    if (themeData) XFree_System(themeData);
    if (theme) XTheme_delete(theme);
    XFree_System(exactXml);
    return groupOk;
}

static bool test_worksheet_features_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：验证/条件格式/超链接/关系与内容类型\n");
    XWorkbook* workbook = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XString_Init_Utf8(sheetName, "Features");
    XWorksheet* worksheet = (XWorksheet*)XWorkbook_addSheet(workbook, sheetName,
        XAbstractSheet_ST_WorkSheet);

    XString_Init_Utf8(first, "1");
    XString_Init_Utf8(second, "100");
    XDataValidation* validation = XDataValidation_create_ex(XDataValidation_Whole,
        XDataValidation_Between, first, second, true);
    XDataValidation* emptyValidation = XDataValidation_create();
    CHECK(!XWorksheet_addDataValidation(worksheet, emptyValidation),
          "工作表拒绝没有有效范围的数据验证且不接管所有权");
    XDataValidation_delete(emptyValidation);
    XString_Init_Utf8(prompt, "请输入 1 & 100");
    XString_Init_Utf8(promptTitle, "范围 <提示>");
    XString_Init_Utf8(error, "数值越界");
    XString_Init_Utf8(errorTitle, "错误");
    XDataValidation_setPromptMessage(validation, prompt, promptTitle);
    XDataValidation_setErrorMessage(validation, error, errorTitle);
    XDataValidation_setPromptMessageVisible(validation, true);
    XDataValidation_setErrorMessageVisible(validation, true);
    XDataValidation_addRange(validation, 2, 2, 5, 3);
    CHECK(XWorksheet_addDataValidation(worksheet, validation), "工作表接收数据验证所有权");

    XFormat* conditionalFormat = XFormat_create();
    XFormat_setFontBold(conditionalFormat, true);
    XColor conditionalColor = XColor_create_rgb(0xff, 0x22, 0x11, 0xff);
    XFormat_setFontColor(conditionalFormat, &conditionalColor);
    XConditionalFormatting* conditional = XConditionalFormatting_create();
    XString_Init_Utf8(conditionFormula, "B2>50");
    CHECK(XConditionalFormatting_addHighlightCellsRule2(conditional,
        XCF_Highlight_GreaterThan, conditionFormula, conditionalFormat, true),
        "条件格式规则深拷贝调用方格式");
    XFormat_delete(conditionalFormat);
    XString_Init_Utf8(topRank, "7");
    XString_Init_Utf8(matchText, "needle");
    XString_Init_Utf8(dataMin, "0");
    XString_Init_Utf8(dataMax, "100");
    XColor lowColor = XColor_create_rgb(0xf8, 0x69, 0x6b, 0xff);
    XColor midColor = XColor_create_rgb(0xff, 0xeb, 0x84, 0xff);
    XColor highColor = XColor_create_rgb(0x63, 0xbe, 0x7b, 0xff);
    CHECK(XConditionalFormatting_addHighlightCellsRule2(conditional,
              XCF_Highlight_TopPercent, topRank, NULL, false) &&
          XConditionalFormatting_addHighlightCellsRule(conditional,
              XCF_Highlight_BelowStdDev2, NULL, false) &&
          XConditionalFormatting_addHighlightCellsRule2(conditional,
              XCF_Highlight_ContainsText, matchText, NULL, false) &&
          XConditionalFormatting_addDataBarRuleEx(conditional, &highColor,
              XCF_VOT_Num, dataMin, XCF_VOT_Percent, dataMax, true, false) &&
          XConditionalFormatting_add2ColorScaleRule(conditional,
              &lowColor, &highColor, false) &&
          XConditionalFormatting_add3ColorScaleRule(conditional,
              &lowColor, &midColor, &highColor, false),
          "添加 Top、标准差、文本、数据条及二/三色阶全部规则类型");
    XConditionalFormatting_addRange(conditional, 2, 2, 5, 3);
    CHECK(XWorksheet_addConditionalFormatting(worksheet, conditional) &&
          XVector_size_base((XContainer*)workbook->m_styles->m_dxfFormatsList) == 1,
          "条件格式注册到 DXF 样式表");

    XString_Init_Utf8(externalUrl, "https://example.com/a?x=1&y=2");
    XString_Init_Utf8(externalDisplay, "Example & docs");
    XString_Init_Utf8(externalTip, "Open <site>");
    XString_Init_Utf8(internalUrl, "#Features!A1");
    CHECK(XWorksheet_writeHyperlink(worksheet, 6, 2, externalUrl, NULL,
          externalDisplay, externalTip) &&
          XWorksheet_writeHyperlink(worksheet, 7, 2, internalUrl, NULL, NULL, NULL),
          "写入外部与内部超链接");
    XWorksheet_setWindowProtected(worksheet, true);
    XWorksheet_setStartPage(worksheet, 4);
    XFormat* rowOnlyFormat = XFormat_create();
    XFormat_setFontItalic(rowOnlyFormat, true);
    XWorksheet_setRowHeight(worksheet, 100, 100, 27.5);
    XWorksheet_setRowFormat(worksheet, 100, 100, rowOnlyFormat);
    XFormat_delete(rowOnlyFormat);

    uint8_t* xml = NULL;
    size_t xmlLength = 0;
    CHECK(XWorksheet_saveToXmlData(worksheet, &xml, &xmlLength) && xml &&
          strstr((const char*)xml, "dataValidations") &&
          strstr((const char*)xml, "<dataValidations count=\"1\">") &&
          strstr((const char*)xml, "conditionalFormatting") &&
          strstr((const char*)xml, "r:id=\"rId1\"") &&
          strstr((const char*)xml, "location=\"Features!A1\"") &&
          strstr((const char*)xml, "sheetProtection") &&
          strstr((const char*)xml, "firstPageNumber=\"4\"") &&
          strstr((const char*)xml, "<row r=\"100\"") &&
          strstr((const char*)xml, "rank=\"7\"") &&
          strstr((const char*)xml, "stdDev=\"2\"") &&
          strstr((const char*)xml, "text=\"needle\"") &&
          strstr((const char*)xml,
              "<dataBar showValue=\"1\"><cfvo type=\"num\" val=\"0\"/><cfvo type=\"percent\" val=\"100\"/>") &&
          strstr((const char*)xml,
              "<cfvo type=\"min\"/><cfvo type=\"percentile\" val=\"50\"/><cfvo type=\"max\"/>"),
          "工作表 XML 包含附加结构与关系引用");
    XString_Init_Utf8(loadedName, "LoadedFeatures");
    XWorksheet* loaded = XWorksheet_create(loadedName, 2, workbook,
        XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loaded && XWorksheet_loadFromXmlData(loaded, xml, xmlLength),
          "加载含附加结构的工作表 XML");
    XDataValidation* loadedValidation = loaded && loaded->m_dataValidations &&
        XVector_size_base((XContainer*)loaded->m_dataValidations) == 1
        ? *(XDataValidation**)XVector_at_base(loaded->m_dataValidations, 0) : NULL;
    CHECK(loadedValidation && loadedValidation->m_validationType == XDataValidation_Whole &&
          loadedValidation->m_validationOperator == XDataValidation_Between &&
          loadedValidation->m_allowBlank && loadedValidation->m_promptMessageVisible &&
          loadedValidation->m_errorMessageVisible &&
          XDataValidation_rangesCount(loadedValidation) == 1 &&
          XString_equals(loadedValidation->m_formula2, second, XChar_CaseSensitive),
          "数据验证类型、公式、消息和范围往返");
    XConditionalFormatting* loadedConditional = loaded && loaded->m_conditionalFormatting &&
        XVector_size_base((XContainer*)loaded->m_conditionalFormatting) == 1
        ? *(XConditionalFormatting**)XVector_at_base(loaded->m_conditionalFormatting, 0) : NULL;
    XConditionalFormatting_Rule* loadedRule = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 0) : NULL;
    CHECK(loadedRule && loadedRule->m_highlightType == XCF_Highlight_GreaterThan &&
          loadedRule->m_stopIfTrue && loadedRule->m_format &&
          XFormat_fontBold(loadedRule->m_format), "条件格式规则与 DXF 格式往返");
    XConditionalFormatting_Rule* loadedTop = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 1) : NULL;
    XConditionalFormatting_Rule* loadedStdDev = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 2) : NULL;
    XConditionalFormatting_Rule* loadedTextRule = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 3) : NULL;
    XConditionalFormatting_Rule* loadedDataBar = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 4) : NULL;
    XConditionalFormatting_Rule* loadedColor3 = loadedConditional
        ? XConditionalFormatting_rule(loadedConditional, 6) : NULL;
    CHECK(loadedConditional && XConditionalFormatting_rulesCount(loadedConditional) == 7 &&
          loadedTop && loadedTop->m_highlightType == XCF_Highlight_TopPercent &&
          loadedTop->m_rank == 7 && loadedStdDev &&
          loadedStdDev->m_highlightType == XCF_Highlight_BelowStdDev2 &&
          loadedTextRule && loadedTextRule->m_text &&
          XString_equals(loadedTextRule->m_text, matchText, XChar_CaseSensitive),
          "Top、标准差与文本条件规则属性往返");
    CHECK(loadedDataBar && loadedDataBar->m_ruleType == XCF_Rule_DataBar &&
          loadedDataBar->m_valType1 == XCF_VOT_Num &&
          loadedDataBar->m_valType2 == XCF_VOT_Percent &&
          loadedDataBar->m_formula2 &&
          XString_equals(loadedDataBar->m_formula2, dataMax, XChar_CaseSensitive) &&
          loadedColor3 && loadedColor3->m_ruleType == XCF_Rule_ColorScale3 &&
          loadedColor3->m_valType2 == XCF_VOT_Percentile &&
          loadedColor3->m_valType3 == XCF_VOT_Max,
          "数据条双阈值和三色阶三个阈值完整往返");
    CHECK(loaded && loaded->m_hyperlinks &&
          XVector_size_base((XContainer*)loaded->m_hyperlinks) == 2 &&
          loaded->m_windowProtection && loaded->m_startPage == 4,
          "超链接、保护和起始页往返");
    CHECK(loaded && fabs(XWorksheet_rowHeight(loaded, 100) - 27.5) < 0.001 &&
          XWorksheet_rowFormat(loaded, 100) &&
          XFormat_fontItalic(XWorksheet_rowFormat(loaded, 100)),
          "无单元格的行高与行格式按顺序写入并往返");

    XRelationships* relationships = XRelationships_create();
    XString_Init_Utf8(hyperlinkType, "hyperlink");
    XString_Init_Utf8(externalMode, "External");
    XRelationships_addWorksheetRelationship(relationships, hyperlinkType, externalUrl, externalMode);
    uint8_t* relationshipXml = NULL;
    size_t relationshipLength = 0;
    XRelationships* loadedRelationships = XRelationships_create();
    CHECK(XRelationships_saveToXmlData(relationships, &relationshipXml, &relationshipLength) &&
          loadedRelationships && XRelationships_loadFromXmlData(loadedRelationships,
              relationshipXml, relationshipLength), "关系 XML 特殊字符往返");
    int relationshipCount = 0;
    XlsxRelationship** found = XRelationships_worksheetRelationships(loadedRelationships,
        hyperlinkType, &relationshipCount);
    CHECK(found && relationshipCount == 1 && found[0]->m_targetMode &&
          XString_equals(found[0]->m_target, externalUrl, XChar_CaseSensitive),
          "关系短类型自动补全 URI 并精确查询");
    if (found) XFree_System(found);

    XContentTypes* contentTypes = XContentTypes_create();
    XString_Init_Utf8(customSheet, "custom-sheet.xml");
    XString_Init_Utf8(customChartSheet, "chart-page.xml");
    XContentTypes_addWorksheetName(contentTypes, customSheet);
    XContentTypes_addChartsheetName(contentTypes, customChartSheet);
    uint8_t* contentXml = NULL;
    size_t contentLength = 0;
    CHECK(XContentTypes_saveToXmlData(contentTypes, &contentXml, &contentLength) &&
          strstr((const char*)contentXml, "/xl/worksheets/custom-sheet.xml") &&
          strstr((const char*)contentXml, "/xl/chartsheets/chart-page.xml") &&
          strstr((const char*)contentXml, "spreadsheetml.chartsheet+xml") &&
          !strstr((const char*)contentXml, "chartshhet"),
          "ContentTypes 使用传入路径并输出正确 Chartsheet MIME");
    uint8_t* exactContentXml = (uint8_t*)XMalloc_System(contentLength);
    if (exactContentXml) memcpy(exactContentXml, contentXml, contentLength);
    XContentTypes* loadedContentTypes = XContentTypes_create();
    uint8_t* loadedContentXml = NULL;
    size_t loadedContentLength = 0;
    CHECK(loadedContentTypes && exactContentXml &&
          XContentTypes_loadFromXmlData(loadedContentTypes, exactContentXml, contentLength) &&
          XContentTypes_saveToXmlData(loadedContentTypes, &loadedContentXml,
              &loadedContentLength) &&
          strstr((const char*)loadedContentXml, "/xl/worksheets/custom-sheet.xml") &&
          strstr((const char*)loadedContentXml, "/xl/chartsheets/chart-page.xml"),
          "ContentTypes 使用 XXmlStreamReader 从无 NUL 数据往返");

    if (loadedContentXml) XFree_System(loadedContentXml);
    if (exactContentXml) XFree_System(exactContentXml);
    if (loadedContentTypes) XContentTypes_delete(loadedContentTypes);
    if (contentXml) XFree_System(contentXml);
    XContentTypes_delete(contentTypes);
    if (relationshipXml) XFree_System(relationshipXml);
    XRelationships_delete(relationships);
    XRelationships_delete(loadedRelationships);
    if (xml) XFree_System(xml);
    if (loaded) XWorksheet_delete(loaded);
    XWorkbook_delete(workbook);
    XString_deinit_base(sheetName);
    XString_deinit_base(first);
    XString_deinit_base(second);
    XString_deinit_base(prompt);
    XString_deinit_base(promptTitle);
    XString_deinit_base(error);
    XString_deinit_base(errorTitle);
    XString_deinit_base(conditionFormula);
    XString_deinit_base(topRank);
    XString_deinit_base(matchText);
    XString_deinit_base(dataMin);
    XString_deinit_base(dataMax);
    XString_deinit_base(externalUrl);
    XString_deinit_base(externalDisplay);
    XString_deinit_base(externalTip);
    XString_deinit_base(internalUrl);
    XString_deinit_base(loadedName);
    XString_deinit_base(hyperlinkType);
    XString_deinit_base(externalMode);
    XString_deinit_base(customSheet);
    XString_deinit_base(customChartSheet);
    return groupOk;
}

static void free_string_array(XString** values, int count)
{
    if (!values) return;
    for (int i = 0; i < count; ++i) {
        if (values[i]) XString_delete_base(values[i]);
    }
    XFree_System(values);
}

static bool test_document_properties_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：Core/App 文档属性 XML 与文件往返\n");
    XString_Init_Utf8(titleName, "title");
    XString_Init_Utf8(titleValue, "研发 & 测试 <2026>");
    XString_Init_Utf8(createdName, "created");
    XString_Init_Utf8(createdValue, "2026-07-26T12:34:56Z");
    XDocPropsCore* core = XDocPropsCore_create(XAbstractOOXmlFile_F_NewFromScratch);
    CHECK(core && XDocPropsCore_setProperty(core, titleName, titleValue) &&
          XDocPropsCore_setProperty(core, createdName, createdValue),
          "设置核心文档属性");
    uint8_t* coreXml = NULL;
    size_t coreLength = 0;
    CHECK(core && XDocPropsCore_saveToXmlData(core, &coreXml, &coreLength) && coreLength > 0,
          "核心属性保存为 XML 数据");
    XDocPropsCore* loadedCore = XDocPropsCore_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loadedCore && XDocPropsCore_loadFromXmlData(loadedCore, coreXml, coreLength),
          "核心属性从 XML 数据加载");
    CHECK(loadedCore && XString_equals(XDocPropsCore_property(loadedCore, titleName), titleValue,
          XChar_CaseSensitive) && XString_equals(XDocPropsCore_property(loadedCore, createdName),
          createdValue, XChar_CaseSensitive), "核心属性特殊字符与日期往返");
    XString** coreNames = NULL;
    int coreNameCount = XDocPropsCore_propertyNames(loadedCore, &coreNames);
    CHECK(coreNameCount == 2, "核心属性名称枚举完整");
    free_string_array(coreNames, coreNameCount);
    if (coreXml) XFree_System(coreXml);

    XString_Init_Utf8(companyName, "Company");
    XString_Init_Utf8(companyValue, "XinYueC & Co.");
    XString_Init_Utf8(headingName, "Worksheets");
    XString_Init_Utf8(partTitle, "数据 <一>");
    XDocPropsApp* app = XDocPropsApp_create(XAbstractOOXmlFile_F_NewFromScratch);
    CHECK(app && XDocPropsApp_setProperty(app, companyName, companyValue),
          "设置应用程序文档属性");
    XDocPropsApp_addHeadingPair(app, headingName, 1);
    XDocPropsApp_addPartTitle(app, partTitle);
    uint8_t* appXml = NULL;
    size_t appLength = 0;
    CHECK(app && XDocPropsApp_saveToXmlData(app, &appXml, &appLength) && appLength > 0,
          "应用程序属性保存为标准 HeadingPairs/TitlesOfParts XML");
    XDocPropsApp* loadedApp = XDocPropsApp_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loadedApp && XDocPropsApp_loadFromXmlData(loadedApp, appXml, appLength),
          "应用程序属性从 XML 数据加载");
    CHECK(loadedApp && XString_equals(XDocPropsApp_property(loadedApp, companyName), companyValue,
          XChar_CaseSensitive), "应用程序属性特殊字符往返");
    CHECK(loadedApp && XVector_size_base((XContainer*)loadedApp->m_headingPairsList) == 1 &&
          XStringList_size_base((XContainer*)loadedApp->m_titlesOfPartsList) == 1,
          "标题对与部件标题列表往返");
    XDocPropsAppHeadingPair* loadedPair = loadedApp
        ? (XDocPropsAppHeadingPair*)XVector_at_base(loadedApp->m_headingPairsList, 0) : NULL;
    XString* loadedTitle = loadedApp
        ? (XString*)XStringList_at_base((XVector*)loadedApp->m_titlesOfPartsList, 0) : NULL;
    CHECK(loadedPair && loadedPair->m_value == 1 &&
          XString_equals(loadedPair->m_name, headingName, XChar_CaseSensitive) &&
          loadedTitle && XString_equals(loadedTitle, partTitle, XChar_CaseSensitive),
          "标题对名称、数量和部件标题内容正确");

    XString_Init_Utf8(corePath, "/tmp/xinyue_core_properties.xml");
    XString_Init_Utf8(appPath, "/tmp/xinyue_app_properties.xml");
    CHECK(XDocPropsCore_saveToXmlFile(core, corePath) &&
          XDocPropsCore_loadFromXmlFile(loadedCore, corePath), "核心属性文件接口往返");
    CHECK(XDocPropsApp_saveToXmlFile(app, appPath) &&
          XDocPropsApp_loadFromXmlFile(loadedApp, appPath), "应用属性文件接口往返");
    remove(XString_toUtf8(corePath));
    remove(XString_toUtf8(appPath));

    if (appXml) XFree_System(appXml);
    XDocPropsCore_delete(core);
    XDocPropsCore_delete(loadedCore);
    XDocPropsApp_delete(app);
    XDocPropsApp_delete(loadedApp);
    XString_deinit_base(titleName);
    XString_deinit_base(titleValue);
    XString_deinit_base(createdName);
    XString_deinit_base(createdValue);
    XString_deinit_base(companyName);
    XString_deinit_base(companyValue);
    XString_deinit_base(headingName);
    XString_deinit_base(partTitle);
    XString_deinit_base(corePath);
    XString_deinit_base(appPath);
    return groupOk;
}

static bool test_styles_roundtrip(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：Styles 标准 OOXML 与格式生命周期往返\n");
    XStyles* styles = XStyles_create(XAbstractOOXmlFile_F_NewFromScratch);
    XFormat* format = XFormat_create();
    XFormat_setFontName_utf8(format, "A&B <Font>");
    XFormat_setFontSize(format, 13);
    XFormat_setFontBold(format, true);
    XFormat_setFontItalic(format, true);
    XFormat_setFontUnderline(format, XFormat_FontUnderlineDouble);
    XColor fontColor = XColor_create_rgb(0x12, 0x34, 0x56, 0xff);
    XFormat_setFontColor(format, &fontColor);
    XFormat_setFillPattern(format, XFormat_PatternSolid);
    XColor fillColor = XColor_create_rgb(0xaa, 0xbb, 0xcc, 0xff);
    XFormat_setPatternForegroundColor(format, &fillColor);
    XFormat_setLeftBorderStyle(format, XFormat_BorderMediumDashDot);
    XColor borderColor = XColor_create_rgb(0xde, 0xad, 0xbe, 0xff);
    XFormat_setLeftBorderColor(format, &borderColor);
    XFormat_setHorizontalAlignment(format, XFormat_AlignHCenter);
    XFormat_setVerticalAlignment(format, XFormat_AlignTop);
    XFormat_setTextWrap(format, true);
    XFormat_setRotation(format, 30);
    XFormat_setIndent(format, 2);
    XFormat_setLocked(format, false);
    XFormat_setHidden(format, true);
    XString_Init_Utf8(numberCode, "#,##0.00;[Red]-#,##0.00");
    XFormat_setNumberFormat_ex(format, 164, numberCode);
    XStyles_addXfFormat(styles, format, false);
    XStyles_addXfFormat(styles, format, false);
    XStyles_addDxfFormat(styles, format, false);
    CHECK(styles && XVector_size_base((XContainer*)styles->m_xfFormatsList) == 2,
          "样式表保留默认格式并去重自定义 XF");
    uint8_t* xml = NULL;
    size_t xmlLength = 0;
    CHECK(XStyles_saveToXmlData(styles, &xml, &xmlLength) && xmlLength > 0,
          "样式表保存 fonts/fills/borders/numFmts/cellXfs");
    CHECK(xml && strstr((const char*)xml, "cellXfs") && strstr((const char*)xml, "formatCode"),
          "样式 XML 包含单元格格式与自定义数字格式");
    XStyles* loaded = XStyles_create(XAbstractOOXmlFile_F_LoadFromExists);
    CHECK(loaded && XStyles_loadFromXmlData(loaded, xml, xmlLength), "样式 XML 数据加载");
    XFormat* loadedFormat = loaded ? XStyles_xfFormat(loaded, 1) : NULL;
    CHECK(loadedFormat && XFormat_fontBold(loadedFormat) && XFormat_fontItalic(loadedFormat) &&
          XFormat_fontSize(loadedFormat) == 13 &&
          strcmp(XFormat_fontName_utf8(loadedFormat), "A&B <Font>") == 0,
          "字体名称、大小和样式往返");
    XColor loadedFontColor = loadedFormat ? XFormat_fontColor(loadedFormat)
                                          : XColor_create_rgb(0, 0, 0, 0);
    XColor loadedFillColor = loadedFormat ? XFormat_patternForegroundColor(loadedFormat)
                                          : XColor_create_rgb(0, 0, 0, 0);
    CHECK(loadedFormat && XColor_rgba(&loadedFontColor) == XColor_rgba(&fontColor) &&
          XFormat_fillPattern(loadedFormat) == XFormat_PatternSolid &&
          XColor_rgba(&loadedFillColor) == XColor_rgba(&fillColor), "字体与填充颜色往返");
    CHECK(loadedFormat && XFormat_leftBorderStyle(loadedFormat) == XFormat_BorderMediumDashDot &&
          XFormat_horizontalAlignment(loadedFormat) == XFormat_AlignHCenter &&
          XFormat_verticalAlignment(loadedFormat) == XFormat_AlignTop &&
          XFormat_textWrap(loadedFormat) && XFormat_rotation(loadedFormat) == 30 &&
          XFormat_indent(loadedFormat) == 2, "边框与对齐属性往返");
    CHECK(loadedFormat && XFormat_numberFormatIndex(loadedFormat) == 164 &&
          XString_equals(XFormat_numberFormat(loadedFormat), numberCode, XChar_CaseSensitive) &&
          !XFormat_locked(loadedFormat) && XFormat_hidden(loadedFormat), "数字格式与保护属性往返");
    XFormat* loadedDxf = loaded ? XStyles_dxfFormat(loaded, 0) : NULL;
    CHECK(loadedDxf && XFormat_fontBold(loadedDxf) &&
          XFormat_fillPattern(loadedDxf) == XFormat_PatternSolid &&
          XFormat_leftBorderStyle(loadedDxf) == XFormat_BorderMediumDashDot,
          "DXF 字体、填充和边框从 styles.xml 往返");

    XString_Init_Utf8(stylePath, "/tmp/xinyue_styles.xml");
    CHECK(XStyles_saveToXmlFile(styles, stylePath) && XStyles_loadFromXmlFile(loaded, stylePath),
          "样式文件接口往返");
    remove(XString_toUtf8(stylePath));
    if (xml) XFree_System(xml);
    XStyles_delete(styles);
    XStyles_delete(loaded);
    XFormat_delete(format);
    XString_deinit_base(numberCode);
    XString_deinit_base(stylePath);
    return groupOk;
}

static bool test_document_charts_and_hyperlinks(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：工作表图表、Chartsheet 与超链接包往返\n");
    XString_Init_Utf8(path, "/tmp/xinyue_chart_package.xlsx");
    XDocument* document = XDocument_create();
    XWorksheet* worksheet = XDocument_currentWorksheet(document);
    XWorksheet_writeNumeric(worksheet, 1, 1, 10.0, NULL);
    XWorksheet_writeNumeric(worksheet, 2, 1, 20.0, NULL);
    XCellRange seriesRange = XCellRange_create_ex(1, 1, 2, 1);
    XChart* worksheetChart = XWorksheet_insertChart(worksheet, 3, 2, 640, 360);
    XChart_setChartType(worksheetChart, XChart_LineChart);
    XChart_setChartTitle_utf8(worksheetChart, "Worksheet & chart");
    XChart_addSeries(worksheetChart, &seriesRange, false, false, false);
    XString_Init_Utf8(url, "https://example.com/report?a=1&b=2");
    XString_Init_Utf8(display, "Report link");
    XWorksheet_writeHyperlink(worksheet, 5, 1, url, NULL, display, NULL);

    XString_Init_Utf8(chartSheetName, "ChartOnly");
    CHECK(XDocument_addSheet(document, chartSheetName, XAbstractSheet_ST_ChartSheet),
          "添加 Chartsheet");
    XChartsheet* chartsheet = (XChartsheet*)XWorkbook_sheet(document->m_workbook, 1);
    XChart* chartsheetChart = XChart_create(chartsheet ? &chartsheet->m_base : NULL,
        XAbstractOOXmlFile_F_NewFromScratch);
    XChart_setChartType(chartsheetChart, XChart_PieChart);
    XChart_setChartTitle_utf8(chartsheetChart, "Chartsheet <pie>");
    XChart_addSeries(chartsheetChart, &seriesRange, false, false, false);
    XChartsheet_setChart(chartsheet, chartsheetChart);
    CHECK(XDocument_saveAs(document, path),
          "保存含工作表图表、Chartsheet 和超链接的 XLSX");

    XZipReader* zip = XZipReader_create(path);
    XString_Init_Utf8(chart1Path, "xl/charts/chart1.xml");
    XString_Init_Utf8(chart2Path, "xl/charts/chart2.xml");
    XString_Init_Utf8(chartSheetPath, "xl/chartsheets/sheet2.xml");
    XString_Init_Utf8(drawing1Path, "xl/drawings/drawing1.xml");
    XString_Init_Utf8(drawing2Path, "xl/drawings/drawing2.xml");
    XByteArray* chart1 = zip ? XZipReader_fileData(zip, chart1Path) : NULL;
    XByteArray* chart2 = zip ? XZipReader_fileData(zip, chart2Path) : NULL;
    XByteArray* chartSheetXml = zip ? XZipReader_fileData(zip, chartSheetPath) : NULL;
    XByteArray* drawing1 = zip ? XZipReader_fileData(zip, drawing1Path) : NULL;
    XByteArray* drawing2 = zip ? XZipReader_fileData(zip, drawing2Path) : NULL;
    CHECK(chart1 && chart2 && chartSheetXml && drawing1 && drawing2,
          "XLSX 包含两个图表、Chartsheet 和对应 Drawing 部件");
    if (chart1) XByteArray_delete_base(chart1);
    if (chart2) XByteArray_delete_base(chart2);
    if (chartSheetXml) XByteArray_delete_base(chartSheetXml);
    if (drawing1) XByteArray_delete_base(drawing1);
    if (drawing2) XByteArray_delete_base(drawing2);
    if (zip) XZipReader_delete(zip);

    XDocument* loaded = XDocument_createFromFile(path);
    CHECK(loaded && XWorkbook_sheetCount(loaded->m_workbook) == 2,
          "加载图表文档并恢复工作表数量");
    XAbstractSheet* loadedFirst = loaded ? XWorkbook_sheet(loaded->m_workbook, 0) : NULL;
    XAbstractSheet* loadedSecond = loaded ? XWorkbook_sheet(loaded->m_workbook, 1) : NULL;
    XWorksheet* loadedWorksheet = loadedFirst &&
        loadedFirst->m_sheetType == XAbstractSheet_ST_WorkSheet ? (XWorksheet*)loadedFirst : NULL;
    XChartsheet* loadedChartsheet = loadedSecond &&
        loadedSecond->m_sheetType == XAbstractSheet_ST_ChartSheet ? (XChartsheet*)loadedSecond : NULL;
    XChart* loadedWorksheetChart = loadedWorksheet && loadedWorksheet->m_chartFiles &&
        XVector_size_base((XContainer*)loadedWorksheet->m_chartFiles) == 1
        ? *(XChart**)XVector_at_base(loadedWorksheet->m_chartFiles, 0) : NULL;
    CHECK(loadedWorksheetChart && loadedWorksheetChart->m_chartType == XChart_LineChart &&
          loadedWorksheetChart->m_chartTitle &&
          XString_equals_utf8(loadedWorksheetChart->m_chartTitle, "Worksheet & chart",
              XChar_CaseSensitive), "工作表内图表类型、标题和绘图关系往返");
    XWorksheet_Hyperlink* loadedHyperlink = loadedWorksheet && loadedWorksheet->m_hyperlinks &&
        XVector_size_base((XContainer*)loadedWorksheet->m_hyperlinks) == 1
        ? (XWorksheet_Hyperlink*)XVector_at_base(loadedWorksheet->m_hyperlinks, 0) : NULL;
    CHECK(loadedHyperlink && loadedHyperlink->m_url &&
          XString_equals(loadedHyperlink->m_url, url, XChar_CaseSensitive),
          "外部超链接通过 sheet relationships 恢复真实 URL");
    XChart* loadedChartsheetChart = loadedChartsheet ? XChartsheet_chart(loadedChartsheet) : NULL;
    CHECK(loadedChartsheetChart && loadedChartsheetChart->m_chartType == XChart_PieChart &&
          loadedChartsheetChart->m_chartTitle &&
          XString_equals_utf8(loadedChartsheetChart->m_chartTitle, "Chartsheet <pie>",
              XChar_CaseSensitive), "Chartsheet 类型与关联图表完整往返");

    XDocument_delete(document);
    XChart_delete(chartsheetChart);
    if (loaded) XDocument_delete(loaded);
    remove(XString_toUtf8(path));
    XString_deinit_base(path);
    XString_deinit_base(url);
    XString_deinit_base(display);
    XString_deinit_base(chartSheetName);
    XString_deinit_base(chart1Path);
    XString_deinit_base(chart2Path);
    XString_deinit_base(chartSheetPath);
    XString_deinit_base(drawing1Path);
    XString_deinit_base(drawing2Path);
    return groupOk;
}

static bool test_document_device_and_images(void)
{
    bool groupOk = true;
    XPrintf("[INFO] 扩展测试：Document 设备与图片包往返\n");
    XString_Init_Utf8(packagePath, "/tmp/xinyue_excel_device.xlsx");
    XString_Init_Utf8(imagePath, xexcel_asset_path("配置cmake.png"));
    XDocument* document = XDocument_create();
    XDocument_setDocumentProperty_utf8(document, "title", "Device & image <roundtrip>");
    XString_Init_Utf8(customTheme,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"XinYueC Theme\"><a:themeElements/></a:theme>");
    XTheme_setThemeXmlData(XWorkbook_theme(document->m_workbook), customTheme);
    XWorksheet* worksheet = XDocument_currentWorksheet(document);
    CHECK(worksheet && XWorksheet_writeString_utf8(worksheet, 1, 1,
          "device & image roundtrip", NULL), "设备文档写入单元格");
    XFormat* deviceFormat = XFormat_create();
    XFormat_setFontBold(deviceFormat, true);
    XFormat_setFillPattern(deviceFormat, XFormat_PatternSolid);
    XColor deviceFill = XColor_create_rgb(0x20, 0x80, 0xe0, 0xff);
    XFormat_setPatternForegroundColor(deviceFormat, &deviceFill);
    XFormat_setHorizontalAlignment(deviceFormat, XFormat_AlignHCenter);
    CHECK(XWorksheet_writeString_utf8(worksheet, 2, 2, "formatted", deviceFormat) &&
          XWorksheet_setColumnFormat(worksheet, 2, 2, deviceFormat) &&
          XWorksheet_setRowFormat(worksheet, 2, 2, deviceFormat),
          "单元格、列和行格式注册到 Workbook 样式表");
    XFormat_delete(deviceFormat);
    int imageIndex = XDocument_insertImage(document, 4, 5, imagePath);
    CHECK(imageIndex == 0 && XDocument_getImageCount(document) == 1,
          "插入实际 PNG 图片");
    XByteArray* originalImage = XByteArray_create();
    CHECK(XDocument_getImageAt(document, 4, 5, originalImage), "按锚点位置读取原始图片");
    XFile* outputDevice = XFile_create_2(packagePath);
    CHECK(outputDevice && XDocument_saveAsDevice(document, (XIODevice*)outputDevice),
          "直接保存到 XIODevice，无临时平台 API");
    if (outputDevice) XClass_delete_base((XClass*)outputDevice);
    XFile* inputDevice = XFile_create_2(packagePath);
    XDocument* loaded = inputDevice ? XDocument_createFromDevice((XIODevice*)inputDevice) : NULL;
    CHECK(loaded && XDocument_isLoadPackage(loaded), "从 XIODevice 内存包加载文档");
    if (inputDevice) XClass_delete_base((XClass*)inputDevice);
    XWorksheet* loadedWorksheet = loaded ? XDocument_currentWorksheet(loaded) : NULL;
    XCell* loadedCell = loadedWorksheet ? XWorksheet_cellAt(loadedWorksheet, 1, 1) : NULL;
    CHECK(loadedCell && XString_equals_utf8(XCell_value(loadedCell),
          "device & image roundtrip", XChar_CaseSensitive), "设备往返恢复单元格");
    CHECK(loaded && XString_equals_utf8(XDocument_documentProperty_utf8(loaded, "title"),
          "Device & image <roundtrip>", XChar_CaseSensitive), "XDocument 包往返恢复核心属性");
    const XTheme* loadedTheme = loaded
        ? XWorkbook_theme(loaded->m_workbook) : NULL;
    CHECK(loadedTheme && XString_equals(XTheme_themeXmlData(loadedTheme), customTheme,
          XChar_CaseSensitive), "XDocument 包往返保留自定义主题 XML");
    XCell* formattedCell = loadedWorksheet ? XWorksheet_cellAt(loadedWorksheet, 2, 2) : NULL;
    XFormat* formatted = formattedCell ? XCell_format(formattedCell) : NULL;
    CHECK(formatted && XFormat_fontBold(formatted) &&
          XFormat_fillPattern(formatted) == XFormat_PatternSolid &&
          XFormat_horizontalAlignment(formatted) == XFormat_AlignHCenter,
          "XDocument 包往返恢复单元格样式");
    CHECK(loadedWorksheet && XWorksheet_columnFormat(loadedWorksheet, 2) &&
          XWorksheet_rowFormat(loadedWorksheet, 2), "XDocument 包往返恢复行列样式");
    CHECK(loaded && XDocument_getImageCount(loaded) == 1, "加载 drawing 关系并恢复图片数量");
    XByteArray* loadedImage = XByteArray_create();
    CHECK(loaded && XDocument_getImageAt(loaded, 4, 5, loadedImage) &&
          XByteArray_size_base((XContainer*)loadedImage) ==
          XByteArray_size_base((XContainer*)originalImage) &&
          memcmp(XByteArray_data(loadedImage), XByteArray_data(originalImage),
                 XByteArray_size_base((XContainer*)originalImage)) == 0,
          "设备往返恢复图片字节与锚点位置");
    XByteArray_delete_base(originalImage);
    XByteArray_delete_base(loadedImage);
    XDocument_delete(document);
    if (loaded) XDocument_delete(loaded);
    remove(XString_toUtf8(packagePath));
    XString_deinit_base(packagePath);
    XString_deinit_base(imagePath);
    XString_deinit_base(customTheme);
    return groupOk;
}

bool XExcelExtendedTest_runAll(void)
{
    g_checks = 0;
    g_failures = 0;
    bool ok = true;
    ok = test_sax_boundaries() && ok;
    ok = test_xmlstream_qt_behaviour() && ok;
    ok = test_zip_roundtrip() && ok;
    ok = test_workbook_worksheet_roundtrip() && ok;
    ok = test_chart_drawing_roundtrip() && ok;
    ok = test_support_modules() && ok;
    ok = test_worksheet_features_roundtrip() && ok;
    ok = test_document_properties_roundtrip() && ok;
    ok = test_styles_roundtrip() && ok;
    ok = test_document_charts_and_hyperlinks() && ok;
    ok = test_document_device_and_images() && ok;
    XPrintf("[INFO] XExcel 扩展测试：%d 项断言，失败 %d\n", g_checks, g_failures);
    return ok && g_failures == 0;
}
