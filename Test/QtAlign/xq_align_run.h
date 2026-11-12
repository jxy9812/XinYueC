/**
 * @file       xq_align_run.h
 * @brief      Qt 6.8 对齐测试运行器入口（无 main）。
 * @details    正式入口：
 *             - 控制台宿主：`bin/XinYueC_Static --test qt-align`；
 *             - 交互 Test 菜单：「Qt6.8 对齐(Test/QtAlign) → 运行全部对齐用例」。
 *             直接用 argc/argv 调用可传 `--list/--filter/--shard/--log/...`；
 *             菜单路径传 (0, NULL) 即跑全部。
 *             [2026-11-12 透传已接线（a4d9af05 合并后整改）] 控制台宿主
 *             `--test qt-align`（或 -t qt-align，含 --test=qt-align/-t=qt-align
 *             =号形式）检出后，main() 预扫把 runner 专属选项（--list/--all/
 *             --filter=/--shard=/--log=/--leak-tolerance=，runner 自 argv[1] 起
 *             解析，argv[0]=程序名占位）收进子 argv 经本函数 (argc, argv)
 *             透传，并从喂宿主 parser 的清单剔除——这些旋钮不再是死参数，
 *             对齐测试失败可用 `--test qt-align --filter=Tag` 秒级隔离。
 *             非 qt-align 语境下宿主 -l/--list 语义零变化；runner 侧解析
 *             （xq_align_run.c）不感知透传来源，直接调用路径行为不变。
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
