# -*- coding: utf-8 -*-
"""
SWD RAM 读写测速脚本（基于 OpenOCD / CMSIS-DAP）
================================================

通过 OpenOCD 的 telnet 接口，对目标 MCU 的 RAM 执行 load_image / dump_image，
统计不同 SWD 时钟、不同块大小下的读写吞吐率（MB/s），并可选读回校验数据。

依赖: Python 3.8+，仅标准库。

用法示例
--------
  # 自动启动 OpenOCD（默认 cmsis-dap + stm32h7x 配置）
  python swd_ram_bench.py

  # 指定时钟序列、块大小、平均轮数
  python swd_ram_bench.py --speed 1000,2000,4000,8000,10000 --sizes 4k,16k,64k --rounds 5

  # 指定起始地址（如 STM32H7 的 AXI SRAM 0x24000000）
  python swd_ram_bench.py --addr 0x24000000

  # 连接已经手动启动的 OpenOCD（telnet 4444），不自行启动
  python swd_ram_bench.py --attach

注意事项
--------
* 测速前目标必须处于 halt 状态（脚本会自动 halt）；CPU halt 后 0x20000000 等
  RAM 即可通过 AHB-AP 直接访问，无需目标运行固件。
* 测速会覆盖起始地址处的 RAM 内容，恢复运行时请留意（例如栈所在区域）。
* 测速完成后目标保持 halt；如需恢复运行，在 OpenOCD telnet 中执行 resume。
* 若固件使能了独立看门狗(IWDG)且未设置调试冻结，halt 期间目标可能被复位，
  表现为数据校验失败或读写异常。
* 脚本默认把 0x20000000 视为 DTCM（128KB），超出会自动裁剪，用 --no-limit-check 关闭。
"""

import argparse
import os
import random
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Windows 控制台可能不是 UTF-8，统一输出编码
try:
    if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_OPENOCD = r"C:\MySoftware\openocd-1adf0ee-i686-w64-mingw32\bin\openocd.exe"

# STM32H7 常见 RAM 区域（起始, 结束, 名称）
KNOWN_REGIONS = [
    (0x20000000, 0x20020000, "DTCM (128KB)"),
    (0x24000000, 0x24080000, "AXI SRAM (512KB)"),
    (0x30000000, 0x30080000, "D2 SRAM1/2/3 (512KB)"),
    (0x38000000, 0x38010000, "D3 SRAM4 (64KB)"),
]


class OpenOCDTelnet:
    """OpenOCD telnet 客户端：应答 IAC 协商、去除回显与提示符。"""

    def __init__(self, host, port, timeout=10.0):
        self.timeout = timeout
        self.pending = b""  # 未消费的原始字节（可能是不完整的 IAC 序列）
        self.data = b""    # 已解析出的文本
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._read_until_prompt()  # 排空启动横幅

    def _parse(self, data):
        """解析原始字节：返回 (文本, 需回复的协商字节, 未消费的尾部)。"""
        out = bytearray()
        replies = bytearray()
        i, n = 0, len(data)
        while i < n:
            if data[i] != 0xFF:
                out.append(data[i])
                i += 1
                continue
            if i + 1 >= n:
                return bytes(out), bytes(replies), data[i:]  # 悬空 0xFF，等待更多数据
            cmd = data[i + 1]
            if not (0xF0 <= cmd <= 0xFF):
                out.append(data[i])
                i += 1
                continue
            if cmd in (0xFB, 0xFD):  # WILL / DO -> 回 WONT / DONT
                if i + 2 >= n:
                    return bytes(out), bytes(replies), data[i:]
                opt = data[i + 2]
                replies += b"\xff" + (b"\xfc" if cmd == 0xFB else b"\xfe") + bytes([opt])
                i += 3
            elif cmd == 0xFA:  # SB ... SE：跳过到 SE
                j = i + 2
                while j + 1 < n and not (data[j] == 0xFF and data[j + 1] == 0xF0):
                    j += 1
                if j + 1 < n:
                    i = j + 2
                else:
                    return bytes(out), bytes(replies), data[i:]
            else:  # 其余 IAC 命令（IP/DM/AYT 等）直接跳过
                i += 2
        return bytes(out), bytes(replies), b""

    def _read_until_prompt(self, timeout=None):
        """读取数据直到出现 OpenOCD 提示符 '> '，同时应答协商。"""
        timeout = timeout or self.timeout
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(4096)
            except (socket.timeout, OSError):
                break
            if not chunk:
                break
            raw = self.pending + chunk
            text, replies, self.pending = self._parse(raw)
            if replies:
                self.sock.sendall(bytes(replies))
            self.data += text
            if self.data.rstrip(b" \r\n").endswith(b">"):
                break
        text = self.data.decode("utf-8", errors="replace")
        self.data = b""
        return text

    def cmd(self, cmd_str, timeout=60.0):
        """发送命令并返回响应文本（已去除回显与提示符）。"""
        self.sock.sendall(cmd_str.encode("ascii") + b"\n")
        text = self._read_until_prompt(timeout)
        # 去除命令回显：OpenOCD telnet 会回显输入的命令行，通常是响应的第一行
        text = text.lstrip("\r\n")
        for prefix in (cmd_str + "\r\n", cmd_str + "\n", cmd_str + "\r"):
            if text.startswith(prefix):
                text = text[len(prefix):]
                break
        else:
            if text.startswith(cmd_str):
                text = text[len(cmd_str):].lstrip("\r\n")
        # 去除末尾提示符 ">"
        text = text.rstrip()
        if text.endswith(">"):
            text = text[:-1].rstrip()
        return text

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def parse_int_list(s):
    out = []
    for x in s.split(","):
        x = x.strip()
        if x:
            out.append(int(x, 0))
    return out


def parse_size(s):
    s = s.strip().lower()
    mult = 1
    if s.endswith("k"):
        mult = 1024
        s = s[:-1]
    elif s.endswith("m"):
        mult = 1024 * 1024
        s = s[:-1]
    v = int(s, 0)
    if v <= 0:
        raise ValueError("无效大小: %r" % s)
    return v * mult


def check_region(addr, size, no_limit_check):
    """若地址落在已知 RAM 区域内且超出范围，则裁剪（或忽略限制）。"""
    for start, end, name in KNOWN_REGIONS:
        if start <= addr < end:
            if addr + size > end:
                if no_limit_check:
                    print("[警告] %#x + %d 超出 %s 范围 %#x~%#x，已忽略限制继续" % (addr, size, name, start, end))
                    return size
                new_size = end - addr
                print("[警告] %#x + %d 超出 %s 范围 %#x~%#x，已裁剪为 %d 字节" % (addr, size, name, start, end, new_size))
                return new_size
            return size
    return size


def is_openocd_error(resp):
    """OpenOCD 命令失败的特征：'error' 或 Tcl 的 invalid/unknown command。"""
    r = resp.lower()
    return ("error" in r or "invalid command name" in r
            or "unknown command" in r or "unknown option" in r)


def gen_pattern(size, seed):
    rng = random.Random(seed)
    return bytes(rng.getrandbits(8) for _ in range(size))


def tail_file(path, n=2500):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")[-n:]
    except OSError:
        return "(无日志)"


def spawn_openocd(args, speed_list, workdir):
    exe = Path(args.openocd).resolve()
    if not exe.is_file():
        raise FileNotFoundError(
            "找不到 OpenOCD 可执行文件: %s\n请用 --openocd 指定路径，或用 --attach 连接已运行的实例" % exe)
    root = exe.parent.parent
    iface = root / "share" / "openocd" / "scripts" / "interface" / args.interface
    tgt = root / "share" / "openocd" / "scripts" / "target" / args.target
    if not iface.is_file():
        raise FileNotFoundError("找不到 interface 配置: %s" % iface)
    if not tgt.is_file():
        raise FileNotFoundError("找不到 target 配置: %s" % tgt)

    cmd = [
        str(exe),
        "-f", str(iface),
        "-c", "telnet_port %d" % args.telnet_port,
        "-c", "tcl_port %d" % args.tcl_port,
        "-c", "transport select %s" % args.transport,
        "-c", "adapter speed %d" % speed_list[0],
        "-f", str(tgt),
    ]
    if getattr(args, "max_buf_size", 0) > 0:
        cmd.append("-c")
        cmd.append("adapter max_buf_size %d" % args.max_buf_size)
    cmd.append("-c")
    cmd.append("init")
    log_path = workdir / "openocd.log"
    print("启动 OpenOCD:")
    print("  " + " ".join(cmd))
    with open(log_path, "w", encoding="utf-8", errors="replace") as logf:
        proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, cwd=str(workdir))

    deadline = time.time() + 20
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("OpenOCD 启动后立即退出，日志:\n" + tail_file(log_path))
        try:
            s = socket.create_connection(("127.0.0.1", args.telnet_port), timeout=0.3)
            s.close()
            return proc
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("OpenOCD telnet 端口 %d 等待超时，日志:\n%s" % (args.telnet_port, tail_file(log_path)))


def run_benchmark(tel, args, speed_list, sizes, addr, workdir):
    results = []
    for speed in speed_list:
        tel.cmd("adapter speed %d" % speed)
        m = re.search(r"adapter speed:\s*(\d+)\s*kHz", tel.cmd("adapter speed"), re.I)
        note = "（确认 %s kHz）" % m.group(1) if m else ""
        print("\n===== SWD 时钟 %d kHz %s =====" % (speed, note))
        print("  %8s %10s %10s %9s %9s %6s" % ("size(B)", "写avg(ms)", "读avg(ms)", "写KiB/s", "读KiB/s", "校验"))
        for size in sizes:
            pattern = gen_pattern(size, args.seed)
            wf = workdir / "w.bin"
            rf = workdir / "r.bin"
            wf.write_bytes(pattern)

            w_times, r_times = [], []
            verify_ok = True
            for r in range(args.rounds):
                tel.cmd("halt")  # 保持 halt（顺带暴露目标被看门狗复位的情况）

                t0 = time.perf_counter()
                resp = tel.cmd('load_image "%s" %#x' % (wf.as_posix(), addr))
                t1 = time.perf_counter()
                if is_openocd_error(resp):
                    raise RuntimeError("load_image 失败(%#x): %s" % (addr, resp[:200]))
                w_times.append(t1 - t0)
                if args.verbose:
                    m = re.search(r"downloaded \d+ bytes in [\d.]+s \(([\d.]+) KiB/s\)", resp)
                    if m:
                        print("    [轮次%d] OpenOCD 自报写速度: %s KiB/s" % (r + 1, m.group(1)))

                rf.unlink(missing_ok=True)
                t0 = time.perf_counter()
                resp = tel.cmd('dump_image "%s" %#x 0x%x' % (rf.as_posix(), addr, size))
                t1 = time.perf_counter()
                if is_openocd_error(resp):
                    raise RuntimeError("dump_image 失败(%#x): %s" % (addr, resp[:200]))
                r_times.append(t1 - t0)

                if not args.no_verify:
                    if not rf.exists() or rf.read_bytes() != pattern:
                        verify_ok = False
                        print("    [轮次%d] 数据校验失败 @ size=%d" % (r + 1, size))

            w_avg = sum(w_times) / len(w_times)
            r_avg = sum(r_times) / len(r_times)
            w_kibs = size / w_avg / 1024
            r_kibs = size / r_avg / 1024
            results.append({"speed": speed, "size": size, "w_kibs": w_kibs, "r_kibs": r_kibs,
                            "w_avg_ms": w_avg * 1e3, "r_avg_ms": r_avg * 1e3, "verify": verify_ok})
            print("  %8d %10.2f %10.2f %9.1f %9.1f %6s" % (
                size, w_avg * 1e3, r_avg * 1e3, w_kibs, r_kibs, "OK" if verify_ok else "FAIL"))
    return results


def print_summary(results):
    if not results:
        return
    best_w = max(results, key=lambda x: x["w_kibs"])
    best_r = max(results, key=lambda x: x["r_kibs"])
    print("\n===== 汇总 =====")
    print("  最高写: %.1f KiB/s  @ %d kHz, 块 %d B" % (best_w["w_kibs"], best_w["speed"], best_w["size"]))
    print("  最高读: %.1f KiB/s  @ %d kHz, 块 %d B" % (best_r["r_kibs"], best_r["speed"], best_r["size"]))

    # 通用饱和检测：按写速率峰值所在档位判断——若峰值档远低于最高时钟档，
    # 说明速率在更低的时钟就封顶（DAP 固件/USB 瓶颈），再提高时钟无意义。
    def max_w(s):
        return max((r["w_kibs"] for r in results if r["speed"] == s), default=0.0)

    speeds = sorted({r["speed"] for r in results})
    peak_speed = max(speeds, key=max_w)
    peak_val = max_w(peak_speed)
    hi = [s for s in speeds if s > peak_speed]
    if hi and peak_val > 0:
        hi_clamped = [s for s in hi if max_w(s) < peak_val * 1.10]
        if hi_clamped:
            print("\n[提示] 写速率在 %d kHz 即封顶（%.1f KiB/s），更高时钟档 %s kHz 速率未再提升，"
                  "瓶颈疑似在 DAP 固件/USB 传输而非 SWCLK 频率。" % (peak_speed, peak_val, hi_clamped))


def main():
    ap = argparse.ArgumentParser(
        description="SWD RAM 读写测速脚本（基于 OpenOCD / CMSIS-DAP）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="示例:\n"
               "  python swd_ram_bench.py\n"
               "  python swd_ram_bench.py --speed 1000,4000,8000,10000 --sizes 4k,16k,64k --rounds 5 --addr 0x24000000\n"
               "  python swd_ram_bench.py --addr 0x20000000 --attach\n")
    ap.add_argument("--openocd", default=os.environ.get("OPENOCD", DEFAULT_OPENOCD),
                    help="OpenOCD 可执行文件路径")
    ap.add_argument("--interface", default="cmsis-dap.cfg", help="interface 配置文件名")
    ap.add_argument("--target", default="stm32h7x.cfg", help="target 配置文件名")
    ap.add_argument("--transport", choices=["swd", "jtag"], default="swd")
    ap.add_argument("--attach", action="store_true", help="连接已运行的 OpenOCD，不自行启动")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--telnet-port", type=int, default=4444)
    ap.add_argument("--tcl-port", type=int, default=6666)
    ap.add_argument("--speed", default="1000,2000,4000,8000,16000,24000,32000,40000,50000",
                    help="SWD 时钟(kHz)序列，逗号分隔。注意 stm32h7x.cfg 初始限制 4000 kHz，init 后脚本会逐档覆写")
    ap.add_argument("--sizes", default="4096,16384,65536", help="RAM 读写块大小(字节)，逗号分隔，支持 k/M 后缀")
    ap.add_argument("--rounds", type=int, default=5, help="每个(时钟,块大小)组合的测量轮数")
    ap.add_argument("--addr", default="0x24000000", help="RAM 起始地址（16 进制）。STM32H7 AXI SRAM: 0x24000000 (512KB)，DTCM: 0x20000000 (128KB)")
    ap.add_argument("--seed", type=int, default=0x5A, help="写数据伪随机种子")
    ap.add_argument("--no-verify", action="store_true", help="跳过读回数据校验")
    ap.add_argument("--no-limit-check", action="store_true", help="不检查地址是否超出已知 RAM 区域")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--keep-tmp", action="store_true", help="保留临时文件")
    ap.add_argument("--max-buf-size", type=int, default=0,
                    help="OpenOCD adapter max_buf_size（限制单包/单次传输字节数，0=不限制）")
    ap.add_argument("--timeout", type=float, default=30.0, help="命令超时(秒)")
    args = ap.parse_args()

    speed_list = parse_int_list(args.speed)
    sizes = [parse_size(s) for s in args.sizes.split(",") if s.strip()]
    addr = int(args.addr, 0)
    if addr % 4:
        print("[警告] 起始地址 %#x 非 4 字节对齐，测速结果可能不准确" % addr)
    fixed = []
    for s in sizes:
        s = check_region(addr, s, args.no_limit_check)
        if s % 4:
            print("[警告] 块大小 %d 非 4 的倍数，已向下取整为 %d" % (s, s - s % 4))
            s -= s % 4
        if s > 0:
            fixed.append(s)
    sizes = list(dict.fromkeys(fixed))  # 裁剪后可能重复，去重
    if not speed_list or not sizes:
        print("[错误] 时钟序列或块大小为空")
        sys.exit(1)
    if args.rounds < 1:
        args.rounds = 1

    print("SWD RAM 测速配置:")
    print("  起始地址: %#x   时钟(kHz): %s   块大小(B): %s   轮数: %d   校验: %s" % (
        addr, speed_list, sizes, args.rounds, "开" if not args.no_verify else "关"))
    if args.max_buf_size > 0:
        print("  单包限制: adapter max_buf_size = %d 字节" % args.max_buf_size)

    workdir = Path(tempfile.mkdtemp(prefix="swd_bench_"))
    proc = None
    tel = None
    try:
        if args.attach:
            print("连接已运行的 OpenOCD (%s:%d) ..." % (args.host, args.telnet_port))
            try:
                tel = OpenOCDTelnet(args.host, args.telnet_port, args.timeout)
            except OSError as e:
                raise RuntimeError("无法连接 %s:%d（%s）。请先启动 OpenOCD，或去掉 --attach 让脚本自行启动。" % (
                    args.host, args.telnet_port, e))
        else:
            proc = spawn_openocd(args, speed_list, workdir)
            tel = OpenOCDTelnet("127.0.0.1", args.telnet_port, args.timeout)

        resp = tel.cmd("halt")
        low = resp.lower()
        if "halted" in low or "already" in low:
            print("目标已 halt:", " ".join(resp.split())[:80])
        elif is_openocd_error(resp):
            print("[警告] halt 响应异常: %s，尝试 reset halt ..." % " ".join(resp.split())[:120])
            resp = tel.cmd("reset halt")
            print("reset halt:", " ".join(resp.split())[:80])
            if is_openocd_error(resp):
                raise RuntimeError("halt/reset halt 均失败，请检查 SWD 连接与 target 配置:\n%s" % resp[:300])
        else:
            print("halt:", " ".join(resp.split())[:80])

        # 确认 target 已创建（防止配置遗漏导致所有命令 invalid）
        tlist = tel.cmd("targets")
        if "invalid command name" in tlist.lower() or not tlist.strip():
            raise RuntimeError("OpenOCD 没有可用的 target（请检查 target 配置文件）: %s" % tlist[:200])
        print("targets:", " ".join(tlist.split())[:80])

        resp = tel.cmd("mdw %#x 4" % addr)
        if is_openocd_error(resp):
            print("[警告] 无法读取 %#x: %s" % (addr, " ".join(resp.split())[:120]))
        else:
            print("内存 %#x 可访问:" % addr, " ".join(resp.split())[:80])

        results = run_benchmark(tel, args, speed_list, sizes, addr, workdir)
        print_summary(results)
    except RuntimeError as e:
        print("\n[错误] %s" % e)
        sys.exit(1)
    finally:
        if tel is not None:
            try:
                if proc is not None:
                    tel.cmd("shutdown")
                tel.close()
            except Exception:
                pass
        if proc is not None:
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
        if not args.keep_tmp:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
