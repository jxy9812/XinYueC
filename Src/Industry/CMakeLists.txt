# ==================== Industry（工业设备层）====================
# 2026-11-01 迁入 Src/ 与 XCore/XGui 平级（框架扩展层与 GUI 同级的模块布局；
# HighSpeedFilter 为自带 main 的模板/演示项目，仍留仓库根）。通道接口
# （XChannel：XIoChannel + DI/DO/AI/AO 四接口 + 传输适配器）与
# 工业设备（XSolenoidValve 电磁阀等）以子目录静态库方式集成（与 Library/
# 同款规矩），并挂进主库组合（Drive 模块对 XinYueCS/XinYueC 预挂本库，
# XGui 同款；动态库 PRIVATE 编入 .so）。
# 依赖方向：XinYueIndustry → XinYueC 主库（XObject/XMemory 等符号），环依
# 赖反向边见文末（CMake 静态库环依赖算法按需重复两库，与"链接行双列主库"
# 同一 idiom）。
#
# [2026-11-01 收编] test/ 两个自带 main 的独立可执行已迁
# Test/XIndustryTest/ 去 main 收编进主程序（--test xindustry-valve /
# xindustry-modbus + 测试菜单「工业」域，XModbusHubTest 同款），本模块
# 回归纯库目标。

# 收集源文件（模块只含自己的头/源；测试已在 Test 模块）
file(GLOB_RECURSE INDUSTRY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(REMOVE_DUPLICATES INDUSTRY_FILE)

# 创建静态库
add_library(XinYueIndustry STATIC ${INDUSTRY_FILE})
# POSITION_INDEPENDENT_CODE：本库随 XinYueC 动态库（PRIVATE 挂接）编入
# .so，XGui/XCore 模块同款要求。
set_target_properties(XinYueIndustry PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 传播头文件路径（XChannel/XSolenoidValve 子目录供裸文件名 include）
target_include_directories(XinYueIndustry PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/XChannel"
    "${CMAKE_CURRENT_SOURCE_DIR}/XSolenoidValve")

# ---- 主库反向边（环依赖） ----
# 本库 TU 引用主库符号（XObject/XMemory 等）：挂 XinYueCS 让 CMake 在消费
# 端链接行把两库按依赖序排布/按需重复（彼时 XinYueCS 已由 Drive 模块创建，
# 跨目录改目标经 CMP0079 NEW，见根 CMakeLists）。
target_link_libraries(XinYueIndustry PUBLIC ${EXPORT_NAME}S)
