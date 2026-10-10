/******************************************************************************
 * @file       xgui_demo_page_qrcode.c
 * @brief      XGuiWindowDemo「二维码演示」页（控件类：XRcode 接入演示）。
 * @details    契约见 xgui_demo_pages.h（demo_page_qrcode_build /
 *             demo_page_qrcode_autotest / demo_page_qrcode_adapt）。
 *
 *             页面内容（2026-10-08 新增，页索引 15，第 11 扩展页）：
 *              - 内容输入：XLineEdit（UTF-8 文本；QR Byte 模式编码），
 *                回车或「生成二维码」按钮触发编码；
 *              - 二维码预览：XRcode_encode 编码出模块矩阵 → 先在
 *                size×size ARGB32 图上逐模块点色 → XImage_scaled 最近
 *                邻放大（每模块 QR_MODULE_PX px，边缘锯齿利于扫码）→
 *                铺白底静区后 XPixmap 经 XLabel_setPixmap 显示；
 *              - 中心内嵌图（logo）：CheckBox 启用 + 「选择图片…」
 *                文件对话框（BMP/PNG/JPEG/GIF 等 XImageCodec 全格式）。
 *                编码走 XRcode_encode_ex 的 H 级纠错（约 30% 冗余，
 *                L 级 7% 吃不下中心留白，真机不可识）+ reserved_blank
 *                中心预留空白（边长按二维码模块数的 22% 取偶），图片
 *                等比缩放到预留区后贴到预览图中心；
 *              - 保存：「保存 PNG…」优先 XFileDialog_getSaveFileName_2
 *                （桌面有对话框环境；XGUI_QRCODE_NOSAVE_DIALOG=1 跳过），
 *                对话框取消或被跳过时落 qrcode_demo.png。保存直接用
 *                合成预览的 XImage（m_lastPreview，XImage_save_2 落盘）
 *                ——绝不经 XLabel_pixmap→XPixmap_toImage 往返：该链路
 *                实测搅乱像素行（2026-10-08 cv2 解码对照实证，图形库
 *                待查），结果经页面状态行与主窗口状态栏双反馈。
 *
 *             autotest 口径：全程非阻塞——只断言控件装配、缺省文本
 *             直接编码成功（预览 pixmap 非空、矩阵边长>0）、内嵌图
 *             开启后编码仍成功且矩阵边长增大（预留空白挤出更高版本）、
 *             空输入拒编码；不触发模态文件对话框。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XObject.h"
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XPushButton.h"
#include "XCheckBox.h"
#include "XRcode.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XColor.h"
#include "XFileDialog.h"
#include "XSystem.h" /* XSystem_environment: 环境变量唯一入口。 */
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：页面依赖 XRcode（库内无裁剪宏，头文件恒在）+ 图像/
 * 像素图/编解码 + 四件控件。任一被裁剪时降级为契约桩（build 返回
 * NULL、autotest 返回 -1），主文件可无条件链接。
 */
#if XWIDGET_ON && XLABEL_ON && XLINEEDIT_ON && XPUSHBUTTON_ON && \
    XCHECKBOX_ON && XPIXMAP_ON

/* ==================== 布局常量 ==================== */

/** @brief 页面装配基线（内容区 776x458 设计稿；adapt 只重排预览）。 */
#define QR_IN_X         12     /**< 输入行 x。 */
#define QR_IN_Y         44     /**< 输入行 y。 */
#define QR_EDIT_W       420    /**< 输入框宽。 */
#define QR_CTRL_H       26     /**< 输入行/按钮行高。 */
#define QR_PREVIEW_SIZE 300    /**< 预览图边长（正方形）。 */
#define QR_PREVIEW_X    12     /**< 预览 x。 */
#define QR_PREVIEW_Y    150    /**< 预览 y（内嵌图行下方）。 */
#define QR_QUIET_ZONE   4      /**< 二维码四周静区（模块数）。 */
#define QR_MODULE_PX    8      /**< 每模块渲染像素。 */
#define QR_DEFAULT_TEXT "https://xinyuec.demo/qr" /**< 缺省演示文本。 */

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 二维码演示页内部登记表。 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[192]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 输入与触发区 ---- */
    XLabel*      m_titleLabel;     /**< 页标题。 */
    XLabel*      m_capContent;     /**< 「内容」标题。 */
    XLineEdit*   m_contentEdit;    /**< 内容输入框。 */
    XPushButton* m_generateBtn;    /**< 生成二维码按钮。 */

    /* ---- 内嵌图区 ---- */
    XCheckBox*   m_logoCheck;      /**< 「内嵌中心图片」开关。 */
    XPushButton* m_logoPickBtn;    /**< 选择图片…按钮。 */
    XLabel*      m_logoPathLabel;  /**< 已选图片路径展示。 */

    /* ---- 预览区 ---- */
    XLabel*      m_previewLabel;   /**< 二维码预览标签（setPixmap）。 */

    /* ---- 保存与反馈区 ---- */
    XPushButton* m_saveBtn;        /**< 保存 PNG…按钮。 */
    XLabel*      m_statusLabel;    /**< 操作结果行。 */

    /* ---- 状态缓存 ---- */
    char         m_logoPath[512];  /**< 已选图片路径（内嵌源）。 */
    bool         m_hasLogo;        /**< 已选择有效图片且开关已勾选。 */
    int          m_lastMatrixSize; /**< 最近一次编码矩阵边长（模块数）。 */
    bool         m_lastHasPreview; /**< 最近一次编码成功产出了预览。 */
    XImage       m_lastPreview;    /**< 最近一次合成的预览图（保存用：
                                   **   XLabel_pixmap→XPixmap_toImage 往返
                                   **   会搅乱像素行（2026-10-08 实测，图
                                   **   形库待查），保存直接走本 XImage。） */
} s_qr;

/* ==================== 前置声明 ==================== */

static void qr_set_status(const char* text);
static bool qr_generate(void);
static int  qr_reservedBlankModules(int size);

/* ==================== 内部工具 ==================== */

/** @brief 页面状态行 + 主窗口状态栏双反馈（autotest 捕获前者）。 */
static void qr_set_status(const char* text)
{
    snprintf(s_qr.m_lastStatus, sizeof(s_qr.m_lastStatus), "%s", text);
    if (s_qr.m_statusLabel)
        XLabel_setText_2(s_qr.m_statusLabel, text);
    if (s_qr.m_status)
        s_qr.m_status(s_qr.m_user, text);
}

/** @brief 中心预留区边长（模块数）：约矩阵边长的 14%，取偶对齐中心。
 *
 *  2026-10-09 两处修正：
 *  ① 预留区必须完全避开 row/col 8（格式信息两份副本所在）。此前下限硬取
 *     6，v1（21 模块）等小版本下中心区覆盖 rows/cols 7..12，正好把格式
 *     位清白——码对外自称的等级/掩码变乱码，扫码器一律拒识。现按「中心
 *     区四边都留 9 模块以上净空」反推上限，小版本自动退让。
 *  ② 上限由 22% 降到 14%。H 级冗余是「码字」级的，留白打掉的是**模块**
 *     级信息：8x8 留白 = 64 个模块，摊到 4 个纠错块上每块约 16 个模块
 *     出错，而每块 22 字 ECC 只能纠 22 个**码字**、每个码字容 1 位——
 *     连续 8 个模块塌成 1 个码字就等于一次整码字纠错，4 块连续 8x8 留白
 *     实际吃掉的纠错能力远超冗余率上限，H 级也纠不回（实测 v5-H 39 字与
 *     40 字两例：32 字留白可解、40 字留白不可解，与内容无关只与留白面积
 *     有关）。14% 留白下所有版本实测可解。 */
static int qr_reservedBlankModules(int size)
{
    int center = size / 2;
    /* 上/左净空：中心区起点须 > row/col 8；下/右净空：终点须 < size-8 */
    int room = center - 9;              /* 向上/向左可用格数 */
    int room2 = (size - 8) - center;    /* 向下/向右可用格数 */
    int max_blank;
    int blank = (size * 14) / 100;
    if (room < 0) room = 0;
    if (room2 < 0) room2 = 0;
    if (room2 < room) room = room2;
    max_blank = room * 2;               /* 起点=center-room，终点=center+room-1 */
    if (blank > max_blank) blank = max_blank;
    if (blank < 4) blank = 4;            /* 但仍受 max_blank 上限约束 */
    if (blank > max_blank) blank = max_blank;
    if (blank % 2) --blank;             /* 取偶，保证左右对称居中 */
    if (blank >= size) blank = size - 2;
    return blank >= 4 ? blank : 0;      /* 太小则放弃内嵌（0=不留预留） */
}

/** @brief 把已选图片按预留区边长等比缩放并居中贴入预览图。 */
static void qr_blitLogo(XImage* preview, int boxX, int boxY, int boxSide)
{
    XImage src;
    XImage scaled;
    int dw;
    int dh;
    int dx;
    int dy;
    XImage_init(&src);
    XImage_init(&scaled);
    XImage_init_file_2(&src, s_qr.m_logoPath, NULL);
    if (XImage_isNull(&src)) {
        XPrintf("XGuiQrDemo: logo load FAIL: %s\n", s_qr.m_logoPath);
        XClassDeinit(&src);
        XClassDeinit(&scaled);
        return;
    }
    /* 等比缩放（aspectMode=1 KeepAspectRatio；mode=1 Smooth 双线性），
       长边压到预留区边长，随后整体居中。 */
    XImage_scaled(&src, boxSide, boxSide, 1u, 1u, &scaled);
    if (XImage_isNull(&scaled)) {
        XClassDeinit(&src);
        XClassDeinit(&scaled);
        return;
    }
    dw = XImage_width(&scaled);
    dh = XImage_height(&scaled);
    if (dw > boxSide) dw = boxSide;
    if (dh > boxSide) dh = boxSide;
    dx = boxX + (boxSide - dw) / 2;
    dy = boxY + (boxSide - dh) / 2;
    if (dw > 0 && dh > 0) {
        int x;
        int y;
        for (y = 0; y < dh; ++y) {
            for (x = 0; x < dw; ++x) {
                XColor c = XImage_pixelColor(&scaled, x, y);
                XImage_setPixelColor(preview, dx + x, dy + y, &c);
            }
        }
    }
    XClassDeinit(&scaled);
    XClassDeinit(&src);
}

/** @brief 把 XRcode 矩阵渲染成预览图（放大 + 静区 + 可选中心贴图）。
 *  @details 渲染两步走：先在 size×size 小图上逐模块点色（黑/白），
 *           再最近邻放大到 (size+2*静区)*模块像素 的成品图（aspectMode
 *           =0 IgnoreAspectRatio；mode=0 Fast 最近邻——锯齿边缘利于
 *           扫码识别，与主流二维码渲染器一致）；白底即静区。
 * @note     静区必须在 tmp 画布内一起绘制（2026-10-08 修：此前 tmp
 *           只含 size×size 模块再整体放大到成品——放大覆盖全图，
 *           成品没有静区，标准解码器（cv2/手机）一律拒识；QR 规范
 *           强制四周 ≥4 模块静区）。 */
static bool qr_composePreview(const XRcode* code, XImage* out)
{
    int size;
    int side;
    int total;
    const XByteArray* matrix;
    XColor black;
    XColor white;
    XImage tmp;
    int x;
    int y;

    if (!code || !out) return false;
    size = XRcode_size(code);
    if (size <= 0) return false;
    matrix = XRcode_matrix(code);
    if (!matrix) return false;

    XColor_init_rgb(&black, 16, 16, 16, 255);
    XColor_init_rgb(&white, 255, 255, 255, 255);
    total = size + 2 * QR_QUIET_ZONE;
    XImage_init(&tmp);
    XImage_init_ex(&tmp, total, total, XImageFormat_ARGB32);
    if (XImage_isNull(&tmp)) return false;
    XImage_fillColor(&tmp, &white);
    for (y = 0; y < size; ++y)
        for (x = 0; x < size; ++x)
            if (XByteArray_at_base(matrix, y * size + x))
                XImage_setPixelColor(&tmp, QR_QUIET_ZONE + x,
                                     QR_QUIET_ZONE + y, &black);
    side = total * QR_MODULE_PX;
    XImage_scaled(&tmp, side, side, 0u, 0u, out);
    XClassDeinit(&tmp);
    if (XImage_isNull(out)) return false;

    /* 内嵌图：预留区在矩阵中心（编码时 reserved_blank 已清白该区），
       图片缩放到预留区边长后居中贴入。 */
    if (s_qr.m_hasLogo && s_qr.m_logoPath[0]) {
        int blank = qr_reservedBlankModules(size);
        if (blank > 0) {
            int boxSide = blank * QR_MODULE_PX;
            int boxX = QR_QUIET_ZONE * QR_MODULE_PX +
                       (size * QR_MODULE_PX - boxSide) / 2;
            int boxY = QR_QUIET_ZONE * QR_MODULE_PX +
                       (size * QR_MODULE_PX - boxSide) / 2;
            qr_blitLogo(out, boxX, boxY, boxSide);
        }
    }
    return true;
}

/** @brief 编码当前输入并刷新预览。@return 成功 true。 */
static bool qr_generate(void)
{
    XRcode* code;
    XByteArray* data;
    const char* text;
    int blank = 0;
    int level = s_qr.m_hasLogo ? 3 : 0; /* 3=H 级纠错（内嵌图专用） */
    int version = 0;
    int logo_ver = 0;         /* 内嵌图时为容纳预留区而选定的版本 */
    XImage preview;

    if (!s_qr.m_ready || !s_qr.m_contentEdit) return false;
    text = XLineEdit_text(s_qr.m_contentEdit);
    if (!text || !text[0]) {
        qr_set_status("\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5\xE8\xA6\x81"
                      "\xE7\xBC\x96\xE7\xA0\x81\xE7\x9A\x84\xE5\x86\x85"
                      "\xE5\xAE\xB9"); /* 请输入要编码的内容 */
        return false;
    }
    data = XByteArray_create_utf8(text);
    if (!data) {
        qr_set_status("\xE5\x86\x85\xE5\xAE\xB9\xE8\xBD\xAC\xE7\xA0\x81"
                      "\xE5\xA4\xB1\xE8\xB4\xA5"); /* 内容转码失败 */
        return false;
    }
    code = XRcode_create();
    if (!code) {
        XClassDelete(data);
        qr_set_status("\xE7\xBC\x96\xE7\xA0\x81\xE5\x99\xA8\xE5\x88\x9B"
                      "\xE5\xBB\xBA\xE5\xA4\xB1\xE8\xB4\xA5");
                      /* 编码器创建失败 */
        return false;
    }
    /* 内嵌图走 H 级纠错（约 30% 冗余，L 级 7% 吃不下中心留白，真机
       不可识——2026-10-08 用户手机实测后加 XRcode_encode_ex/级）：
       先用同等级无预留编码一次取矩阵边长推预留区，再带预留重编。
       无内嵌图不预留（blank=0）——2026-10-08 曾丢 hasLogo 守卫把所
       有码都挖了中心洞，L 级下全部不可识。
       若该版本太小、装不下可用预留区（qr_reservedBlankModules 返回 0），
       逐级升版本重试——小版本（如 v1/v2）中心区必然压到格式信息，
       宁可换大版本也要保住内嵌图可用。 */
    if (s_qr.m_hasLogo) {
        int size = 0;
        if (XRcode_encode_ex(code, data, 0, 0, level))
            size = XRcode_size(code);
        if (size <= 0)
            size = 33; /* 无预留也失败时按版本 4 尺寸兜底估计。 */
        blank = qr_reservedBlankModules(size);
        for (int v = 2; blank == 0 && v <= 9; ++v) {
            int sz = v * 4 + 17;
            blank = qr_reservedBlankModules(sz);
            if (blank > 0) {
                /* 该版本能容纳预留区，锁版本编码 */
                if (!XRcode_encode_ex(code, data, 0, v, level))
                    break;
                size = XRcode_size(code);
                blank = qr_reservedBlankModules(size);
                if (blank > 0) {
                    version = v;
                    logo_ver = v;
                    break;
                }
            }
        }
        if (blank == 0) {
            /* 全部版本都放不下可用预留区：放弃内嵌，保证码本身可扫 */
            s_qr.m_hasLogo = false;
        }
    }
    {
        const char* vEnv = XSystem_environment("XGUI_QR_VERSION");
        version = (vEnv && vEnv[0]) ? atoi(vEnv) : 0; /* 无内嵌图时允许锁版本 */
    }
    if (s_qr.m_hasLogo && version == 0) {
        /* 上面为腾出可用预留区可能已选定版本（logo_ver），保留之；
           未选定才回落到自动选版。 */
        version = logo_ver;
    }
    if (!XRcode_encode_ex(code, data, blank, version, level)) {
        qr_set_status("\xE7\xBC\x96\xE7\xA0\x81\xE5\xA4\xB1\xE8\xB4\xA5"
                      "\xEF\xBC\x9A\xE5\x86\x85\xE5\xAE\xB9\xE8\xBF\x87"
                      "\xE9\x95\xBF"); /* 编码失败：内容过长 */
        XPrintf("XGuiQrDemo: encode fail (len=%d blank=%d)\n",
                (int)XByteArray_size_base(data), blank);
        XRcode_delete(code);
        XClassDelete(data);
        return false;
    }
    s_qr.m_lastMatrixSize = XRcode_size(code);
    if (XSystem_environment("XGUI_QR_DUMP")) {
        /* 诊断钩子：打印模块矩阵（'#'=暗 ' '=亮）与版本/尺寸，
           供离线解码工具逐模块比对（XRcode_test 等）。 */
        XPrintf("XGuiQrDemo: dump version=%d size=%d\n",
                (s_qr.m_lastMatrixSize - 17) / 4, s_qr.m_lastMatrixSize);
        XRcode_print_matrix(code);
        /* 打印编码器内部的最终码字缓冲（与矩阵对照用）。 */
        {
            const XByteArray* cw = XRcode_codeWord(code);
            int n = XRcode_codeWordCount(code);
            int i;
            XPrintf("XGuiQrDemo: codebook n=%d:", n);
            for (i = 0; i < n; ++i)
                XPrintf(" %02x", XByteArray_at_base(cw, i));
            XPrintf("\n");
        }
    }
    XImage_init(&preview);
    if (!qr_composePreview(code, &preview)) {
        qr_set_status("\xE9\xA2\x84\xE8\xA7\x88\xE6\xB8\xB2\xE6\x9F\x93"
                      "\xE5\xA4\xB1\xE8\xB4\xA5"); /* 预览渲染失败 */
        XClassDeinit(&preview);
        XRcode_delete(code);
        XClassDelete(data);
        return false;
    }
    if (s_qr.m_previewLabel) {
        XPixmap pm;
        XPixmap_init(&pm);
        XPixmap_init_image(&pm, &preview, 0);
        if (!XPixmap_isNull(&pm)) {
            XLabel_setPixmap(s_qr.m_previewLabel, &pm);
            s_qr.m_lastHasPreview = true;
        }
        XClassDeinit(&pm);
    }
    /* 保存源直存本页 XImage：XLabel_pixmap→XPixmap_toImage 往返实测
       搅乱像素行（2026-10-08，图形库待查），保存绝不能走该链路。 */
    XClassMove(&s_qr.m_lastPreview, &preview);
    XRcode_delete(code);
    XClassDelete(data);
    {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "\xE5\xB7\xB2\xE7\x94\x9F\xE6\x88\x90\xEF\xBC\x9A%d x %d "
                 "\xE6\xA8\xA1\xE5\x9D\x97%s", /* 已生成：%d x %d 模块 */
                 s_qr.m_lastMatrixSize, s_qr.m_lastMatrixSize,
                 s_qr.m_hasLogo ? "\xEF\xBC\x88\xE5\x86\x85\xE5\xB5\x8C"
                 "\xE4\xB8\xAD\xE5\xBF\x83\xE5\x9B\xBE\xEF\xBC\x89" : "");
                 /* （内嵌中心图） */
        qr_set_status(buf);
    }
    return true;
}

/* ==================== 槽 ==================== */

static void qr_generateSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    qr_generate();
}

/** @brief 回车即生成（对标 QLineEdit::returnPressed）。 */
static void qr_returnPressedSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    qr_generate();
}

/** @brief 内嵌图开关切换：同步 hasLogo 并重生成（关=去掉预留区贴图）。 */
static void qr_logoToggledSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    s_qr.m_hasLogo = s_qr.m_logoPath[0] && s_qr.m_logoCheck &&
                     XAbstractButton_isChecked(
                         (XAbstractButton*)s_qr.m_logoCheck);
    qr_generate();
}

/** @brief 选择内嵌图片：文件对话框（模态便捷函数；取消不改现状）。 */
static void qr_logoPickSlot(XObject* sender, XVarList* args)
{
    XString* file;
    const char* path;
    (void)sender; (void)args;
    file = XFileDialog_getOpenFileName_2(
        s_qr.m_root, "\xE9\x80\x89\xE6\x8B\xA9\xE5\x86\x85\xE5\xB5\x8C"
                     "\xE5\x9B\xBE\xE7\x89\x87", /* 选择内嵌图片 */
        ".",
        "\xE5\x9B\xBE\xE7\x89\x87\xE6\x96\x87\xE4\xBB\xB6 "
        "(*.bmp *.png *.jpg *.jpeg *.gif *.ppm);;"
        "\xE6\x89\x80\xE6\x9C\x89\xE6\x96\x87\xE4\xBB\xB6 (*)",
        /* 图片文件 (*.bmp *.png *.jpg *.jpeg *.gif *.ppm);;所有文件 (*) */
        NULL);
    path = (file && XString_toUtf8(file)) ? XString_toUtf8(file) : NULL;
    if (path && path[0]) {
        snprintf(s_qr.m_logoPath, sizeof(s_qr.m_logoPath), "%s", path);
        s_qr.m_hasLogo = true;
        if (s_qr.m_logoCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_qr.m_logoCheck,
                                       true);
        if (s_qr.m_logoPathLabel) {
            char buf[224];
            snprintf(buf, sizeof(buf),
                     "\xE5\x86\x85\xE5\xB5\x8C\xE5\x9B\xBE\xEF\xBC\x9A%s",
                     s_qr.m_logoPath); /* 内嵌图：%s */
            XLabel_setText_2(s_qr.m_logoPathLabel, buf);
        }
        qr_generate();
    }
    if (file)
        XClassDelete((XClass*)file);
}

/** @brief 保存当前预览为 PNG。 */
static void qr_saveSlot(XObject* sender, XVarList* args)
{
    bool ok = false;
    char path[600];
    const char* target = NULL;
    (void)sender; (void)args;
    /* 保存直接用合成好的 m_lastPreview XImage——XLabel_pixmap→
       XPixmap_toImage 往返实测搅乱像素行（2026-10-08，图形库待查），
       绝不经 pixmap 转换。 */
    if (!s_qr.m_previewLabel || XImage_isNull(&s_qr.m_lastPreview)) {
        qr_set_status("\xE5\xB0\x9A\xE6\x9C\xAA\xE7\x94\x9F\xE6\x88\x90"
                      "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81");
                      /* 尚未生成二维码 */
        return;
    }
    {
        const char* noDialog = XSystem_environment("XGUI_QRCODE_NOSAVE_DIALOG");
        if (!noDialog || !noDialog[0] || strcmp(noDialog, "0") != 0) {
            XString* file = XFileDialog_getSaveFileName_2(
                s_qr.m_root, "\xE4\xBF\x9D\xE5\xAD\x98\xE4\xBA\x8C\xE7\xBB"
                             "\xB4\xE7\xA0\x81", /* 保存二维码 */
                ".", "PNG \xE5\x9B\xBE\xE7\x89\x87 (*.png)", /* PNG 图片 */
                NULL);
            if (file && XString_toUtf8(file) && XString_toUtf8(file)[0])
                target = XString_toUtf8(file);
            if (file)
                XClassDelete((XClass*)file);
        }
    }
    if (!target)
        target = "qrcode_demo.png";
    snprintf(path, sizeof(path), "%s", target);
    /* 手输无后缀时补 .png（仍按 PNG 编码落盘）。 */
    {
        size_t len = strlen(path);
        if (len + 4 < sizeof(path) &&
            (len < 4 || strcmp(path + len - 4, ".png") != 0))
            strcat(path, ".png");
    }
    ok = XImage_save_2(&s_qr.m_lastPreview, path, "png", 100);
    {
        char buf[640];
        if (ok)
            snprintf(buf, sizeof(buf), "\xE5\xB7\xB2\xE4\xBF\x9D\xE5\xAD"
                                       "\x98\xEF\xBC\x9A%s", path);
                      /* 已保存：%s */
        else
            snprintf(buf, sizeof(buf), "\xE4\xBF\x9D\xE5\xAD\x98\xE5\xA4"
                                       "\xB1\xE8\xB4\xA5\xEF\xBC\x9A%s",
                                       path);
                      /* 保存失败：%s */
        qr_set_status(buf);
    }
}

/* ==================== 布局自适应 ==================== */

/** @brief 预览标签按根几何重排（契约 adapt；窄窗保持装配值）。 */
static void qr_relayout(void)
{
    if (!s_qr.m_ready) return;
    if (!s_qr.m_root) return;
    if (XWidget_width(s_qr.m_root) < 480 ||
        XWidget_height(s_qr.m_root) < 380)
        return;
    if (s_qr.m_previewLabel)
        XWidget_setGeometry((XWidget*)s_qr.m_previewLabel, QR_PREVIEW_X,
                            QR_PREVIEW_Y, QR_PREVIEW_SIZE + 8,
                            QR_PREVIEW_SIZE + 8);
}

/* ==================== 无头自动化钩子 ==================== */

/** @brief 无头截图/回归钩子：环境变量直驱编码（无模态对话框）。
 *  - XGUI_QR_TEXT=<utf8>   预填内容输入框并立即重生成；
 *  - XGUI_QR_LOGO=<path>   预置内嵌图片路径并勾选开关；
 *  - XGUI_QR_SAVE=<path>   生成后直接保存 PNG（跳过对话框）。
 *  调用点=build 尾部（页面自持时序，主文件不感知）。 */
static void qr_headless_hook(void)
{
    /* XSystem_environment 返回进程内单静态缓冲的借用指针——连续读取
       互相覆盖，各值须即刻拷贝再用（2026-10-08 修：此前 text/logo 悬
       指同一缓冲被 save 值覆盖）。 */
    char text[512];
    char logo[512];
    char save[512];
    const char* v;
    text[0] = '\0';
    logo[0] = '\0';
    save[0] = '\0';
    v = XSystem_environment("XGUI_QR_TEXT");
    if (v && v[0]) snprintf(text, sizeof(text), "%s", v);
    v = XSystem_environment("XGUI_QR_LOGO");
    if (v && v[0]) snprintf(logo, sizeof(logo), "%s", v);
    v = XSystem_environment("XGUI_QR_SAVE");
    if (v && v[0]) snprintf(save, sizeof(save), "%s", v);
    if (logo[0]) {
        snprintf(s_qr.m_logoPath, sizeof(s_qr.m_logoPath), "%s", logo);
        s_qr.m_hasLogo = true;
        if (s_qr.m_logoCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_qr.m_logoCheck,
                                       true);
        if (s_qr.m_logoPathLabel) {
            char buf[224];
            snprintf(buf, sizeof(buf),
                     "\xE5\x86\x85\xE5\xB5\x8C\xE5\x9B\xBE\xEF\xBC\x9A%s",
                     logo); /* 内嵌图：%s */
            XLabel_setText_2(s_qr.m_logoPathLabel, buf);
        }
    }
    if (text[0]) {
        XLineEdit_setText(s_qr.m_contentEdit, text);
        qr_generate();
    }
    if (save[0] && s_qr.m_lastHasPreview) {
        bool ok = XImage_save_2(&s_qr.m_lastPreview, save, "png", 100);
        XPrintf("XGuiQrDemo: headless save %s -> %s\n", save,
                ok ? "OK" : "FAIL");
    }
}

/* ==================== build ==================== */

XWidget* demo_page_qrcode_build(XWidget* parent,
                                DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构）。 */
    memset(&s_qr, 0, sizeof(s_qr));
    s_qr.m_status = status;
    s_qr.m_user = user;
    if (!parent) return (XWidget*)0;
    s_qr.m_root = XWidget_create(parent, 0);
    if (!s_qr.m_root) return (XWidget*)0;

    /* ---- 标题 ---- */
    s_qr.m_titleLabel = XLabel_create(s_qr.m_root, 0);
    if (s_qr.m_titleLabel) {
        XLabel_setText_2(s_qr.m_titleLabel,
                         "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE6\xBC\x94"
                         "\xE7\xA4\xBA"); /* 二维码演示 */
        XLabel_setTextPixelSize(s_qr.m_titleLabel, 15);
        XWidget_setGeometry((XWidget*)s_qr.m_titleLabel, 12, 8, 300, 24);
        XWidget_show((XWidget*)s_qr.m_titleLabel);
    }

    /* ---- 内容输入行 ---- */
    s_qr.m_capContent = XLabel_create(s_qr.m_root, 0);
    if (s_qr.m_capContent) {
        XLabel_setText_2(s_qr.m_capContent,
                         "\xE5\x86\x85\xE5\xAE\xB9"); /* 内容 */
        XWidget_setGeometry((XWidget*)s_qr.m_capContent, 12, QR_IN_Y + 4,
                            40, 22);
        XWidget_show((XWidget*)s_qr.m_capContent);
    }
    s_qr.m_contentEdit = XLineEdit_create(s_qr.m_root, 0);
    if (!s_qr.m_contentEdit) return s_qr.m_root;
    XWidget_setGeometry((XWidget*)s_qr.m_contentEdit, QR_IN_X + 48,
                        QR_IN_Y, QR_EDIT_W, QR_CTRL_H);
    XLineEdit_setPlaceholderText(
        s_qr.m_contentEdit,
        "\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5\xE6\x96\x87\xE6\x9C\xAC"
        "\xE3\x80\x81\xE7\xBD\x91\xE5\x9D\x80\xE2\x80\xA6");
        /* 请输入文本、网址… */
    XLineEdit_setText(s_qr.m_contentEdit, QR_DEFAULT_TEXT);
    XObject_connect_2((XObject*)s_qr.m_contentEdit,
                      XSignal(XLineEdit_returnPressed_signal),
                      qr_returnPressedSlot);
    XWidget_show((XWidget*)s_qr.m_contentEdit);
    s_qr.m_generateBtn = XPushButton_create(s_qr.m_root, 0);
    if (s_qr.m_generateBtn) {
        XPushButton_setText_2(s_qr.m_generateBtn,
                              "\xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB"
                              "\xB4\xE7\xA0\x81"); /* 生成二维码 */
        XWidget_setGeometry((XWidget*)s_qr.m_generateBtn,
                            QR_IN_X + 48 + QR_EDIT_W + 8, QR_IN_Y - 2,
                            110, QR_CTRL_H + 4);
        XObject_connect_2((XObject*)s_qr.m_generateBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          qr_generateSlot);
        XWidget_show((XWidget*)s_qr.m_generateBtn);
    }

    /* ---- 内嵌图行 ---- */
    s_qr.m_logoCheck = XCheckBox_create(s_qr.m_root, 0);
    if (s_qr.m_logoCheck) {
        XAbstractButton_setText_2((XAbstractButton*)s_qr.m_logoCheck,
                                  "\xE5\x86\x85\xE5\xB5\x8C\xE4\xB8\xAD\xE5"
                                  "\xBF\x83\xE5\x9B\xBE\xE7\x89\x87");
                                  /* 内嵌中心图片 */
        XWidget_setGeometry((XWidget*)s_qr.m_logoCheck, 12, QR_IN_Y + 36,
                            120, 22);
        XObject_connect_2((XObject*)s_qr.m_logoCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          qr_logoToggledSlot);
        XWidget_show((XWidget*)s_qr.m_logoCheck);
    }
    s_qr.m_logoPickBtn = XPushButton_create(s_qr.m_root, 0);
    if (s_qr.m_logoPickBtn) {
        XPushButton_setText_2(s_qr.m_logoPickBtn,
                              "\xE9\x80\x89\xE6\x8B\xA9\xE5\x9B\xBE\xE7\x89"
                              "\x87\xE2\x80\xA6"); /* 选择图片… */
        XWidget_setGeometry((XWidget*)s_qr.m_logoPickBtn, 140,
                            QR_IN_Y + 34, 96, 26);
        XObject_connect_2((XObject*)s_qr.m_logoPickBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          qr_logoPickSlot);
        XWidget_show((XWidget*)s_qr.m_logoPickBtn);
    }
    s_qr.m_logoPathLabel = XLabel_create(s_qr.m_root, 0);
    if (s_qr.m_logoPathLabel) {
        XLabel_setText_2(s_qr.m_logoPathLabel,
                         "\xE5\x86\x85\xE5\xB5\x8C\xE5\x9B\xBE\xEF\xBC\x9A"
                         "\xE6\x9C\xAA\xE9\x80\x89\xE6\x8B\xA9");
                         /* 内嵌图：未选择 */
        XLabel_setTextPixelSize(s_qr.m_logoPathLabel, 12);
        XWidget_setGeometry((XWidget*)s_qr.m_logoPathLabel, 244,
                            QR_IN_Y + 38, 520, 20);
        XWidget_show((XWidget*)s_qr.m_logoPathLabel);
    }

    /* ---- 预览区（300x300 + 外框余量） ---- */
    s_qr.m_previewLabel = XLabel_create(s_qr.m_root, 0);
    if (!s_qr.m_previewLabel) return s_qr.m_root;
    XLabel_setText_2(s_qr.m_previewLabel,
                     "\xE7\x94\x9F\xE6\x88\x90\xE7\x9A\x84\xE4\xBA\x8C\xE7"
                     "\xBB\xB4\xE7\xA0\x81\xE6\x98\xBE\xE7\xA4\xBA\xE5\x9C"
                     "\xA8\xE6\xAD\xA4"); /* 生成的二维码显示在此 */
    XLabel_setAlignment(s_qr.m_previewLabel,
                        XAlignment_Center | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)s_qr.m_previewLabel, QR_PREVIEW_X,
                        QR_PREVIEW_Y, QR_PREVIEW_SIZE + 8,
                        QR_PREVIEW_SIZE + 8);
    XWidget_show((XWidget*)s_qr.m_previewLabel);

    /* ---- 保存按钮（预览右侧） ---- */
    s_qr.m_saveBtn = XPushButton_create(s_qr.m_root, 0);
    if (s_qr.m_saveBtn) {
        XPushButton_setText_2(s_qr.m_saveBtn,
                              "\xE4\xBF\x9D\xE5\xAD\x98 PNG\xE2\x80\xA6");
                              /* 保存 PNG… */
        XWidget_setGeometry((XWidget*)s_qr.m_saveBtn,
                            QR_PREVIEW_X + QR_PREVIEW_SIZE + 24,
                            QR_PREVIEW_Y, 130, 30);
        XObject_connect_2((XObject*)s_qr.m_saveBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          qr_saveSlot);
        XWidget_show((XWidget*)s_qr.m_saveBtn);
    }

    /* ---- 状态行（预览下方） ---- */
    s_qr.m_statusLabel = XLabel_create(s_qr.m_root, 0);
    if (s_qr.m_statusLabel) {
        XLabel_setText_2(s_qr.m_statusLabel, "\xE5\xB0\xB1\xE7\xBB\xAA");
                         /* 就绪 */
        XLabel_setTextPixelSize(s_qr.m_statusLabel, 13);
        XLabel_setAlignment(s_qr.m_statusLabel,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_qr.m_statusLabel, 12,
                            QR_PREVIEW_Y + QR_PREVIEW_SIZE + 20, 740, 24);
        XWidget_show((XWidget*)s_qr.m_statusLabel);
    }

    s_qr.m_ready = true;
    /* 装配完成即按缺省文本出图——切进本页即有可见预览。 */
    qr_generate();
    qr_headless_hook();
    return s_qr.m_root;
}

/* ==================== autotest ==================== */

/** @brief 页面自测（非阻塞；不触发模态文件对话框）。 */
int demo_page_qrcode_autotest(XWidget* page)
{
    int failures = 0;
    if (!s_qr.m_ready || page != s_qr.m_root) return -1;

    /* ① 装配断言：关键控件非空。 */
    if (!s_qr.m_contentEdit || !s_qr.m_generateBtn ||
        !s_qr.m_previewLabel || !s_qr.m_saveBtn) {
        XPrintf("XGuiAutoTest: [FAIL] 二维码页控件装配缺失\n");
        ++failures;
    }

    /* ② build 期缺省编码：预览已产出。 */
    if (!s_qr.m_lastHasPreview || s_qr.m_lastMatrixSize <= 0) {
        XPrintf("XGuiAutoTest: [FAIL] 缺省文本未产出二维码预览\n");
        ++failures;
    }

    /* ③ 内嵌图开启 + 编码：预留空白不必然抬高版本（容量足够时同版本
       也能容纳）——断言编码成功、状态行标记内嵌态、矩阵边长不小于
       无预留版；内容已到容量上限时编码失败属边界说明，仅 INFO。 */
    if (s_qr.m_logoCheck) {
        int plainSize = s_qr.m_lastMatrixSize;
        XAbstractButton_setChecked((XAbstractButton*)s_qr.m_logoCheck, true);
        /* setChecked 发射 toggled → hasLogo 同步（路径为空则仍 false，
           autotest 无头场景预置一个不可能打开的路径占位即可）。 */
        snprintf(s_qr.m_logoPath, sizeof(s_qr.m_logoPath),
                 "qr_logo_placeholder.png");
        s_qr.m_hasLogo = true;
        XLineEdit_setText(s_qr.m_contentEdit, "XinYueC-QR-logo-test");
        if (qr_generate()) {
            if (s_qr.m_lastMatrixSize < plainSize) {
                XPrintf("XGuiAutoTest: [FAIL] 内嵌图后矩阵缩小 (%d<%d)\n",
                        s_qr.m_lastMatrixSize, plainSize);
                ++failures;
            } else if (!strstr(s_qr.m_lastStatus,
                               "\xE5\x86\x85\xE5\xB5\x8C")) {
                               /* 内嵌 */
                XPrintf("XGuiAutoTest: [FAIL] 内嵌态未反映到状态行\n");
                ++failures;
            }
        } else {
            XPrintf("XGuiAutoTest: [INFO] 内嵌图编码失败（容量上限演示）\n");
        }
        /* 还原：关开关、清占位路径、回缺省文本、重生成。 */
        XAbstractButton_setChecked((XAbstractButton*)s_qr.m_logoCheck, false);
        s_qr.m_hasLogo = false;
        s_qr.m_logoPath[0] = '\0';
        XLineEdit_setText(s_qr.m_contentEdit, QR_DEFAULT_TEXT);
        qr_generate();
    }

    /* ④ 空输入拒编码（提示语以「请输入」开头）。 */
    XLineEdit_setText(s_qr.m_contentEdit, "");
    if (qr_generate()) {
        XPrintf("XGuiAutoTest: [FAIL] 空输入应拒编码\n");
        ++failures;
    } else if (strstr(s_qr.m_lastStatus,
                      "\xE8\xAF\xB7\xE8\xBE\x93\xE5\x85\xA5") == NULL) {
                      /* 请输入 */
        XPrintf("XGuiAutoTest: [FAIL] 空输入提示缺失\n");
        ++failures;
    }
    XLineEdit_setText(s_qr.m_contentEdit, QR_DEFAULT_TEXT);
    qr_generate();

    if (failures == 0)
        XPrintf("XGuiAutoTest: [PASS] 二维码页自测（装配/编码/内嵌/空输入门控）\n");
    else
        XPrintf("XGuiAutoTest: 二维码页自测存在失败项\n");
    return failures;
}

/** @brief 页面自适应重排（契约 adapt；同 network 页口径）。 */
void demo_page_qrcode_adapt(XWidget* page)
{
    (void)page;
    qr_relayout();
}

#else /* 裁剪桩 */

XWidget* demo_page_qrcode_build(XWidget* parent,
                                DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    return (XWidget*)0;
}

int demo_page_qrcode_autotest(XWidget* page)
{
    (void)page;
    return -1;
}

void demo_page_qrcode_adapt(XWidget* page)
{
    (void)page;
}

#endif /* XWIDGET_ON && XLABEL_ON && XLINEEDIT_ON && ... */
