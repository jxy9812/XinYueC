# ==================== FreeType（字体光栅化库，阶段二全 provider 集合）====================
# 上游 Qt 捆绑版的完整 CMakeLists 保留为 CMakeLists_upstream_qt.txt 供参照；
# 阶段一最小集合（ftsystem/ftinit/ftdebug/ftbase/smooth）按阶段二设计
# out/xfont_ft_phase2/design.md §8 P2.1 扩容：+7 个字体/提示模块单对象
# （truetype/cff/sfnt/psaux/psnames/pshinter/autofit）+ ftgzip/ftbitmap。
# ftgzip/ftbitmap 是链接期必缺，必须预列：ftoption.h 缺省开 USE_ZLIB
# （:198）与 TT_CONFIG_OPTION_EMBEDDED_BITMAPS（:572）——sfnt 单对象无条件
# 并入 sfwoff.c（sfnt.c:24，:358 调 FT_Gzip_Uncompress，实现在 gzip/ftgzip.c）
# 与 ttsbit.c（sfnt.c:37，无 #ifdef 地调 FT_Bitmap_Convert/Init/Done，实现在
# base/ftbitmap.c）；两符号均不在 ftbase.c 合流清单，也不在其余现编单对象里。
# 同理补 base/ftmm.c：ttobjs.c:792 与 cffobjs.c:699 在缺省开的多母版/命名
# 实例选项下调 FT_Set_Named_Instance（定义于 ftmm.c:611，亦不在 ftbase.c
# 合流清单——烟测链接实证，静态档 make 不解析符号故只有链接期才炸）。
# ftbase.c/smooth.c 等仍为单对象合流文件。PNG/BROTLI 维持 ftoption.h 缺省
# 注释关闭（:276/:309），不引入 libpng/brotli 依赖。

# --- 公开头文件路径（传播给链接方）---
# 生成目录排在真实 include/ 之前：捆绑的 include/freetype/config/ftmodule.h
# 注册了全部模块（含未编译的字体驱动，链接必缺符号），故在构建目录生成一份
# 只含平滑渲染器的最小 ftmodule.h，让 <freetype/config/ftmodule.h> 先命中它。
set(FREETYPE_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

# 生成模块注册表：只挂本次实编 8 模块（1 渲染器 + 7 字体/提示模块），
# 类名/类型与相对次序逐一对照上游全量注册表
# include/freetype/config/ftmodule.h:16-29 抄录（未编译的 t1/cid/pfr/t42/
# winfnt/pcf/bdf 驱动与 raster1/sdf 渲染器一条不列，列了即链接缺符号）
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/freetype/config/ftmodule.h"
"FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )

/* EOF */
")

# 源文件显式列出：阶段一 5 单对象 + 阶段二 9 单对象 + ftmm.c（链接期实证
# 缺符号，依赖依据见文件头注；设计 §8 P2.1 要求链接期即验，不靠"缺了再补"兜底）
set(FREETYPE_SRCS
    src/base/ftsystem.c
    src/base/ftinit.c
    src/base/ftdebug.c
    src/base/ftbase.c
    src/base/ftbitmap.c
    src/base/ftmm.c
    src/gzip/ftgzip.c
    src/smooth/smooth.c
    src/truetype/truetype.c
    src/cff/cff.c
    src/sfnt/sfnt.c
    src/psaux/psaux.c
    src/psnames/psnames.c
    src/pshinter/pshinter.c
    src/autofit/autofit.c
)

# 创建静态库
add_library(freetype STATIC ${FREETYPE_SRCS})
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 库内构建模式：fterrors.h 无此宏会 #undef FT_ERR_PREFIX，FT_THROW 全线炸
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)

# 传播头文件路径
# BEFORE：根工程经 fontconfig 检测全局注入了 -I/usr/include/freetype2（系统
# FreeType），目录级 include 先于 target 级追加——不前置的话 <ft2build.h> 命中
# 系统旧头，与捆绑内部头混编直接炸 FT_ERR_PREFIX。
target_include_directories(freetype BEFORE PUBLIC ${FREETYPE_PUBLIC_INCS})
