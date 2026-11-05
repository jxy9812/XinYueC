# HSF 设备模拟器

**假服务端**：只实现 XLink 会话层 + HSF 业务命令的最小子集，数据全合成，
不碰任何真实设备。

## 用途

| 场景 | 用法 |
|---|---|
| **设备没到位**，先把客户端界面/交互跑起来 | `python sim/hsf_sim.py --show` |
| **C 端客户端联调**（不依赖 Python 服务端） | `python sim/hsf_sim.py --port 6100`，然后起 `HsfClient.exe` |
| 回归测试的**可控数据源** | `--port` / `--period-ms` / `--check` |
| 无显示器环境出图验收 | 默认离屏渲染并存 PNG，`--out` 指定路径 |

## 快速上手

```sh
# 起假服务端 + Python 客户端实窗（数字量页）
python sim/hsf_sim.py --show

# 指定页面（英文别名最稳，中文参数在 Git Bash 下会被转码）
python sim/hsf_sim.py --show --page-name vfd      # 变频器
python sim/hsf_sim.py --show --page-name analog   # 模拟量（压差计）
python sim/hsf_sim.py --show --page-name valve    # 电磁阀
python sim/hsf_sim.py --show --page-name home     # 主界面

# 只起服务端（前台保持），让 C 端客户端连
python sim/hsf_sim.py --port 6100 --no-shot

# 只校验协议常量与服务端一致
python sim/hsf_sim.py --check

# 离屏截图（不弹窗）
python sim/hsf_sim.py --page-name analog --out /tmp/shot.png
```

## 合成数据的形状

刻意在**上半区**（bit16..31）放信号，这样**任何一环按 16 点截断都会暴露**
（CH16..CH31 会全灭、或界面只画 16 个格）：

| 数据 | 值 |
|---|---|
| DI 位图 | `0x00FF00FF`（bit0..7 + bit16..23 通） |
| DO 位图 | `0x8000000F`（bit0..3 + bit31 通） |
| 模拟量 | 通道0 = 2985mV（11.988mA ≈ 0.499MPa 压差计），其余 995mV 活零点 |
| 变频器 | 含转速（50Hz↔1440rpm），启停/调速命令会真改状态 |
| 电磁阀 | 6 路，全部"无到位反馈"（与现场未接行程开关一致） |

数字量的通道名照抄服务端的推导结果（`电磁阀N 控制` / `关到位` / `开到位`），
让模拟界面与真机一致。

> **⚠ 本模拟器的电磁阀是"瞬发"的**：`DIGITAL_CONTROL` 写 DO 后，对应到位
> DI 立刻跟着变，没有 10s 行程。所以在这里**看不到**「动作中」那 10 秒，
> 也**验证不了** 20s 超时告警（状态机下一拍就"到位"，计时器立刻清零）。
>
> 要验证阀的动作时序/超时/重复点击/外部写接管，**必须用服务端的虚拟设备**：
>
> ```sh
> python -m hsfsrv --sim                        # 缺省 10s 行程
> python -m hsfsrv --sim --sim-valve-move 25    # 25s > 20s 阈值 ⇒ 会报警
> python -m hsfsrv --sim --sim-valve-move -1    # 阀卡死
> ```
>
> 详见 `server_py/docs/04-开发与测试.md` §5.4.1。
> 两者的分工：**本模拟器验证客户端界面与协议解析**，
> **`--sim` 验证服务端业务逻辑**（闸门/看门狗/阀状态机/告警）。

> **流量计/浊度/泵两边都有**：本模拟器与 `--sim` 都能推这三样
> （流量 22 寄存器口径一致，瞬时流量同样按 ±2% 正弦波动）。服务端 `--sim`
> 下会**自动补挂**虚拟流量计与两台虚拟浊度仪（现场配置里默认关闭），
> 所以客户端切到水质监测页就有数。

| 数据 | 本模拟器 | `--sim` |
|---|---|---|
| 流量计 | 36.5 m³/h ±2%（周期 60s） | 同口径 |
| 浊度仪 ×2 | 12.50 / 3.20 NTU ±3%（**相位差 180°**） | 同口径 |
| 进水泵 | DO22 回显（`v0_on` = **输出状态**，无运行反馈） | 同口径 |

> **泵的 DO 是 22 不是 12**：阀占 DO16..21（`.13` 聚合进 `.12` 的**偏移
> 16**），泵紧邻下一个口 = 16+6 = 22。`--check` 里有一条接线口径自洽校验
> 专门挡这个偏移写漏（写错时退出码 3，报"泵 DO=12（应为 16+6=22）"）。

## 支持的命令

| 命令 | 支持度 |
|---|---|
| `DIGITAL_SNAPSHOT` / `AI_SNAPSHOT` / `VFD_SNAPSHOT` / `TEMPHUM_SNAPSHOT` / `VALVE_SNAPSHOT` | ✅ 返回对应遥测帧 |
| `VFD_CONTROL` | ✅ 启停/自由停车/点动/复位/频率给定/转速给定都真改内部状态 |
| `TURBIDITY_SNAPSHOT` / `PUMP_SNAPSHOT` | ✅ 浊度 2 台 / 泵 1 台快照 |
| `PUMP_CONTROL` | ✅ 翻 DO22 并回写后快照（越界泵号回 `ok=false`） |
| `DIGITAL_CONTROL` / `VALVE_CONTROL` / `RUN_MODE` | ❌ 明确回 `ok=false`（不静默） |

## 三条实现约定（改它之前先读）

### 1. 只**借用**协议模块，不复制

```python
sys.path.insert(0, SERVER_PY)
from hsfsrv import auth, bson, xlink
```

`xlink.py` / `bson.py` / `auth.py` 是线协议的**唯一实现**。复制一份必然漂移，
症状是"模拟器发的帧客户端解不开"，排查要翻协议层。

### 2. 协议常量**自带一份** + `--check` 校验

模拟器不能 import 服务端业务包（要独立可跑），所以常量在本文件里另写一份。
`--check` 会尝试导入 `hsfsrv.proto` 并逐项比对，不一致立刻报错退出。

**它已经抓出 3 处真错误**：

| 常量 | 写错的 | 实际 |
|---|---|---|
| `OP_SET_FREQUENCY` | 10 | **2** |
| `OP_SET_SPEED` | 11 | **3** |
| `CMD_VFD_SNAPSHOT` | 0x0101 | **0x0102**（0x0101 是 CONTROL） |

第三个正是"变频器页一进去就报『控制已禁用：不支持的动作 op=0』"的根因——
快照请求被当成控制命令处理了。**改任何常量后都跑一次 `--check`。**

### 3. 三处钩子**必须**都给

| 钩子 | 何时被调 | 缺了会怎样 |
|---|---|---|
| `server.on_ready` | 登录成功后**无条件**调用 | 抛 `TypeError` → 会话被打死 |
| `server.dispatch` | 收到业务 Request 时**无条件**调用 | 同上（客户端数字量页一进去就发快照请求） |
| `server.on_publish_now` | 订阅补发 / 首连补发 | 同上 |

三者的**症状完全一样**：登录成功但立刻断开、客户端一直接不到数据。
不难判，但很容易想不到。

> `on_publish_now` 必须对**所有**主题返回二元组。客户端还会订阅温湿度/
> 健康等全局主题，对未支持主题返回 `None` 会在
> `payload, dtype = await ...` 处抛 `cannot unpack non-iterable NoneType`。

## 排查提示

**"字段/名字一直是空"** → 先查**端口是否被旧的模拟器进程占着**。
新进程绑不上端口，客户端连的是旧版（可能还没有该字段）。
这与项目里已记录的「HSF 假性无数据＝旧服务端占端口」是同一个坑。

```sh
netstat -ano | grep :6288      # 看有没有别的进程在听
taskkill //F //IM python.exe    # Windows 下清干净
```
