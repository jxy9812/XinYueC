/**
 * @file       XTuiVim.c
 * @brief      XTui 全屏 vim 风格编辑器控件实现。
 * @details    行为对齐 Linux vim 常用操作：命令模式 h/j/k/l/0/$/i/a/o/O/x/dd
 *             、冒号命令 :w/:q/:wq/:x/:q!，插入模式逐字符编辑、方向键移动、
 *             回车换行、退格删除、ESC 返回命令模式。文本只在内存中维护，
 *             文件读写由调用方完成。
 */

#include "XTuiVim.h"

#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON

#include "XTuiScreen.h"
#include "XTuiTerminal.h"
#include "XTuiTypes.h"
#include <string.h>
#include <stdio.h>

/* ==================== UTF-8 工具 ==================== */

static size_t utf8CharLen(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

/* 返回字符串的 UTF-8 字符数（显示列数）。 */
static int utf8CharCount(const char* s)
{
    int count = 0;
    if (!s)
        return 0;
    while (*s) {
        size_t len = utf8CharLen((unsigned char)*s);
        if (len == 0)
            break;
        s += len;
        ++count;
    }
    return count;
}

/* 返回字符串中字符索引 index 对应的字节偏移；越界返回末尾。 */
static int utf8ByteOffset(const char* s, int index)
{
    int offset = 0;
    int count = 0;
    if (!s || index <= 0)
        return 0;
    while (s[offset]) {
        size_t len = utf8CharLen((unsigned char)s[offset]);
        if (len == 0)
            break;
        if (count >= index)
            break;
        offset += (int)len;
        ++count;
    }
    return offset;
}

/* 返回字节偏移 pos 处的字符长度；越界返回 0。 */
static int utf8CharLenAt(const char* s, int pos)
{
    if (!s || pos < 0)
        return 0;
    size_t len = utf8CharLen((unsigned char)s[pos]);
    return len ? (int)len : 1;
}

/* ==================== 行缓冲管理 ==================== */

static void xvim_free_lines(char** lines, int count)
{
    int i;
    if (!lines)
        return;
    for (i = 0; i < count; ++i) {
        if (lines[i])
            XFree_System(lines[i]);
    }
    XFree_System(lines);
}

static bool xvim_ensure_capacity(XTuiVim* vim, int needed)
{
    int cap;
    char** lines;
    if (needed <= vim->m_linesCapacity)
        return true;
    cap = vim->m_linesCapacity ? vim->m_linesCapacity : 16;
    while (cap < needed)
        cap *= 2;
    lines = (char**)XMalloc_System((size_t)cap * sizeof(char*));
    if (!lines)
        return false;
    memset(lines, 0, (size_t)cap * sizeof(char*));
    if (vim->m_lines && vim->m_lineCount)
        memcpy(lines, vim->m_lines, (size_t)vim->m_lineCount * sizeof(char*));
    if (vim->m_lines)
        XFree_System(vim->m_lines);
    vim->m_lines = lines;
    vim->m_linesCapacity = cap;
    return true;
}

static bool xvim_set_line(XTuiVim* vim, int index, const char* text)
{
    size_t len;
    char* copy;
    if (!vim || index < 0 || index >= vim->m_lineCount)
        return false;
    if (!text)
        text = "";
    len = strlen(text);
    if (len >= XTUI_VIM_LINE_MAX)
        len = XTUI_VIM_LINE_MAX - 1u;
    copy = (char*)XMalloc_System(len + 1);
    if (!copy)
        return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    if (vim->m_lines[index])
        XFree_System(vim->m_lines[index]);
    vim->m_lines[index] = copy;
    return true;
}

static bool xvim_insert_line(XTuiVim* vim, int index, const char* text)
{
    int i;
    size_t len;
    char* copy;
    if (!vim || index < 0 || index > vim->m_lineCount)
        return false;
    if (!xvim_ensure_capacity(vim, vim->m_lineCount + 1))
        return false;
    if (!text)
        text = "";
    len = strlen(text);
    if (len >= XTUI_VIM_LINE_MAX)
        len = XTUI_VIM_LINE_MAX - 1u;
    copy = (char*)XMalloc_System(len + 1);
    if (!copy)
        return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    for (i = vim->m_lineCount; i > index; --i)
        vim->m_lines[i] = vim->m_lines[i - 1];
    vim->m_lines[index] = copy;
    ++vim->m_lineCount;
    return true;
}

static bool xvim_delete_line(XTuiVim* vim, int index)
{
    int i;
    if (!vim || index < 0 || index >= vim->m_lineCount)
        return false;
    if (vim->m_lines[index])
        XFree_System(vim->m_lines[index]);
    for (i = index; i + 1 < vim->m_lineCount; ++i)
        vim->m_lines[i] = vim->m_lines[i + 1];
    --vim->m_lineCount;
    vim->m_lines[vim->m_lineCount] = NULL;
    return true;
}

/* ==================== 撤销快照 ==================== */

static void xvim_snapshot(XTuiVim* vim)
{
    int i;
    char** copy;
    if (!vim)
        return;
    xvim_free_lines(vim->m_undoLines, vim->m_undoCount);
    copy = (char**)XMalloc_System((size_t)(vim->m_lineCount ? vim->m_lineCount : 1) * sizeof(char*));
    if (!copy) {
        vim->m_undoLines = NULL;
        vim->m_undoCount = 0;
        vim->m_undoCapacity = 0;
        return;
    }
    memset(copy, 0, (size_t)(vim->m_lineCount ? vim->m_lineCount : 1) * sizeof(char*));
    for (i = 0; i < vim->m_lineCount; ++i) {
        size_t len = strlen(vim->m_lines[i] ? vim->m_lines[i] : "");
        copy[i] = (char*)XMalloc_System(len + 1);
        if (!copy[i]) {
            int j;
            for (j = 0; j < i; ++j)
                XFree_System(copy[j]);
            XFree_System(copy);
            vim->m_undoLines = NULL;
            vim->m_undoCount = 0;
            vim->m_undoCapacity = 0;
            return;
        }
        memcpy(copy[i], vim->m_lines[i] ? vim->m_lines[i] : "", len + 1);
    }
    vim->m_undoLines = copy;
    vim->m_undoCount = vim->m_lineCount;
    vim->m_undoCapacity = vim->m_lineCount;
}

static void xvim_undo(XTuiVim* vim)
{
    if (!vim || !vim->m_undoLines)
        return;
    xvim_free_lines(vim->m_lines, vim->m_lineCount);
    vim->m_lines = vim->m_undoLines;
    vim->m_lineCount = vim->m_undoCount;
    vim->m_linesCapacity = vim->m_undoCapacity;
    vim->m_undoLines = NULL;
    vim->m_undoCount = 0;
    vim->m_undoCapacity = 0;
    vim->m_modified = true;
    if (vim->m_cursorLine >= vim->m_lineCount)
        vim->m_cursorLine = vim->m_lineCount > 0 ? vim->m_lineCount - 1 : 0;
    if (vim->m_cursorLine < 0)
        vim->m_cursorLine = 0;
    if (vim->m_cursorColumn < 0)
        vim->m_cursorColumn = 0;
}

/* ==================== 插入模式的编辑操作 ==================== */

static void xvim_clamp_cursor(XTuiVim* vim)
{
    const char* line;
    if (!vim)
        return;
    if (vim->m_cursorLine < 0)
        vim->m_cursorLine = 0;
    if (vim->m_cursorLine >= vim->m_lineCount)
        vim->m_cursorLine = vim->m_lineCount > 0 ? vim->m_lineCount - 1 : 0;
    line = vim->m_cursorLine < vim->m_lineCount ? vim->m_lines[vim->m_cursorLine] : "";
    if (vim->m_cursorColumn < 0)
        vim->m_cursorColumn = 0;
    if (vim->m_cursorColumn > utf8CharCount(line))
        vim->m_cursorColumn = utf8CharCount(line);
}

static void xvim_enter_insert(XTuiVim* vim, int line, int column, bool after)
{
    if (!vim)
        return;
    if (line >= 0 && line < vim->m_lineCount)
        vim->m_cursorLine = line;
    if (column >= 0)
        vim->m_cursorColumn = column;
    if (after && vim->m_cursorLine < vim->m_lineCount)
        vim->m_cursorColumn = utf8CharCount(vim->m_lines[vim->m_cursorLine]);
    vim->m_insertMode = true;
    vim->m_commandMode = false;
    vim->m_pendingNormal = '\0';
    vim->m_insertLen = 0;
    vim->m_insertCursor = 0;
    vim->m_insertBuf[0] = '\0';
    xvim_snapshot(vim);
}

static void xvim_leave_insert(XTuiVim* vim)
{
    if (!vim)
        return;
    /* 把插入缓冲中未回车提交的文本合并进当前行。 */
    if (vim->m_insertLen > 0) {
        const char* line = vim->m_lines[vim->m_cursorLine];
        int byteOffset = utf8ByteOffset(line, vim->m_cursorColumn);
        size_t oldLen = strlen(line);
        size_t insLen = (size_t)vim->m_insertLen;
        size_t newLen = oldLen + insLen;
        char* buf;
        if (newLen >= XTUI_VIM_LINE_MAX)
            newLen = XTUI_VIM_LINE_MAX - 1u;
        if (insLen > newLen - oldLen)
            insLen = newLen - oldLen;
        buf = (char*)XMalloc_System(newLen + 1);
        if (buf) {
            memcpy(buf, line, (size_t)byteOffset);
            memcpy(buf + byteOffset, vim->m_insertBuf, insLen);
            memcpy(buf + byteOffset + insLen, line + byteOffset,
                   strlen(line + byteOffset) + 1);
            buf[newLen] = '\0';
            xvim_set_line(vim, vim->m_cursorLine, buf);
            XFree_System(buf);
            vim->m_cursorColumn += utf8CharCount(vim->m_insertBuf);
            vim->m_modified = true;
        }
    }
    vim->m_insertMode = false;
    vim->m_insertLen = 0;
    vim->m_insertCursor = 0;
    vim->m_insertBuf[0] = '\0';
}

static bool xvim_insert_bytes(XTuiVim* vim, const char* utf8, int length)
{
    int lineIndex;
    int byteOffset;
    size_t oldLen;
    size_t insLen;
    size_t newLen;
    char* buf;
    if (!vim || !utf8 || length <= 0)
        return false;
    lineIndex = vim->m_cursorLine;
    if (lineIndex < 0 || lineIndex >= vim->m_lineCount)
        return false;
    byteOffset = utf8ByteOffset(vim->m_lines[lineIndex], vim->m_cursorColumn);
    oldLen = strlen(vim->m_lines[lineIndex]);
    insLen = (size_t)length;
    if (oldLen + insLen >= XTUI_VIM_LINE_MAX)
        return false;
    newLen = oldLen + insLen;
    buf = (char*)XMalloc_System(newLen + 1);
    if (!buf)
        return false;
    memcpy(buf, vim->m_lines[lineIndex], (size_t)byteOffset);
    memcpy(buf + byteOffset, utf8, insLen);
    memcpy(buf + byteOffset + insLen, vim->m_lines[lineIndex] + byteOffset,
           oldLen - (size_t)byteOffset + 1);
    xvim_set_line(vim, lineIndex, buf);
    XFree_System(buf);
    vim->m_cursorColumn += utf8CharCount(utf8);
    vim->m_modified = true;
    return true;
}

static bool xvim_split_line(XTuiVim* vim)
{
    const char* line;
    int byteOffset;
    char* left;
    char* right;
    if (!vim || vim->m_cursorLine < 0 || vim->m_cursorLine >= vim->m_lineCount)
        return false;
    line = vim->m_lines[vim->m_cursorLine];
    byteOffset = utf8ByteOffset(line, vim->m_cursorColumn);
    left = (char*)XMalloc_System((size_t)byteOffset + 1);
    right = (char*)XMalloc_System(strlen(line + byteOffset) + 1);
    if (!left || !right) {
        if (left) XFree_System(left);
        if (right) XFree_System(right);
        return false;
    }
    memcpy(left, line, (size_t)byteOffset);
    left[byteOffset] = '\0';
    strcpy(right, line + byteOffset);
    xvim_set_line(vim, vim->m_cursorLine, left);
    XFree_System(left);
    if (!xvim_insert_line(vim, vim->m_cursorLine + 1, right)) {
        XFree_System(right);
        return false;
    }
    XFree_System(right);
    ++vim->m_cursorLine;
    vim->m_cursorColumn = 0;
    vim->m_modified = true;
    return true;
}

static bool xvim_join_with_previous(XTuiVim* vim)
{
    const char* prev;
    const char* cur;
    char* buf;
    size_t len;
    if (!vim || vim->m_cursorLine <= 0 || vim->m_cursorLine >= vim->m_lineCount)
        return false;
    prev = vim->m_lines[vim->m_cursorLine - 1];
    cur = vim->m_lines[vim->m_cursorLine];
    len = strlen(prev) + strlen(cur);
    if (len >= XTUI_VIM_LINE_MAX)
        return false;
    buf = (char*)XMalloc_System(len + 1);
    if (!buf)
        return false;
    strcpy(buf, prev);
    strcat(buf, cur);
    xvim_set_line(vim, vim->m_cursorLine - 1, buf);
    XFree_System(buf);
    if (!xvim_delete_line(vim, vim->m_cursorLine))
        return false;
    --vim->m_cursorLine;
    vim->m_cursorColumn = utf8CharCount(vim->m_lines[vim->m_cursorLine]);
    vim->m_modified = true;
    return true;
}

static bool xvim_delete_char_at(XTuiVim* vim, int lineIndex, int column)
{
    const char* line;
    int byteOffset;
    int clen;
    char* buf;
    size_t len;
    if (!vim || lineIndex < 0 || lineIndex >= vim->m_lineCount)
        return false;
    line = vim->m_lines[lineIndex];
    byteOffset = utf8ByteOffset(line, column);
    if ((size_t)byteOffset >= strlen(line))
        return false;
    clen = utf8CharLenAt(line, byteOffset);
    len = strlen(line) - (size_t)clen;
    buf = (char*)XMalloc_System(len + 1);
    if (!buf)
        return false;
    memcpy(buf, line, (size_t)byteOffset);
    memcpy(buf + byteOffset, line + byteOffset + clen, len - (size_t)byteOffset + 1);
    xvim_set_line(vim, lineIndex, buf);
    XFree_System(buf);
    vim->m_modified = true;
    return true;
}

/* ==================== 渲染 ==================== */

static bool VXTuiVim_render(XTuiWidget* base, XTuiScreen* screen)
{
    XTuiVim* vim = (XTuiVim*)base;
    XRect r;
    int statusRow;
    int y;
    int x;
    char lineNo[8];
    if (!vim || !screen)
        return false;
    r = XTuiWidget_rect(base);
    if (r.width < 1 || r.height < 2)
        return false;
    statusRow = r.height - 1;

    /* 正文区域：行号 + 文本。 */
    for (y = 0; y < statusRow; ++y) {
        int lineIndex = vim->m_topLine + y;
        int col = 0;
        const char* text = "";
        if (lineIndex >= 0 && lineIndex < vim->m_lineCount)
            text = vim->m_lines[lineIndex];

        /* 整行先清为空格。 */
        for (x = 0; x < r.width; ++x)
            XTuiScreen_setUtf8(screen, r.x + x, r.y + y, " ");

        /* 行号。 */
        if (lineIndex >= 0) {
            int n = snprintf(lineNo, sizeof(lineNo), "%4d ", lineIndex + 1);
            int i;
            if (n < 0) n = 0;
            if (n > r.width) n = r.width;
            for (i = 0; i < n; ++i)
                XTuiScreen_setCell(screen, r.x + i, r.y + y,
                                   (char[]){lineNo[i], '\0'},
                                   XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT,
                                   (int)XTuiAttribute_None);
            col = n;
        }

        /* 文本内容。 */
        if (col < r.width) {
            int offset = 0;
            int charPos = 0;
            while (col < r.width && text[offset]) {
                int clen = utf8CharLenAt(text, offset);
                char buf[XTUI_CELL_UTF8_MAX + 1];
                int attrs = (int)XTuiAttribute_None;
                if (lineIndex == vim->m_cursorLine && charPos == vim->m_cursorColumn)
                    attrs |= (int)XTuiAttribute_Reverse;
                memset(buf, 0, sizeof(buf));
                if (clen > XTUI_CELL_UTF8_MAX)
                    clen = 1;
                memcpy(buf, text + offset, (size_t)clen);
                XTuiScreen_setCell(screen, r.x + col, r.y + y, buf,
                                   XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, attrs);
                offset += clen;
                ++charPos;
                ++col;
            }
            /* 光标在行尾的反显空格。 */
            if (lineIndex == vim->m_cursorLine && charPos == vim->m_cursorColumn &&
                col < r.width)
                XTuiScreen_setCell(screen, r.x + col, r.y + y, " ",
                                   XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT,
                                   (int)XTuiAttribute_Reverse);
        }
    }

    /* 底部状态栏。 */
    {
        const char* mode = vim->m_insertMode ? "-- 插入 --" :
                           (vim->m_commandMode ? "-- 命令 --" : "-- 命令 --");
        const char* path = vim->m_path ? vim->m_path : "";
        char status[XTUI_VIM_STATUS_MAX + 1];
        int n;
        int i;
        for (x = 0; x < r.width; ++x)
            XTuiScreen_setCell(screen, r.x + x, r.y + statusRow, " ",
                               XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT,
                               (int)XTuiAttribute_None);
        if (vim->m_commandMode) {
            n = snprintf(status, sizeof(status), ":%s", vim->m_command);
        } else {
            n = snprintf(status, sizeof(status), "%s %s%s  行 %d/%d 列 %d",
                         mode, path, vim->m_modified ? " [已修改]" : "",
                         vim->m_cursorLine + 1, vim->m_lineCount,
                         vim->m_cursorColumn + 1);
        }
        if (n < 0) n = 0;
        if (n > r.width) n = r.width;
        for (i = 0; i < n; ++i)
            XTuiScreen_setCell(screen, r.x + i, r.y + statusRow,
                               (char[]){status[i], '\0'},
                               XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT,
                               (int)XTuiAttribute_Reverse);
    }

    {
        int cursorX = r.x + vim->m_cursorColumn + 5;
        int cursorY = r.y + (vim->m_cursorLine - vim->m_topLine);
        if (cursorX >= r.x + r.width)
            cursorX = r.x + r.width - 1;
        if (cursorX < r.x)
            cursorX = r.x;
        if (cursorY >= r.y + statusRow)
            cursorY = r.y + statusRow - 1;
        if (cursorY < r.y)
            cursorY = r.y;
        XTuiScreen_setCursor(screen, cursorX, cursorY);
    }
    return true;
}

/* ==================== 命令模式 ==================== */

static void xvim_goto_line(XTuiVim* vim, int line)
{
    int column;
    const char* text;
    if (!vim)
        return;
    if (line < 0)
        line = 0;
    if (line >= vim->m_lineCount)
        line = vim->m_lineCount > 0 ? vim->m_lineCount - 1 : 0;
    column = vim->m_cursorColumn;
    vim->m_cursorLine = line;
    text = line >= 0 && line < vim->m_lineCount ? vim->m_lines[line] : "";
    if (column > utf8CharCount(text))
        column = utf8CharCount(text);
    if (column < 0)
        column = 0;
    vim->m_cursorColumn = column;
}

static void xvim_execute_command(XTuiVim* vim)
{
    const char* cmd = vim->m_command;
    size_t len = (size_t)vim->m_commandLen;
    if (len == 0) {
        vim->m_commandMode = false;
        vim->m_pendingNormal = '\0';
        return;
    }
    if (len == 1 && cmd[0] == 'w') {
        vim->m_wantSave = true;
        vim->m_commandMode = false;
        vim->m_commandLen = 0;
        vim->m_command[0] = '\0';
        return;
    }
    if (len == 1 && cmd[0] == 'q') {
        if (vim->m_modified) {
            snprintf(vim->m_status, sizeof(vim->m_status),
                     "有未保存修改，使用 :wq 保存退出或 :q! 放弃");
            vim->m_commandMode = false;
            vim->m_commandLen = 0;
            vim->m_command[0] = '\0';
            return;
        }
        vim->m_wantQuit = true;
        vim->m_commandMode = false;
        vim->m_commandLen = 0;
        vim->m_command[0] = '\0';
        return;
    }
    if (len == 2 && memcmp(cmd, "wq", 2) == 0) {
        vim->m_wantSaveQuit = true;
        vim->m_commandMode = false;
        vim->m_commandLen = 0;
        vim->m_command[0] = '\0';
        return;
    }
    if (len == 1 && cmd[0] == 'x') {
        vim->m_wantSaveQuit = true;
        vim->m_commandMode = false;
        vim->m_commandLen = 0;
        vim->m_command[0] = '\0';
        return;
    }
    if (len == 2 && memcmp(cmd, "q!", 2) == 0) {
        vim->m_wantQuit = true;
        vim->m_commandMode = false;
        vim->m_commandLen = 0;
        vim->m_command[0] = '\0';
        return;
    }
    snprintf(vim->m_status, sizeof(vim->m_status), "无效命令: :%s", cmd);
    vim->m_commandMode = false;
    vim->m_commandLen = 0;
    vim->m_command[0] = '\0';
}

static bool VXTuiVim_keyPress(XTuiWidget* base, const XTuiKeyEvent* event)
{
    XTuiVim* vim = (XTuiVim*)base;
    if (!vim || !event)
        return false;

    /* 冒号命令输入。 */
    if (vim->m_commandMode) {
        if (event->m_keyType == XTuiKey_Char) {
            size_t len = strlen(event->m_utf8);
            if (vim->m_commandLen + (int)len < (int)sizeof(vim->m_command)) {
                memcpy(vim->m_command + vim->m_commandLen, event->m_utf8, len);
                vim->m_commandLen += (int)len;
                vim->m_command[vim->m_commandLen] = '\0';
            }
            return true;
        }
        if (event->m_keyType == XTuiKey_Backspace) {
            if (vim->m_commandLen > 0) {
                --vim->m_commandLen;
                vim->m_command[vim->m_commandLen] = '\0';
            }
            return true;
        }
        if (event->m_keyType == XTuiKey_Enter) {
            xvim_execute_command(vim);
            return true;
        }
        if (event->m_keyType == XTuiKey_Escape) {
            vim->m_commandMode = false;
            vim->m_commandLen = 0;
            vim->m_command[0] = '\0';
            return true;
        }
        return true;
    }

    /* 插入模式。 */
    if (vim->m_insertMode) {
        if (event->m_keyType == XTuiKey_Char) {
            return xvim_insert_bytes(vim, event->m_utf8, (int)strlen(event->m_utf8));
        }
        if (event->m_keyType == XTuiKey_Enter) {
            return xvim_split_line(vim);
        }
        if (event->m_keyType == XTuiKey_Backspace) {
            if (vim->m_cursorColumn > 0) {
                --vim->m_cursorColumn;
                xvim_delete_char_at(vim, vim->m_cursorLine, vim->m_cursorColumn);
            } else {
                xvim_join_with_previous(vim);
            }
            return true;
        }
        if (event->m_keyType == XTuiKey_Delete) {
            xvim_delete_char_at(vim, vim->m_cursorLine, vim->m_cursorColumn);
            return true;
        }
        if (event->m_keyType == XTuiKey_ArrowLeft) {
            if (vim->m_cursorColumn > 0)
                --vim->m_cursorColumn;
            return true;
        }
        if (event->m_keyType == XTuiKey_ArrowRight) {
            int max = vim->m_cursorLine < vim->m_lineCount ?
                      utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
            if (vim->m_cursorColumn < max)
                ++vim->m_cursorColumn;
            return true;
        }
        if (event->m_keyType == XTuiKey_ArrowUp) {
            if (vim->m_cursorLine > 0) {
                int target = vim->m_cursorLine - 1;
                const char* text = vim->m_lines[target] ? vim->m_lines[target] : "";
                vim->m_cursorLine = target;
                if (vim->m_cursorColumn > utf8CharCount(text))
                    vim->m_cursorColumn = utf8CharCount(text);
            }
            return true;
        }
        if (event->m_keyType == XTuiKey_ArrowDown) {
            if (vim->m_cursorLine + 1 < vim->m_lineCount) {
                int target = vim->m_cursorLine + 1;
                const char* text = vim->m_lines[target] ? vim->m_lines[target] : "";
                vim->m_cursorLine = target;
                if (vim->m_cursorColumn > utf8CharCount(text))
                    vim->m_cursorColumn = utf8CharCount(text);
            }
            return true;
        }
        if (event->m_keyType == XTuiKey_Home) {
            vim->m_cursorColumn = 0;
            return true;
        }
        if (event->m_keyType == XTuiKey_End) {
            vim->m_cursorColumn = vim->m_cursorLine < vim->m_lineCount ?
                                  utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
            return true;
        }
        if (event->m_keyType == XTuiKey_Escape) {
            xvim_leave_insert(vim);
            return true;
        }
        return true;
    }

    /* 命令模式。 */
    switch (event->m_keyType) {
    case XTuiKey_ArrowLeft:
    case XTuiKey_ArrowUp:
    case XTuiKey_ArrowDown:
    case XTuiKey_ArrowRight:
    case XTuiKey_Home:
    case XTuiKey_End:
        if (event->m_keyType == XTuiKey_ArrowLeft && vim->m_cursorColumn > 0)
            --vim->m_cursorColumn;
        else if (event->m_keyType == XTuiKey_ArrowRight) {
            int max = vim->m_cursorLine < vim->m_lineCount ?
                      utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
            if (vim->m_cursorColumn < max)
                ++vim->m_cursorColumn;
        } else if (event->m_keyType == XTuiKey_ArrowUp && vim->m_cursorLine > 0) {
            int target = vim->m_cursorLine - 1;
            const char* text = vim->m_lines[target] ? vim->m_lines[target] : "";
            vim->m_cursorLine = target;
            if (vim->m_cursorColumn > utf8CharCount(text))
                vim->m_cursorColumn = utf8CharCount(text);
        } else if (event->m_keyType == XTuiKey_ArrowDown && vim->m_cursorLine + 1 < vim->m_lineCount) {
            int target = vim->m_cursorLine + 1;
            const char* text = vim->m_lines[target] ? vim->m_lines[target] : "";
            vim->m_cursorLine = target;
            if (vim->m_cursorColumn > utf8CharCount(text))
                vim->m_cursorColumn = utf8CharCount(text);
        }
        else if (event->m_keyType == XTuiKey_Home)
            vim->m_cursorColumn = 0;
        else if (event->m_keyType == XTuiKey_End)
            vim->m_cursorColumn = vim->m_cursorLine < vim->m_lineCount ?
                                  utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
        vim->m_pendingNormal = '\0';
        return true;
    case XTuiKey_Char: {
        char c = event->m_utf8[0];
        /* 先处理待组合键：dd 删除当前行。 */
        if (vim->m_pendingNormal == 'd') {
            if (c == 'd') {
                int line = vim->m_cursorLine;
                vim->m_pendingNormal = '\0';
                if (xvim_delete_line(vim, line)) {
                    vim->m_modified = true;
                    if (vim->m_cursorLine >= vim->m_lineCount)
                        vim->m_cursorLine = vim->m_lineCount > 0 ? vim->m_lineCount - 1 : 0;
                    if (vim->m_cursorLine < 0)
                        vim->m_cursorLine = 0;
                    vim->m_cursorColumn = 0;
                }
                return true;
            }
            vim->m_pendingNormal = '\0';
        }
        if (c == 'h' && vim->m_cursorColumn > 0)
            --vim->m_cursorColumn;
        else if (c == 'l') {
            int max = vim->m_cursorLine < vim->m_lineCount ?
                      utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
            if (vim->m_cursorColumn < max)
                ++vim->m_cursorColumn;
        } else if (c == 'j')
            xvim_goto_line(vim, vim->m_cursorLine + 1);
        else if (c == 'k')
            xvim_goto_line(vim, vim->m_cursorLine - 1);
        else if (c == '0')
            vim->m_cursorColumn = 0;
        else if (c == '$')
            vim->m_cursorColumn = vim->m_cursorLine < vim->m_lineCount ?
                                  utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0;
        else if (c == 'i')
            xvim_enter_insert(vim, vim->m_cursorLine, vim->m_cursorColumn, false);
        else if (c == 'a')
            xvim_enter_insert(vim, vim->m_cursorLine,
                              vim->m_cursorLine < vim->m_lineCount ?
                              utf8CharCount(vim->m_lines[vim->m_cursorLine]) : 0, true);
        else if (c == 'o') {
            if (xvim_insert_line(vim, vim->m_cursorLine + 1, "")) {
                xvim_goto_line(vim, vim->m_cursorLine + 1);
                vim->m_modified = true;
                xvim_enter_insert(vim, vim->m_cursorLine, 0, false);
            }
        } else if (c == 'O') {
            if (xvim_insert_line(vim, vim->m_cursorLine, "")) {
                xvim_goto_line(vim, vim->m_cursorLine);
                vim->m_modified = true;
                xvim_enter_insert(vim, vim->m_cursorLine, 0, false);
            }
        } else if (c == 'x') {
            xvim_delete_char_at(vim, vim->m_cursorLine, vim->m_cursorColumn);
        } else if (c == 'd') {
            vim->m_pendingNormal = 'd';
        } else if (c == ':') {
            vim->m_pendingNormal = '\0';
            vim->m_commandMode = true;
            vim->m_commandLen = 0;
            vim->m_command[0] = '\0';
        } else if (c == 'u') {
            xvim_undo(vim);
        }
        return true;
    }
    case XTuiKey_Escape:
        vim->m_pendingNormal = '\0';
        return true;
    default:
        return false;
    }
}

/* ==================== 构造与析构 ==================== */

static void VXTuiVim_deinit(XTuiVim* vim)
{
    if (!vim)
        return;
    xvim_free_lines(vim->m_lines, vim->m_lineCount);
    vim->m_lines = NULL;
    vim->m_lineCount = 0;
    vim->m_linesCapacity = 0;
    xvim_free_lines(vim->m_undoLines, vim->m_undoCount);
    vim->m_undoLines = NULL;
    vim->m_undoCount = 0;
    vim->m_undoCapacity = 0;
    if (vim->m_path) {
        XFree_System(vim->m_path);
        vim->m_path = NULL;
    }
    XClass_Deinit_Parent(XTuiWidget, (XTuiWidget*)vim);
}

static bool copyLines(char** dest, int destCount, int destCap,
                      char** src, int srcCount)
{
    int i;
    (void)destCount;
    (void)destCap;
    if (!dest || !src || srcCount <= 0)
        return true;
    for (i = 0; i < srcCount; ++i) {
        size_t len = strlen(src[i] ? src[i] : "");
        dest[i] = (char*)XMalloc_System(len + 1);
        if (!dest[i])
            return false;
        memcpy(dest[i], src[i] ? src[i] : "", len + 1);
    }
    return true;
}

static void VXTuiVim_copy(XTuiVim* dest, const XTuiVim* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiVim_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Copy, void(*)(XTuiWidget*, const XTuiWidget*))((XTuiWidget*)dest, (const XTuiWidget*)src);
    dest->m_lineCount = src->m_lineCount;
    dest->m_linesCapacity = src->m_linesCapacity;
    dest->m_lines = (char**)XMalloc_System((size_t)(src->m_linesCapacity ? src->m_linesCapacity : 1) * sizeof(char*));
    if (dest->m_lines) {
        memset(dest->m_lines, 0, (size_t)(src->m_linesCapacity ? src->m_linesCapacity : 1) * sizeof(char*));
        copyLines(dest->m_lines, src->m_lineCount, src->m_linesCapacity,
                  src->m_lines, src->m_lineCount);
    } else {
        dest->m_linesCapacity = 0;
    }
    dest->m_cursorLine = src->m_cursorLine;
    dest->m_cursorColumn = src->m_cursorColumn;
    dest->m_topLine = src->m_topLine;
    dest->m_insertMode = src->m_insertMode;
    dest->m_commandMode = src->m_commandMode;
    dest->m_modified = src->m_modified;
    memcpy(dest->m_command, src->m_command, sizeof(dest->m_command));
    dest->m_commandLen = src->m_commandLen;
    memcpy(dest->m_insertBuf, src->m_insertBuf, sizeof(dest->m_insertBuf));
    dest->m_insertLen = src->m_insertLen;
    dest->m_insertCursor = src->m_insertCursor;
    memcpy(dest->m_status, src->m_status, sizeof(dest->m_status));
    if (src->m_path) {
        size_t len = strlen(src->m_path);
        dest->m_path = (char*)XMalloc_System(len + 1);
        if (dest->m_path)
            memcpy(dest->m_path, src->m_path, len + 1);
    } else {
        dest->m_path = NULL;
    }
    dest->m_wantSave = src->m_wantSave;
    dest->m_wantQuit = src->m_wantQuit;
    dest->m_wantSaveQuit = src->m_wantSaveQuit;
    dest->m_pendingNormal = src->m_pendingNormal;
    /* 复制操作不复制撤销历史。 */
}

static void VXTuiVim_move(XTuiVim* dest, XTuiVim* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiVim_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Move, void(*)(XTuiWidget*, XTuiWidget*))((XTuiWidget*)dest, (XTuiWidget*)src);
    dest->m_lines = src->m_lines;
    dest->m_lineCount = src->m_lineCount;
    dest->m_linesCapacity = src->m_linesCapacity;
    src->m_lines = NULL;
    src->m_lineCount = 0;
    src->m_linesCapacity = 0;
    dest->m_cursorLine = src->m_cursorLine;
    dest->m_cursorColumn = src->m_cursorColumn;
    dest->m_topLine = src->m_topLine;
    dest->m_insertMode = src->m_insertMode;
    dest->m_commandMode = src->m_commandMode;
    dest->m_modified = src->m_modified;
    memcpy(dest->m_command, src->m_command, sizeof(dest->m_command));
    dest->m_commandLen = src->m_commandLen;
    memcpy(dest->m_insertBuf, src->m_insertBuf, sizeof(dest->m_insertBuf));
    dest->m_insertLen = src->m_insertLen;
    dest->m_insertCursor = src->m_insertCursor;
    memcpy(dest->m_status, src->m_status, sizeof(dest->m_status));
    dest->m_path = src->m_path;
    src->m_path = NULL;
    dest->m_wantSave = src->m_wantSave;
    dest->m_wantQuit = src->m_wantQuit;
    dest->m_wantSaveQuit = src->m_wantSaveQuit;
    dest->m_pendingNormal = src->m_pendingNormal;
    dest->m_undoLines = src->m_undoLines;
    dest->m_undoCount = src->m_undoCount;
    dest->m_undoCapacity = src->m_undoCapacity;
    src->m_undoLines = NULL;
    src->m_undoCount = 0;
    src->m_undoCapacity = 0;
}

/* ==================== 虚函数表 ==================== */

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

/* ==================== 构造与析构 ==================== */

void XTuiVim_init(XTuiVim* vim)
{
    if (!vim)
        return;
    memset(((XClass*)vim) + 1, 0, sizeof(XTuiVim) - sizeof(XClass));
    XTuiWidget_init((XTuiWidget*)vim);
    XClassGetVtable(vim) = XTuiVim_class_init();
    vim->m_pendingNormal = '\0';
    vim->m_lineCount = 1;
    vim->m_linesCapacity = 4;
    vim->m_lines = (char**)XMalloc_System((size_t)vim->m_linesCapacity * sizeof(char*));
    if (vim->m_lines) {
        memset(vim->m_lines, 0, (size_t)vim->m_linesCapacity * sizeof(char*));
        vim->m_lines[0] = (char*)XMalloc_System(1);
        if (vim->m_lines[0])
            vim->m_lines[0][0] = '\0';
    } else {
        vim->m_linesCapacity = 0;
    }
}

XTuiVim* XTuiVim_create(void)
{
    XTuiVim* vim = (XTuiVim*)XMalloc_System(sizeof(XTuiVim));
    if (!vim)
        return NULL;
    XTuiVim_init(vim);
    Set_Class_MemoryFree(vim, XFree_System);
    return vim;
}

/* ==================== 公开接口 ==================== */

void XTuiVim_setLines(XTuiVim* vim, const char* const* lines, int count)
{
    int i;
    if (!vim)
        return;
    if (count < 0)
        count = 0;
    xvim_free_lines(vim->m_lines, vim->m_lineCount);
    vim->m_lines = NULL;
    vim->m_lineCount = 0;
    vim->m_linesCapacity = 0;
    if (count == 0)
        count = 1;
    if (!xvim_ensure_capacity(vim, count))
        return;
    for (i = 0; i < count; ++i) {
        const char* text = lines && lines[i] ? lines[i] : "";
        size_t len = strlen(text);
        if (len >= XTUI_VIM_LINE_MAX)
            len = XTUI_VIM_LINE_MAX - 1u;
        vim->m_lines[i] = (char*)XMalloc_System(len + 1);
        if (!vim->m_lines[i])
            break;
        memcpy(vim->m_lines[i], text, len);
        vim->m_lines[i][len] = '\0';
        ++vim->m_lineCount;
    }
    vim->m_cursorLine = 0;
    vim->m_cursorColumn = 0;
    vim->m_topLine = 0;
    vim->m_modified = false;
    vim->m_insertMode = false;
    vim->m_commandMode = false;
    vim->m_commandLen = 0;
    vim->m_command[0] = '\0';
    vim->m_pendingNormal = '\0';
    vim->m_insertLen = 0;
    vim->m_insertCursor = 0;
    vim->m_insertBuf[0] = '\0';
    vim->m_wantSave = false;
    vim->m_wantQuit = false;
    vim->m_wantSaveQuit = false;
}

int XTuiVim_lineCount(const XTuiVim* vim)
{
    return vim ? vim->m_lineCount : 0;
}

const char* XTuiVim_line(const XTuiVim* vim, int index)
{
    if (!vim || index < 0 || index >= vim->m_lineCount || !vim->m_lines)
        return "";
    return vim->m_lines[index] ? vim->m_lines[index] : "";
}

void XTuiVim_setPath(XTuiVim* vim, const char* path)
{
    size_t len;
    char* copy;
    if (!vim)
        return;
    if (!path)
        path = "";
    len = strlen(path);
    copy = (char*)XMalloc_System(len + 1);
    if (!copy)
        return;
    memcpy(copy, path, len + 1);
    if (vim->m_path)
        XFree_System(vim->m_path);
    vim->m_path = copy;
}

const char* XTuiVim_path(const XTuiVim* vim)
{
    return vim && vim->m_path ? vim->m_path : "";
}

bool XTuiVim_isModified(const XTuiVim* vim)
{
    return vim ? vim->m_modified : false;
}

void XTuiVim_clearModified(XTuiVim* vim)
{
    if (vim)
        vim->m_modified = false;
}

bool XTuiVim_wantSave(const XTuiVim* vim)
{
    return vim ? vim->m_wantSave : false;
}

bool XTuiVim_wantQuit(const XTuiVim* vim)
{
    return vim ? vim->m_wantQuit : false;
}

bool XTuiVim_wantSaveQuit(const XTuiVim* vim)
{
    return vim ? vim->m_wantSaveQuit : false;
}

void XTuiVim_ackAction(XTuiVim* vim)
{
    if (!vim)
        return;
    vim->m_wantSave = false;
    vim->m_wantQuit = false;
    vim->m_wantSaveQuit = false;
}

void XTuiVim_setInsertMode(XTuiVim* vim, bool insert)
{
    if (vim)
        vim->m_insertMode = insert;
}

bool XTuiVim_isInsertMode(const XTuiVim* vim)
{
    return vim ? vim->m_insertMode : false;
}

#endif /* XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON */
