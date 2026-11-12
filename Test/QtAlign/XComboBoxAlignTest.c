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
#include "XCompleter.h"    /* P0-54：completer 转发行编辑的联动接线。 */
#include "XItemDelegate.h" /* P3-57：委托转发视图（真委托对象）。 */
#include "XIcon.h"         /* P3-56：图标可解码性探针。 */
#include "XImage.h"        /* P3-56：装饰图像尺寸断言 + 造 PNG 夹具。 */
#include "XPixmapCache.h"  /* P3-56：解码落全局像素图缓存，收尾清理（防泄漏门误报）。 */
#include "XPainter.h"      /* P0-89：'x' 基线宽复算（XPainter_textWidth）。 */
#include "XFont.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XVariant.h"
#include "XInputMethod.h" /* XInputMethodQuery_*（inputMethodQuery 双参口径）。 */
#include "XString.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XEvent.h" /* XEVENT_TYPE_DEFERRED_DELETE（deleteLater 拍）。 */

#include <stdio.h>  /* remove（P3-56 图标夹具清理）。 */

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
     * insertPolicy=InsertAtBottom、sizeAdjustPolicy=AdjustToContentsOnFirstShow
     * ——qcombobox_p.h:396 `sizeAdjustPolicy = QComboBox::
     * AdjustToContentsOnFirstShow`；P0-04 前本库误取 AdjustToContents）。 */
    XQ_EQ_I64(XComboBox_count(cb), 0);
    XQ_EQ_I64(XComboBox_currentIndex(cb), -1);
    XQ_EQ_I64(XComboBox_maxVisibleItems(cb), 10);
    XQ_EQ_I64(XComboBox_maxCount(cb), 2147483647);
    XQ_CHECK(!XComboBox_duplicatesEnabled(cb));
    XQ_CHECK(XComboBox_hasFrame(cb));
    XQ_EQ_I64(XComboBox_insertPolicy(cb),
              XComboBoxInsertPolicy_InsertAtBottom);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(cb),
              XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow);
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

/* ==================== ⑥ sizeAdjustPolicy 缓存/首显冻结（P0-04） ====== */
/* Qt 6.8 依据：
 *  - 缺省档 AdjustToContentsOnFirstShow（qcombobox_p.h:396）；
 *  - sizeHint 缓存（qcombobox.cpp:2508-2521 `recomputeSizeHint(sh)` 的
 *    `if (!sh.isValid())`）；
 *  - 首显失效一次并置 shownOnce（qcombobox.cpp:3090-3099）；
 *  - 内容变化只在 AdjustToContents 档失效（rowsInserted :1110-1125、
 *    rowsRemoved :1150-1161、dataChanged :1084-1098、modelChanged
 *    :3447-3455）；
 *  - setSizeAdjustPolicy 切换即失效重算（:1617-1625）、
 *    setMinimumContentsLength 只在前两档失效（:1650-1661）、
 *    setIconSize 无条件失效（:1685-1691）。 */
XQ_TEST(XComboBox, sizeAdjustPolicyCacheAndFirstShow)
{
    XComboBox* frozen = XComboBox_create(NULL, 0);
    XComboBox* driven = XComboBox_create(NULL, 0);
    XSize h1;
    XSize hShown;
    XSize hFrozen;
    XSize hGrown;

    XQ_REQUIRE(frozen != NULL);
    XQ_REQUIRE(driven != NULL);

    /* ---- 缺省档（P0-04）：AdjustToContentsOnFirstShow ---- */
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(frozen),
              XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow);
    /* 非法档忽略（Qt 无该档；越界值不落位）。 */
    XComboBox_setSizeAdjustPolicy(frozen, 3);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(frozen),
              XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow);

    /* ---- 首显后冻结：内容变化不再变宽（缺省档） ---- */
    XComboBox_addItem_2(frozen, "AA");
    h1 = XWidget_sizeHint_base((XWidget*)frozen);
    XQ_CHECK(h1.width > 0);
    /* 首次显示：showEvent 失效一次并重算，随后 shownOnce 置位。 */
    XWidget_show((XWidget*)frozen);
    hShown = XWidget_sizeHint_base((XWidget*)frozen);
    XQ_EQ_I64(hShown.width, h1.width);
    /* 首显后追加超长项：缺省档不再失效 ⇒ 首选宽冻结（Qt 同）。 */
    XComboBox_addItem_2(frozen,
                        "AVeryLongItemTextThatWouldGrowTheComboWidthIf"
                        "ThePolicyWereContentDriven0123456789");
    hFrozen = XWidget_sizeHint_base((XWidget*)frozen);
    XQ_EQ_I64(hFrozen.width, hShown.width);

    /* ---- 切到 AdjustToContents：立即失效重算并随内容变宽 ---- */
    XComboBox_setSizeAdjustPolicy(frozen,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(frozen),
              XComboBoxSizeAdjustPolicy_AdjustToContents);
    /* 切换即重算（qcombobox.cpp:1617-1625）⇒ 长项已计入。 */
    hGrown = XWidget_sizeHint_base((XWidget*)frozen);
    XQ_CHECK(hGrown.width > hFrozen.width);
    /* 内容驱动档：再追加更长项继续变宽。 */
    XComboBox_addItem_2(frozen,
                        "EvenLongerItemTextForContentDrivenPolicyCheck"
                        "0123456789012345678901234567890123456789");
    {
        XSize hMore = XWidget_sizeHint_base((XWidget*)frozen);
        XQ_CHECK(hMore.width > hGrown.width);
    }

    /* ---- 内容驱动档同样由 removeItem/clear 失效 ---- */
    {
        int before = XWidget_sizeHint_base((XWidget*)frozen).width;
        XComboBox_clear(frozen);
        XComboBox_addItem_2(frozen, "z");
        XQ_CHECK(XWidget_sizeHint_base((XWidget*)frozen).width < before);
    }

    /* ---- 内容驱动档对照（driven）：未显示也随内容变宽 ---- */
    XComboBox_setSizeAdjustPolicy(driven,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_addItem_2(driven, "w");
    {
        XSize w1 = XWidget_sizeHint_base((XWidget*)driven);
        XComboBox_addItem_2(driven,
                            "AReallyLongItemNameToForceWidthGrowth0123456789");
        XQ_CHECK(XWidget_sizeHint_base((XWidget*)driven).width > w1.width);
    }
    /* 显示一次不改变内容驱动档的后续行为（冻结只属首显档）。 */
    XWidget_show((XWidget*)driven);
    {
        int wShown = XWidget_sizeHint_base((XWidget*)driven).width;
        XComboBox_addItem_2(driven,
                            "YetAnotherLongItemNameForPostShowGrowth"
                            "012345678901234567890123456789");
        XQ_CHECK(XWidget_sizeHint_base((XWidget*)driven).width > wShown);
    }

    XClassDelete((XClass*)frozen);
    XClassDelete((XClass*)driven);
}

/* ==================== ⑦ sizeHint 空列表基线/占位/图标/frame（P0-89） == */
/* Qt 6.8 依据（recomputeSizeHint，qcombobox.cpp:347-390）：
 *  - 空列表宽 = 7×fm.horizontalAdvance('x')（:361-364）；
 *  - 带图标项宽 += iconSize.width()+4，最小高 iconSize.height()+2
 *    （:357-364 / :387-390）；
 *  - placeholder 计入首选宽（:380-382）；
 *  - frame 参与 CT_ComboBox 内容尺寸（initStyleOption `option->frame =
 *    d->frame`，:1240；本库 XCommonStyle 的 XStyleCT_ComboBox 分支读
 *    m_spinFrame 定 frameWidth）。 */
XQ_TEST(XComboBox, sizeHintEmptyBaselinePlaceholderAndFrame)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XFont font;
    int xw;
    XSize hEmpty;
    XSize hPlaceholder;
    XSize hFrameOn;
    XSize hFrameOff;

    XQ_REQUIRE(cb != NULL);
    font = XWidget_font((const XWidget*)cb);
    xw = XPainter_textWidth(&font, "x");
    XClassDeinit((XClass*)&font);
    XQ_CHECK(xw > 0);

    /* ---- 空列表基线宽 > 7×'x'（P0-89①；改前 maxW=0 ⇒ 只剩按钮/边距） ---- */
    hEmpty = XWidget_sizeHint_base((XWidget*)cb);
    XQ_CHECK(hEmpty.width > 7 * xw);
    XQ_CHECK(hEmpty.height > 0);

    /* ---- placeholder 计入首选宽（P0-89②；闭态确会画占位文本） ---- */
    XComboBox_setPlaceholderText_2(
        cb, "A rather long placeholder text to dominate the width 0123456789");
    hPlaceholder = XWidget_sizeHint_base((XWidget*)cb);
    XQ_CHECK(hPlaceholder.width > hEmpty.width);
    XComboBox_setPlaceholderText_2(cb, "");

    /* ---- frame 参与尺寸（P3-55 承载：m_frame → opt.m_spinFrame） ----
     * 置 AdjustToContents 使内容变化触发重算（setFrame 本身只 update，
     * Qt 同：qcombobox.cpp setFrame 只 update）。 */
    XComboBox_setSizeAdjustPolicy(cb,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_addItem_2(cb, "FFFFFF");
    hFrameOn = XWidget_sizeHint_base((XWidget*)cb);
    XComboBox_setFrame(cb, false);
    XQ_CHECK(!XComboBox_hasFrame(cb));
    XComboBox_addItem_2(cb, ""); /* 空项不改宽，只触发重算 */
    hFrameOff = XWidget_sizeHint_base((XWidget*)cb);
    XQ_CHECK(hFrameOff.width < hFrameOn.width);
    XQ_CHECK(hFrameOff.height < hFrameOn.height);
    XComboBox_setFrame(cb, true);

    XClassDelete((XClass*)cb);
}

/* ==================== ⑧ 项图标：尺寸计入 + 弹层装饰（P3-56） ========= */
/* Qt 6.8 依据：CE_ComboBoxLabel 图标占 iconSize.width()+4、文本右移
 * （qcommonstyle.cpp:2227-2245）；recomputeSizeHint 带图标项 +iconSize+4
 * 与最小高 iconSize+2（qcombobox.cpp:357-364/387-390）；弹层走
 * DecorationRole（setItemIcon → model->setData(item, icon,
 * Qt::DecorationRole)，qcombobox.cpp:2446-2453），本库以弹层视图
 * DecorationRole 装饰承载（XListView.c:163-180 绘制）。
 * 环境口径：图标文件解码依赖图像插件；本用例先探针解码，可解码才断言
 * 装饰非空（同 XFontComboBoxAlignTest 的环境无关断言先例），尺寸计入
 * 只依赖路径非空、恒断言。 */
XQ_TEST(XComboBox, itemIconSizeAndDecoration)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XImage image;
    XIcon probe;
    XListView* view;
    int wNoIcon;
    int wWithIcon;
    bool decodable = false;
    const char* iconPath = "/tmp/xcombo_align_icon.png";

    XQ_REQUIRE(cb != NULL);
    XComboBox_setSizeAdjustPolicy(cb,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_addItem_2(cb, "Q");
    wNoIcon = XWidget_sizeHint_base((XWidget*)cb).width;

    /* 造一个可解码的 PNG 图标（8×8 纯色）。 */
    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XImage_fill(&image, 0xFF3366CCu);
    XImage_save_2(&image, iconPath, "png", 100);
    XClassDeinit((XClass*)&image);
    XIcon_init_file_2(&probe, iconPath);
    decodable = !XIcon_isNull(&probe);
    XClassDeinit((XClass*)&probe);

    /* ---- 尺寸计入：带图标项宽 += iconSize+4（缺省 16 ⇒ +20） ---- */
    XComboBox_setIconSize(cb, 16);
    XQ_EQ_I64(XComboBox_iconSize(cb), 16);
    XComboBox_setItemIcon_2(cb, 0, iconPath);
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 0), iconPath);
    wWithIcon = XWidget_sizeHint_base((XWidget*)cb).width;
    XQ_EQ_I64(wWithIcon, wNoIcon + XComboBox_iconSize(cb) + 4);

    /* 图标尺寸变化 ⇒ sizeHint 无条件失效重算（qcombobox.cpp:1685-1691）。 */
    XComboBox_setIconSize(cb, 24);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)cb).width,
              wNoIcon + 24 + 4);
    XComboBox_setIconSize(cb, 16);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)cb).width, wWithIcon);

    /* ---- 弹层装饰：DecorationRole 逐行重放（解码成功时非空） ---- */
    view = XComboBox_view(cb);
    XQ_CHECK_PTR(view);
    if (decodable) {
        const void* deco = XAbstractItemView_itemDecoration(
            (XAbstractItemView*)view, 0, 0);
        XQ_CHECK_PTR(deco);
        /* 解码图按 iconSize 缩放（不放大过源图：源 8×8、请求 16×16 时
         * 实现可保持源尺寸），只断言非空且不超过 iconSize。 */
        if (deco) {
            XQ_CHECK(XImage_width((const XImage*)deco) > 0);
            XQ_CHECK(XImage_width((const XImage*)deco)
                     <= XComboBox_iconSize(cb));
        }
    }

    /* 清图标 ⇒ 尺寸回落且装饰清除（装饰表不得留旧借用指针）。 */
    XComboBox_setItemIcon_2(cb, 0, NULL);
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 0), "");
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)cb).width, wNoIcon);
    XQ_CHECK(XAbstractItemView_itemDecoration(
                 (XAbstractItemView*)view, 0, 0) == NULL);

    /* 图标随条目插入/移除平移（平行数组同步搬运）。 */
    XComboBox_addItem_2(cb, "R");
    XComboBox_setItemIcon_2(cb, 0, iconPath);
    XComboBox_insertItem_2(cb, 0, "S");
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 0), "");
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 1), iconPath);
    XComboBox_removeItem(cb, 0);
    XQ_EQ_STR(XComboBox_itemIcon_2(cb, 0), iconPath);

    XClassDelete((XClass*)cb);
    remove(iconPath);
#if XPIXMAPCACHE_ON
    /* 全局像素图缓存收尾清理：XIcon_pixmap 的缩放结果落进程级
     * XPixmapCache（qt_icon_scale/ 前缀，键含**源像素图 per-instance
     * serial**，见 XIcon.c XIconPrivate_scaledPixmap 的 sourceKey 构造）
     * ⇒ 本用例每遍的每次图标解码都新增缓存条目，缓存按 LRU 只在超限
     * 时逐出、正常量级永不触发 ⇒ 用例前后堆计数每遍净增长，泄漏门
     * （容忍 0、复核遍仍增长即判 [LEAK]）必然误报。这是 Qt 同款的
     * 纯性能缓存语义（QPixmapCache 同机制），不是所有权泄漏——用例
     * 收尾清缓存归还全部条目，断言口径不变。 */
    XPixmapCache_clear();
#endif
}

/* ==================== ⑨ completer 转发行编辑（P0-54） ================ */
/* Qt 6.8 依据：setCompleter 转发 d->lineEdit->setCompleter(c)、非可编辑
 * 直接忽略、连接 completerActivated、末尾 c->setWidget(this)
 * （qcombobox.cpp:1950-1963）；completer() 读 d->lineEdit->completer()
 * （:1973-1977）；completerActivated 置当前项并发 activated（:1246-1273）。
 * 补全器经 XObject_emitSignal 驱动 activated 信号，验证联动接线（与
 * QCompleter 弹层点击路径同一信号源）。 */
XQ_TEST(XComboBox, completerForwardedToLineEdit)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XCompleter* comp;
    XCompleter* comp2;
    XLineEdit* edit;
    XString* payload;
    XVarList* args;

    XQ_REQUIRE(cb != NULL);
    /* 本用例的 combo 必须各自挂接探针（连接表挂在**发送者**一侧，② 的
     * combo 析构后旧连接即消亡；只 reset 不挂接 ⇒ cb_lastIndex 恒为
     * 哨兵 -2 的假失败——与 cb_attachProbes 头注释点名的 ② 同款坑）。 */
    cb_attachProbes(cb);
    XComboBox_addItems_2(cb, (const char* const[]){ "alpha", "beta",
                                                    "gamma", NULL });
#if XTABLEWIDGET_ON
    /* 非可编辑：Qt 直接忽略（lineEdit 缺席），不建行编辑、不存指针。 */
    comp = XCompleter_create(NULL);
    XQ_REQUIRE(comp != NULL);
    XComboBox_setCompleter(cb, comp);
    XQ_CHECK(XComboBox_completer(cb) == NULL);
    XQ_CHECK(XComboBox_lineEdit(cb) == NULL);

    /* 转可编辑后再装：转发到内嵌行编辑（单一事实源）。 */
    XComboBox_setEditable(cb, true);
    edit = XComboBox_lineEdit(cb);
    XQ_CHECK_PTR(edit);
    XComboBox_setCompleter(cb, comp);
    XQ_CHECK(XComboBox_completer(cb) == comp);
    XQ_CHECK(XLineEdit_completer(edit) == comp);
    /* Qt 末尾 c->setWidget(this)：补全器锚到组合框。 */
    XQ_CHECK(XCompleter_widget(comp) == (XWidget*)cb);
    /* 补全器无模型 ⇒ 安装即接通组合框条目模型（Qt 同）。 */
    XQ_CHECK(XCompleter_model(comp) == XComboBox_model(cb));

    /* ---- activated 联动：置当前项 + 发射 activated 对 ---- */
    cb_resetProbes();
    payload = XString_create_utf8("gamma");
    XQ_REQUIRE(payload != NULL);
    args = XVarList_Create(XVar(XString*, payload));
    if (args) {
        XObject_emitSignal((XObject*)comp,
                           (size_t)XCompleter_activated_signal(comp, NULL),
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    XQ_EQ_I64(XComboBox_currentIndex(cb), 2);
    XQ_EQ_I64(cb_lastIndex, 2);
    /* XLineEdit 回填（Qt completerActivated 的 setCurrentIndex 同路径）。 */
    XQ_EQ_STR(XLineEdit_text(edit), "gamma");
    XClassDelete((XClass*)payload);

    /* ---- 换装补全器：旧联动断开（不叠加），新补全器接管 ---- */
    comp2 = XCompleter_create(NULL);
    XQ_REQUIRE(comp2 != NULL);
    XQ_CHECK(XCompleter_widget(comp) == (XWidget*)cb);
    XComboBox_setCompleter(cb, comp2);
    XQ_CHECK(XComboBox_completer(cb) == comp2);
    XQ_CHECK(XLineEdit_completer(edit) == comp2);
    /* 旧补全器已清关联位（借用卫生），其 activated 不再影响组合框。 */
    XQ_CHECK(XCompleter_widget(comp) == NULL);
    cb_resetProbes();
    payload = XString_create_utf8("alpha");
    if (payload) {
        args = XVarList_Create(XVar(XString*, payload));
        if (args) {
            XObject_emitSignal(
                (XObject*)comp,
                (size_t)XCompleter_activated_signal(comp, NULL),
                args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
        XClassDelete((XClass*)payload);
    }
    XQ_EQ_I64(XComboBox_currentIndex(cb), 2); /* 仍为 gamma，未被旧补全器改。 */

    /* 新补全器生效一次（恰一次：接线不重复叠加）。 */
    payload = XString_create_utf8("beta");
    if (payload) {
        args = XVarList_Create(XVar(XString*, payload));
        if (args) {
            XObject_emitSignal(
                (XObject*)comp2,
                (size_t)XCompleter_activated_signal(comp2, NULL),
                args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
        XClassDelete((XClass*)payload);
    }
    XQ_EQ_I64(XComboBox_currentIndex(cb), 1);

    /* ---- 解除：completer()/行编辑双空；completerMode 开关保留 ---- */
    XComboBox_setCompleter(cb, NULL);
    XQ_CHECK(XComboBox_completer(cb) == NULL);
    XQ_CHECK(XLineEdit_completer(edit) == NULL);
    XQ_CHECK(XCompleter_widget(comp2) == NULL);

    XClassDelete((XClass*)comp);
    XClassDelete((XClass*)comp2);
#endif /* XTABLEWIDGET_ON */
    XClassDelete((XClass*)cb);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ⑩ 条目委托转发弹层视图（P3-57） =============== */
/* Qt 6.8 依据：setItemDelegate 转发 view()->setItemDelegate(delegate)、
 * NULL 拒绝（qcombobox.cpp:2014-2019）；itemDelegate() 读
 * view()->itemDelegate()（:2006-2012）。 */
XQ_TEST(XComboBox, itemDelegateForwardedToView)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XItemDelegate* delegate;
    XListView* view;

    XQ_REQUIRE(cb != NULL);
    view = XComboBox_view(cb);
    XQ_CHECK_PTR(view);
    XQ_CHECK(XComboBox_itemDelegate(cb) == NULL);

    /* NULL 不转发、不清除（Qt「cannot set a 0 delegate」静默等价）。 */
    XComboBox_setItemDelegate(cb, NULL);
    XQ_CHECK(XComboBox_itemDelegate(cb) == NULL);

    /* 真委托：组合框与视图读同一指针（转发，非本地存储）。 */
    delegate = XItemDelegate_create();
    XQ_REQUIRE(delegate != NULL);
    XComboBox_setItemDelegate(cb, delegate);
    XQ_CHECK(XComboBox_itemDelegate(cb) == (void*)delegate);
    XQ_CHECK(XAbstractItemView_itemDelegate((XAbstractItemView*)view)
             == (void*)delegate);

    /* 视图级清除后组合框查询同空（单一事实源在视图）。 */
    XAbstractItemView_setItemDelegate((XAbstractItemView*)view, NULL);
    XQ_CHECK(XComboBox_itemDelegate(cb) == NULL);

    XClassDelete((XClass*)delegate);
    XClassDelete((XClass*)cb);
}

#endif /* XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */
