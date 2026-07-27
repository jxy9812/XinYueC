#!/usr/bin/env python3
"""
ftp_test_server.py
跨平台 FTP 测试服务器（纯标准库）
- 完整命令集：USER/PASS/PWD/CWD/CDUP/LIST/MLSD/PASV/PORT/EPSV/TYPE/RETR/STOR/APPE
                DELE/RMD/MKD/RNFR/RNTO/QUIT/ABOR/REST/SIZE/MDTM/FEAT/OPTS/SYST/NOOP
- UTF8 支持
- 鉴权（user=u1, pass=p1）
- 真实文件系统读写
"""

import os
import sys
import socket
import ssl
import threading
import time
import argparse

HOST = '127.0.0.1'
PORT = 2121
USER = 'u1'
PASS = 'p1'
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ftp_test_root')


def log(msg):
    print(f'[server] {msg}', flush=True)


def send_ctrl(sock, code, msg):
    """Send a single-line FTP response with proper CRLF."""
    line = f'{code} {msg}\r\n'
    sock.sendall(line.encode('utf-8'))
    log(f'>>> {code} {msg}')


class FtpSession:
    def __init__(self, conn, addr, tls_context=None):
        self.conn = conn
        self.addr = addr
        self.tls_context = tls_context
        self.cwd = '/'
        self.authenticated = False
        self.data_sock = None
        self.data_srv = None
        self.protect_data = False
        self.rest_offset = 0
        self.rename_from = None
        self.transfer_type = 'I'

    def real_path(self, path):
        """Resolve FTP path to (normalized_ftp_path, real_fs_path)."""
        if not path or path == '.':
            path = ''
        if path.startswith('/'):
            full = path
        else:
            full = self.cwd.rstrip('/') + '/' + path
        parts = []
        for p in full.split('/'):
            if p == '' or p == '.':
                continue
            elif p == '..':
                if parts:
                    parts.pop()
            else:
                parts.append(p)
        normalized = '/' + '/'.join(parts)
        real = os.path.join(ROOT, *parts) if parts else ROOT
        return normalized, real

    def setup_data_passive(self):
        """Create passive data listener, return port."""
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, 0))
        srv.listen(1)
        port = srv.getsockname()[1]
        self.data_srv = srv
        return port

    def accept_data(self, timeout=5.0):
        """Accept data connection from passive listener."""
        if not self.data_srv:
            return None
        self.data_srv.settimeout(timeout)
        try:
            self.data_sock, _ = self.data_srv.accept()
            if self.protect_data and self.tls_context:
                self.data_sock = self.tls_context.wrap_socket(
                    self.data_sock, server_side=True)
        except socket.timeout:
            log('data accept timeout')
            self.data_sock = None
        except (OSError, ssl.SSLError) as exc:
            log(f'data TLS accept failed: {exc}')
            self.data_sock = None
        finally:
            try:
                self.data_srv.close()
            except OSError:
                pass
            self.data_srv = None
        return self.data_sock

    def close_data(self):
        if self.data_sock:
            try:
                self.data_sock.close()
            except OSError:
                pass
            self.data_sock = None

    def handle(self):
        send_ctrl(self.conn, 220, 'Welcome to Python FTP Test Server (XinYueC XFtp E2E)')
        buf = b''
        while True:
            try:
                chunk = self.conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while b'\n' in buf:
                    line_bytes, buf = buf.split(b'\n', 1)
                    line = line_bytes.decode('utf-8', errors='replace').rstrip('\r')
                    if not line:
                        continue
                    log(f'<<< {line}')
                    if not self.process_command(line):
                        return
            except (ConnectionResetError, BrokenPipeError, OSError):
                break
        log('client disconnected')

    def process_command(self, line):
        parts = line.split(' ', 1)
        cmd = parts[0].upper()
        arg = parts[1].strip() if len(parts) > 1 else ''

        # ---- 鉴权命令 ----
        if cmd == 'USER':
            if arg == USER:
                send_ctrl(self.conn, 331, f'Password required for {arg}')
            else:
                send_ctrl(self.conn, 530, 'User not found')
            return True
        if cmd == 'PASS':
            if arg == PASS:
                self.authenticated = True
                send_ctrl(self.conn, 230, 'Login successful')
            else:
                send_ctrl(self.conn, 530, 'Login incorrect')
            return True
        if cmd == 'QUIT':
            send_ctrl(self.conn, 221, 'Goodbye')
            return False
        if cmd == 'AUTH':
            if arg.upper() != 'TLS' or not self.tls_context:
                send_ctrl(self.conn, 502, 'AUTH TLS not available')
                return True
            if isinstance(self.conn, ssl.SSLSocket):
                send_ctrl(self.conn, 503, 'Control channel is already protected')
                return True
            send_ctrl(self.conn, 234, 'AUTH TLS successful')
            try:
                self.conn = self.tls_context.wrap_socket(self.conn, server_side=True)
            except ssl.SSLError as exc:
                log(f'control TLS handshake failed: {exc}')
                return False
            return True

        if cmd == 'PBSZ':
            if isinstance(self.conn, ssl.SSLSocket):
                send_ctrl(self.conn, 200, 'PBSZ=0')
            else:
                send_ctrl(self.conn, 503, 'AUTH TLS required')
            return True
        if cmd == 'PROT':
            if not isinstance(self.conn, ssl.SSLSocket):
                send_ctrl(self.conn, 503, 'AUTH TLS required')
            elif arg.upper() == 'P':
                self.protect_data = True
                send_ctrl(self.conn, 200, 'Private data channel enabled')
            elif arg.upper() == 'C':
                self.protect_data = False
                send_ctrl(self.conn, 200, 'Clear data channel enabled')
            else:
                send_ctrl(self.conn, 536, 'Unsupported protection level')
            return True

        # ---- RFC 2389：FEAT/OPTS 必须允许认证前使用；SYST/NOOP 同理 ----
        if cmd == 'FEAT':
            feats = ['UTF8', 'MLSD', 'EPSV', 'EPRT', 'REST STREAM',
                     'TVFS', 'ABOR', 'SIZE', 'MDTM', 'AUTH TLS',
                     'PBSZ', 'PROT P']
            # 多行响应：每行以 CRLF 结尾，最后一行 "211 End" 也必须带 CRLF
            self.conn.sendall(b'211-Features:\r\n')
            for f in feats:
                self.conn.sendall(f' {f}\r\n'.encode('utf-8'))
            self.conn.sendall(b'211 End\r\n')
            log('>>> 211-Features...211 End')
            return True
        if cmd == 'OPTS':
            if arg.upper().startswith('UTF8'):
                send_ctrl(self.conn, 200, 'UTF8 set to on')
            else:
                send_ctrl(self.conn, 200, f'{cmd} ok')
            return True
        if cmd == 'SYST':
            send_ctrl(self.conn, 215, 'UNIX Type: L8')
            return True
        if cmd == 'NOOP':
            send_ctrl(self.conn, 200, 'NOOP ok')
            return True

        # ---- 以下需要已登录 ----
        if not self.authenticated:
            send_ctrl(self.conn, 530, 'Please login first')
            return True

        if cmd == 'PWD' or cmd == 'XPWD':
            send_ctrl(self.conn, 257, f'"{self.cwd}" is current directory')
        elif cmd == 'CWD':
            normalized, real = self.real_path(arg)
            if os.path.isdir(real):
                self.cwd = normalized
                send_ctrl(self.conn, 250, f'CWD successful: {normalized}')
            else:
                send_ctrl(self.conn, 550, f'{arg}: No such directory')
        elif cmd == 'CDUP':
            return self.process_command('CWD ..')
        elif cmd == 'TYPE':
            self.transfer_type = arg.upper()
            send_ctrl(self.conn, 200, f'Type set to {self.transfer_type}')
        elif cmd == 'EPSV':
            port = self.setup_data_passive()
            send_ctrl(self.conn, 229, f'Entering Extended Passive Mode (|||{port}|)')
            # FTPS 客户端会在数据命令前先完成 TLS 握手，必须先接受数据连接。
            if self.protect_data:
                self.accept_data()
            # 普通 FTP 保持惰性 accept，避免改变已有测试时序。
        elif cmd == 'PASV':
            port = self.setup_data_passive()
            p1, p2 = port >> 8, port & 0xFF
            h = HOST.replace('.', ',')
            send_ctrl(self.conn, 227, f'Entering Passive Mode ({h},{p1},{p2})')
            if self.protect_data:
                self.accept_data()
            # 普通 FTP 保持惰性 accept，避免改变已有测试时序。
        elif cmd == 'PORT':
            try:
                nums = [int(x) for x in arg.split(',')]
                ip = '.'.join(str(n) for n in nums[:4])
                port = (nums[4] << 8) | nums[5]
                self.close_data()
                self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.data_sock.connect((ip, port))
                send_ctrl(self.conn, 200, f'PORT command successful ({ip}:{port})')
            except Exception as e:
                send_ctrl(self.conn, 425, f'Cannot connect to {arg}: {e}')
                self.data_sock = None
        elif cmd == 'EPRT':
            # |proto|ip|port|  e.g. |1|127.0.0.1|5000|
            try:
                parts2 = arg.split('|')
                ip = parts2[2]
                port = int(parts2[3])
                self.close_data()
                self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.data_sock.connect((ip, port))
                send_ctrl(self.conn, 200, f'EPRT command successful ({ip}:{port})')
            except Exception as e:
                send_ctrl(self.conn, 425, f'Cannot connect: {e}')
                self.data_sock = None
        elif cmd == 'LIST' or cmd == 'NLST':
            self.do_list(arg, machine=False)
        elif cmd == 'MLSD':
            self.do_list(arg, machine=True)
        elif cmd == 'MLST':
            self.do_mlst(arg)
        elif cmd == 'RETR':
            self.do_retr(arg)
        elif cmd == 'STOR':
            self.do_stor(arg, append=False)
        elif cmd == 'APPE':
            self.do_stor(arg, append=True)
        elif cmd == 'DELE':
            normalized, real = self.real_path(arg)
            if os.path.isfile(real):
                os.remove(real)
                send_ctrl(self.conn, 250, f'{arg} deleted')
            else:
                send_ctrl(self.conn, 550, f'{arg}: No such file')
        elif cmd == 'MKD' or cmd == 'XMKD':
            normalized, real = self.real_path(arg)
            try:
                os.makedirs(real, exist_ok=False)
                send_ctrl(self.conn, 257, f'"{normalized}" created')
            except Exception as e:
                send_ctrl(self.conn, 550, f'{arg}: {e}')
        elif cmd == 'RMD' or cmd == 'XRMD':
            normalized, real = self.real_path(arg)
            try:
                os.rmdir(real)
                send_ctrl(self.conn, 250, f'{arg} removed')
            except Exception as e:
                send_ctrl(self.conn, 550, f'{arg}: {e}')
        elif cmd == 'RNFR':
            normalized, real = self.real_path(arg)
            if os.path.exists(real):
                self.rename_from = real
                send_ctrl(self.conn, 350, f'{arg}: ready for RNTO')
            else:
                send_ctrl(self.conn, 550, f'{arg}: No such file or directory')
        elif cmd == 'RNTO':
            if self.rename_from:
                normalized, real = self.real_path(arg)
                try:
                    os.rename(self.rename_from, real)
                    send_ctrl(self.conn, 250, 'Rename successful')
                except Exception as e:
                    send_ctrl(self.conn, 550, f'Rename failed: {e}')
                self.rename_from = None
            else:
                send_ctrl(self.conn, 503, 'RNFR required first')
        elif cmd == 'SIZE':
            normalized, real = self.real_path(arg)
            if os.path.isfile(real):
                send_ctrl(self.conn, 213, str(os.path.getsize(real)))
            else:
                send_ctrl(self.conn, 550, f'{arg}: No such file')
        elif cmd == 'MDTM':
            normalized, real = self.real_path(arg)
            if os.path.isfile(real):
                mtime = os.path.getmtime(real)
                ts = time.strftime('%Y%m%d%H%M%S', time.gmtime(mtime))
                send_ctrl(self.conn, 213, ts)
            else:
                send_ctrl(self.conn, 550, f'{arg}: No such file')
        elif cmd == 'REST':
            try:
                self.rest_offset = int(arg)
                send_ctrl(self.conn, 350, f'Restarting at {self.rest_offset}')
            except ValueError:
                send_ctrl(self.conn, 501, f'Invalid REST argument: {arg}')
        elif cmd == 'ABOR':
            self.close_data()
            send_ctrl(self.conn, 226, 'ABOR command successful')
        elif cmd in ('STRU', 'MODE', 'ALLO'):
            send_ctrl(self.conn, 200, f'{cmd} ok')
        else:
            send_ctrl(self.conn, 502, f'{cmd} not implemented')
        return True

    def do_list(self, arg, machine=False):
        # 惰性 accept：数据命令到达时才 accept 被动连接
        if self.data_srv and not self.data_sock:
            self.accept_data()
        path = arg
        # 跳过 -la 等选项
        if path.startswith('-'):
            parts = path.split(None, 1)
            path = parts[1] if len(parts) > 1 else ''
        normalized, real = self.real_path(path if path else '.')
        if not os.path.isdir(real):
            send_ctrl(self.conn, 550, f'{path or "."}: No such directory')
            return
        if not self.data_sock:
            send_ctrl(self.conn, 425, 'No data connection')
            return
        send_ctrl(self.conn, 150, 'Opening data connection for directory listing')
        try:
            entries = sorted(os.listdir(real))
            data = ''
            for name in entries:
                full = os.path.join(real, name)
                try:
                    stat = os.stat(full)
                except OSError:
                    continue
                if machine:
                    ftype = 'dir' if os.path.isdir(full) else 'file'
                    modify = time.strftime('%Y%m%d%H%M%S', time.gmtime(stat.st_mtime))
                    data += f'type={ftype};size={stat.st_size};modify={modify}; {name}\r\n'
                else:
                    is_dir = os.path.isdir(full)
                    perms = 'drwxr-xr-x' if is_dir else '-rw-r--r--'
                    nlink = 2 if is_dir else 1
                    size = stat.st_size
                    mtime = time.strftime('%b %d %H:%M', time.localtime(stat.st_mtime))
                    data += f'{perms} {nlink:>3} owner group {size:>8} {mtime} {name}\r\n'
            self.data_sock.sendall(data.encode('utf-8'))
        except Exception as e:
            log(f'LIST/MLSD error: {e}')
        finally:
            self.close_data()
        send_ctrl(self.conn, 226, 'Transfer complete')

    def do_mlst(self, arg):
        """RFC 3659 MLST: 单文件元信息查询，响应在 PI 上（不用数据通道）"""
        path = arg if arg else self.cwd
        normalized, real = self.real_path(path)
        if not os.path.exists(real):
            send_ctrl(self.conn, 550, f'{arg}: No such file or directory')
            return
        try:
            stat = os.stat(real)
            ftype = 'dir' if os.path.isdir(real) else 'file'
            modify = time.strftime('%Y%m%d%H%M%S', time.gmtime(stat.st_mtime))
            # RFC 3659: 250-Listing <name>\r\n 空格+facts+空格+name\r\n250 End
            self.conn.sendall(f'250-Listing {normalized}\r\n'.encode('utf-8'))
            self.conn.sendall(f' modify={modify};type={ftype};size={stat.st_size}; {os.path.basename(real)}\r\n'.encode('utf-8'))
            self.conn.sendall(b'250 End\r\n')
        except OSError as e:
            send_ctrl(self.conn, 550, f'{arg}: {e}')

    def do_retr(self, arg):
        if self.data_srv and not self.data_sock:
            self.accept_data()
        normalized, real = self.real_path(arg)
        if not os.path.isfile(real):
            send_ctrl(self.conn, 550, f'{arg}: No such file')
            return
        if not self.data_sock:
            send_ctrl(self.conn, 425, 'No data connection')
            return
        offset = self.rest_offset
        self.rest_offset = 0
        send_ctrl(self.conn, 150, f'Opening data connection for {arg} ({os.path.getsize(real)} bytes)')
        try:
            with open(real, 'rb') as f:
                if offset > 0:
                    f.seek(offset)
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    self.data_sock.sendall(chunk)
        except Exception as e:
            log(f'RETR error: {e}')
        finally:
            self.close_data()
        send_ctrl(self.conn, 226, 'Transfer complete')

    def do_stor(self, arg, append=False):
        if self.data_srv and not self.data_sock:
            self.accept_data()
        normalized, real = self.real_path(arg)
        if not self.data_sock:
            send_ctrl(self.conn, 425, 'No data connection')
            return
        send_ctrl(self.conn, 150, f'Opening data connection for {arg}')
        try:
            mode = 'ab' if append else 'wb'
            with open(real, mode) as f:
                while True:
                    chunk = self.data_sock.recv(8192)
                    if not chunk:
                        break
                    f.write(chunk)
        except Exception as e:
            log(f'STOR/APPE error: {e}')
        finally:
            self.close_data()
        send_ctrl(self.conn, 226, 'Transfer complete')


def main():
    parser = argparse.ArgumentParser(description='FTP test server for XFtp E2E')
    parser.add_argument('--daemon', action='store_true', help='run in background')
    parser.add_argument('--port', type=int, default=PORT)
    parser.add_argument('--tls', action='store_true', help='enable explicit FTPS')
    parser.add_argument('--cert', help='PEM certificate used by FTPS')
    parser.add_argument('--key', help='PEM private key used by FTPS')
    args = parser.parse_args()

    os.makedirs(ROOT, exist_ok=True)

    tls_context = None
    if args.tls:
        if not args.cert or not args.key:
            parser.error('--tls requires --cert and --key')
        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.minimum_version = ssl.TLSVersion.TLSv1_2
        tls_context.load_cert_chain(certfile=args.cert, keyfile=args.key)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, args.port))
    srv.listen(5)
    mode = 'Explicit FTPS' if tls_context else 'FTP'
    log(f'{mode} server listening on {HOST}:{args.port}, root={ROOT}')

    try:
        while True:
            conn, addr = srv.accept()
            log(f'connect from {addr}')
            t = threading.Thread(
                target=lambda c=conn, a=addr, t=tls_context: FtpSession(c, a, t).handle(),
                daemon=True
            )
            t.start()
    except KeyboardInterrupt:
        log('shutting down')
    finally:
        srv.close()


if __name__ == '__main__':
    main()
