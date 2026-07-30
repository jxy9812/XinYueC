# XDom XML DOM 与 XML 流模块交接文档

更新时间：2026-07-30
工作目录：`/home/xinyue/Code/XinYueC`

## 1. 目标与约束

`Src/XData/XDom` 与 `Src/XData/XXmlStream` 以 C API 对齐本机 Qt 6.8.3 的
`QDomDocument`、`QDomNode`、
`QDomElement`、`QDomAttr`、`QDomText`、`QDomCDATASection`、`QDomComment`、
`QDomDocumentType`、`QDomNodeList`、`QDomNamedNodeMap`、`QDomImplementation`、
`QDomEntity`、`QDomNotation`、`QDomEntityReference`、`QDomDocumentFragment`、
`QDomProcessingInstruction`、`QDomCharacterData`，以及
`QXmlStreamAttribute`、`QXmlStreamAttributes`、`QXmlStreamNamespaceDeclaration`、
`QXmlStreamNamespaceDeclarations`、`QXmlStreamNotationDeclaration`、
`QXmlStreamNotationDeclarations`、`QXmlStreamEntityDeclaration`、
`QXmlStreamEntityDeclarations`、`QXmlStreamEntityResolver`、`QXmlStreamReader` 和
`QXmlStreamWriter`。

必须继续遵守：

- 不引入 Qt、Win32、POSIX、`FILE` 或其他平台 API；XML 只通过项目既有的
  `XString`、`XByteArray`、`XIODevice`、`XXmlStreamReader`、`XMemory`、`XClass` 抽象工作。
- 字符串内部使用 `XString` 的 UTF-16 表示；字符数据的偏移和长度均按 UTF-16 代码单元计算。
- 保持 Qt 名称主体；C 重载使用项目后缀，例如 `_utf8`、`_result`。
- 复制、移动和析构遵循项目的 `XClass`/vtable 约定；DOM 句柄为浅拷贝，深拷贝由
  `cloneNode()` 明确请求。
- 公共头文件保留中文 Doxygen 注释，说明参数、返回值、所有权和生命周期。

## 2. 当前完成状态

| 路径 | 状态 | 说明 |
|---|---|---|
| `Src/XData/XDom/XDom.h` | 已完成 | 中央公开入口，包含类型、枚举、生命周期、解析和序列化 API。 |
| `Src/XData/XDom/XDom.c` | 已完成 | DOM 私有模型、节点树、解析、DTD、序列化和全部公开实现。 |
| `Src/XData/XDom/` | 已完成 | DOM 模块已收敛为唯一公共头 `XDom.h` 和唯一实现 `XDom.c`，不再保留各类型转发头。 |
| `Test/XDataTest/XDomTest.c/.h` | 已完成 | 中文自动化测试和菜单入口。 |
| `Test/XDataTest/XDataTest.c` | 已完成 | 已注册 `XDomTest` 子菜单。 |
| `Src/XData/XXmlStream/XXmlStreamReader.*` | 已完成 | XML 流值类型、声明集合、实体解析器和读取器 API。 |
| `Src/XData/XXmlStream/XXmlStreamWriter.*` | 已完成 | XML 流文档、元素、属性、文本、DTD 和设备输出 API。 |
| `Test/XDataTest/XXmlStreamReaderTest.c` | 已完成 | XML 流读取器、值类型、构造重载和集合 API 中文测试。 |
| `Test/XDataTest/XXmlStreamWriterTest.c` | 已完成 | XML 流写入器、设备/外部缓冲目标和 Qt 边界行为中文测试。 |

## 3. 已完成的 Qt 6.8 对齐

### 3.1 公开 API 与符号

- `XDomImplementation` 的能力查询、文档类型创建、文档创建、空句柄判断和全局
  `InvalidDataPolicy` 已实现。
- 所有具体句柄到 `XDomNode` 的转换均为共享私有节点的浅包装，不复制树。
- `XDomNode`、`XDomNodeList`、`XDomNamedNodeMap`、文档工厂、属性、命名空间、
  字符数据、实体、符号、处理指令、文档片段和实体引用 API 已实现。
- `XDom_BaseNode = 21` 与 `XDom_CharacterDataNode = 22` 已按 Qt 枚举补齐。
- `setContent` 支持 `XByteArray`、UTF-8、`XString`、已有 `XXmlStreamReader` 和
  `XIODevice` 输入；旧式错误输出参数与 `XDomParseResult` 接口均可用。
- 公开头文件共 255 个 `XDom*` 函数声明；对当前 `libXinYueCSd.a` 做符号审计，
  缺失定义数量为 0。

### 3.2 行为与边界

- 节点插入、替换、删除、同父节点移动、文档片段展开、深克隆、导入和浅拷贝已实现。
- 文档最多保留一个根元素和一个文档类型；重复 DTD、重复根元素均被拒绝且不破坏原树。
- `XDomNodeList`、`elementsByTagName()` 和 `elementsByTagNameNS()` 为实时查询列表。
- `NamedNodeMap` 支持名称/命名空间查询与只读实体、notation 映射；`contains()` 无临时句柄泄漏。
- 属性 `specified`、属性子文本同步、`setTagName()`、命名空间前缀、属性排序与自动
  `xmlns` 补全已按 Qt 行为固定。
- 文本、CDATA、注释和处理指令的字符数据 API、UTF-16 索引、`splitText()` 和
  `normalize()` 已实现；`normalize()` 只处理当前层的相邻 Text/CDATA 节点。
- DTD 的名称、publicId、systemId、internal subset、外部实体、内部实体、notation 和
  实体引用已覆盖；CDATA 序列化保留原始数据并安全拆分 `]]>`。
- 序列化支持缩进、紧凑输出、`EncodingFromDocument` 与 `EncodingFromTextStream`；
  `save()` 仅写入 `XIODevice`。
- 已按 Qt 6.8.3 源码修正属性节点追加、`setPrefix()`、`setTagName()`、浮点属性格式、
  字符数据越界插入和 `readElementText()` 非开始元素返回值；这些行为均有回归测试。

### 3.3 Reader 配合项

- `XXmlStreamReader_hasXmlDeclaration()` 已公开，用于可靠区分“没有 XML 声明”与
  Reader 的默认版本值。
- XML 声明接受标准的 `version + encoding + standalone` 顺序，保证由 XExcel 保存的
  `xl/workbook.xml` 能被重新读取。
- XML 流测试中的局部 `XString` 均已配对析构；完整地址检测不再报告该路径泄漏。

### 3.4 QXmlStream 公共类对齐

- `QXmlStreamAttribute`、`QXmlStreamNamespaceDeclaration`、
  `QXmlStreamNotationDeclaration` 和 `QXmlStreamEntityDeclaration` 的字段、生命周期、
  访问器及等价判断均已提供 C API 映射。
- `QXmlStreamAttributes`、`QXmlStreamNamespaceDeclarations`、
  `QXmlStreamNotationDeclarations` 和 `QXmlStreamEntityDeclarations` 均支持创建、销毁、
  `size`/`count`、`isEmpty`、索引访问和顺序等价判断。
- `QXmlStreamReader` 已覆盖增量输入、设备输入、令牌遍历、命名空间、DTD、实体、错误、
  行列偏移、XML 声明和当前令牌读取；`QXmlStreamWriter` 已覆盖文档、元素、属性、文本、
  CDATA、注释、处理指令、实体引用、DTD、命名空间、格式化、设备和当前令牌写出。
- Reader 已提供字节数组、UTF-16 `XString`、UTF-8 字符串和 `XIODevice` 构造入口；Writer 已
  提供 `XByteArray`、`XString` 和 `XIODevice` 输出入口。属性、命名空间、Notation、Entity
  集合已提供深拷贝、追加、插入、删除和清空接口；Reader 返回的集合视图仍为借用值，需用
  `_copy` 接口取得独立所有权。
- C API 用显式的 `equals`、`count` 和 `isEmpty` 函数映射 Qt 的运算符与容器便捷接口；
  读取器和写入器用 `create`/`setDevice`/`addData` 等函数映射 Qt 的构造函数重载。
- `QXmlUtils`、`qdom_p.h` 和 `qxmlstream_p.h` 是 Qt 私有实现，不属于公共类迁移范围。

### 3.6 DOM 文件布局

- 所有 XDom 类型、枚举、生命周期声明和公共 API 均集中在
  `Src/XData/XDom/XDom.h`。
- 所有 DOM 私有模型、节点操作、解析、DTD 和序列化实现均集中在
  `Src/XData/XDom/XDom.c`。
- 测试代码只包含 `XDom.h`；删除的类型专用头文件不再作为公共兼容入口维护。

### 3.5 公共头文件注释规范

- `XDom` 和 `XXmlStream` 公共头文件统一使用文件说明在 include guard 之前、UTF-8 BOM
  在文件首部的格式；类型、枚举、结构体字段和公共函数均保留中文 Doxygen 说明。
- 每个公共函数均使用独立的多行 `/** ... */` 注释块；`@param` 名称与声明完全一致，
  `@return` 说明返回对象的所有权、NULL 语义和释放方式，void 函数也明确写出无返回值。
- 注释明确区分 UTF-8 字节输入、UTF-16 字符串及 UTF-16 代码单元索引，并标注对应的
  Qt 6.8 API、借用参数、复制行为、失败时对象状态和设备所有权限制。

## 4. 测试与验证

`XDomTest_runAll()` 已注册到中文菜单：`测试代码 -> XDataTest -> XDomTest -> 全部测试`；
`XXmlStreamReaderTest_runAll()` 已注册到：`测试代码 -> XDataTest -> XXmlStreamReaderTest -> 全部测试`。
测试覆盖以下类别：

- `XDomImplementation`、所有具体节点到 Node 的转换、默认文档类型构造。
- 节点树操作、根元素/DTD 限制、实时列表、克隆、导入、片段、句柄复制和移动。
- 属性、命名空间、字符数据、CDATA 序列化、实体、DTD、错误行列和内容回读。
- `XString`、Reader、设备输入、XML 声明、UTF-8 声明策略以及失败路径。
- XML 流值类型等价规则，以及四类声明集合的 `size`/`count`/`isEmpty`/`equals`。
- Qt 剩余差异回归，包括属性 `specified`、命名映射、内部/外部实体、重复 DTD 和
  命名空间序列化。

已完成的验证记录：

1. 普通构建成功：`cmake --build build_nosan --target XinYueC_Static -j5`。
2. XData 中文菜单入口已完成连续回归；XChar、XXmlStreamWriter、XXmlStreamReader、XDom 和
   XExcel 扩展流程均无失败。
3. Writer 修复了 `writeEmptyElement()` 后结束父元素时的元素栈收口；生成的 Styles、Drawing、
   Relationships 和 DocProps XML 不再发生错误嵌套。Reader 工作表属性查询同时支持限定名 `r:id`，
   外部超链接关系可以恢复真实 URL；XChart 独立 XML 保存了位置、偏移和尺寸元数据。
4. `XDocPropsCore_saveToXmlData()` 与 `XDocPropsApp_saveToXmlData()` 返回的 XML 数据增加 NUL
   终止字节，`outLen` 仍只表示 XML 有效字节数，避免文本调用方越界读取。
5. 普通 XExcel 完整流程连续运行三轮，均为 `200 项断言，失败 0`；覆盖图表、DrawingAnchor、
   工作表保护/起始页、样式字体/颜色/边框/对齐、外部超链接关系、Document 单元格样式、设备和
   图片包往返。
6. 普通核心菜单回归通过：XChar `12 通过, 0 失败`，XXmlStreamWriter `28 通过, 0 失败`，
   XXmlStreamReader 和 XDom 均无失败；Reader 覆盖新增构造、UTF-16/Latin1/ASCII 输入、拆分 BOM、
   配置保留、位置、集合深拷贝和 `readElementText()` 回归。
7. 目标 sanitizer 菜单中，XExcel 完整流程、XXmlStreamWriter、XXmlStreamReader 和 XDom 均有
   退出码 `0` 且未报告地址错误或 LeakSanitizer 泄漏；重复 sanitizer 启动期间出现过无报告的
   `DEADLYSIGNAL` 瞬时进程异常，因此该现象作为环境/工程级残余风险保留，不把单次 sanitizer
   结果扩大为整个仓库的内存安全证明。
8. 完整工程 ASan 菜单仍受当前工作区未跟踪的 `XNetworkAccessManager.c`
   在新 sanitizer 配置中缺少 `xhttp_manager_shared_http2_detach` 链接定义，使用临时测试桩
   后完整菜单又进入 `AddressSanitizer:DEADLYSIGNAL` 信号输出循环；该问题不在 XML 文件或
   XML 测试栈中，不能将此环境结果表述为完整工程最新 LSan 证明。
9. `git diff --check` 通过；XDom 头文件的 255 个公开函数和 XXmlStream 各公共类型的声明
   均能在静态库中找到定义，缺失数量为 0。

## 5. 当前未完成项

Qt 6.8.3 公共 API 的本轮源码、声明、符号和普通运行审计未发现缺少的 XDom 或 XXmlStream
公开函数。集合返回值使用 `_copy` 明确表达 Qt 值语义，原有 Reader 访问器继续保留借用视图
以兼容现有 XExcel 调用方。

XML 核心 Reader、Writer、XDom 和 XExcel 目标菜单的 sanitizer 证据已经形成；仍未形成的是包含网络、GUI、
协议栈和 XExcel 的完整工程 sanitizer 证据。该全工程路径受 `XNetworkAccessManager.c` 的
未跟踪实现链接桩和既有工程规模影响，不能用 XML 核心菜单结果替代。

后续只有以下非阻断性维护事项：

- 若升级 Qt 版本，应重新以新版本 `qdom.h`/`qdom.cpp` 复核 API 和行为差异。
- 新增业务场景时，将其最小回归用例加入 `XDomTest`，并重新运行普通构建和 sanitizer 菜单。
- 工作区内的 `Src/XCode/XNetwork/XHttp/` 与网络测试文件属于其他正在开发的模块，
  不属于 XDom 交接范围。

## 6. 交接结论

XDom 与 XXmlStream 的本轮 Qt 6.8.3 公共 API 对齐、XExcel 扩展回归和目标 sanitizer 验证已完成；
完整工程 sanitizer 仍是独立事项，不能据此写成“整个仓库全部通过”。后续应保留本文件第 4 节的
构建、菜单和 sanitizer 验证标准，并在网络模块链接桩和工程级 sanitizer 稳定性问题消除后更新
最终全量结论。
