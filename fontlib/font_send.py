# -*- coding: utf-8 -*-
"""
font_send.py — 字库一键烧录上位机(YMODEM over serial, tkinter UI)

用法: python font_send.py
依赖: pyserial(已有), tkinter(Python 自带), 无需安装 ymodem 包(协议内置)

流程: 打开串口 → 发送 font_update 命令 → 等板子吐 'C' → YMODEM 1K 包发送
      font.bin → 回显板子的校验结果(font OK / FAIL)
"""
import os
import queue
import struct
import sys
import threading
import time

# 本机 CSR BlueSuite 会把全局 TCL_LIBRARY 指到它自己的目录,导致 tkinter
# 找不到 init.tcl 而崩溃。若该路径下没有 init.tcl,就清掉让 Tcl 自寻默认路径。
for _var in ("TCL_LIBRARY", "TK_LIBRARY"):
    _p = os.environ.get(_var)
    if _p and not os.path.exists(os.path.join(_p, "init.tcl")):
        os.environ.pop(_var, None)

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import serial
import serial.tools.list_ports

SOH = b"\x01"   # 128B 包头
STX = b"\x02"   # 1024B 包头
EOT = b"\x04"
ACK = 0x06
NAK = 0x15
CAN = 0x18
CRC_C = 0x43    # 'C'

DEFAULT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "font.bin")


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


class YmodemSender:
    """极简 YMODEM 发送端,按 RT-Thread rym 接收端的时序实现(1K 包 + CRC16)。"""

    def __init__(self, ser: serial.Serial, log, progress):
        self.ser = ser
        self.log = log          # callable(str)
        self.progress = progress  # callable(done_bytes, total_bytes)

    def _wait_byte(self, wanted, timeout_s, desc):
        """等待 wanted 集合中的任意字节,返回收到的字节值;超时抛异常。"""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            data = self.ser.read(1)
            if data:
                b = data[0]
                if b in wanted:
                    return b
                if b == CAN:
                    raise RuntimeError("接收端发送 CAN,主动取消了传输")
                # 其他字节(如启动阶段板子多余的 'C')忽略
        raise RuntimeError("等待 %s 超时(%ds)" % (desc, timeout_s))

    def _send_packet(self, seq: int, payload: bytes, retries=10):
        """发一个包并等 ACK。payload 长度必须是 128 或 1024。"""
        header = (SOH if len(payload) == 128 else STX) + bytes((seq & 0xFF, (~seq) & 0xFF))
        frame = header + payload + struct.pack(">H", crc16_xmodem(payload))
        for attempt in range(retries):
            self.ser.write(frame)
            self.ser.flush()
            try:
                got = self._wait_byte({ACK, NAK}, 6, "ACK(seq=%d)" % seq)
            except RuntimeError:
                got = NAK
            if got == ACK:
                return
            self.log("  包 %d 收到 NAK/超时,重发(%d/%d)" % (seq, attempt + 1, retries))
        raise RuntimeError("包 %d 重发 %d 次仍未被确认" % (seq, retries))

    def send_file(self, path: str):
        data = open(path, "rb").read()
        name = os.path.basename(path)
        total = len(data)

        self.log("等待接收端握手 'C' ...")
        self._wait_byte({CRC_C}, 30, "'C' 握手")

        # ---- 包 0: 文件名 + 大小 ----
        meta = name.encode("ascii", "ignore") + b"\x00" + str(total).encode() + b"\x00"
        pkt0 = meta.ljust(128, b"\x00") if len(meta) <= 128 else meta.ljust(1024, b"\x00")
        self.log("发送文件头: %s (%d bytes)" % (name, total))
        self._send_packet(0, pkt0)
        self._wait_byte({CRC_C}, 10, "数据阶段 'C'")

        # ---- 数据包: 1024B/包 ----
        seq = 1
        sent = 0
        while sent < total:
            chunk = data[sent:sent + 1024]
            chunk = chunk.ljust(1024, b"\x1a")   # 末包 0x1A 填充
            self._send_packet(seq, chunk)
            sent = min(sent + 1024, total)
            seq += 1
            self.progress(sent, total)

        # ---- 结束: EOT NAK EOT ACK, 然后空包 0 收尾 ----
        self.log("发送 EOT 收尾 ...")
        self.ser.write(EOT)
        self.ser.flush()
        got = self._wait_byte({NAK, ACK}, 6, "EOT 应答")
        if got == NAK:
            self.ser.write(EOT)
            self.ser.flush()
            self._wait_byte({ACK}, 6, "第二次 EOT 的 ACK")
        self._wait_byte({CRC_C}, 10, "结束阶段 'C'")
        self._send_packet(0, b"\x00" * 128)      # 空文件名包 = 会话结束
        self.log("YMODEM 发送完成 ✔")


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("字库烧录工具 - font.bin → W25Q128 font 分区")
        root.geometry("640x480")
        root.minsize(560, 420)

        self.msg_q = queue.Queue()
        self.busy = False

        frm = ttk.Frame(root, padding=10)
        frm.pack(fill="both", expand=True)

        # 行 1: 串口 + 波特率 + 刷新
        row1 = ttk.Frame(frm)
        row1.pack(fill="x", pady=(0, 6))
        ttk.Label(row1, text="串口:").pack(side="left")
        self.port_cb = ttk.Combobox(row1, width=12, state="readonly")
        self.port_cb.pack(side="left", padx=(4, 10))
        ttk.Label(row1, text="波特率:").pack(side="left")
        self.baud_cb = ttk.Combobox(row1, width=10, state="readonly",
                                    values=["115200", "230400", "460800", "921600"])
        self.baud_cb.set("115200")
        self.baud_cb.pack(side="left", padx=(4, 10))
        ttk.Button(row1, text="刷新串口", command=self.refresh_ports).pack(side="left")

        # 行 2: 文件选择
        row2 = ttk.Frame(frm)
        row2.pack(fill="x", pady=(0, 6))
        ttk.Label(row2, text="字库文件:").pack(side="left")
        self.file_var = tk.StringVar(value=DEFAULT_FILE if os.path.exists(DEFAULT_FILE) else "")
        ttk.Entry(row2, textvariable=self.file_var).pack(side="left", fill="x",
                                                         expand=True, padx=4)
        ttk.Button(row2, text="浏览...", command=self.pick_file).pack(side="left")

        # 行 3: 烧录按钮 + 进度条
        row3 = ttk.Frame(frm)
        row3.pack(fill="x", pady=(0, 6))
        self.send_btn = ttk.Button(row3, text="烧录字库", command=self.start_send)
        self.send_btn.pack(side="left")
        self.prog = ttk.Progressbar(row3, maximum=100)
        self.prog.pack(side="left", fill="x", expand=True, padx=(10, 0))
        self.prog_label = ttk.Label(row3, text="0%", width=6, anchor="e")
        self.prog_label.pack(side="left")

        # 日志窗口
        self.log_text = tk.Text(frm, height=18, state="disabled",
                                font=("Consolas", 9), bg="#101418", fg="#c8d3e0")
        self.log_text.pack(fill="both", expand=True)

        self.refresh_ports()
        self.root.after(50, self.poll_queue)

    # ---------- UI 辅助 ----------

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and self.port_cb.get() not in ports:
            self.port_cb.set(ports[0])

    def pick_file(self):
        path = filedialog.askopenfilename(
            title="选择字库文件",
            initialdir=os.path.dirname(self.file_var.get() or DEFAULT_FILE),
            filetypes=[("字库/固件", "*.bin"), ("所有文件", "*.*")])
        if path:
            self.file_var.set(path)

    def log(self, msg):
        self.msg_q.put(("log", msg))

    def set_progress(self, done, total):
        self.msg_q.put(("prog", (done, total)))

    def poll_queue(self):
        try:
            while True:
                kind, payload = self.msg_q.get_nowait()
                if kind == "log":
                    self.log_text.configure(state="normal")
                    self.log_text.insert("end", payload + "\n")
                    self.log_text.see("end")
                    self.log_text.configure(state="disabled")
                elif kind == "prog":
                    done, total = payload
                    pct = int(done * 100 / total) if total else 0
                    self.prog["value"] = pct
                    self.prog_label["text"] = "%d%%" % pct
                elif kind == "done":
                    self.busy = False
                    self.send_btn["state"] = "normal"
        except queue.Empty:
            pass
        self.root.after(50, self.poll_queue)

    # ---------- 烧录流程(工作线程) ----------

    def start_send(self):
        if self.busy:
            return
        port = self.port_cb.get()
        path = self.file_var.get()
        if not port:
            messagebox.showwarning("提示", "请选择串口(板子的 msh 控制台口)")
            return
        if not path or not os.path.exists(path):
            messagebox.showwarning("提示", "字库文件不存在,请重新选择")
            return

        self.busy = True
        self.send_btn["state"] = "disabled"
        self.prog["value"] = 0
        threading.Thread(target=self.worker,
                         args=(port, int(self.baud_cb.get()), path),
                         daemon=True).start()

    def worker(self, port, baud, path):
        ser = None
        try:
            self.log("打开 %s @ %d ..." % (port, baud))
            self.log("(若失败请先关闭占用该串口的终端软件)")
            ser = serial.Serial(port, baud, timeout=0.2)

            # 唤醒 msh 并发起 font_update
            ser.reset_input_buffer()
            ser.write(b"\r\n")
            time.sleep(0.3)
            ser.reset_input_buffer()
            self.log("发送命令: font_update")
            ser.write(b"font_update\r\n")
            ser.flush()

            # 回显板子输出直到出现 'C'(握手由 YmodemSender 再确认)
            deadline = time.time() + 8
            echo = b""
            while time.time() < deadline:
                chunk = ser.read(64)
                if chunk:
                    echo += chunk
                    if b"C" in chunk:
                        break
            for line in echo.decode("utf-8", "ignore").splitlines():
                line = line.strip().strip("C")
                if line:
                    self.log("[板子] " + line)

            sender = YmodemSender(ser, self.log, self.set_progress)
            t0 = time.time()
            sender.send_file(path)
            self.log("耗时 %.1f 秒" % (time.time() - t0))

            # 读板子的校验结果
            self.log("---- 板子校验输出 ----")
            deadline = time.time() + 5
            tail = b""
            while time.time() < deadline:
                chunk = ser.read(128)
                if chunk:
                    tail += chunk
                    deadline = time.time() + 1.5
            ok = False
            for line in tail.decode("utf-8", "ignore").splitlines():
                line = line.strip()
                if line:
                    self.log("[板子] " + line)
                    if "font OK" in line:
                        ok = True
            if ok:
                self.log("★ 烧录成功,字库校验通过!")
            else:
                self.log("!! 未看到 font OK,请在终端里跑 font_info 复查")
        except Exception as exc:
            self.log("出错: %s" % exc)
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
            self.log("串口已关闭,可重新用终端连接查看")
            self.msg_q.put(("done", None))


if __name__ == "__main__":
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista")
    except Exception:
        pass
    App(root)
    root.mainloop()
