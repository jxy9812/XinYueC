# -*- coding: utf-8 -*-
"""一键跑全部测试（单元 + 端到端）。

    python tests/run_all.py            # 全跑
    python tests/run_all.py --units    # 只跑单元测试

端到端部分会自动在后台起假设备服务端、跑完再关掉，不需要手工准备环境。
"""

import argparse
import os
import subprocess
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PORT = 6199


def run(cmd, label, timeout=300):
    print("\n" + "=" * 70)
    print("  " + label)
    print("=" * 70)
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, cwd=ROOT, timeout=timeout)
        ok = proc.returncode == 0
    except subprocess.TimeoutExpired:
        print("[TIMEOUT] %s" % label)
        ok = False
    print("  -> %s  (%.1fs)" % ("PASS" if ok else "FAIL", time.time() - t0))
    return ok


def port_free(port):
    """端口是否可绑（Windows/Linux 通用，纯 socket）。"""
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind(("0.0.0.0", port))
            return True
        except OSError:
            return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--units", action="store_true", help="只跑单元测试")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    py = sys.executable
    results = []

    results.append(("端口一致性", run([py, "tests/test_ports.py"],
                                       "端口配置（单一事实源）")))
    results.append(("单元测试", run([py, "tests/test_units.py"], "单元测试")))
    results.append(("模式规则", run([py, "tests/test_mode_lamp_rules.py"],
                                     "模式规则（手动灯跟随设备在动 + 切模式全停）")))
    # [按钮灯闪烁 2026-11-24] 启动/停止/复位三盏灯的闪相位与配置落盘。
    results.append(("按钮灯闪烁", run([py, "tests/test_button_blink.py"],
                                      "按钮灯闪烁（复位/启动/停止 + 亮灭毫秒可配）")))
    # [按钮灯闪烁 2026-11-24] 同一口径的**真实 sim 全栈闭环**：虚拟设备 +
    # 真 XLink + 真客户端，从数字量模块 DO 直接观测三盏灯（含 VFD 减速
    # 期间停止灯闪、停稳后常亮）。
    results.append(("按钮灯闪烁 sim", run(
        [py, "tests/test_button_blink_sim.py"],
        "按钮灯闪烁 sim（真实全栈：DO7/DO8/DO9 观察）")))
    # [运行保护 2026-11-15] 进水泵开着 + 流量计仍报空管 → 超时动作
    # （停设备/只报警）+ 流量计告警进报警表。
    results.append(("运行保护", run([py, "tests/test_protect.py"],
                                    "运行保护（进水泵空管超时 + 流量计告警入库）")))
    # [滤速 2026-11-25] 瞬时流量/过滤面积 ⇒ 随流量计帧下发 m/h。
    results.append(("滤速换算", run([py, "tests/test_filter_velocity.py"],
                                    "滤速（m/h = 瞬时流量/面积 + 配置落盘）")))
    # [变频器参数读写 0x011B 2026-11-18] 读默认值/写后回读不撒谎/
    # RunLocked 运行中拒写/权限闸/未装配包络 + 虚拟模型参数寄存器。
    results.append(("变频器参数", run([py, "tests/test_vfd_params.py"],
                                      "变频器参数读写（0x011B：读/写/RunLocked/权限）")))
    # [变频器参数帧层+socket 级 2026-11-18 补洞] 0x011B 的两条**真机约束**
    # （0x3000/0x8000 数量必须=1、写后 0.3~0.5s 不应答窗口 + FC06 错误帧）
    # 与 XLink 帧/requestId 归属——上一层的假 master 永远立刻应答，跑不到。
    results.append(("变频器参数帧层",
                    run([py, "tests/test_vfd_params_frame.py"],
                        "变频器参数帧层 + socket 级（0x011B 真机约束 / XLink 帧）")))
    # [2026-11-15] 周期推送不得阻塞事件循环（一个读不动的客户端不能把
    # 整个 tick 卡住 ⇒ 自动控制停滞）。
    results.append(("推送不卡", run([py, "tests/test_publish_no_stall.py"],
                                    "周期推送不阻塞事件循环（不等 drain）")))
    # [2026-11-15] 全量审计整改的回归锁：换相 carry 停泵汇总 / 配置读文件
    # 容错 / 落盘写库连接为空 / 手动模式「现场在动」的有效性 / 严格布尔。
    results.append(("审计整改", run([py, "tests/test_audit_hardening.py"],
                                    "审计整改（carry 停泵/配置容错/严格布尔）")))
    # [2026-11-16] 数字量名字脏帧（阀点位名汇合表 + 一次置脏只补一帧）。
    # [为什么补注册] 原来只 standalone 跑 ⇒ 2026-11-12 阀位序订正（开=偶）
    # 后断言过期却一直没被抓到——不进门禁的锁等于没有锁。
    results.append(("名字脏帧", run([py, "tests/test_digital_names_dirty.py"],
                                    "数字量名字脏帧（点位名/一次性补发）")))
    # [相机 2026-11-19] camera 配置节（fail-closed/口令脱敏/落盘）+ 分片
    # 协议（16B 片头/12KB 片长/单片必须 < XLink 16KB 上限 —— 超限不是丢
    # 一帧而是整会话断链）+ 订阅门控（没人看相机页就一帧不推）+ 采集器
    # 纯函数（JPEG 解析/切帧/双后端命令行）+ **三端组帧器一致**（桌面端与
    # 网页端各 import 自己那份，喂同一串分片断言结果相同）。
    results.append(("相机", run([py, "tests/test_camera.py"],
                                 "相机（camera 节 + 分片协议 + 订阅门控 + "
                                 "三端组帧一致）")))
    # [相机 MAC/健康/重连 2026-11-20] 动态 IP 按 MAC 解析（ARP+网段探测）、
    # 相机进设备健康、断线重连开关/间隔（fail-closed 校验面）。
    results.append(("相机MAC重连", run([py, "tests/test_camera_mac.py"],
                                        "相机 MAC 解析/健康条目/重连语义")))
    # [放水模式 2026-11-18] 「能配」与「能跑」分成两个集合：auto 配置节
    # 接受并落盘 drain（第四个可配置模式），但 op=5/op=1 仍拒它（只能配、
    # 不能跑）。
    results.append(("放水模式", run([py, "tests/test_auto_drain_mode.py"],
                                    "放水模式（第四个可配置模式：能配不能跑）")))
    results.append(("三模式序列", run([py, "tests/test_auto_modes.py"],
                                      "自动三模式（阀相/泵相/超时报警/闸门）")))
    results.append(("三模式端到端", run([py, "tests/test_sim_e2e_auto.py"],
                                        "自动三模式模拟器端到端（全栈循环 + 无泄漏）")))
    results.append(("入库触发", run([py, "tests/test_history_trigger.py"],
                                    "历史入库触发方式（持续采集 / 仅自动运行）")))
    results.append(("自动循环", run([py, "tests/test_auto_cycle.py"],
                                     "自动循环模式（压差/定时换相 + 阶段记忆）")))
    # [参数矩阵 2026-11-27 用户「自动循环各种参数都要测试跑一遍」] 逐参数
    # 断言**实际动作**（设备组合/转速/方向/加速度/六触发/阀复位/钳位/序号）。
    results.append(("自动循环参数矩阵",
                    run([py, "tests/test_auto_param_matrix.py"],
                        "自动循环逐参数执行矩阵", timeout=600)))    # [嵌套组合 2026-11-27] 递归展开 + 契约 + 状态机。
    results.append(("自动循环嵌套组合",
                    run([py, "tests/test_auto_nested_group.py"],
                        "嵌套组合（展开/契约/推进）")))    # [组合形态全矩阵 2026-11-27] 10 类形态各推进一遍（含三层嵌套/
    # 反复遍数/组内重复/改名/重名后缀/暂停续跑）。
    results.append(("自动循环组合矩阵",
                    run([py, "tests/test_auto_nested_matrix.py"],
                        "组合形态全矩阵（10 类）")))
    # [顶层循环次数 2026-11-27] auto.cycle 的 topRepeat（0=一直循环 /
    # N=展开计划跑完 N 轮自动停止）：配置面三条 SET 路径 + 执行面计数/
    # 到点停/暂停保留/快照口径 + 状态键 autoRound/autoRoundTotal。
    results.append(("自动循环顶层次数",
                    run([py, "tests/test_auto_top_repeat.py"],
                        "顶层循环次数（topRepeat：一直循环/指定轮数）",
                        timeout=300)))
    # [停止＝暂停 2026-11-19] 用户口径「停止保留状态/启动继续运行（定时器
    # 也要保留）/按复位才清零」+「续跑时先等阀到位再按配置启泵」+ 实体复位
    # 按钮同样清零 ⇒ 进正式门禁（不进门的锁等于没有锁）。
    results.append(("停止即暂停", run([py, "tests/test_auto_pause_resume.py"],
                                       "停止＝暂停/启动＝续跑/复位＝清零"
                                       "（含续跑等阀到位 + 实体复位）")))
    # [换相策略 2026-11-14] 换相（阀门切换）期间搅拌泵/进水泵怎么处理：
    # 先停再启 / 保持上一模式状态 / 直接切到当前模式状态（逐泵可配）。
    results.append(("换相策略", run([py, "tests/test_auto_switch_policy.py"],
                                     "换相策略（逐泵：先停再启/保持/直切）")))
    # [远端配置持久化 2026-11-14] 远端设置存在**服务器端** ⇒ 重启（重新
    # load 配置）后还在（本机设置另在客户端存，另有客户端回归锁）。
    results.append(("远端配置持久化", run(
        [py, "tests/test_config_persist.py"],
        "远端配置持久化（服务器端 · 重启后仍在）", timeout=120)))
    # [自动模式全场景 2026-11-14] 12 组动作矩阵 + 6 组端到端换相行为。
    results.append(("自动模式全场景", run([py, "tests/test_auto_scenarios.py"],
                                         "自动模式全场景（策略矩阵 + 端到端换相）",
                                         timeout=600)))
    results.append(("全流程模拟", run([py, "tests/test_sim_full_flow.py"],
                                      "控制全流程模拟（启动/停止/急停/复位/报警锁存）")))
    results.append(("浊度逐台", run([py, "tests/test_turbidity_per_meter.py"],
                                     "两台浊度独立开关（逐台门控/兼容键）")))
    results.append(("RTU 帧解析", run([py, "tests/test_rtu_framing.py"],
                                       "RTU 帧解析（模拟从站）")))
    # [变频器链路韧性 2026-11-18] 板端实锅：链路位错 → 设备回「数据错」帧
    # → 异常帧必须重试（重发即恢复）+ 单组失败不放弃整拍（组级容错）。
    results.append(("变频器链路韧性",
                    run([py, "tests/test_vfd_link_resilience.py"],
                        "变频器链路韧性（异常帧重试 + 组级容错）")))
    # [报警历史 2026-11-17] 健康边沿入 alarms 表 + 0x011A 取数
    # （报警页改为向服务端取数 ⇒ 客户端重启不丢、两端同一份数据）。
    results.append(("报警历史", run([py, "tests/test_alarm_history.py"],
                                     "报警历史（健康边沿入库 + 0x011A 取数）")))
    # [主页概览数据面 2026-11-19] 主页新加「阶段信息行 + 最近报警卡」⇒
    # 服务端状态快照（0x0105）必须真的带上 lastAlarm（空=null / 最近一条）
    # 与自动阶段计时字段；漏一个 ⇒ 界面恒显「--」而没人发现。
    results.append(("主页概览数据面", run([py, "tests/test_home_overview.py"],
                                          "主页概览（lastAlarm + 阶段计时字段）")))
    # [重启裁剪回归 2026-11-19] close→open_for 重建后的「残留维护唤醒」
    # 曾在每次启动时静默删掉最旧 ~1000 行历史（E2E 抓到的数据丢失级实锅；
    # 修法：_start_maintenance 前 clear wake + 裁剪入口按闸门值复核）。
    results.append(("重启裁剪回归", run([py, "tests/test_store_restart_trim.py"],
                                        "重启/重开库不得删历史行（残留唤醒回归）")))
    # [容量限额 2026-11-21 补进门禁] 这份用例**此前从未进过 run_all**
    # （孤儿测试）⇒ 口径随实现演进（裁剪从同步改成后台线程）后它就红了，
    # 没人发现 ⇒「不进门禁的锁等于没有锁」。本轮修好口径（补等后台裁剪
    # 收敛的等待窗口 + 修正 WAL 模式下「物理 ≥ 逻辑」的过时判据）并**注册
    # 进门禁**：限额是给现场守磁盘的硬需求（ring/rotate/stop 三策略），
    # 它必须一直在门禁里盯着。
    results.append(("容量限额", run([py, "tests/test_store_limit.py"],
                                     "落盘容量限额（ring/rotate/stop 三策略）")))
    # [空闲会话心跳回归 2026-11-19] 服务端对客户端心跳「不必回」的竞态曾让
    # 只订事件流的空闲客户端每 ~6-14s 被自己的静默看门狗误杀（E2E 抓到告警
    # 帧被订阅空窗吞掉；修法：收到心跳回一帧）。
    results.append(("空闲会话心跳", run([py, "tests/test_idle_session_heartbeat.py"],
                                        "只订事件流的空闲客户端不被静默超时误杀")))
    # [事件循环时延 2026-11-14] 自动控制应答不能被落盘/慢查询/容量维护顶掉
    # （WAL 检查点内联写路径 + 同会话请求串行 await + 容量维护同步做，
    # 三个病根都已根修）。
    results.append(("事件循环时延", run([py, "tests/test_event_loop_lag.py"],
                                         "自动控制时延（落盘压力 + 慢查询并发）",
                                         timeout=300)))
    results.append(("自动控制时延矩阵", run([py, "tests/test_auto_control_latency.py"],
                                             "自动控制时延矩阵（8 种负载逐个打）",
                                             timeout=600)))
    # [服务端泄漏门 2026-11-14] tracemalloc 堆级判据 × 5 场景（会话生命周期/
    # 自动模式/查询导出/落盘维护/告警事件）× 3 轮；自检开关 HSF_LEAK_INJECT=1
    # 会**故意注入泄漏**用来 A/B 实证这道门真能抓漏（不会红的门等于没门）。
    results.append(("服务端泄漏门", run([py, "tests/test_leak_gate.py"],
                                         "服务端内存泄漏门（5 场景 × 3 轮 · 堆级）",
                                         timeout=900)))
    # [双端对照 2026-11-13] 自管两端 sim（探活 6100/6200，在跑复用、不在跑自己起），
    # 与外部常驻 sim 共存；测试自己起的进程自己收尾。
    results.append(("双端协议面对照", run([py, "tests/test_server_alignment.py"],
                                          "C/PY 服务端协议面对照（19 命令/10 主题/配置/错误口径）",
                                          timeout=420)))

    # [2026-11-22 孤儿测试收编] 下面 5 个此前从不在 run_all 注册，长期腐化：
    # 实测 login_policy 还断言登录策略拒绝码为 BAD_USER(1)（产品 11-09 已改
    # REJECTED(4)）、dual_485 还断言设备名「浊度仪%d」（产品已改「进水/出水
    # 浊度仪」）——都是「不进门禁就随实现演进烂掉」的活证据，本轮已订正
    # 口径并逐个单跑确认全绿（5/5）才收编进来。
    results.append(("登录策略落地", run([py, "tests/test_login_policy_enforce.py"],
                                        "登录策略三键：总数上限/匿名闸/同账并发 + REJECTED 拒绝码")))
    results.append(("权限矩阵", run([py, "tests/test_permissions.py"],
                                  "四档角色权限矩阵（只读/操作者/管理员/拒绝）",
                                  timeout=180)))
    results.append(("自动报警发布", run([py, "tests/test_auto_alarm_publish.py"],
                                      "AUTO_ALARM 发布链路（一帧一条 + 入库）")))
    results.append(("双路485配置", run([py, "tests/test_dual_485.py"],
                                     "两路独立 485 配置 + 健康表地址文案")))
    results.append(("历史分页存储层", run([py, "tests/test_store_page.py"],
                                      "历史分页存储层（query_history 锚点/beforeTs 口径）")))
    # [历史压力1/2 两列 2026-11-23] 模拟量按名字找通道 → 快照成品键 →
    # store 线上列（末尾追加）→ 显示表达式（NULL→"--"）整条链路。
    results.append(("历史压力两列", run([py, "tests/test_history_pressure_cols.py"],
                                     "历史压力1/压力2（取值/valid 闸/改通道/落库）")))
    # 元门禁放最后：此时上面所有 test_*.py 都已注册，扫差集才能全绿。
    results.append(("孤儿测试元门禁", run([py, "tests/test_no_orphans.py"],
                                     "所有 test_*.py 都注册进 run_all（防静默腐化）")))

    if args.units:
        return _summary(results)

    if not port_free(args.port):
        print("[ERROR] 端口 %d 已被占用——先停掉残留的服务端" % args.port)
        print("        Windows: netstat -ano | findstr :%d  → taskkill /F /PID <pid>"
              % args.port)
        return 2

    # [stdout 必须落文件，不能用没人读的管道 —— 2026-11-09 根修]
    # 原实现 `stdout=subprocess.PIPE` 且全流程**从不读**它：harness 打满
    # 64KB（Windows 匿名管道缓冲）后 write() 阻塞 ⇒ 服务端协程被卡住 ⇒
    # 客户端请求超时 ⇒ 端到端套件出现"偶发 no response / reverse=None"
    # 的假红（单独手起 harness 全绿、经 run_all 跑就红几项，实测 74/5 vs
    # 79/0）。日志量越大越容易触发（本次运行日志类别改动又增加了输出）。
    # 落文件即可：既不会阻塞，又保留现场（跑挂时可直接 tail）。
    # [落临时目录，不落仓库] 之前落在 tests/harness.out ⇒ 每次跑都多一个
    # 未跟踪文件污染工作区。改到临时目录（跑完由临时目录自己回收）。
    harness_log_path = os.path.join(tempfile.gettempdir(),
                                    "hsf_harness_%d.out" % os.getpid())
    harness_log = open(harness_log_path, "wb")
    harness = subprocess.Popen(
        [py, "tests/harness.py", "--port", str(args.port), "--seconds", "300"],
        cwd=ROOT, stdout=harness_log, stderr=subprocess.STDOUT)
    try:
        # 等服务端就绪（读 harness 打印的 PORT 行）
        ready = False
        deadline = time.time() + 15
        while time.time() < deadline:
            if harness.poll() is not None:
                break
            if not port_free(args.port):
                ready = True
                break
            time.sleep(0.2)
        if not ready:
            print("[ERROR] 假设备服务端未能就绪")
            return 2
        time.sleep(0.5)

        results.append(("变频器全操作", run(
            [py, "tests/test_vfd_ops.py", "--port", str(args.port)],
            "变频器全操作（66 项）")))
        results.append(("IO 模块", run(
            [py, "tests/test_io_modules.py", "--port", str(args.port)],
            "数字量 / 模拟量 / 健康（68 项）")))
    finally:
        harness.terminate()
        try:
            harness.wait(timeout=5)
        except subprocess.TimeoutExpired:
            harness.kill()
        try:
            harness_log.close()
        except OSError:
            pass

    return _summary(results)


def _summary(results):
    print("\n" + "=" * 70)
    print("  汇总")
    print("=" * 70)
    for label, ok in results:
        print("  [%s] %s" % ("PASS" if ok else "FAIL", label))
    failed = [label for label, ok in results if not ok]
    print("\n  %d/%d 组通过" % (len(results) - len(failed), len(results)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
