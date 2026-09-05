/**
 * @file XRegularExpressionTest.c
 * @brief XRegularExpression Qt 6.8 对齐全量测试。
 */

#include "XRegularExpressionTest.h"
#include "XRegularExpression.h"
#include "XRegularExpressionValidator.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XString.h"
#include "XStringView.h"
#include "XStringList.h"
#include "XHashMap.h"
#include "XThread.h"
#include "XAtomic.h"
#include <stdbool.h>
#include <stdint.h>

#define XREGEX_TEST_PASS(name) XPrintf("[PASS] XRegularExpression: %s\n", name)
#define XREGEX_TEST_FAIL(name, reason) XPrintf("[FAIL] XRegularExpression: %s: %s\n", name, reason)
#define XREGEX_REQUIRE(condition, name, reason) \
    do { if (!(condition)) { XREGEX_TEST_FAIL(name, reason); return false; } } while (0)

static bool XRegularExpression_test_create_and_lifecycle(void)
{
    XRegularExpression expression;
    XRegularExpression_init(&expression);
    XREGEX_REQUIRE(XRegularExpression_pattern_const(&expression) != NULL,
                   "init", "pattern 引用为空");
    XREGEX_REQUIRE(XRegularExpression_isValid(&expression), "empty pattern", "空模式应有效");

    XRegularExpression* created = XRegularExpression_create_utf8("abc", 0);
    XREGEX_REQUIRE(created != NULL, "create_utf8", "创建失败");
    XRegularExpression* copied = XRegularExpression_create_copy(created);
    XREGEX_REQUIRE(copied != NULL && XRegularExpression_equals(created, copied),
                   "copy", "拷贝结果不一致");
    XRegularExpression* moved = XRegularExpression_create_move(copied);
    XREGEX_REQUIRE(moved != NULL && XRegularExpression_equals(created, moved),
                   "move", "移动结果不一致");

    XRegularExpressionMatch* match = XRegularExpressionMatch_create();
    XRegularExpressionMatch* matchCopy = XRegularExpressionMatch_create_copy(match);
    XRegularExpressionMatch* matchMove = XRegularExpressionMatch_create_move(matchCopy);
    XREGEX_REQUIRE(match && matchMove && XRegularExpressionMatch_isValid(matchMove),
                   "match lifecycle", "匹配结果生命周期失败");
    XRegularExpressionMatchIterator* iterator = XRegularExpressionMatchIterator_create();
    XRegularExpressionMatchIterator* iteratorCopy =
            XRegularExpressionMatchIterator_create_copy(iterator);
    XRegularExpressionMatchIterator* iteratorMove =
            XRegularExpressionMatchIterator_create_move(iteratorCopy);
    XREGEX_REQUIRE(iterator && iteratorMove &&
                           XRegularExpressionMatchIterator_isValid(iteratorMove),
                   "iterator lifecycle", "迭代器生命周期失败");

    XRegularExpression_delete_base(moved);
    XRegularExpression_delete_base(copied);
    XRegularExpression_delete_base(created);
    XRegularExpressionMatch_delete_base(matchMove);
    XRegularExpressionMatch_delete_base(matchCopy);
    XRegularExpressionMatch_delete_base(match);
    XRegularExpressionMatchIterator_delete_base(iteratorMove);
    XRegularExpressionMatchIterator_delete_base(iteratorCopy);
    XRegularExpressionMatchIterator_delete_base(iterator);
    XRegularExpression_deinit_base(&expression);
    XREGEX_TEST_PASS("create/init/copy/move/deinit/delete");
    return true;
}

static bool XRegularExpression_test_pattern_and_options(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8(
            "a.b", XRegularExpression_CaseInsensitiveOption |
                    XRegularExpression_DotMatchesEverythingOption);
    XREGEX_REQUIRE(expression != NULL, "pattern create", "创建失败");
    XREGEX_REQUIRE(XRegularExpression_patternOptions(expression) ==
                           (XRegularExpression_CaseInsensitiveOption |
                            XRegularExpression_DotMatchesEverythingOption),
                   "patternOptions", "选项不一致");
    XREGEX_REQUIRE(XRegularExpression_isValid(expression), "isValid", "合法模式未通过");
    XREGEX_REQUIRE(XRegularExpression_captureCount(expression) == 0,
                   "captureCount", "无捕获模式计数错误");
    XString* pattern = XRegularExpression_pattern(expression);
    XREGEX_REQUIRE(pattern && XRegularExpression_pattern_const(expression) &&
                           XString_equals(pattern, XRegularExpression_pattern_const(expression),
                                          XChar_CaseSensitive),
                   "pattern getter", "模式读取结果错误");
    XRegularExpression_optimize(expression);
    XString_delete_base(pattern);

    XRegularExpression_setPattern_utf8(expression, "(?<word>[A-Z]+)");
    XRegularExpression_setPatternOptions(expression, XRegularExpression_NoPatternOption);
    XREGEX_REQUIRE(XRegularExpression_isValid(expression), "setPattern", "重新编译失败");
    XStringList* groups = XRegularExpression_namedCaptureGroups(expression);
    XREGEX_REQUIRE(groups != NULL && XStringList_size_base(groups) == 2,
                   "namedCaptureGroups size", "命名捕获列表大小错误");
    XString* groupName = (XString*)XStringList_at_base(groups, 1);
    XREGEX_REQUIRE(groupName && XString_equals_utf8(groupName, "word", XChar_CaseSensitive),
                   "namedCaptureGroups value", "命名捕获名称错误");

    XStringList_delete_base(groups);
    XRegularExpression_delete_base(expression);
    XREGEX_TEST_PASS("pattern/options/captureCount/namedCaptureGroups");
    return true;
}

static bool XRegularExpression_test_match_and_capture(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8(
            "(?<word>[A-Za-z]+)\\s+(?<number>\\d+)", 0);
    XREGEX_REQUIRE(expression != NULL, "match expression", "创建失败");
    XRegularExpressionMatch* match = XRegularExpression_match_utf8(
            expression, "abc 123", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(match && XRegularExpressionMatch_isValid(match), "match valid", "匹配结果无效");
    XREGEX_REQUIRE(XRegularExpressionMatch_hasMatch(match), "hasMatch", "应当完整匹配");
    XREGEX_REQUIRE(XRegularExpressionMatch_lastCapturedIndex(match) == 2,
                   "lastCapturedIndex", "捕获组索引错误");

    XString* whole = XRegularExpressionMatch_captured(match, 0);
    XString* word = XRegularExpressionMatch_captured(match, 1);
    XString* number = XRegularExpressionMatch_captured(match, 2);
    XREGEX_REQUIRE(whole && word && number && XString_equals_utf8(whole, "abc 123", XChar_CaseSensitive) &&
                           XString_equals_utf8(word, "abc", XChar_CaseSensitive) &&
                           XString_equals_utf8(number, "123", XChar_CaseSensitive),
                   "captured", "捕获文本错误");
    XREGEX_REQUIRE(XRegularExpressionMatch_capturedStart(match, 1) == 0 &&
                           XRegularExpressionMatch_capturedLength(match, 1) == 3 &&
                           XRegularExpressionMatch_capturedEnd(match, 1) == 3,
                   "captured offsets", "捕获偏移错误");

    XAnyStringView wordName = XAnyStringView_create_cstr("word");
    XREGEX_REQUIRE(XRegularExpressionMatch_hasCaptured_2(match, &wordName),
                   "named hasCaptured", "命名捕获查询失败");
    XString* named = XRegularExpressionMatch_captured_2(match, &wordName);
    XREGEX_REQUIRE(named && XString_equals_utf8(named, "abc", XChar_CaseSensitive),
                   "named captured", "命名捕获文本错误");
    XREGEX_REQUIRE(XRegularExpressionMatch_capturedStart_2(match, &wordName) == 0 &&
                           XRegularExpressionMatch_capturedLength_2(match, &wordName) == 3 &&
                           XRegularExpressionMatch_capturedEnd_2(match, &wordName) == 3,
                   "named offsets", "命名捕获偏移错误");
    XStringView namedView = XRegularExpressionMatch_capturedView_2(match, &wordName);
    XString* namedViewString = XStringView_toString(&namedView);
    XRegularExpression* matchExpression =
            XRegularExpressionMatch_regularExpression(match);
    XREGEX_REQUIRE(XRegularExpressionMatch_hasCaptured(match, 1) && namedViewString &&
                           XString_equals_utf8(namedViewString, "abc", XChar_CaseSensitive) &&
                           matchExpression && XRegularExpression_equals(matchExpression, expression) &&
                           XRegularExpressionMatch_regularExpression_const(match) &&
                           XRegularExpressionMatch_matchType(match) == XRegularExpression_NormalMatch &&
                           XRegularExpressionMatch_matchOptions(match) == XRegularExpression_NoMatchOption,
                   "match accessors", "匹配属性读取错误");
    XStringView capturedView = XRegularExpressionMatch_capturedView(match, 1);
    XString* capturedViewString = XStringView_toString(&capturedView);
    XREGEX_REQUIRE(capturedViewString &&
                           XString_equals_utf8(capturedViewString, "abc", XChar_CaseSensitive),
                   "captured view", "捕获视图错误");

    XString* viewSubjectString = XString_create_utf8("abc 123");
    XStringView subjectView = XStringView_create_string(viewSubjectString);
    XRegularExpressionMatch* viewMatch = XRegularExpression_matchView(
            expression, &subjectView, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(viewMatch && XRegularExpressionMatch_hasMatch(viewMatch),
                   "matchView", "UTF-16 视图匹配失败");

    XStringList* texts = XRegularExpressionMatch_capturedTexts(match);
    XREGEX_REQUIRE(texts && XStringList_size_base(texts) == 3,
                   "capturedTexts", "捕获文本列表大小错误");

    XString_delete_base(whole);
    XString_delete_base(word);
    XString_delete_base(number);
    XString_delete_base(named);
    XString_delete_base(namedViewString);
    XRegularExpression_delete_base(matchExpression);
    XString_delete_base(capturedViewString);
    XRegularExpressionMatch_delete_base(viewMatch);
    XString_delete_base(viewSubjectString);
    XStringList_delete_base(texts);
    XRegularExpressionMatch_delete_base(match);
    XRegularExpression_delete_base(expression);
    XREGEX_TEST_PASS("normal match/captures/named captures/offsets");
    return true;
}

static bool XRegularExpression_test_match_modes(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8("[A-Z][0-9]", 0);
    XREGEX_REQUIRE(expression != NULL, "match modes expression", "创建失败");
    XRegularExpressionMatch* partial = XRegularExpression_match_utf8(
            expression, "A", 0, XRegularExpression_PartialPreferCompleteMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(partial && XRegularExpressionMatch_isValid(partial) &&
                           XRegularExpressionMatch_hasPartialMatch(partial) &&
                           !XRegularExpressionMatch_hasMatch(partial),
                   "partial match", "部分匹配状态错误");

    XRegularExpressionMatch* noMatch = XRegularExpression_match_utf8(
            expression, "xx", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(noMatch && XRegularExpressionMatch_isValid(noMatch) &&
                           !XRegularExpressionMatch_hasMatch(noMatch),
                   "no match", "无匹配状态错误");

    XRegularExpressionMatch* noRun = XRegularExpression_match_utf8(
            expression, "A1", 0, XRegularExpression_NoMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(noRun && XRegularExpressionMatch_isValid(noRun) &&
                           !XRegularExpressionMatch_hasMatch(noRun),
                   "NoMatch", "NoMatch 模式错误");

    XRegularExpressionMatch_delete_base(partial);
    XRegularExpressionMatch_delete_base(noMatch);
    XRegularExpressionMatch_delete_base(noRun);
    XRegularExpression_delete_base(expression);
    XREGEX_TEST_PASS("normal/partial/NoMatch modes");
    return true;
}

static bool XRegularExpression_test_global_match(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8("\\d+", 0);
    XRegularExpressionMatchIterator* iterator = XRegularExpression_globalMatch_utf8(
            expression, "a12b34", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(iterator && XRegularExpressionMatchIterator_isValid(iterator),
                   "global iterator valid", "迭代器无效");
    XREGEX_REQUIRE(XRegularExpressionMatchIterator_hasNext(iterator),
                   "global hasNext", "缺少第一个结果");
    XRegularExpressionMatch* peek = XRegularExpressionMatchIterator_peekNext(iterator);
    XRegularExpressionMatch* first = XRegularExpressionMatchIterator_next(iterator);
    XREGEX_REQUIRE(peek && first, "global peek/next", "无法取得第一个结果");
    XString* firstText = XRegularExpressionMatch_captured(first, 0);
    XREGEX_REQUIRE(firstText && XString_equals_utf8(firstText, "12", XChar_CaseSensitive),
                   "global first", "第一个全局结果错误");
    XRegularExpressionMatch_delete_base(peek);
    XRegularExpressionMatch_delete_base(first);
    XString_delete_base(firstText);

    XRegularExpressionMatch* second = XRegularExpressionMatchIterator_next(iterator);
    XString* secondText = second ? XRegularExpressionMatch_captured(second, 0) : NULL;
    XREGEX_REQUIRE(second && secondText && XString_equals_utf8(secondText, "34", XChar_CaseSensitive),
                   "global second", "第二个全局结果错误");
    XREGEX_REQUIRE(!XRegularExpressionMatchIterator_hasNext(iterator),
                   "global end", "迭代器未到末尾");
    XRegularExpression* iteratorExpression =
            XRegularExpressionMatchIterator_regularExpression(iterator);
    XREGEX_REQUIRE(iteratorExpression &&
                           XRegularExpression_equals(iteratorExpression, expression) &&
                           XRegularExpressionMatchIterator_regularExpression_const(iterator) &&
                           XRegularExpressionMatchIterator_matchType(iterator) ==
                                   XRegularExpression_NormalMatch &&
                           XRegularExpressionMatchIterator_matchOptions(iterator) ==
                                   XRegularExpression_NoMatchOption,
                   "iterator accessors", "迭代器属性读取错误");

    XString* globalSubject = XString_create_utf8("a12b34");
    XStringView globalSubjectView = XStringView_create_string(globalSubject);
    XRegularExpressionMatchIterator* viewIterator = XRegularExpression_globalMatchView(
            expression, &globalSubjectView, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(viewIterator && XRegularExpressionMatchIterator_hasNext(viewIterator),
                   "globalMatchView", "UTF-16 视图全局匹配失败");

    XString_delete_base(secondText);
    XRegularExpressionMatch_delete_base(second);
    XRegularExpression_delete_base(iteratorExpression);
    XRegularExpressionMatchIterator_delete_base(viewIterator);
    XString_delete_base(globalSubject);
    XRegularExpressionMatchIterator_delete_base(iterator);
    XRegularExpression_delete_base(expression);
    XREGEX_TEST_PASS("globalMatch/peekNext/next/end");
    return true;
}

static bool XRegularExpression_test_global_edge_cases(void)
{
    XRegularExpression* emptyAlternation = XRegularExpression_create_utf8("a*|b", 0);
    XRegularExpressionMatchIterator* emptyIterator =
            XRegularExpression_globalMatch_utf8(emptyAlternation, "b", 0,
                                                XRegularExpression_NormalMatch,
                                                XRegularExpression_NoMatchOption);
    const int64_t expectedStarts[] = { 0, 0, 1 };
    const int64_t expectedEnds[] = { 0, 1, 1 };
    XREGEX_REQUIRE(emptyAlternation && emptyIterator, "global empty create", "空匹配迭代器创建失败");
    for (size_t i = 0; i < 3; ++i) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(emptyIterator);
        XREGEX_REQUIRE(match && XRegularExpressionMatch_capturedStart(match, 0) == expectedStarts[i] &&
                               XRegularExpressionMatch_capturedEnd(match, 0) == expectedEnds[i],
                       "global empty progression", "空匹配后的同位置非空匹配或偏移错误");
        XRegularExpressionMatch_delete_base(match);
    }
    XREGEX_REQUIRE(!XRegularExpressionMatchIterator_hasNext(emptyIterator),
                   "global empty end", "空匹配迭代器未正常结束");
    XRegularExpressionMatchIterator_delete_base(emptyIterator);
    XRegularExpression_delete_base(emptyAlternation);

    XRegularExpression* dot = XRegularExpression_create_utf8(".*", 0);
    XRegularExpressionMatchIterator* dotIterator = XRegularExpression_globalMatch_utf8(
            dot, "the\nquick\nfox\n", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    const char* dotTexts[] = { "the", "", "quick", "", "fox", "", "" };
    XREGEX_REQUIRE(dot && dotIterator, "global dot create", "换行空匹配迭代器创建失败");
    for (size_t i = 0; i < 7; ++i) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(dotIterator);
        XString* text = match ? XRegularExpressionMatch_captured(match, 0) : NULL;
        XREGEX_REQUIRE(match && text && XString_equals_utf8(text, dotTexts[i],
                                                            XChar_CaseSensitive),
                       "global dot progression", "换行空匹配序列与 Qt 不一致");
        XString_delete_base(text);
        XRegularExpressionMatch_delete_base(match);
    }
    XREGEX_REQUIRE(!XRegularExpressionMatchIterator_hasNext(dotIterator),
                   "global dot end", "换行空匹配迭代器未结束");
    XRegularExpressionMatchIterator_delete_base(dotIterator);
    XRegularExpression_delete_base(dot);

    XRegularExpression* crlf = XRegularExpression_create_utf8("(*CRLF).*", 0);
    XRegularExpressionMatchIterator* crlfIterator = XRegularExpression_globalMatch_utf8(
            crlf, "the\r\nquick\r\nfox", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    const char* crlfTexts[] = { "the", "", "quick", "", "fox", "" };
    XREGEX_REQUIRE(crlf && crlfIterator, "global CRLF create", "CRLF 正则迭代器创建失败");
    for (size_t i = 0; i < 6; ++i) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(crlfIterator);
        XString* text = match ? XRegularExpressionMatch_captured(match, 0) : NULL;
        XREGEX_REQUIRE(match && text && XString_equals_utf8(text, crlfTexts[i],
                                                            XChar_CaseSensitive),
                       "global CRLF progression", "CRLF 空匹配跳过规则错误");
        XString_delete_base(text);
        XRegularExpressionMatch_delete_base(match);
    }
    XREGEX_REQUIRE(!XRegularExpressionMatchIterator_hasNext(crlfIterator),
                   "global CRLF end", "CRLF 空匹配迭代器未结束");
    XRegularExpressionMatchIterator_delete_base(crlfIterator);
    XRegularExpression_delete_base(crlf);

    XRegularExpression* surrogate = XRegularExpression_create_utf8(
            "[\\x{0000}-\\x{FFFF}]*", 0);
    XRegularExpressionMatchIterator* surrogateIterator = XRegularExpression_globalMatch_utf8(
            surrogate, "ABC\xf0\x9d\x85\x9d" "DEF\xf0\x9d\x85\x9e" "GHI", 0,
            XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
    const char* surrogateTexts[] = { "ABC", "", "DEF", "", "GHI", "" };
    XREGEX_REQUIRE(surrogate && surrogateIterator, "global surrogate create", "代理对迭代器创建失败");
    for (size_t i = 0; i < 6; ++i) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(surrogateIterator);
        XString* text = match ? XRegularExpressionMatch_captured(match, 0) : NULL;
        XREGEX_REQUIRE(match && text && XString_equals_utf8(text, surrogateTexts[i],
                                                            XChar_CaseSensitive),
                       "global surrogate progression", "代理对没有按 UTF-16 code point 跳过");
        XString_delete_base(text);
        XRegularExpressionMatch_delete_base(match);
    }
    XREGEX_REQUIRE(!XRegularExpressionMatchIterator_hasNext(surrogateIterator),
                   "global surrogate end", "代理对迭代器未结束");
    XRegularExpressionMatchIterator_delete_base(surrogateIterator);
    XRegularExpression_delete_base(surrogate);

    XRegularExpression* lookbehind = XRegularExpression_create_utf8("\\bstring\\b", 0);
    XRegularExpressionMatch* partial = XRegularExpression_match_utf8(
            lookbehind, "a str", 2, XRegularExpression_PartialPreferCompleteMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(lookbehind && partial && XRegularExpressionMatch_hasPartialMatch(partial) &&
                           XRegularExpressionMatch_capturedStart(partial, 0) == 1,
                   "partial lookbehind", "部分匹配的 lookbehind 起点未按 Qt 调整");

    XRegularExpression* empty = XRegularExpression_create_utf8("", 0);
    XRegularExpressionMatch* emptySubject = XRegularExpression_match_utf8(
            empty, "", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XStringView emptyView = XRegularExpressionMatch_capturedView(emptySubject, 0);
    XString* emptyCaptured = XStringView_toString(&emptyView);
    XREGEX_REQUIRE(empty && emptySubject && emptyCaptured && XString_isEmpty_base(emptyCaptured),
                   "empty captured view", "空主题的空捕获视图访问失败");

    XString_delete_base(emptyCaptured);
    XRegularExpressionMatch_delete_base(emptySubject);
    XRegularExpression_delete_base(empty);
    XRegularExpressionMatch_delete_base(partial);
    XRegularExpression_delete_base(lookbehind);
    XREGEX_TEST_PASS("global empty/CRLF/surrogate/partial boundary cases");
    return true;
}

static bool XRegularExpression_test_swap(void)
{
    XRegularExpression* left = XRegularExpression_create_utf8("left", 0);
    XRegularExpression* right = XRegularExpression_create_utf8("right", 0);
    XREGEX_REQUIRE(left && right, "swap expression create", "交换测试对象创建失败");
    XRegularExpression_swap(left, right);
    XString* leftPattern = XRegularExpression_pattern(left);
    XString* rightPattern = XRegularExpression_pattern(right);
    XREGEX_REQUIRE(leftPattern && rightPattern &&
                           XString_equals_utf8(leftPattern, "right", XChar_CaseSensitive) &&
                           XString_equals_utf8(rightPattern, "left", XChar_CaseSensitive),
                   "swap expression", "正则对象交换结果错误");

    XRegularExpressionMatch* leftMatch = XRegularExpression_match_utf8(
            left, "right", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XRegularExpressionMatch* rightMatch = XRegularExpression_match_utf8(
            right, "left", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(leftMatch && rightMatch, "swap match create", "匹配交换对象创建失败");
    XRegularExpressionMatch_swap(leftMatch, rightMatch);
    XString* swappedMatchText = XRegularExpressionMatch_captured(leftMatch, 0);
    XREGEX_REQUIRE(swappedMatchText && XString_equals_utf8(swappedMatchText, "left",
                                                           XChar_CaseSensitive),
                   "swap match", "匹配结果交换错误");

    XRegularExpression* first = XRegularExpression_create_utf8("1", 0);
    XRegularExpression* second = XRegularExpression_create_utf8("2", 0);
    XRegularExpressionMatchIterator* firstIterator = XRegularExpression_globalMatch_utf8(
            first, "1", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XRegularExpressionMatchIterator* secondIterator = XRegularExpression_globalMatch_utf8(
            second, "2", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(first && second && firstIterator && secondIterator,
                   "swap iterator create", "迭代器交换对象创建失败");
    XRegularExpressionMatchIterator_swap(firstIterator, secondIterator);
    XRegularExpressionMatch* swappedIteratorMatch =
            XRegularExpressionMatchIterator_next(firstIterator);
    XString* swappedIteratorText = swappedIteratorMatch ?
            XRegularExpressionMatch_captured(swappedIteratorMatch, 0) : NULL;
    XREGEX_REQUIRE(swappedIteratorText && XString_equals_utf8(swappedIteratorText, "2",
                                                               XChar_CaseSensitive),
                   "swap iterator", "迭代器交换结果错误");

    XString_delete_base(swappedIteratorText);
    XRegularExpressionMatch_delete_base(swappedIteratorMatch);
    XRegularExpressionMatchIterator_delete_base(firstIterator);
    XRegularExpressionMatchIterator_delete_base(secondIterator);
    XRegularExpression_delete_base(first);
    XRegularExpression_delete_base(second);
    XString_delete_base(swappedMatchText);
    XRegularExpressionMatch_delete_base(leftMatch);
    XRegularExpressionMatch_delete_base(rightMatch);
    XString_delete_base(leftPattern);
    XString_delete_base(rightPattern);
    XRegularExpression_delete_base(left);
    XRegularExpression_delete_base(right);
    XREGEX_TEST_PASS("regular expression/match/iterator swap");
    return true;
}

#if XTHREAD_ON
static XRegularExpression* g_regularExpressionThreadExpression;
static XAtomic_int32_t g_regularExpressionThreadFailures;

static void XRegularExpression_thread_reader(XThread* thread, XVarList* varList)
{
    (void)thread;
    (void)varList;
    for (int i = 0; i < 64; ++i) {
        XRegularExpressionMatch* match = XRegularExpression_match_utf8(
                g_regularExpressionThreadExpression, "abc 123", 0,
                XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
        if (!match || !XRegularExpressionMatch_isValid(match) ||
                !XRegularExpressionMatch_hasMatch(match)) {
            XAtomic_fetch_add_int32(&g_regularExpressionThreadFailures, 1,
                                    XAtomic_MemoryOrder_Relaxed);
        }
        if (match) XRegularExpressionMatch_delete_base(match);
    }
}

static bool XRegularExpression_test_thread_safe_readers(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8("[a-z]+\\s+\\d+", 0);
    XThread* threads[4] = { NULL, NULL, NULL, NULL };
    size_t started = 0;
    XREGEX_REQUIRE(expression != NULL, "thread expression create", "并发测试正则创建失败");
    XAtomic_init(g_regularExpressionThreadFailures, 0);
    g_regularExpressionThreadExpression = expression;
    for (size_t i = 0; i < 4; ++i) {
        threads[i] = XThread_create_func(XRegularExpression_thread_reader, NULL);
        if (!threads[i] || !XThread_start(threads[i])) break;
        ++started;
    }
    for (size_t i = 0; i < started; ++i) {
        XThread_wait(threads[i], 10000);
        XClass_delete_base((XClass*)threads[i]);
    }
    for (size_t i = started; i < 4; ++i) {
        if (threads[i]) XClass_delete_base((XClass*)threads[i]);
    }
    g_regularExpressionThreadExpression = NULL;
    int32_t failures = XAtomic_load_int32(&g_regularExpressionThreadFailures,
                                          XAtomic_MemoryOrder_Relaxed);
    XRegularExpression_delete_base(expression);
    XREGEX_REQUIRE(started == 4 && failures == 0, "thread safe readers",
                   "只读并发匹配失败");
    XREGEX_TEST_PASS("XMutex 保护的并发只读匹配");
    return true;
}
#endif // XTHREAD_ON

static bool XRegularExpression_test_conversion(void)
{
    XString* literal = XString_create_utf8("a.b");
    XString* escaped = XRegularExpression_escape_2(literal);
    XREGEX_REQUIRE(escaped && XString_equals_utf8(escaped, "a\\.b", XChar_CaseSensitive),
                   "escape", "转义结果错误");
    XStringView literalView = XStringView_create_string(literal);
    XString* escapedView = XRegularExpression_escape(&literalView);
    XString* anchored = XRegularExpression_anchoredPattern(&literalView);
    XREGEX_REQUIRE(escapedView && anchored &&
                           XString_equals_utf8(escapedView, "a\\.b", XChar_CaseSensitive) &&
                           XString_equals_utf8(anchored, "\\A(?:a.b)\\z", XChar_CaseSensitive),
                   "escape/anchored view", "视图转换结果错误");

    XString* wildcard = XString_create_utf8("*.txt");
    XString* converted = XRegularExpression_wildcardToRegularExpression_2(
            wildcard, XRegularExpression_DefaultWildcardConversion);
    XREGEX_REQUIRE(converted && XString_equals_utf8(converted, "\\A(?:[^/]*\\.txt)\\z",
                                                    XChar_CaseSensitive),
                   "wildcard conversion", "通配符转换结果错误");
    XString* backslashWildcard = XString_create_utf8("\\");
    XString* convertedBackslash = XRegularExpression_wildcardToRegularExpression_2(
            backslashWildcard, XRegularExpression_DefaultWildcardConversion);
    XREGEX_REQUIRE(backslashWildcard && convertedBackslash &&
                           XString_equals_utf8(convertedBackslash, "\\A(?:\\\\)\\z",
                                               XChar_CaseSensitive),
                   "wildcard backslash conversion", "通配符反斜杠转义结果错误");
    XStringView wildcardView = XStringView_create_string(wildcard);
    XString* convertedView = XRegularExpression_wildcardToRegularExpression(
            &wildcardView, XRegularExpression_DefaultWildcardConversion);
    XREGEX_REQUIRE(convertedView && XString_equals(converted, convertedView,
                                                   XChar_CaseSensitive),
                   "wildcard view conversion", "通配符视图转换结果错误");
    XRegularExpression* fromWildcard = XRegularExpression_fromWildcard(
            &wildcardView,
            XChar_CaseSensitive, XRegularExpression_DefaultWildcardConversion);
    XREGEX_REQUIRE(fromWildcard != NULL, "fromWildcard", "通配符表达式创建失败");
    /* 使用 UTF-8 创建接口验证通配符生成的正则，避免依赖临时视图生命周期。 */
    XRegularExpression* regex = XRegularExpression_create();
    XRegularExpression_setPattern(regex, converted);
    XRegularExpressionMatch* match = XRegularExpression_match_utf8(
            regex, "report.txt", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XREGEX_REQUIRE(match && XRegularExpressionMatch_hasMatch(match),
                   "wildcard match", "通配符正则无法匹配");

    XRegularExpression* validatorExpression = XRegularExpression_create_utf8("[A-Z][0-9]", 0);
    XRegularExpressionValidator* validator = XRegularExpressionValidator_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, validatorExpression);
    XREGEX_REQUIRE(validator && validatorExpression, "validator create", "校验器创建失败");
    int64_t position = 0;
    XRegularExpressionValidator_setRegularExpression(validator, validatorExpression);
    XRegularExpression* validatorGetter =
            XRegularExpressionValidator_regularExpression(validator);
    XRegularExpressionValidator* validatorCopy =
            XRegularExpressionValidator_create_copy(validator);
    XRegularExpressionValidator* validatorMove =
            XRegularExpressionValidator_create_move(validatorCopy);
    XREGEX_REQUIRE(validatorGetter && validatorMove &&
                           XRegularExpression_equals(validatorGetter, validatorExpression) &&
                           XRegularExpressionValidator_regularExpression_const(validatorMove),
                   "validator lifecycle", "校验器拷贝移动失败");
    XREGEX_REQUIRE(XRegularExpressionValidator_validate_utf8(validator, "A", &position) ==
                           XRegularExpressionValidator_Intermediate,
                   "validator intermediate", "校验器部分状态错误");
    XREGEX_REQUIRE(XRegularExpressionValidator_validate_utf8(validator, "A5", &position) ==
                           XRegularExpressionValidator_Acceptable,
                   "validator acceptable", "校验器可接受状态错误");
    XString* acceptableInput = XString_create_utf8("A5");
    XREGEX_REQUIRE(acceptableInput &&
                           XRegularExpressionValidator_validate(validator, acceptableInput, &position) ==
                                   XRegularExpressionValidator_Acceptable,
                   "validator UTF-16", "校验器 UTF-16 状态错误");
    XREGEX_REQUIRE(XRegularExpressionValidator_validate_utf8(validator, "_", &position) ==
                           XRegularExpressionValidator_Invalid && position == 1,
                   "validator invalid", "校验器无效状态错误");

    XRegularExpressionMatch_delete_base(match);
    XRegularExpression_delete_base(regex);
    XRegularExpression_delete_base(fromWildcard);
    XRegularExpression_delete_base(validatorGetter);
    XRegularExpressionValidator_delete_base(validatorMove);
    XRegularExpressionValidator_delete_base(validatorCopy);
    XRegularExpressionValidator_delete_base(validator);
    XRegularExpression_delete_base(validatorExpression);
    XString_delete_base(acceptableInput);
    XString_delete_base(convertedView);
    XString_delete_base(converted);
    XString_delete_base(convertedBackslash);
    XString_delete_base(backslashWildcard);
    XString_delete_base(anchored);
    XString_delete_base(escapedView);
    XString_delete_base(wildcard);
    XString_delete_base(escaped);
    XString_delete_base(literal);
    XREGEX_TEST_PASS("escape/wildcard/anchored/validator");
    return true;
}

static bool XRegularExpression_test_invalid_and_null(void)
{
    XRegularExpression* invalid = XRegularExpression_create_utf8("(", 0);
    XREGEX_REQUIRE(invalid && !XRegularExpression_isValid(invalid), "invalid pattern", "非法模式被接受");
    XREGEX_REQUIRE(XRegularExpression_patternErrorOffset(invalid) >= 0,
                   "error offset", "错误偏移未设置");
    XString* error = XRegularExpression_errorString(invalid);
    XREGEX_REQUIRE(error && !XString_isEmpty_base(error), "error string", "错误文本为空");

    XREGEX_REQUIRE(!XRegularExpression_isValid(NULL) &&
                           XRegularExpression_patternErrorOffset(NULL) == -1 &&
                           XRegularExpressionMatchIterator_hasNext(NULL) == false,
                   "NULL safety", "NULL 安全行为错误");

    XString_delete_base(error);
    XRegularExpression_delete_base(invalid);
    XREGEX_TEST_PASS("invalid pattern/error/NULL safety");
    return true;
}

static bool XRegularExpression_test_string_consumers(void)
{
    XString* text = XString_create_utf8("a1 b2");
    XRegularExpression* digit = XRegularExpression_create_utf8("\\d", 0);
    XREGEX_REQUIRE(text && digit, "string consumer create", "创建失败");
    XREGEX_REQUIRE(XString_indexOf_regularExpression(text, digit, 0, NULL) == 1 &&
                           XString_indexOf_regularExpression(text, digit, -1, NULL) == 4 &&
                           XString_lastIndexOf_regularExpression(text, digit, -1, NULL) == 4 &&
                           XString_contains_regularExpression(text, digit) &&
                           XString_count_regularExpression(text, digit) == 2,
                   "string search consumers", "字符串正则查找结果错误");

    XRegularExpressionMatch* preservedMatch = XRegularExpression_match_utf8(
            digit, "7", 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    XRegularExpression* absent = XRegularExpression_create_utf8("z", 0);
    XREGEX_REQUIRE(preservedMatch &&
                           absent &&
                           XString_indexOf_regularExpression(text, digit, 0, preservedMatch) == 1 &&
                           XString_indexOf_regularExpression(text, absent, 0, preservedMatch) == -1 &&
                           XRegularExpressionMatch_capturedStart(preservedMatch, 0) == 1,
                   "string index match output", "未匹配时不应覆盖已有匹配结果");
    XREGEX_REQUIRE(XString_contains_regularExpression_2(text, digit, preservedMatch) &&
                           XRegularExpressionMatch_capturedStart(preservedMatch, 0) == 1 &&
                           !XString_contains_regularExpression_2(text, absent, preservedMatch) &&
                           XRegularExpressionMatch_capturedStart(preservedMatch, 0) == 1,
                   "string contains match output", "contains 匹配输出行为错误");

    XString* replacement = XString_create_utf8("[\\0]");
    XREGEX_REQUIRE(replacement && XString_replace_regularExpression(text, digit, replacement) &&
                           XString_equals_utf8(text, "a[\\0] b[\\0]", XChar_CaseSensitive),
                   "string replace literal capture", "\\0 应保持为普通文本");

    XString* numberedText = XString_create_utf8("abc-123 def-456");
    XRegularExpression* numbered = XRegularExpression_create_utf8(
            "([a-z]+)-(\\d+)", 0);
    XString* numberedReplacement = XString_create_utf8("\\2/\\1");
    XREGEX_REQUIRE(numberedText && numbered && numberedReplacement &&
                           XString_replace_regularExpression(numberedText, numbered,
                                                              numberedReplacement) &&
                           XString_equals_utf8(numberedText, "123/abc 456/def",
                                               XChar_CaseSensitive),
                   "string replace captures", "数字捕获组替换结果错误");

    XString* overlapText = XString_create_utf8("banana");
    XRegularExpression* overlap = XRegularExpression_create_utf8("ana", 0);
    XREGEX_REQUIRE(overlapText && overlap &&
                           XString_count_regularExpression(overlapText, overlap) == 2,
                   "string regex overlap count", "正则计数没有统计重叠匹配");

    XString* emptyText = XString_create_utf8("abc");
    XRegularExpression* emptyExpression = XRegularExpression_create_utf8("", 0);
    XREGEX_REQUIRE(emptyText && emptyExpression &&
                           XString_lastIndexOf_regularExpression(emptyText, emptyExpression, -1,
                                                                 NULL) == 2 &&
                           XString_lastIndexOf_regularExpression(emptyText, emptyExpression, 0,
                                                                 NULL) == 0,
                   "string regex last index", "负 from 的末尾空匹配边界错误");

    XString* csv = XString_create_utf8("a,,b,");
    XRegularExpression* comma = XRegularExpression_create_utf8(",", 0);
    XStringList* split = XString_split_regularExpression(csv, comma, true);
    XREGEX_REQUIRE(split && XStringList_size_base(split) == 4,
                   "string split consumer", "字符串正则分割数量错误");

    XRegularExpression* invalidSeparator = XRegularExpression_create_utf8("[", 0);
    XStringList* invalidSplit = XString_split_regularExpression(csv, invalidSeparator, true);
    XREGEX_REQUIRE(invalidSeparator && invalidSplit &&
                           XStringList_size_base(invalidSplit) == 0,
                   "invalid regex split", "无效正则分割应返回空列表");

    XStringList* list = XStringList_create();
    XStringList_push_back_utf8(list, "one1");
    XStringList_push_back_utf8(list, "two");
    XStringList_push_back_utf8(list, "three3");
    XStringList* filtered = XStringList_filter_regularExpression(list, digit);
    XRegularExpression* wholeWordDigit = XRegularExpression_create_utf8("\\w+\\d", 0);
    XREGEX_REQUIRE(filtered && XStringList_size_base(filtered) == 2 &&
                           wholeWordDigit &&
                           XStringList_indexOf_regularExpression(list, wholeWordDigit, 0) == 0 &&
                           XStringList_lastIndexOf_regularExpression(list, wholeWordDigit, -1) == 2,
                   "string list regex consumers", "字符串列表正则结果错误");
    XREGEX_REQUIRE(XStringList_replaceInStrings_regularExpression(list, digit,
                                                                  numberedReplacement),
                   "string list replace", "字符串列表正则替换失败");

    XString* removed = XString_create_utf8("a1b2");
    XREGEX_REQUIRE(removed && XString_remove_regularExpression(removed, digit) &&
                           XString_equals_utf8(removed, "ab", XChar_CaseSensitive),
                   "string regex remove", "正则移除结果错误");

    XString* sectionText = XString_create_utf8("a,b,c");
    XString* section = XString_section_regularExpression(sectionText, comma, 1, -1,
                                                          XString_SectionDefault);
    XString* sectionWithSeparators = XString_section_regularExpression(
            sectionText, comma, 1, 1,
            XString_SectionIncludeLeadingSep | XString_SectionIncludeTrailingSep);
    XREGEX_REQUIRE(section && sectionWithSeparators &&
                           XString_equals_utf8(section, "b,c", XChar_CaseSensitive) &&
                           XString_equals_utf8(sectionWithSeparators, ",b,",
                                               XChar_CaseSensitive),
                   "string regex section", "正则分段结果错误");

    XString* sectionEmptyText = XString_create_utf8("a,,b,");
    XString* sectionSkipEmpty = XString_section_regularExpression(
            sectionEmptyText, comma, 0, -1, XString_SectionSkipEmpty);
    XRegularExpression* insensitiveComma = XRegularExpression_create_utf8("x", 0);
    XString* insensitiveSectionText = XString_create_utf8("aXb");
    XString* insensitiveSection = XString_section_regularExpression(
            insensitiveSectionText, insensitiveComma, 0, -1,
            XString_SectionCaseInsensitiveSeps);
    XREGEX_REQUIRE(sectionEmptyText && sectionSkipEmpty && insensitiveComma &&
                           insensitiveSectionText && insensitiveSection &&
                           XString_equals_utf8(sectionSkipEmpty, "a,,b", XChar_CaseSensitive) &&
                           XString_equals_utf8(insensitiveSection, "aXb", XChar_CaseSensitive),
                   "string regex section flags", "正则分段标志结果错误");

    XString* viewText = XString_create_utf8("a12b34");
    XStringView view = XStringView_create_string(viewText);
    XREGEX_REQUIRE(viewText && XStringView_indexOf_regularExpression(&view, digit, 0, NULL) == 1 &&
                           XStringView_indexOf_regularExpression(&view, digit, -1, NULL) == 5 &&
                           XStringView_lastIndexOf_regularExpression(&view, digit, -1, NULL) == 5 &&
                           XStringView_count_regularExpression(&view, digit) == 4 &&
                           XStringView_contains_regularExpression(&view, digit),
                   "string view regex consumers", "字符串视图正则结果错误");
    XREGEX_REQUIRE(XStringView_contains_regularExpression_2(&view, digit, preservedMatch) &&
                           XRegularExpressionMatch_capturedStart(preservedMatch, 0) == 1,
                   "string view contains match output", "字符串视图 contains 输出行为错误");
    XStringList* viewSplit = XStringView_split_regularExpression(&view, comma, true);
    XREGEX_REQUIRE(viewSplit != NULL, "string view regex split", "字符串视图正则分割失败");

    XString_delete_base(viewText);
    XStringList_delete_base(invalidSplit);
    XRegularExpression_delete_base(invalidSeparator);
    XRegularExpression_delete_base(absent);
    XRegularExpressionMatch_delete_base(preservedMatch);
    XStringList_delete_base(viewSplit);
    XString_delete_base(insensitiveSection);
    XString_delete_base(insensitiveSectionText);
    XRegularExpression_delete_base(insensitiveComma);
    XString_delete_base(sectionSkipEmpty);
    XString_delete_base(sectionEmptyText);
    XString_delete_base(sectionWithSeparators);
    XString_delete_base(section);
    XString_delete_base(sectionText);
    XString_delete_base(removed);
    XRegularExpression_delete_base(wholeWordDigit);
    XStringList_delete_base(filtered);
    XStringList_delete_base(list);
    XStringList_delete_base(split);
    XRegularExpression_delete_base(comma);
    XString_delete_base(csv);
    XString_delete_base(emptyText);
    XRegularExpression_delete_base(emptyExpression);
    XString_delete_base(overlapText);
    XRegularExpression_delete_base(overlap);
    XString_delete_base(numberedReplacement);
    XRegularExpression_delete_base(numbered);
    XString_delete_base(numberedText);
    XString_delete_base(replacement);
    XRegularExpression_delete_base(digit);
    XString_delete_base(text);
    XREGEX_TEST_PASS("XString/XStringList 正则消费者");
    return true;
}

static bool XRegularExpression_test_hash_map(void)
{
    XRegularExpression* expression = XRegularExpression_create_utf8(
            "(?<word>[A-Z]+)", XRegularExpression_CaseInsensitiveOption);
    XRegularExpression* equivalent = XRegularExpression_create_utf8(
            "(?<word>[A-Z]+)", XRegularExpression_CaseInsensitiveOption);
    XRegularExpression* different = XRegularExpression_create_utf8(
            "(?<word>[A-Z]+)", XRegularExpression_NoPatternOption);
    XREGEX_REQUIRE(expression && equivalent && different,
                   "regular expression hash create", "正则哈希测试对象创建失败");
    XREGEX_REQUIRE(XRegularExpression_hash(expression, sizeof(*expression)) ==
                           XRegularExpression_hash(equivalent, sizeof(*equivalent)) &&
                           XRegularExpression_compare(expression, equivalent) == XCompare_Equality &&
                           XRegularExpression_compare(expression, different) != XCompare_Equality,
                   "regular expression hash equality", "相同正则的哈希或比较结果错误");

    XHashMap* map = XHashMap_create(sizeof(XRegularExpression), sizeof(int),
                                    XRegularExpression_hash, XRegularExpression_compare);
    XREGEX_REQUIRE(map != NULL, "regular expression hash map", "正则哈希映射创建失败");
    XMapBaseSetKeyCopyMethod(map, XClass_copy_base);
    XMapBaseSetKeyMoveMethod(map, XClass_move_base);
    XMapBaseSetKeyDeinitMethod(map, XRegularExpression_deinit_base);
    int value = 68;
    XREGEX_REQUIRE(XHashMap_insert_base(map, expression, &value),
                   "regular expression hash insert", "正则哈希键插入失败");
    int* found = (int*)XHashMap_value_base(map, equivalent);
    XREGEX_REQUIRE(found && *found == value && XHashMap_size_base(map) == 1,
                   "regular expression hash lookup", "正则哈希键查找失败");
    XREGEX_REQUIRE(XHashMap_remove_base(map, equivalent) && XHashMap_size_base(map) == 0,
                   "regular expression hash remove", "正则哈希键删除失败");

    XHashMap_delete_base(map);
    XRegularExpression_delete_base(different);
    XRegularExpression_delete_base(equivalent);
    XRegularExpression_delete_base(expression);
    XREGEX_TEST_PASS("XHashMap 正则键哈希/比较/生命周期");
    return true;
}

static bool XRegularExpression_test_all(void)
{
    bool ok = true;
    ok = XRegularExpression_test_create_and_lifecycle() && ok;
    ok = XRegularExpression_test_pattern_and_options() && ok;
    ok = XRegularExpression_test_match_and_capture() && ok;
    ok = XRegularExpression_test_match_modes() && ok;
    ok = XRegularExpression_test_global_match() && ok;
    ok = XRegularExpression_test_global_edge_cases() && ok;
    ok = XRegularExpression_test_swap() && ok;
    ok = XRegularExpression_test_conversion() && ok;
    ok = XRegularExpression_test_string_consumers() && ok;
#if XTHREAD_ON
#endif // XTHREAD_ON
    ok = XRegularExpression_test_hash_map() && ok;
    ok = XRegularExpression_test_invalid_and_null() && ok;
    XPrintf("[RESULT] XRegularExpression: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void XRegularExpression_test_all_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_all();
}

static void XRegularExpression_test_lifecycle_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_create_and_lifecycle();
}

static void XRegularExpression_test_match_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_match_and_capture();
}

static void XRegularExpression_test_global_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_global_match();
}

static void XRegularExpression_test_edge_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_global_edge_cases();
}

static void XRegularExpression_test_conversion_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_conversion();
}

static void XRegularExpression_test_string_consumers_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_string_consumers();
}

#if XTHREAD_ON
static void XRegularExpression_test_thread_wrapper(XVariant* data)
{
    (void)data;
    XRegularExpression_test_thread_safe_readers();
}
#endif // XTHREAD_ON

void XTestMenu_XRegularExpressionTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XRegularExpression(Qt6.8)");
    XTestMenu_addMenu(root, menu);
    XAction* action = XTestMenu_addAction(menu, "全部测试");
    XTestMenu_setActionFunction(action, XRegularExpression_test_all_wrapper);
    action = XTestMenu_addAction(menu, "生命周期/拷贝/移动");
    XTestMenu_setActionFunction(action, XRegularExpression_test_lifecycle_wrapper);
    action = XTestMenu_addAction(menu, "普通匹配/捕获组");
    XTestMenu_setActionFunction(action, XRegularExpression_test_match_wrapper);
    action = XTestMenu_addAction(menu, "全局匹配迭代器");
    XTestMenu_setActionFunction(action, XRegularExpression_test_global_wrapper);
    action = XTestMenu_addAction(menu, "空匹配/Unicode/部分匹配边界");
    XTestMenu_setActionFunction(action, XRegularExpression_test_edge_wrapper);
    action = XTestMenu_addAction(menu, "转义/通配符/校验器");
    XTestMenu_setActionFunction(action, XRegularExpression_test_conversion_wrapper);
    action = XTestMenu_addAction(menu, "XString/XStringList消费者");
    XTestMenu_setActionFunction(action, XRegularExpression_test_string_consumers_wrapper);
#if XTHREAD_ON
    action = XTestMenu_addAction(menu, "并发只读匹配");
    XTestMenu_setActionFunction(action, XRegularExpression_test_thread_wrapper);
#endif // XTHREAD_ON
}
