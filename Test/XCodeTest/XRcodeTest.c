#include"XCodeTest.h"
#include"XMemory.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XRcode.h"
#include"XByteArray.h"
#include"XPrintf.h"
void XRcodeTest()
{
	XRcode* code=XRcode_create();
	XByteArray* array= XByteArray_create_utf8("测试\n");
	XRcode_encode(code, array,20,0);
	XRcode_print_matrix(code);
	XClassDelete(array);
	XRcode_delete(code);
	XCoreApplication_quit();
}

void XTestMenu_XRcodeTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XRcode(二维码)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XRcodeTest);
	}
}

/* ============================================================================
 *            自动化回归（`--test xcode-qrcode`）：连续编码稳定性
 *
 * [锁什么] 2026-11-16 现场抓到的真缺陷：`set_code_word` 只把数据拷进
 * RS 缓冲、**没清零其后的 ecc 区**，而 `rs_encode_block` 是移位除法
 * （最后一轮会读到 `data[ecc_count]`）⇒ 同一实例第二次编码时把上次的
 * 残留当输入 ⇒ 纠错码字算错 ⇒ **码外观完全正常、尺寸/格式位/码字数全对，
 * 但扫码器解不出**；且首次编码偶然正确（create 时内存为 0）——
 * 「有时能扫有时不能」的现场症状就是这么来的。
 *
 * 三条断言（都只能靠逐字节比对，尺寸/格式位自查抓不到）：
 *   ① 同一实例连续两次同内容 ⇒ 码字 + 矩阵逐字节一致；
 *   ② 中间插一个不同内容的编码再回来 ⇒ 仍与首次一致；
 *   ③ 不同纠错级别交替编码（L↔H、不同版本）⇒ 各级别内部一致。
 * ========================================================================== */

static int qrCompareBuffers(const XByteArray* a, const XByteArray* b,
                            int count)
{
    int i;
    if (!a || !b) return 0;
    if ((int)XByteArray_size_base(a) != (int)XByteArray_size_base(b)) return 0;
    if (count > (int)XByteArray_size_base(a)) count = (int)XByteArray_size_base(a);
    for (i = 0; i < count; ++i) {
        if (XByteArray_at_base(a, i) != XByteArray_at_base(b, i)) return 0;
    }
    return 1;
}

/** @brief 编一次并取码字/矩阵快照（返回值 1=成功）。 */
static int qrSnapshot(XRcode* qr, XByteArray* data, int level,
                      XByteArray** outCw, XByteArray** outMatrix, int* outSize)
{
    const XByteArray* cw;
    const XByteArray* m;
    if (!XRcode_encode_ex(qr, data, 0, 0, level)) return 0;
    cw = XRcode_codeWord(qr);
    m = XRcode_matrix(qr);
    if (!cw || !m) return 0;
    *outCw = XByteArray_create_copy(cw);
    *outMatrix = XByteArray_create_copy(m);
    *outSize = XRcode_size(qr);
    return (*outCw && *outMatrix) ? 1 : 0;
}

int XRcodeTest_run(void)
{
    int fail = 0;
    XRcode* qr;
    XByteArray* textA;
    XByteArray* textB;
    const char* kA = "http://hsf.example.com:8085/";
    const char* kB = "http://10.0.0.7:8085/";

    qr = XRcode_create();
    textA = XByteArray_create_utf8(kA);
    textB = XByteArray_create_utf8(kB);
    if (!qr || !textA || !textB) {
        XPrintf("[XRcode] 初始化失败\n");
        if (qr) XRcode_delete(qr);
        XClassDelete((XClass*)textA);
        XClassDelete((XClass*)textB);
        return 1;
    }

    /* ① 连续两次同内容（同一实例） */
    {
        XByteArray* cw1 = NULL; XByteArray* m1 = NULL; int s1 = 0;
        XByteArray* cw2 = NULL; XByteArray* m2 = NULL; int s2 = 0;
        if (!qrSnapshot(qr, textA, 3, &cw1, &m1, &s1)
                || !qrSnapshot(qr, textA, 3, &cw2, &m2, &s2)) {
            XPrintf("[XRcode] ① 编码失败\n");
            ++fail;
        } else {
            if (s1 != s2 || !qrCompareBuffers(cw1, cw2,
                                             XRcode_codeWordCount(qr))) {
                XPrintf("[XRcode] ① 连续编码码字不一致（%d/%d 字节）\n",
                        s1, s2);
                ++fail;
            }
            if (!qrCompareBuffers(m1, m2, s1 * s1)) {
                XPrintf("[XRcode] ① 连续编码矩阵不一致（v%d）\n",
                        (s1 - 17) / 4);
                ++fail;
            }
        }
        XClassDelete((XClass*)cw1); XClassDelete((XClass*)m1);
        XClassDelete((XClass*)cw2); XClassDelete((XClass*)m2);
    }

    /* ② 中间插短内容（版本 v4→v3）再回来 */
    {
        XByteArray* cw1 = NULL; XByteArray* m1 = NULL; int s1 = 0;
        XByteArray* cwX = NULL; XByteArray* mX = NULL; int sX = 0;
        XByteArray* cw3 = NULL; XByteArray* m3 = NULL; int s3 = 0;
        if (!qrSnapshot(qr, textA, 3, &cw1, &m1, &s1)
                || !qrSnapshot(qr, textB, 3, &cwX, &mX, &sX)
                || !qrSnapshot(qr, textA, 3, &cw3, &m3, &s3)) {
            XPrintf("[XRcode] ② 编码失败\n");
            ++fail;
        } else if (s1 != s3 || sX == s1) {
            XPrintf("[XRcode] ② 版本变化未发生（%d/%d/%d）\n",
                    s1, sX, s3);
            ++fail;
        } else if (!qrCompareBuffers(cw1, cw3, XRcode_codeWordCount(qr))
                   || !qrCompareBuffers(m1, m3, s1 * s1)) {
            XPrintf("[XRcode] ② 跨版本后编码结果漂移（v%d vs v%d）\n",
                    (s1 - 17) / 4, (s3 - 17) / 4);
            ++fail;
        }
        XClassDelete((XClass*)cw1); XClassDelete((XClass*)m1);
        XClassDelete((XClass*)cwX); XClassDelete((XClass*)mX);
        XClassDelete((XClass*)cw3); XClassDelete((XClass*)m3);
    }

    /* ③ 纠错级别交替（L↔H）：同级别内部必须一致 */
    {
        XByteArray* lcw1 = NULL; XByteArray* lm1 = NULL; int ls1 = 0;
        XByteArray* hcw = NULL; XByteArray* hm = NULL; int hs = 0;
        XByteArray* lcw2 = NULL; XByteArray* lm2 = NULL; int ls2 = 0;
        if (!qrSnapshot(qr, textA, 0, &lcw1, &lm1, &ls1)
                || !qrSnapshot(qr, textA, 3, &hcw, &hm, &hs)
                || !qrSnapshot(qr, textA, 0, &lcw2, &lm2, &ls2)) {
            XPrintf("[XRcode] ③ 编码失败\n");
            ++fail;
        } else if (ls1 != ls2 || hs <= ls1) {
            XPrintf("[XRcode] ③ 版本/尺寸异常（L=%d/%d H=%d）\n",
                    ls1, ls2, hs);
            ++fail;
        } else if (!qrCompareBuffers(lcw1, lcw2, XRcode_codeWordCount(qr))
                   || !qrCompareBuffers(lm1, lm2, ls1 * ls1)) {
            XPrintf("[XRcode] ③ L↔H 交替后 L 级结果漂移\n");
            ++fail;
        }
        XClassDelete((XClass*)lcw1); XClassDelete((XClass*)lm1);
        XClassDelete((XClass*)hcw); XClassDelete((XClass*)hm);
        XClassDelete((XClass*)lcw2); XClassDelete((XClass*)lm2);
    }

    XClassDelete((XClass*)textA);
    XClassDelete((XClass*)textB);
    XRcode_delete(qr);
    XPrintf("[XRcode] 回归完成：%s（%d 项失败）\n",
            fail == 0 ? "PASS" : "FAIL", fail);
    return fail;
}