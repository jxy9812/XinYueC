/**
 * @file XRegularExpression.c
 * @brief Qt 6.8 QRegularExpression 对齐实现。
 */

#include "XRegularExpression.h"
#include "XMemory.h"
#include "XAtomic.h"
#include "XHashFunc.h"
#include "XMutex.h"
#include "pcre2_xin_memory.h"
#include <limits.h>
#include <string.h>

typedef struct XRegularExpressionData {
    XAtomic_int32_t m_refCount;
    XString m_pattern;
    XString m_errorString;
    XRegularExpression_PatternOptions m_patternOptions;
    pcre2_general_context* m_generalContext;
    XMutex* m_compileMutex;
    pcre2_code* m_code;
    int m_errorCode;
    int64_t m_errorOffset;
    int m_captureCount;
    bool m_usingCrLfNewlines;
    bool m_dirty;
} XRegularExpressionData;

/* ============================== 内部辅助函数 ============================== */

static bool XRegularExpression_assign_utf16(XString* target, const uint16_t* data, size_t length)
{
    if (!target) return false;
    if (length == 0) {
        XString_clear_base(target);
        return true;
    }

    XString* value = XString_create_with_length_utf16(data, length);
    if (!value) return false;
    XString_move_base(target, value);
    XString_delete_base(value);
    return true;
}

static XRegularExpressionData* XRegularExpression_data_create(void)
{
    XRegularExpressionData* data = (XRegularExpressionData*)XMalloc_System(sizeof(XRegularExpressionData));
    if (!data) return NULL;

    memset(data, 0, sizeof(*data));
    XAtomic_init(data->m_refCount, 1);
    XString_init(&data->m_pattern);
    XString_init(&data->m_errorString);
    XString_assign_utf8(&data->m_errorString, "no error");
    data->m_errorOffset = -1;
    data->m_dirty = true;
    data->m_generalContext = pcre2_xin_general_context_create();
    if (!data->m_generalContext) {
        XString_deinit_base(&data->m_pattern);
        XString_deinit_base(&data->m_errorString);
        XFree_System(data);
        return NULL;
    }
    data->m_compileMutex = XMutex_create(XLock_NonRecursive);
    if (!data->m_compileMutex) {
        pcre2_general_context_free(data->m_generalContext);
        XString_deinit_base(&data->m_pattern);
        XString_deinit_base(&data->m_errorString);
        XFree_System(data);
        return NULL;
    }
    return data;
}

static void XRegularExpression_data_cleanCompiled(XRegularExpressionData* data)
{
    if (!data) return;
    if (data->m_code) {
        pcre2_code_free(data->m_code);
        data->m_code = NULL;
    }
    data->m_errorCode = 0;
    data->m_errorOffset = -1;
    data->m_captureCount = 0;
    data->m_usingCrLfNewlines = false;
    XString_assign_utf8(&data->m_errorString, "no error");
}

static void XRegularExpression_data_destroy(XRegularExpressionData* data)
{
    if (!data) return;
    XRegularExpression_data_cleanCompiled(data);
    if (data->m_generalContext)
        pcre2_general_context_free(data->m_generalContext);
    if (data->m_compileMutex)
        XMutex_delete(data->m_compileMutex);
    XString_deinit_base(&data->m_pattern);
    XString_deinit_base(&data->m_errorString);
    XFree_System(data);
}

static void XRegularExpression_data_addRef(XRegularExpressionData* data)
{
    if (!data) return;
    XAtomic_fetch_add_int32(&data->m_refCount, 1, XAtomic_MemoryOrder_Relaxed);
}

static void XRegularExpression_data_release(XRegularExpressionData* data)
{
    if (!data) return;
    if (XAtomic_fetch_sub_int32(&data->m_refCount, 1, XAtomic_MemoryOrder_AcqRel) == 1)
        XRegularExpression_data_destroy(data);
}

static XRegularExpressionData* XRegularExpression_data_clone(const XRegularExpressionData* source)
{
    if (!source) return XRegularExpression_data_create();
    XRegularExpressionData* data = XRegularExpression_data_create();
    if (!data) return NULL;
    if (!XString_assign(&data->m_pattern, &source->m_pattern)) {
        XRegularExpression_data_release(data);
        return NULL;
    }
    data->m_patternOptions = source->m_patternOptions;
    return data;
}

static bool XRegularExpression_detach(XRegularExpression* expression)
{
    if (!expression) return false;
    if (!expression->m_data) {
        expression->m_data = XRegularExpression_data_create();
        return expression->m_data != NULL;
    }
    if (XAtomic_load_int32(&expression->m_data->m_refCount, XAtomic_MemoryOrder_Acquire) == 1)
        return true;

    XRegularExpressionData* data = XRegularExpression_data_clone(expression->m_data);
    if (!data) return false;
    XRegularExpression_data_release(expression->m_data);
    expression->m_data = data;
    return true;
}

static void XRegularExpression_markDirty(XRegularExpressionData* data)
{
    if (!data) return;
    if (data->m_compileMutex) XMutex_lock(data->m_compileMutex);
    XRegularExpression_data_cleanCompiled(data);
    data->m_dirty = true;
    if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
}

static void XRegularExpression_compilePattern(XRegularExpressionData* data)
{
    if (!data) return;
    if (data->m_compileMutex) XMutex_lock(data->m_compileMutex);
    if (!data->m_dirty) {
        if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
        return;
    }
    data->m_dirty = false;
    XRegularExpression_data_cleanCompiled(data);

    if (!data->m_generalContext) {
        data->m_errorCode = PCRE2_ERROR_NOMEMORY;
        XString_assign_utf8(&data->m_errorString, "memory context creation failed");
        if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
        return;
    }

    static const uint16_t emptyPattern[] = { 0 };
    const uint16_t* pattern = XString_utf16(&data->m_pattern);
    size_t patternLength = XString_size_base(&data->m_pattern);
    if (!pattern) pattern = emptyPattern;

    int options = PCRE2_UTF;
    if (data->m_patternOptions & XRegularExpression_CaseInsensitiveOption)
        options |= PCRE2_CASELESS;
    if (data->m_patternOptions & XRegularExpression_DotMatchesEverythingOption)
        options |= PCRE2_DOTALL;
    if (data->m_patternOptions & XRegularExpression_MultilineOption)
        options |= PCRE2_MULTILINE;
    if (data->m_patternOptions & XRegularExpression_ExtendedPatternSyntaxOption)
        options |= PCRE2_EXTENDED;
    if (data->m_patternOptions & XRegularExpression_InvertedGreedinessOption)
        options |= PCRE2_UNGREEDY;
    if (data->m_patternOptions & XRegularExpression_DontCaptureOption)
        options |= PCRE2_NO_AUTO_CAPTURE;
    if (data->m_patternOptions & XRegularExpression_UseUnicodePropertiesOption)
        options |= PCRE2_UCP;

    pcre2_compile_context* compileContext =
            pcre2_compile_context_create(data->m_generalContext);
    if (!compileContext) {
        data->m_errorCode = PCRE2_ERROR_NOMEMORY;
        XString_assign_utf8(&data->m_errorString, "compile context creation failed");
        if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
        return;
    }

    PCRE2_SIZE errorOffset = 0;
    data->m_code = pcre2_compile((PCRE2_SPTR)pattern, (PCRE2_SIZE)patternLength,
                                 options, &data->m_errorCode, &errorOffset, compileContext);
    pcre2_compile_context_free(compileContext);

    if (!data->m_code) {
        data->m_errorOffset = (int64_t)errorOffset;
        uint16_t message[512];
        int messageLength = pcre2_get_error_message(data->m_errorCode, message,
                                                     sizeof(message) / sizeof(message[0]));
        if (messageLength >= 0)
            XRegularExpression_assign_utf16(&data->m_errorString, message, (size_t)messageLength);
        else
            XString_assign_utf8(&data->m_errorString, "PCRE2 pattern compilation failed");
        if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
        return;
    }

    data->m_errorCode = 0;
    data->m_errorOffset = -1;
    (void)pcre2_jit_compile(data->m_code,
                            PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT | PCRE2_JIT_PARTIAL_HARD);
    (void)pcre2_pattern_info(data->m_code, PCRE2_INFO_CAPTURECOUNT, &data->m_captureCount);

    unsigned int newline = 0;
    if (pcre2_pattern_info(data->m_code, PCRE2_INFO_NEWLINE, &newline) != 0)
        (void)pcre2_config(PCRE2_CONFIG_NEWLINE, &newline);
    data->m_usingCrLfNewlines = newline == PCRE2_NEWLINE_CRLF ||
            newline == PCRE2_NEWLINE_ANY || newline == PCRE2_NEWLINE_ANYCRLF;
    if (data->m_compileMutex) XMutex_unlock(data->m_compileMutex);
}

static int XRegularExpression_captureIndexForName(const XRegularExpressionData* data,
                                                  const XAnyStringView* name)
{
    if (!data || !name || XAnyStringView_empty(name)) return -1;
    XRegularExpression_compilePattern((XRegularExpressionData*)data);
    if (!data->m_code) return -1;

    PCRE2_SPTR* table = NULL;
    uint32_t count = 0;
    uint32_t entrySize = 0;
    (void)pcre2_pattern_info(data->m_code, PCRE2_INFO_NAMETABLE, &table);
    (void)pcre2_pattern_info(data->m_code, PCRE2_INFO_NAMECOUNT, &count);
    (void)pcre2_pattern_info(data->m_code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize);

    XString* nameString = NULL;
    if (!XAnyStringView_isUtf16(name)) {
        if (XAnyStringView_isLatin1(name))
            nameString = XString_create_with_length_latin1((const uint8_t*)name->m_data_latin1,
                                                            (size_t)XAnyStringView_size(name));
        else
            nameString = XString_create_with_length_utf8(name->m_data_utf8,
                                                          (size_t)XAnyStringView_size(name));
    }

    const XChar* utf16Name = XAnyStringView_isUtf16(name) ? name->m_data_utf16 : NULL;
    size_t utf16Length = XAnyStringView_isUtf16(name) ? (size_t)XAnyStringView_size(name) : 0;
    int result = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint16_t* row = (const uint16_t*)table + (size_t)entrySize * i;
        const uint16_t* currentName = row + 1;
        size_t currentLength = 0;
        while (currentName[currentLength] != 0) ++currentLength;
        bool equal = false;
        if (utf16Name) {
            equal = utf16Length == currentLength &&
                    memcmp(utf16Name, currentName, currentLength * sizeof(uint16_t)) == 0;
        } else if (nameString) {
            XString* current = XString_create_with_length_utf16(currentName, currentLength);
            equal = current && XString_equals(current, nameString, XChar_CaseSensitive);
            if (current) XString_delete_base(current);
        }
        if (equal) {
            result = (int)row[0];
            break;
        }
    }
    if (nameString) XString_delete_base(nameString);
    return result;
}

static bool XRegularExpression_setSubject(XString* subject, const uint16_t* data, size_t length)
{
    if (!subject) return false;
    if (!data || length == 0) {
        XString_clear_base(subject);
        return true;
    }
    return XRegularExpression_assign_utf16(subject, data, length);
}

static pcre2_jit_stack* XRegularExpression_jitStackCallback(void* data)
{
    return (pcre2_jit_stack*)data;
}

static void XRegularExpression_prepareOffsets(XRegularExpressionMatch* match,
                                               pcre2_match_data* matchData,
                                               int result)
{
    if (!match || !matchData) return;
    if (result > 0) {
        match->m_capturedCount = (size_t)result;
    } else if (result == PCRE2_ERROR_PARTIAL) {
        match->m_capturedCount = 1;
    } else {
        match->m_capturedCount = 0;
    }

    if (match->m_capturedCount == 0) return;
    size_t offsetCount = match->m_capturedCount * 2;
    match->m_capturedOffsets = (int64_t*)XMalloc_System(offsetCount * sizeof(int64_t));
    if (!match->m_capturedOffsets) {
        match->m_capturedCount = 0;
        return;
    }

    PCRE2_SIZE* offsets = pcre2_get_ovector_pointer(matchData);
    for (size_t i = 0; i < offsetCount; ++i) {
        if (offsets[i] == PCRE2_UNSET || offsets[i] > (PCRE2_SIZE)INT64_MAX)
            match->m_capturedOffsets[i] = -1;
        else
            match->m_capturedOffsets[i] = (int64_t)offsets[i];
    }
}

static XRegularExpressionMatch* XRegularExpression_matchInternal(const XRegularExpression* expression,
                                                                  const uint16_t* subject,
                                                                  size_t subjectLength,
                                                                  int64_t offset,
                                                                  XRegularExpression_MatchType matchType,
                                                                  XRegularExpression_MatchOptions matchOptions,
                                                                  const XRegularExpressionMatch* previous)
{
    XRegularExpressionMatch* match = XRegularExpressionMatch_create();
    if (!match) return NULL;
    if (expression) XRegularExpression_copy_base(&match->m_regularExpression, expression);
    if (!XRegularExpression_setSubject(&match->m_subject, subject, subjectLength)) {
        match->m_isValid = false;
        return match;
    }
    match->m_matchType = matchType;
    match->m_matchOptions = matchOptions;

    XRegularExpressionData* data = expression ? expression->m_data : NULL;
    if (!data) {
        match->m_isValid = false;
        return match;
    }
    XRegularExpression_compilePattern(data);
    if (!data->m_code) {
        match->m_isValid = false;
        return match;
    }
    if (offset < 0) offset += (int64_t)subjectLength;
    if (offset < 0 || (uint64_t)offset > (uint64_t)subjectLength) {
        match->m_isValid = false;
        return match;
    }
    match->m_isValid = true;
    if (matchType == XRegularExpression_NoMatch) return match;

    int options = 0;
    if (matchOptions & XRegularExpression_AnchorAtOffsetMatchOption)
        options |= PCRE2_ANCHORED;
    if (matchOptions & XRegularExpression_DontCheckSubjectStringMatchOption)
        options |= PCRE2_NO_UTF_CHECK;
    if (matchType == XRegularExpression_PartialPreferCompleteMatch)
        options |= PCRE2_PARTIAL_SOFT;
    else if (matchType == XRegularExpression_PartialPreferFirstMatch)
        options |= PCRE2_PARTIAL_HARD;

    static const uint16_t emptySubject[] = { 0 };
    const uint16_t* subjectData = subjectLength ? XString_utf16(&match->m_subject) : emptySubject;
    pcre2_match_context* matchContext = pcre2_match_context_create(data->m_generalContext);
    pcre2_match_data* matchData = pcre2_match_data_create_from_pattern(data->m_code,
                                                                        data->m_generalContext);
    pcre2_jit_stack* jitStack = NULL;
    if (matchContext)
        jitStack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, data->m_generalContext);
    if (matchContext && jitStack)
        pcre2_jit_stack_assign(matchContext, XRegularExpression_jitStackCallback, jitStack);

    if (!matchContext || !matchData) {
        if (jitStack) pcre2_jit_stack_free(jitStack);
        if (matchData) pcre2_match_data_free(matchData);
        if (matchContext) pcre2_match_context_free(matchContext);
        match->m_isValid = false;
        return match;
    }

    bool previousMatchWasEmpty = previous && previous->m_hasMatch &&
            previous->m_capturedCount > 0 && previous->m_capturedOffsets &&
            previous->m_capturedOffsets[0] == previous->m_capturedOffsets[1];
    int result;
    if (previousMatchWasEmpty) {
        result = pcre2_match(data->m_code, (PCRE2_SPTR)subjectData,
                             (PCRE2_SIZE)subjectLength, (PCRE2_SIZE)offset,
                             options | PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED,
                             matchData, matchContext);
        if (result == PCRE2_ERROR_NOMATCH) {
            ++offset;
            if (offset < (int64_t)subjectLength &&
                    XChar_isLowSurrogate(subjectData[offset])) {
                ++offset;
            } else if (data->m_usingCrLfNewlines && offset < (int64_t)subjectLength &&
                       offset > 0 && subjectData[offset - 1] == '\r' &&
                       subjectData[offset] == '\n') {
                ++offset;
            }
            if (offset > (int64_t)subjectLength) {
                result = PCRE2_ERROR_NOMATCH;
            } else {
                result = pcre2_match(data->m_code, (PCRE2_SPTR)subjectData,
                                     (PCRE2_SIZE)subjectLength, (PCRE2_SIZE)offset,
                                     options, matchData, matchContext);
            }
        }
    } else {
        result = pcre2_match(data->m_code, (PCRE2_SPTR)subjectData,
                             (PCRE2_SIZE)subjectLength, (PCRE2_SIZE)offset,
                             options, matchData, matchContext);
    }
    match->m_hasMatch = result > 0;
    match->m_hasPartialMatch = result == PCRE2_ERROR_PARTIAL;
    if (result == PCRE2_ERROR_NOMATCH || result == PCRE2_ERROR_PARTIAL || result > 0)
        XRegularExpression_prepareOffsets(match, matchData, result);
    else
        match->m_isValid = false;

    if (result == PCRE2_ERROR_PARTIAL && match->m_capturedCount > 0 &&
            match->m_capturedOffsets && match->m_capturedOffsets[0] >= 0) {
        uint32_t maximumLookBehind = 0;
        if (pcre2_pattern_info(data->m_code, PCRE2_INFO_MAXLOOKBEHIND,
                               &maximumLookBehind) == 0)
            match->m_capturedOffsets[0] -= (int64_t)maximumLookBehind;
    }

    if (jitStack) pcre2_jit_stack_free(jitStack);
    pcre2_match_data_free(matchData);
    pcre2_match_context_free(matchContext);
    return match;
}

static bool XRegularExpression_append_literal(XString* result, const char* literal)
{
    return XString_append_utf8(result, literal);
}

static XString* XRegularExpression_wildcardInternal(const XStringView* pattern,
                                                    XRegularExpression_WildcardConversionOptions options)
{
    XString* result = XString_create();
    if (!result) return NULL;
    if (!pattern || !XStringView_data(pattern)) {
        if (!(options & XRegularExpression_UnanchoredWildcardConversion)) {
            XString* anchored = XRegularExpression_anchoredPattern_2(result);
            XString_delete_base(result);
            return anchored;
        }
        return result;
    }

    const XChar* data = XStringView_data(pattern);
    size_t length = (size_t)XStringView_size(pattern);
    bool nonPath = (options & XRegularExpression_NonPathWildcardConversion) != 0;
    for (size_t i = 0; i < length; ++i) {
        XChar c = data[i];
        if (c == '*') {
            if (nonPath) {
                XRegularExpression_append_literal(result, "[\\d\\D]*");
            } else {
#ifdef _WIN32
                XRegularExpression_append_literal(result, "[^/\\\\]*");
#else
                XRegularExpression_append_literal(result, "[^/]*");
#endif
            }
            while (i + 1 < length && data[i + 1] == '*') ++i;
        } else if (c == '?') {
            if (nonPath) {
                XRegularExpression_append_literal(result, "[\\d\\D]");
            } else {
#ifdef _WIN32
                XRegularExpression_append_literal(result, "[^/\\\\]");
#else
                XRegularExpression_append_literal(result, "[^/]");
#endif
            }
        } else if (c == '[') {
            XString_append_char(result, c);
            ++i;
            if (i < length && data[i] == '!') {
                XString_append_char(result, '^');
                ++i;
            }
            if (i < length && data[i] == ']') XString_append_char(result, data[i++]);
            while (i < length && data[i] != ']') {
                bool isPathSeparator = data[i] == '/';
#ifdef _WIN32
                isPathSeparator = isPathSeparator || data[i] == '\\';
#endif
                if (!nonPath && isPathSeparator) {
                    return result;
                }
                if (data[i] == '\\') XString_append_char(result, '\\');
                XString_append_char(result, data[i++]);
            }
            if (i < length) XString_append_char(result, data[i]);
        } else if (c == '\\') {
#ifdef _WIN32
            if (nonPath)
                XRegularExpression_append_literal(result, "\\\\");
            else
                XRegularExpression_append_literal(result, "[/\\\\]");
#else
            XString_append_char(result, '\\');
            XString_append_char(result, c);
#endif
        } else if (c == '/' && !nonPath) {
#ifdef _WIN32
            XRegularExpression_append_literal(result, "[/\\\\]");
#else
            XString_append_char(result, c);
#endif
        } else if (c == '$' || c == '(' || c == ')' || c == '+' || c == '.' ||
                   c == '^' || c == '{' || c == '|' || c == '}') {
            XString_append_char(result, '\\');
            XString_append_char(result, c);
        } else {
            XString_append_char(result, c);
        }
    }

    if (!(options & XRegularExpression_UnanchoredWildcardConversion)) {
        XString* anchored = XRegularExpression_anchoredPattern_2(result);
        XString_delete_base(result);
        return anchored;
    }
    return result;
}

/* ============================== 虚函数实现 ============================== */

static void VXRegularExpression_deinit(XRegularExpression* expression)
{
    if (!expression) return;
    XRegularExpression_data_release(expression->m_data);
    expression->m_data = NULL;
}

static void VXRegularExpression_copy(XRegularExpression* dest, const XRegularExpression* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpression_init(dest);
    XRegularExpression_data_release(dest->m_data);
    dest->m_data = src->m_data;
    XRegularExpression_data_addRef(dest->m_data);
}

static void VXRegularExpression_move(XRegularExpression* dest, XRegularExpression* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpression_init(dest);
    XRegularExpression_data_release(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXRegularExpressionMatch_deinit(XRegularExpressionMatch* match)
{
    if (!match) return;
    if (match->m_capturedOffsets) XFree_System(match->m_capturedOffsets);
    match->m_capturedOffsets = NULL;
    match->m_capturedCount = 0;
    XRegularExpression_deinit_base(&match->m_regularExpression);
    XString_deinit_base(&match->m_subject);
}

static void VXRegularExpressionMatch_copy(XRegularExpressionMatch* dest,
                                           const XRegularExpressionMatch* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionMatch_init(dest);
    XRegularExpression_copy_base(&dest->m_regularExpression, &src->m_regularExpression);
    XString_copy_base(&dest->m_subject, &src->m_subject);
    if (dest->m_capturedOffsets) XFree_System(dest->m_capturedOffsets);
    dest->m_capturedOffsets = NULL;
    dest->m_capturedCount = src->m_capturedCount;
    if (src->m_capturedCount) {
        size_t bytes = src->m_capturedCount * 2 * sizeof(int64_t);
        dest->m_capturedOffsets = (int64_t*)XMalloc_System(bytes);
        if (dest->m_capturedOffsets)
            memcpy(dest->m_capturedOffsets, src->m_capturedOffsets, bytes);
        else
            dest->m_capturedCount = 0;
    }
    dest->m_matchType = src->m_matchType;
    dest->m_matchOptions = src->m_matchOptions;
    dest->m_hasMatch = src->m_hasMatch;
    dest->m_hasPartialMatch = src->m_hasPartialMatch;
    dest->m_isValid = src->m_isValid;
}

static void VXRegularExpressionMatch_move(XRegularExpressionMatch* dest,
                                           XRegularExpressionMatch* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionMatch_init(dest);
    XRegularExpression_move_base(&dest->m_regularExpression, &src->m_regularExpression);
    XString_move_base(&dest->m_subject, &src->m_subject);
    if (dest->m_capturedOffsets) XFree_System(dest->m_capturedOffsets);
    dest->m_capturedOffsets = src->m_capturedOffsets;
    dest->m_capturedCount = src->m_capturedCount;
    dest->m_matchType = src->m_matchType;
    dest->m_matchOptions = src->m_matchOptions;
    dest->m_hasMatch = src->m_hasMatch;
    dest->m_hasPartialMatch = src->m_hasPartialMatch;
    dest->m_isValid = src->m_isValid;
    src->m_capturedOffsets = NULL;
    src->m_capturedCount = 0;
    src->m_hasMatch = false;
    src->m_hasPartialMatch = false;
    src->m_isValid = false;
}

static void VXRegularExpressionMatchIterator_deinit(XRegularExpressionMatchIterator* iterator)
{
    if (!iterator) return;
    if (iterator->m_next) XRegularExpressionMatch_delete_base(iterator->m_next);
    iterator->m_next = NULL;
    XRegularExpression_deinit_base(&iterator->m_regularExpression);
    XString_deinit_base(&iterator->m_subject);
}

static void VXRegularExpressionMatchIterator_copy(XRegularExpressionMatchIterator* dest,
                                                  const XRegularExpressionMatchIterator* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionMatchIterator_init(dest);
    XRegularExpression_copy_base(&dest->m_regularExpression, &src->m_regularExpression);
    XString_copy_base(&dest->m_subject, &src->m_subject);
    if (dest->m_next) XRegularExpressionMatch_delete_base(dest->m_next);
    dest->m_next = src->m_next ? XRegularExpressionMatch_create_copy(src->m_next) : NULL;
    dest->m_nextOffset = src->m_nextOffset;
    dest->m_matchType = src->m_matchType;
    dest->m_matchOptions = src->m_matchOptions;
    dest->m_isValid = src->m_isValid;
}

static void VXRegularExpressionMatchIterator_move(XRegularExpressionMatchIterator* dest,
                                                  XRegularExpressionMatchIterator* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionMatchIterator_init(dest);
    XRegularExpression_move_base(&dest->m_regularExpression, &src->m_regularExpression);
    XString_move_base(&dest->m_subject, &src->m_subject);
    if (dest->m_next) XRegularExpressionMatch_delete_base(dest->m_next);
    dest->m_next = src->m_next;
    dest->m_nextOffset = src->m_nextOffset;
    dest->m_matchType = src->m_matchType;
    dest->m_matchOptions = src->m_matchOptions;
    dest->m_isValid = src->m_isValid;
    src->m_next = NULL;
    src->m_nextOffset = 0;
    src->m_matchType = XRegularExpression_NoMatch;
    src->m_matchOptions = XRegularExpression_NoMatchOption;
    src->m_isValid = false;
}

/* ============================== 虚函数表 ============================== */

XVtable* XRegularExpression_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRegularExpression))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRegularExpression_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXRegularExpression_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXRegularExpression_move);
    return XVTABLE_DEFAULT;
}

XVtable* XRegularExpressionMatch_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRegularExpressionMatch))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRegularExpressionMatch_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXRegularExpressionMatch_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXRegularExpressionMatch_move);
    return XVTABLE_DEFAULT;
}

XVtable* XRegularExpressionMatchIterator_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRegularExpressionMatchIterator))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRegularExpressionMatchIterator_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXRegularExpressionMatchIterator_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXRegularExpressionMatchIterator_move);
    return XVTABLE_DEFAULT;
}

/* ============================== 构造函数 ============================== */

void XRegularExpression_init(XRegularExpression* expression)
{
    if (!expression) return;
    memset(expression, 0, sizeof(*expression));
    XClass_init(expression);
    XClassSetVtable(expression, XRegularExpression);
    expression->m_data = XRegularExpression_data_create();
}

void XRegularExpressionMatch_init(XRegularExpressionMatch* match)
{
    if (!match) return;
    memset(match, 0, sizeof(*match));
    XClass_init(match);
    XClassSetVtable(match, XRegularExpressionMatch);
    XRegularExpression_init(&match->m_regularExpression);
    XString_init(&match->m_subject);
    match->m_matchType = XRegularExpression_NoMatch;
    match->m_isValid = true;
}

void XRegularExpressionMatchIterator_init(XRegularExpressionMatchIterator* iterator)
{
    if (!iterator) return;
    memset(iterator, 0, sizeof(*iterator));
    XClass_init(iterator);
    XClassSetVtable(iterator, XRegularExpressionMatchIterator);
    XRegularExpression_init(&iterator->m_regularExpression);
    XString_init(&iterator->m_subject);
    iterator->m_next = XRegularExpressionMatch_create();
    iterator->m_matchType = XRegularExpression_NoMatch;
    iterator->m_matchOptions = XRegularExpression_NoMatchOption;
    iterator->m_isValid = iterator->m_next != NULL;
}

XRegularExpression* XRegularExpression_create(void)
{
    XRegularExpression* expression = (XRegularExpression*)XMalloc_System(sizeof(*expression));
    if (!expression) return NULL;
    XRegularExpression_init(expression);
    Set_Class_MemoryFree(expression, XFree_System);
    return expression;
}

XRegularExpression* XRegularExpression_create_copy(const XRegularExpression* other)
{
    if (!other) return NULL;
    XRegularExpression* expression = XRegularExpression_create();
    if (!expression) return NULL;
    XRegularExpression_copy_base(expression, other);
    return expression;
}

XRegularExpression* XRegularExpression_create_move(XRegularExpression* other)
{
    if (!other) return NULL;
    XRegularExpression* expression = XRegularExpression_create();
    if (!expression) return NULL;
    XRegularExpression_move_base(expression, other);
    return expression;
}

XRegularExpression* XRegularExpression_create_utf8(const char* pattern,
                                                     XRegularExpression_PatternOptions options)
{
    XRegularExpression* expression = XRegularExpression_create();
    if (!expression) return NULL;
    XRegularExpression_setPattern_utf8(expression, pattern);
    XRegularExpression_setPatternOptions(expression, options);
    return expression;
}

XRegularExpressionMatch* XRegularExpressionMatch_create(void)
{
    XRegularExpressionMatch* match = (XRegularExpressionMatch*)XMalloc_System(sizeof(*match));
    if (!match) return NULL;
    XRegularExpressionMatch_init(match);
    Set_Class_MemoryFree(match, XFree_System);
    return match;
}

XRegularExpressionMatch* XRegularExpressionMatch_create_copy(const XRegularExpressionMatch* other)
{
    if (!other) return NULL;
    XRegularExpressionMatch* match = XRegularExpressionMatch_create();
    if (!match) return NULL;
    XRegularExpressionMatch_copy_base(match, other);
    return match;
}

XRegularExpressionMatch* XRegularExpressionMatch_create_move(XRegularExpressionMatch* other)
{
    if (!other) return NULL;
    XRegularExpressionMatch* match = XRegularExpressionMatch_create();
    if (!match) return NULL;
    XRegularExpressionMatch_move_base(match, other);
    return match;
}

XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create(void)
{
    XRegularExpressionMatchIterator* iterator =
            (XRegularExpressionMatchIterator*)XMalloc_System(sizeof(*iterator));
    if (!iterator) return NULL;
    XRegularExpressionMatchIterator_init(iterator);
    Set_Class_MemoryFree(iterator, XFree_System);
    return iterator;
}

XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create_copy(
        const XRegularExpressionMatchIterator* other)
{
    if (!other) return NULL;
    XRegularExpressionMatchIterator* iterator = XRegularExpressionMatchIterator_create();
    if (!iterator) return NULL;
    XRegularExpressionMatchIterator_copy_base(iterator, other);
    return iterator;
}

XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create_move(
        XRegularExpressionMatchIterator* other)
{
    if (!other) return NULL;
    XRegularExpressionMatchIterator* iterator = XRegularExpressionMatchIterator_create();
    if (!iterator) return NULL;
    XRegularExpressionMatchIterator_move_base(iterator, other);
    return iterator;
}

/* ============================== QRegularExpression API ============================== */

void XRegularExpression_setPattern(XRegularExpression* expression, const XString* pattern)
{
    if (!expression) return;
    if (!pattern) {
        if (expression->m_data && XString_isEmpty_base(&expression->m_data->m_pattern)) return;
        if (!XRegularExpression_detach(expression)) return;
        XString_clear_base(&expression->m_data->m_pattern);
        XRegularExpression_markDirty(expression->m_data);
        return;
    }
    if (expression->m_data &&
            XString_equals(&expression->m_data->m_pattern, pattern, XChar_CaseSensitive)) return;
    if (!XRegularExpression_detach(expression)) return;
    XString_assign(&expression->m_data->m_pattern, pattern);
    XRegularExpression_markDirty(expression->m_data);
}

void XRegularExpression_setPattern_utf8(XRegularExpression* expression, const char* pattern)
{
    if (!expression) return;
    const char* value = pattern ? pattern : "";
    if (expression->m_data &&
            XString_equals_utf8(&expression->m_data->m_pattern, value,
                                XChar_CaseSensitive)) return;
    if (!XRegularExpression_detach(expression)) return;
    XString_assign_utf8(&expression->m_data->m_pattern, value);
    XRegularExpression_markDirty(expression->m_data);
}

XString* XRegularExpression_pattern(const XRegularExpression* expression)
{
    if (!expression || !expression->m_data) return NULL;
    return XString_create_copy(&expression->m_data->m_pattern);
}

const XString* XRegularExpression_pattern_const(const XRegularExpression* expression)
{
    return expression && expression->m_data ? &expression->m_data->m_pattern : NULL;
}

XRegularExpression_PatternOptions XRegularExpression_patternOptions(const XRegularExpression* expression)
{
    return expression && expression->m_data ? expression->m_data->m_patternOptions : 0;
}

void XRegularExpression_setPatternOptions(XRegularExpression* expression,
                                          XRegularExpression_PatternOptions options)
{
    if (!expression) return;
    if (expression->m_data && expression->m_data->m_patternOptions == options) return;
    if (!XRegularExpression_detach(expression)) return;
    expression->m_data->m_patternOptions = options;
    XRegularExpression_markDirty(expression->m_data);
}

bool XRegularExpression_isValid(const XRegularExpression* expression)
{
    if (!expression || !expression->m_data) return false;
    XRegularExpression_compilePattern(expression->m_data);
    return expression->m_data->m_code != NULL;
}

int64_t XRegularExpression_patternErrorOffset(const XRegularExpression* expression)
{
    if (!expression || !expression->m_data) return -1;
    XRegularExpression_compilePattern(expression->m_data);
    return expression->m_data->m_errorOffset;
}

const XString* XRegularExpression_errorString_const(const XRegularExpression* expression)
{
    if (!expression || !expression->m_data) return NULL;
    XRegularExpression_compilePattern(expression->m_data);
    return &expression->m_data->m_errorString;
}

XString* XRegularExpression_errorString(const XRegularExpression* expression)
{
    const XString* error = XRegularExpression_errorString_const(expression);
    return error ? XString_create_copy(error) : NULL;
}

int XRegularExpression_captureCount(const XRegularExpression* expression)
{
    if (!XRegularExpression_isValid(expression)) return -1;
    return expression->m_data->m_captureCount;
}

XStringList* XRegularExpression_namedCaptureGroups(const XRegularExpression* expression)
{
    XStringList* result = XStringList_create();
    if (!result || !XRegularExpression_isValid(expression)) return result;

    int count = expression->m_data->m_captureCount + 1;
    for (int i = 0; i < count; ++i) XStringList_push_back_utf8(result, "");

    PCRE2_SPTR* table = NULL;
    uint32_t nameCount = 0;
    uint32_t entrySize = 0;
    (void)pcre2_pattern_info(expression->m_data->m_code, PCRE2_INFO_NAMETABLE, &table);
    (void)pcre2_pattern_info(expression->m_data->m_code, PCRE2_INFO_NAMECOUNT, &nameCount);
    (void)pcre2_pattern_info(expression->m_data->m_code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize);
    for (uint32_t i = 0; i < nameCount; ++i) {
        const uint16_t* row = (const uint16_t*)table + (size_t)entrySize * i;
        int index = (int)row[0];
        if (index >= 0 && index < count) {
            size_t length = 0;
            while (row[1 + length] != 0) ++length;
            XString* name = XString_create_with_length_utf16(row + 1, length);
            XString* destination = (XString*)XStringList_at_base(result, index);
            if (name && destination) {
                XString_assign(destination, name);
            }
            if (name) XString_delete_base(name);
        }
    }
    return result;
}

void XRegularExpression_optimize(const XRegularExpression* expression)
{
    if (expression && expression->m_data) XRegularExpression_compilePattern(expression->m_data);
}

bool XRegularExpression_equals(const XRegularExpression* left, const XRegularExpression* right)
{
    if (left == right) return true;
    if (!left || !right || !left->m_data || !right->m_data) return false;
    return left->m_data->m_patternOptions == right->m_data->m_patternOptions &&
           XString_equals(&left->m_data->m_pattern, &right->m_data->m_pattern,
                          XChar_CaseSensitive);
}

void XRegularExpression_swap(XRegularExpression* left, XRegularExpression* right)
{
    if (!left || !right || left == right) return;
    XRegularExpressionData* data = left->m_data;
    left->m_data = right->m_data;
    right->m_data = data;
}

uint64_t XRegularExpression_hash(const void* key, size_t len)
{
    (void)len;
    const XRegularExpression* expression = (const XRegularExpression*)key;
    if (!expression || !expression->m_data) return 0;

    static const uint8_t emptyData[] = { 0 };
    const XString* pattern = &expression->m_data->m_pattern;
    size_t patternLength = XString_size_base(pattern);
    const uint16_t* patternData = patternLength ? XString_utf16(pattern) : NULL;
    uint64_t patternHash = XHash_xxhash64(patternData ? patternData : emptyData,
                                          patternLength * sizeof(uint16_t));
    uint32_t options = expression->m_data->m_patternOptions;
    uint64_t optionsHash = XHash_xxhash64(&options, sizeof(options));
    return patternHash ^ (optionsHash + UINT64_C(0x9E3779B97F4A7C15) +
                          (patternHash << 6) + (patternHash >> 2));
}

int32_t XRegularExpression_compare(const void* left, const void* right)
{
    const XRegularExpression* lhs = (const XRegularExpression*)left;
    const XRegularExpression* rhs = (const XRegularExpression*)right;
    if (lhs == rhs) return XCompare_Equality;
    if (!lhs) return XCompare_Less;
    if (!rhs) return XCompare_Greater;

    const XString* leftPattern = XRegularExpression_pattern_const(lhs);
    const XString* rightPattern = XRegularExpression_pattern_const(rhs);
    if (leftPattern != rightPattern) {
        if (!leftPattern) return XCompare_Less;
        if (!rightPattern) return XCompare_Greater;
        int32_t result = XString_compare(leftPattern, rightPattern);
        if (result != XCompare_Equality) return result < 0 ? XCompare_Less : XCompare_Greater;
    }

    XRegularExpression_PatternOptions leftOptions = XRegularExpression_patternOptions(lhs);
    XRegularExpression_PatternOptions rightOptions = XRegularExpression_patternOptions(rhs);
    if (leftOptions < rightOptions) return XCompare_Less;
    if (leftOptions > rightOptions) return XCompare_Greater;
    return XCompare_Equality;
}

XRegularExpressionMatch* XRegularExpression_match(const XRegularExpression* expression,
                                                    const XString* subject,
                                                    int64_t offset,
                                                    XRegularExpression_MatchType matchType,
                                                    XRegularExpression_MatchOptions matchOptions)
{
    const uint16_t* data = subject ? XString_utf16(subject) : NULL;
    size_t length = subject ? XString_size_base(subject) : 0;
    return XRegularExpression_matchInternal(expression, data, length, offset, matchType,
                                            matchOptions, NULL);
}

XRegularExpressionMatch* XRegularExpression_matchView(const XRegularExpression* expression,
                                                        const XStringView* subjectView,
                                                        int64_t offset,
                                                        XRegularExpression_MatchType matchType,
                                                        XRegularExpression_MatchOptions matchOptions)
{
    const uint16_t* data = subjectView ? XStringView_utf16(subjectView) : NULL;
    size_t length = subjectView ? (size_t)XStringView_size(subjectView) : 0;
    return XRegularExpression_matchInternal(expression, data, length, offset, matchType,
                                            matchOptions, NULL);
}

XRegularExpressionMatch* XRegularExpression_match_utf8(const XRegularExpression* expression,
                                                        const char* subject,
                                                        int64_t offset,
                                                        XRegularExpression_MatchType matchType,
                                                        XRegularExpression_MatchOptions matchOptions)
{
    XString* value = XString_create_utf8(subject ? subject : "");
    if (!value) return NULL;
    XRegularExpressionMatch* result = XRegularExpression_match(expression, value, offset,
                                                                 matchType, matchOptions);
    XString_delete_base(value);
    return result;
}

XString* XRegularExpression_escape(const XStringView* string)
{
    XString* result = XString_create();
    if (!result || !string) return result;
    const XChar* data = XStringView_data(string);
    size_t length = (size_t)XStringView_size(string);
    for (size_t i = 0; i < length; ++i) {
        XChar current = data[i];
        if (current == 0) {
            XRegularExpression_append_literal(result, "\\0");
        } else if (!((current >= 'a' && current <= 'z') ||
                     (current >= 'A' && current <= 'Z') ||
                     (current >= '0' && current <= '9') || current == '_')) {
            XString_append_char(result, '\\');
            XString_append_char(result, current);
            if (current >= 0xD800 && current <= 0xDBFF && i + 1 < length)
                XString_append_char(result, data[++i]);
        } else {
            XString_append_char(result, current);
        }
    }
    return result;
}

XString* XRegularExpression_escape_2(const XString* string)
{
    XStringView view = XStringView_create_string(string);
    return XRegularExpression_escape(&view);
}

XString* XRegularExpression_wildcardToRegularExpression(const XStringView* pattern,
                                                         XRegularExpression_WildcardConversionOptions options)
{
    return XRegularExpression_wildcardInternal(pattern, options);
}

XString* XRegularExpression_wildcardToRegularExpression_2(const XString* pattern,
                                                           XRegularExpression_WildcardConversionOptions options)
{
    XStringView view = XStringView_create_string(pattern);
    return XRegularExpression_wildcardInternal(&view, options);
}

XRegularExpression* XRegularExpression_fromWildcard(const XStringView* pattern,
                                                     XChar_CaseSensitivity caseSensitivity,
                                                     XRegularExpression_WildcardConversionOptions options)
{
    XString* converted = XRegularExpression_wildcardInternal(pattern, options);
    if (!converted) return NULL;
    XRegularExpression* result = XRegularExpression_create();
    if (result) {
        XRegularExpression_setPattern(result, converted);
        if (caseSensitivity != XChar_CaseSensitive)
            XRegularExpression_setPatternOptions(result, XRegularExpression_CaseInsensitiveOption);
    }
    XString_delete_base(converted);
    return result;
}

XString* XRegularExpression_anchoredPattern(const XStringView* expression)
{
    XString* result = XString_create_utf8("\\A(?:");
    if (!result) return NULL;
    if (expression && XStringView_data(expression)) {
        XString* value = XString_create_with_length_utf16(XStringView_utf16(expression),
                                                          (size_t)XStringView_size(expression));
        if (value) {
            XString_append(result, value);
            XString_delete_base(value);
        }
    }
    XRegularExpression_append_literal(result, ")\\z");
    return result;
}

XString* XRegularExpression_anchoredPattern_2(const XString* expression)
{
    XStringView view = XStringView_create_string(expression);
    return XRegularExpression_anchoredPattern(&view);
}

/* ============================== QRegularExpressionMatch API ============================== */

XRegularExpression* XRegularExpressionMatch_regularExpression(const XRegularExpressionMatch* match)
{
    return match ? XRegularExpression_create_copy(&match->m_regularExpression) : NULL;
}

const XRegularExpression* XRegularExpressionMatch_regularExpression_const(const XRegularExpressionMatch* match)
{
    return match ? &match->m_regularExpression : NULL;
}

void XRegularExpressionMatch_swap(XRegularExpressionMatch* left,
                                   XRegularExpressionMatch* right)
{
    if (!left || !right || left == right) return;
    XRegularExpression_swap(&left->m_regularExpression, &right->m_regularExpression);
    XString_swap(&left->m_subject, &right->m_subject);
    int64_t* offsets = left->m_capturedOffsets;
    left->m_capturedOffsets = right->m_capturedOffsets;
    right->m_capturedOffsets = offsets;
    size_t capturedCount = left->m_capturedCount;
    left->m_capturedCount = right->m_capturedCount;
    right->m_capturedCount = capturedCount;
    XRegularExpression_MatchType matchType = left->m_matchType;
    left->m_matchType = right->m_matchType;
    right->m_matchType = matchType;
    XRegularExpression_MatchOptions matchOptions = left->m_matchOptions;
    left->m_matchOptions = right->m_matchOptions;
    right->m_matchOptions = matchOptions;
    bool value = left->m_hasMatch;
    left->m_hasMatch = right->m_hasMatch;
    right->m_hasMatch = value;
    value = left->m_hasPartialMatch;
    left->m_hasPartialMatch = right->m_hasPartialMatch;
    right->m_hasPartialMatch = value;
    value = left->m_isValid;
    left->m_isValid = right->m_isValid;
    right->m_isValid = value;
}

XRegularExpression_MatchType XRegularExpressionMatch_matchType(const XRegularExpressionMatch* match)
{
    return match ? match->m_matchType : XRegularExpression_NoMatch;
}

XRegularExpression_MatchOptions XRegularExpressionMatch_matchOptions(const XRegularExpressionMatch* match)
{
    return match ? match->m_matchOptions : XRegularExpression_NoMatchOption;
}

bool XRegularExpressionMatch_hasMatch(const XRegularExpressionMatch* match)
{
    return match ? match->m_hasMatch : false;
}

bool XRegularExpressionMatch_hasPartialMatch(const XRegularExpressionMatch* match)
{
    return match ? match->m_hasPartialMatch : false;
}

bool XRegularExpressionMatch_isValid(const XRegularExpressionMatch* match)
{
    return match ? match->m_isValid : false;
}

int XRegularExpressionMatch_lastCapturedIndex(const XRegularExpressionMatch* match)
{
    if (!match || match->m_capturedCount == 0) return -1;
    return (int)match->m_capturedCount - 1;
}

bool XRegularExpressionMatch_hasCaptured(const XRegularExpressionMatch* match, int nth)
{
    if (!match || nth < 0 || (size_t)nth >= match->m_capturedCount || !match->m_capturedOffsets)
        return false;
    return match->m_capturedOffsets[(size_t)nth * 2] >= 0;
}

bool XRegularExpressionMatch_hasCaptured_2(const XRegularExpressionMatch* match,
                                            const XAnyStringView* name)
{
    if (!match || !name) return false;
    int index = XRegularExpression_captureIndexForName(match->m_regularExpression.m_data, name);
    return XRegularExpressionMatch_hasCaptured(match, index);
}

XStringView XRegularExpressionMatch_capturedView(const XRegularExpressionMatch* match, int nth)
{
    if (!XRegularExpressionMatch_hasCaptured(match, nth)) return XStringView_create();
    int64_t start = match->m_capturedOffsets[(size_t)nth * 2];
    int64_t end = match->m_capturedOffsets[(size_t)nth * 2 + 1];
    if (start < 0 || end < start) return XStringView_create();
    const XChar* subject = XString_unicode(&match->m_subject);
    if (!subject) return XStringView_create();
    return XStringView_create_data(subject + start, end - start);
}

XStringView XRegularExpressionMatch_capturedView_2(const XRegularExpressionMatch* match,
                                                    const XAnyStringView* name)
{
    if (!match || !name) return XStringView_create();
    int index = XRegularExpression_captureIndexForName(match->m_regularExpression.m_data, name);
    return XRegularExpressionMatch_capturedView(match, index);
}

XString* XRegularExpressionMatch_captured(const XRegularExpressionMatch* match, int nth)
{
    XStringView view = XRegularExpressionMatch_capturedView(match, nth);
    if (!XStringView_data(&view) || XStringView_size(&view) == 0) return XString_create();
    return XString_create_with_length_utf16(XStringView_utf16(&view),
                                            (size_t)XStringView_size(&view));
}

XString* XRegularExpressionMatch_captured_2(const XRegularExpressionMatch* match,
                                             const XAnyStringView* name)
{
    XStringView view = XRegularExpressionMatch_capturedView_2(match, name);
    if (!XStringView_data(&view) || XStringView_size(&view) == 0) return XString_create();
    return XString_create_with_length_utf16(XStringView_utf16(&view),
                                            (size_t)XStringView_size(&view));
}

XStringList* XRegularExpressionMatch_capturedTexts(const XRegularExpressionMatch* match)
{
    XStringList* result = XStringList_create();
    if (!result || !match) return result;
    for (size_t i = 0; i < match->m_capturedCount; ++i) {
        XString* text = XRegularExpressionMatch_captured(match, (int)i);
        if (text) {
            XStringList_push_back_base(result, text);
            XString_deinit_base(text);
            XFree_System(text);
        }
    }
    return result;
}

int64_t XRegularExpressionMatch_capturedStart(const XRegularExpressionMatch* match, int nth)
{
    if (!XRegularExpressionMatch_hasCaptured(match, nth)) return -1;
    return match->m_capturedOffsets[(size_t)nth * 2];
}

int64_t XRegularExpressionMatch_capturedStart_2(const XRegularExpressionMatch* match,
                                                const XAnyStringView* name)
{
    if (!match || !name) return -1;
    return XRegularExpressionMatch_capturedStart(match,
                                                  XRegularExpression_captureIndexForName(match->m_regularExpression.m_data, name));
}

int64_t XRegularExpressionMatch_capturedEnd(const XRegularExpressionMatch* match, int nth)
{
    if (!XRegularExpressionMatch_hasCaptured(match, nth)) return -1;
    return match->m_capturedOffsets[(size_t)nth * 2 + 1];
}

int64_t XRegularExpressionMatch_capturedEnd_2(const XRegularExpressionMatch* match,
                                               const XAnyStringView* name)
{
    if (!match || !name) return -1;
    return XRegularExpressionMatch_capturedEnd(match,
                                                XRegularExpression_captureIndexForName(match->m_regularExpression.m_data, name));
}

int64_t XRegularExpressionMatch_capturedLength(const XRegularExpressionMatch* match, int nth)
{
    int64_t start = XRegularExpressionMatch_capturedStart(match, nth);
    int64_t end = XRegularExpressionMatch_capturedEnd(match, nth);
    return start < 0 || end < start ? 0 : end - start;
}

int64_t XRegularExpressionMatch_capturedLength_2(const XRegularExpressionMatch* match,
                                                 const XAnyStringView* name)
{
    int64_t start = XRegularExpressionMatch_capturedStart_2(match, name);
    int64_t end = XRegularExpressionMatch_capturedEnd_2(match, name);
    return start < 0 || end < start ? 0 : end - start;
}

/* ============================== QRegularExpressionMatchIterator API ============================== */

static int64_t XRegularExpression_nextOffset(const XRegularExpressionMatchIterator* iterator)
{
    if (!iterator || !iterator->m_next) return 0;
    int64_t start = XRegularExpressionMatch_capturedStart(iterator->m_next, 0);
    int64_t end = XRegularExpressionMatch_capturedEnd(iterator->m_next, 0);
    if (start < 0 || end < 0) return iterator->m_nextOffset;
    return end;
}

bool XRegularExpressionMatchIterator_isValid(const XRegularExpressionMatchIterator* iterator)
{
    return iterator ? iterator->m_isValid : false;
}

bool XRegularExpressionMatchIterator_hasNext(const XRegularExpressionMatchIterator* iterator)
{
    return iterator && iterator->m_next &&
           (XRegularExpressionMatch_hasMatch(iterator->m_next) ||
            XRegularExpressionMatch_hasPartialMatch(iterator->m_next));
}

XRegularExpressionMatch* XRegularExpressionMatchIterator_next(XRegularExpressionMatchIterator* iterator)
{
    if (!XRegularExpressionMatchIterator_hasNext(iterator)) return NULL;
    XRegularExpressionMatch* result = XRegularExpressionMatch_create_copy(iterator->m_next);
    iterator->m_nextOffset = XRegularExpression_nextOffset(iterator);
    XRegularExpressionMatch* next = XRegularExpression_matchInternal(
            &iterator->m_regularExpression, XString_utf16(&iterator->m_subject),
            XString_size_base(&iterator->m_subject), iterator->m_nextOffset,
            XRegularExpressionMatch_matchType(iterator->m_next),
            XRegularExpressionMatch_matchOptions(iterator->m_next), iterator->m_next);
    XRegularExpressionMatch_delete_base(iterator->m_next);
    iterator->m_next = next;
    return result;
}

XRegularExpressionMatch* XRegularExpressionMatchIterator_peekNext(const XRegularExpressionMatchIterator* iterator)
{
    return XRegularExpressionMatchIterator_hasNext(iterator) ?
            XRegularExpressionMatch_create_copy(iterator->m_next) : NULL;
}

XRegularExpression* XRegularExpressionMatchIterator_regularExpression(
        const XRegularExpressionMatchIterator* iterator)
{
    return iterator ? XRegularExpression_create_copy(&iterator->m_regularExpression) : NULL;
}

const XRegularExpression* XRegularExpressionMatchIterator_regularExpression_const(
        const XRegularExpressionMatchIterator* iterator)
{
    return iterator ? &iterator->m_regularExpression : NULL;
}

void XRegularExpressionMatchIterator_swap(XRegularExpressionMatchIterator* left,
                                          XRegularExpressionMatchIterator* right)
{
    if (!left || !right || left == right) return;
    XRegularExpression_swap(&left->m_regularExpression, &right->m_regularExpression);
    XString_swap(&left->m_subject, &right->m_subject);
    XRegularExpressionMatch* next = left->m_next;
    left->m_next = right->m_next;
    right->m_next = next;
    int64_t nextOffset = left->m_nextOffset;
    left->m_nextOffset = right->m_nextOffset;
    right->m_nextOffset = nextOffset;
    bool valid = left->m_isValid;
    left->m_isValid = right->m_isValid;
    right->m_isValid = valid;
    XRegularExpression_MatchType matchType = left->m_matchType;
    left->m_matchType = right->m_matchType;
    right->m_matchType = matchType;
    XRegularExpression_MatchOptions matchOptions = left->m_matchOptions;
    left->m_matchOptions = right->m_matchOptions;
    right->m_matchOptions = matchOptions;
}

XRegularExpression_MatchType XRegularExpressionMatchIterator_matchType(
        const XRegularExpressionMatchIterator* iterator)
{
    return iterator ? iterator->m_matchType : XRegularExpression_NoMatch;
}

XRegularExpression_MatchOptions XRegularExpressionMatchIterator_matchOptions(
        const XRegularExpressionMatchIterator* iterator)
{
    return iterator ? iterator->m_matchOptions : XRegularExpression_NoMatchOption;
}

/* ============================== 全局匹配构造 ============================== */

static XRegularExpressionMatchIterator* XRegularExpression_globalMatchInternal(
        const XRegularExpression* expression, const uint16_t* subject, size_t subjectLength,
        int64_t offset, XRegularExpression_MatchType matchType,
        XRegularExpression_MatchOptions matchOptions)
{
    XRegularExpressionMatchIterator* iterator = XRegularExpressionMatchIterator_create();
    if (!iterator) return NULL;
    if (expression) XRegularExpression_copy_base(&iterator->m_regularExpression, expression);
    XRegularExpression_setSubject(&iterator->m_subject, subject, subjectLength);
    iterator->m_nextOffset = offset;
    iterator->m_matchType = matchType;
    iterator->m_matchOptions = matchOptions;
    if (iterator->m_next) XRegularExpressionMatch_delete_base(iterator->m_next);
    iterator->m_next = XRegularExpression_match(&iterator->m_regularExpression,
                                                 &iterator->m_subject, offset,
                                                 matchType, matchOptions);
    iterator->m_isValid = iterator->m_next && XRegularExpressionMatch_isValid(iterator->m_next);
    return iterator;
}

XRegularExpressionMatchIterator* XRegularExpression_globalMatch(const XRegularExpression* expression,
                                                                  const XString* subject,
                                                                  int64_t offset,
                                                                  XRegularExpression_MatchType matchType,
                                                                  XRegularExpression_MatchOptions matchOptions)
{
    const uint16_t* data = subject ? XString_utf16(subject) : NULL;
    size_t length = subject ? XString_size_base(subject) : 0;
    return XRegularExpression_globalMatchInternal(expression, data, length, offset,
                                                   matchType, matchOptions);
}

XRegularExpressionMatchIterator* XRegularExpression_globalMatchView(const XRegularExpression* expression,
                                                                      const XStringView* subjectView,
                                                                      int64_t offset,
                                                                      XRegularExpression_MatchType matchType,
                                                                      XRegularExpression_MatchOptions matchOptions)
{
    const uint16_t* data = subjectView ? XStringView_utf16(subjectView) : NULL;
    size_t length = subjectView ? (size_t)XStringView_size(subjectView) : 0;
    return XRegularExpression_globalMatchInternal(expression, data, length, offset,
                                                   matchType, matchOptions);
}

XRegularExpressionMatchIterator* XRegularExpression_globalMatch_utf8(const XRegularExpression* expression,
                                                                      const char* subject,
                                                                      int64_t offset,
                                                                      XRegularExpression_MatchType matchType,
                                                                      XRegularExpression_MatchOptions matchOptions)
{
    XString* value = XString_create_utf8(subject ? subject : "");
    if (!value) return NULL;
    XRegularExpressionMatchIterator* result = XRegularExpression_globalMatch(expression, value, offset,
                                                                               matchType, matchOptions);
    XString_delete_base(value);
    return result;
}
