# -*- coding: utf-8 -*-
"""
file_pull.py — 从板子 littlefs 用 YMODEM 拉文件到电脑

对应板端 msh 命令:
    sy <board_path>          # 默认走控制台串口(uart1)

用法:
    python file_pull.py

依赖:
    pyserial, tkinter(Python 自带)
    无需安装 ymodem 包(接收协议内置,对齐 RT-Thread rym 发送端)

流程:
    打开串口 → 发 sy <path> → 本机发 'C' 握手
    → 收包0(文件名/大小) → 收数据包 → 存到本地路径
"""
from __future__ import annotations

import os
import queue
import struct
import threading
import time

# 本机 CSR BlueSuite 会把全局 TCL_LIBRARY 指歪,导致 tkinter 崩
for _var in ("TCL_LIBRARY", "TK_LIBRARY"):
    _p = os.environ.get(_var)
    if _p and not os.path.exists(os.path.join(_p, "init.tcl")):
        os.environ.pop(_var, None)

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import serial
import serial.tools.list_ports

SOH = 0x01  # 128B
STX = 0x02  # 1024B
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
CRC_C = 0x43  # 'C'

DEFAULT_BOARD_PATH = "/pcm/last.pcm"
DEFAULT_SAVE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pulled")


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


class YmodemReceiver:
    """
    极简 YMODEM 接收端,对齐 RT-Thread rym_send_on_device / sy 命令时序。

    板端发送侧主要用 SOH(128B) 包;这里同时兼容 STX(1024B)。
    """

    def __init__(self, ser: serial.Serial, log, progress):
        self.ser = ser
        self.log = log
        self.progress = progress

    def _read_exact(self, n: int, timeout_s: float) -> bytes:
        deadline = time.time() + timeout_s
        buf = bytearray()
        while len(buf) < n and time.time() < deadline:
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf.extend(chunk)
            else:
                time.sleep(0.001)
        if len(buf) != n:
            raise RuntimeError("读数据超时,期望 %d 字节,实际 %d" % (n, len(buf)))
        return bytes(buf)

    def _read_byte(self, timeout_s: float) -> int:
        data = self._read_exact(1, timeout_s)
        return data[0]

    def _write(self, data: bytes):
        self.ser.write(data)
        self.ser.flush()

    def _handshake_c(self, total_timeout_s: float = 25.0):
        """发 'C' 握手,等板端发文件头(SOH/STX)。
        关键: RT-Thread rym 发送端收到第一个 'C' 就开始发包0,
        之后立刻等 ACK。若本机多发的 'C' 被它当成 ACK -> 直接中止。
        所以 'C' 最多每 ~1s 发一次,一旦看到任何回字节就停止重发。"""
        self.log("握手: 发 'C',等文件头 SOH ...")
        deadline = time.time() + total_timeout_s
        last_c = 0.0
        while time.time() < deadline:
            now = time.time()
            if now - last_c > 1.0:
                self._write(bytes((CRC_C,)))
                last_c = now
            b = self.ser.read(1)
            if not b:
                continue
            code = b[0]
            if code in (SOH, STX):
                return code
            if code == CAN:
                raise RuntimeError("板端发送 CAN,取消传输")
            # msh 回显/噪声: 继续读,不要立刻重发 'C'
        raise RuntimeError("握手超时: 板端未开始发送文件(检查路径/是否 sy 成功)")

    def _recv_packet_after_header(self, header_code: int, timeout_s: float = 6.0):
        """已读到 SOH/STX,继续读完一包,校验 CRC,返回 (seq, payload)。"""
        if header_code == SOH:
            data_sz = 128
        elif header_code == STX:
            data_sz = 1024
        else:
            raise RuntimeError("未知包头 0x%02X" % header_code)

        rest = self._read_exact(2 + data_sz + 2, timeout_s)  # seq, ~seq, data, crc
        seq = rest[0]
        seq_inv = rest[1]
        if ((seq + seq_inv) & 0xFF) != 0xFF:
            raise RuntimeError("序号校验失败 seq=%d inv=%d" % (seq, seq_inv))

        payload = rest[2:2 + data_sz]
        crc_recv = (rest[-2] << 8) | rest[-1]
        crc_calc = crc16_xmodem(payload)
        if crc_recv != crc_calc:
            raise RuntimeError("CRC 失败 seq=%d calc=%04X recv=%04X" % (seq, crc_calc, crc_recv))
        return seq, payload

    @staticmethod
    def _parse_header(payload: bytes):
        """解析包0: name\\0 size\\0 ..."""
        # 去掉尾部填充 0
        raw = payload.split(b"\x00")
        name = raw[0].decode("ascii", "ignore") if raw else ""
        size = 0
        if len(raw) >= 2 and raw[1]:
            try:
                # size 可能是 "12345" 或 "12345 0 0" 形式,取第一段
                size = int(raw[1].decode("ascii", "ignore").split()[0])
            except Exception:
                size = 0
        return name, size

    def receive_to_path(self, save_path: str) -> dict:
        """
        完成一次接收,写入 save_path。
        返回 {name, size, saved, path}
        """
        os.makedirs(os.path.dirname(os.path.abspath(save_path)) or ".", exist_ok=True)

        # 清一下残留
        time.sleep(0.05)
        self.ser.reset_input_buffer()

        header_code = self._handshake_c(25.0)
        seq, payload = self._recv_packet_after_header(header_code)
        if seq != 0:
            raise RuntimeError("期望文件头包 seq=0,实际 seq=%d" % seq)

        name, size = self._parse_header(payload)
        self.log("文件头: name=%s size=%d" % (name or "(unknown)", size))
        if size <= 0:
            self.log("警告: 未解析到有效 size,将按收满的原始包保存(可能含 0x1A 填充)")

        # ACK + 'C' 进入数据阶段(对齐 rym 发送端)
        self._write(bytes((ACK, CRC_C)))

        out = open(save_path, "wb")
        received = 0
        expect_seq = 1
        pkt_count = 0
        try:
            while True:
                # 等下一个包头或 EOT
                b = self._read_byte(15.0)
                if b == EOT:
                    self.log("收到 EOT,收尾 ...")
                    # rym: 第一次 EOT → NAK; 第二次 EOT → ACK + C; 再收空包0 → ACK
                    self._write(bytes((NAK,)))
                    b2 = self._read_byte(6.0)
                    if b2 != EOT:
                        raise RuntimeError("期望第二次 EOT,收到 0x%02X" % b2)
                    self._write(bytes((ACK, CRC_C)))

                    # 空文件名结束包
                    end_hdr = self._read_byte(10.0)
                    if end_hdr not in (SOH, STX):
                        raise RuntimeError("期望结束空包 SOH/STX,收到 0x%02X" % end_hdr)
                    end_seq, end_payload = self._recv_packet_after_header(end_hdr)
                    if end_seq != 0:
                        self.log("警告: 结束包 seq=%d (通常为0)" % end_seq)
                    # 空名表示会话结束
                    self._write(bytes((ACK,)))
                    break

                if b == CAN:
                    raise RuntimeError("板端 CAN,传输取消")

                if b not in (SOH, STX):
                    # 噪声/回显,跳过
                    continue

                seq, payload = self._recv_packet_after_header(b)
                pkt_count += 1
                if pkt_count <= 3:
                    self.log("  包#%d seq=%d (%d bytes)" % (pkt_count, seq, len(payload)))

                # 建立后偶发重发包0,忽略(只 ACK)
                if seq == 0:
                    self._write(bytes((ACK,)))
                    continue

                # RT-Thread rym 发送端收到非 ACK 就中止,不支持重传。
                # 所以这里只要 CRC 已校验通过(在 _recv_packet_after_header 里),
                # 就一律 ACK 顺序接收,绝不发 NAK。
                if seq != (expect_seq & 0xFF):
                    self.log("警告: seq=%d 期望=%d,仍 ACK 接收(rym 不重传)" % (seq, expect_seq & 0xFF))

                # 按声明 size 截断最后填充
                if size > 0:
                    remain = size - received
                    if remain <= 0:
                        chunk = b""
                    else:
                        chunk = payload[:remain]
                else:
                    chunk = payload

                if chunk:
                    out.write(chunk)
                    received += len(chunk)

                self._write(bytes((ACK,)))
                expect_seq = (seq + 1) & 0xFF
                if size > 0:
                    self.progress(min(received, size), size)
                else:
                    self.progress(received, max(received, 1))

        finally:
            out.close()

        # 若 size 已知且多写了(不应),截断; 若少了,告警
        final_size = os.path.getsize(save_path)
        if size > 0 and final_size != size:
            if final_size > size:
                with open(save_path, "rb+") as f:
                    f.truncate(size)
                final_size = size
            else:
                self.log("警告: 收到 %d 字节 < 声明 %d 字节" % (final_size, size))

        self.log("保存完成: %s (%d bytes)" % (save_path, final_size))
        return {"name": name, "size": size, "saved": final_size, "path": save_path}


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("板端文件提取 - YMODEM (sy)")
        root.geometry("680x520")
        root.minsize(600, 460)

        self.msg_q = queue.Queue()
        self.busy = False

        frm = ttk.Frame(root, padding=10)
        frm.pack(fill="both", expand=True)

        # 行1: 串口
        row1 = ttk.Frame(frm)
        row1.pack(fill="x", pady=(0, 6))
        ttk.Label(row1, text="串口:").pack(side="left")
        self.port_cb = ttk.Combobox(row1, width=14, state="readonly")
        self.port_cb.pack(side="left", padx=(4, 10))
        ttk.Label(row1, text="波特率:").pack(side="left")
        self.baud_cb = ttk.Combobox(
            row1, width=10, state="readonly",
            values=["115200", "230400", "460800", "921600"],
        )
        self.baud_cb.set("115200")
        self.baud_cb.pack(side="left", padx=(4, 10))
        ttk.Button(row1, text="刷新串口", command=self.refresh_ports).pack(side="left")

        # 行2: 板端路径
        row2 = ttk.Frame(frm)
        row2.pack(fill="x", pady=(0, 6))
        ttk.Label(row2, text="板端路径:").pack(side="left")
        self.board_var = tk.StringVar(value=DEFAULT_BOARD_PATH)
        ttk.Entry(row2, textvariable=self.board_var).pack(
            side="left", fill="x", expand=True, padx=4
        )
        ttk.Button(row2, text="PCM默认", command=self.use_pcm_default).pack(side="left")

        # 行3: 本地保存
        row3 = ttk.Frame(frm)
        row3.pack(fill="x", pady=(0, 6))
        ttk.Label(row3, text="保存到:").pack(side="left")
        default_save = os.path.join(DEFAULT_SAVE_DIR, "last.pcm")
        self.save_var = tk.StringVar(value=default_save)
        ttk.Entry(row3, textvariable=self.save_var).pack(
            side="left", fill="x", expand=True, padx=4
        )
        ttk.Button(row3, text="浏览...", command=self.pick_save).pack(side="left")

        # 行4: 操作
        row4 = ttk.Frame(frm)
        row4.pack(fill="x", pady=(0, 6))
        self.pull_btn = ttk.Button(row4, text="提取文件", command=self.start_pull)
        self.pull_btn.pack(side="left")
        self.prog = ttk.Progressbar(row4, maximum=100)
        self.prog.pack(side="left", fill="x", expand=True, padx=(10, 0))
        self.prog_label = ttk.Label(row4, text="0%", width=6, anchor="e")
        self.prog_label.pack(side="left")

        tip = ttk.Label(
            frm,
            text="提示: 先关掉占用该串口的终端; 板端需已挂载 littlefs 且文件存在。"
                 " PCM 为 s16le/mono/44100,无 WAV 头。",
            wraplength=640,
            foreground="#555",
        )
        tip.pack(fill="x", pady=(0, 6))

        self.log_text = tk.Text(
            frm, height=18, state="disabled",
            font=("Consolas", 9), bg="#101418", fg="#c8d3e0",
        )
        self.log_text.pack(fill="both", expand=True)

        self.refresh_ports()
        self.root.after(50, self.poll_queue)

    def use_pcm_default(self):
        self.board_var.set(DEFAULT_BOARD_PATH)
        self.save_var.set(os.path.join(DEFAULT_SAVE_DIR, "last.pcm"))

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and self.port_cb.get() not in ports:
            self.port_cb.set(ports[0])

    def pick_save(self):
        initial = self.save_var.get() or DEFAULT_SAVE_DIR
        initialdir = os.path.dirname(initial) if initial else DEFAULT_SAVE_DIR
        path = filedialog.asksaveasfilename(
            title="选择本地保存路径",
            initialdir=initialdir if os.path.isdir(initialdir) else os.path.expanduser("~"),
            initialfile=os.path.basename(initial) or "last.pcm",
            defaultextension=".pcm",
            filetypes=[
                ("PCM/二进制", "*.pcm *.bin *.dat"),
                ("文本", "*.txt"),
                ("所有文件", "*.*"),
            ],
        )
        if path:
            self.save_var.set(path)

    def log(self, msg: str):
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
                    self.pull_btn["state"] = "normal"
        except queue.Empty:
            pass
        self.root.after(50, self.poll_queue)

    def start_pull(self):
        if self.busy:
            return
        port = self.port_cb.get()
        board_path = (self.board_var.get() or "").strip()
        save_path = (self.save_var.get() or "").strip()
        if not port:
            messagebox.showwarning("提示", "请选择串口(板子 msh 控制台口)")
            return
        if not board_path.startswith("/"):
            messagebox.showwarning("提示", "板端路径请用绝对路径,例如 /pcm/last.pcm")
            return
        if not save_path:
            messagebox.showwarning("提示", "请填写本地保存路径")
            return

        self.busy = True
        self.pull_btn["state"] = "disabled"
        self.prog["value"] = 0
        self.prog_label["text"] = "0%"
        threading.Thread(
            target=self.worker,
            args=(port, int(self.baud_cb.get()), board_path, save_path),
            daemon=True,
        ).start()

    def worker(self, port: str, baud: int, board_path: str, save_path: str):
        ser = None
        try:
            self.log("打开 %s @ %d ..." % (port, baud))
            self.log("(若失败请先关闭占用该串口的终端软件)")
            ser = serial.Serial(port, baud, timeout=0.05)

            # 唤醒 msh
            ser.reset_input_buffer()
            ser.write(b"\r\n")
            time.sleep(0.25)
            # 读掉提示符
            _ = ser.read(256)
            ser.reset_input_buffer()

            cmd = "sy %s\r\n" % board_path
            self.log("发送命令: %s" % cmd.strip())
            ser.write(cmd.encode("ascii", "ignore"))
            ser.flush()

            # 短暂收一点回显(不要太久,马上要进 YMODEM)
            time.sleep(0.15)
            echo = ser.read(512)
            if echo:
                text = echo.decode("utf-8", "ignore")
                for line in text.splitlines():
                    line = line.strip()
                    if line and line not in ("C",):
                        self.log("[板子] " + line)

            # 若板端立刻报错(文件不存在),通常不会进入 YMODEM
            lower = echo.decode("utf-8", "ignore").lower()
            if "error open file" in lower or "invalid file path" in lower:
                raise RuntimeError("板端打开文件失败,请先 ls 确认路径存在")

            recv = YmodemReceiver(ser, self.log, self.set_progress)
            t0 = time.time()
            info = recv.receive_to_path(save_path)
            self.log("耗时 %.1f 秒" % (time.time() - t0))
            self.log("★ 提取成功: %s (%d bytes)" % (info["path"], info["saved"]))

            # PCM 使用提示
            if save_path.lower().endswith(".pcm") or board_path.endswith(".pcm"):
                self.log("---- 播放提示 ----")
                self.log("格式通常: s16le mono 44100 (以 /pcm/last.txt 为准)")
                self.log(
                    "ffmpeg -f s16le -ar 44100 -ac 1 -i \"%s\" \"%s\""
                    % (save_path, os.path.splitext(save_path)[0] + ".wav")
                )

        except Exception as exc:
            self.log("出错: %s" % exc)
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
            self.log("串口已关闭,可重新用终端连接")
            self.msg_q.put(("done", None))


if __name__ == "__main__":
    os.makedirs(DEFAULT_SAVE_DIR, exist_ok=True)
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista")
    except Exception:
        pass
    App(root)
    root.mainloop()
