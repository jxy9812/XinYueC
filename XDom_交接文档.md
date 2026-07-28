# XDom XML DOM 模块交接文档

更新时间：2026-07-28  
工作目录：`D:\code\CMake\XinYueC`

## 1. 目标与约束

本模块位于 `Src/XData/XDom`，目标是以 C API 对齐 Qt 6.8 的 `QDomDocument`、`QDomNode`、`QDomElement`、`QDomAttr`、`QDomText`、`QDomCDATASection`、`QDomComment`、`QDomDocumentType`、`QDomNodeList`、`QDomNamedNodeMap`、`QDomImplementation`、`QDomEntity`、`QDomNotation`，并补充完整 `QDomNode` 所需的文档片段、实体引用、处理指令及字符数据句柄。

必须遵守：

- 不引入 Qt、Win32、POSIX、文件系统 API 或其他平台 API。XML 实现只能依赖 `XString`、`XByteArray`、`XXmlStreamReader`、`XMemory`、`XClass` 等现有抽象。
- 字符串内部使用 `XString` 的 UTF-16 表示；`XDomCharacterData` 的长度和偏移按 UTF-16 代码单元计算；字节输入输出使用 UTF-8。
- 公开 API 保持 Qt 名称主体；C 的重载使用项目约定后缀，例如 `_utf8`、`_result`。
- 不实现 QtCore5Compat 的旧 SAX 兼容层：`QXmlAttributes`、`QXmlSimpleReader`、`QXmlReader`、
  `QXmlInputSource` 及各类 SAX Handler 均不在当前 XML 模块范围内。属性流式访问统一使用
  已有的 `XXmlStreamAttribute` / `XXmlStreamAttributes`（对应 `QXmlStreamAttribute` /
  `QXmlStreamAttributes`）。
- 所有 `copy`、`move` 虚函数和私有辅助函数必须先检查源、目标和目标 vtable。目标未初始化时先 `Type_init()`，源未初始化安全返回，`dest == src` 直接返回。DOM 的复制语义是 Qt 风格浅拷贝；深拷贝使用 `cloneNode()`。
- 公共头文件必须包含中文 Doxygen 注释：`@brief`、`@details`、每个参数的 `@param`、返回值的 `@return`、所有权和生命周期说明的 `@note`。该格式已经写入根目录的代码风格文档。

## 2. 当前文件状态

| 路径 | 状态 | 说明 |
|---|---|---|
| `Src/XData/XDom/XDom.h` | 已创建，544 行 | 中央公开头文件，包含所有类型、枚举和 API 声明。前半部分注释较完整，后半部分 API 声明仍需逐函数补充详细中文注释。 |
| `Src/XData/XDom/XDom.c` | 已创建，约 3179 行 | 单文件私有实现，已能编译；见下文的已实现和风险说明。 |
| `Src/XData/XDom/XDom*.h` | 已创建但不完整 | 每个类头文件目前只是包含 `XDom.h` 的薄转发头，缺少文件注释和该类 API 的说明。 |
| `Test/XDataTest/XDomTest.c/.h` | 未创建 | 尚无 DOM 自动测试。 |
| `Test/XDataTest/XDataTest.c` | 未修改 | 尚未注册 DOM 菜单。 |

`Src/XData/XDom/` 目前还是未跟踪目录。工作树同时存在许多与 XML 无关的用户修改，继续工作时不要还原或覆盖它们。

## 3. 已完成实现

### 3.1 内部模型

`XDomNodePrivate` 使用引用计数保存节点数据、父子关系、属性、DTD 实体和符号声明。所有公共对象是轻量句柄，结构第一个成员为 `XClass`，第二个成员持有共享私有节点。

已经实现：

- 句柄的创建、浅拷贝、移动、析构和空句柄。
- 子节点插入、替换、移除、追加、文档片段展开、深克隆、标准文本规范化。
- 元素属性、命名空间属性、属性节点、`NamedNodeMap`、元素后代查询和文档节点工厂。
- 文本、CDATA、注释的字符数据修改 API；`splitText()` 会为 CDATA 创建普通文本节点，这与 Qt 行为一致。
- DTD 名称、publicId、systemId、internalSubset、实体和符号映射的基本承载。
- 通过已有 `XXmlStreamReader` 解析 `StartDocument`、DTD、元素、属性、文本、CDATA、注释、处理指令、实体引用。
- `toString(indent)` / `toByteArray(indent)` 的基础序列化。
- `QDomNode::isText()` 已调整为普通文本和 CDATA 都返回真；`toText()` 同样接受 CDATA。

### 3.2 已验证

Windows 下执行过：

```powershell
cmake --build build --config Debug --target XinYueC_Static -j 5
```

结果：成功生成 `bin\Debug\XinYueC_Static.exe`，退出码为 `0`。构建输出含项目既有的大量警告；本次没有阻断编译的 DOM 错误。

注意：这只证明编译和链接通过，不代表 DOM 行为已经测试通过。

## 4. 未完成的公开 API

下列声明还没有对应实现。继续前应先实现它们，否则任何引用这些 API 的测试或业务代码都会链接失败。

### 4.1 `XDomImplementation`

- `XDomImplementation_hasFeature`
- `XDomImplementation_hasFeature_utf8`
- `XDomImplementation_createDocumentType`
- `XDomImplementation_createDocumentType_utf8`
- `XDomImplementation_createDocument`
- `XDomImplementation_createDocument_utf8`
- `XDomImplementation_isNull`
- `XDomImplementation_invalidDataPolicy`
- `XDomImplementation_setInvalidDataPolicy`

推荐行为：`hasFeature("XML", "")` 和 `hasFeature("XML", "1.0")` 返回真；`createDocument()` 创建文档、可选插入 doctype、创建并挂接根元素；无效名称返回空文档或空句柄；InvalidDataPolicy 为进程级状态，文档必须注明它不是并发可重入设置。

### 4.2 类型到 Node 的转换

以下 `toNode()` 函数尚未实现：

- `XDomElement_toNode`
- `XDomAttr_toNode`
- `XDomText_toNode`
- `XDomCDATASection_toNode`
- `XDomComment_toNode`
- `XDomDocument_toNode`
- `XDomDocumentType_toNode`
- `XDomDocumentFragment_toNode`
- `XDomEntity_toNode`
- `XDomNotation_toNode`

实现方式应统一为创建 `XDomNode` 空句柄后，对底层 `m_impl` 增加引用。不得复制节点树。

### 4.3 仍需补充的 Qt 对齐 API

当前公开头只覆盖了一部分 Qt 6.8 API。建议补齐并评审是否需要公开：

- `setContent(XString)`、`setContent(XIODevice*)` 和从现有 Reader 继续构建的版本；接口名按现有 C 后缀规范设计。
- `QDomDocument::setContent` 的所有解析选项、错误位置和字符串输入编码语义。
- `QDomNode::save` 的设备/流抽象版本（若项目确实需要）；不能使用 `FILE` 或平台 API。
- `QDomDocument` 的 `createEntityReference`、`QDomEntityReference` 的最终公开行为和测试。
- 是否需要支持 XML 声明的 `standalone="no"`：当前私有节点只保存布尔真值，不能区分“显式 no”和“未声明”。

## 5. 已知问题和高优先级风险

这些问题应该在添加测试前或测试驱动下优先修正。

1. `XDomNodeList` 当前是创建时的快照，不是 Qt 的 live list。Qt 要求树发生增删后，已经取得的 `childNodes()`、`elementsByTagName()` 和 `elementsByTagNameNS()` 列表重新查询时能反映更新。需要将列表私有数据改为“源节点 + 查询过滤条件 + 文档版本号”，而不是复制节点数组。

2. `XDomNamedNodeMap_contains()` 和 `_utf8()` 通过创建临时 `XDomNode` 查询，未释放临时句柄，存在泄漏。应直接调用 `xxml_dom_map_find_name()`，或在查询后销毁包装对象。

3. 文档节点尚未限制“最多一个根元素”和“最多一个 doctype”。`xxml_dom_node_child_allowed()` 只限制了节点种类，未检查现有子节点数量。插入和解析都应拒绝第二个根元素，并保持失败操作不会破坏原树。

4. CDATA 序列化目前错误地调用通用文本转义，CDATA 内容中的 `&`、`<` 会被改写。必须保留原内容，并将每个 `]]>` 拆成 `]]]]><![CDATA[>` 后再输出。

5. DTD 的 internal subset 提取采用简单的 `strchr('[')` / `strrchr(']')`。当 DTD 注释、实体值或其他语法中包含方括号时会错误。应使用状态机扫描引号、注释、内部子集嵌套语义，或让 `XXmlStreamReader` 暴露可靠的原始 internal subset。

6. `XDomDocument_setContent_result()` 已接入 Reader，但未做运行时测试。须覆盖 Reader 当前 XML 声明、UTF-8/UTF-16、错误行列、DTD 实体、命名空间、空白节点和实体引用的实际 token 行为。

7. 解析器无法从 Reader 公开 API 区分“没有 XML 声明”和“Reader 默认给出 version=1.0”，现以原始字节开头是否 `<?xml` 作为临时判断。此方式未覆盖 UTF-16 BOM/XML 声明。更好的做法是在 `XXmlStreamReader` 公开 `hasXmlDeclaration()` 和错误位置快照。

8. `XDom` 普通工厂节点和解析关闭命名空间处理时已改为清空 localName/prefix/namespaceURI；但所有调用路径、属性重命名和 `setPrefix()` 还需要测试，防止 DOM Level 1 与 Namespace API 混用时产生不一致状态。

9. `XDomNodeType` 缺少 Qt 的 `BaseNode = 21` 枚举值；当前只有 `UnknownNode = 0` 和 `CharacterDataNode = 22`。应补回 API 对齐枚举值并写明其不是 XML 实体节点。

10. `normalize()` 当前递归合并普通文本且不合并 CDATA，遵循 Qt 文档/DOM 直觉；Qt 6.8 实现本身有“只处理当前层且把 CDATA 当 text”的差异。需要明确项目选择文档语义还是逐源码兼容，并以测试锁定。

11. 单个公共头转发文件只有四行，违反本项目“详细中文头文件注释”的新约束。要么为每个文件补充该类型摘要、所有权和包含关系注释，要么把各类型声明从中央头分拆到相应头文件，同时保持 `XDom.h` 作为总入口。

12. 没有 DOM 专用测试，因此内存引用计数、失败路径、树重新挂接和序列化都未经过回归验证。建议在 Linux 运行 ASan/UBSan 或 valgrind（若项目构建方式允许）辅助查泄漏和悬挂引用。

## 6. 测试与菜单计划

新增：

```text
Test/XDataTest/XDomTest.h
Test/XDataTest/XDomTest.c
```

建议测试函数采用已有风格：

```c
bool XDomTest_runAll(void);
void XMenu_XDomTest(XMenu* root);
```

在 `Test/XDataTest/XDataTest.c` 中包含 `XDomTest.h`，并在 `XMenu_XDataTest()` 中调用：

```c
XMenu_XDomTest(menu);
```

菜单项目和输出必须使用中文。不要把 `runAll` 放入不相关的测试菜单；保持 Reader 和 Writer 当前各自的菜单注册方式。

首批测试至少覆盖：

- 句柄浅拷贝可见性、`cloneNode(true/false)` 独立性、移动后源对象为空但可析构。
- 对未初始化的目标调用 `copy_base` / `move_base`，以及源未初始化、源目标同一对象的安全行为。
- 根元素、父子重挂接、片段展开、错误引用节点、多个根元素的拒绝。
- 属性、属性节点、普通/命名空间属性、命名节点映射替换和移除。
- `NodeList` 实时更新行为和越界/负索引空节点。
- UTF-16 代码单元长度、`substringData`、超范围 `insertData` 补空格、`splitText` 的 0/末尾/越界/负数/无父节点/CDATA 情况。
- 文本、CDATA、注释、PI、实体引用的类型判断、转换与序列化；特别验证 CDATA `]]>`。
- `setContent` 的成功、错误信息、行列、默认丢弃纯空白、`PreserveSpacingOnlyNodes`、命名空间开关、UTF-8 与 UTF-16。
- DTD public/system/internal subset、实体、notation，以及序列化后再解析。
- `QDomImplementation` 全部 API。

## 7. Linux 继续步骤

建议从干净构建目录开始，但不要删除用户已有 `build-linux` 内容，除非先确认其中没有需要保留的产物。

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target XinYueC_Static -j5
./bin/XinYueC_Static
```

若 Linux 输出目录由生成器配置为不同路径，先用：

```bash
find build-linux -type f -name 'XinYueC_Static*'
```

定位可执行文件。完成 `XDomTest` 菜单注册后，通过中文菜单运行 DOM 测试；也应保留现有 `--list`、`--test` 命令行测试入口的一致性。

Linux 首次构建建议增加：

```bash
cmake -S . -B build-linux-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-linux-asan --target XinYueC_Static -j5
```

若第三方库或已有代码不兼容 sanitizer，不要为 DOM 临时绕过平台抽象；先记录现有全局构建问题，再用最小 DOM 测试程序做验证。

## 8. 建议执行顺序

1. 实现第 4 节列出的 API，先完成 `Implementation` 与所有 `toNode()`，然后重新构建。
2. 修复第 5 节的 CDATA、map 泄漏、文档根元素约束和 NodeList 实时语义。
3. 将公共头文件注释补齐，并在代码风格文档中核对注释格式是否继续满足要求。
4. 新建 DOM 测试和中文菜单注册，先以手工建树与序列化测试固定内存/所有权语义。
5. 再以 `XXmlStreamReader` 驱动的解析测试固定命名空间、编码、DTD 和错误位置行为。
6. 在 Linux Debug 与 sanitizer 构建下通过完整 DOM 测试后，再判断 Reader/Writer 是否需要为 DOM 暴露补充 API，例如 `hasXmlDeclaration()`。

## 9. 与现有 Reader/Writer 的关系

`XXmlStreamReader` 和 `XXmlStreamWriter` 已有较大未提交修改和测试改动，且在 `Test/XDataTest/XDataTest.c` 中已有独立中文菜单项。DOM 解析必须复用 Reader，不应重写 XML 词法分析器，也不能直接调用平台 API。

在修改 Reader 时，优先考虑向 DOM 提供以下稳定信息：

- XML 声明是否实际存在；
- 错误发生时锁存的行号和列号；
- 完整且不丢失方括号语义的 DTD internal subset；
- UTF-8、UTF-16 输入的统一字节解码结果。

这些补充应连同 Reader 回归测试一起完成，避免 DOM 通过访问 Reader 私有状态或依赖当前偶然行为。
