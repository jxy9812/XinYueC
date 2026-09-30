# VirtualKeyboard — 拼音词组库（V3 双格式：txt 源 + bin 运行资产）

全键拼音 IME 的词组候选打底词库，与 `Src/XGui/VirtualKeyboard/XPinyinTable.c`
的单字候选表（V1）配套。文件：

| 文件 | 说明 |
| --- | --- |
| `phrases_zh.txt` | 词库**源格式**：UTF-8（带 BOM）、LF 换行、按拼音音节序列字典序存放；维护入口——改词先改 txt 再重编 bin |
| `phrases_zh.bin` | 词库**运行资产**（V3）：XIPB 二进制格式，由 `Tools/VirtualKeyboard/ime_phrases_compile.py` 从 txt 编译产出，加载器默认加载（快路径，免逐行解析） |
| `test/phrases_test_fixture.txt` / `.bin` | 回归夹具（txt=坏行解析夹具；bin=XIPB 好件夹具，恰 2 条） |
| `test/phrases_test_fixture_badcrc.bin` | 坏件：词串区翻转 1 字节 → bin 步⑥ CRC 拒绝 |
| `test/phrases_test_fixture_badver.bin` | 坏件：version=2 → bin 步③ 版本拒绝 |
| `test/phrases_test_fixture_trunc.bin` | 坏件：截断至头+1 条目（40B）→ bin 步⑤ 体积上界 56>40 拒绝 |
| `test/phrases_test_fixture_badmagic.bin` | 坏件：magic='XIPX' → 魔数不符被嗅探走文本路径全坏负缓存（覆盖文本路径，非 bin 拒绝样本） |
| `README.md` | 本说明：格式、口径、来源局限与扩展建议 |

## XIPB 二进制格式（V3）

全文件小端、无 BOM。头 24 字节 + 条目区（16 字节/条）+ 词串区：

```
偏移 0   magic[4]     'XIPB'
偏移 4   version u16  =1（加载器拒绝一切非 1）
偏移 6   syllableCount u16   音节表条数（=k_imeSyllables，现 412）
偏移 8   entryCount u32      条目数
偏移 12  wordsLen u32        词串区字节数
偏移 16  syllableFingerprint u32  音节表指纹（FNV-1a64 低 32 位）
偏移 20  crc32 u32           CRC-32/ISO-HDLC（zlib 口径），范围=[24, 文件尾)
```

- 条目 16 字节：音节 id u16×4（count<4 补 0）+ 有效数 u8（2..4）+
  组内频序 u8（1..255）+ 保留 u8×2（写 0，载入校验=0）+ 词偏移 u32。
- 词串区：UTF-8 原字节，词间恰一个 0x00；词 NUL 由下一偏移-1 或区尾-1
  承担；首词偏移恒 0、词偏移严格递增。
- 音节指纹：FNV-1a 64 位，输入=按 id 序排列的全部音节 ASCII 串以单个
  0x00 连接、无尾 NUL，取低 32 位（现 412 音节表=0xCA78F865）。
- 载入顺序校验：①raw≥24 ②magic ③version==1 ④音节条数与指纹与运行期
  音节表现算一致 ⑤体积上界 ⑤′体积恒等式 raw==24+count*16+wordsLen
  ⑥CRC ⑦建索引 ⑧O(n) 逐条 walk 复检六规则的 bin 等价物 ⑨落位。任一
  步失败=整体拒绝+负缓存粘滞，**绝不降级读同名 txt**。

### ⚠ 音节表任何变更（增删/重排）须重编 bin

bin 冻结了编译期的音节 id；上游 `k_imeSyllables` 增删音节会被条数校验
拦下、同条数重排会被指纹校验拦下——这是防线不是缺陷，重编 bin 即恢复：

```
python Tools/VirtualKeyboard/ime_phrases_compile.py Library/VirtualKeyboard/phrases_zh.txt Library/VirtualKeyboard/phrases_zh.bin --verify
```

## 行格式（txt 源）

每行一词，字段以 TAB 分隔：

```
拼音音节序列(空格分隔) <TAB> 汉字词 <TAB> 组内频序整数
```

示例（节选）：

```
ni hao	你好	1
wo men	我们	1
shi jian	时间	1
shi shi	事实	1
shi shi	实施	2
zuo wei	作为	1
zuo wei	座位	2
```

- 音节为**无调**拼音，小写。ü 键位口径与单字表一致：ü 写作 `v`
  （`lv/nv/lue/nue`），j/q/x 后的 ü 按正词法写作 `u`（`ju/qu/xu` 系）。
- `组内频序整数`：同一拼音序列组内从 1 递增，按大致常用度升序——口径同
  `XPinyinTable.c` 的 `m_rank`（组内频序）。**当前为模型知识近似排序，
  非语料统计**，跨组的绝对频度不可比。
- `#` 起始为注释行；除注释外每行都是数据行，无空行。
- 排序键是**音节元组**的字典序（逐音节比较），文件整体非降，可用
  lower_bound 二分定位到组首；同组内按频序升序。

## 规模（2026-09-29 实测统计）

- 数据行 3143 条（双字 2815 + 三字 328），不同拼音序列组 3030 个。
- 使用合法音节 372/412；未用 40 个均为无常用词的罕用/叹词语节。

## 与虚拟键盘框架的关系（2026-09-30）

「虚拟键盘 Qt 形态框架化」（Src/XGui/VirtualKeyboard/）落地后，本
目录的资产与管线**零改动**，消费关系调整如下（如实声明，与设计评审
apiMapping#13 一致）：

- `XPinyinEngine.c/.h`（组串状态机）本体零改动：实例改由拼音输入法插件
  （XVirtualKeyboardPinyinInputMethod）内嵌持有（Qt 插件自持状态口径），
  键盘面板不再直连 `m_ime` 成员——面板经 InputContext/Engine 消费候选
  （wordCandidateListModel）与组串显示（preeditText 镜像）。
- 词库文件格式、加载契约（`XPinyinPhrase_setPath/reload` 懒加载+
  负缓存粘滞）与 `Tools/VirtualKeyboard` 编译管线不受影响；词组加载触发点随面板
  `setImeEnabled` 薄委托语义保留（便捷 API 委托引擎装插件）。
- 回归锚不变：`Test/XGuiTest/XKeyboardTest.c` ⑥ 系列直连状态机用例
  （模块级、纯状态机）原样保留；面板集成断言改读 context.preeditText /
  候选模型（语义不变、路径更换，双世界门控随迁）；黄金摘要断言（⑥e
  txt/bin 双侧逐位一致）继续锁定本目录资产。

## 数据来源与局限（如实声明）

- 词条与频序均由**模型知识人工整理**（2026-09-29），未使用任何真实语料
  统计；频序只反映大致常用度直觉，建议接入语料后重排（见下）。
- 多音字按该词中的主导读音标注（如 便宜 `pian yi`、银行 `yin hang`、
  了解 `liao jie`、大夫 `dai fu`、重新 `chong xin`）；同音异形词不回避，
  自然形成同音组（如 `shi shi` 事实/实施、`yan jing` 眼睛/眼镜、
  `yin xiang` 音箱/音响/印象）。
- 切分歧义场景的词有意保留（`xian` 可切 `xian` 或 `xi an`，故
  `xi an 西安`、`wan an 晚安`、`ping an 平安`、`shi er 十二` 等在库中，
  由切分 DP 输出多路候选）。
- **上游缺口**：`k_imeSyllables`（408 音节）经 grep 核实缺少 4 个标准
  音节：`dian`、`diao`、`juan`、`ling`（数组 `dia→die` 之间无
  dian/diao，`lie→lin→liu` 之间无 ling，`ju→jue` 之间无 juan）。因此
  以下 99 个含该四音节的常用词**未收录**，待上游补表后可直接机械追加
  （读音如下括注拼音即可）。**【2026-09-29 已补全】** 上游 `k_imeSyllables` 已补入
  dian/diao/juan/ling（408→412），单字表补 28 字（电点店垫颠巅淀/调掉吊钓雕刁/卷捐娟倦眷绢/领零灵铃龄凌岭玲令），
  下列 99 词已全部机械追加并重排（词库现 3143 条）：
  一点(yi dian)、有点(you dian)、凌晨(ling chen)、另外(ling wai)、
  领导(ling dao)、点头(dian tou)、聆听(ling ting)、地点(di dian)、
  电梯(dian ti)、门铃(men ling)、床垫(chuang dian)、吊灯(diao deng)、
  电风扇(dian feng shan)、电冰箱(dian bing xiang)、电饭锅(dian fan guo)、
  电池(dian chi)、充电(chong dian)、充电器(chong dian qi)、
  充电线(chong dian xian)、电线(dian xian)、手电筒(shou dian tong)、
  电视机(dian shi ji)、电话(dian hua)、电话卡(dian hua ka)、
  电脑(dian nao)、点赞(dian zan)、领子(ling zi)、领带(ling dai)、
  点餐(dian can)、点菜(dian cai)、零食(ling shi)、糕点(gao dian)、
  点名(dian ming)、字典(zi dian)、词典(ci dian)、灵感(ling gan)、
  电子琴(dian zi qin)、音调(yin diao)、古典(gu dian)、点歌(dian ge)、
  电影院(dian ying yuan)、电影票(dian ying piao)、电影(dian ying)、
  电影节(dian ying jie)、经典(jing dian)、景点(jing dian)、
  免税店(mian shui dian)、晚点(wan dian)、误点(wu dian)、
  正点(zheng dian)、准点(zhun dian)、终点站(zhong dian zhan)、
  终点(zhong dian)、起点(qi dian)、掉头(diao tou)、吊销(diao xiao)、
  电动车(dian dong che)、保龄球(bao ling qiu)、领奖(ling jiang)、
  领奖台(ling jiang tai)、点球(dian qiu)、领先(ling xian)、
  雨点(yu dian)、雷电(lei dian)、发电机(fa dian ji)、
  电动机(dian dong ji)、电流(dian liu)、电压(dian ya)、电路(dian lu)、
  电器(dian qi)、断电(duan dian)、停电(ting dian)、供电(gong dian)、
  发电(fa dian)、省电(sheng dian)、漏电(lou dian)、触电(chu dian)、
  静电(jing dian)、核电(he dian)、核电站(he dian zhan)、
  零件(ling jian)、电子(dian zi)、心电图(xin dian tu)、
  调动(diao dong)、商店(shang dian)、便利店(bian li dian)、
  书店(shu dian)、观点(guan dian)、特点(te dian)、铃声(ling sheng)、
  来电(lai dian)、热点(re dian)、焦点(jiao dian)、灵活(ling huo)、
  机灵(ji ling)、凌乱(ling luan)、污点(wu dian)、电视(dian shi)、
  钓鱼(diao yu)

## 生成与校验（V3 管线）

编译器 `Tools/VirtualKeyboard/ime_phrases_compile.py`：从 `XPinyinTable.c` 提取
`k_imeSyllables` 白名单（非硬编码副本），逐行六条校验（音节合法 2..4 个/
词 1..15 字节无控制字节且良构 UTF-8/rank 1..255/恰 3 个 TAB 字段），严格
模式（默认）任何坏行=编译失败并列全部分行号（防静默丢词），另校验组内
频序恰为 1..k 连续、(拼音序列,词) 二元组唯一；稳定排序（音节 id 序列字
典序, 组内频序）后产出 XIPB，`--verify` 自检按载入器同路径逐条 walk。
回归 `Tools/VirtualKeyboard/test_ime_phrases_compile.py`：重新生成
`phrases_zh.bin`（严格）与 `test/phrases_test_fixture.bin`（--lenient）并逐字
节比对入库产物，EXIT=0 即产物一致；另校验 4 个坏件夹具与好件的偏差恰为
各自命中的拒绝点。

历史文本资产校验口径（V2 遗留，仍然成立）：BOM/UTF-8、LF-only、行格式
正则、音节合法性、CJK、唯一性、排序非降、组内频序 1..k、16 组二分抽查
（`nihao→你好`、`women→我们`、`shijian→时间`、`xian→先生`、
`xian→西安`（经 `xi an`）、`shi shi→事实`、`yan jing→眼睛` 等）。

## 建议人工抽查清单（top 20）

频序与读音建议人工复核的高频词：
我们、你们、他们、自己、什么、现在、时间、因为、所以、可以、知道、
觉得、工作、学习、生活、中国、西安、晚安、银行、便宜。
重点核对多音字读音口径（便宜 `pian yi`、银行 `yin hang`、重新
`chong xin`、大夫 `dai fu`、了解 `liao jie`）。

## 后续语料派生建议

1. 用真实语料（新闻/聊天/输入法公开词频）统计词频，替换模型知识频序，
   并补充 4 字常用语（成语与高频短语；XIPB 条目格式 2..4 音节已预留
   4 音节位，m_syllable[4] 容量口径已锁死，>4 音节整句不支持）。
2. 上游 `k_imeSyllables` 已补 dian/diao/juan/ling（408→412，99 词已
   机械追加）。后续音节表再扩时：改表 → 重编 bin（见上方告警）→ 跑
   Tools/VirtualKeyboard 回归。
3. "音节表提取 + 词库校验"已固化为 Tools/VirtualKeyboard 管线；上游音节表与词库
   一起回归（编译器每次提取运行期白名单，缺口会以坏行列出）。
4. 若 V4 引入整句/长词，需扩展条目格式（当前 16B 定长、音节上限 4
   已锁死），保持现有二分路径兼容或另行版本化（version 字段已预留）。
