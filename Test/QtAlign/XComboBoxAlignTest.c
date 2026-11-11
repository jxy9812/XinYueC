/**
 * @file       XComboBoxAlignTest.c
 * @brief      XComboBox × Qt 6.8 QComboBox 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qcombobox.h
 *               （count/maxCount/maxVisibleItems/duplicatesEnabled/frame/
 *               insertPolicy/sizeAdjustPolicy/minimumContentsLength/iconSize/
 *               placeholderText/editable/lineEdit/validator/completer/
 *               itemDelegate/model/view/modelColumn/rootModelIndex/
 *               currentIndex/currentText/currentData/itemText/itemIcon/
 *               itemData/addItem(s)/insertItem(s)/insertSeparator/removeItem/
 *               setItemText/setItemIcon/setItemData/clear/clearEditText/
 *               setEditText/setCurrentIndex/setCurrentText/showPopup/hidePopup
 *               + 七个信号）；
 *             - Qt 6.8.3 源码 qcombobox.cpp：
 *               showPopup 无条目直接返回（:2621-2625）、
 *               setModel 只删「parent 为本控件」的旧模型（:2038-2062）、
 *               setLineEdit 先以 currentText 回填再挂信号（:1836）、
 *               insertItems 起点 qBound(0,index,count) 且保序（:2357-2370）、
 *               insertItem 越界钳位 + maxCount 尾部裁剪（:2313-2350）、
 *               setFrame 同值早退 + update（setFrame 定义处）；
 *             - 本机 Qt 6.8.0 + offscreen 探针实测：
 *               `addItems({"b","c"})` 后 currentIndex=0 且
 *               currentIndexChanged/currentTextChanged 各发 1 次；
 *               `insertItems(-1,{"x","y"})` 得顺序 x,y,b,c 且当前项由 0
 *               静默平移为 2（发信号数不变）；
 *               可编辑态 `setCurrentText("无此项")` 不改 currentIndex、
 *               只把编辑框文本改成该串；
 *               `setLineEdit(new QLineEdit)` 后编辑框文本 == currentText；
 *               `setModel(外部模型)` 后 count/currentText **取自模型**（模型
 *               行 2 ⇒ count 2、cur 0、text "x"），给模型加行 ⇒ count 跟随；
 *               删除外部模型后 count=0、currentIndex=-1；
 *               `insertSeparator(1)` 插入**空文本行**且
 *               `itemData(1, Qt::AccessibleDescriptionRole)=="separator"`、
 *               该行 flags 去掉 ItemIsEnabled/ItemIsSelectable；
 *               findText 的 MatchExactly 恒大小写敏感、
 *               Contains/StartsWith/EndsWith 缺省不敏感；
 *               addItem(icon,text,userData) 的 userData 落 Qt::UserRole；
 *               inputMethodQuery 可编辑态三项取自内嵌行编辑框、只读态
 *               ImSurroundingText 为无效变体。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

#include "XComboBox.h"
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XListView.h"
#include "XLineEdit.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XVariant.h"
#include "XInputMethod.h" /* XInputMethodQuery_*（inputMethodQuery 双参口径）。 */
#include "XString.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XEvent.h" /* XEVENT_TYPE_DEFERRED_DELETE（deleteLater 拍）。 */

/* ==================== 信号探针 ==================== */

static int cb_indexChanged = 0;   /**< currentIndexChanged 次数。 */
static int cb_textChanged = 0;    /**< currentTextChanged 次数。 */
static int cb_editTextChanged = 0;/**< editTextChanged 次数。 */
static int cb_lastIndex = -2;     /**< 最近一次 currentIndexChanged 载荷。 */

static void cb_indexChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, index);
    cb_lastIndex = index;
    ++cb_indexChanged;
}

static void cb_textChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++cb_textChanged;
}

static void cb_editTextSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++cb_editTextChanged;
}

/** @brief 复位全部信号探针。 */
static void cb_resetProbes(void)
{
    cb_indexChanged = 0;
    cb_textChanged = 0;
    cb_editTextChanged = 0;
    cb_lastIndex = -2;
}

/** @brief 把三类信号探针挂到指定组合框。
 * @note  信号槽随接收者（组合框）析构自动断开，但连接本身属于
 *        「本用例的组合框实例」——每个用例新建的 combo 必须各自挂接，
 *        否则计数器恒为初值（此前 ② 只复位不挂接 ⇒ editTextChanged
 *        恒 0 的假失败）。 */
static void cb_attachProbes(XComboBox* cb)
{
    if (!cb) return;
    XObject_connect_2((XObject*)cb,
                      (size_t)XComboBox_currentIndexChanged_signal(cb, 0),
                      cb_indexChangedSlot);
    XObject_connect_2((XObject*)cb,
                      (size_t)XComboBox_currentTextChanged_signal(cb, NULL),
                      cb_textChangedSlot);
    XObject_connect_2((XObject*)cb,
                      (size_t)XComboBox_editTextChanged_signal(cb, NULL),
                      cb_editTextSlot);
}

/* ==================== ① 缺省值 + 条目增删（含当前索引平移） ======== */

XQ_TEST(XComboBox, defaultsAndItemInsertRemove)
{
    XComboBox* cb = XComboBox_create(NULL, 0);

    XQ_REQUIRE(cb != NULL);

    /* 缺省值（qcombobox.h 属性缺省：maxVisibleItems=10、
     * maxCount=INT_MAX、duplicates=false、frame=true、
     * insertPolicy=InsertAtBottom、sizeAdjustPolicy=AdjustToContents）。 */
    XQ_EQ_I64(XComboBox_count(cb), 0);
    XQ_EQ_I64(XComboBox_currentIndex(cb), -1);
    XQ_EQ_I64(XComboBox_maxVisibleItems(cb), 10);
    XQ_EQ_I64(XComboBox_maxCount(cb), 2147483647);
    XQ_CHECK(!XComboBox_duplicatesEnabled(cb));
    XQ_CHECK(XComboBox_hasFrame(cb));
    XQ_EQ_I64(XComboBox_insertPolicy(cb),
              XComboBoxInsertPolicy_InsertAtBottom);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(cb),
              XComboBoxSizeAdjustPolicy_AdjustToContents);
    XQ_EQ_I64(XComboBox_minimumContentsLength(cb), 0);
    XQ_CHECK(!XComboBox_isEditable(cb));
    XQ_CHECK(XComboBox_lineEdit(cb) == NULL);
    XQ_EQ_STR(XComboBox_placeholderText_2(cb), "");
    XQ_EQ_I64(XComboBox_iconSize(cb), 16);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "");

    cb_resetProbes();
    cb_attachProbes(cb);

    /* 空组合框首批插入：首项自动成为当前项（rowsInserted 语义），
     * 本机 Qt 实测 index/text 信号各 1 次。 */
    XComboBox_addItems_2(cb, (const char* const[]){ "A", "B", "C", NULL });
    XQ_EQ_I64(XComboBox_count(cb), 3);
    XQ_EQ_I64(XComboBox_currentIndex(cb), 0);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "A");
    XQ_EQ_I64(cb_indexChanged, 1);
    XQ_EQ_I64(cb_textChanged, 1);

    /* 在当前项之后插入：当前项不动、不发信号。 */
    cb_resetProbes();
    XComboBox_insertItem_2(cb, 1, "B2");
    XQ_EQ_I64(XComboBox_count(cb), 4);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "B2");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 0);
    XQ_EQ_I64(cb_indexChanged, 0);

    /* 在当前项之前插入：当前索引静默右移（Qt persistent index 平移，
     * 不发 currentIndexChanged）。 */
    XComboBox_insertItem_2(cb, 0, "A0");
    XQ_EQ_I64(XComboBox_count(cb), 5);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "A0");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 1);
    XQ_EQ_I64(cb_indexChanged, 0);

    /* 越界索引两侧钳位（qBound(0,index,count)）。 */
    XComboBox_insertItem_2(cb, -7, "head");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "head");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 2);
    XComboBox_insertItem_2(cb, 999, "tail");
    XQ_EQ_STR(XComboBox_itemText_2(cb, XComboBox_count(cb) - 1), "tail");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 2);

    /* 移除当前项之前的页：当前索引左移（rowsRemoved 语义）。 */
    cb_resetProbes();
    XComboBox_removeItem(cb, 0);
    XQ_EQ_I64(XComboBox_count(cb), 6);
    XQ_EQ_I64(XComboBox_currentIndex(cb), 1);

    /* setCurrentIndex 越界 ⇒ -1（Qt 不钳到末项）；-1 与同值不重复发信号。 */
    cb_resetProbes();
    XComboBox_setCurrentIndex(cb, 999);
    XQ_EQ_I64(XComboBox_currentIndex(cb), -1);
    XQ_EQ_I64(cb_lastIndex, -1);
    XQ_EQ_I64(cb_indexChanged, 1);
    XQ_EQ_I64(cb_textChanged, 1);
    cb_resetProbes();
    XComboBox_setCurrentIndex(cb, -1);
    XComboBox_setCurrentIndex(cb, 0);
    XComboBox_setCurrentIndex(cb, 0);
    XQ_EQ_I64(cb_indexChanged, 1);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "A0");

    /* setMaxCount 尾部裁剪（Qt `model->removeRows(max, rowCount-max)`）。 */
    XComboBox_setCurrentIndex(cb, 5);
    XComboBox_setMaxCount(cb, 2);
    XQ_EQ_I64(XComboBox_count(cb), 2);
    XQ_EQ_I64(XComboBox_currentIndex(cb), 1);

    /* frame 同值早退 + 往返；maxVisibleItems 非正值忽略。 */
    XComboBox_setFrame(cb, false);
    XQ_CHECK(!XComboBox_hasFrame(cb));
    XComboBox_setFrame(cb, false);
    XQ_CHECK(!XComboBox_hasFrame(cb));
    XComboBox_setMaxVisibleItems(cb, 4);
    XQ_EQ_I64(XComboBox_maxVisibleItems(cb), 4);
    XComboBox_setMaxVisibleItems(cb, 0);
    XQ_EQ_I64(XComboBox_maxVisibleItems(cb), 4);
    XComboBox_setDuplicatesEnabled(cb, true);
    XQ_CHECK(XComboBox_duplicatesEnabled(cb));

    XClassDelete((XClass*)cb);
}

/* ==================== ② 批量插入保序 + 可编辑/编辑框回填 ============ */

XQ_TEST(XComboBox, insertItemsOrderAndEditableLineEdit)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XLineEdit* edit;

    XQ_REQUIRE(cb != NULL);
    cb_attachProbes(cb);
    XComboBox_addItems_2(cb, (const char* const[]){ "b", "c", NULL });
    XQ_EQ_I64(XComboBox_count(cb), 2);
    XQ_EQ_I64(XComboBox_currentIndex(cb), 0);

    /* 负起点批量插入：钳到 0 且**保序**（本机 Qt 实测 x,y,b,c）。 */
    XComboBox_insertItems_2(cb, -1,
                            (const char* const[]){ "x", "y", NULL });
    XQ_EQ_I64(XComboBox_count(cb), 4);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "x");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "y");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 2), "b");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 3), "c");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 2); /* 静默平移 2（Qt 实测同）。 */

    /* 正起点批量插入同样保序。 */
    cb_resetProbes();
    XComboBox_insertItems_2(cb, 1, (const char* const[]){ "p", "q", NULL });
    XQ_EQ_I64(XComboBox_count(cb), 6);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "p");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 2), "q");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 4);

    /* findText 大小写敏感（Qt 缺省 MatchExactly|MatchCaseSensitive）。 */
    XQ_EQ_I64(XComboBox_findText_2(cb, "b"), 4);
    XQ_EQ_I64(XComboBox_findText_2(cb, "B"), -1);

    /* 可编辑：Qt setEditable(true) 经 setLineEdit 以 currentText 回填。 */
    cb_resetProbes();
    XComboBox_setEditable(cb, true);
    XQ_CHECK(XComboBox_isEditable(cb));
    edit = XComboBox_lineEdit(cb);
    XQ_CHECK_PTR(edit);
    XQ_EQ_STR(XLineEdit_text(edit), XComboBox_currentText_2(cb));
    XQ_EQ_STR(XLineEdit_text(edit), "b");
    XQ_EQ_I64(cb_editTextChanged, 0); /* 回填在挂信号前，不产生编辑信号。 */

    /* 编辑文本变化 ⇒ editTextChanged + currentText 取编辑框值（分叉态）。 */
    XComboBox_setEditText_2(cb, "loose");
    XQ_EQ_I64(cb_editTextChanged, 1);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "loose");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 4); /* 分叉态不改当前索引。 */

    /* setCurrentText 命中项 ⇒ 改索引并回填编辑框（Qt 同）。 */
    XComboBox_setCurrentText_2(cb, "c");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 5);
    XQ_EQ_STR(XLineEdit_text(XComboBox_lineEdit(cb)), "c");

    /* 可编辑态 setCurrentText 未命中 ⇒ setEditText（不改索引；本机 Qt 实测）。 */
    XComboBox_setCurrentText_2(cb, "nope");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 5);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "nope");

    /* setLineEdit 安装外部编辑框：Qt 先回填 currentText 再挂信号。 */
    edit = XLineEdit_create(NULL, 0);
    XQ_REQUIRE(edit != NULL);
    XComboBox_setLineEdit(cb, edit);
    XQ_CHECK(XComboBox_lineEdit(cb) == edit);
    XQ_EQ_STR(XLineEdit_text(edit), "nope");
    XQ_CHECK(XComboBox_isEditable(cb));

    /* 旧编辑框走 deleteLater：先兑现延迟删除，再析构组合框（避免与
     * 父链级联重复回收）。 */
    XComboBox_setEditable(cb, false);
    XQ_CHECK(XComboBox_lineEdit(cb) == NULL);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    /* insertSeparator：Qt 口径为**空文本 + 不可选分隔条**（本机 Qt 探针
     * itemText==""、AccessibleDescriptionRole=="separator"、flags 去掉
     * ItemIsEnabled/ItemIsSelectable）。 */
    XComboBox_insertSeparator(cb, 0);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "");
    XQ_CHECK(XComboBox_isItemSeparator(cb, 0));
    XQ_CHECK(!XComboBox_isItemSeparator(cb, 1));

    XClassDelete((XClass*)cb);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ⑤ 分隔条 / 匹配标志 / 重载 / IM / 弹层宽 ====== */

XQ_TEST(XComboBox, separatorsMatchFlagsOverloadsImAndPopupWidth)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XVariant* var;
    XString* str;
    int sepIndex;

    XQ_REQUIRE(cb != NULL);

    /* ---- insertSeparator 索引钳制 + 标记平移（Qt 6.8 探针：-1 插最前、
     * 99 追加，与 insertItem 同口径）。 ---- */
    XComboBox_addItems_2(cb, (const char* const[]){ "Apple", "apple pie",
                                                    "Banana", "pineapple",
                                                    NULL });
    XQ_EQ_I64(XComboBox_count(cb), 4);
    XComboBox_insertSeparator(cb, 2);
    XQ_EQ_I64(XComboBox_count(cb), 5);
    XQ_CHECK(XComboBox_isItemSeparator(cb, 2));
    XQ_EQ_STR(XComboBox_itemText_2(cb, 2), "");
    XQ_CHECK(!XComboBox_isItemSeparator(cb, 1));
    XQ_CHECK(!XComboBox_isItemSeparator(cb, 3));
    XComboBox_removeItem(cb, 0); /* 分隔标记随条目左移 */
    XQ_CHECK(XComboBox_isItemSeparator(cb, 1));
    XQ_CHECK(!XComboBox_isItemSeparator(cb, 0));
    sepIndex = XComboBox_findText_2(cb, "Banana");
    XQ_EQ_I64(sepIndex, 2);
    XComboBox_clear(cb);
    XQ_EQ_I64(XComboBox_count(cb), 0);
    XComboBox_addItems_2(cb, (const char* const[]){ "Apple", "apple pie",
                                                    "Banana", "pineapple",
                                                    NULL });
    XComboBox_insertSeparator(cb, -1);
    XQ_CHECK(XComboBox_isItemSeparator(cb, 0)); /* 负数插最前 */
    XComboBox_insertSeparator(cb, 99);
    XQ_CHECK(XComboBox_isItemSeparator(cb, XComboBox_count(cb) - 1));
    XComboBox_clear(cb);

    /* ---- findText_3 / findData_3 的 Qt::MatchFlags 口径（本机 Qt 探针）。 ---- */
    XComboBox_addItems_2(cb, (const char* const[]){ "Apple", "apple pie",
                                                    "Banana", "pineapple",
                                                    NULL });
    /* MatchExactly 恒大小写敏感（QVariant 相等比较）：无 "apple" 精确项。 */
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple",
                                   (unsigned)(XComboBoxMatchExactly |
                                              XComboBoxMatchCaseSensitive)), -1);
    XQ_EQ_I64(XComboBox_findText_3(cb, "Apple", XComboBoxMatchExactly), 0);
    /* Contains 缺省不敏感 ⇒ "Apple"(0)；带 CaseSensitive ⇒ "apple pie"(1)。 */
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple", XComboBoxMatchContains), 0);
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple",
                                   (unsigned)(XComboBoxMatchContains |
                                              XComboBoxMatchCaseSensitive)), 1);
    /* StartsWith 缺省不敏感 ⇒ "Apple"(0)；敏感 ⇒ "apple pie"(1)。 */
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple", XComboBoxMatchStartsWith), 0);
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple",
                                   (unsigned)(XComboBoxMatchStartsWith |
                                              XComboBoxMatchCaseSensitive)), 1);
    /* EndsWith 缺省不敏感 ⇒ "Apple"(0)；敏感 ⇒ "pineapple"(3)。 */
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple", XComboBoxMatchEndsWith), 0);
    XQ_EQ_I64(XComboBox_findText_3(cb, "apple",
                                   (unsigned)(XComboBoxMatchEndsWith |
                                              XComboBoxMatchCaseSensitive)), 3);
    /* 正则/通配无引擎：按不支持返回 -1（不静默降级）。 */
    XQ_EQ_I64(XComboBox_findText_3(cb, "^app.*e$",
                                   XComboBoxMatchRegularExpression), -1);
    XQ_EQ_I64(XComboBox_findText_3(cb, "app*", XComboBoxMatchWildcard), -1);
    /* 缺省 findText 等价 MatchExactly|MatchCaseSensitive。 */
    XQ_EQ_I64(XComboBox_findText_2(cb, "Apple"), 0);
    XQ_EQ_I64(XComboBox_findText_2(cb, "apple"), -1);

    /* ---- addItem/insertItem(icon,text,userData) 与 (text,userData) 重载 ---- */
    XComboBox_addItem_3(cb, "icon.png", "withIcon", "ud1");
    XQ_EQ_I64(XComboBox_count(cb), 5);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 4), "withIcon");
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 4), "icon.png");
    XQ_EQ_STR(XComboBox_itemData_2(cb, 4), "ud1");
    XComboBox_insertItem_3(cb, 0, NULL, "first", "ud0");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "first");
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 0), "");   /* NULL 图标 = 空 */
    XQ_EQ_STR(XComboBox_itemData_2(cb, 0), "ud0");
    XComboBox_addItem_4(cb, "plain", "pd");
    XQ_EQ_STR(XComboBox_itemText_2(cb, XComboBox_count(cb) - 1), "plain");
    XQ_EQ_STR(XComboBox_itemData_2(cb, XComboBox_count(cb) - 1), "pd");
    XComboBox_insertItem_4(cb, 1, "middle", "md");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "middle");
    XQ_EQ_STR(XComboBox_itemData_2(cb, 1), "md");
    /* findData 标志版本：Contains 命中子串。 */
    XQ_EQ_I64(XComboBox_findData_3(cb, "d", XComboBoxMatchContains), 0);
    XQ_EQ_I64(XComboBox_findData_3(cb, "ud0",
                                   (unsigned)(XComboBoxMatchExactly |
                                              XComboBoxMatchCaseSensitive)), 0);
    XQ_EQ_I64(XComboBox_findData_3(cb, "UD0", XComboBoxMatchExactly), -1);

    /* ---- inputMethodQuery 双参重载（qcombobox.h:161；Qt 探针口径） ---- */
    {
        /* 只读：ImSurroundingText 无效（NULL），ImCursorRectangle 有值。 */
        var = XComboBox_inputMethodQuery_2(cb, XInputMethodQuery_ImSurroundingText,
                                           NULL);
        XQ_CHECK(var == NULL);
        if (var) XClassDelete((XClass*)var);
        var = XComboBox_inputMethodQuery_2(cb, XInputMethodQuery_ImCursorRectangle,
                                           NULL);
        XQ_CHECK_PTR(var);
        if (var) XQ_EQ_I64(XVariant_type(var), XVariantType_User);
        if (var) XClassDelete((XClass*)var);
    }
    XComboBox_setEditable(cb, true);
    XComboBox_setEditText_2(cb, "HelloWorld");
    XLineEdit_setCursorPosition(XComboBox_lineEdit(cb), 5);
    var = XComboBox_inputMethodQuery_2(cb, XInputMethodQuery_ImSurroundingText,
                                       NULL);
    XQ_CHECK_PTR(var);
    if (var) {
        str = XString_fromVariant(var);
        XQ_CHECK_PTR(str);
        if (str) XQ_EQ_STR(XString_toUtf8(str), "HelloWorld");
        if (str) XClassDelete((XClass*)str);
        XClassDelete((XClass*)var);
    }
    var = XComboBox_inputMethodQuery_2(cb, XInputMethodQuery_ImCursorPosition,
                                       NULL);
    XQ_CHECK_PTR(var);
    if (var) XQ_EQ_I64(XVariant_toInt32(var), 5);
    if (var) XClassDelete((XClass*)var);
    /* ImCursorRectangle 可编辑态取行编辑光标矩形（非 NULL，User 承载）。 */
    var = XComboBox_inputMethodQuery_2(cb, XInputMethodQuery_ImCursorRectangle,
                                       NULL);
    XQ_CHECK_PTR(var);
    if (var) XQ_EQ_I64(XVariant_type(var), XVariantType_User);
    if (var) XClassDelete((XClass*)var);
    XComboBox_setEditable(cb, false);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    /* ---- 弹层宽度虚槽：基类=控件宽（Qt 探针：普通组合框弹层==控件宽） ---- */
    XWidget_setGeometry((XWidget*)cb, 0, 0, 137, 24);
    XQ_EQ_I64(XComboBox_popupWidth(cb), 137);
    XQ_EQ_I64(XComboBox_popupWidth_base(cb), 137);

    XClassDelete((XClass*)cb);
}

/* ==================== ③ model 是唯一数据源（Qt 6.8 口径） ========== */

XQ_TEST(XComboBox, externalModelIsSingleSource)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XAbstractItemModel* ext;
    XListView* view;

    XQ_REQUIRE(cb != NULL);
    XComboBox_addItems_2(cb, (const char* const[]){ "a", "b", NULL });

    ext = XAbstractItemModel_create();
    XQ_REQUIRE(ext != NULL);
    XAbstractItemModel_setDimension(ext, 2, 1);
    XAbstractItemModel_setData_2(ext, 0, 0, "m1");
    XAbstractItemModel_setData_2(ext, 1, 0, "m2");

    /* setModel 为借用（Qt：组合框不取得所有权），且模型即唯一数据源：
     * count/itemText/currentText 全部跟随模型（Qt 探针：模型 2 行 ⇒
     * count 2、cur 0、text "x"）。 */
    XComboBox_setModel(cb, ext);
    XQ_CHECK(XComboBox_model(cb) == ext);
    XQ_EQ_I64(XComboBox_count(cb), 2);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "m1");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "m2");
    XQ_EQ_I64(XComboBox_currentIndex(cb), 0);
    XQ_EQ_STR(XComboBox_currentText_2(cb), "m1");

    /* view() 懒创建弹层视图并把外部模型交给它，且不覆盖模型内容。 */
    view = XComboBox_view(cb);
    XQ_CHECK_PTR(view);
    XQ_CHECK(XAbstractItemView_model((XAbstractItemView*)view) == ext);
    XQ_EQ_I64(XAbstractItemModel_rowCount(ext), 2);
    XQ_EQ_STR(XAbstractItemModel_data_2(ext, 0, 0), "m1");

    /* 模型结构/数据变化 ⇒ 组合框跟随（rowsInserted/dataChanged）。 */
    XQ_CHECK(XAbstractItemModel_insertRow(ext, 2));
    XAbstractItemModel_setData_2(ext, 2, 0, "m3");
    XQ_EQ_I64(XComboBox_count(cb), 3);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 2), "m3");
    XAbstractItemModel_setData_2(ext, 0, 0, "m1x");
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "m1x");

    /* 条目写入回写模型（Qt：QComboBox::insertItem 就是往 d->model 插行）。 */
    XComboBox_addItem_2(cb, "added");
    XQ_EQ_I64(XAbstractItemModel_rowCount(ext), 4);
    XQ_EQ_STR(XAbstractItemModel_data_2(ext, 3, 0), "added");
    XQ_EQ_I64(XComboBox_count(cb), 4);
    XComboBox_removeItem(cb, 1);
    XQ_EQ_I64(XAbstractItemModel_rowCount(ext), 3);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 1), "m3");
    XComboBox_setItemText_2(cb, 0, "renamed");
    XQ_EQ_STR(XAbstractItemModel_data_2(ext, 0, 0), "renamed");

    /* setModel(NULL) 为空操作（Qt 对 0 模型告警返回）。 */
    XComboBox_setModel(cb, NULL);
    XQ_CHECK(XComboBox_model(cb) == ext);

    /* 自建模型（未 setModel 时懒创建）由组合框拥有，析构时释放。 */
    {
        XComboBox* own = XComboBox_create(NULL, 0);
        XAbstractItemModel* inner;
        XQ_REQUIRE(own != NULL);
        XComboBox_addItem_2(own, "solo");
        inner = XComboBox_model(own);
        XQ_CHECK_PTR(inner);
        XQ_EQ_I64(XAbstractItemModel_rowCount(inner), 1);
        XQ_EQ_STR(XAbstractItemModel_data_2(inner, 0, 0), "solo");
        XClassDelete((XClass*)own); /* 内部模型随组合框释放，泄漏门校验。 */
    }

    /* 外部模型销毁 ⇒ 组合框失联清空（Qt 探针：count 0、cur -1），
     * 且组合框不释放借用模型（此处由用例释放）。 */
    XClassDelete((XClass*)ext);
    XQ_EQ_I64(XComboBox_count(cb), 0);
    XQ_EQ_I64(XComboBox_currentIndex(cb), -1);
    /* Qt 的 model() 恒有模型（外部模型被删后组合框回到缺省空模型口径）。 */
    XQ_CHECK(XComboBox_model(cb) != NULL);
    XQ_CHECK(XComboBox_itemText_2(cb, 0) != NULL);
    XQ_EQ_STR(XComboBox_itemText_2(cb, 0), "");

    XClassDelete((XClass*)cb);
}

/* ==================== ④ 弹出状态机（showPopup/hidePopup） ========== */

XQ_TEST(XComboBox, showPopupEmptyNoop)
{
    XComboBox* cb = XComboBox_create(NULL, 0);

    XQ_REQUIRE(cb != NULL);
    XQ_CHECK(!XComboBox_popupVisible(cb));

    /* 空组合框不建弹层（Qt qcombobox.cpp:2621 `if (count() <= 0) return;`）。 */
    XComboBox_showPopup(cb);
    XQ_CHECK(!XComboBox_popupVisible(cb));

    /* 未弹出时 hidePopup 为空操作（幂等）。 */
    XComboBox_hidePopup(cb);
    XQ_CHECK(!XComboBox_popupVisible(cb));

    /* 有条目时仍应能创建弹层视图对象（不显示窗口，仅对象契约）。 */
    XComboBox_addItem_2(cb, "only");
    XQ_CHECK_PTR(XComboBox_view(cb));
    XComboBox_hidePopup(cb);
    XQ_CHECK(!XComboBox_popupVisible(cb));

    XClassDelete((XClass*)cb);
}

#endif /* XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */
