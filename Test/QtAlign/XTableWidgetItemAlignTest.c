/**
 * @file       XTableWidgetItemAlignTest.c
 * @brief      XTableWidgetItem × Qt 6.8 QTableWidgetItem 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *               qtablewidget.h（ItemType Type=0/UserType=1000、
 *               clone、row/column/tableWidget、setSelected/isSelected、
 *               flags/setFlags、text/setText、icon、statusTip/toolTip/
 *               whatsThis、font、textAlignment、background/foreground、
 *               checkState、sizeHint、data/setData、operator<、
 *               read/write、拷贝构造与 operator=）；
 *             - Qt 6.8 源码 src/widgets/itemviews/qtablewidget.cpp 原文：
 *               * 构造缺省 flags = ItemIsEditable|Selectable|UserCheckable|
 *                 Enabled|DragEnabled|DropEnabled（:1327-1334）；
 *               * setData 先做 EditRole→DisplayRole 归一化，且**同值早退**
 *                 （`if (values[i].value == value) return;`，:1399-1412）；
 *               * setFlags 无条件写入并 `model->itemChanged(this)`
 *                 （:1129-1135，同值也通知）；
 *               * operator< 走 `variantLessThan`（:1441-1444 →
 *                 qabstractitemmodel.cpp:828-839）：整型/浮点/其他三档；
 *               * 拷贝构造 `rtti(Type), view(nullptr), itemFlags(...)`
 *                 ——type/tableWidget/选中态都不拷（:1521-1533）；
 *               * setSizeHint 只在 `QSize::isValid()`（w>=0 && h>=0）时
 *                 存值，(0,0) 有效（qtablewidget.h:108-111）。
 *             - 本机 Qt 6.8.0 offscreen 探针实测：
 *               ① 自定义 type=1001 的条目：拷贝构造与 clone() 的
 *                  type()==0；operator= 保留左值 type；
 *               ② setText 同值 itemChanged=0、异值=1、再同值仍=1；
 *                  setFlags 同值 itemChanged=1；
 *               ③ sizeHint(0,0) 的 SizeHintRole 有效，(-1,5) 无效；
 *               ④ int 3 < int 5、int 5 < "a"、缺失值条目 < "a"。
 *
 *             **已对齐（2026-11-11）**：
 *             ② text()/text_toUtf8 对 int/double/bool 型 DisplayRole 按
 *                QVariant::toString 转文本（gap 295，本机 Qt 6.8.0 实测
 *                42→"42"、3.5→"3.5"、true→"true"）；
 *             ③ setSelected 对**游离条目**是空操作（gap 296：Qt 的
 *                `if (d->view)` 分支）；表内选中态仍以选中位承载并随
 *                XTableWidget 的选择路径回写；
 *             ④ setCheckState 原样存 enum（gap 297，Qt 实测 7/‑3 均原样）。
 *
 *             **保留偏差**（见交付 gaps）：
 *             ① read/write 线格式额外写 type+flags（Qt 6.8 只写 role
 *                表），既有回归 xgui_regression_test.c:34530-34556 锁定
 *                flags 往返，本轮保留（本库自有的明确序列化格式）。
 * @note       C99；条目/表格/流对象全部释放，漏删由运行器 `[LEAK]`
 *             判失败。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include <string.h>

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTableWidget.h"
#include "XAbstractItemView.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XVariant.h"
#include "XString.h"
#include "XContainer.h"
#include "XColor.h"
#include "XFont.h"
#include "XGeometry.h"
#include "XByteArray.h"
#include "XDataStream.h"
#include "XVector.h"

/* ==================== 信号探针 ==================== */

static int g_tiItemChanged;

static void xtiItemChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_tiItemChanged;
}

/* ==================== 值构造助手 ==================== */

static void xtiSetInt(XTableWidgetItem* item, int role, int value)
{
    XVariant* v = XVariant_create_int(value);
    if (!v) return;
    XTableWidgetItem_setData(item, role, v);
    XClassDelete((XClass*)v);
}

static void xtiSetDouble(XTableWidgetItem* item, int role, double value)
{
    XVariant* v = XVariant_create_double(value);
    if (!v) return;
    XTableWidgetItem_setData(item, role, v);
    XClassDelete((XClass*)v);
}

/** @brief 建一个 1x1、唯一格挂 item 的表格（item 归表所有）。 */
static XTableWidget* xtiMakeTable(XTableWidgetItem* item)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    if (!tw) return NULL;
    XTableWidget_setRowCount(tw, 1);
    XTableWidget_setColumnCount(tw, 1);
    if (item) XTableWidget_setItem(tw, 0, 0, item);
    return tw;
}

/* ==================== ① 缺省值 ==================== */

XQ_TEST(XTableWidgetItem, defaults)
{
    XTableWidgetItem* item = XTableWidgetItem_create();
    XColor color;
    XSize size;
    XQ_REQUIRE(item != NULL);

    XQ_EQ_I64(XTableWidgetItem_type(item), XTableWidgetItem_Type);
    /* Qt 构造缺省 flags：可编辑/可选/可勾选/启用/可拖/可放（0x3f）。 */
    XQ_EQ_I64(XTableWidgetItem_flags(item),
              XItemFlag_ItemIsEditable | XItemFlag_ItemIsSelectable |
              XItemFlag_ItemIsUserCheckable | XItemFlag_ItemIsEnabled |
              XItemFlag_ItemIsDragEnabled | XItemFlag_ItemIsDropEnabled);
    XQ_CHECK(!XTableWidgetItem_isSelected(item));
    XQ_CHECK(XTableWidgetItem_tableWidget(item) == NULL);
    XQ_EQ_I64(XTableWidgetItem_row(item), -1);
    XQ_EQ_I64(XTableWidgetItem_column(item), -1);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(item), "");
    XQ_CHECK(XTableWidgetItem_text_const(item) == NULL);
    XQ_EQ_I64(XTableWidgetItem_checkState(item), XItemCheckState_Unchecked);
    XQ_EQ_I64(XTableWidgetItem_textAlignment(item), 0);
    XQ_CHECK(!XTableWidgetItem_background(item, &color));
    XQ_CHECK(!XTableWidgetItem_foreground(item, &color));
    XQ_CHECK(!XTableWidgetItem_sizeHint(item, &size));
    XQ_CHECK(XTableWidgetItem_icon(item) == NULL);
    XQ_CHECK(XTableWidgetItem_font(item) == NULL);
    XQ_EQ_STR(XTableWidgetItem_statusTip_toUtf8(item), "");
    XQ_EQ_STR(XTableWidgetItem_toolTip_toUtf8(item), "");
    XQ_EQ_STR(XTableWidgetItem_whatsThis_toUtf8(item), "");

    XClassDelete((XClass*)item);
}

/* ==================== ② role 数据：文本与 EditRole 归一化 ============ */

XQ_TEST(XTableWidgetItem, dataRoles)
{
    XTableWidgetItem* item = XTableWidgetItem_create_utf8("甲", 1001);
    XString* text;
    const XVariant* v;
    XQ_REQUIRE(item != NULL);

    /* 构造入参 type 生效（ItemType 可为自定义值）。 */
    XQ_EQ_I64(XTableWidgetItem_type(item), 1001);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(item), "甲");
    /* text() 返回新建副本，调用方释放。 */
    text = XTableWidgetItem_text(item);
    XQ_CHECK_PTR(text);
    XQ_CHECK(text && strcmp(XString_toUtf8(text), "甲") == 0);
    if (text) XClassDelete((XClass*)text);

    /* EditRole 与 DisplayRole 共槽（Qt 归一化，qtablewidget.cpp:1399）。 */
    xtiSetInt(item, XItemDataRole_EditRole, 42);
    v = XTableWidgetItem_data(item, XItemDataRole_DisplayRole);
    XQ_CHECK_PTR(v);
    XQ_EQ_I64(v ? XVariant_toInt(v) : -1, 42);
    XQ_CHECK(XTableWidgetItem_data(item, XItemDataRole_EditRole) == v);

    /* setData(NULL) 移除该 role；文本回到空串。 */
    XTableWidgetItem_setData(item, XItemDataRole_DisplayRole, NULL);
    XQ_CHECK(XTableWidgetItem_data(item, XItemDataRole_DisplayRole) == NULL);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(item), "");

    /* setText 写入 DisplayRole。 */
    XTableWidgetItem_setText_utf8(item, "乙");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(item), "乙");

    XClassDelete((XClass*)item);
}

/* ==================== ③ 便捷 role：tip/font/color/align/check/size ==== */

XQ_TEST(XTableWidgetItem, convenienceRoles)
{
    XTableWidgetItem* item = XTableWidgetItem_create();
    XColor bg = XColor_create_rgb(10, 20, 30, 255);
    XColor fg = XColor_create_rgb(200, 100, 50, 255);
    XColor out;
    XFont font;
    XSize size;
    XQ_REQUIRE(item != NULL);

    /* 文本型 role：写入/读取/清除。 */
    XTableWidgetItem_setStatusTip_utf8(item, "状态提示");
    XTableWidgetItem_setToolTip_utf8(item, "工具提示");
    XTableWidgetItem_setWhatsThis_utf8(item, "这是什么");
    XQ_EQ_STR(XTableWidgetItem_statusTip_toUtf8(item), "状态提示");
    XQ_EQ_STR(XTableWidgetItem_toolTip_toUtf8(item), "工具提示");
    XQ_EQ_STR(XTableWidgetItem_whatsThis_toUtf8(item), "这是什么");
    XTableWidgetItem_setToolTip_utf8(item, NULL);
    XQ_EQ_STR(XTableWidgetItem_toolTip_toUtf8(item), "");

    /* BackgroundRole/ForegroundRole 往返 + NULL 清除。 */
    XQ_CHECK(!XTableWidgetItem_background(item, &out));
    XTableWidgetItem_setBackground(item, &bg);
    XQ_CHECK(XTableWidgetItem_background(item, &out));
    XQ_CHECK(XColor_rgba(&out) == XColor_rgba(&bg));
    XTableWidgetItem_setBackground(item, NULL);
    XQ_CHECK(!XTableWidgetItem_background(item, &out));

    XQ_CHECK(!XTableWidgetItem_foreground(item, &out));
    XTableWidgetItem_setForeground(item, &fg);
    XQ_CHECK(XTableWidgetItem_foreground(item, &out));
    XQ_CHECK(XColor_rgba(&out) == XColor_rgba(&fg));
    XTableWidgetItem_setForeground(item, NULL);
    XQ_CHECK(!XTableWidgetItem_foreground(item, &out));

    /* FontRole：借用指针往返（Qt 为 QFont 值拷贝，本库按借用承载）。 */
    XFont_init(&font);
    XQ_CHECK(XTableWidgetItem_font(item) == NULL);
    XTableWidgetItem_setFont(item, &font);
    XQ_CHECK(XTableWidgetItem_font(item) == &font);
    XTableWidgetItem_setFont(item, NULL);
    XQ_CHECK(XTableWidgetItem_font(item) == NULL);
    XClassDeinit((XClass*)&font);

    /* TextAlignmentRole：0 表示清除。 */
    XQ_EQ_I64(XTableWidgetItem_textAlignment(item), 0);
    XTableWidgetItem_setTextAlignment(item, XAlignment_Center);
    XQ_EQ_I64(XTableWidgetItem_textAlignment(item), XAlignment_Center);
    XTableWidgetItem_setTextAlignment(item, 0);
    XQ_EQ_I64(XTableWidgetItem_textAlignment(item), 0);

    /* CheckStateRole 三态往返。 */
    XQ_EQ_I64(XTableWidgetItem_checkState(item), XItemCheckState_Unchecked);
    XTableWidgetItem_setCheckState(item, XItemCheckState_PartiallyChecked);
    XQ_EQ_I64(XTableWidgetItem_checkState(item),
              XItemCheckState_PartiallyChecked);
    XTableWidgetItem_setCheckState(item, XItemCheckState_Checked);
    XQ_EQ_I64(XTableWidgetItem_checkState(item), XItemCheckState_Checked);
    /* 越界枚举原样存取（gap 297；Qt 6.8.0 实测 7 / -3 均原样保留）。 */
    XTableWidgetItem_setCheckState(item, 7);
    XQ_EQ_I64(XTableWidgetItem_checkState(item), 7);
    XTableWidgetItem_setCheckState(item, -3);
    XQ_EQ_I64(XTableWidgetItem_checkState(item), -3);
    XTableWidgetItem_setCheckState(item, XItemCheckState_Unchecked);

    /* SizeHintRole：Qt 以 QSize::isValid()（w>=0 && h>=0）判定——
     * (0,0) 有效存值、负值清除。 */
    XSize_init(&size, 0, 0);
    XTableWidgetItem_setSizeHint(item, &size);
    XQ_CHECK(XTableWidgetItem_sizeHint(item, &size));
    XQ_EQ_I64(size.width, 0);
    XQ_EQ_I64(size.height, 0);
    XSize_init(&size, -1, 5);
    XTableWidgetItem_setSizeHint(item, &size);
    XQ_CHECK(!XTableWidgetItem_sizeHint(item, &size));
    XSize_init(&size, 10, 20);
    XTableWidgetItem_setSizeHint(item, &size);
    XQ_CHECK(XTableWidgetItem_sizeHint(item, &size));
    XQ_EQ_I64(size.width, 10);
    XQ_EQ_I64(size.height, 20);

    /* NULL 尺寸等同清除。 */
    XTableWidgetItem_setSizeHint(item, NULL);
    XQ_CHECK(!XTableWidgetItem_sizeHint(item, &size));

    XClassDelete((XClass*)item);
}

/* ==================== ④ operator< 分档比较 ==================== */

XQ_TEST(XTableWidgetItem, lessThanVariants)
{
    XTableWidgetItem* a = XTableWidgetItem_create_utf8("a", 0);
    XTableWidgetItem* b = XTableWidgetItem_create_utf8("b", 0);
    XTableWidgetItem* empty = XTableWidgetItem_create();
    XTableWidgetItem* three = XTableWidgetItem_create();
    XTableWidgetItem* five = XTableWidgetItem_create();
    XTableWidgetItem* twoHalf = XTableWidgetItem_create();
    XTableWidgetItem* str5 = XTableWidgetItem_create_utf8("5", 0);
    XQ_REQUIRE(a && b && empty && three && five && twoHalf && str5);

    xtiSetInt(three, XItemDataRole_DisplayRole, 3);
    xtiSetInt(five, XItemDataRole_DisplayRole, 5);
    xtiSetDouble(twoHalf, XItemDataRole_DisplayRole, 2.5);

    /* 字符串档：localeAwareCompare（ASCII 下与字节序一致）。 */
    XQ_CHECK(XTableWidgetItem_lessThan(a, b));
    XQ_CHECK(!XTableWidgetItem_lessThan(b, a));
    XQ_CHECK(!XTableWidgetItem_lessThan(a, a));

    /* 整数档：3 < 5（旧实现两值都当"无值"⇒ 判等，本轮根修）。 */
    XQ_CHECK(XTableWidgetItem_lessThan(three, five));
    XQ_CHECK(!XTableWidgetItem_lessThan(five, three));

    /* 含浮点 ⇒ 浮点档：2.5 < 3。 */
    XQ_CHECK(XTableWidgetItem_lessThan(twoHalf, three));
    XQ_CHECK(!XTableWidgetItem_lessThan(three, twoHalf));

    /* 数值 vs 字符串 ⇒ 字符串档（Qt variantLessThan 取较宽档位）：
     * "3" < "5"、"5" < "a"。 */
    XQ_CHECK(XTableWidgetItem_lessThan(three, str5));
    XQ_CHECK(!XTableWidgetItem_lessThan(str5, three));
    XQ_CHECK(XTableWidgetItem_lessThan(five, a));
    XQ_CHECK(!XTableWidgetItem_lessThan(a, five));

    /* 缺失 DisplayRole ⇒ 空串参与字符串比较（最小的非负值）。 */
    XQ_CHECK(XTableWidgetItem_lessThan(empty, a));
    XQ_CHECK(!XTableWidgetItem_lessThan(a, empty));
    XQ_CHECK(!XTableWidgetItem_lessThan(empty, empty));

    XClassDelete((XClass*)a);
    XClassDelete((XClass*)b);
    XClassDelete((XClass*)empty);
    XClassDelete((XClass*)three);
    XClassDelete((XClass*)five);
    XClassDelete((XClass*)twoHalf);
    XClassDelete((XClass*)str5);
}

/* ==================== ⑤ 拷贝构造/clone：type 与选中态不拷 ============ */

XQ_TEST(XTableWidgetItem, copyDoesNotCarryType)
{
    XTableWidgetItem* src = XTableWidgetItem_create_utf8("源", 1001);
    XTableWidgetItem* copy;
    XTableWidgetItem* clone;
    XQ_REQUIRE(src != NULL);

    XTableWidgetItem_setFlags(src, XItemFlag_ItemIsSelectable |
                                   XItemFlag_ItemIsEnabled);
    /* 游离条目 setSelected 是空操作（gap 296：Qt 的 `if (d->view)` 分支；
     * 本机 Qt 6.8.0 实测未插入的 QTableWidgetItem setSelected(true) 后
     * isSelected()==false）。旧演示套件 xgui_demo_apitest_views.c:3137-3146
     * 反向锁着旧口径（迁移台账）。 */
    XTableWidgetItem_setSelected(src, true);
    XQ_CHECK(!XTableWidgetItem_isSelected(src));
    XTableWidgetItem_setCheckState(src, XItemCheckState_Checked);

    copy = XTableWidgetItem_create_copy(src);
    XQ_REQUIRE(copy != NULL);
    /* Qt 拷贝构造：rtti(Type)、view(nullptr)、flags 随拷、选中不拷。 */
    XQ_EQ_I64(XTableWidgetItem_type(copy), XTableWidgetItem_Type);
    XQ_CHECK(XTableWidgetItem_tableWidget(copy) == NULL);
    XQ_EQ_I64(XTableWidgetItem_flags(copy),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    XQ_CHECK(!XTableWidgetItem_isSelected(copy));
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(copy), "源");
    XQ_EQ_I64(XTableWidgetItem_checkState(copy), XItemCheckState_Checked);
    /* 源对象不受影响。 */
    XQ_EQ_I64(XTableWidgetItem_type(src), 1001);
    XQ_CHECK(!XTableWidgetItem_isSelected(src));

    /* clone() 同样回落 Type（Qt `new QTableWidgetItem(*this)`）。 */
    clone = XTableWidgetItem_clone_base(src);
    XQ_REQUIRE(clone != NULL);
    XQ_EQ_I64(XTableWidgetItem_type(clone), XTableWidgetItem_Type);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(clone), "源");
    XQ_EQ_I64(XTableWidgetItem_checkState(clone), XItemCheckState_Checked);

    /* XClassCopy 对标 Qt operator=（:1536-1544）：只拷 values+flags，
     * 保留左侧 type（1002）。 */
    {
        XTableWidgetItem* dst = XTableWidgetItem_create_utf8("旧", 1002);
        XQ_REQUIRE(dst != NULL);
        XClassCopy(dst, src);
        XQ_EQ_I64(XTableWidgetItem_type(dst), 1002);
        XQ_EQ_STR(XTableWidgetItem_text_toUtf8(dst), "源");
        XQ_EQ_I64(XTableWidgetItem_flags(dst),
                  XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
        XQ_EQ_I64(XTableWidgetItem_checkState(dst),
                  XItemCheckState_Checked);
        XClassDelete((XClass*)dst);
    }

    XClassDelete((XClass*)clone);
    XClassDelete((XClass*)copy);
    XClassDelete((XClass*)src);
}

/* ==================== ⑥ 同值 setData 不发变更信号 ==================== */

XQ_TEST(XTableWidgetItem, sameValueSetDataIsSilent)
{
    XTableWidgetItem* item = XTableWidgetItem_create_utf8("x", 0);
    XTableWidget* tw;
    XQ_REQUIRE(item != NULL);
    tw = xtiMakeTable(item);
    XQ_REQUIRE(tw != NULL);

    g_tiItemChanged = 0;
    XObject_connect((XObject*)tw,
                      XSignal(XTableWidget_cellChanged_signal),
                      (XObject*)tw, xtiItemChangedSlot,
                      XConnectionType_Direct);

    /* Qt 同值早退：同文本不计数（qtablewidget.cpp:1399-1412）。 */
    XTableWidgetItem_setText_utf8(item, "x");
    XQ_EQ_I64(g_tiItemChanged, 0);
    XTableWidgetItem_setText_utf8(item, "y");
    XQ_EQ_I64(g_tiItemChanged, 1);
    XTableWidgetItem_setText_utf8(item, "y");
    XQ_EQ_I64(g_tiItemChanged, 1);
    XTableWidgetItem_setData(item, XItemDataRole_EditRole, NULL); /* 移除 */
    XQ_EQ_I64(g_tiItemChanged, 2);
    XTableWidgetItem_setData(item, XItemDataRole_DisplayRole, NULL); /* 已缺 */
    XQ_EQ_I64(g_tiItemChanged, 3);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑦ setFlags 同值也通知 ==================== */

XQ_TEST(XTableWidgetItem, setFlagsAlwaysNotifies)
{
    XTableWidgetItem* item = XTableWidgetItem_create_utf8("f", 0);
    XTableWidget* tw;
    uint32_t flags;
    XQ_REQUIRE(item != NULL);
    tw = xtiMakeTable(item);
    XQ_REQUIRE(tw != NULL);

    flags = XTableWidgetItem_flags(item);
    g_tiItemChanged = 0;
    XObject_connect((XObject*)tw,
                      XSignal(XTableWidget_cellChanged_signal),
                      (XObject*)tw, xtiItemChangedSlot,
                      XConnectionType_Direct);

    /* Qt 无同值早退（qtablewidget.cpp:1129-1135）。 */
    XTableWidgetItem_setFlags(item, flags);
    XQ_EQ_I64(g_tiItemChanged, 1);
    XTableWidgetItem_setFlags(item, XItemFlag_ItemIsEnabled);
    XQ_EQ_I64(g_tiItemChanged, 2);
    XQ_EQ_I64(XTableWidgetItem_flags(item), XItemFlag_ItemIsEnabled);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑧ 序列化回环与健壮性 ==================== */

XQ_TEST(XTableWidgetItem, serializationRoundTrip)
{
    XTableWidgetItem* src = XTableWidgetItem_create_utf8("序列", 7);
    XTableWidgetItem* dst = XTableWidgetItem_create();
    XByteArray* bytes = XByteArray_create();
    XColor fg = XColor_create_rgb(1, 2, 3, 255);
    XColor out;
    XQ_REQUIRE(src != NULL && dst != NULL && bytes != NULL);

    XTableWidgetItem_setCheckState(src, XItemCheckState_Checked);
    XTableWidgetItem_setForeground(src, &fg);
    XTableWidgetItem_setFlags(src, XItemFlag_ItemIsSelectable |
                                   XItemFlag_ItemIsEnabled);
    xtiSetInt(src, XItemDataRole_UserRole + 1, 4242);

    {
        XDataStream ws;
        XDataStream_init_array(&ws, bytes);
        XQ_CHECK(XTableWidgetItem_write(src, &ws));
        XClassDeinit((XClass*)&ws);
    }
    {
        XDataStream rs;
        XDataStream_init_arrayReadOnly(&rs, bytes);
        XQ_CHECK(XTableWidgetItem_read(dst, &rs));
        XClassDeinit((XClass*)&rs);
    }

    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(dst), "序列");
    XQ_EQ_I64(XTableWidgetItem_checkState(dst), XItemCheckState_Checked);
    XQ_EQ_I64(XTableWidgetItem_flags(dst),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    XQ_EQ_I64(XTableWidgetItem_type(dst), 7);
    XQ_CHECK(XTableWidgetItem_foreground(dst, &out));
    XQ_CHECK(XColor_rgba(&out) == XColor_rgba(&fg));
    {
        const XVariant* uv = XTableWidgetItem_data(dst,
                                                   XItemDataRole_UserRole + 1);
        XQ_CHECK_PTR(uv);
        XQ_EQ_I64(uv ? XVariant_toInt(uv) : -1, 4242);
    }

    /* 截断流：read 失败且目标条目保持原值。 */
    {
        XTableWidgetItem* keep = XTableWidgetItem_create_utf8("keep", 0);
        XDataStream rs;
        XQ_REQUIRE(keep != NULL);
        if (XByteArray_size_base(bytes) > 5)
            XVector_resize_base((XVector*)bytes, 5);
        XDataStream_init_arrayReadOnly(&rs, bytes);
        XQ_CHECK(!XTableWidgetItem_read(keep, &rs));
        XQ_EQ_STR(XTableWidgetItem_text_toUtf8(keep), "keep");
        XClassDeinit((XClass*)&rs);
        XClassDelete((XClass*)keep);
    }

    /* NULL 参数安全。 */
    XQ_CHECK(!XTableWidgetItem_write(NULL, NULL));
    XQ_CHECK(!XTableWidgetItem_read(NULL, NULL));

    XClassDelete((XClass*)bytes);
    XClassDelete((XClass*)dst);
    XClassDelete((XClass*)src);
}

/* ==================== ⑨ 所有权：setItem 接管 / takeItem 交还 ========== */

XQ_TEST(XTableWidgetItem, ownershipThroughTable)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XTableWidgetItem* item;
    XTableWidgetItem* taken;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);

    item = XTableWidgetItem_create_utf8("A", 0);
    XQ_REQUIRE(item != NULL);
    XTableWidget_setItem(tw, 1, 0, item);
    XQ_CHECK(XTableWidget_item(tw, 1, 0) == item);
    XQ_CHECK(XTableWidgetItem_tableWidget(item) == tw);
    XQ_EQ_I64(XTableWidgetItem_row(item), 1);
    XQ_EQ_I64(XTableWidgetItem_column(item), 0);

    /* takeItem 移出（所有权交还调用方），索引反查复位。 */
    taken = XTableWidget_takeItem(tw, 1, 0);
    XQ_CHECK(taken == item);
    XQ_CHECK(XTableWidget_item(tw, 1, 0) == NULL);
    XQ_CHECK(XTableWidgetItem_tableWidget(taken) == NULL);
    XQ_EQ_I64(XTableWidgetItem_row(taken), -1);
    XQ_EQ_I64(XTableWidgetItem_column(taken), -1);
    XClassDelete((XClass*)taken);

    XClassDelete((XClass*)tw); /* 其余条目（无）随表释放 */
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
