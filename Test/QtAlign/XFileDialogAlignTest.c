/**
 * @file       XFileDialogAlignTest.c
 * @brief      XFileDialog × Qt 6.8 QFileDialog 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qfiledialog.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qfiledialog.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 构造默认 fileMode=AnyFile(0)、acceptMode=AcceptOpen(0)、
 *               options=0（F0，qfiledialog.cpp:2921 `setFileMode(AnyFile)`）；
 *               viewMode=Detail（qfiledialog.cpp:166 文档 + 本机 Qt 6.8
 *               探针 F0 实测 0=Detail）；
 *             - 构造缺省 nameFilters==["All Files (*)"]、selectedNameFilter
 *               同值（qplatformdialoghelper.cpp:587-603；探针 F0）；
 *             - 无选择且 fileMode 不是 ExistingFile/ExistingFiles 时
 *               selectedFiles 回落当前目录（qfiledialog.cpp:1291-1306；
 *               探针 F0/F5）；
 *             - selectFile 为**替换**语义并按当前目录补全绝对路径，空串
 *               忽略（qfiledialog.cpp:1058-1098；探针 F7）；
 *             - setNameFilters 整体替换后当前选中过滤器取**首条**
 *               （qfiledialog.cpp:1409-1436 `useNameFilter(0)`；F1 实测
 *               selectedNameFilter=="Text (*.txt)"），空列表则清空；
 *             - selectNameFilter 只对**列表内**过滤器生效（qfiledialog.cpp:
 *               1449-1475 findText 判定；F2 未知过滤器不改状态、F3 已知
 *               过滤器切换）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XFileDialog.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XFileSystem_config.h"
/* P3-37~47 断言所需控件类型（实例控件树/委托/枚举位集）。 */
#include "XTreeWidget.h"
#include "XListView.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XComboBox.h"
#include "XLabel.h"
#include "XAbstractButton.h"
#include "XItemDelegate.h"
#include "XStringUtils.h" /* XSnprintf（格式化纪律：经 StringUtils） */
#if XFILE_ON && XDIR_ON
#include "XDir.h"
#endif

/** @brief 取列表元素数（借用列表）。 */
static int64_t xfdCount(const XStringList* list)
{
    if (!list) return 0;
    return XStringList_size_base((const XContainer*)list);
}

/** @brief 空串安全转换：本库 XString_toUtf8 对空串返回 NULL。 */
static const char* xfdCStr(const char* s)
{
    return s ? s : "";
}

/** @brief 判断列表下标元素是否等于给定 UTF-8 文本。 */
static bool xfdAtEq(const XStringList* list, int64_t index, const char* utf8)
{
    const XString* item;
    if (!list || !utf8) return false;
    item = (const XString*)(const void*)XStringList_at_base(
        (const XVector*)list, index);
    if (!item) return false;
    return strcmp(xfdCStr(XString_toUtf8(item)), utf8) == 0;
}

/** @brief 消费一个堆上 XString 并判断其 UTF-8 内容（失败也释放）。 */
static bool xfdTakeEq(XString* text, const char* utf8)
{
    bool same;
    if (!text || !utf8) {
        if (text) XClassDelete((XClass*)text);
        return false;
    }
    same = strcmp(xfdCStr(XString_toUtf8(text)), utf8) == 0;
    XClassDelete((XClass*)text);
    return same;
}

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XFileDialog, constructorDefaults)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* filters;
    XStringList* files;
    XQ_REQUIRE(dlg != NULL);

    /* 实测 F0：fileMode=0（AnyFile）、acceptMode=0（AcceptOpen）、
     * options=0（qfiledialog.cpp:2921 + 属性文档 :1678）；viewMode 缺省
     * Detail（qfiledialog.cpp:166，本机 6.8 探针 F0 实测 0）。 */
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_AnyFile);
    XQ_EQ_I64(XFileDialog_acceptMode(dlg), XFileDialog_AcceptOpen);
    XQ_EQ_I64(XFileDialog_viewMode(dlg), XFileDialogViewMode_Detail);
    XQ_EQ_I64(XFileDialog_options(dlg), 0);
    XQ_CHECK(!XFileDialog_testOption(dlg,
                                     XFileDialog_DontUseNativeDialog));

    /* 缺省 nameFilters==["All Files (*)"]、selectedNameFilter 同值
     * （qplatformdialoghelper.cpp:587-603；探针 F0）。 */
    filters = XFileDialog_nameFilters(dlg);
    XQ_EQ_I64(xfdCount(filters), 1);
    XQ_CHECK(xfdAtEq(filters, 0, "All Files (*)"));
    if (filters) XClassDelete((XClass*)filters);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), "All Files (*)"));

    /* 无选择 + AnyFile：selectedFiles 回落当前目录（qfiledialog.cpp:
     * 1291-1306；探针 F0）。裁剪构建（XDIR_ON=0）取不到当前目录，
     * 回落为空列表。 */
    files = XFileDialog_selectedFiles(dlg);
#if XFILE_ON && XDIR_ON
    XQ_EQ_I64(xfdCount(files), 1);
    {
        XString* cwd = XDir_currentPath();
        if (cwd) {
            XQ_CHECK(xfdAtEq(files, 0, xfdCStr(XString_toUtf8(cwd))));
            XClassDelete((XClass*)cwd);
        }
    }
#else
    XQ_EQ_I64(xfdCount(files), 0);
#endif
    if (files) XClassDelete((XClass*)files);

    /* ExistingFile 模式无选择：空列表（探针 F5）；selectedFile 同口径。 */
    XFileDialog_setFileMode(dlg, XFileDialog_ExistingFile);
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 0);
    if (files) XClassDelete((XClass*)files);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), ""));
    XFileDialog_setFileMode(dlg, XFileDialog_AnyFile);

    /* directory 仍为本库「显式设置才非空」口径。 */
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), ""));

    XClassDelete(dlg);
}

/* ==================== ② 名称过滤器选择语义（F1~F3） ==================== */

XQ_TEST(XFileDialog, nameFilterSelection)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* filters;
    XString* f;
    XQ_REQUIRE(dlg != NULL);
    filters = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(filters != NULL);
    {
        XString* a = XString_create_utf8("Text (*.txt)");
        XString* b = XString_create_utf8("All (*)");
        XQ_REQUIRE(a != NULL && b != NULL);
        XStringList_push_back_move_base((XVector*)filters, a);
        XStringList_push_back_move_base((XVector*)filters, b);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
    }

    /* 实测 F1：setNameFilters 后选中首条（useNameFilter(0)）。 */
    XFileDialog_setNameFilters(dlg, filters);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "Text (*.txt)"));

    /* 实测 F2：列表外过滤器不改变选中。 */
    f = XString_create_utf8("Nope (*.zzz)");
    XQ_REQUIRE(f != NULL);
    XFileDialog_selectNameFilter(dlg, f);
    XClassDelete((XClass*)f);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "Text (*.txt)"));

    /* 实测 F3：列表内过滤器切换成功。 */
    f = XString_create_utf8("All (*)");
    XQ_REQUIRE(f != NULL);
    XFileDialog_selectNameFilter(dlg, f);
    XClassDelete((XClass*)f);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), "All (*)"));

    /* 空列表清空选中（本机 Qt 6.8 实测 setNameFilters(空) 后为 ""）。 */
    XFileDialog_setNameFilters(dlg, NULL);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), ""));
    {
        XStringList* got = XFileDialog_nameFilters(dlg);
        XQ_EQ_I64(xfdCount(got), 0);
        if (got) XClassDelete((XClass*)got);
    }

    XClassDelete((XClass*)filters);
    XClassDelete(dlg);
}

/* ==================== ③ setNameFilter 的 ";;" 拆分 ==================== */

XQ_TEST(XFileDialog, setNameFilterSplit)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* filter;
    XStringList* got;
    XQ_REQUIRE(dlg != NULL);

    /* 对标 qfiledialog.cpp:1363-1367 setNameFilter = setNameFilters(
     * qt_make_filter_list(filter))——按 ";;" 拆分，首条成为选中。 */
    filter = XString_create_utf8("图片 (*.png);;文本 (*.txt)");
    XQ_REQUIRE(filter != NULL);
    XFileDialog_setNameFilter(dlg, filter);
    XClassDelete((XClass*)filter);

    got = XFileDialog_nameFilters(dlg);
    XQ_EQ_I64(xfdCount(got), 2);
    XQ_CHECK(xfdAtEq(got, 0, "图片 (*.png)"));
    XQ_CHECK(xfdAtEq(got, 1, "文本 (*.txt)"));
    if (got) XClassDelete((XClass*)got);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "图片 (*.png)"));

    XClassDelete(dlg);
}

/* ==================== ④ fileMode / acceptMode / 选项往返 ==================== */

XQ_TEST(XFileDialog, modeAndOptionsRoundTrip)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    XFileDialog_setFileMode(dlg, XFileDialog_ExistingFile);
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_ExistingFile);
    XFileDialog_setFileMode(dlg, XFileDialog_Directory);
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_Directory);

    XFileDialog_setAcceptMode(dlg, XFileDialog_AcceptSave);
    XQ_EQ_I64(XFileDialog_acceptMode(dlg), XFileDialog_AcceptSave);

    /* 选项位值与 Qt::Option 一致（qfiledialog.h:43-56）。 */
    XFileDialog_setOption(dlg, XFileDialog_DontConfirmOverwrite, true);
    XQ_CHECK(XFileDialog_testOption(dlg,
                                    XFileDialog_DontConfirmOverwrite));
    XFileDialog_setOption(dlg, XFileDialog_ReadOnly, true);
    XQ_EQ_I64(XFileDialog_options(dlg),
              (XFileDialog_DontConfirmOverwrite | XFileDialog_ReadOnly));
    XFileDialog_setOption(dlg, XFileDialog_DontConfirmOverwrite, false);
    XQ_CHECK(!XFileDialog_testOption(dlg,
                                     XFileDialog_DontConfirmOverwrite));
    XQ_EQ_I64(XFileDialog_options(dlg), XFileDialog_ReadOnly);

    XClassDelete(dlg);
}

/* ==================== ⑤ selectFile 替换 + 路径补全（F7） ============== */

XQ_TEST(XFileDialog, selectFileReplacesAndResolves)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* a;
    XString* b;
    XStringList* files;
    XQ_REQUIRE(dlg != NULL);

    /* 目录 "doc"（相对）用于验证相对名补全口径：QDir::filePath 拼接
     * → "doc/<name>"（探针 F7 在 cwd="/home/..." 时得到绝对路径；
     * 本库 setDirectory 保留调用方给的相对目录，拼接口径一致）。 */
    {
        XString* d = XString_create_utf8("doc");
        XQ_REQUIRE(d != NULL);
        XFileDialog_setDirectory(dlg, d);
        XClassDelete((XClass*)d);
    }
    a = XString_create_utf8("a.txt");
    b = XString_create_utf8("b.txt");
    XQ_REQUIRE(a != NULL && b != NULL);
    XFileDialog_selectFile(dlg, a);
    XFileDialog_selectFile(dlg, b);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)b);

    /* 替换而非累积（qfiledialog.cpp:1088 clear + setText；探针 F7：
     * 两次 selectFile 后 selectedFiles()==1 条）。 */
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "doc/b.txt"));
    if (files) XClassDelete((XClass*)files);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "doc/b.txt"));

    /* 空串忽略（qfiledialog.cpp:1060 `if (filename.isEmpty()) return`）。 */
    {
        XString* empty = XString_create_utf8("");
        XQ_REQUIRE(empty != NULL);
        XFileDialog_selectFile(dlg, empty);
        XClassDelete((XClass*)empty);
    }
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "doc/b.txt"));

    /* 绝对名原样保留，并把目录切到父目录（qfiledialog.cpp:1069-1075）。 */
    {
        XString* abs = XString_create_utf8("/tmp/xfd_abs.txt");
        XQ_REQUIRE(abs != NULL);
        XFileDialog_selectFile(dlg, abs);
        XClassDelete((XClass*)abs);
    }
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "/tmp/xfd_abs.txt"));
#if XFILE_ON && XDIR_ON && XFILEINFO_ON
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), "/tmp"));

    /* 无选择 + AnyFile → selectedFiles 回落 directory（探针 F0/F5）。 */
    if (dlg->m_selectedFiles)
        XStringList_clear_base((XContainer*)dlg->m_selectedFiles);
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "/tmp"));
    if (files) XClassDelete((XClass*)files);
#endif

    /* selectUrl 转发 selectFile（Qt selectUrl→selectFile(toLocalFile)，
     * qfiledialog.cpp:1099-1112）——[P0-42] 旧断言锁的是「原串入选中
     * 表」的反 Qt 行为（file:///tmp/x.c 原样往返），已按台账改为
     * 「父目录切换 + 1 条绝对路径」：X 侧 URL 恒为路径串（不做
     * toLocalFile 转换），故以绝对路径串 /tmp/x.c 扮演本地 URL。 */
    {
        XString* u = XString_create_utf8("/tmp/x.c");
        XQ_REQUIRE(u != NULL);
        XFileDialog_selectUrl(dlg, u);
        XClassDelete((XClass*)u);
    }
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "/tmp/x.c"));
    if (files) XClassDelete((XClass*)files);
#if XFILE_ON && XDIR_ON && XFILEINFO_ON
    /* 绝对 URL 同时把 directory 切到父目录（selectFile 语义）。 */
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), "/tmp"));
#endif
#if XFILE_ON && XDIR_ON && XFILEINFO_ON
    /* 相对 URL 按当前目录补全（selectFile 语义，非原样记录）——
     * 绝对名已把目录切到 /tmp，相对名落 /tmp/rel.c。裁剪构建
     * （XFILEINFO_ON=0）不切父目录，此断言不成立。 */
    {
        XString* u = XString_create_utf8("rel.c");
        XQ_REQUIRE(u != NULL);
        XFileDialog_selectUrl(dlg, u);
        XClassDelete((XClass*)u);
    }
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "/tmp/rel.c"));
#endif

    XClassDelete(dlg);
}

/* ==================== ⑤b 目录 URL 单一事实源（P0-41） ==================== */

XQ_TEST(XFileDialog, directoryUrlSingleSource)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* p;
    XQ_REQUIRE(dlg != NULL);

    /* setDirectory ⇒ directoryUrl 由 directory 派生（Qt
     * qfiledialog.cpp:1040-1044 同口径）。旧实现两个字段互不联动：
     * setDirectory 后 directoryUrl()==""、setDirectoryUrl 后
     * directory() 仍旧值。 */
    p = XString_create_utf8("/tmp");
    XQ_REQUIRE(p != NULL);
    XFileDialog_setDirectory(dlg, p);
    XClassDelete((XClass*)p);
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), "/tmp"));
    XQ_CHECK(xfdTakeEq(XFileDialog_directoryUrl(dlg), "/tmp"));

    /* setDirectoryUrl ⇒ 本地 URL 直接 setDirectory（qfiledialog.cpp:
     * 1031-1038）；X 侧 URL 恒为路径串，无 file:// 前缀往返。 */
    p = XString_create_utf8("/var");
    XQ_REQUIRE(p != NULL);
    XFileDialog_setDirectoryUrl(dlg, p);
    XClassDelete((XClass*)p);
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), "/var"));
    XQ_CHECK(xfdTakeEq(XFileDialog_directoryUrl(dlg), "/var"));

    /* 清空：两 getter 同为空串（不存在一空一旧的矛盾态）。 */
    XFileDialog_setDirectory(dlg, NULL);
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), ""));
    XQ_CHECK(xfdTakeEq(XFileDialog_directoryUrl(dlg), ""));

    XClassDelete(dlg);
}

/* ==================== ⑤c selectUrl 空串忽略（P0-42 边界） ============ */

XQ_TEST(XFileDialog, selectUrlIgnoresEmpty)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* p;
    XString* empty;
    XQ_REQUIRE(dlg != NULL);
    p = XString_create_utf8("/tmp/keep.c");
    empty = XString_create_utf8("");
    XQ_REQUIRE(p != NULL && empty != NULL);
    XFileDialog_selectUrl(dlg, p);
    XFileDialog_selectUrl(dlg, empty); /* 空串忽略（selectFile 同口径） */
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "/tmp/keep.c"));
    XClassDelete((XClass*)p);
    XClassDelete((XClass*)empty);
    XClassDelete(dlg);
}

/* ==================== ⑥ 父子所有权 ==================== */

XQ_TEST(XFileDialog, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XFileDialog* dlg;
    XQ_REQUIRE(parent != NULL);
    dlg = XFileDialog_create(parent, 0);
    XQ_REQUIRE(dlg != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    XQ_CHECK(XClassGetVtable((XObject*)dlg) == XFileDialog_class_init());
    XQ_CHECK(XWidget_isWindow((XWidget*)dlg));
    XClassDelete(dlg);
    XClassDelete(parent);
}

/* ==================== P3-37 实例路径控件树 ====================
 * Qt：QFileDialogPrivate::setVisible 在首次显示时 createWidgets()
 * （qfiledialog.cpp:858-876）。X 侧此前 init 只置状态、实例路径显示
 * 空壳；现 VXFileDialog_showEvent 首显经 xff_buildInto 装配。 */

#if XFILE_ON && XDIR_ON
/** @brief 按 objectName 查对话框直接子控件（测试侧；等价 QObject::
 *  findChild）。 */
static XWidget* xfdChild(XFileDialog* dlg, const char* name)
{
    XString tmp;
    XWidget* w;
    if (!dlg) return NULL;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    w = (XWidget*)XObject_findChild((XObject*)dlg, &tmp,
                                    XFindChildOption_DirectChildrenOnly);
    XClassDeinit((XClass*)&tmp);
    return w;
}

/** @brief 直接子控件数（XObject_children 借用）。 */
static int64_t xfdChildCount(XFileDialog* dlg)
{
    const XVector* kids = dlg ? XObject_children((XObject*)dlg) : NULL;
    return kids ? (int64_t)XVector_size_base(kids) : 0;
}

/** @brief 文件列表树中是否存在文本为 name 的行（列 0）。 */
static bool xfdTreeHasRow(const XTreeWidget* view, const char* name)
{
    int n = view ? XTreeWidget_topLevelItemCount(view) : 0;
    int i;
    for (i = 0; i < n; ++i) {
        XTreeWidgetItem* item = XTreeWidget_topLevelItem(view, i);
        const char* t = item ? XTreeWidgetItem_textAt_2(item, 0) : NULL;
        if (t && strcmp(t, name) == 0) return true;
    }
    return false;
}
#endif /* XFILE_ON && XDIR_ON */

#if XFILE_ON && XDIR_ON
XQ_TEST(XFileDialog, instanceShowBuildsWidgetTree)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    int64_t before;
    XQ_REQUIRE(dlg != NULL);

    /* 构造后不建树（对标 Qt：createWidgets 在首次显示时才跑）。 */
    before = xfdChildCount(dlg);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_view") == NULL);

    XWidget_show((XWidget*)dlg);
    XQ_CHECK(xfdChildCount(dlg) > before);
    /* 控件树关键成员在场（地址栏/导航窗格/文件树/名称框/两按钮）。 */
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_dir_combo") != NULL);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_view") != NULL);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_sidebar") != NULL);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_name_edit") != NULL);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_ok") != NULL);
    XQ_CHECK(xfdChild(dlg, "qt_file_dialog_cancel") != NULL);

    /* 幂等：重复 SHOW 不再装配（子控件数不翻倍）。 */
    {
        int64_t after1 = xfdChildCount(dlg);
        XWidget_hide((XWidget*)dlg);
        XWidget_show((XWidget*)dlg);
        XQ_EQ_I64(xfdChildCount(dlg), after1);
    }

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* 回收纪律（取消崩溃根修）：实例对话框析构走与静态路径同一登记表，
 * 析构帧兑现布局句柄/导航模型——本用例以「建-显-删」验证析构不崩
 * 且 QtAlign 宿主堆计数零增长（布局/模型若泄漏即被泄漏门抓到）。 */
XQ_TEST(XFileDialog, instanceShowThenDeleteReclaimsLayouts)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    XWidget_show((XWidget*)dlg);
    XQ_REQUIRE(xfdChild(dlg, "qt_file_dialog_view") != NULL);
    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
    XQ_CHECK(true); /* 未崩溃即通过；泄漏由宿主泄漏门判定。 */
}
#endif /* XFILE_ON && XDIR_ON */

/* ==================== P3-38 open_2（非阻塞打开 + 关闭断开） ========== */

static int g_xfdOpenHits;
static char g_xfdOpenPayload[256];

static void xfdOpenSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_xfdOpenHits;
    if (args) {
        /* 解包首参（XString*；fileSelected 载荷）。 */
        XVarList_args_1(args, XString*, s);
        const char* u = s ? XString_toUtf8(s) : NULL;
        if (u) {
            size_t i;
            for (i = 0; u[i] && i + 1 < sizeof(g_xfdOpenPayload); ++i)
                g_xfdOpenPayload[i] = u[i];
            g_xfdOpenPayload[i] = '\0';
        }
    }
}

XQ_TEST(XFileDialog, openConnectsSelectedAndDisconnectsOnClose)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XObject* receiver = XObject_create();
    XString* p;
    XQ_REQUIRE(dlg != NULL && receiver != NULL);

    /* 单文件模式 ⇒ 连 fileSelected（Qt 文档：fileMode 非 ExistingFiles
     * 时连 fileSelected）。open 后窗口模态显示并立即返回。 */
    XFileDialog_setFileMode(dlg, XFileDialog_ExistingFile);
    g_xfdOpenHits = 0;
    g_xfdOpenPayload[0] = '\0';
    XFileDialog_open_2(dlg, receiver, xfdOpenSlot, false);
    XQ_CHECK(XWidget_isVisible((XWidget*)dlg));

    /* 结算一个真实存在的路径（accept 的 ExistingFile 分支校验存在性）
     * 后收口：filesSelected 恒发、单选补发 fileSelected ⇒ 槽恰一次。 */
    p = XString_create_utf8("/tmp/xfd_open_probe.c");
    XQ_REQUIRE(p != NULL);
    {
        FILE* f = fopen("/tmp/xfd_open_probe.c", "wb");
        if (f) fclose(f);
    }
    XFileDialog_selectFile(dlg, p);
    XClassDelete((XClass*)p);
    XFileDialog_accept(dlg);
    XQ_EQ_I64(g_xfdOpenHits, 1);
    XQ_CHECK(strcmp(g_xfdOpenPayload, "/tmp/xfd_open_probe.c") == 0);
    /* 关闭收口自动断开（对标 receiverToDisconnectOnClose）：记录清空。 */
    XQ_CHECK(dlg->m_openReceiver == NULL && dlg->m_openMember == NULL);
    XQ_CHECK(!XWidget_isVisible((XWidget*)dlg));
    remove("/tmp/xfd_open_probe.c");

    /* 重复 open 不叠连：再开一次、再收口，槽只加一（无残留旧连接
     * 导致一次收口触发两次）。 */
    XFileDialog_open_2(dlg, receiver, xfdOpenSlot, false);
    XQ_EQ_I64(g_xfdOpenHits, 1);
    {
        FILE* f = fopen("/tmp/xfd_open_probe.c", "wb");
        if (f) fclose(f);
    }
    p = XString_create_utf8("/tmp/xfd_open_probe.c");
    XQ_REQUIRE(p != NULL);
    XFileDialog_selectFile(dlg, p);
    XClassDelete((XClass*)p);
    XFileDialog_accept(dlg);
    XQ_EQ_I64(g_xfdOpenHits, 2);
    remove("/tmp/xfd_open_probe.c");

    XClassDelete((XClass*)receiver);
    XClassDelete(dlg);
}

/* ==================== P3-40 getOpenFileUrls 转发 getOpenFileNames ========
 * Qt：getOpenFileUrls 即 getOpenFileNames 的 URL 变体，但 X 侧 URL 恒为
 * 路径串（无 file:// 前缀往返），二者结果等价（QFileDialog::getOpenFileUrls
 * 在 X 对齐中直接转发，不做 toLocalFile）。自动化宿主（无显示）下两便捷
 * 函数走桩路径返回同口径的空列表契约（URL==path 由转发保持，断言体在
 * 有头环境下同样成立：条目逐项相等、无 file:// 前缀）；真实弹窗交互不
 * 在自动化中驱动。 */

XQ_TEST(XFileDialog, openFileUrlsForwardsToNames)
{
    XStringList* names = XFileDialog_getOpenFileNames(NULL, NULL, NULL, NULL, NULL);
    XStringList* urls = XFileDialog_getOpenFileUrls(NULL, NULL, NULL, NULL, NULL);
    XQ_REQUIRE(names != NULL && urls != NULL);
    /* 两便捷函数返回同口径（转发等价；X URL==path 无前缀转换）。 */
    XQ_EQ_I64(xfdCount(names), xfdCount(urls));
    {
        int64_t i, n = xfdCount(names);
        for (i = 0; i < n; ++i) {
            XString* ni = (XString*)XStringList_at_base((const XVector*)names, i);
            XString* ui = (XString*)XStringList_at_base((const XVector*)urls, i);
            XQ_CHECK(ni && ui &&
                     strcmp(XString_toUtf8(ni), XString_toUtf8(ui)) == 0);
            /* URL 即路径串：不得出现 file:// 前缀（X 侧不往返 toLocalFile）。 */
            if (ui) {
                const char* u = XString_toUtf8(ui);
                XQ_CHECK(u && (u[0] != 'f' || strncmp(u, "file:", 5) != 0));
            }
        }
    }
    XClassDelete((XClass*)names);
    XClassDelete((XClass*)urls);
}

/* ==================== P3-41/42 saveState/restoreState 私有格式 ======== */

XQ_TEST(XFileDialog, saveRestoreStateRoundTrip)
{
    XFileDialog* a = XFileDialog_create(NULL, 0);
    XFileDialog* b = XFileDialog_create(NULL, 0);
    XByteArray* state = XByteArray_create();
    XByteArray* junk = XByteArray_create();
    XString* dir;
    XStringList* hist;
    XStringList* side;
    XStringList* got;
    XQ_REQUIRE(a != NULL && b != NULL && state != NULL && junk != NULL);

    XFileDialog_setViewMode(a, XFileDialogViewMode_List);
    dir = XString_create_utf8("/tmp");
    XQ_REQUIRE(dir != NULL);
    XFileDialog_setDirectory(a, dir);
    XClassDelete((XClass*)dir);
    hist = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    side = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(hist != NULL && side != NULL);
    XStringList_push_back_utf8(hist, "/tmp");
    XStringList_push_back_utf8(hist, "/home");
    XStringList_push_back_utf8(side, "/tmp");
    XFileDialog_setHistory(a, hist);
    XFileDialog_setSidebarUrls(a, side);
    XClassDelete((XClass*)hist);
    XClassDelete((XClass*)side);

    XFileDialog_saveState(a, state);
    /* 私有格式非空（旧桩输出空容器）。 */
    XQ_CHECK(XByteArray_size_base((const XVector*)state) > 0);

    /* 同格式回读：逐字段回填。 */
    XQ_CHECK(XFileDialog_restoreState(b, state));
    XQ_EQ_I64(XFileDialog_viewMode(b), XFileDialogViewMode_List);
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(b), "/tmp"));
    got = XFileDialog_history(b);
    XQ_EQ_I64(xfdCount(got), 2);
    XQ_CHECK(xfdAtEq(got, 0, "/tmp"));
    XQ_CHECK(xfdAtEq(got, 1, "/home"));
    if (got) XClassDelete((XClass*)got);
    got = XFileDialog_sidebarUrls(b);
    XQ_EQ_I64(xfdCount(got), 1);
    XQ_CHECK(xfdAtEq(got, 0, "/tmp"));
    if (got) XClassDelete((XClass*)got);

    /* 校验先行：魔数/版本不符整体拒绝（不做部分恢复）。 */
    XByteArray_push_back_1(junk, (uint8_t)'B');
    XByteArray_push_back_1(junk, (uint8_t)'A');
    XByteArray_push_back_1(junk, (uint8_t)'D');
    XByteArray_push_back_1(junk, (uint8_t)'!');
    XQ_CHECK(!XFileDialog_restoreState(b, junk));
    XQ_CHECK(!XFileDialog_restoreState(b, NULL));
    /* 截断（只有魔数、无版本/字段）同样拒绝。 */
    {
        XByteArray* trunc = XByteArray_create();
        XQ_REQUIRE(trunc != NULL);
        XFileDialog_saveState(a, trunc);
        /* 截到 8 字节：魔数 + 版本在，字段缺 ⇒ 读取失败拒绝。 */
        while (XByteArray_size_base((const XVector*)trunc) > 8)
            XByteArray_pop_back_base((XVector*)trunc);
        XQ_CHECK(!XFileDialog_restoreState(b, trunc));
        XClassDelete((XClass*)trunc);
    }
    /* 拒绝时状态不被部分改写（viewMode 仍为 List 来自成功那次）。 */
    XQ_EQ_I64(XFileDialog_viewMode(b), XFileDialogViewMode_List);

    XClassDelete((XClass*)state);
    XClassDelete((XClass*)junk);
    XClassDelete(a);
    XClassDelete(b);
}

/* ==================== P3-43 setFilter 缺省值 + 接进枚举 ================ */

XQ_TEST(XFileDialog, filterDefaultIsQtValue)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    /* Qt QFileDialogPrivate::init 缺省 filter 口径（qfiledialog.cpp:166）：
     * AllEntries | NoDotAndDotDot | AllDirs。位值与 QDir::Filters 同域。 */
    XQ_EQ_I64(XFileDialog_filter(dlg),
              (int)(XDir_AllEntries | XDir_NoDotAndDotDot | XDir_AllDirs));
    XFileDialog_setFilter(dlg, (int)XDir_Files);
    XQ_EQ_I64(XFileDialog_filter(dlg), (int)XDir_Files);
    /* NoFilter(-1) 哨兵原样存取（合并式外处理，见 xff_listDir 注）。 */
    XFileDialog_setFilter(dlg, (int)XDir_NoFilter);
    XQ_EQ_I64(XFileDialog_filter(dlg), (int)XDir_NoFilter);
    XClassDelete(dlg);
}

#if XFILE_ON && XDIR_ON
/** @brief 建临时目录 + 指定条目（目录/文件）；失败返回 false。 */
static bool xfdMakeSample(const char* dir)
{
    char path[512];
    FILE* f;
    XDir d;
    XString* s = XString_create_utf8(dir);
    bool ok;
    if (!s) return false;
    XDir_init_2(&d, s);
    ok = XDir_mkpath(&d, s);
    XClassDeinit((XClass*)&d);
    XClassDelete((XClass*)s);
    if (!ok) return false;
    XSnprintf(path, sizeof(path), "%s/a.txt", dir);
    f = fopen(path, "wb");
    if (f) fclose(f);
    XSnprintf(path, sizeof(path), "%s/sub", dir);
    s = XString_create_utf8(path);
    if (s) {
        XDir_init_2(&d, s);
        (void)XDir_mkpath(&d, s);
        XClassDeinit((XClass*)&d);
        XClassDelete((XClass*)s);
    }
    return true;
}

XQ_TEST(XFileDialog, filterDrivesEnumeration)
{
    const char* dirPath = "/tmp/xfd_filter_probe";
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XTreeWidget* view;
    XString* dir;
    XQ_REQUIRE(dlg != NULL);
    XQ_REQUIRE(xfdMakeSample(dirPath));

    dir = XString_create_utf8(dirPath);
    XQ_REQUIRE(dir != NULL);
    XFileDialog_setDirectory(dlg, dir);
    XClassDelete((XClass*)dir);
    XWidget_show((XWidget*)dlg);
    view = (XTreeWidget*)xfdChild(dlg, "qt_file_dialog_view");
    XQ_REQUIRE(view != NULL);
    /* 缺省（AllEntries|AllDirs）：目录 sub + 文件 a.txt 都在列。 */
    XQ_CHECK(xfdTreeHasRow(view, "sub"));
    XQ_CHECK(xfdTreeHasRow(view, "a.txt"));

    /* 收窄到只文件：目录枚举关闭（类型位门控），目录行消失——位集
     * 真实接进枚举（此前硬编码恒列目录＋文件，setFilter 无效果）。 */
    XFileDialog_setFilter(dlg, (int)(XDir_Files | XDir_NoDotAndDotDot));
    XQ_CHECK(xfdTreeHasRow(view, "a.txt"));
    XQ_CHECK(!xfdTreeHasRow(view, "sub"));

    /* 收窄到只目录：文件枚举关闭。 */
    XFileDialog_setFilter(dlg,
                          (int)(XDir_Dirs | XDir_AllDirs |
                                XDir_NoDotAndDotDot));
    XQ_CHECK(xfdTreeHasRow(view, "sub"));
    XQ_CHECK(!xfdTreeHasRow(view, "a.txt"));

    /* NoFilter 哨兵按无限制处理（含目录与文件）。 */
    XFileDialog_setFilter(dlg, (int)XDir_NoFilter);
    XQ_CHECK(xfdTreeHasRow(view, "sub"));
    XQ_CHECK(xfdTreeHasRow(view, "a.txt"));

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
    remove("/tmp/xfd_filter_probe/a.txt");
    remove("/tmp/xfd_filter_probe/sub");
    remove("/tmp/xfd_filter_probe");
}

/* ==================== P3-44 sidebarUrls 驱动导航窗格 =================== */

XQ_TEST(XFileDialog, sidebarUrlsDriveNavPane)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* urls;
    XListView* nav;
    XAbstractItemModel* model;
    XString* want;
    XQ_REQUIRE(dlg != NULL);

    urls = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(urls != NULL);
    XStringList_push_back_utf8(urls, "/tmp");
    XStringList_push_back_utf8(urls, "/var");
    XFileDialog_setSidebarUrls(dlg, urls);
    XClassDelete((XClass*)urls);

    XWidget_show((XWidget*)dlg);
    nav = (XListView*)xfdChild(dlg, "qt_file_dialog_sidebar");
    XQ_REQUIRE(nav != NULL);
    model = XAbstractItemView_model((XAbstractItemView*)nav);
    XQ_REQUIRE(model != NULL);
    /* 条目整体来自 m_sidebarUrls（2 条），不再是固定内置表（≥6 条）。 */
    XQ_EQ_I64(XAbstractItemModel_rowCount(model), 2);
    /* 条目名取路径末段（路径串承载的 basename 约定）。 */
    XQ_EQ_STR(xfdCStr(XAbstractItemModel_data_2(model, 0, 0)), "tmp");
    XQ_EQ_STR(xfdCStr(XAbstractItemModel_data_2(model, 1, 0)), "var");

    /* 点击第 2 行 ⇒ 跳到 /var（行→路径映射与装配同源，点第 i 行不会
     * 跳到别的目录）。 */
    XAbstractItemView_clicked_signal((XAbstractItemView*)nav, 1, 0);
    want = XFileDialog_directory(dlg);
    XQ_CHECK(want && strcmp(xfdCStr(XString_toUtf8(want)), "/var") == 0);
    if (want) XClassDelete((XClass*)want);

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}
#endif /* XFILE_ON && XDIR_ON */

/* ==================== P3-45 history 驱动地址下拉 ====================== */

#if XFILE_ON && XDIR_ON
XQ_TEST(XFileDialog, historyPopulatesAddressCombo)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* hist;
    XComboBox* combo;
    XString* dir;
    XStringList* got;
    XQ_REQUIRE(dlg != NULL);

    hist = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(hist != NULL);
    XStringList_push_back_utf8(hist, "/tmp");
    XStringList_push_back_utf8(hist, "/var");
    XFileDialog_setHistory(dlg, hist);
    XClassDelete((XClass*)hist);
    dir = XString_create_utf8("/etc");
    XQ_REQUIRE(dir != NULL);
    XFileDialog_setDirectory(dlg, dir);
    XClassDelete((XClass*)dir);

    XWidget_show((XWidget*)dlg);
    combo = (XComboBox*)xfdChild(dlg, "qt_file_dialog_dir_combo");
    XQ_REQUIRE(combo != NULL);
    /* 历史逐项入下拉 + 当前目录置顶选中（3 条：/etc + /tmp + /var）。 */
    XQ_EQ_I64(XComboBox_count(combo), 3);
    XQ_CHECK(XComboBox_findText_2(combo, "/tmp") >= 0);
    XQ_CHECK(XComboBox_findText_2(combo, "/var") >= 0);
    XQ_CHECK(strcmp(xfdCStr(XComboBox_currentText_2(combo)), "/etc") == 0);

    /* 本对话框 history 为 per-dialog（不接全局后退栈）：换目录后
     * history() 随地址下拉内容更新。 */
    dir = XString_create_utf8("/tmp");
    XQ_REQUIRE(dir != NULL);
    XFileDialog_setDirectory(dlg, dir);
    XClassDelete((XClass*)dir);
    got = XFileDialog_history(dlg);
    XQ_CHECK(got != NULL && xfdCount(got) == 2);
    if (got) XClassDelete((XClass*)got);

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}
#endif /* XFILE_ON && XDIR_ON */

/* ==================== P3-46 setItemDelegate 接线 ====================== */

#if XFILE_ON && XDIR_ON
XQ_TEST(XFileDialog, itemDelegateWiredToView)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XItemDelegate* delegate = XItemDelegate_create();
    XTreeWidget* view;
    XQ_REQUIRE(dlg != NULL && delegate != NULL);

    /* 装配前登记：getter 回落登记值（未建树）。 */
    XFileDialog_setItemDelegate(dlg, (void*)delegate);
    XQ_CHECK(XFileDialog_itemDelegate(dlg) == (void*)delegate);

    XWidget_show((XWidget*)dlg);
    view = (XTreeWidget*)xfdChild(dlg, "qt_file_dialog_view");
    XQ_REQUIRE(view != NULL);
    /* 装配后接线到视图：视图委托即登记值（参与编辑路径；绘制面不查
     * 委托，见 XAbstractItemView.h 注）。 */
    XQ_CHECK(XAbstractItemView_itemDelegate((XAbstractItemView*)view) ==
             (void*)delegate);
    XQ_CHECK(XFileDialog_itemDelegate(dlg) == (void*)delegate);

    /* 运行期换委托：立即接到视图。 */
    {
        XItemDelegate* d2 = XItemDelegate_create();
        XQ_REQUIRE(d2 != NULL);
        XFileDialog_setItemDelegate(dlg, (void*)d2);
        XQ_CHECK(XAbstractItemView_itemDelegate((XAbstractItemView*)view) ==
                 (void*)d2);
        /* NULL 恢复默认：视图与 getter 同为空。 */
        XFileDialog_setItemDelegate(dlg, NULL);
        XQ_CHECK(XAbstractItemView_itemDelegate((XAbstractItemView*)view) ==
                 NULL);
        XQ_CHECK(XFileDialog_itemDelegate(dlg) == NULL);
        XClassDelete((XClass*)d2);
    }

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
    XClassDelete((XClass*)delegate);
}

/* ==================== P3-47 setLabelText 三角色接线 =================== */

XQ_TEST(XFileDialog, labelTextDrivesControls)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* t;
    XWidget* cancel;
    XWidget* ok;
    XWidget* nameLabel;
    XWidget* typeLabel;
    XQ_REQUIRE(dlg != NULL);

    t = XString_create_utf8("名称:");
    XQ_REQUIRE(t != NULL);
    XFileDialog_setLabelText(dlg, XFileDialogDialogLabel_FileName, t);
    XClassDelete((XClass*)t);
    t = XString_create_utf8("类型:");
    XQ_REQUIRE(t != NULL);
    XFileDialog_setLabelText(dlg, XFileDialogDialogLabel_FileType, t);
    XClassDelete((XClass*)t);
    t = XString_create_utf8("不要");
    XQ_REQUIRE(t != NULL);
    XFileDialog_setLabelText(dlg, XFileDialogDialogLabel_Reject, t);
    XClassDelete((XClass*)t);
    t = XString_create_utf8("好");
    XQ_REQUIRE(t != NULL);
    XFileDialog_setLabelText(dlg, XFileDialogDialogLabel_Accept, t);
    XClassDelete((XClass*)t);

    XWidget_show((XWidget*)dlg);

    /* Reject → 取消钮文本；Accept → 确定钮文本（Accept 为既有接线）。 */
    cancel = xfdChild(dlg, "qt_file_dialog_cancel");
    XQ_REQUIRE(cancel != NULL);
    XQ_CHECK(strcmp(xfdCStr(XString_toUtf8(
                        XAbstractButton_text((XAbstractButton*)cancel))),
                    "不要") == 0);
    ok = xfdChild(dlg, "qt_file_dialog_ok");
    XQ_REQUIRE(ok != NULL);
    XQ_CHECK(strcmp(xfdCStr(XString_toUtf8(
                        XAbstractButton_text((XAbstractButton*)ok))),
                    "好") == 0);

    /* FileName/FileType → 两个底部标签。
     * 口径改用「按 objectName 经 findChild 取回」（与本文件其余命名子
     * 控件同一套路）：旧写法遍历 XObject_children 把每个子对象强转
     * XWidget 再比 XClassGetVtable==XLabel_class_init()，既依赖
     * 「类表单例指针等于实例 vtable」这一不成立的前提（实例 vt 为
     * 每对象一份拷贝），又会把非控件子对象（布局/装饰 XObject）一起
     * 强转读到脏指针 ⇒ 断言恒假/段错误。命名标签在 xff_buildInto
     * 装配时由 xff_setName 登记（XFF_NAME_NAMELABEL/TYPELABEL）。 */
    nameLabel = xfdChild(dlg, "qt_file_dialog_name_label");
    XQ_REQUIRE(nameLabel != NULL);
    XQ_CHECK(strcmp(xfdCStr(XString_toUtf8(
                        XLabel_text((XLabel*)nameLabel))), "名称:") == 0);
    typeLabel = xfdChild(dlg, "qt_file_dialog_type_label");
    XQ_REQUIRE(typeLabel != NULL);
    XQ_CHECK(strcmp(xfdCStr(XString_toUtf8(
                        XLabel_text((XLabel*)typeLabel))), "类型:") == 0);

    /* LookIn 无对应控件（Win10 地址行无「目录:」标签）：仅存取。 */
    t = XString_create_utf8("目录");
    XQ_REQUIRE(t != NULL);
    XFileDialog_setLabelText(dlg, XFileDialogDialogLabel_LookIn, t);
    XClassDelete((XClass*)t);
    XQ_CHECK(xfdTakeEq(XFileDialog_labelText(dlg,
                                             XFileDialogDialogLabel_LookIn),
                       "目录"));

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}
#endif /* XFILE_ON && XDIR_ON */

#endif /* 模块开关 */
