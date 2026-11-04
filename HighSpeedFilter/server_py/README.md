# HsfServerPy 文档

Python 版高速过滤器服务端的**独立文档集**。全部内容自包含——
不需要翻 C 侧头文件也能读懂协议、部署和排障。

| 文档 | 内容 | 适合谁 |
|---|---|---|
| [01 部署与运维](docs/01-部署与运维.md) | 环境准备、部署、systemd、日常运维、故障处理 | 现场/运维 |
| [02 线协议规格](docs/02-线协议规格.md) | XLink 帧格式、登录认证、全部命令与主题、键表 | 对接开发 |
| [03 设备与寄存器口径](docs/03-设备与寄存器口径.md) | 五类设备的地址、换算、协议怪癖 | 现场调试/驱动开发 |
| [04 开发与测试](docs/04-开发与测试.md) | 模块结构、设计决策、已知陷阱、测试方法 | 维护者 |

---

## 端口约定：改一处就够

**所有端口与串口缺省值都集中在 `hsfsrv/ports.py`。**

```python
PY_SERVER_PORT = 6200      # 服务端监听端口（改这里）
C_SERVER_PORT = 6100       # C 版端口（协议规格值）
SERIAL_PORT_WINDOWS = "COM17"      # 桌面端缺省串口
SERIAL_PORT_POSIX = "/dev/ttyS3"   # 板端缺省串口
```

改 `PY_SERVER_PORT` 一处即可，服务端监听、客户端缺省连接、`--help`
显示、部署脚本全部自动跟随。

**为什么要和 C 版（6100）错开**：让两套服务端可以同机并存做对比调试
（共用端口会让后启动的起不来）。协议完全一致，客户端 `--port` 选边即可。

> 客户端目录下有一份**内容完全相同**的 `ports.py`（两端独立部署，
> 不能互相 import）。`tests/test_ports.py` 会逐字节比对两份。

## 这是什么

一台采集现场仪表并向上位机提供实时数据的服务端：

- **链路**：TCP（XLink v2）+ 二进制 BSON 载荷，登录鉴权、订阅推送
- **采集**：一路 RS485（变频器 + 温湿度）+ 三个 Modbus TCP 设备
- **控制**：变频器启停/频率/点动、数字量 16 路 DO 输出
- **约束**：自动模式下拒绝全部远程控制；点动看门狗 600ms 超时停机

## 三十秒上手

```sh
# 本机（桌面端：串口缺省 COM17，端口缺省 6200）
python -m hsfsrv --help
python -m hsfsrv --diag        # 先自检设备连通性
python -m hsfsrv               # 起服（6200）

# 无硬件时用假设备自测
python tests/run_all.py

# 板上
sh deploy.sh --no-serial      # 部署 + systemd + 起服务
ssh root@192.168.0.10 'journalctl -u hsfserverpy -f'
```

## 设备一览

| 设备 | 链路 | 地址 | 关键口径 |
|---|---|---|---|
| M512/M513 变频器 | RS485 | `/dev/ttyS3` 站 2 | 频率 0x1001 单位 0.01Hz |
| 温湿度 ZS-SHT30 | RS485 | `/dev/ttyS3` 站 4 | FC04 ×2，各 ×0.1 |
| 模拟量 8 路 4~20mA | TCP | `192.168.0.11:8234` | `I(mA) = mV / 249Ω` |
| 数字量 16DI/16DO | TCP | `192.168.0.12:502` | FC02 读 DI / FC01 读 DO / FC05 写 |
| 数字量 16DI/16DO | TCP | `192.168.0.13:502` | 同上 |

## 目录结构

```
server_py/
├── README.md            本文件（索引）
├── docs/                独立文档集
├── deploy.sh            板端部署脚本
├── cfg/                 运行期配置（首次自动生成）
├── hsfsrv/              服务端实现
│   ├── proto.py         线协议常量 + 帧头编解码
│   ├── bson.py          BSON 编解码（标准库）
│   ├── auth.py          登录认证 + 账户库
│   ├── xlink.py         XLink 服务端会话层
│   ├── modbus.py        Modbus RTU/TCP 主站
│   ├── devices.py       设备驱动
│   ├── app.py           业务层
│   └── config.py        配置
└── tests/               单元 + 端到端测试
```
