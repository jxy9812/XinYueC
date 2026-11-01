# ==================== XGui 头目录导出（构建期包含路径清单）====================
# 本文件是 **XGui 模块自持**的头目录导出清单，单一真值源，被两处 include：
#
#   ① 根 CMakeLists.txt —— 在 add_subdirectory(Drive) 之前 include 并落地到
#      根作用域（include_directories）。落地时点被快照语义锁死：目录级
#      INCLUDE_DIRECTORIES 是 add_subdirectory 时刻的快照，先注册的目标拿
#      不到后落的根清单（2026-11-01 Src 曾因快照语义 C1083 实证）。
#
#   ② Src/XGui/CMakeLists.txt —— 模块自身 target_include_directories(XinYueGui
#      PUBLIC ...) 与根共用同一份清单，避免两处各写一份路径而漂移。
#
# ---- 为什么 XGui 头目录需要落到根作用域（而非只走链接图）============
# 框架的全局强制包含链（根函数 xinyue_force_include_config → -FI
# CXinYueConfig.h）会把配置头拉进**每一个**目标，包括不链接 XinYueGui 的
# Drive/Library/* 目标（zlib/mbedtls/pcre2/...）。该链为：
#
#   XMemory.h → CXinYueConfig.h → "XGui/XGuiConfig.h"（经上级 Src 根解析，
#   XCore 模块抬升的 /.. 项已在列）→ XGuiConfig.h:1061 →
#   "Graphics/XImageCodec/XImageCodec_config.h"（相对 XGuiConfig.h 自身目录
#   解析）→ XImageCodec_config.h:44 → "XGuiConfig.h"（**裸名**）
#
# 裸名 "XGuiConfig.h" 只能由 XGui 模块目录（Src/XGui，XGuiConfig.h 所在处）
# 解析。而 2026-11-01 模块化拆分把 XGui 迁入 Src/ 后，XGui 头目录改为
# 「不抬升、走链接图传递」——那只对**消费 XinYueGui 的目标**成立；上面那条
# 链上的 Drive/Library/* 目标既不链接 XinYueGui，也不在 XGui 模块内
# （它们先于 add_subdirectory(Src/XGui) 注册），于是裸名解析失败：
#   fatal error: XGuiConfig.h: 没有那个文件或目录
#   （实测于 Library/zlib 的 gzwrite.c/gzread.c/gzlib.c 三个 TU）
#
# XGui 模块自身排在 Library/* 之后（它要 target_link_libraries(XinYueGui
# PUBLIC zlib)，zlib 目标须先存在），其 PARENT_SCOPE 抬升来不及给先注册
# 的目标 —— 故改为「模块提供导出清单 + 根在消费者注册前提前落地」，既守住
# XGui 头目录不全局扩散（只暴露 Src/XGui 顶层：XAlignment.h / XGuiConfig.h，
# 无同名遮蔽），又让裸名包含链完整闭合。
#
# ---- 清单内容 ============================================================
# 模块根目录 + 全部叶子目录（Style/、Widget/、Graphics/... 等）。
#
# 根目录（XGuiConfig.h）必需：裸名 "XGuiConfig.h" 的解析点，见上文链路。
#
# 叶子目录必需：XGui 的控件头在库外被裸名包含（Test/XGuiTest/
# XButtonGroupTest.c → "XButtonGroup.h"、XComboBoxTest.c → "XComboBox.h"），
# 而消费目标 XinYueC_Dynamic 链的是**共享库** ${EXPORT_NAME}——共享库对
# XGuiLayer 是 PRIVATE 链接（不能改 PUBLIC：会把未导出的 XinYueGui 带进
# install(EXPORT) 导出集，破坏安装包生成），故共享库的消费者拿不到
# XinYueGui 的 PUBLIC 头目录。落地为全局可见即恢复拆分前「Src 全子树
# 全局可见」的口径（拆分前 FIND_INCLUDE_DIR(INCLUDE_SRC_LIST "Src") 把
# Src/XGui 全部子目录都收进根包含路径）。
#
# 命名遮蔽核查：XGui 全部 240 个头文件与 Src/XCore、Drive、Test、
# Src/Industry、Library 各目录**无同名**（同名即全局可见时后者会被遮蔽），
# 故全局落地无遮蔽风险。
#
# 采集用 FIND_INCLUDE_DIR（根 include(CMake_Config.cmake) 提供的宏，
# 递归收集 ${curdir} 下全部含 .h/.hpp 的目录）；以绝对路径为 curdir，
# 使根与模块两处 include 得到的清单完全一致（同一真值源）。
FIND_INCLUDE_DIR(XGUI_HEADER_EXPORT_DIRS "${CMAKE_CURRENT_LIST_DIR}")
# 模块根目录本身必须在内（FIND_INCLUDE_DIR 只收「含头的目录」，模块根
# 恰好含 XGuiConfig.h/XAlignment.h 已在其中；此处显式追加以防目录结构调整
# 后根目录不含头时被漏收）。
list(APPEND XGUI_HEADER_EXPORT_DIRS "${CMAKE_CURRENT_LIST_DIR}")
list(REMOVE_DUPLICATES XGUI_HEADER_EXPORT_DIRS)
