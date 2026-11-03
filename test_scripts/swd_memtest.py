# -*- coding: utf-8 -*-
"""SWD 内存读写压力测试（memtest 风味）

在 CMSIS-DAP + OpenOCD + Cortex-M 目标的环境下，对目标的 RAM 做以下四维度的交叉读写压力测试：
- 多种图案 
- 读/写方向
- 不同访问宽度
- 不同地址，

并把失败样本按"损坏形态"分类，用来回答两个问题：

   1. 链路/固件在高速档下**会不会错**？
   2. 一旦错，**怎么个错法**（哪一位、哪种规律）？

作者
----

- rinscr3003
    - with Copilot: Deepseek V4.1 Flash


与 `swd_ram_bench_v2.py` 的关系
-------------------------------

后者测的是**吞吐率**（KiB/s），本脚本测的是**正确性**。二者共用同一套 telnet
传输层与目标识别代码，故本脚本直接从同目录导入 `swd_ram_bench_v2`。

提示
----

* 每笔 SWD 传输都伴随一次引脚模式切换与跨核等待，所以本脚本测的是**逐字正确性**
  而非吞吐率；吞吐率请用 `swd_ram_bench_v2.py`。
* 固件在等待溢出或传输未完成时可能返回错误 ACK 而不报错，表现为**单条命令耗时
  异常长**。脚本用 `--slow-sample-ms`（默认 500 ms）按**单条写/读命令的往返**
  把这类样本单独计数，便于与真正的值不符区分（该阈值不含脚本排空日志的固定等待）。


用法
----
    # 连已有的 OpenOCD（默认 127.0.0.1:4444），每档自成收发
    python swd_memtest.py

    # 低速档与高速档交叉（写读方向互换）
    python swd_memtest.py --modes all

    # 指定地址、宽度、档位
    python swd_memtest.py --addr 0x20000000 --width 16 --speeds 2000,4000,8000

    # 自动生成全部速度对（写入速度 × 读出速度）
    python swd_memtest.py --speeds 1000,2000,4000,8000 --matrix

    # 只打 walking ones/zeros（定位具体哪一位错）
    python swd_memtest.py --suites walk --modes all

    # 用自定义图案文件
    python swd_memtest.py --pattern-file my_patterns.json

    # 自己拉起 OpenOCD（结束时按 PID 结束该进程）
    python swd_memtest.py --spawn --interface cmsis-dap.cfg --target stm32h7x.cfg

    # 查看可用的图案集 / 损坏模型
    python swd_memtest.py --list-suites
    python swd_memtest.py --list-models

以 H750 为目标为例：换目标要改什么
----------------------------------
本脚本的**默认值**是按 STM32H750/STM32H7 + CMSIS-DAP 这一组合设定的。
换别的目标时，下面的项必须同步修改，否则容易出现“命令都成功、读回来却
全是错的”这类假失败：

| 要改的东西 | 在哪里改 | 默认值 / 说明 |
|---|---|---|
| OpenOCD 的 interface/target cfg | `--interface` / `--target` | 默认取自 `swd_ram_bench_v2.DEFAULT_INTERFACE/DEFAULT_TARGET`，即 `cmsis-dap.cfg` + `stm32h7x.cfg`。**换芯片必须同时改这两个** |
| RAM 区域与默认起始地址 | `swd_ram_bench_v2.KNOWN_REGIONS` / `DEFAULT_ADDR` | 默认是 STM32H7 的 DTCM / AXI SRAM / D2 SRAM1-3 / D3 SRAM4。地址范围检查与 `--region-index` 都依赖它 |
| cfg 支持的内核 | `swd_ram_bench_v2.TARGET_CPU_PARTS` | 默认仅 Cortex-M7。不匹配时脚本给黄色告警（`--require-cpu` 时中止） |
| 速度档位 | `--speeds` | 默认 `1000,2000,4000,8000`，同时测试高速/低速路径 |
| 测试地址 | `--addr` | 默认从 `KNOWN_REGIONS` 选；务必落在目标真实 RAM 内 |

> `KNOWN_REGIONS` 只做**区域级**判断：同一内核下不同型号的 RAM 划分并不相同，
> 所以“内核对得上”并不代表“地址划分对得上”。
"""
from __future__ import annotations

import argparse
import atexit
import json
import os
import random
import re
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
import swd_ram_bench_v2 as m  # noqa: E402  同目录的传输层与目标识别代码

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
except Exception:
    pass
try:
    # 本脚本把输出同时写到控制台与文件；Windows 控制台默认不是 UTF-8，
    # 中文会在管道/重定向下变成乱码（文件本身仍是对的，但控制台难读）。
    # line_buffering：Tee 安装之前的早期打印也同样不应被全缓冲扣住。
    sys.__stdout__.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
except Exception:
    pass

# 检测用档位。取默认档位里最低的一档：检测属于开销，不应成为风险来源，也不该
# 用一个高档位去判断「链路是否还活着」——那样一旦高档位本身不可靠，检测失败就
# 无法区分「链路断了」与「这个档位不行」。
DEFAULT_HEALTH_SPEED = 1000


# ===========================================================================
#  终端结论：PASS / FAIL 字符画
# ===========================================================================
# 只用 '#' 拼字母，5 行固定高度；每个字形右补空格后再拼接，保证列对齐。
_FONT = {
    "P": ("### ", "#  #", "### ", "#   ", "#   "),
    "A": (" ## ", "#  #", "####", "#  #", "#  #"),
    "S": (" ###", "#   ", " ## ", "   #", "### "),
    "F": ("####", "#   ", "### ", "#   ", "#   "),
    "I": ("###", " # ", " # ", " # ", "###"),
    "L": ("#   ", "#   ", "#   ", "#   ", "####"),
}


def _banner(word: str) -> str:
    rows = [""] * 5
    for ch in word:
        glyph = _FONT[ch]
        for i in range(5):
            rows[i] += glyph[i] + "  "
    return "\n".join(r.rstrip() for r in rows)


BANNER_PASS = _banner("PASS")
BANNER_FAIL = _banner("FAIL")


def print_verdict(passed: bool, note: str = "") -> None:
    """在终端打印绿色 PASS / 红色 FAIL 字符画。

    颜色只在真实终端上启用：重定向到文件时 ANSI 转义码会变成乱码，而日志文件
    要能直接阅读。判定只看调用方给的布尔值，不做二次解释。
    """
    art = BANNER_PASS if passed else BANNER_FAIL
    colored = False
    try:
        if sys.__stdout__.isatty():
            colored = True if os.name != "nt" else m.enable_windows_vt()
    except Exception:
        colored = False
    if colored:
        color = "\033[32m" if passed else "\033[31m"
        art = color + art + "\033[0m"
    print()
    print(art)
    if note:
        print(note)
    print()


def stop_spawned_openocd(proc) -> None:
    """结束**本脚本自己拉起**的 OpenOCD。

    按 PID 终止，不按进程名：本机可能同时有别的 OpenOCD 实例（别的板子、别的
    会话），按名字杀会把它们一起弄死。Windows 上先试 `taskkill /PID <pid> /T`，
    以便把 OpenOCD 可能派生的子进程一并收掉；其余平台用 terminate。

    该函数是幂等的：进程已退出就什么也不做，因此可以同时挂在 atexit 与
    finally 上（Ctrl-C、异常、正常收尾三条路径都能覆盖）。
    """
    if proc is None:
        return
    try:
        if proc.poll() is not None:
            return
    except Exception:
        return
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                           capture_output=True, timeout=15)
        else:
            proc.terminate()
    except Exception:
        pass
    try:
        proc.wait(timeout=5)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


def a32(x: int) -> str:
    """地址的定宽十六进制。**不要**用 Width.hex() 来格式化地址：
    Width.hex() 是按“访问宽度”取掩码的，用在地址上会把 16 位模式下的
    0x24001000 写成 0x1000（实测踩到）。地址恒定 32 位。
    """
    return "0x%08X" % (int(x) & 0xFFFFFFFF)


# ===========================================================================
#  访问宽度
# ===========================================================================
# OpenOCD 的命名：md 读 memory，mw 写 memory，后缀 b/h/w = byte/half/word
class Width:
    def __init__(self, bits: int):
        if bits not in (8, 16, 32):
            raise ValueError("宽度只支持 8 / 16 / 32")
        self.bits = bits
        self.bytes = bits // 8
        self.digits = bits // 4
        self.r = {8: "mdb", 16: "mdh", 32: "mdw"}[bits]   # 读命令
        self.w = {8: "mwb", 16: "mwh", 32: "mww"}[bits]   # 写命令
        self.mask = (1 << bits) - 1

    def hex(self, x: int) -> str:
        """定宽十六进制字面量。必须掩码：PowerShell 会把十六进制实参当 Int32 求值，
        `0x80000000` 会变成 -2147483648，拼出 `0x-80000000` 这种非法值。"""
        return "0x%0*X" % (self.digits, int(x) & self.mask)

    def u(self, x: int) -> int:
        return int(x) & self.mask

    def __str__(self):
        return "%d 位（%s / %s）" % (self.bits, self.r, self.w)


# ===========================================================================
#  图案集（注册表，可扩展）
# ===========================================================================
# 每个图案集返回 [(名称, 值), ...]，值会被掩码到当前访问宽度。
# 签名约定：f(width, seed=...)。只有随机类图案集真正用 seed，其余接受并忽略，
# 这样注册表可以统一调用，将来新增图案集也不必改主流程。
SUITES: dict = {}


def suite(name):
    def deco(fn):
        SUITES[name] = fn
        return fn
    return deco


@suite("basic")
def _s_basic(w: Width, seed: int = 0xC0FFEE):
    """最基础的几种结构：全 0/1、相邻位互补、2 位与 4 位周期。"""
    vals = [0x00000000, 0xFFFFFFFF, 0x55555555, 0xAAAAAAAA,
            0x33333333, 0xCCCCCCCC, 0xF0F0F0F0, 0x0F0F0F0F]
    return [("b%02d_%0*X" % (i, w.digits, w.u(v)), w.u(v)) for i, v in enumerate(vals)]


@suite("walk")
def _s_walk(w: Width, seed: int = 0xC0FFEE):
    """walking ones + walking zeros，逐位注入。

    为什么它最重要：walking ones 的 popcount 恒为 1（奇），walking zeros 恒为
    位数-1。若接收侧的校验是**奇偶**，累加器少算/多算**任何一位**，这两组就会
    整片翻车。所以它是对"奇偶累加错位"最敏感的图案组。

    逆序（高位先）以便同时暴露"顺序/地址相关"效应。
    """
    out = []
    for k in range(w.bits - 1, -1, -1):
        out.append(("w1[%02d]" % k, 1 << k))
    for k in range(w.bits - 1, -1, -1):
        out.append(("w0[%02d]" % k, w.u(~(1 << k))))
    return out


@suite("rand")
def _s_rand(w: Width, n=16, seed=0xC0FFEE):
    """伪随机：无结构图案上跑覆盖率（种子来自 `--seed`，可复现）。"""
    rng = random.Random(seed)
    return [("rnd%02d" % i, rng.getrandbits(w.bits)) for i in range(n)]


@suite("const")
def _s_const(w: Width, seed: int = 0xC0FFEE):
    """常量扫描：0x00 / 0xFF / 交替的单字节值，找"某个字节值必错"型缺陷。"""
    out = []
    for b in (0x00, 0xFF, 0x0F, 0xF0, 0x55, 0xAA, 0x01, 0x80):
        v = 0
        for i in range(w.bytes):
            v |= b << (8 * i)
        out.append(("const_%02X" % b, w.u(v)))
    return out


@suite("addr")
def _s_addr(w: Width, seed: int = 0xC0FFEE):
    """地址相关：把地址本身写进数据，检出地址/数据线耦合。"""
    return []       # 需要多地址，主流程单独处理（见 run_addr_suite）


@suite("quick")
def _s_quick(w: Width, seed: int = 0xC0FFEE):
    """最小覆盖集：约 10 个图案，专打"位序两端 + 校验奇偶"这几个关键点。

    当被测链路健康状况不明、或单次运行预算很紧时用它，先用最小代价判断
    "是不是系统性错了"，再决定要不要跑全套。
    """
    vals = [0x00000000, w.mask,
            1 << 0, 1 << (w.bits - 1),
            w.u(~1), w.u(~(1 << (w.bits - 1)))]
    return [("q%02d_%0*X" % (i, w.digits, v), v) for i, v in enumerate(vals)]


# ===========================================================================
#  损坏模型（注册表，可扩展）
# ===========================================================================
# 每个模型是 f(写值, 宽度) -> 预期读回值。用于把失败样本归类成
# "怎么个错法"，比只看"值不等"有用得多。
MODELS: dict = {}


def model(name, note=""):
    def deco(fn):
        MODELS[name] = (fn, note)
        return fn
    return deco


@model("shift1", "每字节：bit0 复制到 bit1，其余左移一位，MSB 丢（PIOC 采样错位的特征）")
def _m_shift1(w: int, width: Width):
    out = 0
    for i in range(width.bytes):
        b = (w >> (8 * i)) & 0xFF
        out |= (((b << 1) | (b & 1)) & 0xFF) << (8 * i)
    return width.u(out)


@model("invert", "全位取反（读相位反了）")
def _m_invert(w: int, width: Width):
    return width.u(~w)


@model("zero", "恒为 0（读不到）")
def _m_zero(w: int, width: Width):
    return 0


@model("ones", "恒为全 1（总线浮空/上拉）")
def _m_ones(w: int, width: Width):
    return width.mask


@model("swap16", "半字交换（字节序/移位错）")
def _m_swap16(w: int, width: Width):
    if width.bits < 16:
        return -1        # 不适用
    return width.u(((w & 0xFFFF) << 16) | ((w >> 16) & 0xFFFF)) if width.bits == 32 else width.u(
        ((w & 0xFF) << 8) | ((w >> 8) & 0xFF))


def classify(w: int, r: int, width: Width):
    """返回 (类别, 说明)。类别为 'ok' 或某个模型名，或 'other'。"""
    if w == r:
        return "ok", ""
    for name, (fn, _note) in MODELS.items():
        try:
            if fn(w, width) == r:
                return name, ""
        except Exception:
            pass
    # 'other' 必须能自证：同时记下写进去的值与读回来的值，再加差异掩码。
    # 只报“差异=xxx”时，事后无法判断到底哪一端、哪一位出了问题。
    return "other", "期望 %s 得到 %s（差异 %s）" % (
        width.hex(w), width.hex(r), width.hex(w ^ r))


# ===========================================================================
#  传输层
# ===========================================================================
class Tele:
    """对 OpenOCD telnet 的一次封装。单连接长跑 —— 这一点很重要。

    为什么必须单连接：有些复现脚本给**每条命令新开一个 telnet 连接**，跑二三十条
    就会把 OpenOCD 的 telnet 服务堵住，表现为"命令超时"，会被误判成"设备挂死"。
    那不是设备的问题。判"设备是否真挂死"的可靠办法是**另开一个 CMSIS-DAP 会话
    去读 CPUID/DPIDR**：读得到就说明设备健康，问题在主机侧。
    """

    def __init__(self, host, port, width: Width, timeout=30.0):
        self.width = width
        self.t = m.OpenOCDTelnet(host, port, timeout)
        self.t.drain()
        self.cur_speed = None       # 记录当前档位，避免重复下发
        self.ncmd = 0               # 累计命令数（用于定位"跑到第几条出问题"）
        self.tcmd_ms = 0.0          # 上一条命令的往返耗时（不含 drain）
        self.op_ms = 0.0            # 自上次 _begin() 起的命令耗时合计
        self._pat = {}

    # ---- 基础 ----
    def _begin(self):
        """开始一个新操作的计时区间（写一次、读一次各算一个操作）。"""
        self.op_ms = 0.0

    def cmd(self, s, timeout=30.0, drain=True):
        """执行一条命令，并（默认）排空 OpenOCD 广播的日志。

        耗时只算命令本身的往返，**不含** drain：drain 会等 socket 静默
        `quiet` 秒（默认 0.25 s），把它算进去会让每条命令都"至少几百毫秒"，
        慢响应判据就再也区分不出真正的等超时。
        """
        self.ncmd += 1
        t0 = time.monotonic()
        r = m.cmd_clean(self.t, s, timeout=timeout)
        self.tcmd_ms = (time.monotonic() - t0) * 1000.0
        self.op_ms += self.tcmd_ms
        if drain:
            self.t.drain()
        return r

    def speed(self, khz):
        """只在档位真的变化时才下发（少一条命令，也少一次 Set_Clock_Delay）。"""
        if khz == self.cur_speed:
            return
        self.cmd("adapter speed %d" % khz, timeout=30.0)
        self.cur_speed = khz

    def halt(self):
        return self.cmd("halt")

    def health(self, addr, expect_hex=None):
        """低速读一次 CPUID。返回 (ok, 描述)。

        这是**检测**：读不到或读到错值都说明链路已经不干净，继续测没有意义。

        注意：检测**恒定用 32 位读**（`mdw`），不跟随 `--width`。
        CPUID 本来就是 32 位寄存器，而比较的是完整值；若用 16 位读，
        只会拿到低半字（实测 `c271` vs 期望 `411fc271`）⇒ 必然误判为链路异常。
        """
        self.speed(DEFAULT_HEALTH_SPEED)
        r = self.cmd("mdw %#x 1" % addr, timeout=15.0)
        if m.is_openocd_error(r):
            return False, r.strip().replace("\n", " ")[:140]
        # 这里不能用 self._words()（它按 --width 抽字），必须固定 32 位
        vals = []
        for line in r.splitlines():
            tail = line.split(":", 1)[1] if ":" in line else line
            vals.extend(x.lower() for x in re.findall(r"\b[0-9a-fA-F]{8}\b", tail))
        if not vals:
            return False, "无数据：" + r.strip().replace("\n", " ")[:110]
        if expect_hex is None:
            return True, vals[0]
        return (vals[0] == expect_hex.lower()), "%s（期望 %s）" % (vals[0], expect_hex)

    # ---- 解析 ----
    def _pat_for(self, addr):
        p = self._pat.get(addr)
        if p is None:
            p = self._pat[addr] = re.compile("%x:" % addr, re.I)
        return p

    def _words(self, resp):
        """从响应里抽出定宽的十六进制字。只取冒号之后的部分，避免把地址当数据。"""
        out = []
        rx = re.compile(r"\b[0-9a-fA-F]{%d}\b" % self.width.digits)
        for line in resp.splitlines():
            tail = line.split(":", 1)[1] if ":" in line else line
            out.extend(x.lower() for x in rx.findall(tail))
        return out

    # ---- 读 / 写 ----
    def write(self, addr, val):
        r = self.cmd("%s %#x %s" % (self.width.w, addr, self.width.hex(val)))
        if m.is_openocd_error(r):
            return False, r.strip().replace("\n", " ")[:150]
        return True, ""

    def read(self, addr, count=1):
        """count=1 时读两次并取第二次的值。

        原因（与具体目标无关的 OpenOCD/ADIv5 语义）：单字读返回的是**上一笔**传输的
        结果（posted read）。连读两次才能拿到该地址的真值。
        count>1 时一次读 count 个字，走另一条数据通路（DRW），错法可能与单字读不同。
        """
        r = self.cmd("%s %#x %d" % (self.width.r, addr, count))
        if m.is_openocd_error(r):
            return None, r.strip().replace("\n", " ")[:150]
        if not self._pat_for(addr).search(r):
            return None, "无地址前缀（响应错位）：" + r.strip().replace("\n", " ")[:110]
        vals = self._words(r)
        if len(vals) < count:
            return None, "只拿到 %d/%d 个字" % (len(vals), count)
        if count == 1:
            r2 = self.cmd("%s %#x 1" % (self.width.r, addr))
            if m.is_openocd_error(r2):
                return None, r2.strip().replace("\n", " ")[:150]
            v2 = self._words(r2)
            if not v2:
                return None, "第二次读无数据"
            return v2[0], ""
        return vals, ""

    def close(self):
        try:
            self.t.close()
        except Exception:
            pass


# ===========================================================================
#  速度档位与模式
# ===========================================================================
def parse_modes(spec: str, speeds):
    """把 --modes 解析成 [(名称, 写kHz, 读kHz), ...]。

    支持三种写法：
      * 名称：Wlo-Rlo / Wlo-Rhi / Whi-Rlo / Whi-Rhi / all
        名称里的 lo/hi 映射到 `--speeds` 的**最小/最大**档
      * `each`：每个档位自成收发（写读同速），适合先看「哪个档位开始出错」
      * 显式：`写:读`，如 `1000:8000`
    """
    lo = min(speeds) if speeds else DEFAULT_HEALTH_SPEED
    hi = max(speeds) if speeds else DEFAULT_HEALTH_SPEED
    named = {
        "Wlo-Rlo": (lo, lo),
        "Wlo-Rhi": (lo, hi),
        "Whi-Rlo": (hi, lo),
        "Whi-Rhi": (hi, hi),
    }
    out = []
    seen = set()
    for tok in spec.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok.lower() == "each":
            # 逐档自成收发：把失败定位到具体档位，而不是只看出一个「写读组合」
            for khz in sorted(speeds):
                name = "w%d-r%d" % (khz, khz)
                if name not in seen:
                    seen.add(name)
                    out.append((name, khz, khz))
            continue
        if tok.lower() == "all":
            toks = ["Wlo-Rlo", "Wlo-Rhi", "Whi-Rlo", "Whi-Rhi"]
        elif tok in named:
            w, r = named[tok]
            if tok not in seen:
                seen.add(tok)
                out.append((tok, w, r))
            continue
        elif ":" in tok:
            a, _, b = tok.partition(":")
            try:
                w, r = int(a, 0), int(b, 0)
            except ValueError:
                raise SystemExit("无法解析模式 %r（应为 名称 或 写:读）" % tok)
            name = "w%d-r%d" % (w, r)
            if name not in seen:
                seen.add(name)
                out.append((name, w, r))
            continue
        else:
            raise SystemExit("未知模式 %r（可选：Wlo-Rlo/Wlo-Rhi/Whi-Rlo/Whi-Rhi/all 或 写:读）" % tok)
        # all 展开
        for t in toks:
            w, r = named[t]
            if t not in seen:
                seen.add(t)
                out.append((t, w, r))
    return out


def matrix_modes(speeds):
    """全部 (写, 读) 速度对，用于 --matrix。"""
    out = []
    for w in sorted(speeds):
        for r in sorted(speeds):
            out.append(("w%d-r%d" % (w, r), w, r))
    return out


# ===========================================================================
#  自定义图案文件
# ===========================================================================
def load_patterns(path: Path, width: Width):
    """从文件载入图案。支持三种格式：

      * `.json` 为整数数组           -> `[0x55555555, 0xAAAAAAAA]`
      * `.json` 为对象数组           -> `[{"name": "x", "value": "0x5555"}]`
      * 其它扩展名                   -> 每行一个十六进制/十进制值，`#` 注释
    """
    txt = path.read_text(encoding="utf-8-sig", errors="replace")
    out = []
    if path.suffix.lower() == ".json":
        data = json.loads(txt)
        if isinstance(data, dict):
            data = [{"name": k, "value": v} for k, v in data.items()]
        for i, item in enumerate(data):
            if isinstance(item, dict):
                name = str(item.get("name", "p%02d" % i))
                val = item.get("value", item.get("v"))
            else:
                name, val = "p%02d" % i, item
            out.append((name, width.u(int(str(val), 0))))
    else:
        n = 0
        for line in txt.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 2:
                out.append((parts[0], width.u(int(parts[1], 0))))
            else:
                out.append(("p%02d" % n, width.u(int(parts[0], 0))))
            n += 1
    return out


# ===========================================================================
#  主流程
# ===========================================================================
def build_patterns(args, width: Width):
    if args.pattern_file:
        return load_patterns(Path(args.pattern_file), width), "file:%s" % args.pattern_file
    pats, names = [], []
    for s in args.suites.split(","):
        s = s.strip()
        if not s:
            continue
        if s not in SUITES:
            raise SystemExit("未知图案集 %r（可用：%s）" % (s, ", ".join(sorted(SUITES))))
        if s == "addr":
            names.append(s)
            continue
        pats.extend(SUITES[s](width, seed=args.seed))
        names.append(s)
    return pats, ",".join(names)


def run_addr_suite(tele: Tele, args, mode_name, w_khz, r_khz, width: Width):
    """地址相关测试：相邻地址写互补图案，检出地址/数据线耦合。"""
    n = args.words
    tel = tele
    tel.speed(w_khz)
    ok = True
    for k in range(n):
        v = 0x55555555 & width.mask if k % 2 == 0 else 0xAAAAAAAA & width.mask
        okw, e = tel.write(args.addr + width.bytes * k, v)
        if not okw:
            return False, "写 #%d 失败：%s" % (k, e)
    tel.speed(r_khz)
    got, e = tel.read(args.addr, n)
    if got is None:
        return False, "读失败：%s" % e
    # 按**整数**比较，不要比字符串：width.hex() 带 "0x" 前缀，而 OpenOCD
    # 返回的是裸十六进制（"55555555"），字符串比较会恒不相等 ⇒ 假失败。
    # 诊断信号：若"所有模式（含最低速对照）都同样失败"，几乎一定是测试代码的锅，
    # 而不是被测设备 —— 真机故障不会在所有档位上表现完全一致。
    bad = []
    for k in range(n):
        exp_i = (0x55555555 if k % 2 == 0 else 0xAAAAAAAA) & width.mask
        if int(got[k], 16) != exp_i:
            bad.append("[%d] 期望 %s 得到 %s" % (k, width.hex(exp_i), got[k]))
    return (not bad), ("全部正确" if not bad else "; ".join(bad[:6]))


def main():
    ap = argparse.ArgumentParser(
        description="SWD 内存读写压力测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("用法\n----")[-1] if "用法\n----" in __doc__ else None)
    ap.add_argument("host", nargs="?", default="127.0.0.1",
                    help="OpenOCD telnet 主机（默认 127.0.0.1）")
    ap.add_argument("port", nargs="?", type=int, default=4444,
                    help="OpenOCD telnet 端口（默认 4444）")

    # --- 目标 ---
    g = ap.add_argument_group("目标与访问")
    g.add_argument("--addr", type=lambda s: int(s, 0), default=None,
                   help="测试起始地址。不指定时用 --region-index 从已知 RAM 区选")
    g.add_argument("--region-index", type=int, default=1,
                   help="未指定 --addr 时选用 KNOWN_REGIONS 的第几项（默认 1）")
    g.add_argument("--width", type=int, default=32, choices=(8, 16, 32),
                   help="访问宽度（默认 32）")
    g.add_argument("--words", type=int, default=8,
                   help="地址相关测试覆盖多少个字（默认 8）")
    g.add_argument("--read-len", type=int, default=1,
                   help="读长度：1=单字读（走 posted read，读两次取第二次）；"
                        ">1=连续读 count 个字（走 DRW，与单字读是不同数据通路，错法可能不同）")
    g.add_argument("--force-addr", action="store_true",
                   help="允许地址落在已知 RAM 区域之外（危险：可能写坏外设）")

    # --- 图案 ---
    g = ap.add_argument_group("图案")
    g.add_argument("--suites", default="basic", help="逗号分隔：%s" % ", ".join(sorted(SUITES)))
    g.add_argument("--pattern-file", default=None,
                   help="从文件载入图案（.json 数组，或每行一个值的文本）")
    g.add_argument("--seed", type=int, default=0xC0FFEE, help="伪随机种子")

    # --- 速度 ---
    g = ap.add_argument_group("速度")
    g.add_argument("--speeds", default="1000,2000,4000,8000",
                   help="逗号分隔的候选档位(kHz)。")
    g.add_argument("--modes", default="each",
                   help="逗号分隔：each（逐档同速收发）/ Wlo-Rlo/Wlo-Rhi/Whi-Rlo/Whi-Rhi/all 或 写:读")
    g.add_argument("--matrix", action="store_true",
                   help="忽略 --modes，生成 --speeds 的全部 (写,读) 组合")
    g.add_argument("--hs-threshold", type=int, default=4000,
                   help="多少 kHz 及以上算'高速'（仅用于--max-hs-ops 计数，不代表固件行为）")
    g.add_argument("--restore-speed", type=int, default=None,
                   help="每模式结束恢复到哪个档位（默认 --speeds 的最小值）")

    # --- 安全 ---
    g = ap.add_argument_group("安全")
    g.add_argument("--abort-after", type=int, default=6, help="连续失败后中止")
    g.add_argument("--max-hs-ops", type=int, default=1000,
                   help="【硬上限】整轮测试中高速操作的最大次数。默认 1000 足以让默认档位"
                        "（约 290 次）与全套图案（约 400 次）跑完；设小会成为提前结束的约束")
    g.add_argument("--slow-sample-ms", type=float, default=500.0,
                   help="单条写/读命令的往返耗时阈值（不含脚本排空日志的固定等待）。"
                        "超阈值的样本额外计数，用于把「固件等超时/未完成但不报错」与真正的值不符分开（0=不检查）")
    g.add_argument("--health-every", type=int, default=1,
                   help="每 N 个样本做一次 CPUID 检测（0 = 只在每模式首尾）")
    g.add_argument("--no-halt", action="store_true",
                   help="不自动 halt（默认会 halt，因为目标运行中内存访问不稳定）")
    g.add_argument("--rounds", type=int, default=1, help="每个模式重复轮数")
    g.add_argument("--interval", type=float, default=0.0, help="样本间隔秒数")
    g.add_argument("--keep-speed", action="store_true",
                   help="块内不切回低速（少一条命令/样本）")

    # --- 身份与期望 ---
    g = ap.add_argument_group("目标识别")
    g.add_argument("--cpuid-addr", type=lambda s: int(s, 0), default=m.CPUID_ADDR,
                   help="CPUID 地址（默认 0xE000ED00）")
    g.add_argument("--expect-cpuid", type=lambda s: int(s, 0), default=None,
                   help="期望的 CPUID 全值。不指定则只做'能读到且前后一致'检查")
    g.add_argument("--require-cpu", action="store_true",
                   help="内核 PartNo 不在已知表内时中止（默认只告警）")

    # --- OpenOCD 生命周期 ---
    g = ap.add_argument_group("OpenOCD")
    g.add_argument("--spawn", action="store_true",
                   help="自己拉起 OpenOCD（默认为连接其它已在本地运行的 OpenOCD）")
    g.add_argument("--openocd", default=m.DEFAULT_OPENOCD, help="OpenOCD 可执行文件")
    g.add_argument("--interface", default=m.DEFAULT_INTERFACE, help="interface cfg 文件名")
    g.add_argument("--target", default=m.DEFAULT_TARGET, help="target cfg 文件名")
    g.add_argument("--transport", default="swd", help="transport（默认 swd）")
    g.add_argument("--max-buf-size", type=int, default=0, help="adapter max_buf_size（0=不设）")

    # --- 输出 ---
    g = ap.add_argument_group("输出")
    g.add_argument("--tag", default="mt", help="输出文件名标签")
    g.add_argument("--outdir", default=None, help="输出目录（默认 <脚本目录>/memtest_out）")
    g.add_argument("--quiet", action="store_true", help="不打印逐样本进度")
    g.add_argument("--timeout", type=float, default=30.0, help="单条命令超时秒数")

    # --- 自描述 ---
    g = ap.add_argument_group("信息")
    g.add_argument("--list-suites", action="store_true", help="列出图案集后退出")
    g.add_argument("--list-models", action="store_true", help="列出损坏模型后退出")

    args = ap.parse_args()

    if args.list_suites:
        print("可用图案集：")
        w = Width(args.width)
        for name, fn in sorted(SUITES.items()):
            doc = (fn.__doc__ or "").strip().splitlines()
            head = doc[0] if doc else ""
            try:
                n = len(fn(w))
            except Exception:
                n = -1
            if n > 0:
                extra = "（%d 个图案）" % n
            elif name == "addr":
                extra = "（需多地址，由主流程单独处理）"
            else:
                extra = ""
            print("  %-8s %s %s" % (name, head, extra))
        return 0
    if args.list_models:
        print("可用损坏模型：")
        for name, (_fn, note) in sorted(MODELS.items()):
            print("  %-10s %s" % (name, note))
        return 0

    width = Width(args.width)
    speeds = m.parse_int_list(args.speeds)
    if not speeds:
        raise SystemExit("--speeds 不能为空")
    speeds = sorted(speeds)
    restore = args.restore_speed if args.restore_speed is not None else speeds[0]

    # ---- 地址 ----
    if args.addr is None:
        if not (0 <= args.region_index < len(m.KNOWN_REGIONS)):
            raise SystemExit("--region-index 超出范围（已知 %d 个区域）" % len(m.KNOWN_REGIONS))
        start, end, name = m.KNOWN_REGIONS[args.region_index]
        args.addr = start + 0x1000      # 跳过区域头部，避免踩栈/保留区
        print("[地址] 未指定 --addr，选用 %s：%#x" % (name, args.addr))
    # 注意：地址必须恒定按 32 位处理，**不能**用 width.u() 掩码。
    # width.u() 是按“访问宽度”取掩码的，用在地址上会把 16 位模式下的
    # 0x24001000 截成 0x1000（实测踩到）。只有**数据值**才按访问宽度掩码。
    args.addr &= 0xFFFFFFFF
    if args.addr & (width.bytes - 1):
        print("[警告] 地址 %#x 未按 %d 字节对齐，部分目标会拒绍访问" % (args.addr, width.bytes))
    args.cpuid_addr &= 0xFFFFFFFF

    in_region = any(s <= args.addr < e for s, e, _ in m.KNOWN_REGIONS)
    if not in_region and not args.force_addr:
        raise SystemExit(
            "[拒绝执行] 地址 %#x 不在已知 RAM 区域内。\n"
            "           本脚本会破坏该地址处的内容；写外设寄存器可能造成不可预期后果。\n"
            "           确认可写请加 --force-addr。" % args.addr)
    if not in_region:
        print("[警告] --force-addr 已生效：地址 %#x 不在已知 RAM 区域" % args.addr)

    # ---- 输出目录 ----
    outdir = Path(args.outdir) if args.outdir else (HERE / "memtest_out")
    outdir.mkdir(parents=True, exist_ok=True)
    out_txt = outdir / ("memtest_%s_out.txt" % args.tag)
    out_json = outdir / ("memtest_%s.json" % args.tag)

    # ---- 模式 ----
    modes = matrix_modes(speeds) if args.matrix else parse_modes(args.modes, speeds)
    if args.matrix:
        print("[模式] --matrix：生成 %d 个速度组合" % len(modes))

    pats, suite_desc = build_patterns(args, width)

    # ---- 输出文件（同时镜像到控制台） ----
    class Tee:
        """把 stdout 同时写到日志文件与控制台。

        文件用**行缓冲**（buffering=1）：本脚本常被 Ctrl-C / 强杀打断，全缓冲会把
        最后一段进度留在进程缓冲里丢掉，而恰恰是出错前的最后几行最有用。
        """

        def __init__(self, path):
            self.f = open(path, "w", encoding="utf-8", errors="replace", buffering=1)

        def write(self, s):
            self.f.write(s)
            sys.__stdout__.write(s)

        def log(self, s, console=True):
            """写一行日志：文件始终写，控制台按需。

            --quiet 只静音控制台，不丢文件记录——否则事后无从核对进度。
            """
            self.f.write(s)
            if console:
                sys.__stdout__.write(s)

        def flush(self):
            try:
                self.f.flush()
            except Exception:
                pass

        def close(self):
            try:
                self.f.close()
            except Exception:
                pass

    sys.stdout = Tee(out_txt)

    print("=" * 96)
    print("SWD 内存压力测试  %s:%d  tag=%s" % (args.host, args.port, args.tag))
    print("  访问宽度=%s   地址=%#x%s" % (width, args.addr, "" if in_region else "  [区域外!]"))
    print("  档位=%s   模式=%s" % (speeds, ", ".join("%s(%d/%d)" % t for t in modes)))
    # 种子按无符号 32 位显示：负数会写成 "0x-0000ABC" 这种畸形值。
    print("  图案集=%s   图案数=%d   种子=0x%08X%s"
          % (suite_desc, len(pats), args.seed & 0xFFFFFFFF,
             "  [+地址相关]" if "addr" in suite_desc else ""))
    print("  高速阈值=%d kHz   高速操作上限=%d   连续失败止损=%d"
          % (args.hs_threshold, args.max_hs_ops, args.abort_after))
    print("  慢响应阈值=%s   检测间隔=%s   轮数=%d"
          % (("%.0f ms" % args.slow_sample_ms) if args.slow_sample_ms > 0 else "不检查",
             args.health_every or "仅每模式首尾", args.rounds))
    print("=" * 96)

    # ---- 拉起 OpenOCD 或连接已有 ----
    proc = None
    if args.spawn:
        ns = SimpleNamespace(openocd=args.openocd, interface=args.interface,
                             target=args.target, telnet_port=args.port,
                             tcl_port=args.port + 2222, transport=args.transport,
                             max_buf_size=args.max_buf_size)
        print("[OpenOCD] 自行拉起实例（端口 %d）…" % args.port)
        proc = m.spawn_openocd(ns, speeds, outdir)
        # 立刻挂 atexit：即使中途 Ctrl-C 或被异常打断，也能按 PID 收掉自己拉起的
        # 实例，不会把它遗留成占着 4444 端口的孤儿进程。
        atexit.register(stop_spawned_openocd, proc)
        print("[OpenOCD] 就绪（PID %d，脚本结束时自动结束）" % proc.pid)

    tele = None
    try:
        tele = Tele(args.host, args.port, width, args.timeout)
    except Exception as e:
        print("[中止] 无法连接 OpenOCD telnet %s:%d —— %s: %s"
              % (args.host, args.port, type(e).__name__, e))
        print("       请确认 OpenOCD 已运行并监听该端口；若要本脚本自行拉起，加 --spawn。")
        return 2

    # ---- 目标识别 ----
    raw, part = m.read_cpuid(tele.t)
    cpu_name = m.KNOWN_CPU_PARTS.get(part, "未知内核")
    print("[目标] CPUID=%s  PartNo=0x%03X ⇒ %s"
          % (("%08x" % raw) if raw is not None else "<读不到>",
             part if part is not None else 0, cpu_name))
    if raw is None:
        # 读不到 CPUID 就无法建立检测（检测本身就是反复读 CPUID），继续测没有
        # 意义，因此无论有没有 --require-cpu 都中止。
        print("[中止] 读不到 CPUID（目标未连接 / 未 halt / cfg 与目标不匹配？）")
        tele.close()
        return 2
    if part not in m.KNOWN_CPU_PARTS:
        # PartNo 不在表内：可能只是新芯片，也可能是 cfg 连错了目标。默认只告警，
        # 由操作者判断；--require-cpu 时按硬性失败处理。
        missg = ("内核 PartNo=0x%03X 不在已知表内（已知：%s）"
                 % (part, "、".join(sorted(m.KNOWN_CPU_PARTS))))
        if args.require_cpu:
            print("[中止] %s ⇒ --require-cpu 要求中止" % missg)
            tele.close()
            return 2
        print("[警告] %s；确认 cfg 与目标匹配可忽略，否则请检查 --interface/--target" % missg)
    expect_hex = ("%08x" % args.expect_cpuid) if args.expect_cpuid is not None else None
    if expect_hex and expect_hex.lower() != ("%08x" % raw):
        print("[中止] CPUID 与 --expect-cpuid(%s) 不符 ⇒ 连到的不是期望目标" % expect_hex)
        tele.close()
        return 2

    if not args.no_halt:
        tele.cmd("halt")
        print("[目标] 已 halt")

    gate_addr = args.cpuid_addr
    gate_expect = expect_hex if expect_hex else ("%08x" % raw)
    okg, info = tele.health(gate_addr, gate_expect)
    if not okg:
        print("[中止] 开场检测失败：%s" % info)
        tele.close()
        return 2
    print("[检测] 开场通过：CPUID=%s\n" % info)

    results = {}
    hs_ops = 0
    hs_capped = False
    body_completed = False

    try:
        for round_i in range(args.rounds):
            for (mode_name, w_khz, r_khz) in modes:
                mode_is_hs = (w_khz >= args.hs_threshold) or (r_khz >= args.hs_threshold)
                print("\n" + "#" * 96)
                print("# 轮 %d/%d  模式 %s ：写 %d kHz → 读 %d kHz"
                      % (round_i + 1, args.rounds, mode_name, w_khz, r_khz))
                print("#" * 96)
                tally = Counter()
                samples = []
                slow_hits = []
                consec = 0
                n_ok = 0

                for name, W in pats:
                    if mode_is_hs and hs_ops >= args.max_hs_ops:
                        print("\n[硬上限] 高速操作已达 %d 次，立即停止。" % hs_ops)
                        hs_capped = True
                        break

                    # 写
                    tele.speed(w_khz)
                    tele._begin()
                    if w_khz >= args.hs_threshold:
                        hs_ops += 1
                    okw, ew = tele.write(args.addr, W)
                    w_ms = tele.op_ms
                    # 读
                    tele.speed(r_khz)
                    tele._begin()
                    if r_khz >= args.hs_threshold:
                        hs_ops += 1
                    if args.read_len > 1:
                        got, er = tele.read(args.addr, args.read_len)
                        R = got[0] if got else None
                    else:
                        R, er = tele.read(args.addr, 1)
                    r_ms = tele.op_ms
                    if not args.keep_speed:
                        tele.speed(restore)
                    if args.interval:
                        time.sleep(args.interval)
                    # 慢响应按**单条命令往返**计，写/读各取最大：
                    # 它既可能是"固件在等超时/未完成"的特征，也可能只是主机态抖动，
                    # 所以不进 total/fail，只作为旁证记录。
                    cmd_ms = max(w_ms, r_ms)
                    if args.slow_sample_ms > 0 and cmd_ms >= args.slow_sample_ms:
                        slow_hits.append((name, cmd_ms))

                    if not okw:
                        cls, note = "写失败", "写命令报错：%s" % ew
                        tally[cls] += 1
                        consec += 1
                        samples.append({"name": name, "W": width.hex(W), "cls": cls,
                                        "note": note, "cmd": tele.ncmd})
                    elif R is None:
                        cls, note = "读失败", "读命令报错：%s" % er
                        tally[cls] += 1
                        consec += 1
                        samples.append({"name": name, "W": width.hex(W), "cls": cls,
                                        "note": note, "cmd": tele.ncmd})
                    else:
                        Ri = int(R, 16)
                        cls, note = classify(W, Ri, width)
                        tally[cls] += 1
                        # 命令能走通 ⇒ 链路仍在应答，重置 IO 连续失败计数。
                        # （值不符由 tally/样本记录，不计入 consec，否则会把
                        #   “链路活着但内容错”误判成链路断了。）
                        consec = 0
                        if cls == "ok":
                            n_ok += 1
                        else:
                            samples.append({"name": name, "W": width.hex(W),
                                            "R": width.hex(Ri), "cls": cls, "note": note,
                                            "diff": width.hex(W ^ Ri), "cmd": tele.ncmd})

                    done = n_ok + len(samples)
                    sys.stdout.log("  [%3d/%3d] %-12s %s %s\n"
                                   % (done, len(pats), name, width.hex(W),
                                      "OK" if cls == "ok" else "<< %s %s" % (cls, note)),
                                   console=not args.quiet)

                    # 检测
                    if args.health_every and done % args.health_every == 0:
                        okg, hinfo = tele.health(gate_addr, gate_expect)
                        if not okg:
                            print("\n[中止] 中途检测失败（命令 #%d，图案 %s）：%s"
                                  % (tele.ncmd, name, hinfo))
                            raise SystemExit(1)
                    if consec >= args.abort_after:
                        print("\n[止损] 连续失败 %d 次，结束该模式" % consec)
                        break

                # 地址相关测试
                if "addr" in suite_desc and not hs_capped:
                    print("\n  地址相关测试（相邻地址互补图案）…")
                    ok_a, msg_a = run_addr_suite(tele, args, mode_name, w_khz, r_khz, width)
                    print("    %s：%s" % (mode_name, msg_a))
                    if not ok_a:
                        tally["地址耦合"] += 1
                    tele.speed(restore)

                total = sum(tally.values())
                nfail = total - tally.get("ok", 0)
                print("\n  已跑图案=%d  失败=%d  本轮高速操作=%d  累计命令=%d"
                      % (total, nfail, hs_ops, tele.ncmd))
                print("  分类: %s" % (", ".join("%s×%d" % kv for kv in tally.most_common())
                                      or "（无）"))
                if slow_hits:
                    print("  慢响应样本: %d 个（阈值 %.0f ms，最长 %.0f ms）"
                          % (len(slow_hits), args.slow_sample_ms,
                             max(ms for _n, ms in slow_hits)))
                    for nm, ms in slow_hits[:5]:
                        print("    %-12s %.0f ms" % (nm, ms))
                if samples:
                    print("  失败样本:")
                    for s in samples[:25]:
                        if "R" in s:
                            print("    %-12s W=%s R=%s  [%s] %s"
                                  % (s["name"], s["W"], s["R"], s["cls"], s.get("note", "")))
                        else:
                            print("    %-12s W=%s  [%s] %s"
                                  % (s["name"], s["W"], s["cls"], s.get("note", "")[:110]))
                    if len(samples) > 25:
                        print("    … 其余 %d 条见 JSON" % (len(samples) - 25))

                key = "%s#%d" % (mode_name, round_i + 1)
                results[key] = {"mode": mode_name, "write_khz": w_khz, "read_khz": r_khz,
                                "width": width.bits, "addr": a32(args.addr),
                                "tally": dict(tally), "total": total, "fail": nfail,
                                "samples": samples, "slow": len(slow_hits),
                                "hs_ops": hs_ops, "capped": hs_capped}

                tele.speed(restore)
                okg, hinfo = tele.health(gate_addr, gate_expect)
                alive = bool(okg)
                print("  模式末检测: %s" % ("✓ 链路正常" if alive else "✗ 链路异常 (%s)" % hinfo))
                if not alive:
                    print("  [中止] 链路已坏，停止后续模式。")
                    raise SystemExit(1)
                if hs_capped:
                    print("\n[注意] 已因高速上限停止，后续模式未执行。")
                    break
            if hs_capped:
                break
        body_completed = True
    finally:
        try:
            if tele is not None:
                tele.speed(restore)
                print("\n已恢复档位 %d kHz" % restore)
        except Exception:
            print("\n[警告] 档位恢复失败（连接可能已断）")
        if tele is not None:
            # 自己拉的实例先发 shutdown：让 OpenOCD 自己收尾、刷新日志；
            # 随后再用 PID 兜底（它可能已自行退出，stop() 是幂等的）。
            try:
                if proc is not None:
                    tele.cmd("shutdown", timeout=10.0, drain=False)
            except Exception:
                pass
            tele.close()
        if proc is not None:
            stop_spawned_openocd(proc)
            print("[OpenOCD] 已结束自建实例（PID %d）" % proc.pid)
        if not body_completed:
            print_verdict(False, "（测试未跑完：中途中止或链路异常）")
        # 这里只 flush 不 close：汇总与 JSON 还要在这些行之后打印。
        # 日志文件是行缓冲的，逐行已落盘，即便此刻被中断也不丢内容。
        try:
            sys.stdout.flush()
        except Exception:
            pass

    # ---- 汇总 ----
    print("\n" + "=" * 96)
    print("汇总")
    print("=" * 96)
    print("%-14s %8s %8s  %s" % ("模式", "失败", "总数", "分类"))
    print("-" * 96)
    for k, v in results.items():
        print("%-14s %8d %8d  %s"
              % (k, v["fail"], v["total"],
                 ", ".join("%s×%d" % (a, b) for a, b in
                           sorted(v["tally"].items(), key=lambda x: -x[1]))))

    all_fail = sum(v["fail"] for v in results.values())
    all_tot = sum(v["total"] for v in results.values())
    print("-" * 96)
    print("合计：失败 %d / 总数 %d" % (all_fail, all_tot))
    print()
    print("判读提示：")
    print("  * 失败只在'高速'侧 ⇒ 时序/采样问题（对照与低速侧应全过）")
    print("  * 失败被归类为某个模型名 ⇒ 见 --list-models 里的规律，可据此定位")
    print("  * `读失败`（命令报错）而非 `值不符` ⇒ 多半是链路/固件层问题，不是位序问题")
    print("  * 出现 `写失败` ⇒ 先怀疑链路，因为写方向没有读校验")
    print("  * 慢响应（单条命令往返超过 --slow-sample-ms）而不报错 ⇒ 先怀疑传输在等超时/未完成，")
    print("    可用示波器对照 SWCLK 整帧，而不是当成位序问题")
    print("  * 判定'设备是否真挂死'：另开一个会话读 CPUID；读得到说明设备健康")

    # 结论只认「全部图案全部通过」：有失败、一个样本都没跑（配置错）、或因为
    # --max-hs-ops 只跑了一半，都不算 PASS。慢响应不影响结论（它可能只是主机态）。
    passed = (all_fail == 0) and (all_tot > 0) and not hs_capped
    if hs_capped:
        verdict_note = "（因高速操作上限提前结束，未覆盖全部计划用例）"
    elif all_tot <= 0:
        verdict_note = "（没有执行任何用例）"
    else:
        verdict_note = "失败 %d / 总数 %d" % (all_fail, all_tot)

    out_json.write_text(json.dumps(
        {"host": args.host, "port": args.port, "addr": a32(args.addr),
         "width": width.bits, "speeds": speeds, "suites": suite_desc,
         "patterns": len(pats), "seed": args.seed,
         "cpuid": ("%08x" % raw), "cpu": cpu_name,
         "verdict": "PASS" if passed else "FAIL", "results": results},
        ensure_ascii=False, indent=2), encoding="utf-8")

    print("\n结果 JSON: %s" % out_json)
    print("原始输出 : %s" % out_txt)
    print_verdict(passed, verdict_note)
    # 正常收尾时显式关掉日志文件；异常路径靠解释器退出时对 sys.stdout 的 flush，
    # 行缓冲已保证内容不丢。
    try:
        sys.stdout.flush()
        sys.stdout.close()
    except Exception:
        pass
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
