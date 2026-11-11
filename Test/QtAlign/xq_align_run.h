/**
 * @file       xq_align_run.h
 * @brief      Qt 6.8 对齐测试运行器入口（无 main）。
 * @details    正式入口：
 *             - 控制台宿主：`bin/XinYueC_Static --test qt-align`；
 *             - 交互 Test 菜单：「Qt6.8 对齐(Test/QtAlign) → 运行全部对齐用例」。
 *             直接用 argc/argv 调用可传 `--list/--filter/--shard/--log/...`；
 *             菜单路径传 (0, NULL) 即跑全部。
 * @note       实现见 xq_align_run.c；用例文件只许用 XQ_TEST 自注册，禁止自带 main。
 * @author     XinYueC 团队
 */
#ifndef XQ_ALIGN_RUN_H
#define XQ_ALIGN_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行 Test/QtAlign 下自注册的对齐用例。
 * @param argc 参数个数（可为 0）。
 * @param argv 参数数组（可为 NULL）。
 * @return 0 全通过；1 有失败/泄漏；2 参数错误。
 */
int xq_align_run(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif /* XQ_ALIGN_RUN_H */
