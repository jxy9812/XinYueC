# verify/webui 验收环境口径（数据三页工匠 2026-11-13 留档）

## 16 页截图双跑：ref_sweep.py / web_sweep_edge.py（2026-11-13）

两套脚本产出逐页对照的两个 16 张截图集（Py 客户端参考 vs 网页实现）：

| 脚本 | 跑法 | 输出 | 判据 |
|---|---|---|---|
| `ref_sweep.py` | `python verify/webui/ref_sweep.py`（编排器任意 python；Qt 截图子进程自动用 `D:/anaconda3/python.exe`） | `shots/ref/p00.png .. p15.png` | 0 = 16 张齐全非空白；1 缺图/空白；2 C sim 失败；3 登录失败；4 无 anaconda；5 webui 拉回失败 |
| `web_sweep_edge.py` | `python verify/webui/web_sweep_edge.py`（纯标准库） | `shots/web/p00.png .. p15.png` | 0 = 16 张齐全；1 缺图/空白/跨页同图；2 前置失败；3 登录失败；4 无 Edge |

**[页数 18→16（2026-11-13「三页合并」后）]** 参考客户端现在只有 16 页
（`client_py/hsfsrv_client/pages_registry.py:38-42` PAGE_COUNT=16；原
「悬浮窗设置(16) / 窗口设置(17)」两页的内容并入「本地设置(13)」，页号
16/17 从注册表与导航树删除）⇒ 网页端 `/api/pages` 同样是 count=16 /
names 16 项 / nav 15 项 ⇒ 两套截图脚本都按 **16 页**扫。
**合并之前拍的 18 张参考图已作废**（导航树还列着两个已注销的页、「本地
设置」页缺合并后新增的卡）⇒ 比对基线必须用 `ref_sweep.py` 重拍，
否则 p13/p16/p17 的比对无效（会误判成「网页多了/少了东西」）。

**网页端口运行期解析（两套脚本一致，2026-11-13 加）**：解析顺序 =
`HSF_WEB_PORT` 环境变量 → 在跑的 hsfweb（**优先「上游 = 6100」那个**，
同上游多个时取**最小**端口，结果确定）→ 候选端口探活 `/api/ping`
（自报 `server=hsf-webui`）。**不写死端口**：本机 8080 被**别人的**常驻
程序占着（`F:\直滤\read_analog_gui.py`，见下节），hsfweb 实际听 8085；
写死端口迟早打到别人的服务上。

**时序纪律（铁律，两套脚本的 root 会话互斥）：**

1. **Py 客户端与 webui 的 root 会话互斥**：webui 的上游主链路也用 root 登录
   6100 ⇒ `ref_sweep.py` 开始前**必须先停 webui**，跑完**等 8 秒**再拉回
   （8 秒 = C 服务端把旧 root 会话按静默超时回收的窗口；立即拉回会被
   「该账户已在线」瞬时拒绝——脚本里也带了登录重试兜底）。
   **只停「目标端口上那一个」hsfweb**：认进程 = 进程名 python*.exe +
   命令行含 `-m hsfweb` + 正在监听目标端口（2026-11-13 实测本机同时跑着
   8085/8086/8090/8094/8097/8098/8100 好几个 webui 实例——别的阶段的隔离
   环境，**不能碰**；8080 上更是别人的程序 `read_analog_gui.py`）。
   拉回时**显式 `--web-port <原端口>`**：hsfweb 缺省听 8080，8080 被占时它
   会「端口被占，改听随机端口」⇒ 常驻服务挪到随机端口上，后续脚本再也
   找不到它（真端口会打进 `verify/webui/webui_restart.log`）。
2. **两套脚本不要同时跑**：`ref_sweep.py` 会停/拉回 webui；`web_sweep_edge.py`
   需要 webui 在跑（它自己探活、不在才拉起，**从不停 webui**）。
   正确顺序：先 `ref_sweep.py`（结束时 webui 已被拉回），再
   `web_sweep_edge.py`。
3. **ref_sweep 的 Qt 口径**：真窗口（拒绝 `QT_QPA_PLATFORM=offscreen`——
   本机 offscreen 无字体整页空白）、必须 anaconda python（PyQt5 在那）、
   单进程单会话从 p00 扫到 p15（**绝不能每页起一个进程**——同账会话互斥，
   第二个连接被拒）、每页 `goto_page` 后 QEventLoop 跑满 ≥1200ms 再 grab
   （QSS polish + 首帧推送要时间）。
4. **web_sweep 的 Edge 口径**：每页独立 `--user-data-dir`（共用 profile 时
   第二发不落盘）、`--virtual-time-budget=8000`、URL 带 `?authtoken=<tok>`
   直登 + `#/page/N` 深链。**msedge.exe 本体是启动器壳**：~0.2s 就 exit 0，
   真 headless 浏览器分离出去、PNG 在启动器退出**之后**才落盘
   ⇒ 脚本在启动器退出后轮询等文件（等 45s），各页因此天然串行——
   **千万别改成「进程退出就查文件」或并发拍**：18 发并发挤一起时后页在
   8s 预算内完不成 authtoken 直登，拍出来全是登录遮罩、甚至不落盘
   （2026-11-13 整轮翻车实测；不同页逐字节相同 = 登录遮罩指纹，
   脚本已把这类图判败）。
   **临时 profile 必须删干净**：headless 浏览器是分离进程、写完 PNG 还在跑
   ⇒ 直接 rmtree 会被占用文件顶掉（每页留 13MB，16 页 ≈ 208MB 垃圾）。
   脚本改成「先按 profile 路径认出那些 Edge 进程杀掉，再 rmtree 重试，
   收尾再兜底扫一遍」（`cleanup_profile()`）——2026-11-13 复跑验证：跑完
   临时目录 0 残留、headless 进程 0 残留。
5. **截图目录先清后拍**：两套脚本各自清掉目标目录里旧的 `pNN.png` 再拍，
   陈旧图会让「缺图」判据假绿。
6. **空白判据**：文件 <15KB、不同页逐字节相同，或（ref 侧）QImage 网格
   取样全同色——QSS 没生效 / 整页无内容 / 登录遮罩的指纹，都算败。
7. **跑多了会撞 C sim 僵尸会话**（本 README 下方「共享 C sim 登录被拒的
   自愈口径」）：`/api/login` 401 "authentication rejected" 且凭据无误时，
   按 `netstat CLOSE_WAIT` 取证后走该节的标准恢复口径重启 C sim。
   截图脚本对登录失败只重试并如实报败（退出码 3），不绕闸。
   另：web 截图若整批出现「压差/健康有数但流量/浊度/温湿度 0 或 --」，
   先怀疑 webui 上游数据面缺流（同一僵尸会话根因），不是页面缺口——
   C sim 健康时重拍即可。
8. **状态栏消息的截图口径（2026-11-13 第 4 轮已对齐）**：
   ref_sweep 是**单会话顺序走页**——先扫过 p09 历史页，其后所有页的状态栏
   都常驻「已载入服务端历史：最早一条 …」。
   web_sweep 现在**也是单会话**：第 4 轮起改用 DevTools 协议
   （`web_sweep_edge.py` 的 `shoot_pages_session` + 同目录 `cdp_client.py`）
   ——**一个浏览器实例、一个文档**，逐页只换 hash（router.js 的 hashchange
   ⇒ 不重载文档）⇒ 状态栏消息跨页保留，两端口径一致（p10/p11/p12 与参考
   逐字相同）。
   [为什么不用「每页一个 Edge 进程」] 每页新开 = 新文档 = 状态栏消息被重置
   ⇒ 未经过 p09 的页恒显「登录成功（root，角色 2）」，与参考端差一句。
   [为什么不用「同一 profile 多进程」] Edge 共用 profile 时第二发不落盘
   （本文件第 4 条的实锅）⇒ 改走协议、只开一个实例。
   旧口径（每页一进程）仍保留为**回退路径**：CDP 采集失败时自动退回，截图
   照出，只是状态栏文案与参考端差一句。



## 常驻服务与探活

- 上游 C 模拟服务端：6100 上当前是 **Python 版** `python -m hsfsrv --sim --port 6100`
  （C 版 `bin/HsfServer.exe --sim` 是同协议的另一实现，脚本只在 6100 **没人听**时
  才拉它起来；DB 在 `bin/cfg/hsf_data.db`，历史 4 万行量级、日志 7 万行量级，重启不丢）。
- 网页服务器：`HighSpeedFilter/webui_py/hsfweb` 目录下
  `python -m hsfweb --server 127.0.0.1 --port 6100 --web-port 8085`
  （`--server/--port` 指**上游**，`--web-port` 指**网页**端口；**api.py/store.py 等后端 .py 改动必须重启该进程才生效**，
  重启后用 `tasklist` 确认旧进程真死，Git Bash 杀进程带 `MSYS_NO_PATHCONV=1`）。
- **网页端口不是 8080**（2026-11-13 实测）：8080 上跑着**别人的**常驻程序
  `F:\直滤\read_analog_gui.py`，**不是 hsfweb、绝不能杀**；hsfweb 实际听 **8085**。
  端口还会随部署变 ⇒ 脚本一律**运行期解析**（见上节），别在命令里写死 8080。
- 探活：`curl http://127.0.0.1:8085/api/ping`（应回 `{"ok":true,"server":"hsf-webui"}`）
  + TCP 127.0.0.1:6100。
- 回归：`python verify/webui/smoke_backend.py`（自管进程，复用常驻服务不清理）。

## 重跑方法（2026-11-13）

```bash
cd F:/Code/CMake/XinYueC
# ① 先跑 Py 客户端参考图（会停 webui → 拍 16 页 → 等 8 秒 → 拉回 webui）
python verify/webui/ref_sweep.py ; echo "ref exit=$?"
# ② 再跑网页版截图（webui 必须在跑；脚本自己探活、缺了才起）
python verify/webui/web_sweep_edge.py ; echo "web exit=$?"
```

- 两脚本都**自管前置**：6100 的 C sim 在跑就复用、不在才起；webui 同理
  （`ref_sweep` 例外：它必须先停 webui 再拉回）。
- **别并行跑两个脚本**（root 会话互斥，见上节时序纪律第 2 条）。
- 端口想钉死就设环境变量：`HSF_WEB_PORT=8085 python verify/webui/web_sweep_edge.py`。
- 目标目录的旧 `pNN.png` 每次开拍前会被清掉（陈旧图会让「缺图」判据假绿）。
- 退出码：ref `0` 全齐 / `1` 缺图或空白 / `2` C sim 前置失败 / `3` 登录失败 /
  `4` 缺 anaconda python / `5` webui 拉回失败；web `0` 全齐 / `1` 缺图或空白 /
  `2` 前置失败 / `3` 登录失败 / `4` 缺 Edge / `5` 上游未就绪。
- 单页重拍：ref 侧改 `PAGE_COUNT`/起页号不方便，直接重跑整轮（单进程单会话，
  整轮约 1 分钟）；web 侧可只跑缺的那几页（把 `for i in range(PAGE_COUNT)` 换成
  目标页号，脚本其余部分不变）。

## 截图自证

```bash
# 网页端口按实际解析（本机 = 8085；8080 上是别人的程序，别用）
WEB=8085
TOK=$(curl -s -X POST http://127.0.0.1:$WEB/api/login -H "Content-Type: application/json" \
      -d '{"user":"root","password":"root"}' | python -c "import sys,json;print(json.load(sys.stdin)['token'])")
"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --headless=new --disable-gpu \
  --user-data-dir="C:/Windows/Temp/edge_shot_x" \
  --screenshot="F:/Code/CMake/XinYueC/verify/webui/shots_data3/pageNN.png" \
  --window-size=1280,800 --virtual-time-budget=9000 \
  "http://127.0.0.1:$WEB/?authtoken=$TOK#/page/N"
```

注意：并行截图要给每个实例不同的 `--user-data-dir`，否则第二发不落盘。

## 共享 C sim 登录被拒的自愈口径（2026-11-13 定案）

症状：`POST /api/login` 401 "authentication rejected"，或裸客户端报
「该账户可能已在别处登录，或会话数已达上限」——而凭据正确、策略正常
（`bin/cfg/hsf_login_policy.json` allowConcurrent=true / maxSessions=8）、
6100 上没有任何 ESTABLISHED 连接。

排查：`netstat -ano | findstr :6100` —— 若看到 **CLOSE_WAIT** 滞留（对端进程已死、
C sim 一侧不关），即 C sim 僵尸会话不回收（真缺陷，已记 PARITY_PLAN §4；等 3 分钟
不自愈）。**不要改登录策略文件、不要动账户库。**

恢复（标准口径，2026-11-13 经会话主人批准执行过一次）：

```bash
netstat -ano | findstr :6100        # 找到 C sim PID（LISTEN 行末列）
MSYS_NO_PATHCONV=1 taskkill /F /PID <sim_pid>
tasklist | findstr HsfServer        # 确认真消失
# 原样拉起（工作目录必须在 bin/）：
cd /d F:\Code\CMake\XinYueC\bin && HsfServer.exe --sim
# 等 ~10s 后探活：TCP 127.0.0.1:6100 可连 → 重跑 smoke_backend.py
```

C 框架本身的修复（会话回收）路由给框架/服务端工匠，不在页面阶段做。

## 数据三页截图存档

`shots_data3/`：page09_final/page09_chart/page10_final/page11_final2
（隔离环境：自起 Python sim 6200 + webui 8091 上拍的，报警页有真实边沿记录；
page09/page10/page11（无后缀）为共享 8080 环境）。隔离环境用完即拆，
不与常驻服务混线。

## 逐页配色对比（2026-11-17「light 主题对齐 web 端」新增）

| 脚本 | 跑法 | 输出 | 判据 |
|---|---|---|---|
| `compare_palette.py` | `python verify/webui/compare_palette.py [页号...]`（需 PIL） | 逐页打印**量化配色直方图**的差集 | 两端集合一致 ⇒ 该页配色已对齐 |

**[为什么要有它] 肉眼判颜色会被骗**：50% 缩略图里"黑字"看着像蓝字
（抗锯齿 + 缩放混色），裁剪放大才发现两端其实都是黑字。⇒ 颜色一律
**像素取证**：① 量化（每通道 16 级归桶）后取前 12 大色比差集；② 抗锯齿
像素会制造噪声桶（#E0E0E0 桶精确色一个都没有 ⇒ 纯 AA）⇒ 占比 <2% 视为
可忽略。
