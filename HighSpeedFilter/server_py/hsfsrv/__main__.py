# -*- coding: utf-8 -*-
"""高速过滤器服务端（Python 版）：入口。

对应 C 侧 HighSpeedFilter/server/main_server.c，装配顺序一致：
    配置 → 工业 IO（设备对象+采集任务）→ XLink 服务端（登录/路由/推送）
    → 事件循环

用法：
    python3 -m hsfsrv                         全设备（串口 + 三个 TCP 设备）
    python3 -m hsfsrv --no-serial             不碰串口（板上 demo 占用 ttyS3 时）
    python3 -m hsfsrv --port 6101             临时换端口（不写回配置）
    python3 -m hsfsrv --config /path/x.json   指定配置文件
    python3 -m hsfsrv --diag                  起服前先自检一轮设备连通性

Ctrl+C 退出（会向所有客户端发 Goodbye，对端立刻感知而非等超时）。
"""

import argparse
import asyncio
import signal
import sys

from . import app as app_mod
from . import auth as auth_mod
from . import config as config_mod
from . import proto
from . import xlink


def log(msg):
    """统一日志出口：带时间戳、行内即时刷出（管道/重定向下也可见）。

    [为什么 flush] 板上常以 `python3 -m hsfsrv > /tmp/x.log 2>&1 &` 跑，
    Python 默认块缓冲会让日志卡在 4~8KB 才落盘，排查时看不到现场。
    """
    import time
    stamp = time.strftime("%H:%M:%S")
    sys.stdout.write("[%s] %s\n" % (stamp, msg))
    sys.stdout.flush()


def parse_args(argv):
    ap = argparse.ArgumentParser(
        prog="hsfsrv",
        description="高速过滤器服务端（XLink over TCP，Python 版）")
    ap.add_argument("--port", type=int, default=None,
                    help="监听端口（缺省取配置文件，首次运行落盘 %d）"
                         % config_mod.DEFAULT_PORT)
    ap.add_argument("--config", default=None,
                    help="配置文件路径（缺省 <程序根>/cfg/hsf_server_py.json）")
    ap.add_argument("--no-serial", action="store_true",
                    help="跳过 485 串口装配（只跑 TCP 设备）")
    ap.add_argument("--no-io", action="store_true",
                    help="跳过全部设备装配（只起登录/协议链路，供联调）")
    ap.add_argument("--diag", action="store_true",
                    help="起服前先自检一轮设备连通性并打印结果")
    ap.add_argument("--no-auth", action="store_true",
                    help="关闭登录认证（仅限调试；生产禁用）")
    return ap.parse_args(argv)


def load_config(args):
    """装配配置：缺省路径 → 读文件 → 命令行覆盖。"""
    cfg = config_mod.Config()
    base_dir = args.config and _dir_of(args.config) or _program_root()
    if args.config:
        cfg.config_path = args.config
    cfg.resolve_paths(base_dir)
    if not cfg.load():
        log("配置读取失败（%s）——按缺省值启动" % cfg.config_path)
    if args.no_serial or args.no_io:
        cfg.enable_serial = False
        log("串口装配：跳过（%s）"
            % ("--no-io" if args.no_io else "--no-serial"))
    if args.no_io:
        cfg.enable_analog = False
        cfg.enable_digital = False
    if args.port:
        cfg.port = args.port
        log("端口取自命令行 %d（本次生效，不写配置）" % cfg.port)
    return cfg


def _dir_of(path):
    import os
    return os.path.dirname(os.path.abspath(path)) or "."


def _program_root():
    """程序根目录（server_py/ 的上一级，与 C 侧 cfg/ 同级口径）。

    [为什么不用 cwd] 服务端常被 systemd/nohup 从别的目录拉起；用 cwd 会让
    配置文件跑到别处，导致「明明改了配置却不生效」。按本文件位置反推最稳。
    """
    import os
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def setup_users(cfg):
    """装配账户库：落盘 JSON；空库时创建缺省账户并落盘。

    [root 单会话策略] 由 app.policy_blocks_login 在登录时强制，不在这里。
    """
    store = auth_mod.UserStore(cfg.users_path, log=log)
    if not store.load():
        log("账户库加载失败（%s）——继续以内存库启动" % cfg.users_path)
    if len(store) == 0:
        store.create_defaults()
        if store.save():
            log("缺省账户已创建（%s）：admin/admin 只读、root/root 可改"
                % cfg.users_path)
        else:
            log("缺省账户落盘失败（%s）——本次仍可用" % cfg.users_path)
    else:
        log("账户库已加载（%s，%d 账户）" % (cfg.users_path, len(store)))
    return store


async def probe_devices(cfg):
    """自检：逐个设备读一拍，打印连通性（--diag 用）。"""
    from . import devices
    from .modbus import TcpMaster
    log("=== 设备自检 ===")
    now = int(asyncio.get_event_loop().time() * 1000)

    if cfg.enable_serial and (cfg.enable_vfd or cfg.enable_temphum):
        from .modbus import RtuMaster
        if cfg.enable_vfd:
            m = RtuMaster(cfg.serial.port, cfg.serial.baudrate,
                          cfg.vfd_unit_id, cfg.serial.timeout_ms / 1000.0,
                          cfg.serial.retries, log)
            dev = devices.VfdM512(lambda: m, cfg.vfd_unit_id, log)
            try:
                m.open()
                await dev.poll(now)
                log("  变频器 %s 站%d：OK  runState=%d  outFreq=%.2fHz"
                    % (cfg.serial.port, cfg.vfd_unit_id, dev.run_state,
                       dev.output_freq_hz() or 0.0))
            except Exception as exc:                    # noqa: BLE001
                log("  变频器 %s 站%d：失败 %s"
                    % (cfg.serial.port, cfg.vfd_unit_id, exc))
            finally:
                await dev.close()
        if cfg.enable_temphum:
            m = RtuMaster(cfg.serial.port, cfg.serial.baudrate,
                          cfg.temphum_unit_id, cfg.serial.timeout_ms / 1000.0,
                          cfg.serial.retries, log)
            dev = devices.TempHum(lambda: m, cfg.temphum_unit_id, log)
            try:
                m.open()
                await dev.poll(now)
                log("  温湿度 %s 站%d：OK  %.1fC / %.1f%%RH"
                    % (cfg.serial.port, cfg.temphum_unit_id,
                       dev.temp_tenths() / 10.0, dev.hum_tenths() / 10.0))
            except Exception as exc:                    # noqa: BLE001
                log("  温湿度 %s 站%d：失败 %s"
                    % (cfg.serial.port, cfg.temphum_unit_id, exc))
            finally:
                await dev.close()

    if cfg.enable_analog:
        ai = cfg.analog
        m = TcpMaster(ai.host, ai.port, ai.unit_id, timeout=2.0, log=log)
        dev = devices.AnalogInput(m, ai.channels, log)
        try:
            await dev.poll(now)
            ch1 = dev.ma(0) if ai.channels else 0.0
            log("  模拟量 %s:%d：OK  8 路读取成功，CH1=%.3fmA"
                % (ai.host, ai.port, ch1))
        except Exception as exc:                        # noqa: BLE001
            log("  模拟量 %s:%d：失败 %s" % (ai.host, ai.port, exc))
        finally:
            await m.close()

    if cfg.enable_digital:
        for dc in cfg.digital_modules:
            m = TcpMaster(dc.host, dc.port, dc.unit_id,
                          timeout=dc.timeout_s, log=log)
            dev = devices.DigitalIo(m, dc.name, log)
            try:
                await dev.poll(now)
                log("  %s %s:%d：OK  DI=0x%04X DO=0x%04X"
                    % (dc.name, dc.host, dc.port, dev.di_mask(), dev.do_mask()))
            except Exception as exc:                    # noqa: BLE001
                log("  %s %s:%d：失败 %s" % (dc.name, dc.host, dc.port, exc))
            finally:
                await m.close()
    log("=== 自检结束 ===")


async def run(cfg, args):
    """主流程：装配 → 起服 → 等退出信号。"""
    log("HsfServerPy: 启动（Python %d.%d）"
        % (sys.version_info[0], sys.version_info[1]))
    log("配置文件：%s" % cfg.config_path)

    if args.diag:
        await probe_devices(cfg)

    # ---- 业务层 ----
    business = app_mod.App(cfg, log=log)
    business.build_devices()
    # 恢复持久化的运行模式（老配置无该键时按手动，行为不变）。
    business.run_mode = cfg.run_mode(default=business.MODE_MANUAL)
    log("运行模式 = %s"
        % ("自动（远程控制禁用）" if business.is_auto() else "手动"))

    # ---- 账户库 ----
    users = setup_users(cfg)

    # ---- XLink 服务端 ----
    server = xlink.Server(cfg.port, users, log=log,
                          require_auth=not args.no_auth)
    business.attach(server)
    try:
        await server.start()
    except OSError as exc:
        log("监听 %d 端口失败：%s" % (cfg.port, exc))
        return 2

    # ---- 采集任务 ----
    await business.start_polling()

    log("XLink 服务端就绪（端口 %d，鉴权%s，遥测 %dms）"
        % (server.port, "关" if args.no_auth else "开", business.TELEMETRY_MS))
    log("设备：%s" % _device_summary(cfg))
    log("监听 0.0.0.0:%d —— Ctrl+C 退出" % server.port)

    # ---- 等退出信号 ----
    stop_event = asyncio.Event()

    def _request_stop(*_args):
        stop_event.set()

    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _request_stop)
        except (NotImplementedError, RuntimeError):
            # Windows 上 add_signal_handler 不支持：退化为 KeyboardInterrupt。
            pass

    try:
        await stop_event.wait()
    except asyncio.CancelledError:
        pass

    log("收到退出信号，收尾中…")
    await server.stop()
    await business.stop_polling()
    log("HsfServerPy: 已退出")
    return 0


def _device_summary(cfg):
    parts = []
    if cfg.enable_vfd:
        parts.append("变频器(%s站%d)" % (cfg.serial.port, cfg.vfd_unit_id))
    if cfg.enable_temphum:
        parts.append("温湿度(%s站%d)" % (cfg.serial.port, cfg.temphum_unit_id))
    if cfg.enable_analog:
        parts.append("模拟量(%s:%d)" % (cfg.analog.host, cfg.analog.port))
    if cfg.enable_digital:
        for dc in cfg.digital_modules:
            parts.append("%s(%s:%d)" % (dc.name, dc.host, dc.port))
    return "、".join(parts) if parts else "无（--no-io）"


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    cfg = load_config(args)
    try:
        # asyncio.run 而非 get_event_loop().run_until_complete：
        # 后者在 3.10+ 无运行中循环时发 DeprecationWarning，且行为随版本漂移。
        return asyncio.run(run(cfg, args))
    except KeyboardInterrupt:
        log("用户中断")
        return 0


if __name__ == "__main__":
    sys.exit(main())
