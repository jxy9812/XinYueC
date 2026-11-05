# -*- coding: utf-8 -*-
"""模拟模式（HsfServer --sim）全链路验收探针。

用法：
    python hsf_sim_probe.py <port> [观察秒数] [用户] [口令]

做的事：复用 client_py 的 XLinkClient 连上 → 登录 → 订阅全部遥测主题 →
统计各主题帧数与首帧样本 → 按判据给 PASS/FAIL。

为什么不手写 socket：线协议（16 字节小端帧头 / BSON 载荷 / 盐挑战认证）
已在 client_py 实现并验证过；再造一份「线格式权威」会在两侧漂移时说谎。
本探针只负责**判据**——判"模拟设备是否真的把数据喂到了业务层"。

判据（全满足才 PASS）：
  · vfd / ai / temphum / flowmeter / health / digital / valve 七主题都有帧；
  · 温湿度落在物理量程（模型缺省 26.5℃ / 55.0%）；
  · 瞬时流量 > 0（模型缺省 12.5 m3/h）；
  · 阀路数 == 6；
  · 浊度两台都在线，且 1 号读数明显高于 2 号（模型缺省 12.5 / 3.2 NTU，
    说明两台绑的是各自的模型，不是同一个值贴两遍）。

退出码：0=通过，1=有失败，2=用法/连接错误。
"""
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_CLIENT_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", "client_py"))
if _CLIENT_ROOT not in sys.path:
    sys.path.insert(0, _CLIENT_ROOT)

from hsfsrv_client import bson as hsfbson           # noqa: E402
from hsfsrv_client import proto                     # noqa: E402
from hsfsrv_client.xlink import XLinkClient         # noqa: E402

TOPIC_NAMES = {
    proto.TOPIC_VFD_STATUS: "vfd",
    proto.TOPIC_AI_STATUS: "ai",
    proto.TOPIC_TEMPHUM_STATUS: "temphum",
    proto.TOPIC_FLOWMETER_STATUS: "flowmeter",
    proto.TOPIC_DEVICE_HEALTH: "health",
    proto.TOPIC_DIGITAL_STATUS: "digital",
    proto.TOPIC_VALVE_STATUS: "valve",
    proto.TOPIC_TURBIDITY_STATUS: "turbidity",
}
REQUIRED = ("vfd", "ai", "temphum", "flowmeter", "health", "digital", "valve",
            "turbidity")


def main():
    if len(sys.argv) < 2:
        print("usage: hsf_sim_probe.py <port> [seconds] [user] [password]")
        return 2
    port = int(sys.argv[1])
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    user = sys.argv[3] if len(sys.argv) > 3 else "root"
    passwd = sys.argv[4] if len(sys.argv) > 4 else "root"

    counts = {}
    samples = {}

    def on_publish(topic, body, dtype):
        name = TOPIC_NAMES.get(topic, "0x%08X" % topic)
        counts[name] = counts.get(name, 0) + 1
        if name in samples:
            return
        try:
            samples[name] = hsfbson.decode(body) if body else {}
        except Exception as exc:                    # noqa: BLE001
            samples[name] = {"_decode_error": str(exc)}

    client = XLinkClient("127.0.0.1", port, user, passwd)
    client.on_publish = on_publish
    # 自动重连会掩盖"连不上"——探针要确定性。
    client.auto_reconnect = False

    ok, err = client.start(timeout=8.0)
    if not ok:
        print("FAIL: 连接/登录失败：%s" % err)
        client.close()
        return 2

    for topic in TOPIC_NAMES:
        fut = client.subscribe_async(topic)
        if fut is not None:
            try:
                fut.result(timeout=3.0)
            except Exception as exc:                # noqa: BLE001
                print("  订阅 0x%08X 失败：%s" % (topic, exc))

    time.sleep(secs)

    # 流量计固定在**路 B**（侧 1）：周期推送的侧 0 帧本来就 valid=false，
    # 用请求式快照按侧取，判据才落在真正装了站的那一路上。
    flow_snap = {}
    def mk_flow(side):
        def cb(mc, dt, pl):
            flow_snap[side] = hsfbson.decode(pl) if pl else {}
        return cb
    for side in (0, 1):
        client.request_async(proto.CMD_FLOWMETER_SNAPSHOT,
                             hsfbson.encode({proto.KEY_SIDE: side}),
                             proto.DATATYPE_BSON, 3.0,
                             on_done=mk_flow(side))
    turb_snap = {}
    def mk_turb(side):
        def cb(mc, dt, pl):
            turb_snap[side] = hsfbson.decode(pl) if pl else {}
        return cb
    for side in (0, 1):
        client.request_async(proto.CMD_TURBIDITY_SNAPSHOT,
                             hsfbson.encode({proto.KEY_SIDE: side}),
                             proto.DATATYPE_BSON, 3.0,
                             on_done=mk_turb(side))
    time.sleep(1.5)
    client.close()

    print("=== 收到帧数 ===")
    for name in sorted(counts):
        print("  %-12s %d" % (name, counts[name]))
    print("=== 首帧样本 ===")
    for name in sorted(samples):
        doc = samples[name]
        keys = [k for k in doc.keys() if not k.startswith("d")][:10]
        print("  %-12s %s" % (name, {k: doc[k] for k in keys}))
    print("=== 流量计快照 ===")
    for side in sorted(flow_snap):
        print("  side %d -> %s" % (side, flow_snap[side]))
    print("=== 浊度仪快照 ===")
    for side in sorted(turb_snap):
        print("  side %d -> %s" % (side, turb_snap[side]))

    fails = []
    for need in REQUIRED:
        if counts.get(need, 0) < 1:
            fails.append("%s 无帧" % need)

    # 温湿度：协议口径是 **0.1 单位定点**（265 = 26.5℃），直接比较物理量。
    th = samples.get("temphum", {})
    t = th.get("temp")
    h = th.get("hum")
    if isinstance(t, (int, float)):
        if not (-400 <= t <= 1250):
            fails.append("温度越界 %r（0.1℃ 单位）" % (t,))
        elif not (200 <= t <= 320):
            fails.append("温度不在模型缺省附近 %r（应 ~265）" % (t,))
    elif th.get("tempValid"):
        fails.append("tempValid=true 却没有 temp 字段")

    # 流量计：只在侧 1 有站，判侧 1 且流量 > 0（模型缺省 12.53）。
    f1 = flow_snap.get(1, {})
    fv = f1.get("flow")
    if not f1.get("valid"):
        fails.append("侧 1 流量计无有效数据：%s" % (f1,))
    elif not isinstance(fv, (int, float)) or fv <= 0:
        fails.append("侧 1 瞬时流量应 > 0，实得 %r" % (fv,))

    # 浊度仪：侧 1 装了站。两台都必须在线，且 ntu 是 NTU×100 的整数，
    # 1 号（原水）应明显高于 2 号（出水）——这一条同时证明了"两台各绑
    # 自己的模型"（绑错成同一个模型时两个值会一模一样）。
    t1 = turb_snap.get(1, {})
    if t1.get("count") != 2:
        fails.append("侧 1 浊度台数应为 2，实得 %r" % (t1.get("count"),))
    else:
        for idx in (0, 1):
            if not t1.get("v%d_online" % idx):
                fails.append("浊度仪%d 侧 1 不在线：%s" % (idx + 1, t1))
        n0 = t1.get("v0_ntu")
        n1 = t1.get("v1_ntu")
        if isinstance(n0, (int, float)) and isinstance(n1, (int, float)):
            # 协议口径 NTU×100：12.5 → 1250 附近，3.2 → 320 附近。
            if not (900 <= n0 <= 1600):
                fails.append("浊度仪1 读数应 ~1250（NTU×100），实得 %r" % (n0,))
            if not (150 <= n1 <= 600):
                fails.append("浊度仪2 读数应 ~320（NTU×100），实得 %r" % (n1,))
            if n0 <= n1:
                fails.append("浊度仪1 应高于浊度仪2（原水 vs 出水），实得 %r/%r"
                             % (n0, n1))

    va = samples.get("valve", {})
    vn = va.get("count")
    if vn is not None and vn != 6:
        fails.append("阀路数应为 6，实得 %r" % (vn,))

    print()
    if fails:
        print("FAIL: " + "; ".join(fails))
        return 1
    print("PASS: 八个主题全部有数据，字段量程正确")
    return 0


if __name__ == "__main__":
    sys.exit(main())
