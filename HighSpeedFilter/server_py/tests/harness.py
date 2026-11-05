# -*- coding: utf-8 -*-
"""测试装置入口：起一份全假设备服务端，供外部脚本连测。

    python tests/harness.py --port 6199 [--seconds 120]
"""
import argparse, asyncio, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hsfsrv import auth as A, config as C
from fake_devices import build_fake_app, serve

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=6199)
    ap.add_argument("--seconds", type=int, default=120)
    args = ap.parse_args()
    cfg = C.Config()
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_run")
    cfg.resolve_paths(base)
    cfg.config_path = os.path.join(base, "cfg", "x.json")
    cfg.users_path = os.path.join(base, "cfg", "users.json")
    cfg.port = args.port
    cfg.enable_serial = False
    users = A.UserStore(cfg.users_path)
    users.create_defaults()
    log = lambda m: print("[srv]", m, flush=True)

    async def run():
        business = build_fake_app(cfg, log=log)
        server = await serve(business, cfg, users, log)
        print("PORT", server.port, flush=True)
        await asyncio.sleep(args.seconds)
    asyncio.run(run())

if __name__ == "__main__":
    main()
