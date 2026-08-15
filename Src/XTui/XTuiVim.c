/**
 * @file       XTuiVim.c
 * @brief      XTui 全屏 vim 风格编辑器控件实现。
 * @details    文本缓冲和编辑状态完全在控件内维护；文件读写由
 *             XConsoleShellVi 通过公开缓冲接口完成。
 */

/* 先引入 Shell 配置，使 XConsoleShellConfig.h 在 EDITOR_TUI 关闭时
   能强制裁剪 XTuiVim 子功能，再定义控件结构体和实现。 */
#include "XConsoleShellConfig.h"
#include "XTuiVim.h"

#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON

#include "XTuiScreen.h"
#include "XTuiTerminal.h"
#include "XTuiTypes.h"
#if XRegularExpression_ON && \
    (XTUI_VIM_SEARCH_ON || XTUI_VIM_SUBSTITUTE_ON)
#include "XRegularExpression.h"
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xvim_record(XTuiVim* vim, const char* text);
static void xvim_goto(XTuiVim* vim, int line, int column);
static void xvim_linewise_op(XTuiVim* vim, char op, int count);
static bool xvim_delete_char(XTuiVim* vim, int line, int column);
#if XTUI_VIM_ADVANCED_MOTION_ON
static bool xvim_match_bracket(XTuiVim* vim);
#endif
#if XRegularExpression_ON && \
    (XTUI_VIM_SEARCH_ON || XTUI_VIM_SUBSTITUTE_ON)
static void xvim_delete_regular_expression(XRegularExpression* expression);
static void xvim_delete_regular_match(XRegularExpressionMatch* match);
static void xvim_delete_regular_iterator(XRegularExpressionMatchIterator* iterator);
#endif

/* ==================== UTF-8 与行工具 ==================== */

static int xvim_utf8_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

static int xvim_chars(const char* s)
{
    int n = 0;
    int len;
    if (!s) return 0;
    while (*s) {
        len = xvim_utf8_len((unsigned char)*s);
        s += len > 0 ? len : 1;
        ++n;
    }
    return n;
}

static int xvim_byte(const char* s, int column)
{
    int p = 0;
    int i = 0;
    if (!s || column <= 0) return 0;
    while (s[p] && i < column) {
        p += xvim_utf8_len((unsigned char)s[p]);
        ++i;
    }
    return p;
}

#if XRegularExpression_ON && \
    (XTUI_VIM_SEARCH_ON || XTUI_VIM_SUBSTITUTE_ON)
/* XRegularExpression 的 UTF-8 便捷接口仍以 UTF-16 code unit 表示偏移。 */
static int xvim_utf16_units(unsigned char c)
{
    return (c & 0xf8) == 0xf0 ? 2 : 1;
}

static int64_t xvim_utf16_offset(const char* s, int column)
{
    int p = 0;
    int i = 0;
    int64_t units = 0;
    if (!s || column <= 0) return 0;
    while (s[p] && i < column) {
        units += xvim_utf16_units((unsigned char)s[p]);
        p += xvim_utf8_len((unsigned char)s[p]);
        ++i;
    }
    return units;
}

static int xvim_column_from_utf16(const char* s, int64_t offset)
{
    int p = 0;
    int column = 0;
    int64_t units = 0;
    if (!s || offset <= 0) return 0;
    while (s[p] && units < offset) {
        units += xvim_utf16_units((unsigned char)s[p]);
        p += xvim_utf8_len((unsigned char)s[p]);
        ++column;
    }
    return column;
}

static int xvim_byte_from_utf16(const char* s, int64_t offset)
{
    int p = 0;
    int64_t units = 0;
    if (!s || offset <= 0) return 0;
    while (s[p] && units < offset) {
        units += xvim_utf16_units((unsigned char)s[p]);
        p += xvim_utf8_len((unsigned char)s[p]);
    }
    return p;
}
#endif

static int xvim_char_at(const char* s, int column)
{
    int p;
    if (!s || column < 0) return 0;
    p = xvim_byte(s, column);
    return s[p] ? (unsigned char)s[p] : 0;
}

static void xvim_free_lines(char** lines, int count)
{
    int i;
    if (!lines) return;
    for (i = 0; i < count; ++i)
        if (lines[i]) XFree_System(lines[i]);
    XFree_System(lines);
}

static char** xvim_clone_lines(char* const* src, int count, int* capacity)
{
    char** dst;
    int i;
    int cap = count > 0 ? count : 1;
    dst = (char**)XMalloc_System((size_t)cap * sizeof(char*));
    if (!dst) return NULL;
    memset(dst, 0, (size_t)cap * sizeof(char*));
    for (i = 0; i < count; ++i) {
        size_t len = strlen(src && src[i] ? src[i] : "");
        if (len >= XTUI_VIM_LINE_MAX) len = XTUI_VIM_LINE_MAX - 1u;
        dst[i] = (char*)XMalloc_System(len + 1u);
        if (!dst[i]) {
            xvim_free_lines(dst, i);
            return NULL;
        }
        memcpy(dst[i], src && src[i] ? src[i] : "", len);
        dst[i][len] = '\0';
    }
    if (capacity) *capacity = cap;
    return dst;
}

#if XTUI_VIM_UNDO_REDO_ON
static void xvim_free_snapshot(XVimSnapshot* snapshot)
{
    if (!snapshot) return;
    xvim_free_lines(snapshot->lines, snapshot->count);
    snapshot->lines = NULL;
    snapshot->count = 0;
    snapshot->capacity = 0;
}

static void xvim_clear_stack(XVimSnapshot* stack, int length)
{
    int i;
    if (!stack) return;
    for (i = 0; i < length; ++i) xvim_free_snapshot(&stack[i]);
    XFree_System(stack);
}
#endif

static bool xvim_ensure_lines(XTuiVim* vim, int needed)
{
    char** p;
    int cap;
    if (needed <= vim->m_linesCapacity) return true;
    cap = vim->m_linesCapacity ? vim->m_linesCapacity : 4;
    while (cap < needed) cap *= 2;
    p = (char**)XMalloc_System((size_t)cap * sizeof(char*));
    if (!p) return false;
    memset(p, 0, (size_t)cap * sizeof(char*));
    if (vim->m_lines && vim->m_lineCount)
        memcpy(p, vim->m_lines, (size_t)vim->m_lineCount * sizeof(char*));
    if (vim->m_lines) XFree_System(vim->m_lines);
    vim->m_lines = p;
    vim->m_linesCapacity = cap;
    return true;
}

static bool xvim_set_line_raw(XTuiVim* vim, int line, const char* text)
{
    char* copy;
    size_t len;
    if (!vim || line < 0 || line >= vim->m_lineCount) return false;
    text = text ? text : "";
    len = strlen(text);
    if (len >= XTUI_VIM_LINE_MAX) len = XTUI_VIM_LINE_MAX - 1u;
    copy = (char*)XMalloc_System(len + 1u);
    if (!copy) return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    if (vim->m_lines[line]) XFree_System(vim->m_lines[line]);
    vim->m_lines[line] = copy;
    return true;
}

static bool xvim_insert_line_raw(XTuiVim* vim, int line, const char* text)
{
    char* copy;
    int i;
    size_t len;
    if (!vim || line < 0 || line > vim->m_lineCount) return false;
    if (!xvim_ensure_lines(vim, vim->m_lineCount + 1)) return false;
    text = text ? text : "";
    len = strlen(text);
    if (len >= XTUI_VIM_LINE_MAX) len = XTUI_VIM_LINE_MAX - 1u;
    copy = (char*)XMalloc_System(len + 1u);
    if (!copy) return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    for (i = vim->m_lineCount; i > line; --i) vim->m_lines[i] = vim->m_lines[i - 1];
    vim->m_lines[line] = copy;
    ++vim->m_lineCount;
    return true;
}

static bool xvim_delete_line_raw(XTuiVim* vim, int line)
{
    int i;
    if (!vim || line < 0 || line >= vim->m_lineCount) return false;
    if (vim->m_lines[line]) XFree_System(vim->m_lines[line]);
    for (i = line; i + 1 < vim->m_lineCount; ++i) vim->m_lines[i] = vim->m_lines[i + 1];
    --vim->m_lineCount;
    vim->m_lines[vim->m_lineCount] = NULL;
    if (vim->m_lineCount == 0) return xvim_insert_line_raw(vim, 0, "");
    return true;
}

/* ==================== 撤销/重做与寄存器 ==================== */

#if XTUI_VIM_UNDO_REDO_ON
static bool xvim_push_stack(XVimSnapshot** stack, int* length, int* capacity,
                            char* const* lines, int count)
{
    XVimSnapshot* p;
    int cap;
    char** copy;
    if (!stack || !length || !capacity) return false;
    if (*length == *capacity) {
        cap = *capacity ? *capacity * 2 : 8;
        p = (XVimSnapshot*)XMalloc_System((size_t)cap * sizeof(XVimSnapshot));
        if (!p) return false;
        memset(p, 0, (size_t)cap * sizeof(XVimSnapshot));
        if (*stack && *length) memcpy(p, *stack, (size_t)*length * sizeof(XVimSnapshot));
        if (*stack) XFree_System(*stack);
        *stack = p;
        *capacity = cap;
    }
    copy = xvim_clone_lines(lines, count, NULL);
    if (!copy) return false;
    (*stack)[*length].lines = copy;
    (*stack)[*length].count = count;
    (*stack)[*length].capacity = count > 0 ? count : 1;
    ++*length;
    return true;
}

static void xvim_clear_redo(XTuiVim* vim)
{
    int i;
    if (!vim) return;
    for (i = 0; i < vim->m_redoLen; ++i) xvim_free_snapshot(&vim->m_redoStack[i]);
    vim->m_redoLen = 0;
}

static bool xvim_begin_change(XTuiVim* vim)
{
    if (!vim) return false;
    if (!xvim_push_stack(&vim->m_undoStack, &vim->m_undoLen, &vim->m_undoCap,
                         vim->m_lines, vim->m_lineCount)) return false;
    xvim_clear_redo(vim);
    return true;
}

static void xvim_restore(XTuiVim* vim, XVimSnapshot* snapshot)
{
    if (!vim || !snapshot || !snapshot->lines) return;
    xvim_free_lines(vim->m_lines, vim->m_lineCount);
    vim->m_lines = snapshot->lines;
    vim->m_lineCount = snapshot->count;
    vim->m_linesCapacity = snapshot->capacity;
    snapshot->lines = NULL;
    snapshot->count = 0;
    snapshot->capacity = 0;
    if (vim->m_cursorLine >= vim->m_lineCount) vim->m_cursorLine = vim->m_lineCount - 1;
    if (vim->m_cursorLine < 0) vim->m_cursorLine = 0;
    if (vim->m_cursorColumn > xvim_chars(vim->m_lines[vim->m_cursorLine]))
        vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    vim->m_modified = true;
}

static void xvim_undo(XTuiVim* vim)
{
    XVimSnapshot* top;
    if (!vim || vim->m_undoLen <= 0) return;
    if (!xvim_push_stack(&vim->m_redoStack, &vim->m_redoLen, &vim->m_redoCap,
                         vim->m_lines, vim->m_lineCount)) return;
    top = &vim->m_undoStack[vim->m_undoLen - 1];
    xvim_restore(vim, top);
    --vim->m_undoLen;
}

static void xvim_redo(XTuiVim* vim)
{
    XVimSnapshot* top;
    if (!vim || vim->m_redoLen <= 0) return;
    if (!xvim_push_stack(&vim->m_undoStack, &vim->m_undoLen, &vim->m_undoCap,
                         vim->m_lines, vim->m_lineCount)) return;
    top = &vim->m_redoStack[vim->m_redoLen - 1];
    xvim_restore(vim, top);
    --vim->m_redoLen;
}
#else
static bool xvim_begin_change(XTuiVim* vim)
{
    return vim != NULL;
}
#endif

#if XTUI_VIM_YANK_PASTE_ON
static void xvim_clear_register_value(XVimRegister* reg)
{
    if (!reg) return;
    xvim_free_lines(reg->lines, reg->count);
    reg->lines = NULL;
    reg->count = 0;
    reg->lineWise = false;
}

static bool xvim_assign_register_value(XVimRegister* reg, char* const* lines,
                                       int count, bool lineWise)
{
    int cap;
    char** copy;
    if (!reg || count <= 0) return false;
    copy = xvim_clone_lines(lines, count, &cap);
    if (!copy) return false;
    xvim_clear_register_value(reg);
    reg->lines = copy;
    reg->count = count;
    reg->lineWise = lineWise;
    (void)cap;
    return true;
}

static bool xvim_append_register_value(XVimRegister* reg, char* const* lines,
                                       int count, bool lineWise)
{
    char** combined;
    int total;
    int i;
    bool combinedLineWise;
    if (!reg || !lines || count <= 0) return false;
    if (!reg->lines || reg->count <= 0)
        return xvim_assign_register_value(reg, lines, count, lineWise);
    combinedLineWise = reg->lineWise || lineWise;
    total = reg->count + count;
    combined = (char**)XMalloc_System((size_t)total * sizeof(char*));
    if (!combined) return false;
    memset(combined, 0, (size_t)total * sizeof(char*));
    for (i = 0; i < total; ++i) {
        const char* text = i < reg->count ? reg->lines[i] : lines[i - reg->count];
        size_t length = strlen(text ? text : "");
        combined[i] = (char*)XMalloc_System(length + 1u);
        if (!combined[i]) {
            xvim_free_lines(combined, i);
            return false;
        }
        memcpy(combined[i], text ? text : "", length + 1u);
    }
    xvim_clear_register_value(reg);
    reg->lines = combined;
    reg->count = total;
    reg->lineWise = combinedLineWise;
    return true;
}

static void xvim_clear_unnamed_register(XTuiVim* vim)
{
    if (!vim) return;
    xvim_free_lines(vim->m_regLines, vim->m_regCount);
    vim->m_regLines = NULL;
    vim->m_regCount = 0;
    vim->m_regLineWise = false;
}

static void xvim_clear_register(XTuiVim* vim)
{
    if (!vim) return;
    xvim_clear_unnamed_register(vim);
#if XTUI_VIM_REGISTER_ON
    int i;
    for (i = 0; i < 26; ++i) xvim_clear_register_value(&vim->m_namedRegisters[i]);
    for (i = 0; i < 10; ++i) xvim_clear_register_value(&vim->m_numberedRegisters[i]);
    vim->m_activeRegister = '\0';
    vim->m_appendRegister = false;
#endif
}

static bool xvim_set_register(XTuiVim* vim, char* const* lines, int count, bool lineWise)
{
    int cap;
    char** copy;
    if (!vim || count <= 0) return false;
#if XTUI_VIM_REGISTER_ON
    if (vim->m_activeRegister == '_') {
        vim->m_activeRegister = '\0';
        vim->m_appendRegister = false;
        return false;
    }
#endif
    copy = xvim_clone_lines(lines, count, &cap);
    if (!copy) return false;
    xvim_clear_unnamed_register(vim);
    vim->m_regLines = copy;
    vim->m_regCount = count;
    vim->m_regLineWise = lineWise;
#if XTUI_VIM_REGISTER_ON
    if (vim->m_activeRegister >= 'a' && vim->m_activeRegister <= 'z')
        (void)(vim->m_appendRegister ?
            xvim_append_register_value(
                &vim->m_namedRegisters[vim->m_activeRegister - 'a'], lines, count, lineWise) :
            xvim_assign_register_value(
                &vim->m_namedRegisters[vim->m_activeRegister - 'a'], lines, count, lineWise));
    vim->m_activeRegister = '\0';
    vim->m_appendRegister = false;
#endif
    (void)cap;
    return true;
}

#if XTUI_VIM_REGISTER_ON
static void xvim_set_numbered_delete(XTuiVim* vim)
{
    int i;
    if (!vim || vim->m_regCount <= 0) return;
    for (i = 9; i > 1; --i) {
        xvim_clear_register_value(&vim->m_numberedRegisters[i]);
        vim->m_numberedRegisters[i] = vim->m_numberedRegisters[i - 1];
        memset(&vim->m_numberedRegisters[i - 1], 0, sizeof(XVimRegister));
    }
    (void)xvim_assign_register_value(&vim->m_numberedRegisters[1],
                                      vim->m_regLines, vim->m_regCount,
                                      vim->m_regLineWise);
}

static void xvim_set_numbered_yank(XTuiVim* vim)
{
    if (!vim || vim->m_regCount <= 0) return;
    (void)xvim_assign_register_value(&vim->m_numberedRegisters[0],
                                      vim->m_regLines, vim->m_regCount,
                                      vim->m_regLineWise);
}

static const XVimRegister* xvim_paste_register(XTuiVim* vim)
{
    const XVimRegister* reg = NULL;
    if (!vim) return NULL;
    if (vim->m_activeRegister >= 'a' && vim->m_activeRegister <= 'z')
        reg = &vim->m_namedRegisters[vim->m_activeRegister - 'a'];
    else if (vim->m_activeRegister >= '0' && vim->m_activeRegister <= '9')
        reg = &vim->m_numberedRegisters[vim->m_activeRegister - '0'];
    vim->m_activeRegister = '\0';
    vim->m_appendRegister = false;
    return reg;
}
#endif
#else
static void xvim_clear_register(XTuiVim* vim)
{
    (void)vim;
}

static bool xvim_set_register(XTuiVim* vim, char* const* lines, int count,
                              bool lineWise)
{
    (void)lines;
    (void)lineWise;
    return vim != NULL && count > 0;
}
#endif

/* ==================== 光标与文本变换 ==================== */

static void xvim_clamp(XTuiVim* vim)
{
    int max;
    if (!vim || vim->m_lineCount <= 0) return;
    if (vim->m_cursorLine < 0) vim->m_cursorLine = 0;
    if (vim->m_cursorLine >= vim->m_lineCount) vim->m_cursorLine = vim->m_lineCount - 1;
    max = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    if (vim->m_cursorColumn < 0) vim->m_cursorColumn = 0;
    if (vim->m_cursorColumn > max) vim->m_cursorColumn = max;
}

static void xvim_ensure_cursor_visible(XTuiVim* vim)
{
    XRect rect;
    int body;
    int maxTop;
    if (!vim) return;
    rect = XTuiWidget_rect((XTuiWidget*)vim);
    body = rect.height > 1 ? rect.height - 1 : 1;
    maxTop = vim->m_lineCount > body ? vim->m_lineCount - body : 0;
    if (vim->m_topLine < 0) vim->m_topLine = 0;
    if (vim->m_topLine > maxTop) vim->m_topLine = maxTop;
    if (vim->m_cursorLine < vim->m_topLine) vim->m_topLine = vim->m_cursorLine;
    if (vim->m_cursorLine >= vim->m_topLine + body)
        vim->m_topLine = vim->m_cursorLine - body + 1;
    if (vim->m_topLine > maxTop) vim->m_topLine = maxTop;
}

static void xvim_normalize_cursor(XTuiVim* vim)
{
    int length;
    if (!vim) return;
    xvim_clamp(vim);
    length = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    if (length > 0 && vim->m_cursorColumn >= length)
        vim->m_cursorColumn = length - 1;
}

static void xvim_page(XTuiVim* vim, int direction)
{
    XRect rect;
    int body;
    if (!vim) return;
    rect = XTuiWidget_rect((XTuiWidget*)vim);
    body = rect.height > 2 ? rect.height - 1 : 1;
    xvim_goto(vim, vim->m_cursorLine + direction * body, vim->m_cursorColumn);
    xvim_ensure_cursor_visible(vim);
}

static void xvim_half_page(XTuiVim* vim, int direction)
{
    XRect rect;
    int body;
    if (!vim) return;
    rect = XTuiWidget_rect((XTuiWidget*)vim);
    body = rect.height > 3 ? (rect.height - 1) / 2 : 1;
    xvim_goto(vim, vim->m_cursorLine + direction * body, vim->m_cursorColumn);
    xvim_ensure_cursor_visible(vim);
}

static void xvim_scroll_viewport(XTuiVim* vim, int direction)
{
    XRect rect;
    int body;
    int maxTop;
    if (!vim) return;
    rect = XTuiWidget_rect((XTuiWidget*)vim);
    body = rect.height > 1 ? rect.height - 1 : 1;
    maxTop = vim->m_lineCount > body ? vim->m_lineCount - body : 0;
    vim->m_topLine += direction;
    if (vim->m_topLine < 0) vim->m_topLine = 0;
    if (vim->m_topLine > maxTop) vim->m_topLine = maxTop;
    if (vim->m_cursorLine < vim->m_topLine) vim->m_cursorLine = vim->m_topLine;
    if (vim->m_cursorLine >= vim->m_topLine + body)
        vim->m_cursorLine = vim->m_topLine + body - 1;
    xvim_normalize_cursor(vim);
}

static int xvim_first_nonblank(const char* text)
{
    int i = 0;
    int n = xvim_chars(text);
    while (i < n) {
        int c = xvim_char_at(text, i);
        if (c != ' ' && c != '\t') break;
        ++i;
    }
    return i;
}

static void xvim_goto(XTuiVim* vim, int line, int column)
{
    if (!vim) return;
    vim->m_cursorLine = line;
    xvim_clamp(vim);
    if (column >= 0) {
        vim->m_cursorColumn = column;
        xvim_clamp(vim);
    }
}

#if XTUI_VIM_JUMPLIST_ON
static void xvim_jump_push(XTuiVim* vim)
{
    int index;
    if (!vim) return;
    if (vim->m_jumpIndex < vim->m_jumpLength)
        vim->m_jumpLength = vim->m_jumpIndex + 1;
    if (vim->m_jumpLength > 0) {
        index = vim->m_jumpLength - 1;
        if (vim->m_jumpLines[index] == vim->m_cursorLine &&
            vim->m_jumpColumns[index] == vim->m_cursorColumn) {
            vim->m_jumpIndex = vim->m_jumpLength;
            return;
        }
    }
    if (vim->m_jumpLength == XTUI_VIM_JUMPLIST_MAX) {
        memmove(vim->m_jumpLines, vim->m_jumpLines + 1,
                (XTUI_VIM_JUMPLIST_MAX - 1u) * sizeof(vim->m_jumpLines[0]));
        memmove(vim->m_jumpColumns, vim->m_jumpColumns + 1,
                (XTUI_VIM_JUMPLIST_MAX - 1u) * sizeof(vim->m_jumpColumns[0]));
        --vim->m_jumpLength;
    }
    index = vim->m_jumpLength++;
    vim->m_jumpLines[index] = vim->m_cursorLine;
    vim->m_jumpColumns[index] = vim->m_cursorColumn;
    vim->m_jumpIndex = vim->m_jumpLength;
}

static bool xvim_jump_navigate(XTuiVim* vim, bool newer)
{
    if (!vim) return false;
    if (!newer && vim->m_jumpIndex == vim->m_jumpLength) {
        xvim_jump_push(vim);
        vim->m_jumpIndex = vim->m_jumpLength - 1;
    }
    if (newer) {
        if (vim->m_jumpIndex + 1 >= vim->m_jumpLength) return false;
        ++vim->m_jumpIndex;
    } else {
        if (vim->m_jumpIndex <= 0) return false;
        --vim->m_jumpIndex;
    }
    xvim_goto(vim, vim->m_jumpLines[vim->m_jumpIndex],
              vim->m_jumpColumns[vim->m_jumpIndex]);
    xvim_ensure_cursor_visible(vim);
    return true;
}
#endif

#if XTUI_VIM_MARK_ON
static void xvim_set_mark(XTuiVim* vim, char name)
{
    int index = name - 'a';
    if (!vim || index < 0 || index >= 26) return;
    vim->m_markLines[index] = vim->m_cursorLine;
    vim->m_markColumns[index] = vim->m_cursorColumn;
}

static bool xvim_jump_mark(XTuiVim* vim, char name, bool lineWise)
{
    int index = name - 'a';
    if (!vim || index < 0 || index >= 26 || vim->m_markLines[index] < 0 ||
        vim->m_markLines[index] >= vim->m_lineCount) return false;
#if XTUI_VIM_JUMPLIST_ON
    xvim_jump_push(vim);
#endif
    vim->m_cursorLine = vim->m_markLines[index];
    vim->m_cursorColumn = lineWise ? xvim_first_nonblank(vim->m_lines[vim->m_cursorLine]) :
                                   vim->m_markColumns[index];
    xvim_normalize_cursor(vim);
    return true;
}
#endif

static bool xvim_replace_range(XTuiVim* vim, int line, int start, int end,
                               const char* replacement)
{
    const char* text;
    char* out;
    int a, b;
    size_t len, repl;
    if (!vim || line < 0 || line >= vim->m_lineCount || start > end) return false;
    text = vim->m_lines[line];
    a = xvim_byte(text, start);
    b = xvim_byte(text, end);
    if (b < a) return false;
    repl = strlen(replacement ? replacement : "");
    len = strlen(text) - (size_t)(b - a) + repl;
    if (len >= XTUI_VIM_LINE_MAX) return false;
    out = (char*)XMalloc_System(len + 1u);
    if (!out) return false;
    memcpy(out, text, (size_t)a);
    if (repl) memcpy(out + a, replacement, repl);
    memcpy(out + a + repl, text + b, strlen(text + b) + 1u);
    if (!xvim_set_line_raw(vim, line, out)) { XFree_System(out); return false; }
    XFree_System(out);
    return true;
}

static bool xvim_delete_range(XTuiVim* vim, int line, int start, int end)
{
    return xvim_replace_range(vim, line, start, end, "");
}

static bool xvim_insert_text(XTuiVim* vim, const char* utf8)
{
    int line, col, byte;
    const char* text;
    char* out;
    size_t oldLen, addLen;
    if (!vim || !utf8 || !utf8[0]) return false;
    line = vim->m_cursorLine;
    col = vim->m_cursorColumn;
    text = vim->m_lines[line];
    byte = xvim_byte(text, col);
    oldLen = strlen(text);
    addLen = strlen(utf8);
    if (oldLen + addLen >= XTUI_VIM_LINE_MAX) return false;
    out = (char*)XMalloc_System(oldLen + addLen + 1u);
    if (!out) return false;
    memcpy(out, text, (size_t)byte);
    memcpy(out + byte, utf8, addLen);
    memcpy(out + byte + addLen, text + byte, oldLen - (size_t)byte + 1u);
    if (!xvim_set_line_raw(vim, line, out)) { XFree_System(out); return false; }
    XFree_System(out);
    vim->m_cursorColumn += xvim_chars(utf8);
    vim->m_modified = true;
    return true;
}

#if XTUI_VIM_VISUAL_ON
static bool xvim_insert_block_text(XTuiVim* vim, const char* utf8)
{
    int i;
    int column;
    if (!vim || !utf8 || !utf8[0] || !vim->m_blockInsertMode) return false;
    column = vim->m_cursorColumn;
    for (i = vim->m_blockInsertFirstLine;
         i <= vim->m_blockInsertLastLine && i < vim->m_lineCount; ++i) {
        int length = xvim_chars(vim->m_lines[i]);
        int target = column < length ? column : length;
        if (!xvim_replace_range(vim, i, target, target, utf8)) return false;
    }
    vim->m_cursorColumn += xvim_chars(utf8);
    vim->m_modified = true;
    return true;
}

static void xvim_delete_block_insert_previous(XTuiVim* vim)
{
    int i;
    int column;
    if (!vim || !vim->m_blockInsertMode || vim->m_cursorColumn <= vim->m_blockInsertColumn)
        return;
    column = vim->m_cursorColumn - 1;
    for (i = vim->m_blockInsertFirstLine;
         i <= vim->m_blockInsertLastLine && i < vim->m_lineCount; ++i) {
        if (column < xvim_chars(vim->m_lines[i]))
            (void)xvim_delete_char(vim, i, column);
    }
    --vim->m_cursorColumn;
}
#endif

static bool xvim_delete_char(XTuiVim* vim, int line, int column)
{
    int count;
    if (!vim || line < 0 || line >= vim->m_lineCount) return false;
    count = xvim_chars(vim->m_lines[line]);
    if (column < 0 || column >= count) return false;
    if (!xvim_delete_range(vim, line, column, column + 1)) return false;
    vim->m_modified = true;
    return true;
}

static bool xvim_split(XTuiVim* vim)
{
    const char* text;
    int byte;
    char* left;
    char* right;
    if (!vim) return false;
    text = vim->m_lines[vim->m_cursorLine];
    byte = xvim_byte(text, vim->m_cursorColumn);
    left = (char*)XMalloc_System((size_t)byte + 1u);
    right = (char*)XMalloc_System(strlen(text + byte) + 1u);
    if (!left || !right) { if (left) XFree_System(left); if (right) XFree_System(right); return false; }
    memcpy(left, text, (size_t)byte); left[byte] = '\0';
    strcpy(right, text + byte);
    if (!xvim_set_line_raw(vim, vim->m_cursorLine, left) ||
        !xvim_insert_line_raw(vim, vim->m_cursorLine + 1, right)) {
        XFree_System(left); XFree_System(right); return false;
    }
    XFree_System(left); XFree_System(right);
    ++vim->m_cursorLine; vim->m_cursorColumn = 0; vim->m_modified = true;
    return true;
}

static bool xvim_join_previous(XTuiVim* vim)
{
    int prev;
    size_t len;
    char* out;
    if (!vim || vim->m_cursorLine <= 0) return false;
    prev = vim->m_cursorLine - 1;
    len = strlen(vim->m_lines[prev]) + strlen(vim->m_lines[vim->m_cursorLine]);
    if (len >= XTUI_VIM_LINE_MAX) return false;
    out = (char*)XMalloc_System(len + 1u);
    if (!out) return false;
    strcpy(out, vim->m_lines[prev]); strcat(out, vim->m_lines[vim->m_cursorLine]);
    if (!xvim_set_line_raw(vim, prev, out)) { XFree_System(out); return false; }
    XFree_System(out);
    xvim_delete_line_raw(vim, vim->m_cursorLine);
    --vim->m_cursorLine;
    vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    vim->m_modified = true;
    return true;
}

/* ==================== 词移动与查找 ==================== */

static bool xvim_word_char(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int xvim_word_forward_line(const char* text, int col, bool end, bool big)
{
    int n = xvim_chars(text);
    int c = col;
    int cls;
    if (c >= n) return n;
    cls = big ? (xvim_char_at(text, c) != ' ' && xvim_char_at(text, c) != '\t') :
          xvim_word_char(xvim_char_at(text, c));
    if (end) {
        while (c + 1 < n) {
            int next = xvim_char_at(text, c + 1);
            int same = big ? (next != ' ' && next != '\t') : xvim_word_char(next);
            if (!same) break;
            ++c;
        }
        return c;
    }
    while (c < n) {
        int ch = xvim_char_at(text, c);
        int same = big ? (ch != ' ' && ch != '\t') : xvim_word_char(ch);
        if (same != cls) break;
        ++c;
    }
    while (c < n) {
        int ch = xvim_char_at(text, c);
        if (ch != ' ' && ch != '\t') break;
        ++c;
    }
    return c;
}

static void xvim_word_forward(XTuiVim* vim, bool end, bool big)
{
    int n;
    if (!vim) return;
    n = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    vim->m_cursorColumn = xvim_word_forward_line(vim->m_lines[vim->m_cursorLine],
                                                  vim->m_cursorColumn, end, big);
    while (!end && vim->m_cursorColumn >= n && vim->m_cursorLine + 1 < vim->m_lineCount) {
        ++vim->m_cursorLine; vim->m_cursorColumn = 0;
        n = xvim_chars(vim->m_lines[vim->m_cursorLine]);
        if (n > 0) break;
    }
    if (end && vim->m_cursorColumn >= n && vim->m_cursorLine + 1 < vim->m_lineCount) {
        ++vim->m_cursorLine; vim->m_cursorColumn = 0;
        n = xvim_chars(vim->m_lines[vim->m_cursorLine]);
        if (n > 0) vim->m_cursorColumn = xvim_word_forward_line(vim->m_lines[vim->m_cursorLine], 0, true, big);
    }
}

static void xvim_word_backward(XTuiVim* vim, bool big)
{
    int c;
    const char* text;
    if (!vim) return;
    text = vim->m_lines[vim->m_cursorLine];
    c = vim->m_cursorColumn;
    while (c > 0 && (xvim_char_at(text, c - 1) == ' ' || xvim_char_at(text, c - 1) == '\t')) --c;
    if (c == 0 && vim->m_cursorLine > 0) {
        --vim->m_cursorLine; text = vim->m_lines[vim->m_cursorLine]; c = xvim_chars(text);
        while (c > 0 && (xvim_char_at(text, c - 1) == ' ' || xvim_char_at(text, c - 1) == '\t')) --c;
    }
    if (c > 0) {
        bool cls = big ? (xvim_char_at(text, c - 1) != ' ' && xvim_char_at(text, c - 1) != '\t') : xvim_word_char(xvim_char_at(text, c - 1));
        while (c > 0) {
            int ch = xvim_char_at(text, c - 1);
            bool same = big ? (ch != ' ' && ch != '\t') : xvim_word_char(ch);
            if (same != cls) break;
            --c;
        }
    }
    vim->m_cursorColumn = c;
}

#if XTUI_VIM_ADVANCED_MOTION_ON
static bool xvim_find_char(XTuiVim* vim, char ch, bool backward, bool till)
{
    const char* text;
    int n, c, i, step;
    if (!vim || !ch) return false;
    text = vim->m_lines[vim->m_cursorLine]; n = xvim_chars(text);
    c = vim->m_cursorColumn; step = backward ? -1 : 1;
    for (i = c + step; i >= 0 && i < n; i += step) {
        if (xvim_char_at(text, i) == (unsigned char)ch) {
            if (till) i -= backward ? -1 : 1;
            if (i < 0) i = 0; if (i > n) i = n;
            vim->m_cursorColumn = i; vim->m_findChar = ch;
            vim->m_findBackward = backward; vim->m_findTill = till;
            return true;
        }
    }
    return false;
}
#endif

#if XRegularExpression_ON && \
    (XTUI_VIM_SEARCH_ON || XTUI_VIM_SUBSTITUTE_ON)
static XRegularExpression* xvim_create_regular_expression(XTuiVim* vim, const char* pattern,
                                                           XRegularExpression_PatternOptions options)
{
    XRegularExpression* expression;
    const XString* error;
    const char* message;
    expression = XRegularExpression_create_utf8(
        pattern, options | XRegularExpression_UseUnicodePropertiesOption);
    if (!expression) {
        if (vim) snprintf(vim->m_status, sizeof(vim->m_status), "正则表达式内存不足");
        return NULL;
    }
    if (XRegularExpression_isValid(expression)) return expression;
    error = XRegularExpression_errorString_const(expression);
    message = error ? XString_toUtf8(error) : "";
    if (vim) snprintf(vim->m_status, sizeof(vim->m_status), "无效正则: %.72s", message ? message : "");
    xvim_delete_regular_expression(expression);
    return NULL;
}

static void xvim_delete_regular_expression(XRegularExpression* expression)
{
    if (expression) XRegularExpression_delete_base((XClass*)expression);
}

static void xvim_delete_regular_match(XRegularExpressionMatch* match)
{
    if (match) XRegularExpressionMatch_delete_base((XClass*)match);
}

static void xvim_delete_regular_iterator(XRegularExpressionMatchIterator* iterator)
{
    if (iterator) XRegularExpressionMatchIterator_delete_base((XClass*)iterator);
}

#if XTUI_VIM_SEARCH_ON
static bool xvim_regex_match_in_range(const XRegularExpression* expression, const char* text,
                                      int firstColumn, int lastColumn, bool last, int* column)
{
    XRegularExpressionMatchIterator* iterator;
    XRegularExpressionMatch* match;
    int found = -1;
    if (!expression || !text || !column) return false;
    iterator = XRegularExpression_globalMatch_utf8(expression, text,
                                                    xvim_utf16_offset(text, firstColumn),
                                                    XRegularExpression_NormalMatch,
                                                    XRegularExpression_NoMatchOption);
    if (!iterator) return false;
    while (XRegularExpressionMatchIterator_hasNext(iterator)) {
        int start;
        match = XRegularExpressionMatchIterator_next(iterator);
        if (!match) break;
        start = xvim_column_from_utf16(text, XRegularExpressionMatch_capturedStart(match, 0));
        xvim_delete_regular_match(match);
        if (start < firstColumn || (lastColumn >= 0 && start >= lastColumn)) continue;
        found = start;
        if (!last) break;
    }
    xvim_delete_regular_iterator(iterator);
    if (found < 0) return false;
    *column = found;
    return true;
}

static bool xvim_search(XTuiVim* vim, const char* needle, bool backward)
{
    XRegularExpression* expression;
    int originalLine;
    int originalColumn;
    int line;
    int column;
    if (!vim || !needle || !needle[0]) return false;
    expression = xvim_create_regular_expression(vim, needle,
                                                 XRegularExpression_NoPatternOption);
    if (!expression) return false;
    originalLine = vim->m_cursorLine;
    originalColumn = vim->m_cursorColumn;
    if (!backward) {
        if (xvim_regex_match_in_range(expression, vim->m_lines[originalLine],
                                      originalColumn + 1, -1, false, &column)) {
            vim->m_cursorColumn = column;
            xvim_delete_regular_expression(expression);
            return true;
        }
        for (line = originalLine + 1; line < vim->m_lineCount; ++line) {
            if (xvim_regex_match_in_range(expression, vim->m_lines[line], 0, -1, false, &column)) {
                vim->m_cursorLine = line; vim->m_cursorColumn = column;
                xvim_delete_regular_expression(expression);
                return true;
            }
        }
        for (line = 0; line < originalLine; ++line) {
            if (xvim_regex_match_in_range(expression, vim->m_lines[line], 0, -1, false, &column)) {
                vim->m_cursorLine = line; vim->m_cursorColumn = column;
                xvim_delete_regular_expression(expression);
                return true;
            }
        }
        if (xvim_regex_match_in_range(expression, vim->m_lines[originalLine], 0,
                                      originalColumn, false, &column)) {
            vim->m_cursorColumn = column;
            xvim_delete_regular_expression(expression);
            return true;
        }
    } else {
        if (xvim_regex_match_in_range(expression, vim->m_lines[originalLine], 0,
                                      originalColumn, true, &column)) {
            vim->m_cursorColumn = column;
            xvim_delete_regular_expression(expression);
            return true;
        }
        for (line = originalLine - 1; line >= 0; --line) {
            if (xvim_regex_match_in_range(expression, vim->m_lines[line], 0, -1, true, &column)) {
                vim->m_cursorLine = line; vim->m_cursorColumn = column;
                xvim_delete_regular_expression(expression);
                return true;
            }
        }
        for (line = vim->m_lineCount - 1; line > originalLine; --line) {
            if (xvim_regex_match_in_range(expression, vim->m_lines[line], 0, -1, true, &column)) {
                vim->m_cursorLine = line; vim->m_cursorColumn = column;
                xvim_delete_regular_expression(expression);
                return true;
            }
        }
        if (xvim_regex_match_in_range(expression, vim->m_lines[originalLine], originalColumn,
                                      -1, true, &column)) {
            vim->m_cursorColumn = column;
            xvim_delete_regular_expression(expression);
            return true;
        }
    }
    xvim_delete_regular_expression(expression);
    return false;
}
#endif
#else
#if XTUI_VIM_SEARCH_ON
static bool xvim_search(XTuiVim* vim, const char* needle, bool backward)
{
    int line;
    int cursorByte;
    const char* text;
    const char* hit;
    const char* candidate;
    if (!vim || !needle || !needle[0]) return false;
    text = vim->m_lines[vim->m_cursorLine];
    cursorByte = xvim_byte(text, vim->m_cursorColumn);
    if (!backward) {
        hit = strstr(text + cursorByte + (text[cursorByte] ? 1 : 0), needle);
        if (hit) { vim->m_cursorColumn = xvim_chars(text) - xvim_chars(hit); return true; }
        for (line = vim->m_cursorLine + 1; line < vim->m_lineCount; ++line) {
            text = vim->m_lines[line]; hit = strstr(text, needle);
            if (hit) { vim->m_cursorLine = line; vim->m_cursorColumn = xvim_chars(text) - xvim_chars(hit); return true; }
        }
        for (line = 0; line <= vim->m_cursorLine; ++line) {
            text = vim->m_lines[line]; hit = strstr(text, needle);
            if (line == vim->m_cursorLine && hit && hit >= text + cursorByte) hit = NULL;
            if (hit) { vim->m_cursorLine = line; vim->m_cursorColumn = xvim_chars(text) - xvim_chars(hit); return true; }
        }
    } else {
        candidate = NULL; hit = strstr(text, needle);
        while (hit && hit < text + cursorByte) { candidate = hit; hit = strstr(hit + 1, needle); }
        if (candidate) { vim->m_cursorColumn = xvim_chars(text) - xvim_chars(candidate); return true; }
        for (line = vim->m_cursorLine - 1; line >= 0; --line) {
            text = vim->m_lines[line]; hit = strstr(text, needle);
            if (hit) { candidate = hit; while ((hit = strstr(hit + 1, needle)) != NULL) candidate = hit; vim->m_cursorLine = line; vim->m_cursorColumn = xvim_chars(text) - xvim_chars(candidate); return true; }
        }
        for (line = vim->m_lineCount - 1; line >= vim->m_cursorLine; --line) {
            text = vim->m_lines[line];
            hit = line == vim->m_cursorLine ? strstr(text + cursorByte, needle) : strstr(text, needle);
            if (hit) { vim->m_cursorLine = line; vim->m_cursorColumn = xvim_chars(text) - xvim_chars(hit); return true; }
        }
    }
    return false;
}
#endif
#endif

/* ==================== 选区、操作符与粘贴 ==================== */

static void xvim_order(int sl, int sc, int el, int ec, int* a, int* b, int* c, int* d)
{
    if (sl < el || (sl == el && sc <= ec)) { *a = sl; *b = sc; *c = el; *d = ec; }
    else { *a = el; *b = ec; *c = sl; *d = sc; }
}

static bool xvim_yank_range(XTuiVim* vim, int sl, int sc, int el, int ec, bool lineWise)
{
    int a, b, c, d, i, count;
    if (!vim) return false;
    xvim_order(sl, sc, el, ec, &a, &b, &c, &d);
    if (lineWise) { a = sl < el ? sl : el; c = sl > el ? sl : el; b = d = 0; }
    count = c - a + 1;
    if (lineWise) return xvim_set_register(vim, &vim->m_lines[a], count, true);
    if (a == c) {
        char* part;
        int ba = xvim_byte(vim->m_lines[a], b), bb;
        if (d < b) d = b;
        bb = xvim_byte(vim->m_lines[a], d + 1);
        size_t len = (size_t)(bb - ba);
        part = (char*)XMalloc_System(len + 1u); if (!part) return false;
        memcpy(part, vim->m_lines[a] + ba, len); part[len] = '\0';
        if (!xvim_set_register(vim, &part, 1, false)) { XFree_System(part); return false; }
        XFree_System(part); return true;
    }
    /* 跨行字符选区按行寄存器处理，保持粘贴可预期。 */
    return xvim_set_register(vim, &vim->m_lines[a], count, true);
}

#if XTUI_VIM_VISUAL_ON
static bool xvim_yank_block(XTuiVim* vim, int sl, int sc, int el, int ec)
{
    int a, b, c, d, i;
    int left, right;
    char** parts;
    bool ok;
    if (!vim) return false;
    xvim_order(sl, sc, el, ec, &a, &b, &c, &d);
    left = b < d ? b : d;
    right = b > d ? b : d;
    parts = (char**)XMalloc_System((size_t)(c - a + 1) * sizeof(char*));
    if (!parts) return false;
    memset(parts, 0, (size_t)(c - a + 1) * sizeof(char*));
    for (i = a; i <= c; ++i) {
        const char* text = vim->m_lines[i];
        int length = xvim_chars(text);
        int begin = left < length ? left : length;
        int end = right + 1 < length ? right + 1 : length;
        int beginByte = xvim_byte(text, begin);
        int endByte = xvim_byte(text, end);
        size_t len = endByte >= beginByte ? (size_t)(endByte - beginByte) : 0u;
        parts[i - a] = (char*)XMalloc_System(len + 1u);
        if (!parts[i - a]) break;
        memcpy(parts[i - a], text + beginByte, len);
        parts[i - a][len] = '\0';
    }
    ok = i > c && xvim_set_register(vim, parts, c - a + 1, false);
    for (i = a; i <= c; ++i)
        if (parts[i - a]) XFree_System(parts[i - a]);
    XFree_System(parts);
    return ok;
}

static bool xvim_delete_block(XTuiVim* vim, int sl, int sc, int el, int ec)
{
    int a, b, c, d, i;
    int left, right;
    if (!vim) return false;
    xvim_order(sl, sc, el, ec, &a, &b, &c, &d);
    left = b < d ? b : d;
    right = b > d ? b : d;
    for (i = a; i <= c && i < vim->m_lineCount; ++i) {
        int length = xvim_chars(vim->m_lines[i]);
        int begin = left < length ? left : length;
        int end = right + 1 < length ? right + 1 : length;
        if (end > begin) xvim_delete_range(vim, i, begin, end);
    }
    vim->m_cursorLine = a < vim->m_lineCount ? a : vim->m_lineCount - 1;
    vim->m_cursorColumn = left;
    return true;
}

static bool xvim_delete_selection(XTuiVim* vim, int sl, int sc, int el, int ec)
{
    int a, b, c, d, i;
    const char* first;
    const char* last;
    int firstByte;
    int lastByte;
    size_t prefixLen;
    size_t suffixLen;
    char* out;
    if (!vim) return false;
    xvim_order(sl, sc, el, ec, &a, &b, &c, &d);
    if (a == c) {
        bool ok = xvim_delete_range(vim, a, b, d + 1);
        if (ok) { vim->m_cursorLine = a; vim->m_cursorColumn = b; }
        return ok;
    }
    first = vim->m_lines[a];
    last = vim->m_lines[c];
    firstByte = xvim_byte(first, b);
    lastByte = xvim_byte(last, d + 1);
    prefixLen = (size_t)firstByte;
    suffixLen = strlen(last + lastByte);
    out = (char*)XMalloc_System(prefixLen + suffixLen + 1u);
    if (!out) return false;
    memcpy(out, first, prefixLen);
    memcpy(out + prefixLen, last + lastByte, suffixLen + 1u);
    if (!xvim_set_line_raw(vim, a, out)) { XFree_System(out); return false; }
    XFree_System(out);
    for (i = c; i > a; --i) xvim_delete_line_raw(vim, i);
    vim->m_cursorLine = a; vim->m_cursorColumn = b; return true;
}
#endif

#if XTUI_VIM_YANK_PASTE_ON
static void xvim_paste(XTuiVim* vim, bool before)
{
    int i, line;
    char** lines;
    int count;
    bool lineWise;
    const XVimRegister* selected;
    if (!vim) return;
#if XTUI_VIM_REGISTER_ON
    selected = xvim_paste_register(vim);
#else
    selected = NULL;
#endif
    lines = selected ? selected->lines : vim->m_regLines;
    count = selected ? selected->count : vim->m_regCount;
    lineWise = selected ? selected->lineWise : vim->m_regLineWise;
    if (!lines || count <= 0) return;
    if (lineWise) {
        line = vim->m_cursorLine + (before ? 0 : 1);
        if (!xvim_begin_change(vim)) return;
        for (i = 0; i < count; ++i) xvim_insert_line_raw(vim, line + i, lines[i]);
        vim->m_cursorLine = line; vim->m_cursorColumn = 0; vim->m_modified = true;
    } else {
        if (!xvim_begin_change(vim)) return;
        if (count > 1) {
            int targetLine = vim->m_cursorLine;
            int targetColumn = vim->m_cursorColumn + (before ? 0 : 1);
            for (i = 0; i < count; ++i) {
                while (targetLine >= vim->m_lineCount) xvim_insert_line_raw(vim, vim->m_lineCount, "");
                vim->m_cursorLine = targetLine;
                vim->m_cursorColumn = targetColumn;
                xvim_insert_text(vim, lines[i]);
                ++targetLine;
            }
            vim->m_cursorLine = targetLine - 1;
            vim->m_cursorColumn = targetColumn;
        } else if (before) {
            int old = vim->m_cursorColumn; vim->m_cursorColumn = old;
            xvim_insert_text(vim, lines[0]);
        } else {
            ++vim->m_cursorColumn; xvim_insert_text(vim, lines[0]);
        }
    }
    xvim_record(vim, before ? "P" : "p");
}
#endif

#if XTUI_VIM_VISUAL_ON
static void xvim_finish_visual(XTuiVim* vim)
{
    vim->m_visualMode = false;
    vim->m_visualLineWise = false;
    vim->m_visualBlock = false;
}
#endif

/* ==================== 插入/替换模式 ==================== */

static void xvim_set_insert_repeat(XTuiVim* vim, const char* prefix)
{
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (!vim) return;
    if (!prefix) prefix = "i:";
    strncpy(vim->m_insertRepeatPrefix, prefix,
            sizeof(vim->m_insertRepeatPrefix) - 1u);
    vim->m_insertRepeatPrefix[sizeof(vim->m_insertRepeatPrefix) - 1u] = '\0';
#else
    (void)vim;
    (void)prefix;
#endif
}

static void xvim_enter_insert(XTuiVim* vim, bool replace, bool alreadySnapshotted)
{
    if (!vim) return;
    if (!alreadySnapshotted && !xvim_begin_change(vim)) return;
    vim->m_insertMode = true;
    vim->m_replaceMode = replace;
    vim->m_commandMode = false;
    vim->m_pendingNormal = '\0';
    vim->m_insertLen = 0;
    vim->m_insertCursor = vim->m_cursorColumn;
    vim->m_insertBuf[0] = '\0';
}

static void xvim_leave_insert(XTuiVim* vim)
{
    if (!vim) return;
    if (vim->m_insertLen > 0) {
        char repeat[XTUI_VIM_CMD_MAX + 1];
        const char* prefixText = "i:";
        size_t prefix;
#if XTUI_VIM_ADVANCED_MOTION_ON
        if (vim->m_insertRepeatPrefix[0]) prefixText = vim->m_insertRepeatPrefix;
#endif
        prefix = strlen(prefixText);
        if ((size_t)vim->m_insertLen + prefix < sizeof(repeat)) {
            memcpy(repeat, prefixText, prefix);
            memcpy(repeat + prefix, vim->m_insertBuf, (size_t)vim->m_insertLen);
            repeat[prefix + (size_t)vim->m_insertLen] = '\0';
            xvim_record(vim, repeat);
        }
    }
    vim->m_insertMode = false;
    vim->m_replaceMode = false;
    vim->m_insertLen = 0;
    vim->m_insertCursor = vim->m_cursorColumn;
    vim->m_insertBuf[0] = '\0';
#if XTUI_VIM_ADVANCED_MOTION_ON
    vim->m_insertRepeatPrefix[0] = '\0';
#endif
    if (vim->m_cursorColumn > 0 && vim->m_cursorColumn > xvim_chars(vim->m_lines[vim->m_cursorLine]))
        vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]);
}

static bool xvim_replace_char(XTuiVim* vim, const char* utf8)
{
    int n;
    if (!vim || !utf8 || !utf8[0]) return false;
    n = xvim_chars(vim->m_lines[vim->m_cursorLine]);
    if (vim->m_cursorColumn < n) {
        if (!xvim_replace_range(vim, vim->m_cursorLine, vim->m_cursorColumn,
                                vim->m_cursorColumn + 1, utf8)) return false;
        ++vim->m_cursorColumn;
    } else if (!xvim_insert_text(vim, utf8)) return false;
    vim->m_modified = true; return true;
}

static bool xvim_insert_delete_to_start(XTuiVim* vim)
{
    int column;
    if (!vim || vim->m_cursorColumn <= 0) return false;
    column = vim->m_cursorColumn;
    if (!xvim_delete_range(vim, vim->m_cursorLine, 0, column)) return false;
    vim->m_cursorColumn = 0;
    vim->m_modified = true;
    return true;
}

static bool xvim_insert_delete_previous_word(XTuiVim* vim)
{
    const char* text;
    int start;
    if (!vim || vim->m_cursorColumn <= 0) return false;
    text = vim->m_lines[vim->m_cursorLine];
    start = vim->m_cursorColumn;
    while (start > 0 && (xvim_char_at(text, start - 1) == ' ' ||
                         xvim_char_at(text, start - 1) == '\t')) --start;
    while (start > 0 && xvim_word_char(xvim_char_at(text, start - 1))) --start;
    if (!xvim_delete_range(vim, vim->m_cursorLine, start, vim->m_cursorColumn))
        return false;
    vim->m_cursorColumn = start;
    vim->m_modified = true;
    return true;
}

/* ==================== 冒号命令与搜索 ==================== */

static void xvim_leave_command(XTuiVim* vim)
{
    vim->m_commandMode = false; vim->m_commandLen = 0; vim->m_command[0] = '\0';
}

#if XTUI_VIM_SUBSTITUTE_ON
#if XRegularExpression_ON
static bool xvim_append_substitute_bytes(char* out, size_t capacity, size_t* used,
                                         const char* text, size_t length)
{
    if (!out || !used || (!text && length)) return false;
    if (*used + length >= capacity) return false;
    if (length) memcpy(out + *used, text, length);
    *used += length;
    return true;
}

static bool xvim_append_substitute_capture(char* out, size_t capacity, size_t* used,
                                           const XRegularExpressionMatch* match, int nth)
{
    XString* captured;
    const char* text;
    bool ok;
    captured = XRegularExpressionMatch_captured(match, nth);
    if (!captured) return false;
    text = XString_toUtf8(captured);
    ok = xvim_append_substitute_bytes(out, capacity, used, text ? text : "",
                                      text ? strlen(text) : 0u);
    XClass_delete_base((XClass*)captured);
    return ok;
}

static bool xvim_append_substitute_replacement(char* out, size_t capacity, size_t* used,
                                               const char* replacement,
                                               const XRegularExpressionMatch* match)
{
    size_t i = 0;
    replacement = replacement ? replacement : "";
    while (replacement[i]) {
        if (replacement[i] == '\\' && replacement[i + 1]) {
            if (isdigit((unsigned char)replacement[i + 1])) {
                int nth = replacement[i + 1] - '0';
                if (!xvim_append_substitute_capture(out, capacity, used, match, nth)) return false;
            } else if (!xvim_append_substitute_bytes(out, capacity, used,
                                                     replacement + i + 1, 1u)) return false;
            i += 2;
        } else if (replacement[i] == '&') {
            if (!xvim_append_substitute_capture(out, capacity, used, match, 0)) return false;
            ++i;
        } else {
            if (!xvim_append_substitute_bytes(out, capacity, used, replacement + i, 1u)) return false;
            ++i;
        }
    }
    return true;
}

static bool xvim_substitute_line(XTuiVim* vim, int line,
                                 const XRegularExpression* expression,
                                 const char* replacement, bool global)
{
    char out[XTUI_VIM_LINE_MAX];
    const char* text;
    XRegularExpressionMatchIterator* iterator;
    XRegularExpressionMatch* match;
    size_t used = 0;
    int sourceByte = 0;
    int matches = 0;
    if (!vim || line < 0 || line >= vim->m_lineCount || !expression) return false;
    text = vim->m_lines[line];
    iterator = XRegularExpression_globalMatch_utf8(expression, text, 0,
                                                    XRegularExpression_NormalMatch,
                                                    XRegularExpression_NoMatchOption);
    if (!iterator) return false;
    while (XRegularExpressionMatchIterator_hasNext(iterator)) {
        int64_t start16;
        int64_t end16;
        int startByte;
        int endByte;
        match = XRegularExpressionMatchIterator_next(iterator);
        if (!match) break;
        start16 = XRegularExpressionMatch_capturedStart(match, 0);
        end16 = XRegularExpressionMatch_capturedEnd(match, 0);
        startByte = xvim_byte_from_utf16(text, start16);
        endByte = xvim_byte_from_utf16(text, end16);
        if (startByte < sourceByte || endByte < startByte ||
            !xvim_append_substitute_bytes(out, sizeof(out), &used,
                                           text + sourceByte, (size_t)(startByte - sourceByte)) ||
            !xvim_append_substitute_replacement(out, sizeof(out), &used, replacement, match)) {
            xvim_delete_regular_match(match);
            xvim_delete_regular_iterator(iterator);
            return false;
        }
        sourceByte = endByte;
        ++matches;
        xvim_delete_regular_match(match);
        if (!global) break;
    }
    if (!matches || !xvim_append_substitute_bytes(out, sizeof(out), &used,
                                                  text + sourceByte,
                                                  strlen(text + sourceByte))) {
        xvim_delete_regular_iterator(iterator);
        return false;
    }
    out[used] = '\0';
    xvim_delete_regular_iterator(iterator);
    return xvim_set_line_raw(vim, line, out);
}
#else
static bool xvim_substitute_line(XTuiVim* vim, int line, const char* old,
                                 const char* replacement, bool global)
{
    char out[XTUI_VIM_LINE_MAX];
    const char* p;
    size_t oldLen, replLen, used = 0;
    int matches = 0;
    if (!vim || !old || !old[0]) return false;
    p = vim->m_lines[line]; oldLen = strlen(old); replLen = strlen(replacement ? replacement : "");
    while (*p && used + 1u < sizeof(out)) {
        const char* hit = strstr(p, old);
        if (!hit || (!global && matches > 0)) {
            size_t tail = strlen(p); if (tail > sizeof(out) - used - 1u) tail = sizeof(out) - used - 1u;
            memcpy(out + used, p, tail); used += tail; break;
        }
        if ((size_t)(hit - p) > sizeof(out) - used - 1u) break;
        memcpy(out + used, p, (size_t)(hit - p)); used += (size_t)(hit - p);
        if (replLen > sizeof(out) - used - 1u) replLen = sizeof(out) - used - 1u;
        memcpy(out + used, replacement ? replacement : "", replLen); used += replLen;
        p = hit + oldLen; ++matches;
    }
    out[used] = '\0';
    if (!matches) return false;
    return xvim_set_line_raw(vim, line, out);
}
#endif

#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
static bool xvim_substitute_confirm_find(XTuiVim* vim)
{
    XRegularExpression* expression;
    XRegularExpression_PatternOptions options = XRegularExpression_NoPatternOption;
    int line;
    if (!vim) return false;
    if (vim->m_substituteIgnoreCase)
        options |= XRegularExpression_CaseInsensitiveOption;
    expression = xvim_create_regular_expression(vim, vim->m_substitutePattern, options);
    if (!expression) return false;
    for (line = vim->m_substituteLine; line <= vim->m_substituteLastLine; ++line) {
        const char* text = vim->m_lines[line];
        int startColumn = line == vim->m_substituteLine ? vim->m_substituteNextColumn : 0;
        XRegularExpressionMatch* match = XRegularExpression_match_utf8(
            expression, text, xvim_utf16_offset(text, startColumn),
            XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
        if (match && XRegularExpressionMatch_hasMatch(match)) {
            int start = xvim_column_from_utf16(text, XRegularExpressionMatch_capturedStart(match, 0));
            int end = xvim_column_from_utf16(text, XRegularExpressionMatch_capturedEnd(match, 0));
            xvim_delete_regular_match(match);
            if (start < startColumn) continue;
            vim->m_substituteLine = line;
            vim->m_substituteColumn = start;
            vim->m_substituteEndColumn = end;
            vim->m_cursorLine = line;
            vim->m_cursorColumn = start;
            snprintf(vim->m_status, sizeof(vim->m_status),
                     "替换? (y/n/a/l/q)");
            xvim_delete_regular_expression(expression);
            return true;
        }
        xvim_delete_regular_match(match);
        vim->m_substituteLine = line + 1;
        vim->m_substituteNextColumn = 0;
    }
    xvim_delete_regular_expression(expression);
    vim->m_substituteConfirm = false;
    snprintf(vim->m_status, sizeof(vim->m_status), "替换完成");
    return false;
}

static bool xvim_substitute_confirm_replace(XTuiVim* vim)
{
    XRegularExpression* expression;
    XRegularExpression_PatternOptions options = XRegularExpression_NoPatternOption;
    XRegularExpressionMatch* match;
    const char* text;
    char replacement[XTUI_VIM_LINE_MAX];
    size_t length = 0;
    int start;
    int end;
    bool ok;
    if (!vim || !vim->m_substituteConfirm) return false;
    if (vim->m_substituteIgnoreCase)
        options |= XRegularExpression_CaseInsensitiveOption;
    expression = xvim_create_regular_expression(vim, vim->m_substitutePattern, options);
    if (!expression) return false;
    text = vim->m_lines[vim->m_substituteLine];
    match = XRegularExpression_match_utf8(
        expression, text, xvim_utf16_offset(text, vim->m_substituteColumn),
        XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
    if (!match || !XRegularExpressionMatch_hasMatch(match)) {
        xvim_delete_regular_match(match);
        xvim_delete_regular_expression(expression);
        return false;
    }
    start = xvim_column_from_utf16(text, XRegularExpressionMatch_capturedStart(match, 0));
    end = xvim_column_from_utf16(text, XRegularExpressionMatch_capturedEnd(match, 0));
    if (start != vim->m_substituteColumn ||
        !xvim_append_substitute_replacement(replacement, sizeof(replacement), &length,
                                             vim->m_substituteReplacement, match)) {
        xvim_delete_regular_match(match);
        xvim_delete_regular_expression(expression);
        return false;
    }
    replacement[length] = '\0';
    ok = xvim_replace_range(vim, vim->m_substituteLine, start, end, replacement);
    xvim_delete_regular_match(match);
    xvim_delete_regular_expression(expression);
    if (!ok) return false;
    vim->m_modified = true;
    vim->m_substituteNextColumn = start + xvim_chars(replacement);
    if (vim->m_substituteNextColumn <= start) vim->m_substituteNextColumn = start + 1;
    if (!vim->m_substituteGlobal) {
        ++vim->m_substituteLine;
        vim->m_substituteNextColumn = 0;
    }
    return true;
}

static void xvim_substitute_confirm_skip(XTuiVim* vim)
{
    if (!vim) return;
    vim->m_substituteNextColumn = vim->m_substituteEndColumn;
    if (vim->m_substituteNextColumn <= vim->m_substituteColumn)
        vim->m_substituteNextColumn = vim->m_substituteColumn + 1;
    if (!vim->m_substituteGlobal) {
        ++vim->m_substituteLine;
        vim->m_substituteNextColumn = 0;
    }
}
#endif

static bool xvim_parse_address(const XTuiVim* vim, const char** cursor, int* line)
{
    const char* p;
    int value = 0;
    if (!vim || !cursor || !*cursor || !line) return false;
    p = *cursor;
    if (*p == '.') { *line = vim->m_cursorLine; *cursor = p + 1; return true; }
    if (*p == '$') { *line = vim->m_lineCount - 1; *cursor = p + 1; return true; }
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    if (value <= 0 || value > vim->m_lineCount) return false;
    *line = value - 1;
    *cursor = p;
    return true;
}

static bool xvim_parse_range(const XTuiVim* vim, const char** cursor, int* first, int* last)
{
    const char* p;
    int start;
    int end;
    if (!vim || !cursor || !*cursor || !first || !last) return false;
    p = *cursor;
    start = end = vim->m_cursorLine;
    if (*p == '%') { start = 0; end = vim->m_lineCount - 1; ++p; }
    else if (xvim_parse_address(vim, &p, &start)) {
        end = start;
        if (*p == ',' || *p == ';') {
            ++p;
            if (!xvim_parse_address(vim, &p, &end)) return false;
        }
    }
    if (start > end) return false;
    *cursor = p;
    *first = start;
    *last = end;
    return true;
}

static bool xvim_copy_substitute_part(const char** cursor, char delimiter,
                                      char* output, size_t capacity)
{
    const char* p;
    size_t length = 0;
    if (!cursor || !*cursor || !output || capacity < 1u) return false;
    p = *cursor;
    while (*p && *p != delimiter) {
        if (*p == '\\' && p[1] == delimiter) ++p;
        if (length + 1u >= capacity) return false;
        output[length++] = *p++;
    }
    if (*p != delimiter) return false;
    output[length] = '\0';
    *cursor = p + 1;
    return true;
}

static bool xvim_parse_substitute(XTuiVim* vim, const char* cmd)
{
    bool global = false;
    bool ignoreCase = false;
    bool confirm = false;
    const char* p = cmd;
    char old[XTUI_VIM_CMD_MAX + 1];
    char repl[XTUI_VIM_CMD_MAX + 1];
    char delimiter;
    int i;
    int first;
    int last;
    bool changed = false;
#if XRegularExpression_ON
    XRegularExpression* expression;
    XRegularExpression_PatternOptions options = XRegularExpression_NoPatternOption;
#endif
    if (!vim || !cmd || !xvim_parse_range(vim, &p, &first, &last) || *p != 's') return false;
    ++p;
    delimiter = *p++;
    if (!delimiter || isalnum((unsigned char)delimiter) || delimiter == ' ') return false;
    if (!xvim_copy_substitute_part(&p, delimiter, old, sizeof(old)) || !old[0] ||
        !xvim_copy_substitute_part(&p, delimiter, repl, sizeof(repl))) return false;
    while (*p) {
        if (*p == 'g') global = true;
        else if (*p == 'i') ignoreCase = true;
        else if (*p == 'I') ignoreCase = false;
        else if (*p == 'c') {
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
            confirm = true;
#else
            return false;
#endif
        }
        else return false;
        ++p;
    }
#if XRegularExpression_ON
    if (ignoreCase) options |= XRegularExpression_CaseInsensitiveOption;
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    if (confirm) {
        size_t oldLength = strlen(old);
        size_t replLength = strlen(repl);
        if (oldLength >= sizeof(vim->m_substitutePattern) ||
            replLength >= sizeof(vim->m_substituteReplacement) ||
            !xvim_begin_change(vim)) return false;
        memcpy(vim->m_substitutePattern, old, oldLength + 1u);
        memcpy(vim->m_substituteReplacement, repl, replLength + 1u);
        vim->m_substituteConfirm = true;
        vim->m_substituteGlobal = global;
        vim->m_substituteIgnoreCase = ignoreCase;
        vim->m_substituteFirstLine = first;
        vim->m_substituteLastLine = last;
        vim->m_substituteLine = first;
        vim->m_substituteNextColumn = 0;
        (void)xvim_substitute_confirm_find(vim);
        return true;
    }
#else
    (void)confirm;
#endif
    expression = xvim_create_regular_expression(vim, old, options);
    if (!expression) return false;
#else
    if (confirm) return false;
#endif
    if (!xvim_begin_change(vim)) {
#if XRegularExpression_ON
        xvim_delete_regular_expression(expression);
#endif
        return false;
    }
#if XRegularExpression_ON
    for (i = first; i <= last; ++i)
        if (xvim_substitute_line(vim, i, expression, repl, global)) changed = true;
    xvim_delete_regular_expression(expression);
#else
    for (i = first; i <= last; ++i)
        if (xvim_substitute_line(vim, i, old, repl, global)) changed = true;
#endif
    if (changed) vim->m_modified = true;
    return true;
}
#endif

#if XTUI_VIM_MULTIBUFFER_ON
static bool xvim_request_path_action(XTuiVim* vim, const char* path,
                                     bool writePath, bool saveAs)
{
    size_t length;
    if (!vim || !path || !path[0]) return false;
    length = strlen(path);
    if (length >= sizeof(vim->m_actionPath)) {
        snprintf(vim->m_status, sizeof(vim->m_status), "文件路径过长");
        return false;
    }
    memcpy(vim->m_actionPath, path, length + 1u);
    vim->m_wantWritePath = writePath;
    vim->m_wantSaveAs = saveAs;
    return true;
}

static bool xvim_request_edit(XTuiVim* vim, const char* path, bool force)
{
    size_t length;
    if (!vim || !path || !path[0]) return false;
    if (vim->m_modified && !force) {
        snprintf(vim->m_status, sizeof(vim->m_status),
                 "有未保存修改，使用 :e! 放弃当前缓冲修改");
        return false;
    }
    length = strlen(path);
    if (length >= sizeof(vim->m_actionPath)) {
        snprintf(vim->m_status, sizeof(vim->m_status), "文件路径过长");
        return false;
    }
    memcpy(vim->m_actionPath, path, length + 1u);
    vim->m_wantEdit = true;
    vim->m_wantForce = force;
    return true;
}

static bool xvim_request_buffer(XTuiVim* vim, const char* cmd)
{
    int index = 0;
    if (!vim || !cmd) return false;
    if (strcmp(cmd, "bnext") == 0 || strcmp(cmd, "bn") == 0) {
        vim->m_wantBufferNext = true;
        return true;
    }
    if (strcmp(cmd, "bprevious") == 0 || strcmp(cmd, "bprev") == 0 ||
        strcmp(cmd, "bp") == 0) {
        vim->m_wantBufferPrev = true;
        return true;
    }
    if (strcmp(cmd, "buffers") == 0 || strcmp(cmd, "ls") == 0 || strcmp(cmd, "files") == 0) {
        vim->m_wantBufferList = true;
        return true;
    }
    if (cmd[0] != 'b') return false;
    if (!cmd[1]) {
        vim->m_wantBufferList = true;
        return true;
    }
    if (cmd[1] != ' ') return false;
    ++cmd;
    while (*cmd == ' ') ++cmd;
    while (*cmd >= '0' && *cmd <= '9') {
        index = index * 10 + (*cmd - '0');
        ++cmd;
    }
    if (*cmd || index <= 0) return false;
    vim->m_wantBufferIndex = index;
    return true;
}
#endif

#if XTUI_VIM_HISTORY_ON
static void xvim_history_add(char history[][XTUI_VIM_CMD_MAX + 1], int* length,
                             int* index, const char* text)
{
    int i;
    if (!history || !length || !index || !text || !text[0]) return;
    if (*length > 0 && strcmp(history[*length - 1], text) == 0) {
        *index = *length;
        return;
    }
    if (*length == XTUI_VIM_HISTORY_MAX) {
        for (i = 1; i < *length; ++i)
            memcpy(history[i - 1], history[i], sizeof(history[i - 1]));
        --*length;
    }
    memcpy(history[*length], text, strlen(text) + 1u);
    ++*length;
    *index = *length;
}

static void xvim_history_previous(char history[][XTUI_VIM_CMD_MAX + 1], int length,
                                  int* index, char* value, int* valueLength)
{
    if (!history || !index || !value || !valueLength || length <= 0) return;
    if (*index > 0) --*index;
    memcpy(value, history[*index], XTUI_VIM_CMD_MAX + 1u);
    *valueLength = (int)strlen(value);
}

static void xvim_history_next(char history[][XTUI_VIM_CMD_MAX + 1], int length,
                              int* index, char* value, int* valueLength)
{
    if (!history || !index || !value || !valueLength) return;
    if (*index + 1 < length) {
        ++*index;
        memcpy(value, history[*index], XTUI_VIM_CMD_MAX + 1u);
        *valueLength = (int)strlen(value);
    } else {
        *index = length;
        value[0] = '\0';
        *valueLength = 0;
    }
}
#endif

static void xvim_execute_command(XTuiVim* vim)
{
    const char* cmd = vim->m_command;
    if (!cmd[0]) { xvim_leave_command(vim); return; }
#if XTUI_VIM_HISTORY_ON
    xvim_history_add(vim->m_commandHistory, &vim->m_commandHistoryLen,
                     &vim->m_commandHistoryIndex, cmd);
#endif
    if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) vim->m_wantSave = true;
    else if (strcmp(cmd, "w!") == 0 || strcmp(cmd, "write!") == 0) {
        vim->m_wantSave = true;
#if XTUI_VIM_MULTIBUFFER_ON
        vim->m_wantForce = true;
#endif
    }
    else if (strcmp(cmd, "q") == 0) {
        if (vim->m_modified) snprintf(vim->m_status, sizeof(vim->m_status), "有未保存修改，使用 :wq 保存退出或 :q! 放弃");
        else vim->m_wantQuit = true;
    } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) vim->m_wantSaveQuit = true;
    else if (strcmp(cmd, "wq!") == 0 || strcmp(cmd, "x!") == 0) {
        vim->m_wantSaveQuit = true;
#if XTUI_VIM_MULTIBUFFER_ON
        vim->m_wantForce = true;
#endif
    } else if (strcmp(cmd, "q!") == 0) {
        vim->m_wantQuit = true;
#if XTUI_VIM_MULTIBUFFER_ON
        vim->m_wantForce = true;
#endif
    }
#if XTUI_VIM_EX_ON
    else if (strcmp(cmd, "set nu") == 0 || strcmp(cmd, "set number") == 0) vim->m_showLineNumbers = true;
    else if (strcmp(cmd, "set nonu") == 0 || strcmp(cmd, "set nonumber") == 0) vim->m_showLineNumbers = false;
#if XTUI_VIM_SEARCH_ON
    else if (strcmp(cmd, "nohlsearch") == 0) vim->m_searchHighlight = false;
    else if (strcmp(cmd, "set hlsearch") == 0) vim->m_searchHighlight = true;
#endif
#endif
#if XTUI_VIM_MULTIBUFFER_ON
    else if (strncmp(cmd, "e! ", 3) == 0) (void)xvim_request_edit(vim, cmd + 3, true);
    else if (strncmp(cmd, "edit! ", 6) == 0) (void)xvim_request_edit(vim, cmd + 6, true);
    else if (strncmp(cmd, "e ", 2) == 0) (void)xvim_request_edit(vim, cmd + 2, false);
    else if (strncmp(cmd, "edit ", 5) == 0) (void)xvim_request_edit(vim, cmd + 5, false);
    else if (xvim_request_buffer(vim, cmd)) { }
#endif
#if XTUI_VIM_YANK_PASTE_ON
    else if (strcmp(cmd, "put") == 0) xvim_paste(vim, false);
    else if (strcmp(cmd, "put!") == 0) xvim_paste(vim, true);
#else
    else if (strcmp(cmd, "put") == 0 || strcmp(cmd, "put!") == 0)
        snprintf(vim->m_status, sizeof(vim->m_status), "未启用寄存器粘贴");
#endif
    else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "d") == 0)
        xvim_linewise_op(vim, 'd', 1);
#if XTUI_VIM_YANK_PASTE_ON
    else if (strcmp(cmd, "yank") == 0 || strcmp(cmd, "y") == 0)
        xvim_linewise_op(vim, 'y', 1);
#endif
#if XTUI_VIM_MULTIBUFFER_ON
    else if (strcmp(cmd, "wa") == 0 || strcmp(cmd, "wall") == 0)
        vim->m_wantWriteAll = true;
    else if (strcmp(cmd, "wqa") == 0 || strcmp(cmd, "wqall") == 0) {
        vim->m_wantWriteAll = true;
        vim->m_wantQuitAll = true;
    } else if (strcmp(cmd, "qa") == 0 || strcmp(cmd, "qall") == 0)
        vim->m_wantQuitAll = true;
    else if (strcmp(cmd, "qa!") == 0 || strcmp(cmd, "qall!") == 0) {
        vim->m_wantQuitAll = true;
        vim->m_wantForce = true;
    } else if (strcmp(cmd, "bd") == 0 || strcmp(cmd, "bdelete") == 0)
        vim->m_wantBufferClose = true;
    else if (strcmp(cmd, "bd!") == 0 || strcmp(cmd, "bdelete!") == 0) {
        vim->m_wantBufferClose = true;
        vim->m_wantForce = true;
    } else if (strncmp(cmd, "w ", 2) == 0 || strncmp(cmd, "write ", 6) == 0) {
        const char* path = cmd[1] == ' ' ? cmd + 2 : cmd + 6;
        while (*path == ' ') ++path;
        (void)xvim_request_path_action(vim, path, true, false);
    } else if (strncmp(cmd, "saveas ", 7) == 0) {
        const char* path = cmd + 7;
        while (*path == ' ') ++path;
        (void)xvim_request_path_action(vim, path, false, true);
    }
#endif
    else {
        vim->m_status[0] = '\0';
#if XTUI_VIM_SUBSTITUTE_ON
        if (!xvim_parse_substitute(vim, cmd) && !vim->m_status[0])
            snprintf(vim->m_status, sizeof(vim->m_status), "无效命令: :%s", cmd);
#else
        snprintf(vim->m_status, sizeof(vim->m_status), "无效命令: :%s", cmd);
#endif
    }
    xvim_leave_command(vim);
}

#if XTUI_VIM_SEARCH_ON
static void xvim_start_search(XTuiVim* vim, bool backward)
{
    vim->m_searchMode = true; vim->m_searchBackward = backward;
    vim->m_searchHighlight = true;
    vim->m_searchLen = 0; vim->m_search[0] = '\0';
#if XTUI_VIM_HISTORY_ON
    vim->m_searchHistoryIndex = vim->m_searchHistoryLen;
#endif
}

static void xvim_finish_search(XTuiVim* vim)
{
    if (vim->m_searchLen > 0) {
        memcpy(vim->m_lastSearch, vim->m_search, (size_t)vim->m_searchLen + 1u);
        vim->m_lastSearchLen = vim->m_searchLen;
#if XTUI_VIM_HISTORY_ON
        xvim_history_add(vim->m_searchHistory, &vim->m_searchHistoryLen,
                         &vim->m_searchHistoryIndex, vim->m_search);
#endif
        vim->m_status[0] = '\0';
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        if (!xvim_search(vim, vim->m_search, vim->m_searchBackward) && !vim->m_status[0])
            snprintf(vim->m_status, sizeof(vim->m_status), "未找到: %s", vim->m_search);
    }
    vim->m_searchMode = false; vim->m_searchLen = 0; vim->m_search[0] = '\0';
}
#endif

/* ==================== 普通模式操作 ==================== */

static void xvim_record(XTuiVim* vim, const char* text)
{
#if XTUI_VIM_ADVANCED_MOTION_ON
    size_t n;
    if (!vim || !text) return;
    n = strlen(text); if (n > XTUI_VIM_CMD_MAX) n = XTUI_VIM_CMD_MAX;
    memcpy(vim->m_lastRepeat, text, n); vim->m_lastRepeat[n] = '\0'; vim->m_lastRepeatLen = (int)n;
#else
    (void)vim;
    (void)text;
#endif
}

#if XTUI_VIM_MACRO_ON
static void xvim_macro_record_event(XTuiVim* vim, const XTuiKeyEvent* event)
{
    int index;
    if (!vim || !event || !vim->m_macroRecording || vim->m_macroPlaying) return;
    index = vim->m_macroRegister - 'a';
    if (index < 0 || index >= 26 || vim->m_macroLengths[index] >= XTUI_VIM_CMD_MAX) return;
    if (event->m_keyType == XTuiKey_Char) {
        if (event->m_utf8[0] < 0x80)
            vim->m_macros[index][vim->m_macroLengths[index]++] = event->m_utf8[0];
    } else if (event->m_keyType == XTuiKey_Escape) {
        vim->m_macros[index][vim->m_macroLengths[index]++] = '\x1b';
    } else if (event->m_keyType == XTuiKey_Enter) {
        vim->m_macros[index][vim->m_macroLengths[index]++] = '\n';
    }
    vim->m_macros[index][vim->m_macroLengths[index]] = '\0';
}

static void xvim_macro_play(XTuiVim* vim, char name)
{
    int index;
    int i;
    if (!vim) return;
    if (name == '@') name = vim->m_lastMacro;
    index = name - 'a';
    if (index < 0 || index >= 26 || vim->m_macroLengths[index] <= 0 || vim->m_macroPlaying)
        return;
    vim->m_lastMacro = name;
    vim->m_macroPlaying = true;
    for (i = 0; i < vim->m_macroLengths[index]; ++i) {
        XTuiKeyEvent event;
        unsigned char key = (unsigned char)vim->m_macros[index][i];
        if (key == 0x1b)
            XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Escape,
                              XKeyboardModifier_NoModifier);
        else if (key == '\n')
            XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Enter,
                              XKeyboardModifier_NoModifier);
        else {
            XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Char,
                              XKeyboardModifier_NoModifier);
            event.m_utf8[0] = (char)key;
            event.m_utf8[1] = '\0';
        }
        XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
    }
    vim->m_macroPlaying = false;
}
#endif

static void xvim_linewise_op(XTuiVim* vim, char op, int count)
{
    int a, i, n;
    bool captured;
    if (!vim) return;
    a = vim->m_cursorLine; n = count > 0 ? count : 1;
    if (a + n > vim->m_lineCount) n = vim->m_lineCount - a;
    if (op == 'y') {
        captured = xvim_set_register(vim, &vim->m_lines[a], n, true);
#if XTUI_VIM_REGISTER_ON
        if (captured) xvim_set_numbered_yank(vim);
#endif
        return;
    }
    if (!xvim_begin_change(vim)) return;
    captured = xvim_set_register(vim, &vim->m_lines[a], n, true);
#if XTUI_VIM_REGISTER_ON
    if (op == 'd' && captured) xvim_set_numbered_delete(vim);
#endif
    for (i = 0; i < n; ++i) xvim_delete_line_raw(vim, a);
    vim->m_cursorLine = a < vim->m_lineCount ? a : vim->m_lineCount - 1; vim->m_cursorColumn = 0; vim->m_modified = true;
    if (op == 'c') xvim_enter_insert(vim, false, true);
}

#if XTUI_VIM_ADVANCED_MOTION_ON || XTUI_VIM_VISUAL_ON
static bool xvim_motion_target(XTuiVim* vim, char c, int* line, int* col, bool* inclusive)
{
    int oldLine, oldCol;
    if (!vim || !line || !col || !inclusive) return false;
    oldLine = vim->m_cursorLine; oldCol = vim->m_cursorColumn; *inclusive = true;
    if (c == 'w') xvim_word_forward(vim, false, false);
    else if (c == 'W') xvim_word_forward(vim, false, true);
    else if (c == 'e') xvim_word_forward(vim, true, false);
    else if (c == 'E') xvim_word_forward(vim, true, true);
    else if (c == 'b') xvim_word_backward(vim, false);
    else if (c == 'B') xvim_word_backward(vim, true);
    else if (c == '$') { vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]); *inclusive = true; }
    else if (c == '0' || c == '^') vim->m_cursorColumn = 0;
    else if (c == 'h' && vim->m_cursorColumn > 0) --vim->m_cursorColumn;
    else if (c == 'l' && vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine])) ++vim->m_cursorColumn;
    else if (c == 'j') xvim_goto(vim, vim->m_cursorLine + 1, vim->m_cursorColumn);
    else if (c == 'k') xvim_goto(vim, vim->m_cursorLine - 1, vim->m_cursorColumn);
#if XTUI_VIM_ADVANCED_MOTION_ON
    else if (c == 'G') xvim_goto(vim, vim->m_lineCount - 1, vim->m_cursorColumn);
    else if (c == '%') { if (!xvim_match_bracket(vim)) { vim->m_cursorLine = oldLine; vim->m_cursorColumn = oldCol; return false; } }
#endif
    else { vim->m_cursorLine = oldLine; vim->m_cursorColumn = oldCol; return false; }
    *line = vim->m_cursorLine; *col = vim->m_cursorColumn;
    vim->m_cursorLine = oldLine; vim->m_cursorColumn = oldCol;
    return true;
}
#endif

static void xvim_word_object(const XTuiVim* vim, bool includeSpace, int* start, int* end)
{
    const char* text;
    int n, a, b;
    if (!vim || !start || !end) return;
    text = vim->m_lines[vim->m_cursorLine]; n = xvim_chars(text);
    a = vim->m_cursorColumn; b = a;
    while (a > 0 && xvim_word_char(xvim_char_at(text, a - 1))) --a;
    while (b < n && xvim_word_char(xvim_char_at(text, b))) ++b;
    if (includeSpace) {
        if (b < n && (xvim_char_at(text, b) == ' ' || xvim_char_at(text, b) == '\t')) {
            while (b < n && (xvim_char_at(text, b) == ' ' || xvim_char_at(text, b) == '\t')) ++b;
        } else while (a > 0 && (xvim_char_at(text, a - 1) == ' ' || xvim_char_at(text, a - 1) == '\t')) --a;
    }
    *start = a; *end = b;
}

#if XTUI_VIM_ADVANCED_MOTION_ON
static bool xvim_pair_object(const XTuiVim* vim, char delimiter,
                             bool includeDelimiter, int* start, int* end)
{
    const char* text;
    int open;
    int close;
    int cursor;
    int left = -1;
    int right = -1;
    int depth;
    int i;
    if (!vim || !start || !end) return false;
    text = vim->m_lines[vim->m_cursorLine];
    cursor = vim->m_cursorColumn;
    if (delimiter == '(' || delimiter == ')' || delimiter == '[' || delimiter == ']' ||
        delimiter == '{' || delimiter == '}' || delimiter == '<' || delimiter == '>') {
        if (delimiter == '(' || delimiter == ')') { open = '('; close = ')'; }
        else if (delimiter == '[' || delimiter == ']') { open = '['; close = ']'; }
        else if (delimiter == '{' || delimiter == '}') { open = '{'; close = '}'; }
        else { open = '<'; close = '>'; }
        depth = 0;
        for (i = cursor; i >= 0; --i) {
            int ch = xvim_char_at(text, i);
            if (ch == close) ++depth;
            else if (ch == open) {
                if (depth == 0) { left = i; break; }
                --depth;
            }
        }
        if (left < 0) return false;
        depth = 0;
        for (i = left + 1; i < xvim_chars(text); ++i) {
            int ch = xvim_char_at(text, i);
            if (ch == open) ++depth;
            else if (ch == close) {
                if (depth == 0) { right = i; break; }
                --depth;
            }
        }
    } else if (delimiter == '"' || delimiter == '\'') {
        for (i = cursor; i >= 0; --i) {
            if (xvim_char_at(text, i) == delimiter) { left = i; break; }
        }
        if (left < 0) return false;
        for (i = left + 1; i < xvim_chars(text); ++i) {
            if (xvim_char_at(text, i) == delimiter) { right = i; break; }
        }
    } else return false;
    if (right <= left) return false;
    *start = left + (includeDelimiter ? 0 : 1);
    *end = right + (includeDelimiter ? 1 : 0);
    return *end > *start;
}

static bool xvim_match_bracket(XTuiVim* vim)
{
    int current;
    int open;
    int close;
    int direction;
    int line;
    int column;
    int depth = 0;
    if (!vim) return false;
    current = xvim_char_at(vim->m_lines[vim->m_cursorLine], vim->m_cursorColumn);
    if (current == '(') { open = '('; close = ')'; direction = 1; }
    else if (current == '[') { open = '['; close = ']'; direction = 1; }
    else if (current == '{') { open = '{'; close = '}'; direction = 1; }
    else if (current == ')') { open = '('; close = ')'; direction = -1; }
    else if (current == ']') { open = '['; close = ']'; direction = -1; }
    else if (current == '}') { open = '{'; close = '}'; direction = -1; }
    else return false;
    if (direction > 0) {
        for (line = vim->m_cursorLine; line < vim->m_lineCount; ++line) {
            int start = line == vim->m_cursorLine ? vim->m_cursorColumn + 1 : 0;
            int length = xvim_chars(vim->m_lines[line]);
            for (column = start; column < length; ++column) {
                current = xvim_char_at(vim->m_lines[line], column);
                if (current == open) ++depth;
                else if (current == close) {
                    if (depth == 0) { vim->m_cursorLine = line; vim->m_cursorColumn = column; return true; }
                    --depth;
                }
            }
        }
    } else {
        for (line = vim->m_cursorLine; line >= 0; --line) {
            int start = line == vim->m_cursorLine ? vim->m_cursorColumn - 1 : xvim_chars(vim->m_lines[line]) - 1;
            for (column = start; column >= 0; --column) {
                current = xvim_char_at(vim->m_lines[line], column);
                if (current == close) ++depth;
                else if (current == open) {
                    if (depth == 0) { vim->m_cursorLine = line; vim->m_cursorColumn = column; return true; }
                    --depth;
                }
            }
        }
    }
    return false;
}
#endif

#if XTUI_VIM_SEARCH_ON
static bool xvim_search_word(XTuiVim* vim, bool backward)
{
    int start;
    int end;
    int beginByte;
    int endByte;
    size_t length;
    if (!vim) return false;
    xvim_word_object(vim, false, &start, &end);
    if (end <= start) return false;
    beginByte = xvim_byte(vim->m_lines[vim->m_cursorLine], start);
    endByte = xvim_byte(vim->m_lines[vim->m_cursorLine], end);
    length = (size_t)(endByte - beginByte);
    if (length >= sizeof(vim->m_lastSearch)) length = sizeof(vim->m_lastSearch) - 1u;
    memcpy(vim->m_lastSearch, vim->m_lines[vim->m_cursorLine] + beginByte, length);
    vim->m_lastSearch[length] = '\0';
    vim->m_lastSearchLen = (int)length;
    vim->m_searchBackward = backward;
    vim->m_searchHighlight = true;
#if XTUI_VIM_JUMPLIST_ON
    xvim_jump_push(vim);
#endif
    return xvim_search(vim, vim->m_lastSearch, backward);
}
#endif

#if XTUI_VIM_ADVANCED_MOTION_ON
static void xvim_operator_motion(XTuiVim* vim, char motion, int count)
{
    int line, col, a, b, c, d, i, step;
    bool inclusive;
    bool captured;
    char op;
    if (!vim) return;
    op = vim->m_pendingOperator;
    vim->m_cursorLine = vim->m_opStartLine;
    vim->m_cursorColumn = vim->m_opStartColumn;
    line = vim->m_cursorLine;
    col = vim->m_cursorColumn;
    for (step = 0; step < (count > 0 ? count : 1); ++step) {
        if (!xvim_motion_target(vim, motion, &line, &col, &inclusive)) {
            vim->m_pendingOperator = '\0';
            return;
        }
        vim->m_cursorLine = line;
        vim->m_cursorColumn = col;
    }
    vim->m_cursorLine = vim->m_opStartLine;
    vim->m_cursorColumn = vim->m_opStartColumn;
    xvim_order(vim->m_opStartLine, vim->m_opStartColumn, line, col, &a, &b, &c, &d);
    if (motion == 'w' || motion == 'W' || motion == 'b' || motion == 'B') inclusive = false;
    if (op == 'y') {
        captured = xvim_yank_range(vim, a, b, c, inclusive ? d : d - 1, false);
#if XTUI_VIM_REGISTER_ON
        if (captured) xvim_set_numbered_yank(vim);
#endif
    }
    else {
        if (!xvim_begin_change(vim)) { vim->m_pendingOperator = '\0'; return; }
        captured = xvim_yank_range(vim, a, b, c, inclusive ? d : d - 1, false);
#if XTUI_VIM_REGISTER_ON
        if (op == 'd' && captured) xvim_set_numbered_delete(vim);
#endif
        if (a == c) {
            int end = inclusive ? d + 1 : d;
            if (end <= b) end = b + 1;
            xvim_delete_range(vim, a, b, end);
        } else {
            xvim_delete_range(vim, a, b, xvim_chars(vim->m_lines[a]));
            for (i = a + 1; i < c; ++i) xvim_delete_line_raw(vim, a + 1);
            if (c < vim->m_lineCount) {
                int index = a + 1; const char* tail = vim->m_lines[index];
                int cut = xvim_byte(tail, inclusive ? d + 1 : d); size_t tailLen = strlen(tail + cut);
                char* out = (char*)XMalloc_System(tailLen + 1u);
                if (out) { memcpy(out, tail + cut, tailLen + 1u); xvim_set_line_raw(vim, a, out); XFree_System(out); xvim_delete_line_raw(vim, index); }
            }
        }
        vim->m_cursorLine = a; vim->m_cursorColumn = b; vim->m_modified = true;
        if (op == 'c') {
#if XTUI_VIM_ADVANCED_MOTION_ON
            char prefix[8];
            snprintf(prefix, sizeof(prefix), "c%c:", motion);
            xvim_set_insert_repeat(vim, prefix);
#endif
            xvim_enter_insert(vim, false, true);
        }
    }
    vim->m_pendingOperator = '\0'; vim->m_cursorLine = a; vim->m_cursorColumn = b;
    if (op != 'c') xvim_clamp(vim);
    if (op == 'd') xvim_record(vim, motion == '$' ? "D" : (motion == 'w' ? "dw" : "d"));
}
#endif

#if XTUI_VIM_ADVANCED_MOTION_ON
static void xvim_repeat_text(XTuiVim* vim, const char* text, bool replace)
{
    if (!vim || !text || !xvim_begin_change(vim)) return;
    while (*text) {
        char one[XTUI_CELL_UTF8_MAX + 1];
        int n = xvim_utf8_len((unsigned char)*text);
        if (n > XTUI_CELL_UTF8_MAX) n = 1;
        memcpy(one, text, (size_t)n); one[n] = '\0';
        if (replace) (void)xvim_replace_char(vim, one);
        else (void)xvim_insert_text(vim, one);
        text += n;
    }
}

static void xvim_repeat(XTuiVim* vim)
{
    int count;
    if (!vim || !vim->m_lastRepeatLen) return;
    if (strncmp(vim->m_lastRepeat, "i:", 2) == 0) {
        xvim_repeat_text(vim, vim->m_lastRepeat + 2, false);
    }
    else if (strncmp(vim->m_lastRepeat, "R:", 2) == 0) {
        xvim_repeat_text(vim, vim->m_lastRepeat + 2, true);
    }
    else if (strncmp(vim->m_lastRepeat, "s:", 2) == 0) {
        if (xvim_begin_change(vim)) {
            xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn);
            xvim_repeat_text(vim, vim->m_lastRepeat + 2, false);
        }
    }
    else if (strncmp(vim->m_lastRepeat, "S:", 2) == 0) {
        if (xvim_begin_change(vim)) {
            xvim_set_line_raw(vim, vim->m_cursorLine, "");
            vim->m_cursorColumn = 0;
            xvim_repeat_text(vim, vim->m_lastRepeat + 2, false);
        }
    }
    else if (strncmp(vim->m_lastRepeat, "C:", 2) == 0) {
        if (xvim_begin_change(vim)) {
            xvim_delete_range(vim, vim->m_cursorLine, vim->m_cursorColumn,
                              xvim_chars(vim->m_lines[vim->m_cursorLine]));
            xvim_repeat_text(vim, vim->m_lastRepeat + 2, false);
        }
    }
    else if (strncmp(vim->m_lastRepeat, "o:", 2) == 0 ||
             strncmp(vim->m_lastRepeat, "O:", 2) == 0) {
        bool above = vim->m_lastRepeat[0] == 'O';
        if (xvim_begin_change(vim)) {
            int line = vim->m_cursorLine + (above ? 0 : 1);
            if (xvim_insert_line_raw(vim, line, "")) {
                vim->m_cursorLine = line; vim->m_cursorColumn = 0;
                xvim_repeat_text(vim, vim->m_lastRepeat + 2, false);
            }
        }
    }
    else if (strncmp(vim->m_lastRepeat, "x:", 2) == 0) {
        count = atoi(vim->m_lastRepeat + 2);
        if (count < 1) count = 1;
        if (xvim_begin_change(vim)) while (count-- > 0)
            xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn);
    }
    else if (strcmp(vim->m_lastRepeat, "x") == 0) { if (xvim_begin_change(vim)) { xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn); } }
    else if (strcmp(vim->m_lastRepeat, "dd") == 0) xvim_linewise_op(vim, 'd', 1);
    else if (strcmp(vim->m_lastRepeat, "dw") == 0) { vim->m_pendingOperator = 'd'; vim->m_opStartLine = vim->m_cursorLine; vim->m_opStartColumn = vim->m_cursorColumn; xvim_operator_motion(vim, 'w', 1); }
    else if (strncmp(vim->m_lastRepeat, "r:", 2) == 0) {
        if (vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine])) {
            if (xvim_begin_change(vim)) { xvim_replace_range(vim, vim->m_cursorLine, vim->m_cursorColumn, vim->m_cursorColumn + 1, vim->m_lastRepeat + 2); vim->m_modified = true; }
        }
    }
#if XTUI_VIM_YANK_PASTE_ON
    else if (strcmp(vim->m_lastRepeat, "p") == 0) xvim_paste(vim, false);
    else if (strcmp(vim->m_lastRepeat, "P") == 0) xvim_paste(vim, true);
#endif
    else if (strcmp(vim->m_lastRepeat, "D") == 0) {
        if (xvim_begin_change(vim))
            xvim_delete_range(vim, vim->m_cursorLine, vim->m_cursorColumn,
                              xvim_chars(vim->m_lines[vim->m_cursorLine]));
    }
    else if (strcmp(vim->m_lastRepeat, "J") == 0) {
        if (vim->m_cursorLine + 1 < vim->m_lineCount && xvim_begin_change(vim)) {
            const char* first = vim->m_lines[vim->m_cursorLine];
            const char* second = vim->m_lines[vim->m_cursorLine + 1];
            size_t n = strlen(first) + strlen(second) + (first[0] && second[0] ? 2u : 1u);
            char* out = (char*)XMalloc_System(n + 1u);
            if (out) {
                strcpy(out, first); if (first[0] && second[0]) strcat(out, " ");
                strcat(out, second); xvim_set_line_raw(vim, vim->m_cursorLine, out);
                XFree_System(out); xvim_delete_line_raw(vim, vim->m_cursorLine + 1);
            }
        }
    }
    else if (strcmp(vim->m_lastRepeat, "~") == 0) {
        int ch = xvim_char_at(vim->m_lines[vim->m_cursorLine], vim->m_cursorColumn);
        if (ch && xvim_begin_change(vim)) {
            char changed[2] = { (char)(islower((unsigned char)ch) ?
                                       toupper((unsigned char)ch) :
                                       tolower((unsigned char)ch)), '\0' };
            (void)xvim_replace_range(vim, vim->m_cursorLine, vim->m_cursorColumn,
                                     vim->m_cursorColumn + 1, changed);
        }
    }
}
#endif

/* ==================== 渲染与按键入口 ==================== */

static bool xvim_visual_active(const XTuiVim* vim)
{
#if XTUI_VIM_VISUAL_ON
    return vim && vim->m_visualMode;
#else
    (void)vim;
    return false;
#endif
}

static bool xvim_search_active(const XTuiVim* vim)
{
#if XTUI_VIM_SEARCH_ON
    return vim && vim->m_searchMode;
#else
    (void)vim;
    return false;
#endif
}

static bool xvim_selected(const XTuiVim* vim, int line, int col)
{
#if XTUI_VIM_VISUAL_ON
    int a, b, c, d;
    if (!vim->m_visualMode) return false;
    if (vim->m_visualLineWise) return line >= (vim->m_visualStartLine < vim->m_cursorLine ? vim->m_visualStartLine : vim->m_cursorLine) && line <= (vim->m_visualStartLine > vim->m_cursorLine ? vim->m_visualStartLine : vim->m_cursorLine);
    if (vim->m_visualBlock) {
        int top = vim->m_visualStartLine < vim->m_cursorLine ? vim->m_visualStartLine : vim->m_cursorLine;
        int bottom = vim->m_visualStartLine > vim->m_cursorLine ? vim->m_visualStartLine : vim->m_cursorLine;
        int left = vim->m_visualStartColumn < vim->m_cursorColumn ? vim->m_visualStartColumn : vim->m_cursorColumn;
        int right = vim->m_visualStartColumn > vim->m_cursorColumn ? vim->m_visualStartColumn : vim->m_cursorColumn;
        return line >= top && line <= bottom && col >= left && col <= right;
    }
    xvim_order(vim->m_visualStartLine, vim->m_visualStartColumn, vim->m_cursorLine, vim->m_cursorColumn, &a, &b, &c, &d);
    if (line < a || line > c) return false;
    if (a == c) return col >= b && col <= d;
    if (line == a) return col >= b;
    if (line == c) return col <= d;
    return true;
#else
    (void)vim;
    (void)line;
    (void)col;
    return false;
#endif
}

static bool VXTuiVim_render(XTuiWidget* base, XTuiScreen* screen)
{
    XTuiVim* vim = (XTuiVim*)base;
    XRect r; int y, x, body, numberWidth = 0;
    if (!vim || !screen) return false;
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
    XRegularExpression* searchExpression = NULL;
    if (vim && vim->m_searchHighlight && vim->m_lastSearch[0]) {
        searchExpression = XRegularExpression_create_utf8(
            vim->m_lastSearch, XRegularExpression_UseUnicodePropertiesOption);
        if (searchExpression && !XRegularExpression_isValid(searchExpression)) {
            xvim_delete_regular_expression(searchExpression);
            searchExpression = NULL;
        }
    }
#endif
    r = XTuiWidget_rect(base);
    if (r.width < 1 || r.height < 2) {
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
        xvim_delete_regular_expression(searchExpression);
#endif
        return false;
    }
    body = r.height - 1;
    xvim_ensure_cursor_visible(vim);
    if (vim->m_showLineNumbers) numberWidth = 6;
    for (y = 0; y < body; ++y) {
        int line = vim->m_topLine + y, col = 0, pos = 0, chars = 0;
        const char* text = line >= 0 && line < vim->m_lineCount ? vim->m_lines[line] : "";
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
        XRegularExpressionMatchIterator* searchMatches = NULL;
        XRegularExpressionMatch* searchMatch = NULL;
        int searchStart = -1;
        int searchEnd = -1;
        if (searchExpression && line >= 0 && line < vim->m_lineCount) {
            searchMatches = XRegularExpression_globalMatch_utf8(
                searchExpression, text, 0, XRegularExpression_NormalMatch,
                XRegularExpression_NoMatchOption);
            if (searchMatches && XRegularExpressionMatchIterator_hasNext(searchMatches)) {
                searchMatch = XRegularExpressionMatchIterator_next(searchMatches);
                if (searchMatch) {
                    searchStart = xvim_column_from_utf16(
                        text, XRegularExpressionMatch_capturedStart(searchMatch, 0));
                    searchEnd = xvim_column_from_utf16(
                        text, XRegularExpressionMatch_capturedEnd(searchMatch, 0));
                }
            }
        }
#endif
        for (x = 0; x < r.width; ++x) XTuiScreen_setCell(screen, r.x + x, r.y + y, " ", XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, 0);
        if (vim->m_showLineNumbers && line >= 0) {
            char no[16]; int n = snprintf(no, sizeof(no), "%4d ", line + 1); int i;
            if (n > numberWidth) n = numberWidth;
            for (i = 0; i < n && i < r.width; ++i) XTuiScreen_setCell(screen, r.x + i, r.y + y, (char[]){no[i], '\0'}, XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, 0);
            col = n;
        }
        while (text[pos] && col < r.width) {
            int clen = xvim_utf8_len((unsigned char)text[pos]); char cell[XTUI_CELL_UTF8_MAX + 1]; int attr = 0;
            if (clen > XTUI_CELL_UTF8_MAX) clen = 1; memset(cell, 0, sizeof(cell)); memcpy(cell, text + pos, (size_t)clen);
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
            while (searchMatch && searchEnd <= chars) {
                xvim_delete_regular_match(searchMatch);
                searchMatch = NULL;
                searchStart = searchEnd = -1;
                if (searchMatches && XRegularExpressionMatchIterator_hasNext(searchMatches)) {
                    searchMatch = XRegularExpressionMatchIterator_next(searchMatches);
                    if (searchMatch) {
                        searchStart = xvim_column_from_utf16(
                            text, XRegularExpressionMatch_capturedStart(searchMatch, 0));
                        searchEnd = xvim_column_from_utf16(
                            text, XRegularExpressionMatch_capturedEnd(searchMatch, 0));
                    }
                }
            }
            if (searchMatch && chars >= searchStart && chars < searchEnd)
                attr |= (int)XTuiAttribute_Underline;
#endif
            if (xvim_selected(vim, line, chars) || (line == vim->m_cursorLine && chars == vim->m_cursorColumn)) attr |= (int)XTuiAttribute_Reverse;
            XTuiScreen_setCell(screen, r.x + col, r.y + y, cell, XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, attr);
            pos += clen; ++col; ++chars;
        }
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
        xvim_delete_regular_match(searchMatch);
        xvim_delete_regular_iterator(searchMatches);
#endif
        if (line == vim->m_cursorLine && chars == vim->m_cursorColumn && col < r.width)
            XTuiScreen_setCell(screen, r.x + col, r.y + y, " ", XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, (int)XTuiAttribute_Reverse);
    }
    for (x = 0; x < r.width; ++x) XTuiScreen_setCell(screen, r.x + x, r.y + body, " ", XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, (int)XTuiAttribute_Reverse);
    {
        char status[XTUI_VIM_STATUS_MAX + 1];
        const char* mode;
        int n, pos, column;
#if XTUI_VIM_SEARCH_ON
        if (vim->m_searchMode) mode = vim->m_searchBackward ? "?" : "/";
        else
#endif
        if (vim->m_commandMode) mode = ":";
#if XTUI_VIM_VISUAL_ON
        else if (vim->m_visualMode)
            mode = vim->m_visualLineWise ? "-- 可视行 --" :
                   (vim->m_visualBlock ? "-- 可视块 --" : "-- 可视 --");
#endif
        else mode = vim->m_insertMode ?
                    (vim->m_replaceMode ? "-- 替换 --" : "-- 插入 --") :
                    "-- 命令 --";
        if (vim->m_commandMode) n = snprintf(status, sizeof(status), ":%s", vim->m_command);
#if XTUI_VIM_SEARCH_ON
        else if (vim->m_searchMode) n = snprintf(status, sizeof(status), "%s%s", mode, vim->m_search);
#endif
        else n = snprintf(status, sizeof(status), "%s %s%s  行 %d/%d 列 %d", mode, vim->m_path ? vim->m_path : "", vim->m_modified ? " [已修改]" : "", vim->m_cursorLine + 1, vim->m_lineCount, vim->m_cursorColumn + 1);
        if (n < 0) status[0] = '\0';
        else if ((size_t)n >= sizeof(status)) {
            pos = (int)sizeof(status) - 1;
            while (pos > 0 && ((unsigned char)status[pos] & 0xc0) == 0x80) --pos;
            status[pos] = '\0';
        }
        /* XTuiScreen 的一个单元保存一个完整 UTF-8 字符，不能逐字节写状态栏。 */
        for (pos = 0, column = 0; status[pos] && column < r.width; ++column) {
            int length = xvim_utf8_len((unsigned char)status[pos]);
            char cell[XTUI_CELL_UTF8_MAX + 1];
            int i;
            for (i = 1; i < length && status[pos + i]; ++i) { }
            if (i < length) length = 1;
            if (length > XTUI_CELL_UTF8_MAX) length = 1;
            memset(cell, 0, sizeof(cell));
            memcpy(cell, status + pos, (size_t)length);
            XTuiScreen_setCell(screen, r.x + column, r.y + body, cell,
                               XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT,
                               (int)XTuiAttribute_Reverse);
            pos += length;
        }
    }
    {
        int cx = r.x + numberWidth + vim->m_cursorColumn; int cy = r.y + vim->m_cursorLine - vim->m_topLine;
        if (cx < r.x) cx = r.x; if (cx >= r.x + r.width) cx = r.x + r.width - 1;
        if (cy < r.y) cy = r.y; if (cy >= r.y + body) cy = r.y + body - 1;
        XTuiScreen_setCursor(screen, cx, cy);
    }
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON
    xvim_delete_regular_expression(searchExpression);
#endif
    return true;
}

static bool VXTuiVim_keyPress(XTuiWidget* base, const XTuiKeyEvent* event)
{
    XTuiVim* vim = (XTuiVim*)base;
    char c;
    int count;
    if (!vim || !event) return false;
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    if (vim->m_substituteConfirm) {
        if (event->m_keyType == XTuiKey_Escape) c = 'q';
        else if (event->m_keyType == XTuiKey_Enter) c = 'y';
        else if (event->m_keyType == XTuiKey_Char) c = event->m_utf8[0];
        else return true;
        if (c == 'q' || c == 'Q') {
            vim->m_substituteConfirm = false;
            snprintf(vim->m_status, sizeof(vim->m_status), "替换已停止");
            return true;
        }
        if (c == 'n' || c == 'N') {
            xvim_substitute_confirm_skip(vim);
            (void)xvim_substitute_confirm_find(vim);
            return true;
        }
        if (c == 'y' || c == 'Y' || c == 'l' || c == 'L' || c == 'a' || c == 'A') {
            bool all = c == 'a' || c == 'A';
            bool last = c == 'l' || c == 'L';
            int safety = vim->m_lineCount * XTUI_VIM_LINE_MAX;
            do {
                if (!xvim_substitute_confirm_replace(vim)) {
                    vim->m_substituteConfirm = false;
                    snprintf(vim->m_status, sizeof(vim->m_status), "替换失败");
                    return true;
                }
                if (last) {
                    vim->m_substituteConfirm = false;
                    snprintf(vim->m_status, sizeof(vim->m_status), "替换完成");
                    return true;
                }
                if (!xvim_substitute_confirm_find(vim)) return true;
            } while (all && --safety > 0);
            return true;
        }
        return true;
    }
#endif
#if XTUI_VIM_MACRO_ON
    if (!vim->m_commandMode && !xvim_search_active(vim) && !vim->m_insertMode &&
        !xvim_visual_active(vim) && event->m_keyType == XTuiKey_Char &&
        vim->m_macroRecording && event->m_utf8[0] == 'q' &&
        vim->m_pendingNormal == '\0') {
        vim->m_macroRecording = false;
        vim->m_pendingNormal = '\0';
        snprintf(vim->m_status, sizeof(vim->m_status), "宏录制结束");
        return true;
    }
    if (!vim->m_commandMode && !xvim_search_active(vim) && !vim->m_insertMode &&
        !xvim_visual_active(vim) && vim->m_pendingNormal == 'q' &&
        event->m_keyType == XTuiKey_Char) {
        char name = event->m_utf8[0];
        vim->m_pendingNormal = '\0';
        if (name >= 'a' && name <= 'z') {
            int index = name - 'a';
            vim->m_macroRegister = name;
            vim->m_macroLengths[index] = 0;
            vim->m_macros[index][0] = '\0';
            vim->m_macroRecording = true;
            snprintf(vim->m_status, sizeof(vim->m_status), "录制宏 @%c", name);
        }
        return true;
    }
    if (!vim->m_commandMode && !xvim_search_active(vim) && !vim->m_insertMode &&
        !xvim_visual_active(vim) && vim->m_pendingNormal == '@' &&
        event->m_keyType == XTuiKey_Char) {
        char name = event->m_utf8[0];
        vim->m_pendingNormal = '\0';
        if (name == '@' || (name >= 'a' && name <= 'z')) xvim_macro_play(vim, name);
        return true;
    }
    xvim_macro_record_event(vim, event);
#endif
    if (vim->m_commandMode) {
        if (event->m_keyType == XTuiKey_Char) { size_t n = strlen(event->m_utf8); if (vim->m_commandLen + (int)n < (int)sizeof(vim->m_command)) { memcpy(vim->m_command + vim->m_commandLen, event->m_utf8, n); vim->m_commandLen += (int)n; vim->m_command[vim->m_commandLen] = '\0'; } return true; }
        if (event->m_keyType == XTuiKey_Backspace) { if (vim->m_commandLen > 0) vim->m_command[--vim->m_commandLen] = '\0'; return true; }
#if XTUI_VIM_HISTORY_ON
        if (event->m_keyType == XTuiKey_ArrowUp) { xvim_history_previous(vim->m_commandHistory, vim->m_commandHistoryLen, &vim->m_commandHistoryIndex, vim->m_command, &vim->m_commandLen); return true; }
        if (event->m_keyType == XTuiKey_ArrowDown) { xvim_history_next(vim->m_commandHistory, vim->m_commandHistoryLen, &vim->m_commandHistoryIndex, vim->m_command, &vim->m_commandLen); return true; }
#endif
        if (event->m_keyType == XTuiKey_Enter) { xvim_execute_command(vim); return true; }
        if (event->m_keyType == XTuiKey_Escape) { xvim_leave_command(vim); return true; }
        return true;
    }
#if XTUI_VIM_SEARCH_ON
    if (vim->m_searchMode) {
        if (event->m_keyType == XTuiKey_Char) { size_t n = strlen(event->m_utf8); if (vim->m_searchLen + (int)n < (int)sizeof(vim->m_search)) { memcpy(vim->m_search + vim->m_searchLen, event->m_utf8, n); vim->m_searchLen += (int)n; vim->m_search[vim->m_searchLen] = '\0'; } return true; }
        if (event->m_keyType == XTuiKey_Backspace) { if (vim->m_searchLen > 0) vim->m_search[--vim->m_searchLen] = '\0'; return true; }
#if XTUI_VIM_HISTORY_ON
        if (event->m_keyType == XTuiKey_ArrowUp) { xvim_history_previous(vim->m_searchHistory, vim->m_searchHistoryLen, &vim->m_searchHistoryIndex, vim->m_search, &vim->m_searchLen); return true; }
        if (event->m_keyType == XTuiKey_ArrowDown) { xvim_history_next(vim->m_searchHistory, vim->m_searchHistoryLen, &vim->m_searchHistoryIndex, vim->m_search, &vim->m_searchLen); return true; }
#endif
        if (event->m_keyType == XTuiKey_Enter) { xvim_finish_search(vim); return true; }
        if (event->m_keyType == XTuiKey_Escape) { vim->m_searchMode = false; return true; }
        return true;
    }
#endif
    if (vim->m_insertMode) {
        if (event->m_keyType == XTuiKey_Char) {
            if (event->m_modifiers & XKeyboardModifier_ControlModifier) {
                if (event->m_utf8[0] == 23) {
                    if (xvim_begin_change(vim)) xvim_insert_delete_previous_word(vim);
                    return true;
                }
                if (event->m_utf8[0] == 21) {
                    if (xvim_begin_change(vim)) xvim_insert_delete_to_start(vim);
                    return true;
                }
            }
#if XTUI_VIM_VISUAL_ON
            bool changed = vim->m_blockInsertMode ?
                xvim_insert_block_text(vim, event->m_utf8) :
                (vim->m_replaceMode ? xvim_replace_char(vim, event->m_utf8) :
                 xvim_insert_text(vim, event->m_utf8));
#else
            bool changed = vim->m_replaceMode ? xvim_replace_char(vim, event->m_utf8) :
                                                xvim_insert_text(vim, event->m_utf8);
#endif
            size_t n = strlen(event->m_utf8);
            if (changed && vim->m_insertLen + (int)n < (int)sizeof(vim->m_insertBuf)) {
                memcpy(vim->m_insertBuf + vim->m_insertLen, event->m_utf8, n);
                vim->m_insertLen += (int)n;
                vim->m_insertBuf[vim->m_insertLen] = '\0';
            }
            return changed;
        }
        if (event->m_keyType == XTuiKey_Enter) { xvim_begin_change(vim); return xvim_split(vim); }
        if (event->m_keyType == XTuiKey_Backspace) {
#if XTUI_VIM_VISUAL_ON
            if (vim->m_blockInsertMode) { xvim_begin_change(vim); xvim_delete_block_insert_previous(vim); }
            else
#endif
            if (vim->m_cursorColumn > 0) { xvim_begin_change(vim); --vim->m_cursorColumn; xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn); }
            else xvim_begin_change(vim), xvim_join_previous(vim);
            return true;
        }
        if (event->m_keyType == XTuiKey_Delete) { xvim_begin_change(vim); xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn); return true; }
        if (event->m_keyType == XTuiKey_ArrowLeft) { if (vim->m_cursorColumn > 0) --vim->m_cursorColumn; return true; }
        if (event->m_keyType == XTuiKey_ArrowRight) { if (vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine])) ++vim->m_cursorColumn; return true; }
        if (event->m_keyType == XTuiKey_ArrowUp) { xvim_goto(vim, vim->m_cursorLine - 1, vim->m_cursorColumn); return true; }
        if (event->m_keyType == XTuiKey_ArrowDown) { xvim_goto(vim, vim->m_cursorLine + 1, vim->m_cursorColumn); return true; }
        if (event->m_keyType == XTuiKey_PageUp) { xvim_page(vim, -1); return true; }
        if (event->m_keyType == XTuiKey_PageDown) { xvim_page(vim, 1); return true; }
        if (event->m_keyType == XTuiKey_Home) { vim->m_cursorColumn = 0; return true; }
        if (event->m_keyType == XTuiKey_End) { vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]); return true; }
        if (event->m_keyType == XTuiKey_Escape) { xvim_leave_insert(vim);
#if XTUI_VIM_VISUAL_ON
            vim->m_blockInsertMode = false;
#endif
            return true; }
        return true;
    }
#if XTUI_VIM_VISUAL_ON
    if (vim->m_visualMode) {
        if (event->m_keyType == XTuiKey_Escape) { xvim_finish_visual(vim); return true; }
        if (event->m_keyType != XTuiKey_Char) return true;
        c = event->m_utf8[0];
        if (c == 'h' || c == 'j' || c == 'k' || c == 'l'
#if XTUI_VIM_ADVANCED_MOTION_ON
            || c == 'w' || c == 'b'
#endif
           ) { int line, col; bool inc; xvim_motion_target(vim, c, &line, &col, &inc); vim->m_cursorLine = line; vim->m_cursorColumn = col; return true; }
        if (c == 'd'
#if XTUI_VIM_YANK_PASTE_ON
            || c == 'y'
#endif
#if XTUI_VIM_REPLACE_ON
            || c == 'c'
#endif
           ) {
           int sl = vim->m_visualStartLine, sc = vim->m_visualStartColumn;
           int el = vim->m_cursorLine, ec = vim->m_cursorColumn;
            bool captured = vim->m_visualBlock ?
                xvim_yank_block(vim, sl, sc, el, ec) :
                xvim_yank_range(vim, sl, sc, el, ec, vim->m_visualLineWise);
#if XTUI_VIM_REGISTER_ON
            if (c == 'y' && captured) xvim_set_numbered_yank(vim);
#endif
            if (c != 'y' && xvim_begin_change(vim)) {
#if XTUI_VIM_REGISTER_ON
                if (c == 'd' && captured) xvim_set_numbered_delete(vim);
#endif
                if (vim->m_visualBlock) {
                    xvim_delete_block(vim, sl, sc, el, ec);
                    vim->m_modified = true;
#if XTUI_VIM_REPLACE_ON
                    if (c == 'c') {
                        int a, b, d, unused;
                        xvim_order(sl, sc, el, ec, &a, &b, &unused, &d);
                        vim->m_blockInsertMode = true;
                        vim->m_blockInsertFirstLine = a;
                        vim->m_blockInsertLastLine = unused;
                        vim->m_blockInsertColumn = b < d ? b : d;
                        vim->m_cursorLine = a;
                        vim->m_cursorColumn = vim->m_blockInsertColumn;
                    }
#endif
                } else if (vim->m_visualLineWise) {
                    int a = sl < el ? sl : el, b = sl > el ? sl : el, i;
                    for (i = a; i <= b; ++i) xvim_delete_line_raw(vim, a);
                    vim->m_cursorLine = a < vim->m_lineCount ? a : vim->m_lineCount - 1;
                    vim->m_cursorColumn = 0; vim->m_modified = true;
                } else {
                    xvim_delete_selection(vim, sl, sc, el, ec);
                    vim->m_modified = true;
                }
            }
            xvim_finish_visual(vim);
            if (c == 'c') { xvim_set_insert_repeat(vim, "i:"); xvim_enter_insert(vim, false, true); }
            return true;
        }
        if (vim->m_visualBlock && (c == 'I' || c == 'A')) {
            int a, b, d, last;
            xvim_order(vim->m_visualStartLine, vim->m_visualStartColumn,
                       vim->m_cursorLine, vim->m_cursorColumn, &a, &b, &last, &d);
            vim->m_blockInsertMode = true;
            vim->m_blockInsertFirstLine = a;
            vim->m_blockInsertLastLine = last;
            vim->m_blockInsertColumn = c == 'I' ? (b < d ? b : d) :
                                                  (b > d ? b : d) + 1;
            vim->m_cursorLine = a;
            vim->m_cursorColumn = vim->m_blockInsertColumn;
            xvim_finish_visual(vim);
            xvim_set_insert_repeat(vim, "i:");
            xvim_enter_insert(vim, false, false);
            return true;
        }
#if XTUI_VIM_YANK_PASTE_ON
        if (c == 'p' || c == 'P') { xvim_finish_visual(vim); xvim_paste(vim, c == 'P'); return true; }
#endif
        return true;
    }
#endif
    xvim_normalize_cursor(vim);
    if (event->m_keyType == XTuiKey_ArrowLeft) { if (vim->m_cursorColumn > 0) --vim->m_cursorColumn; return true; }
    if (event->m_keyType == XTuiKey_ArrowRight) { int length = xvim_chars(vim->m_lines[vim->m_cursorLine]); if (length > 0 && vim->m_cursorColumn + 1 < length) ++vim->m_cursorColumn; return true; }
    if (event->m_keyType == XTuiKey_ArrowUp) { xvim_goto(vim, vim->m_cursorLine - 1, vim->m_cursorColumn); return true; }
    if (event->m_keyType == XTuiKey_ArrowDown) { xvim_goto(vim, vim->m_cursorLine + 1, vim->m_cursorColumn); return true; }
    if (event->m_keyType == XTuiKey_PageUp) { xvim_page(vim, -1); return true; }
    if (event->m_keyType == XTuiKey_PageDown) { xvim_page(vim, 1); return true; }
    if (event->m_keyType == XTuiKey_Home) { vim->m_cursorColumn = 0; return true; }
    if (event->m_keyType == XTuiKey_End) { int length = xvim_chars(vim->m_lines[vim->m_cursorLine]); vim->m_cursorColumn = length > 0 ? length - 1 : 0; return true; }
    if (event->m_keyType == XTuiKey_Escape) { vim->m_pendingOperator = '\0'; vim->m_pendingNormal = '\0'; vim->m_count = 0; return true; }
    if (event->m_keyType != XTuiKey_Char) return false;
    c = event->m_utf8[0];
    if (event->m_modifiers & XKeyboardModifier_ControlModifier) {
#if XTUI_VIM_UNDO_REDO_ON
        if (c == 18) { xvim_redo(vim); return true; }
#endif
#if XTUI_VIM_VISUAL_ON
        if (c == 22) {
            vim->m_visualMode = true; vim->m_visualLineWise = false;
            vim->m_visualBlock = true; vim->m_visualStartLine = vim->m_cursorLine;
            vim->m_visualStartColumn = vim->m_cursorColumn;
            return true;
        }
#endif
#if XTUI_VIM_JUMPLIST_ON
        if (c == 15) { (void)xvim_jump_navigate(vim, false); return true; }
        if (c == 9) { (void)xvim_jump_navigate(vim, true); return true; }
#endif
        if (c == 4) { xvim_half_page(vim, 1); return true; }
        if (c == 21) { xvim_half_page(vim, -1); return true; }
        if (c == 6) { xvim_page(vim, 1); return true; }
        if (c == 2) { xvim_page(vim, -1); return true; }
        if (c == 5) { xvim_scroll_viewport(vim, 1); return true; }
        if (c == 25) { xvim_scroll_viewport(vim, -1); return true; }
    }
#if XTUI_VIM_REGISTER_ON
    if (vim->m_pendingNormal == '"') {
        vim->m_pendingNormal = '\0';
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            vim->m_activeRegister = (char)tolower((unsigned char)c);
            vim->m_appendRegister = c >= 'A' && c <= 'Z';
        }
        return true;
    }
#endif
#if XTUI_VIM_MARK_ON
    if (vim->m_pendingNormal == 'm') {
        vim->m_pendingNormal = '\0';
        xvim_set_mark(vim, c);
        return true;
    }
    if (vim->m_pendingNormal == '\'' || vim->m_pendingNormal == '`') {
        bool lineWise = vim->m_pendingNormal == '\'';
        vim->m_pendingNormal = '\0';
        xvim_jump_mark(vim, c, lineWise);
        return true;
    }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (vim->m_pendingNormal == 'f' || vim->m_pendingNormal == 'F' || vim->m_pendingNormal == 't' || vim->m_pendingNormal == 'T') { xvim_find_char(vim, c, vim->m_pendingNormal == 'F' || vim->m_pendingNormal == 'T', vim->m_pendingNormal == 't' || vim->m_pendingNormal == 'T'); vim->m_pendingNormal = '\0'; return true; }
#endif
#if XTUI_VIM_REPLACE_ON
    if (vim->m_pendingNormal == 'r') {
        vim->m_pendingNormal = '\0';
        if (vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine]) &&
            xvim_begin_change(vim)) {
            if (xvim_replace_range(vim, vim->m_cursorLine, vim->m_cursorColumn,
                                   vim->m_cursorColumn + 1, event->m_utf8)) {
                vim->m_modified = true;
                {
                    char repeat[XTUI_VIM_CMD_MAX + 1];
                    snprintf(repeat, sizeof(repeat), "r:%s", event->m_utf8);
                    xvim_record(vim, repeat);
                }
            }
        }
        return true;
    }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (vim->m_pendingNormal == 'g') { int target = vim->m_count > 0 ? vim->m_count - 1 : 0; vim->m_pendingNormal = '\0'; vim->m_count = 0; if (c == 'g') {
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        xvim_goto(vim, target, 0); } return true; }
    if (vim->m_pendingNormal == 'z') {
        XRect rect = XTuiWidget_rect((XTuiWidget*)vim);
        int body = rect.height > 1 ? rect.height - 1 : 1;
        vim->m_pendingNormal = '\0';
        if (c == 't') vim->m_topLine = vim->m_cursorLine;
        else if (c == 'z') vim->m_topLine = vim->m_cursorLine - body / 2;
        else if (c == 'b') vim->m_topLine = vim->m_cursorLine - body + 1;
        else return true;
        xvim_ensure_cursor_visible(vim);
        return true;
    }
    if (vim->m_pendingNormal == 'i' || vim->m_pendingNormal == 'a') {
        if (c == 'w' || c == 'W' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '<' || c == '>' || c == '"' || c == '\'') {
            int start, end; char op = vim->m_pendingOperator;
            char objectMode = vim->m_pendingNormal;
            bool object = c == 'w' || c == 'W' ?
                (xvim_word_object(vim, vim->m_pendingNormal == 'a', &start, &end), true) :
                xvim_pair_object(vim, c, vim->m_pendingNormal == 'a', &start, &end);
            vim->m_pendingNormal = '\0'; vim->m_pendingOperator = '\0';
            if (!object) return true;
            if (op == 'y') {
                bool captured = xvim_yank_range(vim, vim->m_cursorLine, start,
                                                vim->m_cursorLine, end - 1, false);
#if XTUI_VIM_REGISTER_ON
                if (captured) xvim_set_numbered_yank(vim);
#endif
            }
            else if (xvim_begin_change(vim)) {
                xvim_delete_range(vim, vim->m_cursorLine, start, end);
                vim->m_cursorColumn = start; vim->m_modified = true;
                if (op == 'c') {
#if XTUI_VIM_ADVANCED_MOTION_ON
                    char prefix[8];
                    snprintf(prefix, sizeof(prefix), "c%c%c:", objectMode, c);
                    xvim_set_insert_repeat(vim, prefix);
#endif
                    xvim_enter_insert(vim, false, true);
                }
            }
            return true;
        }
        vim->m_pendingNormal = '\0'; vim->m_pendingOperator = '\0';
        return true;
    }
#endif
    if (vim->m_pendingOperator) {
        if (c == vim->m_pendingOperator) { char op = vim->m_pendingOperator; int n = vim->m_count ? vim->m_count : 1; vim->m_pendingOperator = '\0'; xvim_linewise_op(vim, op, n); xvim_record(vim, op == 'd' ? "dd" : (op == 'y' ? "yy" : "cc")); vim->m_count = 0; return true; }
#if XTUI_VIM_ADVANCED_MOTION_ON
        if (c == 'i' || c == 'a') { vim->m_pendingNormal = c; return true; }
        if (c >= '1' && c <= '9') { vim->m_count = vim->m_count * 10 + (c - '0'); return true; }
        count = vim->m_count ? vim->m_count : 1; xvim_operator_motion(vim, c, count); vim->m_count = 0; return true;
#else
        vim->m_pendingOperator = '\0';
        return true;
#endif
    }
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (c >= '1' && c <= '9') { vim->m_count = vim->m_count * 10 + (c - '0'); return true; }
    count = vim->m_count ? vim->m_count : 1; vim->m_count = 0;
#else
    count = 1;
#endif
    if (c == '0') { if (count > 1) vim->m_count = count * 10; else vim->m_cursorColumn = 0; return true; }
    if (c == 'h' || c == 'l') { while (count-- > 0) { int length = xvim_chars(vim->m_lines[vim->m_cursorLine]); if (c == 'h' && vim->m_cursorColumn > 0) --vim->m_cursorColumn; if (c == 'l' && length > 0 && vim->m_cursorColumn + 1 < length) ++vim->m_cursorColumn; } return true; }
    if (c == 'j' || c == 'k') { while (count-- > 0) xvim_goto(vim, vim->m_cursorLine + (c == 'j' ? 1 : -1), vim->m_cursorColumn); return true; }
    if (c == '$') { int length = xvim_chars(vim->m_lines[vim->m_cursorLine]); vim->m_cursorColumn = length > 0 ? length - 1 : 0; return true; }
    if (c == '^') { vim->m_cursorColumn = xvim_first_nonblank(vim->m_lines[vim->m_cursorLine]); return true; }
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (c == 'w' || c == 'W' || c == 'e' || c == 'E') { while (count-- > 0) xvim_word_forward(vim, c == 'e' || c == 'E', c == 'W' || c == 'E'); return true; }
    if (c == 'b' || c == 'B') { while (count-- > 0) xvim_word_backward(vim, c == 'B'); return true; }
    if (c == 'H') { xvim_goto(vim, vim->m_topLine, vim->m_cursorColumn); return true; }
    if (c == 'M') { XRect rect = XTuiWidget_rect((XTuiWidget*)vim); int body = rect.height > 1 ? rect.height - 1 : 1; xvim_goto(vim, vim->m_topLine + body / 2, vim->m_cursorColumn); return true; }
    if (c == 'L') { XRect rect = XTuiWidget_rect((XTuiWidget*)vim); int body = rect.height > 1 ? rect.height - 1 : 1; xvim_goto(vim, vim->m_topLine + body - 1, vim->m_cursorColumn); return true; }
    if (c == 'G') {
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        xvim_goto(vim, count > 1 ? count - 1 : vim->m_lineCount - 1, vim->m_cursorColumn); return true; }
    if (c == 'g') { vim->m_pendingNormal = 'g'; vim->m_count = count > 1 ? count : 0; return true; }
    if (c == 'f' || c == 'F' || c == 't' || c == 'T') { vim->m_pendingNormal = c; return true; }
    if (c == ';' || c == ',') { xvim_find_char(vim, vim->m_findChar, c == ',' ? !vim->m_findBackward : vim->m_findBackward, vim->m_findTill); return true; }
#endif
#if XTUI_VIM_VISUAL_ON
    if (c == 'v' || c == 'V') { vim->m_visualMode = true; vim->m_visualLineWise = c == 'V'; vim->m_visualBlock = false; vim->m_visualStartLine = vim->m_cursorLine; vim->m_visualStartColumn = vim->m_cursorColumn; return true; }
#endif
#if XTUI_VIM_REGISTER_ON
    if (c == '"') { vim->m_pendingNormal = '"'; return true; }
#endif
#if XTUI_VIM_MARK_ON
    if (c == 'm' || c == '\'' || c == '`') { vim->m_pendingNormal = c; return true; }
#endif
#if XTUI_VIM_MACRO_ON
    if (c == 'q' || c == '@') { vim->m_pendingNormal = c; return true; }
#endif
    if (c == 'i') { xvim_set_insert_repeat(vim, "i:"); xvim_enter_insert(vim, false, false); xvim_record(vim, "i"); return true; }
    if (c == 'a') { if (vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine])) ++vim->m_cursorColumn; xvim_set_insert_repeat(vim, "i:"); xvim_enter_insert(vim, false, false); xvim_record(vim, "a"); return true; }
    if (c == 'I') { vim->m_cursorColumn = xvim_first_nonblank(vim->m_lines[vim->m_cursorLine]); xvim_set_insert_repeat(vim, "i:"); xvim_enter_insert(vim, false, false); xvim_record(vim, "I"); return true; }
    if (c == 'A') { vim->m_cursorColumn = xvim_chars(vim->m_lines[vim->m_cursorLine]); xvim_set_insert_repeat(vim, "i:"); xvim_enter_insert(vim, false, false); xvim_record(vim, "A"); return true; }
#if XTUI_VIM_REPLACE_ON
    if (c == 'R') { xvim_set_insert_repeat(vim, "R:"); xvim_enter_insert(vim, true, false); xvim_record(vim, "R"); return true; }
    if (c == 'r') { vim->m_pendingNormal = 'r'; return true; }
    if (c == 's') { if (vim->m_cursorColumn < xvim_chars(vim->m_lines[vim->m_cursorLine]) && xvim_begin_change(vim)) { xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn); xvim_set_insert_repeat(vim, "s:"); xvim_enter_insert(vim, false, true); } return true; }
    if (c == 'S') { if (xvim_begin_change(vim)) { xvim_set_line_raw(vim, vim->m_cursorLine, ""); vim->m_cursorColumn = 0; vim->m_modified = true; xvim_set_insert_repeat(vim, "S:"); xvim_enter_insert(vim, false, true); } return true; }
    if (c == '~') { int ch = xvim_char_at(vim->m_lines[vim->m_cursorLine], vim->m_cursorColumn); if (ch && xvim_begin_change(vim)) { char changed[2] = { (char)(islower((unsigned char)ch) ? toupper((unsigned char)ch) : tolower((unsigned char)ch)), '\0' }; if (xvim_replace_range(vim, vim->m_cursorLine, vim->m_cursorColumn, vim->m_cursorColumn + 1, changed)) { vim->m_modified = true; if (vim->m_cursorColumn + 1 < xvim_chars(vim->m_lines[vim->m_cursorLine])) ++vim->m_cursorColumn; xvim_record(vim, "~"); } } return true; }
#endif
    if (c == 'o' || c == 'O') { if (xvim_begin_change(vim)) { int line = c == 'o' ? vim->m_cursorLine + 1 : vim->m_cursorLine; if (xvim_insert_line_raw(vim, line, "")) { vim->m_cursorLine = line; vim->m_cursorColumn = 0; vim->m_modified = true; xvim_set_insert_repeat(vim, c == 'o' ? "o:" : "O:"); xvim_enter_insert(vim, false, true); } } return true; }
    if (c == 'x') { if (xvim_begin_change(vim)) { int n = count; while (n-- > 0) xvim_delete_char(vim, vim->m_cursorLine, vim->m_cursorColumn); vim->m_modified = true; } {
        char repeat[24]; snprintf(repeat, sizeof(repeat), count > 1 ? "x:%d" : "x", count); xvim_record(vim, repeat);
    } return true; }
    if (c == 'd'
#if XTUI_VIM_REPLACE_ON
        || c == 'c'
#endif
#if XTUI_VIM_YANK_PASTE_ON
        || c == 'y'
#endif
       ) { vim->m_pendingOperator = c; vim->m_opStartLine = vim->m_cursorLine; vim->m_opStartColumn = vim->m_cursorColumn; vim->m_count = count > 1 ? count : 0; return true; }
#if XTUI_VIM_YANK_PASTE_ON
    if (c == 'p' || c == 'P') { xvim_paste(vim, c == 'P'); return true; }
#endif
#if XTUI_VIM_UNDO_REDO_ON
    if (c == 'u') { xvim_undo(vim); return true; }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (c == '.' ) { xvim_repeat(vim); return true; }
#endif
#if XTUI_VIM_SEARCH_ON
    if (c == '/') { xvim_start_search(vim, false); return true; }
    if (c == '?') { xvim_start_search(vim, true); return true; }
    if (c == 'n') { if (vim->m_lastSearch[0]) {
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        xvim_search(vim, vim->m_lastSearch, vim->m_searchBackward); } return true; }
    if (c == 'N') { if (vim->m_lastSearch[0]) {
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        xvim_search(vim, vim->m_lastSearch, !vim->m_searchBackward); } return true; }
    if (c == '*') { xvim_search_word(vim, false); return true; }
    if (c == '#') { xvim_search_word(vim, true); return true; }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (c == '%') {
#if XTUI_VIM_JUMPLIST_ON
        xvim_jump_push(vim);
#endif
        xvim_match_bracket(vim); return true; }
    if (c == 'z') { vim->m_pendingNormal = 'z'; return true; }
#endif
    if (c == ':') { vim->m_commandMode = true; vim->m_commandLen = 0; vim->m_command[0] = '\0';
#if XTUI_VIM_HISTORY_ON
        vim->m_commandHistoryIndex = vim->m_commandHistoryLen;
#endif
        return true; }
#if XTUI_VIM_ADVANCED_MOTION_ON
    if (c == 'D') { if (xvim_begin_change(vim)) { xvim_delete_range(vim, vim->m_cursorLine, vim->m_cursorColumn, xvim_chars(vim->m_lines[vim->m_cursorLine])); vim->m_modified = true; } xvim_record(vim, "D"); return true; }
#if XTUI_VIM_REPLACE_ON
    if (c == 'C') { if (xvim_begin_change(vim)) { xvim_delete_range(vim, vim->m_cursorLine, vim->m_cursorColumn, xvim_chars(vim->m_lines[vim->m_cursorLine])); vim->m_modified = true; xvim_set_insert_repeat(vim, "C:"); xvim_enter_insert(vim, false, true); } return true; }
#endif
    if (c == 'J') { if (vim->m_cursorLine + 1 < vim->m_lineCount && xvim_begin_change(vim)) { const char* first = vim->m_lines[vim->m_cursorLine]; const char* second = vim->m_lines[vim->m_cursorLine + 1]; bool needSpace = first[0] && second[0]; size_t n = strlen(first) + strlen(second) + (needSpace ? 1u : 0u); if (n < XTUI_VIM_LINE_MAX) { char* out = (char*)XMalloc_System(n + 1u); if (out) { strcpy(out, first); if (needSpace) strcat(out, " "); strcat(out, second); xvim_set_line_raw(vim, vim->m_cursorLine, out); XFree_System(out); xvim_delete_line_raw(vim, vim->m_cursorLine + 1); vim->m_modified = true; xvim_record(vim, "J"); } } } return true; }
#endif
    return true;
}

/* ==================== XClass 生命周期 ==================== */

static void VXTuiVim_deinit(XTuiVim* vim)
{
    if (!vim) return;
    xvim_free_lines(vim->m_lines, vim->m_lineCount); vim->m_lines = NULL;
#if XTUI_VIM_UNDO_REDO_ON
    xvim_clear_stack(vim->m_undoStack, vim->m_undoLen); vim->m_undoStack = NULL;
    xvim_clear_stack(vim->m_redoStack, vim->m_redoLen); vim->m_redoStack = NULL;
#endif
    xvim_clear_register(vim);
    if (vim->m_path) XFree_System(vim->m_path);
    vim->m_path = NULL; vim->m_lineCount = 0;
    XClass_Deinit_Parent(XTuiWidget, (XTuiWidget*)vim);
}

static void VXTuiVim_copy(XTuiVim* dest, const XTuiVim* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XTuiVim_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Copy, void(*)(XTuiWidget*, const XTuiWidget*))((XTuiWidget*)dest, (const XTuiWidget*)src);
    dest->m_lines = xvim_clone_lines(src->m_lines, src->m_lineCount, &dest->m_linesCapacity);
    dest->m_lineCount = src->m_lineCount;
    dest->m_cursorLine = src->m_cursorLine; dest->m_cursorColumn = src->m_cursorColumn; dest->m_topLine = src->m_topLine;
    dest->m_insertMode = src->m_insertMode; dest->m_replaceMode = src->m_replaceMode; dest->m_commandMode = src->m_commandMode; dest->m_modified = src->m_modified;
#if XTUI_VIM_SEARCH_ON
    dest->m_searchMode = src->m_searchMode; dest->m_searchBackward = src->m_searchBackward; dest->m_searchHighlight = src->m_searchHighlight;
#endif
    memcpy(dest->m_command, src->m_command, sizeof(dest->m_command)); dest->m_commandLen = src->m_commandLen;
#if XTUI_VIM_HISTORY_ON
    memcpy(dest->m_commandHistory, src->m_commandHistory, sizeof(dest->m_commandHistory)); dest->m_commandHistoryLen = src->m_commandHistoryLen; dest->m_commandHistoryIndex = src->m_commandHistoryIndex;
#endif
#if XTUI_VIM_SEARCH_ON
    memcpy(dest->m_search, src->m_search, sizeof(dest->m_search)); memcpy(dest->m_lastSearch, src->m_lastSearch, sizeof(dest->m_lastSearch)); dest->m_searchLen = src->m_searchLen; dest->m_lastSearchLen = src->m_lastSearchLen;
#if XTUI_VIM_HISTORY_ON
    memcpy(dest->m_searchHistory, src->m_searchHistory, sizeof(dest->m_searchHistory)); dest->m_searchHistoryLen = src->m_searchHistoryLen; dest->m_searchHistoryIndex = src->m_searchHistoryIndex;
#endif
#endif
    memcpy(dest->m_insertBuf, src->m_insertBuf, sizeof(dest->m_insertBuf)); dest->m_insertLen = src->m_insertLen; dest->m_insertCursor = src->m_insertCursor;
    memcpy(dest->m_status, src->m_status, sizeof(dest->m_status));
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    dest->m_substituteConfirm = src->m_substituteConfirm; dest->m_substituteGlobal = src->m_substituteGlobal; dest->m_substituteIgnoreCase = src->m_substituteIgnoreCase; dest->m_substituteFirstLine = src->m_substituteFirstLine; dest->m_substituteLastLine = src->m_substituteLastLine; dest->m_substituteLine = src->m_substituteLine; dest->m_substituteColumn = src->m_substituteColumn; dest->m_substituteEndColumn = src->m_substituteEndColumn; dest->m_substituteNextColumn = src->m_substituteNextColumn; memcpy(dest->m_substitutePattern, src->m_substitutePattern, sizeof(dest->m_substitutePattern)); memcpy(dest->m_substituteReplacement, src->m_substituteReplacement, sizeof(dest->m_substituteReplacement));
#endif
    dest->m_wantSave = src->m_wantSave; dest->m_wantQuit = src->m_wantQuit; dest->m_wantSaveQuit = src->m_wantSaveQuit;
#if XTUI_VIM_MULTIBUFFER_ON
    dest->m_wantEdit = src->m_wantEdit; dest->m_wantBufferNext = src->m_wantBufferNext; dest->m_wantBufferPrev = src->m_wantBufferPrev; dest->m_wantBufferList = src->m_wantBufferList; dest->m_wantBufferIndex = src->m_wantBufferIndex; dest->m_wantForce = src->m_wantForce; dest->m_wantWritePath = src->m_wantWritePath; dest->m_wantSaveAs = src->m_wantSaveAs; dest->m_wantWriteAll = src->m_wantWriteAll; dest->m_wantQuitAll = src->m_wantQuitAll; dest->m_wantBufferClose = src->m_wantBufferClose; memcpy(dest->m_actionPath, src->m_actionPath, sizeof(dest->m_actionPath));
#endif
    dest->m_pendingOperator = src->m_pendingOperator; dest->m_pendingNormal = src->m_pendingNormal; dest->m_count = src->m_count;
#if XTUI_VIM_VISUAL_ON
    dest->m_visualMode = src->m_visualMode; dest->m_visualLineWise = src->m_visualLineWise; dest->m_visualBlock = src->m_visualBlock; dest->m_visualStartLine = src->m_visualStartLine; dest->m_visualStartColumn = src->m_visualStartColumn; dest->m_blockInsertMode = src->m_blockInsertMode; dest->m_blockInsertFirstLine = src->m_blockInsertFirstLine; dest->m_blockInsertLastLine = src->m_blockInsertLastLine; dest->m_blockInsertColumn = src->m_blockInsertColumn;
#endif
#if XTUI_VIM_MARK_ON
    memcpy(dest->m_markLines, src->m_markLines, sizeof(dest->m_markLines)); memcpy(dest->m_markColumns, src->m_markColumns, sizeof(dest->m_markColumns));
#endif
#if XTUI_VIM_JUMPLIST_ON
    memcpy(dest->m_jumpLines, src->m_jumpLines, sizeof(dest->m_jumpLines)); memcpy(dest->m_jumpColumns, src->m_jumpColumns, sizeof(dest->m_jumpColumns)); dest->m_jumpLength = src->m_jumpLength; dest->m_jumpIndex = src->m_jumpIndex;
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    dest->m_findChar = src->m_findChar; dest->m_findBackward = src->m_findBackward; dest->m_findTill = src->m_findTill; memcpy(dest->m_lastRepeat, src->m_lastRepeat, sizeof(dest->m_lastRepeat)); dest->m_lastRepeatLen = src->m_lastRepeatLen;
    memcpy(dest->m_insertRepeatPrefix, src->m_insertRepeatPrefix, sizeof(dest->m_insertRepeatPrefix));
#endif
    dest->m_showLineNumbers = src->m_showLineNumbers;
    if (src->m_path) { dest->m_path = (char*)XMalloc_System(strlen(src->m_path) + 1u); if (dest->m_path) strcpy(dest->m_path, src->m_path); }
}

static void VXTuiVim_move(XTuiVim* dest, XTuiVim* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XTuiVim_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Move, void(*)(XTuiWidget*, XTuiWidget*))((XTuiWidget*)dest, (XTuiWidget*)src);
    dest->m_lines = src->m_lines; dest->m_lineCount = src->m_lineCount; dest->m_linesCapacity = src->m_linesCapacity; src->m_lines = NULL; src->m_lineCount = 0; src->m_linesCapacity = 0;
#if XTUI_VIM_UNDO_REDO_ON
    dest->m_undoStack = src->m_undoStack; dest->m_undoLen = src->m_undoLen; dest->m_undoCap = src->m_undoCap; src->m_undoStack = NULL; src->m_undoLen = src->m_undoCap = 0;
    dest->m_redoStack = src->m_redoStack; dest->m_redoLen = src->m_redoLen; dest->m_redoCap = src->m_redoCap; src->m_redoStack = NULL; src->m_redoLen = src->m_redoCap = 0;
#endif
#if XTUI_VIM_YANK_PASTE_ON
    dest->m_regLines = src->m_regLines; dest->m_regCount = src->m_regCount; dest->m_regLineWise = src->m_regLineWise; src->m_regLines = NULL; src->m_regCount = 0;
#endif
    dest->m_cursorLine = src->m_cursorLine; dest->m_cursorColumn = src->m_cursorColumn; dest->m_topLine = src->m_topLine; dest->m_insertMode = src->m_insertMode; dest->m_replaceMode = src->m_replaceMode; dest->m_commandMode = src->m_commandMode; dest->m_modified = src->m_modified; memcpy(dest->m_command, src->m_command, sizeof(dest->m_command)); dest->m_commandLen = src->m_commandLen;
#if XTUI_VIM_HISTORY_ON
    memcpy(dest->m_commandHistory, src->m_commandHistory, sizeof(dest->m_commandHistory)); dest->m_commandHistoryLen = src->m_commandHistoryLen; dest->m_commandHistoryIndex = src->m_commandHistoryIndex;
#endif
#if XTUI_VIM_SEARCH_ON
    dest->m_searchMode = src->m_searchMode; dest->m_searchBackward = src->m_searchBackward; dest->m_searchHighlight = src->m_searchHighlight; memcpy(dest->m_search, src->m_search, sizeof(dest->m_search)); memcpy(dest->m_lastSearch, src->m_lastSearch, sizeof(dest->m_lastSearch)); dest->m_searchLen = src->m_searchLen; dest->m_lastSearchLen = src->m_lastSearchLen;
#if XTUI_VIM_HISTORY_ON
    memcpy(dest->m_searchHistory, src->m_searchHistory, sizeof(dest->m_searchHistory)); dest->m_searchHistoryLen = src->m_searchHistoryLen; dest->m_searchHistoryIndex = src->m_searchHistoryIndex;
#endif
#endif
    memcpy(dest->m_insertBuf, src->m_insertBuf, sizeof(dest->m_insertBuf)); dest->m_insertLen = src->m_insertLen; dest->m_insertCursor = src->m_insertCursor; memcpy(dest->m_status, src->m_status, sizeof(dest->m_status));
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    dest->m_substituteConfirm = src->m_substituteConfirm; dest->m_substituteGlobal = src->m_substituteGlobal; dest->m_substituteIgnoreCase = src->m_substituteIgnoreCase; dest->m_substituteFirstLine = src->m_substituteFirstLine; dest->m_substituteLastLine = src->m_substituteLastLine; dest->m_substituteLine = src->m_substituteLine; dest->m_substituteColumn = src->m_substituteColumn; dest->m_substituteEndColumn = src->m_substituteEndColumn; dest->m_substituteNextColumn = src->m_substituteNextColumn; memcpy(dest->m_substitutePattern, src->m_substitutePattern, sizeof(dest->m_substitutePattern)); memcpy(dest->m_substituteReplacement, src->m_substituteReplacement, sizeof(dest->m_substituteReplacement));
#endif
    dest->m_path = src->m_path; src->m_path = NULL; dest->m_wantSave = src->m_wantSave; dest->m_wantQuit = src->m_wantQuit; dest->m_wantSaveQuit = src->m_wantSaveQuit;
#if XTUI_VIM_MULTIBUFFER_ON
    dest->m_wantEdit = src->m_wantEdit; dest->m_wantBufferNext = src->m_wantBufferNext; dest->m_wantBufferPrev = src->m_wantBufferPrev; dest->m_wantBufferList = src->m_wantBufferList; dest->m_wantBufferIndex = src->m_wantBufferIndex; dest->m_wantForce = src->m_wantForce; dest->m_wantWritePath = src->m_wantWritePath; dest->m_wantSaveAs = src->m_wantSaveAs; dest->m_wantWriteAll = src->m_wantWriteAll; dest->m_wantQuitAll = src->m_wantQuitAll; dest->m_wantBufferClose = src->m_wantBufferClose; memcpy(dest->m_actionPath, src->m_actionPath, sizeof(dest->m_actionPath));
#endif
    dest->m_pendingOperator = src->m_pendingOperator; dest->m_pendingNormal = src->m_pendingNormal; dest->m_count = src->m_count;
#if XTUI_VIM_VISUAL_ON
    dest->m_visualMode = src->m_visualMode; dest->m_visualLineWise = src->m_visualLineWise; dest->m_visualBlock = src->m_visualBlock; dest->m_visualStartLine = src->m_visualStartLine; dest->m_visualStartColumn = src->m_visualStartColumn; dest->m_blockInsertMode = src->m_blockInsertMode; dest->m_blockInsertFirstLine = src->m_blockInsertFirstLine; dest->m_blockInsertLastLine = src->m_blockInsertLastLine; dest->m_blockInsertColumn = src->m_blockInsertColumn;
#endif
#if XTUI_VIM_MARK_ON
    memcpy(dest->m_markLines, src->m_markLines, sizeof(dest->m_markLines)); memcpy(dest->m_markColumns, src->m_markColumns, sizeof(dest->m_markColumns));
#endif
#if XTUI_VIM_JUMPLIST_ON
    memcpy(dest->m_jumpLines, src->m_jumpLines, sizeof(dest->m_jumpLines)); memcpy(dest->m_jumpColumns, src->m_jumpColumns, sizeof(dest->m_jumpColumns)); dest->m_jumpLength = src->m_jumpLength; dest->m_jumpIndex = src->m_jumpIndex;
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    dest->m_findChar = src->m_findChar; dest->m_findBackward = src->m_findBackward; dest->m_findTill = src->m_findTill; memcpy(dest->m_lastRepeat, src->m_lastRepeat, sizeof(dest->m_lastRepeat)); dest->m_lastRepeatLen = src->m_lastRepeatLen;
    memcpy(dest->m_insertRepeatPrefix, src->m_insertRepeatPrefix, sizeof(dest->m_insertRepeatPrefix));
#endif
    dest->m_showLineNumbers = src->m_showLineNumbers;
}

XVtable* XTuiVim_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiVim)
    XVTABLE_INHERIT_XCLASS(XTuiWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXTuiWidget_Render, VXTuiVim_render);
    XVTABLE_OVERLOAD_DEFAULT(EXTuiWidget_KeyPress, VXTuiVim_keyPress);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiVim_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiVim_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiVim_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiVim);
    return XVTABLE_DEFAULT;
}

void XTuiVim_init(XTuiVim* vim)
{
#if XTUI_VIM_MARK_ON
    int i;
#endif
    if (!vim) return;
    memset(((XClass*)vim) + 1, 0, sizeof(XTuiVim) - sizeof(XClass));
    XTuiWidget_init((XTuiWidget*)vim); XClassGetVtable(vim) = XTuiVim_class_init();
    vim->m_lineCount = 1; vim->m_linesCapacity = 4; vim->m_lines = (char**)XMalloc_System(4u * sizeof(char*));
    if (vim->m_lines) { memset(vim->m_lines, 0, 4u * sizeof(char*)); vim->m_lines[0] = (char*)XMalloc_System(1u); if (vim->m_lines[0]) vim->m_lines[0][0] = '\0'; }
    vim->m_showLineNumbers = true;
#if XTUI_VIM_SEARCH_ON
    vim->m_searchHighlight = true;
#endif
#if XTUI_VIM_MARK_ON
    for (i = 0; i < 26; ++i) {
        vim->m_markLines[i] = -1;
        vim->m_markColumns[i] = -1;
    }
#endif
}

XTuiVim* XTuiVim_create_ex(XMemoryType memory)
{
    XTuiVim* vim = (XTuiVim*)XMemory_malloc(sizeof(XTuiVim), memory);
    if (!vim) return NULL; XTuiVim_init(vim); Set_Class_Memory(vim, memory); Set_Class_IsHeap(vim, true); return vim;
}

void XTuiVim_setLines(XTuiVim* vim, const char* const* lines, int count)
{
    int i;
    if (!vim) return;
    if (count < 1) count = 1;
    xvim_free_lines(vim->m_lines, vim->m_lineCount); vim->m_lines = NULL; vim->m_lineCount = 0; vim->m_linesCapacity = 0;
#if XTUI_VIM_UNDO_REDO_ON
    xvim_clear_stack(vim->m_undoStack, vim->m_undoLen); vim->m_undoStack = NULL; vim->m_undoLen = vim->m_undoCap = 0;
    xvim_clear_stack(vim->m_redoStack, vim->m_redoLen); vim->m_redoStack = NULL; vim->m_redoLen = vim->m_redoCap = 0;
#endif
    if (!xvim_ensure_lines(vim, count)) return;
    for (i = 0; i < count; ++i) if (!xvim_insert_line_raw(vim, i, lines && lines[i] ? lines[i] : "")) break;
    /* insert_line_raw starts from an empty buffer; remove the initial spare slot if needed. */
    if (vim->m_lineCount > count) xvim_delete_line_raw(vim, 0);
    vim->m_cursorLine = 0; vim->m_cursorColumn = 0; vim->m_topLine = 0; vim->m_modified = false; vim->m_insertMode = false; vim->m_replaceMode = false; vim->m_commandMode = false;
#if XTUI_VIM_SEARCH_ON
    vim->m_searchMode = false; vim->m_searchHighlight = true; vim->m_searchLen = vim->m_lastSearchLen = 0; vim->m_search[0] = vim->m_lastSearch[0] = '\0';
#endif
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    vim->m_substituteConfirm = false; vim->m_substituteGlobal = false; vim->m_substituteIgnoreCase = false; vim->m_substituteFirstLine = vim->m_substituteLastLine = vim->m_substituteLine = vim->m_substituteColumn = vim->m_substituteEndColumn = vim->m_substituteNextColumn = 0; vim->m_substitutePattern[0] = vim->m_substituteReplacement[0] = '\0';
#endif
#if XTUI_VIM_VISUAL_ON
    vim->m_visualMode = false; vim->m_visualLineWise = false; vim->m_visualBlock = false;
    vim->m_blockInsertMode = false; vim->m_blockInsertFirstLine = 0;
    vim->m_blockInsertLastLine = 0; vim->m_blockInsertColumn = 0;
#endif
#if XTUI_VIM_MARK_ON
    for (i = 0; i < 26; ++i) vim->m_markLines[i] = vim->m_markColumns[i] = -1;
#endif
#if XTUI_VIM_JUMPLIST_ON
    vim->m_jumpLength = vim->m_jumpIndex = 0;
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    vim->m_insertRepeatPrefix[0] = '\0';
#endif
    vim->m_commandLen = 0; vim->m_command[0] = '\0'; vim->m_wantSave = vim->m_wantQuit = vim->m_wantSaveQuit = false;
#if XTUI_VIM_MULTIBUFFER_ON
    vim->m_actionPath[0] = '\0'; vim->m_wantEdit = vim->m_wantBufferNext = vim->m_wantBufferPrev = vim->m_wantBufferList = vim->m_wantForce = false; vim->m_wantWritePath = vim->m_wantSaveAs = vim->m_wantWriteAll = vim->m_wantQuitAll = vim->m_wantBufferClose = false; vim->m_wantBufferIndex = 0;
#endif
#if XTUI_VIM_UNDO_REDO_ON
    vim->m_undoLen = vim->m_redoLen = 0;
#endif
    xvim_clear_register(vim);
}

int XTuiVim_lineCount(const XTuiVim* vim) { return vim ? vim->m_lineCount : 0; }
const char* XTuiVim_line(const XTuiVim* vim, int index) { return vim && index >= 0 && index < vim->m_lineCount && vim->m_lines[index] ? vim->m_lines[index] : ""; }

void XTuiVim_setPath(XTuiVim* vim, const char* path)
{
    char* copy; if (!vim) return; path = path ? path : ""; copy = (char*)XMalloc_System(strlen(path) + 1u); if (!copy) return; strcpy(copy, path); if (vim->m_path) XFree_System(vim->m_path); vim->m_path = copy;
}

const char* XTuiVim_path(const XTuiVim* vim) { return vim && vim->m_path ? vim->m_path : ""; }
bool XTuiVim_isModified(const XTuiVim* vim) { return vim ? vim->m_modified : false; }
void XTuiVim_clearModified(XTuiVim* vim) { if (vim) vim->m_modified = false; }
bool XTuiVim_wantSave(const XTuiVim* vim) { return vim ? vim->m_wantSave : false; }
bool XTuiVim_wantQuit(const XTuiVim* vim) { return vim ? vim->m_wantQuit : false; }
bool XTuiVim_wantSaveQuit(const XTuiVim* vim) { return vim ? vim->m_wantSaveQuit : false; }
#if XTUI_VIM_MULTIBUFFER_ON
bool XTuiVim_wantEdit(const XTuiVim* vim) { return vim ? vim->m_wantEdit : false; }
const char* XTuiVim_actionPath(const XTuiVim* vim) { return vim ? vim->m_actionPath : ""; }
bool XTuiVim_wantBufferNext(const XTuiVim* vim) { return vim ? vim->m_wantBufferNext : false; }
bool XTuiVim_wantBufferPrev(const XTuiVim* vim) { return vim ? vim->m_wantBufferPrev : false; }
bool XTuiVim_wantBufferList(const XTuiVim* vim) { return vim ? vim->m_wantBufferList : false; }
int XTuiVim_wantBufferIndex(const XTuiVim* vim) { return vim ? vim->m_wantBufferIndex : 0; }
bool XTuiVim_wantForce(const XTuiVim* vim) { return vim ? vim->m_wantForce : false; }
bool XTuiVim_wantWritePath(const XTuiVim* vim) { return vim ? vim->m_wantWritePath : false; }
bool XTuiVim_wantSaveAs(const XTuiVim* vim) { return vim ? vim->m_wantSaveAs : false; }
bool XTuiVim_wantWriteAll(const XTuiVim* vim) { return vim ? vim->m_wantWriteAll : false; }
bool XTuiVim_wantQuitAll(const XTuiVim* vim) { return vim ? vim->m_wantQuitAll : false; }
bool XTuiVim_wantBufferClose(const XTuiVim* vim) { return vim ? vim->m_wantBufferClose : false; }
#else
bool XTuiVim_wantEdit(const XTuiVim* vim) { (void)vim; return false; }
const char* XTuiVim_actionPath(const XTuiVim* vim) { (void)vim; return ""; }
bool XTuiVim_wantBufferNext(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantBufferPrev(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantBufferList(const XTuiVim* vim) { (void)vim; return false; }
int XTuiVim_wantBufferIndex(const XTuiVim* vim) { (void)vim; return 0; }
bool XTuiVim_wantForce(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantWritePath(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantSaveAs(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantWriteAll(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantQuitAll(const XTuiVim* vim) { (void)vim; return false; }
bool XTuiVim_wantBufferClose(const XTuiVim* vim) { (void)vim; return false; }
#endif
void XTuiVim_ackAction(XTuiVim* vim)
{
    if (!vim) return;
    vim->m_wantSave = vim->m_wantQuit = vim->m_wantSaveQuit = false;
#if XTUI_VIM_MULTIBUFFER_ON
    vim->m_wantEdit = vim->m_wantBufferNext = vim->m_wantBufferPrev = false;
    vim->m_wantBufferList = vim->m_wantForce = false;
    vim->m_wantWritePath = vim->m_wantSaveAs = vim->m_wantWriteAll = false;
    vim->m_wantQuitAll = vim->m_wantBufferClose = false;
    vim->m_wantBufferIndex = 0;
    vim->m_actionPath[0] = '\0';
#endif
}
void XTuiVim_setInsertMode(XTuiVim* vim, bool insert) { if (vim) { if (insert && !vim->m_insertMode) xvim_enter_insert(vim, false, false); else if (!insert && vim->m_insertMode) xvim_leave_insert(vim); } }
bool XTuiVim_isInsertMode(const XTuiVim* vim) { return vim ? vim->m_insertMode : false; }

#endif /* XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON */
