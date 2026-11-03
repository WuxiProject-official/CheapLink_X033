# -*- coding: utf-8 -*-
"""
SWD RAM 读写测速脚本（基于 OpenOCD / CMSIS-DAP）
================================================

通过 OpenOCD 的 telnet 接口，对目标 MCU 的 RAM 执行 load_image / dump_image，
统计不同 SWD 时钟、不同块大小下的读写吞吐率（MB/s），并可选读回校验数据。

依赖: Python 3.8+，仅标准库。

作者
----

- rinscr3003
    - Copilot: Deepseek V4.1 Flash@Medium

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
* 脚本会在默认 cfg 下读 CPUID 校验目标内核：默认配置（stm32h7x.cfg）面向
  Cortex-M7（STM32H743/H750/H753），目标内核不是 Cortex-M7 时说明它不是
  默认配置支持的 MCU，会输出黄色警告（RAM 布局与初始化序列都不符预期）。
  用 --interface/--target 指定了别的 cfg 时本检查自动跳过，由你自行确认 cfg
  与目标匹配；也可用 --no-cpu-check 强制跳过。
* 换目标芯片时，请改本文件顶部的「默认目标配置」一节，那里集中了 cfg 文件名、
  RAM 起始地址与区域划分、支持的内核集合。
* 测速会覆盖起始地址处的 RAM 内容，恢复运行时请留意（例如栈所在区域）。
* 测速完成后目标保持 halt；如需恢复运行，在 OpenOCD telnet 中执行 resume。
* 若固件使能了独立看门狗(IWDG)且未设置调试冻结，halt 期间目标可能被复位，
  表现为数据校验失败或读写异常。
* 脚本默认把 0x20000000 视为 DTCM（128KB），超出会自动裁剪，用 --no-limit-check 关闭。

测量区间
--------
计时区间 = 发送 telnet 命令 -> 收到 OpenOCD 提示符，即端到端块传输耗时，包含：

  telnet 往返(约 0.5ms) + Tcl 解析 + OpenOCD 读写本地文件 + USB 传输
  + 探针固件 + SWD 时序 + 响应回传

因此这个数字**不能**直接换算成 SWCLK 能力：

* 每次 load_image/dump_image 有数 ms 的固定开销（与块大小无关），小块样本
  会被严重稀释：1KB 样本里固定开销占三分之一。
* 读路径还包含 dump_image 落盘（页缓存下可忽略，冷盘/网络盘上不可忽略）。

所以脚本按块大小做线性回归，同时给出「固定开销」「边际成本(µs/B)」和
「渐进 KiB/s」，只有渐进值才反映链路本身的能力。

统计方式
--------
* 每个(时钟,块大小)组合先跑 --warmup 轮预热，不计入统计（首轮稳定偏慢数毫秒）。
* 每轮样本都校验 OpenOCD 自报字节数与读回数据，不合格的样本直接丢弃并重测，
  而不是留在平均值里。重试预算受总尝试次数约束：至多 2N 次尝试（N=--rounds），
  因此保证至少 N 个有效样本，同时不会无限重测。
* 每个组合给出两个结果：
    - 中位结果 KiB/s：剔除离群后的可复现能力值，适合横向比较链路本身；
    - 用户感知 KiB/s（均值）：把偶发卡顿一并算进去，对应实际使用中体验到的速度。
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


# ===========================================================================
#  默认目标配置（STM32H743 系）—— 换目标时请改这一节
# ===========================================================================
# 下面这些常量都只对「本脚本默认使用的 interface/target 配置 + STM32H743 系」
# 这一组合成立。若你要测别的芯片，改这里即可，不必翻遍全文：
#
#   1) DEFAULT_INTERFACE / DEFAULT_TARGET：OpenOCD 的配置文件。
#      换成别的芯片时必须同时改这两个，否则 OpenOCD 会按 H7 的初始化序列
#      去访问目标，轻则读不到内存，重则误写寄存器。
#   2) DEFAULT_ADDR / KNOWN_REGIONS：目标 RAM 的起始地址与区域划分，
#      只影响地址范围检查与默认起始地址。
#   3) TARGET_CPU_PARTS：默认配置支持的 CPU 内核。目标内核不在这个集合里时，
#      说明不是默认配置支持的芯片，脚本会给出黄色警告（见 --no-cpu-check）。
#      注意这里只做「内核级」判定，不做具体型号判定：同一内核下可能有许多
#      型号，RAM 划分并不相同，所以内核匹配也不代表 RAM 布局匹配。
#
# 如果你用 --interface/--target 指定了别的 cfg，脚本会认为你清楚自己在测什么，
# 自动跳过这项检查（你有义务确认 cfg 与目标匹配）。
# ===========================================================================

DEFAULT_INTERFACE = "cmsis-dap.cfg"
DEFAULT_TARGET = "stm32h7x.cfg"
DEFAULT_ADDR = "0x24000000"
DEFAULT_SPEED = "1000,2000,4000,8000,10000,15000,20000"

# 默认 cfg（stm32h7x.cfg）支持的内核：STM32H743/H750/H753 都是 Cortex-M7
TARGET_CPU_PARTS = {0xC27: "Cortex-M7"}

# 目标 RAM 区域（起始, 结束, 名称）。仅用于地址范围检查与提示，按需修改。
KNOWN_REGIONS = [
    (0x20000000, 0x20020000, "DTCM (128KB)"),
    (0x24000000, 0x24080000, "AXI SRAM (512KB)"),
    (0x30000000, 0x30080000, "D2 SRAM1/2/3 (512KB)"),
    (0x38000000, 0x38010000, "D3 SRAM4 (64KB)"),
]

# --- 内核识别 -------------------------------------------------------------
# CPUID（0xE000ED00）低 16 位的 PartNo 决定内核型号。所有 Cortex-M 都有。
CPUID_ADDR = 0xE000ED00
PART_NO_MASK = 0xFFF
KNOWN_CPU_PARTS = {
    0xC20: "Cortex-M0", 0xC21: "Cortex-M1", 0xC23: "Cortex-M3",
    0xC24: "Cortex-M4", 0xC27: "Cortex-M7", 0xC60: "Cortex-M0+",
    0xD20: "Cortex-M23", 0xD21: "Cortex-M33", 0xD22: "Cortex-M55",
    0xD23: "Cortex-M85",
}

# --- 终端颜色 -------------------------------------------------------------
ANSI_YELLOW = "\033[33m"
ANSI_RESET = "\033[0m"
USE_COLOR = False


def enable_windows_vt():
    """在 Windows 控制台上打开 ANSI 转义序列处理（Win10+）。"""
    if os.name != "nt":
        return True
    try:
        import ctypes
        k32 = ctypes.windll.kernel32
        handle = k32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = ctypes.c_uint32()
        if not k32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(k32.SetConsoleMode(handle, mode.value | 0x0004))
    except Exception:
        return False


def init_color(mode):
    """按 --color 决定是否输出颜色。mode: auto / always / never"""
    global USE_COLOR
    if mode == "never":
        USE_COLOR = False
        return
    if mode == "always":
        USE_COLOR = True
        return
    if os.environ.get("NO_COLOR"):  # https://no-color.org/
        USE_COLOR = False
        return
    USE_COLOR = bool(getattr(sys.stdout, "isatty", lambda: False)())
    if USE_COLOR:
        USE_COLOR = enable_windows_vt()


def yellow(text):
    return (ANSI_YELLOW + text + ANSI_RESET) if USE_COLOR else ("[#]"+text)


def warn(text):
    """黄色警告。非彩色终端下退化为普通 [警告] 文本。"""
    print(yellow(text))


class OpenOCDTelnet:
    """OpenOCD telnet 客户端：应答 IAC 协商、去除回显与提示符。"""

    def __init__(self, host, port, timeout=10.0):
        self.timeout = timeout
        self.pending = b""  # 未消费的原始字节（可能是不完整的 IAC 序列）
        self.data = b""    # 已解析出的文本
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        # 关掉 Nagle：计时区间包含命令发送，延迟合并会污染小样本
        try:
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        self._read_until_prompt()  # 排空启动横幅

    def drain(self, quiet=0.25):
        """丢弃当前挂起的输出，返回丢弃的字节数。

        OpenOCD 会把它自己的日志广播给已连接的客户端，而日志到达时刻与命令
        响应无关。若连接建立前后有残留输出（典型来源：启动探测用的伪连接被
        断开时 OpenOCD 打出的 'Bad file descriptor'），后续计时就会量到陈旧
        响应而不是本次命令的结果，必须先排空。
        """
        self.sock.settimeout(quiet)
        dropped = 0
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                dropped += len(chunk)
        except (socket.timeout, OSError):
            pass
        finally:
            self.sock.settimeout(self.timeout)
        self.pending = b""
        return dropped

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
                    warn("[警告] %#x + %d 超出 %s 范围 %#x~%#x，已忽略限制继续" % (addr, size, name, start, end))
                    return size
                new_size = end - addr
                warn("[警告] %#x + %d 超出 %s 范围 %#x~%#x，已裁剪为 %d 字节" % (addr, size, name, start, end, new_size))
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


# OpenOCD 广播给 telnet 客户端的日志行特征（不是命令结果）
LOG_NOISE_MARKERS = ("bad file descriptor", "dropped 'telnet'", "accepting 'telnet'")

# OpenOCD 自报的传输结果，load_image 与 dump_image 都会打印
SELF_REPORT_RE = re.compile(r"(?:downloaded|dumped)\s+(\d+)\s+bytes\s+in\s+([\d.]+)s")


def is_log_noise(resp):
    """响应是否只是 OpenOCD 广播的日志，而非命令结果。"""
    r = resp.lower()
    if not r.strip():
        return True
    return any(m in r for m in LOG_NOISE_MARKERS)


def timed(tel, cmd_str, timeout):
    """执行一条命令，返回 (耗时秒, 响应文本)。"""
    t0 = time.perf_counter()
    resp = tel.cmd(cmd_str, timeout)
    return time.perf_counter() - t0, resp


def cmd_clean(tel, cmd_str, attempts=3, timeout=60.0):
    """执行命令，若响应只是 OpenOCD 广播的日志则排空后重发。

    日志到达时刻与命令响应无关，因此响应里可能只有日志而没有命令结果。
    """
    resp = ""
    for _ in range(attempts):
        resp = tel.cmd(cmd_str, timeout)
        if not is_log_noise(resp):
            return resp
        tel.drain()
    return resp


def stats_ms(xs):
    """对秒序列返回 (均值ms, 中位数ms, 最小ms, 标准差ms)。"""
    s = sorted(xs)
    n = len(s)
    mean = sum(s) / n
    med = s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])
    var = sum((x - mean) ** 2 for x in s) / n if n > 1 else 0.0
    return mean * 1e3, med * 1e3, s[0] * 1e3, var ** 0.5 * 1e3


def sample_ok(resp_w, resp_r, size, rf, pattern, no_verify):
    """判断一轮样本是否可信，返回 (是否有效, 无效原因)。

    两类污染都要挡住：
      * 响应错位 —— 收到的是上一条命令的陈旧响应，耗时会小得离谱；
      * 传输不完整 —— 读回数据与写入不符。
    """
    for name, resp in (("写", resp_w), ("读", resp_r)):
        m = SELF_REPORT_RE.search(resp)
        if not m:
            return False, "%s响应里没有自报字节数（疑似响应错位）" % name
        if int(m.group(1)) != size:
            return False, "%s自报 %s 字节，与块大小 %d 不符" % (name, m.group(1), size)
    if not no_verify and (not rf.exists() or rf.read_bytes() != pattern):
        return False, "读回数据不一致"
    return True, ""


def read_cpuid(tel, addr=CPUID_ADDR):
    """读 CPUID，返回 (原始值, PartNo)；读不到返回 (None, None)。"""
    resp = cmd_clean(tel, "mdw %#x 1" % addr, timeout=10.0)
    if is_openocd_error(resp):
        return None, None
    m = re.search(r"%x:\s*([0-9a-fA-F]{8})" % addr, resp, re.I)
    if not m:
        m = re.search(r":\s*([0-9a-fA-F]{8})", resp)
    if not m:
        return None, None
    raw = int(m.group(1), 16)
    return raw, (raw >> 4) & PART_NO_MASK


def check_target_cpu(tel, args, cfg_is_default):
    """确认目标内核属于默认配置支持的范围，不属于则黄色警告。

    判断是否目标不被当前配置文件支持。CPUID 在任何 Cortex-M 上都可读，因此即使
    cfg 与目标不匹配也能拿到结果。

    用户自己指定了 interface/target 时跳过本检查（由用户自行确认 cfg 与目标匹配）。
    """
    if args.no_cpu_check:
        print("目标检测: 已跳过（--no-cpu-check）")
        return True
    if not cfg_is_default:
        print("目标检测: 已跳过（使用了自定义 interface/target 配置，请自行确认其与目标匹配）")
        return True

    raw, part = read_cpuid(tel)
    if part is None:
        warn("[警告] 读不到 CPUID(%#x)，无法确认目标内核，请自行核对 RAM 地址与 cfg 是否匹配。"
             % CPUID_ADDR)
        return False

    name = KNOWN_CPU_PARTS.get(part, "未知内核")
    if part in TARGET_CPU_PARTS:
        print("目标检测: CPUID(%#x)=%#010x  PartNo=%#05x（%s）  ->  与默认配置（%s）匹配"
              % (CPUID_ADDR, raw, part, name, DEFAULT_TARGET))
        return True

    warn("[警告] 目标内核与默认配置不匹配，不是 %s 支持的 MCU！" % DEFAULT_TARGET)
    warn("       CPUID(%#x) = %#010x  ->  PartNo = %#05x（%s）"
         % (CPUID_ADDR, raw, part, name))
    warn("       默认配置（%s）面向 %s，本脚本默认的 RAM 划分与起始地址"
         % (DEFAULT_TARGET, "、".join(sorted(TARGET_CPU_PARTS.values()))))
    warn("       （%s 等）都是按它写的。" % "、".join("0x%08X" % r[0] for r in KNOWN_REGIONS))
    warn("       当前目标的 RAM 布局与初始化序列都不符合预期，测速结果不可直接采信。")
    warn("       请改本文件顶部的「默认目标配置」一节，或用 --addr / --interface / --target")
    warn("       指定正确的配置（指定后本检查会自动跳过），也可用 --no-cpu-check 强制继续。")
    return False


def adapter_option_supported(exe, iface, option):
    """探测当前 OpenOCD 构建是否认识某个 adapter 子命令。

    不同版本/构建的 adapter 子命令集合差异很大（例如 0.12.0 的某些构建里
    没有 max_buf_size），不认识的选项会让 OpenOCD 直接退出。
    """
    try:
        r = subprocess.run([str(exe), "-f", str(iface), "-c", option, "-c", "shutdown"],
                           capture_output=True, timeout=25)
    except (OSError, subprocess.SubprocessError):
        return False
    blob = (r.stdout + r.stderr).decode("utf-8", errors="replace").lower()
    if any(k in blob for k in ("invalid command name", "unknown option", "not found")):
        return False
    return r.returncode == 0


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
        opt = "adapter max_buf_size %d" % args.max_buf_size
        if adapter_option_supported(exe, iface, opt):
            cmd += ["-c", opt]
        else:
            warn("[警告] 当前 OpenOCD 不支持 '%s'，已忽略该选项（版本或构建未支持）" % opt)
    cmd.append("-c")
    cmd.append("init")
    log_path = workdir / "openocd.log"
    print("启动 OpenOCD:")
    print("  " + " ".join(cmd))
    with open(log_path, "w", encoding="utf-8", errors="replace") as logf:
        proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, cwd=str(workdir))

    # 等待就绪：读日志里的监听提示，而不是「连一下端口再关掉」。
    # 后者会制造一次伪连接，OpenOCD 把它当真实客户端，断开时的错误日志会
    # 广播给下一个连上来的客户端，导致首条命令的响应被污染。
    needle = "Listening on port %d for telnet connections" % args.telnet_port
    deadline = time.time() + 20
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("OpenOCD 启动后立即退出，日志:\n" + tail_file(log_path))
        try:
            if needle in log_path.read_text(encoding="utf-8", errors="replace"):
                return proc
        except OSError:
            pass
        time.sleep(0.05)
    raise RuntimeError("OpenOCD telnet 端口 %d 等待超时，日志:\n%s" % (args.telnet_port, tail_file(log_path)))


def run_benchmark(tel, args, speed_list, sizes, addr, workdir):
    results = []
    for speed in speed_list:
        cmd_clean(tel, "adapter speed %d" % speed)
        m = re.search(r"adapter speed:\s*(\d+)\s*kHz", cmd_clean(tel, "adapter speed"), re.I)
        # 这个回读只是为了确认的确给 OpenOCD 设置好了时钟。
        note = "（回读请求值 %s kHz）" % m.group(1) if m else ""
        print("\n===== SWD 时钟 %d kHz %s =====" % (speed, note))
        print("  %8s %9s %9s %9s %9s %9s %8s %6s" % (
            "size(B)", "写med(ms)", "写min(ms)", "写σ(ms)", "读med(ms)", "写KiB/s", "读KiB/s", "校验"))
        for size in sizes:
            pattern = gen_pattern(size, args.seed)
            wf = workdir / "w.bin"
            rf = workdir / "r.bin"
            wf.write_bytes(pattern)
            load_cmd = 'load_image "%s" %#x' % (wf.as_posix(), addr)
            dump_cmd = 'dump_image "%s" %#x 0x%x' % (rf.as_posix(), addr, size)

            # 预热：每个组合的首轮稳定偏慢（USB 端点/主机缓存/OpenOCD 缓冲），
            # 混进统计会把结果抬高数毫秒，对小块样本尤其致命。
            for _ in range(args.warmup):
                tel.cmd("halt")
                tel.cmd(load_cmd)
                rf.unlink(missing_ok=True)
                tel.cmd(dump_cmd)

            w_times, r_times, verify_ok = [], [], True
            # 重试预算：总尝试次数不超过 2N，保证至少拿到 N 个有效样本的同时
            # 不会因为持续失败而无限重测。
            attempts_left = 2 * args.rounds
            while len(w_times) < args.rounds and attempts_left > 0:
                attempts_left -= 1
                tel.cmd("halt")  # 保持 halt（顺带暴露目标被看门狗复位的情况）
                wt, resp_w = timed(tel, load_cmd, args.timeout)
                if is_openocd_error(resp_w):
                    raise RuntimeError("load_image 失败(%#x): %s" % (addr, resp_w[:200]))
                rf.unlink(missing_ok=True)
                rt, resp_r = timed(tel, dump_cmd, args.timeout)
                if is_openocd_error(resp_r):
                    raise RuntimeError("dump_image 失败(%#x): %s" % (addr, resp_r[:200]))

                ok, why = sample_ok(resp_w, resp_r, size, rf, pattern, args.no_verify)
                if not ok:
                    if attempts_left <= 0:
                        verify_ok = False
                    print("    [丢弃] %d 字节样本无效（%s），剩余重试预算 %d" % (size, why, attempts_left))
                    tel.drain()
                    continue
                w_times.append(wt)
                r_times.append(rt)
                if args.verbose:
                    sm = SELF_REPORT_RE.search(resp_w)
                    if sm:
                        print("    [轮次%d] OpenOCD 自报写: %s bytes / %ss" % (
                            len(w_times), sm.group(1), sm.group(2)))
            rejected = 2 * args.rounds - attempts_left - len(w_times)
            if not w_times:
                print("  %8d  样本全部无效，跳过该组合" % size)
                continue

            w_mean, w_med, w_min, w_sd = stats_ms(w_times)
            r_mean, r_med, r_min, r_sd = stats_ms(r_times)
            # 中位数：剔除离群后的可复现能力值
            w_kibs = size / (w_med / 1e3) / 1024
            r_kibs = size / (r_med / 1e3) / 1024
            # 用户感知速度：对全部有效样本取均值，即用户实际跑一遍平均会看到的速度。
            # 它会把偶发卡顿算进去，因此恒不高于上面的中位结果。
            w_kibs_feel = size / (w_mean / 1e3) / 1024
            r_kibs_feel = size / (r_mean / 1e3) / 1024
            results.append({"speed": speed, "size": size,
                            "w_kibs": w_kibs, "r_kibs": r_kibs,
                            "w_kibs_feel": w_kibs_feel, "r_kibs_feel": r_kibs_feel,
                            "w_med_ms": w_med, "r_med_ms": r_med, "w_mean_ms": w_mean,
                            "r_mean_ms": r_mean, "w_min_ms": w_min, "w_sd_ms": w_sd,
                            "valid": len(w_times), "rejected": rejected, "verify": verify_ok})
            print("  %8d %9.2f %9.2f %9.2f %9.2f %9.1f %8.1f %6s" % (
                size, w_med, w_min, w_sd, r_med, w_kibs, r_kibs,
                "OK" if verify_ok else "FAIL"))
            print("           用户感知(均值): 写 %.1f  读 %.1f KiB/s   有效样本 %d/%d，丢弃 %d" % (
                w_kibs_feel, r_kibs_feel, len(w_times), args.rounds, rejected))
    return results


def decompose(results, field="w"):
    """同一时钟档内按块大小线性回归，分离固定开销与边际成本。

    time(size) = 固定开销 + 边际 * size
      * 截距 = 每条 load_image/dump_image 的固有成本（telnet 往返 + Tcl 解析
        + OpenOCD 侧建立访问），与块大小无关；
      * 斜率 = 每字节边际成本，其倒数即块足够大时的渐进速率。

    只保留校验通过的样本；返回 [(speed, 固定开销ms, 边际µs/B, 渐进KiB/s), ...]。
    """
    rows = []
    for s in sorted({r["speed"] for r in results}):
        pts = [(r["size"], r["%s_med_ms" % field]) for r in results
               if r["speed"] == s and r["verify"]]
        if len(pts) < 2:
            continue
        n = len(pts)
        mx = sum(p[0] for p in pts) / n
        my = sum(p[1] for p in pts) / n
        den = sum((p[0] - mx) ** 2 for p in pts)
        if den <= 0:
            continue
        slope = sum((p[0] - mx) * (p[1] - my) for p in pts) / den  # ms/byte
        icept = my - slope * mx
        rows.append((s, icept, slope * 1e3, 1000.0 / (slope * 1024) if slope > 0 else 0.0))
    return rows


def print_summary(results):
    if not results:
        return
    best_w = max(results, key=lambda x: x["w_kibs"])
    best_r = max(results, key=lambda x: x["r_kibs"])
    best_wf = max(results, key=lambda x: x["w_kibs_feel"])
    best_rf = max(results, key=lambda x: x["r_kibs_feel"])
    print("\n===== 汇总 =====")
    print("  [中位结果] 最高写: %.1f KiB/s  @ %d kHz, 块 %d B" % (
        best_w["w_kibs"], best_w["speed"], best_w["size"]))
    print("  [中位结果] 最高读: %.1f KiB/s  @ %d kHz, 块 %d B" % (
        best_r["r_kibs"], best_r["speed"], best_r["size"]))
    print("  [用户感知] 最高写: %.1f KiB/s  @ %d kHz, 块 %d B" % (
        best_wf["w_kibs_feel"], best_wf["speed"], best_wf["size"]))
    print("  [用户感知] 最高读: %.1f KiB/s  @ %d kHz, 块 %d B" % (
        best_rf["r_kibs_feel"], best_rf["speed"], best_rf["size"]))
    total_rejected = sum(r["rejected"] for r in results)
    if total_rejected:
        print("  共丢弃 %d 个无效样本（校验失败/响应错位）" % total_rejected)

    rows = decompose(results, "w")
    if rows:
        print("\n===== 结果统计分解（写路径，按块大小线性回归） =====")
        print("  %9s %14s %13s %13s" % ("请求kHz", "固定开销(ms)", "边际(µs/B)", "渐进KiB/s"))
        for s, icept, slope_us, asym in rows:
            print("  %9d %14.2f %13.3f %13.1f" % (s, icept, slope_us, asym))
        print("  固定开销是每条 load_image/dump_image 命令的固有成本，与块大小无关；")
        print("  渐进 KiB/s 才反映链路本身的能力，KiB/s 单看会被固定开销稀释。")

    # 饱和检测：渐进速率不再随时钟上升即进入平台期，此时 x 轴已失去意义。
    # 用「最早达到峰值 95% 的档位」判定，避免把噪声挑出来的峰值档误当成封顶点。
    if len(rows) >= 2:
        asyms = [(s, a) for s, _, _, a in rows]
        best_a = max(a for _, a in asyms)
        if best_a > 0:
            plateau = min(s for s, a in asyms if a >= best_a * 0.95)
            if any(s > plateau for s, _ in asyms):
                print("\n[提示] 渐进写速率在 %d kHz 已达 %.1f KiB/s，更高时钟档未再提升，"
                      % (plateau, best_a))
                print("       说明天花板已不在 SWCLK 上（USB/协议层），继续提高 --speed 不会改变结果。")


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
    ap.add_argument("--interface", default=DEFAULT_INTERFACE,
                    help="interface 配置文件名（改用自定义 cfg 时会跳过目标内核检查）")
    ap.add_argument("--target", default=DEFAULT_TARGET,
                    help="target 配置文件名（改用自定义 cfg 时会跳过目标内核检查）")
    ap.add_argument("--transport", choices=["swd", "jtag"], default="swd")
    ap.add_argument("--attach", action="store_true", help="连接已运行的 OpenOCD，不自行启动")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--telnet-port", type=int, default=4444)
    ap.add_argument("--tcl-port", type=int, default=6666)
    ap.add_argument("--speed", default=DEFAULT_SPEED,
                    help="SWD 时钟(kHz)序列，逗号分隔。注意 cfg 里的初始限制（如 stm32h7x.cfg 为 4000 kHz），init 后脚本会逐档覆写")
    ap.add_argument("--sizes", default="4096,16384,65536", help="RAM 读写块大小(字节)，逗号分隔，支持 k/M 后缀")
    ap.add_argument("--rounds", type=int, default=5, help="每个(时钟,块大小)组合的测量轮数")
    ap.add_argument("--warmup", type=int, default=1,
                    help="每个组合的预热轮数，不计入统计（首轮稳定偏慢数毫秒，0=关闭）")
    ap.add_argument("--addr", default=DEFAULT_ADDR, help="RAM 起始地址（16 进制）。默认值按 STM32H7 AXI SRAM: 0x24000000 (512KB)，DTCM: 0x20000000 (128KB)")
    ap.add_argument("--seed", type=int, default=0x5A, help="写数据伪随机种子")
    ap.add_argument("--no-verify", action="store_true", help="跳过读回数据校验")
    ap.add_argument("--no-cpu-check", action="store_true",
                    help="跳过 CPUID 目标内核检测（默认配置下目标内核不是 Cortex-M7 时会警告）")
    ap.add_argument("--color", choices=["auto", "always", "never"], default="auto",
                    help="警告文字是否使用 ANSI 颜色，默认 auto（仅 tty 时启用）")
    ap.add_argument("--no-limit-check", action="store_true", help="不检查地址是否超出已知 RAM 区域")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--keep-tmp", action="store_true", help="保留临时文件")
    ap.add_argument("--max-buf-size", type=int, default=0,
                    help="OpenOCD adapter max_buf_size（限制单包/单次传输字节数，0=不限制）")
    ap.add_argument("--timeout", type=float, default=30.0, help="命令超时(秒)")
    args = ap.parse_args()
    init_color(args.color)

    speed_list = parse_int_list(args.speed)
    sizes = [parse_size(s) for s in args.sizes.split(",") if s.strip()]
    addr = int(args.addr, 0)
    if addr % 4:
        warn("[警告] 起始地址 %#x 非 4 字节对齐，测速结果可能不准确" % addr)
    fixed = []
    for s in sizes:
        s = check_region(addr, s, args.no_limit_check)
        if s % 4:
            warn("[警告] 块大小 %d 非 4 的倍数，已向下取整为 %d" % (s, s - s % 4))
            s -= s % 4
        if s > 0:
            fixed.append(s)
    sizes = list(dict.fromkeys(fixed))  # 裁剪后可能重复，去重
    if not speed_list or not sizes:
        print("[错误] 时钟序列或块大小为空")
        sys.exit(1)
    if args.rounds < 1:
        args.rounds = 1
    if args.warmup < 0:
        args.warmup = 0

    print("SWD RAM 测速配置:")
    print("  起始地址: %#x   时钟(kHz): %s   块大小(B): %s   轮数: %d   预热: %d   校验: %s" % (
        addr, speed_list, sizes, args.rounds, args.warmup, "开" if not args.no_verify else "关"))
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

        # 对齐响应流：OpenOCD 会把自身日志广播给已连接的客户端，且日志到达
        # 时刻与命令响应无关。先排空残留，再确认 halt 响应确实是本次命令的结果。
        # 以前这里在响应异常时改发 reset halt，会让响应流整体错位一条命令，
        # 后果是首轮量到陈旧响应（耗时小得离谱）并误报校验失败。
        tel.drain()
        resp = cmd_clean(tel, "halt")
        low = resp.lower()
        if "halted" in low or "already" in low:
            print("目标已 halt:", " ".join(resp.split())[:80])
        elif is_openocd_error(resp):
            warn("[警告] halt 响应异常: %s，尝试 reset halt ..." % " ".join(resp.split())[:120])
            resp = cmd_clean(tel, "reset halt")
            print("reset halt:", " ".join(resp.split())[:80])
            if is_openocd_error(resp):
                raise RuntimeError("halt/reset halt 均失败，请检查 SWD 连接与 target 配置:\n%s" % resp[:300])
        else:
            print("halt:", " ".join(resp.split())[:80])

        # 确认 target 已创建（防止配置遗漏导致所有命令 invalid）
        tlist = cmd_clean(tel, "targets")
        if "invalid command name" in tlist.lower() or not tlist.strip():
            raise RuntimeError("OpenOCD 没有可用的 target（请检查 target 配置文件）: %s" % tlist[:200])
        print("targets:", " ".join(tlist.split())[:80])

        # 目标内核检测：只默认 cfg 下生效，确认目标不是别的内核（否则 RAM
        # 划分与初始化序列都不符合预期）。用户指定了别的 cfg 时跳过。
        cfg_is_default = (args.interface == DEFAULT_INTERFACE and args.target == DEFAULT_TARGET)
        check_target_cpu(tel, args, cfg_is_default)

        resp = cmd_clean(tel, "mdw %#x 4" % addr)
        if is_openocd_error(resp):
            warn("[警告] 无法读取 %#x: %s" % (addr, " ".join(resp.split())[:120]))
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
