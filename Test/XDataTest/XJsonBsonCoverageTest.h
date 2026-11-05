/**
 * @file       XJsonBsonCoverageTest.h
 * @brief      XJson / XBson 全 API 组合与所有权回归测试入口。
 * @details    自动化测试：`--test xjson-bson`。覆盖 XJsonValue / XJsonObject /
 *             XJsonArray / XJsonDocument 与 XBsonValue / XBsonDocument /
 *             XBsonArray 的全部公开 API 组合，并对每个场景做堆在用字节数
 *             回基线校验（所有权/泄漏回归）。返回失败项数。
 */
#ifndef XJSONBSONCOVERAGETEST_H
#define XJSONBSONCOVERAGETEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 执行 XJson/XBson 全量覆盖测试。@return 失败项数（0=全绿）。 */
int XJsonBsonCoverageTest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* XJSONBSONCOVERAGETEST_H */
