#include "XStyles.h"

#include "XByteArray.h"
#include "XFile.h"
#include "XMemory.h"
#include "XXmlStreamReader.h"
#include "XXmlStreamWriter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParsedNumFmt {
    int m_id;
    XString* m_code;
} ParsedNumFmt;

static void delete_format_vector(XVector* formats)
{
    if (!formats) return;
    size_t count = XVector_size_base((XContainer*)formats);
    for (size_t i = 0; i < count; ++i) {
        XFormat** format = (XFormat**)XVector_at_base(formats, i);
        if (format && *format) XFormat_delete(*format);
    }
    XVector_clear_base(formats);
}

static bool append_format_copy(XVector* formats, const XFormat* source)
{
    if (!formats) return false;
    XFormat* copy = XFormat_create();
    if (!copy) return false;
    if (source) XFormat_copy(copy, source);
    if (!XVector_push_back_2(formats, &copy, 1)) {
        XFormat_delete(copy);
        return false;
    }
    return true;
}

XStyles* XStyles_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XStyles* self = (XStyles*)XMalloc_System(sizeof(XStyles));
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_fontsList = XVector_create(sizeof(XFormat*));
    self->m_fillsList = XVector_create(sizeof(XFormat*));
    self->m_bordersList = XVector_create(sizeof(XFormat*));
    self->m_xfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_dxfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_customNumFmtIdMap = XMap_create_ex(sizeof(int), sizeof(int), int_compare, false);
    self->m_nextCustomNumFmtId = 164;
    for (int i = 0; i < 64; ++i)
        self->m_indexedColors[i] = XColor_create_rgb(0, 0, 0, 0);
    if (!self->m_fontsList || !self->m_fillsList || !self->m_bordersList ||
        !self->m_xfFormatsList || !self->m_dxfFormatsList || !self->m_customNumFmtIdMap) {
        XStyles_delete(self);
        return NULL;
    }
    return self;
}

void XStyles_delete(XStyles* self)
{
    if (!self) return;
    XVector* vectors[] = { self->m_fontsList, self->m_fillsList, self->m_bordersList,
                           self->m_xfFormatsList, self->m_dxfFormatsList };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        if (!vectors[i]) continue;
        delete_format_vector(vectors[i]);
        XVector_delete_base(vectors[i]);
    }
    if (self->m_customNumFmtIdMap) {
        XMap_delete_base(self->m_customNumFmtIdMap);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

void XStyles_addXfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self || !self->m_xfFormatsList) return;
    if (!self->m_emptyFormatAdded) {
        XFormat* empty = XFormat_create();
        if (!empty) return;
        XFormat_setXfIndex(empty, 0);
        if (!XVector_push_back_2(self->m_xfFormatsList, &empty, 1)) {
            XFormat_delete(empty);
            return;
        }
        self->m_emptyFormatAdded = true;
    }
    if (!format || XFormat_isEmpty(format)) return;
    if (!force) {
        size_t count = XVector_size_base((XContainer*)self->m_xfFormatsList);
        for (size_t i = 0; i < count; ++i) {
            XFormat* existing = *(XFormat**)XVector_at_base(self->m_xfFormatsList, i);
            if (existing && XFormat_equals(existing, format)) return;
        }
    }
    XFormat* copy = XFormat_create();
    if (!copy) return;
    XFormat_copy(copy, format);
    XFormat_setXfIndex(copy, (int)XVector_size_base((XContainer*)self->m_xfFormatsList));
    if (!XVector_push_back_2(self->m_xfFormatsList, &copy, 1)) XFormat_delete(copy);
}

XFormat* XStyles_xfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_xfFormatsList || idx < 0 ||
        (size_t)idx >= XVector_size_base((XContainer*)self->m_xfFormatsList)) return NULL;
    return *(XFormat**)XVector_at_base(self->m_xfFormatsList, (size_t)idx);
}

void XStyles_addDxfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self || !self->m_dxfFormatsList || !format) return;
    if (!force) {
        size_t count = XVector_size_base((XContainer*)self->m_dxfFormatsList);
        for (size_t i = 0; i < count; ++i) {
            XFormat* existing = *(XFormat**)XVector_at_base(self->m_dxfFormatsList, i);
            if (existing && XFormat_equals(existing, format)) return;
        }
    }
    XFormat* copy = XFormat_create();
    if (!copy) return;
    XFormat_copy(copy, format);
    XFormat_setDxfIndex(copy, (int)XVector_size_base((XContainer*)self->m_dxfFormatsList));
    if (!XVector_push_back_2(self->m_dxfFormatsList, &copy, 1)) XFormat_delete(copy);
}

XFormat* XStyles_dxfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_dxfFormatsList || idx < 0 ||
        (size_t)idx >= XVector_size_base((XContainer*)self->m_dxfFormatsList)) return NULL;
    return *(XFormat**)XVector_at_base(self->m_dxfFormatsList, (size_t)idx);
}

XColor XStyles_getColorByIndex(XStyles* self, int idx)
{
    if (self && idx >= 0 && idx < 64) return self->m_indexedColors[idx];
    return XColor_create_rgb(0, 0, 0, 0);
}

static const char* underline_to_string(XFormat_FontUnderline value)
{
    switch (value) {
    case XFormat_FontUnderlineSingle: return "single";
    case XFormat_FontUnderlineDouble: return "double";
    case XFormat_FontUnderlineSingleAccounting: return "singleAccounting";
    case XFormat_FontUnderlineDoubleAccounting: return "doubleAccounting";
    default: return NULL;
    }
}

static XFormat_FontUnderline underline_from_string(const XString* value)
{
    if (!value || XString_equals_utf8(value, "single", XChar_CaseSensitive))
        return XFormat_FontUnderlineSingle;
    if (XString_equals_utf8(value, "double", XChar_CaseSensitive))
        return XFormat_FontUnderlineDouble;
    if (XString_equals_utf8(value, "singleAccounting", XChar_CaseSensitive))
        return XFormat_FontUnderlineSingleAccounting;
    if (XString_equals_utf8(value, "doubleAccounting", XChar_CaseSensitive))
        return XFormat_FontUnderlineDoubleAccounting;
    return XFormat_FontUnderlineNone;
}

static const char* border_to_string(XFormat_BorderStyle value)
{
    static const char* names[] = { "none", "thin", "medium", "dashed", "dotted",
        "thick", "double", "hair", "mediumDashed", "dashDot", "mediumDashDot",
        "dashDotDot", "mediumDashDotDot", "slantDashDot" };
    return (value >= XFormat_BorderNone && value <= XFormat_BorderSlantDashDot)
        ? names[value] : "none";
}

static XFormat_BorderStyle border_from_string(const XString* value)
{
    if (!value) return XFormat_BorderNone;
    for (int i = XFormat_BorderNone; i <= XFormat_BorderSlantDashDot; ++i) {
        if (XString_equals_utf8(value, border_to_string((XFormat_BorderStyle)i), XChar_CaseSensitive))
            return (XFormat_BorderStyle)i;
    }
    return XFormat_BorderNone;
}

static const char* fill_to_string(XFormat_FillPattern value)
{
    static const char* names[] = { "none", "solid", "mediumGray", "darkGray", "lightGray",
        "darkHorizontal", "darkVertical", "darkDown", "darkUp", "darkGrid", "darkTrellis",
        "lightHorizontal", "lightVertical", "lightDown", "lightUp", "lightTrellis",
        "gray125", "gray0625", "lightGrid" };
    return (value >= XFormat_PatternNone && value <= XFormat_PatternLightGrid)
        ? names[value] : "none";
}

static XFormat_FillPattern fill_from_string(const XString* value)
{
    if (!value) return XFormat_PatternNone;
    for (int i = XFormat_PatternNone; i <= XFormat_PatternLightGrid; ++i) {
        if (XString_equals_utf8(value, fill_to_string((XFormat_FillPattern)i), XChar_CaseSensitive))
            return (XFormat_FillPattern)i;
    }
    return XFormat_PatternNone;
}

static const char* horizontal_to_string(XFormat_HorizontalAlignment value)
{
    static const char* names[] = { "general", "left", "center", "right", "fill",
        "justify", "centerContinuous", "distributed" };
    return (value >= XFormat_AlignHGeneral && value <= XFormat_AlignHDistributed)
        ? names[value] : "general";
}

static XFormat_HorizontalAlignment horizontal_from_string(const XString* value)
{
    if (!value) return XFormat_AlignHGeneral;
    for (int i = XFormat_AlignHGeneral; i <= XFormat_AlignHDistributed; ++i) {
        if (XString_equals_utf8(value, horizontal_to_string((XFormat_HorizontalAlignment)i), XChar_CaseSensitive))
            return (XFormat_HorizontalAlignment)i;
    }
    return XFormat_AlignHGeneral;
}

static const char* vertical_to_string(XFormat_VerticalAlignment value)
{
    static const char* names[] = { "top", "center", "bottom", "justify", "distributed" };
    return (value >= XFormat_AlignTop && value <= XFormat_AlignVDistributed)
        ? names[value] : "bottom";
}

static XFormat_VerticalAlignment vertical_from_string(const XString* value)
{
    if (!value) return XFormat_AlignBottom;
    for (int i = XFormat_AlignTop; i <= XFormat_AlignVDistributed; ++i) {
        if (XString_equals_utf8(value, vertical_to_string((XFormat_VerticalAlignment)i), XChar_CaseSensitive))
            return (XFormat_VerticalAlignment)i;
    }
    return XFormat_AlignBottom;
}

static void write_int_attribute(XXmlStreamWriter* writer, const char* name, int value)
{
    char text[32];
    snprintf(text, sizeof(text), "%d", value);
    XXmlStreamWriter_writeAttribute_utf8(writer, name, text);
}

static void write_val_element(XXmlStreamWriter* writer, const char* name, const char* value)
{
    XXmlStreamWriter_writeEmptyElement_utf8(writer, name);
    XXmlStreamWriter_writeAttribute_utf8(writer, "val", value ? value : "");
}

static void color_to_argb(const XColor* color, char output[9])
{
    snprintf(output, 9, "%02X%02X%02X%02X", XColor_alpha(color), XColor_red(color),
             XColor_green(color), XColor_blue(color));
}

static void write_color(XXmlStreamWriter* writer, const char* element, const XColor* color)
{
    if (!color || !XColor_isValid(color)) return;
    char argb[9];
    color_to_argb(color, argb);
    XXmlStreamWriter_writeEmptyElement_utf8(writer, element);
    XXmlStreamWriter_writeAttribute_utf8(writer, "rgb", argb);
}

static void write_font(XXmlStreamWriter* writer, const XFormat* format)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, "font");
    if (format) {
        if (XFormat_fontBold(format)) XXmlStreamWriter_writeEmptyElement_utf8(writer, "b");
        if (XFormat_fontItalic(format)) XXmlStreamWriter_writeEmptyElement_utf8(writer, "i");
        if (XFormat_fontStrikeOut(format)) XXmlStreamWriter_writeEmptyElement_utf8(writer, "strike");
        int size = XFormat_fontSize(format);
        if (size > 0) {
            char value[32]; snprintf(value, sizeof(value), "%d", size);
            write_val_element(writer, "sz", value);
        }
        const char* underline = underline_to_string(XFormat_fontUnderline(format));
        if (underline) write_val_element(writer, "u", underline);
        if (XFormat_fontScript(format) == XFormat_FontScriptSuper)
            write_val_element(writer, "vertAlign", "superscript");
        else if (XFormat_fontScript(format) == XFormat_FontScriptSub)
            write_val_element(writer, "vertAlign", "subscript");
        XColor color = XFormat_fontColor(format);
        write_color(writer, "color", &color);
        const char* name = XFormat_fontName_utf8(format);
        if (name && name[0]) write_val_element(writer, "name", name);
    }
    XXmlStreamWriter_writeEndElement(writer);
}

static void write_fill(XXmlStreamWriter* writer, const XFormat* format)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, "fill");
    XXmlStreamWriter_writeStartElement_utf8(writer, "patternFill");
    XXmlStreamWriter_writeAttribute_utf8(writer, "patternType",
        fill_to_string(format ? XFormat_fillPattern(format) : XFormat_PatternNone));
    if (format) {
        XColor foreground = XFormat_patternForegroundColor(format);
        XColor background = XFormat_patternBackgroundColor(format);
        write_color(writer, "fgColor", &foreground);
        write_color(writer, "bgColor", &background);
    }
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
}

static void write_border_side(XXmlStreamWriter* writer, const char* name,
                              XFormat_BorderStyle style, const XColor* color)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, name);
    if (style != XFormat_BorderNone)
        XXmlStreamWriter_writeAttribute_utf8(writer, "style", border_to_string(style));
    write_color(writer, "color", color);
    XXmlStreamWriter_writeEndElement(writer);
}

static void write_border(XXmlStreamWriter* writer, const XFormat* format)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, "border");
    if (!format) {
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "left");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "right");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "top");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "bottom");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "diagonal");
    } else {
        XColor left = XFormat_leftBorderColor(format);
        XColor right = XFormat_rightBorderColor(format);
        XColor top = XFormat_topBorderColor(format);
        XColor bottom = XFormat_bottomBorderColor(format);
        XColor diagonal = XFormat_diagonalBorderColor(format);
        write_border_side(writer, "left", XFormat_leftBorderStyle(format), &left);
        write_border_side(writer, "right", XFormat_rightBorderStyle(format), &right);
        write_border_side(writer, "top", XFormat_topBorderStyle(format), &top);
        write_border_side(writer, "bottom", XFormat_bottomBorderStyle(format), &bottom);
        write_border_side(writer, "diagonal", XFormat_diagonalBorderStyle(format), &diagonal);
    }
    XXmlStreamWriter_writeEndElement(writer);
}

static int format_count_or_default(const XStyles* self)
{
    int count = self && self->m_xfFormatsList
        ? (int)XVector_size_base((XContainer*)self->m_xfFormatsList) : 0;
    return count > 0 ? count : 1;
}

static const XFormat* format_at_or_default(const XStyles* self, int index)
{
    if (!self || !self->m_xfFormatsList ||
        (size_t)index >= XVector_size_base((XContainer*)self->m_xfFormatsList)) return NULL;
    return *(XFormat**)XVector_at_base(self->m_xfFormatsList, (size_t)index);
}

bool XStyles_saveToXmlData(const XStyles* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "styleSheet");
    XXmlStreamWriter_writeDefaultNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/spreadsheetml/2006/main");

    int count = format_count_or_default(self);
    int customCount = 0;
    for (int i = 0; i < count; ++i) {
        const XFormat* format = format_at_or_default(self, i);
        const XString* code = format ? XFormat_numberFormat(format) : NULL;
        if (code && XString_size(code) > 0 && XFormat_numberFormatIndex(format) >= 164)
            customCount++;
    }
    if (customCount > 0) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "numFmts");
        write_int_attribute(writer, "count", customCount);
        for (int i = 0; i < count; ++i) {
            const XFormat* format = format_at_or_default(self, i);
            const XString* code = format ? XFormat_numberFormat(format) : NULL;
            if (!code || XString_size(code) == 0 || XFormat_numberFormatIndex(format) < 164) continue;
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "numFmt");
            write_int_attribute(writer, "numFmtId", XFormat_numberFormatIndex(format));
            XXmlStreamWriter_writeAttribute_utf8(writer, "formatCode", XString_toUtf8(code));
        }
        XXmlStreamWriter_writeEndElement(writer);
    }

    XXmlStreamWriter_writeStartElement_utf8(writer, "fonts");
    write_int_attribute(writer, "count", count);
    for (int i = 0; i < count; ++i) write_font(writer, format_at_or_default(self, i));
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "fills");
    write_int_attribute(writer, "count", count + 2);
    write_fill(writer, NULL);
    XFormat* gray = XFormat_create();
    if (gray) XFormat_setFillPattern(gray, XFormat_PatternGray125);
    write_fill(writer, gray);
    if (gray) XFormat_delete(gray);
    for (int i = 0; i < count; ++i) write_fill(writer, format_at_or_default(self, i));
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "borders");
    write_int_attribute(writer, "count", count);
    for (int i = 0; i < count; ++i) write_border(writer, format_at_or_default(self, i));
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "cellStyleXfs");
    XXmlStreamWriter_writeAttribute_utf8(writer, "count", "1");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "xf");
    XXmlStreamWriter_writeAttribute_utf8(writer, "numFmtId", "0");
    XXmlStreamWriter_writeAttribute_utf8(writer, "fontId", "0");
    XXmlStreamWriter_writeAttribute_utf8(writer, "fillId", "0");
    XXmlStreamWriter_writeAttribute_utf8(writer, "borderId", "0");
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "cellXfs");
    write_int_attribute(writer, "count", count);
    for (int i = 0; i < count; ++i) {
        const XFormat* format = format_at_or_default(self, i);
        XXmlStreamWriter_writeStartElement_utf8(writer, "xf");
        write_int_attribute(writer, "numFmtId", format ? XFormat_numberFormatIndex(format) : 0);
        write_int_attribute(writer, "fontId", i);
        write_int_attribute(writer, "fillId", i + 2);
        write_int_attribute(writer, "borderId", i);
        XXmlStreamWriter_writeAttribute_utf8(writer, "xfId", "0");
        if (format && XFormat_hasNumFmtData(format))
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyNumberFormat", "1");
        if (format && XFormat_hasFontData(format))
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyFont", "1");
        if (format && XFormat_hasFillData(format))
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyFill", "1");
        if (format && XFormat_hasBorderData(format))
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyBorder", "1");
        if (format && XFormat_hasAlignmentData(format)) {
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyAlignment", "1");
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "alignment");
            XXmlStreamWriter_writeAttribute_utf8(writer, "horizontal",
                horizontal_to_string(XFormat_horizontalAlignment(format)));
            XXmlStreamWriter_writeAttribute_utf8(writer, "vertical",
                vertical_to_string(XFormat_verticalAlignment(format)));
            write_int_attribute(writer, "wrapText", XFormat_textWrap(format));
            write_int_attribute(writer, "textRotation", XFormat_rotation(format));
            write_int_attribute(writer, "indent", XFormat_indent(format));
            write_int_attribute(writer, "shrinkToFit", XFormat_shrinkToFit(format));
        }
        if (format && XFormat_hasProtectionData(format)) {
            XXmlStreamWriter_writeAttribute_utf8(writer, "applyProtection", "1");
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "protection");
            write_int_attribute(writer, "locked", XFormat_locked(format));
            write_int_attribute(writer, "hidden", XFormat_hidden(format));
        }
        XXmlStreamWriter_writeEndElement(writer);
    }
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "cellStyles");
    XXmlStreamWriter_writeAttribute_utf8(writer, "count", "1");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "cellStyle");
    XXmlStreamWriter_writeAttribute_utf8(writer, "name", "Normal");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xfId", "0");
    XXmlStreamWriter_writeAttribute_utf8(writer, "builtinId", "0");
    XXmlStreamWriter_writeEndElement(writer);

    int dxfCount = self->m_dxfFormatsList
        ? (int)XVector_size_base((XContainer*)self->m_dxfFormatsList) : 0;
    XXmlStreamWriter_writeStartElement_utf8(writer, "dxfs");
    write_int_attribute(writer, "count", dxfCount);
    for (int i = 0; i < dxfCount; ++i) {
        const XFormat* format = *(XFormat**)XVector_at_base(self->m_dxfFormatsList, (size_t)i);
        XXmlStreamWriter_writeStartElement_utf8(writer, "dxf");
        if (format && XFormat_hasFontData(format)) write_font(writer, format);
        if (format && XFormat_hasFillData(format)) write_fill(writer, format);
        if (format && XFormat_hasBorderData(format)) write_border(writer, format);
        XXmlStreamWriter_writeEndElement(writer);
    }
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "tableStyles");
    XXmlStreamWriter_writeAttribute_utf8(writer, "count", "0");
    XXmlStreamWriter_writeEndDocument(writer);

    XByteArray* buffer = XXmlStreamWriter_toByteArray(writer);
    size_t size = buffer ? XByteArray_size_base((XContainer*)buffer) : 0;
    if (!XXmlStreamWriter_hasError(writer) && size > 0) {
        *outData = (uint8_t*)XMalloc_System(size + 1);
        if (*outData) {
            memcpy(*outData, XByteArray_data(buffer), size);
            (*outData)[size] = '\0';
            *outLen = size;
        }
    }
    XXmlStreamWriter_delete_base(writer);
    return *outData != NULL;
}

bool XStyles_saveToXmlFile(XStyles* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XStyles_saveToXmlData(self, &data, &len)) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    bool ok = file && XIODevice_open_base((XIODevice*)file,
        XIODevice_WriteOnly | XIODevice_Truncate) &&
        XIODevice_write_1((XIODevice*)file, (const char*)data, (int64_t)len) == (int64_t)len;
    if (file) {
        if (XIODevice_isOpen((XIODevice*)file)) XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    }
    XFree_System(data);
    return ok;
}

static const XString* attribute(const XXmlStreamReader* reader, const char* name)
{
    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, name);
    const XString* value = XXmlStreamAttributes_value(XXmlStreamReader_attributes(reader), &key);
    XString_deinit_base(&key);
    return value;
}

static int attribute_int(const XXmlStreamReader* reader, const char* name, int fallback)
{
    const XString* value = attribute(reader, name);
    const char* text = value ? XString_toUtf8(value) : NULL;
    return text ? atoi(text) : fallback;
}

static bool parse_argb(const char* text, XColor* color)
{
    if (!text || !color) return false;
    size_t length = strlen(text);
    if (length != 6 && length != 8) return false;
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 16);
    if (!end || *end != '\0') return false;
    int alpha = length == 8 ? (int)((value >> 24) & 0xff) : 255;
    int red = (int)((value >> 16) & 0xff);
    int green = (int)((value >> 8) & 0xff);
    int blue = (int)(value & 0xff);
    XColor_setRgb(color, red, green, blue, alpha);
    return true;
}

static void apply_border_style(XFormat* format, int side, XFormat_BorderStyle style)
{
    if (!format) return;
    if (side == 1) XFormat_setLeftBorderStyle(format, style);
    else if (side == 2) XFormat_setRightBorderStyle(format, style);
    else if (side == 3) XFormat_setTopBorderStyle(format, style);
    else if (side == 4) XFormat_setBottomBorderStyle(format, style);
    else if (side == 5) XFormat_setDiagonalBorderStyle(format, style);
}

static void apply_border_color(XFormat* format, int side, const XColor* color)
{
    if (!format || !color) return;
    if (side == 1) XFormat_setLeftBorderColor(format, color);
    else if (side == 2) XFormat_setRightBorderColor(format, color);
    else if (side == 3) XFormat_setTopBorderColor(format, color);
    else if (side == 4) XFormat_setBottomBorderColor(format, color);
    else if (side == 5) XFormat_setDiagonalBorderColor(format, color);
}

static const XString* num_format_code(const XVector* formats, int id)
{
    if (!formats) return NULL;
    size_t count = XVector_size_base((XContainer*)formats);
    for (size_t i = 0; i < count; ++i) {
        ParsedNumFmt* format = (ParsedNumFmt*)XVector_at_base((XVector*)formats, i);
        if (format && format->m_id == id) return format->m_code;
    }
    return NULL;
}

static void clear_num_formats(XVector* formats)
{
    if (!formats) return;
    size_t count = XVector_size_base((XContainer*)formats);
    for (size_t i = 0; i < count; ++i) {
        ParsedNumFmt* format = (ParsedNumFmt*)XVector_at_base(formats, i);
        if (format && format->m_code) XString_delete_base(format->m_code);
    }
    XVector_delete_base(formats);
}

bool XStyles_loadFromXmlData(XStyles* self, const uint8_t* data, size_t len)
{
    if (!self || !data || len == 0) return false;
    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XVector* numFormats = XVector_create(sizeof(ParsedNumFmt));
    if (!bytes || !reader || !numFormats) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        if (numFormats) XVector_delete_base(numFormats);
        return false;
    }
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    delete_format_vector(self->m_fontsList);
    delete_format_vector(self->m_fillsList);
    delete_format_vector(self->m_bordersList);
    delete_format_vector(self->m_xfFormatsList);
    delete_format_vector(self->m_dxfFormatsList);
    self->m_emptyFormatAdded = false;

    enum { SectionNone, SectionFonts, SectionFills, SectionBorders,
           SectionCellXfs, SectionDxfs } section = SectionNone;
    enum { DxfComponentNone, DxfComponentFont, DxfComponentFill,
           DxfComponentBorder } dxfComponent = DxfComponentNone;
    XFormat* current = NULL;
    int borderSide = 0;
    bool rootSeen = false;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* element = XXmlStreamReader_name(reader);
        if (token == XXmlStream_StartElement && element) {
            if (XString_equals_utf8(element, "styleSheet", XChar_CaseSensitive)) rootSeen = true;
            else if (XString_equals_utf8(element, "fonts", XChar_CaseSensitive)) section = SectionFonts;
            else if (XString_equals_utf8(element, "fills", XChar_CaseSensitive)) section = SectionFills;
            else if (XString_equals_utf8(element, "borders", XChar_CaseSensitive)) section = SectionBorders;
            else if (XString_equals_utf8(element, "cellXfs", XChar_CaseSensitive)) section = SectionCellXfs;
            else if (XString_equals_utf8(element, "dxfs", XChar_CaseSensitive)) section = SectionDxfs;
            else if (XString_equals_utf8(element, "numFmt", XChar_CaseSensitive)) {
                const XString* code = attribute(reader, "formatCode");
                ParsedNumFmt parsed = { attribute_int(reader, "numFmtId", 0),
                                        code ? XString_create_copy(code) : NULL };
                if (parsed.m_code) XVector_push_back_2(numFormats, &parsed, 1);
            } else if ((section == SectionFonts && XString_equals_utf8(element, "font", XChar_CaseSensitive)) ||
                       (section == SectionFills && XString_equals_utf8(element, "fill", XChar_CaseSensitive)) ||
                       (section == SectionBorders && XString_equals_utf8(element, "border", XChar_CaseSensitive)) ||
                       (section == SectionDxfs && XString_equals_utf8(element, "dxf", XChar_CaseSensitive))) {
                if (current) XFormat_delete(current);
                current = XFormat_create();
                if (section == SectionDxfs) dxfComponent = DxfComponentNone;
            } else if (current && section == SectionDxfs && XString_equals_utf8(element, "font", XChar_CaseSensitive)) {
                dxfComponent = DxfComponentFont;
            } else if (current && section == SectionDxfs && XString_equals_utf8(element, "fill", XChar_CaseSensitive)) {
                dxfComponent = DxfComponentFill;
            } else if (current && section == SectionDxfs && XString_equals_utf8(element, "border", XChar_CaseSensitive)) {
                dxfComponent = DxfComponentBorder;
            } else if (section == SectionCellXfs && XString_equals_utf8(element, "xf", XChar_CaseSensitive)) {
                if (current) XFormat_delete(current);
                current = XFormat_create();
                int fontId = attribute_int(reader, "fontId", -1);
                int fillId = attribute_int(reader, "fillId", -1);
                int borderId = attribute_int(reader, "borderId", -1);
                int numFmtId = attribute_int(reader, "numFmtId", 0);
                if (fontId >= 0 && (size_t)fontId < XVector_size_base((XContainer*)self->m_fontsList))
                    XFormat_mergeFormat(current, *(XFormat**)XVector_at_base(self->m_fontsList, fontId));
                if (fillId >= 0 && (size_t)fillId < XVector_size_base((XContainer*)self->m_fillsList))
                    XFormat_mergeFormat(current, *(XFormat**)XVector_at_base(self->m_fillsList, fillId));
                if (borderId >= 0 && (size_t)borderId < XVector_size_base((XContainer*)self->m_bordersList))
                    XFormat_mergeFormat(current, *(XFormat**)XVector_at_base(self->m_bordersList, borderId));
                const XString* code = num_format_code(numFormats, numFmtId);
                if (code) XFormat_setNumberFormat_ex(current, numFmtId, code);
                else if (numFmtId != 0) XFormat_setNumberFormatIndex(current, numFmtId);
            } else if (current && (section == SectionFonts ||
                       (section == SectionDxfs && dxfComponent == DxfComponentFont))) {
                const XString* val = attribute(reader, "val");
                if (XString_equals_utf8(element, "b", XChar_CaseSensitive)) XFormat_setFontBold(current, !val || atoi(XString_toUtf8(val)) != 0);
                else if (XString_equals_utf8(element, "i", XChar_CaseSensitive)) XFormat_setFontItalic(current, !val || atoi(XString_toUtf8(val)) != 0);
                else if (XString_equals_utf8(element, "strike", XChar_CaseSensitive)) XFormat_setFontStrikeOut(current, !val || atoi(XString_toUtf8(val)) != 0);
                else if (XString_equals_utf8(element, "sz", XChar_CaseSensitive) && val) XFormat_setFontSize(current, atoi(XString_toUtf8(val)));
                else if (XString_equals_utf8(element, "name", XChar_CaseSensitive) && val) XFormat_setFontName_utf8(current, XString_toUtf8(val));
                else if (XString_equals_utf8(element, "u", XChar_CaseSensitive)) XFormat_setFontUnderline(current, underline_from_string(val));
                else if (XString_equals_utf8(element, "vertAlign", XChar_CaseSensitive) && val)
                    XFormat_setFontScript(current, XString_equals_utf8(val, "superscript", XChar_CaseSensitive)
                        ? XFormat_FontScriptSuper : XFormat_FontScriptSub);
                else if (XString_equals_utf8(element, "color", XChar_CaseSensitive)) {
                    const XString* rgb = attribute(reader, "rgb");
                    XColor color; XColor_init(&color);
                    if (rgb && parse_argb(XString_toUtf8(rgb), &color)) XFormat_setFontColor(current, &color);
                }
            } else if (current && (section == SectionFills ||
                       (section == SectionDxfs && dxfComponent == DxfComponentFill))) {
                if (XString_equals_utf8(element, "patternFill", XChar_CaseSensitive)) {
                    const XString* pattern = attribute(reader, "patternType");
                    XFormat_setFillPattern(current, fill_from_string(pattern));
                } else if (XString_equals_utf8(element, "fgColor", XChar_CaseSensitive) ||
                           XString_equals_utf8(element, "bgColor", XChar_CaseSensitive)) {
                    const XString* rgb = attribute(reader, "rgb");
                    XColor color; XColor_init(&color);
                    if (rgb && parse_argb(XString_toUtf8(rgb), &color)) {
                        if (XString_equals_utf8(element, "fgColor", XChar_CaseSensitive)) XFormat_setPatternForegroundColor(current, &color);
                        else XFormat_setPatternBackgroundColor(current, &color);
                    }
                }
            } else if (current && (section == SectionBorders ||
                       (section == SectionDxfs && dxfComponent == DxfComponentBorder))) {
                if (XString_equals_utf8(element, "left", XChar_CaseSensitive)) borderSide = 1;
                else if (XString_equals_utf8(element, "right", XChar_CaseSensitive)) borderSide = 2;
                else if (XString_equals_utf8(element, "top", XChar_CaseSensitive)) borderSide = 3;
                else if (XString_equals_utf8(element, "bottom", XChar_CaseSensitive)) borderSide = 4;
                else if (XString_equals_utf8(element, "diagonal", XChar_CaseSensitive)) borderSide = 5;
                if (borderSide && !XString_equals_utf8(element, "color", XChar_CaseSensitive)) {
                    const XString* style = attribute(reader, "style");
                    apply_border_style(current, borderSide,
                        border_from_string(style));
                } else if (borderSide && XString_equals_utf8(element, "color", XChar_CaseSensitive)) {
                    const XString* rgb = attribute(reader, "rgb");
                    XColor color; XColor_init(&color);
                    if (rgb && parse_argb(XString_toUtf8(rgb), &color))
                        apply_border_color(current, borderSide, &color);
                }
            } else if (current && section == SectionCellXfs && XString_equals_utf8(element, "alignment", XChar_CaseSensitive)) {
                const XString* horizontal = attribute(reader, "horizontal");
                const XString* vertical = attribute(reader, "vertical");
                if (horizontal) XFormat_setHorizontalAlignment(current,
                    horizontal_from_string(horizontal));
                if (vertical) XFormat_setVerticalAlignment(current,
                    vertical_from_string(vertical));
                XFormat_setTextWrap(current, attribute_int(reader, "wrapText", 0) != 0);
                XFormat_setRotation(current, attribute_int(reader, "textRotation", 0));
                XFormat_setIndent(current, attribute_int(reader, "indent", 0));
                XFormat_setShrinkToFit(current, attribute_int(reader, "shrinkToFit", 0) != 0);
            } else if (current && section == SectionCellXfs && XString_equals_utf8(element, "protection", XChar_CaseSensitive)) {
                XFormat_setLocked(current, attribute_int(reader, "locked", 1) != 0);
                XFormat_setHidden(current, attribute_int(reader, "hidden", 0) != 0);
            }
        } else if (token == XXmlStream_EndElement && element) {
            if (section == SectionFonts && XString_equals_utf8(element, "font", XChar_CaseSensitive) && current) {
                XVector_push_back_2(self->m_fontsList, &current, 1); current = NULL;
            } else if (section == SectionFills && XString_equals_utf8(element, "fill", XChar_CaseSensitive) && current) {
                XVector_push_back_2(self->m_fillsList, &current, 1); current = NULL;
            } else if (section == SectionBorders && XString_equals_utf8(element, "border", XChar_CaseSensitive) && current) {
                XVector_push_back_2(self->m_bordersList, &current, 1); current = NULL;
            } else if (section == SectionCellXfs && XString_equals_utf8(element, "xf", XChar_CaseSensitive) && current) {
                XFormat_setXfIndex(current,
                    (int)XVector_size_base((XContainer*)self->m_xfFormatsList));
                XVector_push_back_2(self->m_xfFormatsList, &current, 1); current = NULL;
            } else if (section == SectionDxfs && XString_equals_utf8(element, "dxf", XChar_CaseSensitive) && current) {
                XFormat_setDxfIndex(current,
                    (int)XVector_size_base((XContainer*)self->m_dxfFormatsList));
                XVector_push_back_2(self->m_dxfFormatsList, &current, 1); current = NULL;
                dxfComponent = DxfComponentNone;
            } else if (section == SectionDxfs &&
                       (XString_equals_utf8(element, "font", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "fill", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "border", XChar_CaseSensitive))) {
                dxfComponent = DxfComponentNone;
            } else if (XString_equals_utf8(element, "fonts", XChar_CaseSensitive) ||
                       XString_equals_utf8(element, "fills", XChar_CaseSensitive) ||
                       XString_equals_utf8(element, "borders", XChar_CaseSensitive) ||
                       XString_equals_utf8(element, "cellXfs", XChar_CaseSensitive) ||
                       XString_equals_utf8(element, "dxfs", XChar_CaseSensitive)) {
                section = SectionNone;
            } else if ((section == SectionBorders ||
                        (section == SectionDxfs && dxfComponent == DxfComponentBorder)) &&
                       (XString_equals_utf8(element, "left", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "right", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "top", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "bottom", XChar_CaseSensitive) ||
                        XString_equals_utf8(element, "diagonal", XChar_CaseSensitive))) {
                borderSide = 0;
            }
        }
    }
    if (current) XFormat_delete(current);
    bool ok = rootSeen && !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    clear_num_formats(numFormats);
    if (ok && XVector_size_base((XContainer*)self->m_xfFormatsList) == 0) {
        ok = append_format_copy(self->m_xfFormatsList, NULL);
    }
    self->m_emptyFormatAdded = ok;
    return ok;
}

bool XStyles_loadFromXmlFile(XStyles* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* data = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!data) return false;
    bool ok = XStyles_loadFromXmlData(self, XByteArray_data(data),
        XByteArray_size_base((XContainer*)data));
    XByteArray_delete_base(data);
    return ok;
}
