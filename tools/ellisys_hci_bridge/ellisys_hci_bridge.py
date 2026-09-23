#!/usr/bin/env python3
"""UART BTSnoop to Ellisys HCI Injection UDP bridge."""

from __future__ import annotations

import datetime as dt
import json
import os
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Optional

import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk


APP_NAME = "Ellisys HCI 实时抓取工具"
APP_VERSION = "1.0.0"

BTSNOOP_MAGIC = b"btsnoop\x00"
BTSNOOP_HEADER_SIZE = 16
BTSNOOP_RECORD_HEADER_SIZE = 24
BTSNOOP_VERSION = 1
BTSNOOP_DLT_H4 = 1002
MAX_BTSNOOP_PACKET_SIZE = 65535

H4_COMMAND = 0x01
H4_ACL = 0x02
H4_SCO = 0x03
H4_EVENT = 0x04
H4_ISO = 0x05
SUPPORTED_H4_TYPES = {H4_COMMAND, H4_ACL, H4_SCO, H4_EVENT, H4_ISO}

ELLISYS_SERVICE_ID_HCI = 0x0002
ELLISYS_SERVICE_VERSION = 0x01
ELLISYS_OBJECT_DATETIME_NS = 0x02
ELLISYS_OBJECT_BITRATE = 0x80
ELLISYS_OBJECT_HCI_PACKET_TYPE = 0x81
ELLISYS_OBJECT_HCI_PACKET_DATA = 0x82
ELLISYS_OBJECT_CONTROLLER_INDEX = 0x83

ELLISYS_HCI_COMMAND = 0x01
ELLISYS_HCI_ACL_FROM_HOST = 0x02
ELLISYS_HCI_ACL_FROM_CONTROLLER = 0x82
ELLISYS_HCI_SCO_FROM_HOST = 0x03
ELLISYS_HCI_SCO_FROM_CONTROLLER = 0x83
ELLISYS_HCI_EVENT = 0x84
ELLISYS_HCI_ISO_FROM_HOST = 0x05
ELLISYS_HCI_ISO_FROM_CONTROLLER = 0x85

DEFAULT_SERIAL_BAUD = 2_000_000
DEFAULT_HCI_BITRATE = 921_600
DEFAULT_ELLISYS_HOST = "127.0.0.1"
DEFAULT_ELLISYS_PORT = 24352

BAUD_RATES = (
    "115200",
    "460800",
    "921600",
    "1000000",
    "1382400",
    "2000000",
    "2764800",
    "3000000",
)

PACKET_TYPE_NAMES = {
    H4_COMMAND: "Command",
    H4_ACL: "ACL",
    H4_SCO: "SCO",
    H4_EVENT: "Event",
    H4_ISO: "ISO",
}


def application_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


CONFIG_FILENAME = "config.json"


def config_path() -> Path:
    return application_dir() / CONFIG_FILENAME


def load_config() -> dict:
    path = config_path()
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_config(data: dict) -> None:
    config_path().write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


@dataclass(frozen=True)
class BtsnoopRecord:
    original_length: int
    included_length: int
    flags: int
    cumulative_drops: int
    timestamp_us: int
    packet_data: bytes

    @property
    def direction_to_host(self) -> bool:
        return bool(self.flags & 0x01)

    @property
    def h4_type(self) -> int:
        return self.packet_data[0]

    @property
    def hci_data(self) -> bytes:
        return self.packet_data[1:]


@dataclass(frozen=True)
class ParserResult:
    records: tuple[BtsnoopRecord, ...]
    new_headers: int


class BtsnoopStreamParser:
    """Incrementally parse a possibly fragmented BTSnoop H4 byte stream."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.header_seen = False
        self.header_count = 0
        self.discarded_bytes = 0
        self.invalid_records = 0

    def feed(self, data: bytes) -> ParserResult:
        if data:
            self.buffer.extend(data)

        records: list[BtsnoopRecord] = []
        new_headers = 0

        while True:
            if not self.header_seen:
                if not self._find_and_consume_header():
                    break
                new_headers += 1
                continue

            if self.buffer.startswith(BTSNOOP_MAGIC):
                if len(self.buffer) < BTSNOOP_HEADER_SIZE:
                    break
                if self._valid_header_at(0):
                    self.header_seen = False
                    continue

            if len(self.buffer) < BTSNOOP_RECORD_HEADER_SIZE:
                break

            original_length, included_length, flags, drops = struct.unpack_from(
                ">IIII", self.buffer, 0
            )
            timestamp_us = struct.unpack_from(">Q", self.buffer, 16)[0]

            if (
                included_length < 1
                or included_length > MAX_BTSNOOP_PACKET_SIZE
                or original_length < included_length
            ):
                self.invalid_records += 1
                self.header_seen = False
                self.discarded_bytes += 1
                del self.buffer[0]
                continue

            record_size = BTSNOOP_RECORD_HEADER_SIZE + included_length
            if len(self.buffer) < record_size:
                # A reset can interrupt a record. Only resynchronize to a fully
                # validated file header while the claimed record is incomplete.
                header_offset = self.buffer.find(BTSNOOP_MAGIC, 1)
                if header_offset >= 0 and self._valid_header_at(header_offset):
                    self.discarded_bytes += header_offset
                    del self.buffer[:header_offset]
                    self.header_seen = False
                    continue
                break

            packet_data = bytes(self.buffer[BTSNOOP_RECORD_HEADER_SIZE:record_size])
            del self.buffer[:record_size]

            if packet_data[0] not in SUPPORTED_H4_TYPES:
                self.invalid_records += 1
                continue

            records.append(
                BtsnoopRecord(
                    original_length=original_length,
                    included_length=included_length,
                    flags=flags,
                    cumulative_drops=drops,
                    timestamp_us=timestamp_us,
                    packet_data=packet_data,
                )
            )

        return ParserResult(tuple(records), new_headers)

    def _find_and_consume_header(self) -> bool:
        offset = self.buffer.find(BTSNOOP_MAGIC)
        if offset < 0:
            keep = min(len(self.buffer), len(BTSNOOP_MAGIC) - 1)
            discard = len(self.buffer) - keep
            if discard:
                self.discarded_bytes += discard
                del self.buffer[:discard]
            return False

        if len(self.buffer) < offset + BTSNOOP_HEADER_SIZE:
            if offset:
                self.discarded_bytes += offset
                del self.buffer[:offset]
            return False

        version, data_link_type = struct.unpack_from(">II", self.buffer, offset + 8)
        if version != BTSNOOP_VERSION or data_link_type != BTSNOOP_DLT_H4:
            self.invalid_records += 1
            self.discarded_bytes += offset + 1
            del self.buffer[: offset + 1]
            return False

        self.discarded_bytes += offset
        del self.buffer[: offset + BTSNOOP_HEADER_SIZE]
        self.header_seen = True
        self.header_count += 1
        return True

    def _valid_header_at(self, offset: int) -> bool:
        if len(self.buffer) < offset + BTSNOOP_HEADER_SIZE:
            return False
        version, data_link_type = struct.unpack_from(">II", self.buffer, offset + 8)
        return version == BTSNOOP_VERSION and data_link_type == BTSNOOP_DLT_H4


def ellisys_packet_type(h4_type: int, direction_to_host: bool) -> int:
    if h4_type == H4_COMMAND:
        return ELLISYS_HCI_COMMAND
    if h4_type == H4_EVENT:
        return ELLISYS_HCI_EVENT
    if h4_type == H4_ACL:
        return ELLISYS_HCI_ACL_FROM_CONTROLLER if direction_to_host else ELLISYS_HCI_ACL_FROM_HOST
    if h4_type == H4_SCO:
        return ELLISYS_HCI_SCO_FROM_CONTROLLER if direction_to_host else ELLISYS_HCI_SCO_FROM_HOST
    if h4_type == H4_ISO:
        return ELLISYS_HCI_ISO_FROM_CONTROLLER if direction_to_host else ELLISYS_HCI_ISO_FROM_HOST
    raise ValueError(f"不支持的 H4 包类型: 0x{h4_type:02X}")


def build_ellisys_hci_packet(
    packet_time: dt.datetime,
    bit_rate: float,
    packet_type: int,
    packet_data: bytes,
    controller_index: int = 0,
) -> bytes:
    """Build one UDP payload according to the official Ellisys Injection API."""
    if packet_time.tzinfo is None:
        packet_time = packet_time.replace(tzinfo=dt.timezone.utc)
    else:
        packet_time = packet_time.astimezone(dt.timezone.utc)

    nanoseconds = (
        ((packet_time.hour * 60 + packet_time.minute) * 60 + packet_time.second)
        * 1_000_000_000
        + packet_time.microsecond * 1_000
    )

    payload = bytearray()
    payload.extend(struct.pack("<HB", ELLISYS_SERVICE_ID_HCI, ELLISYS_SERVICE_VERSION))
    payload.append(ELLISYS_OBJECT_DATETIME_NS)
    payload.extend(struct.pack("<HBB", packet_time.year, packet_time.month, packet_time.day))
    payload.extend(nanoseconds.to_bytes(6, "little"))
    payload.extend((ELLISYS_OBJECT_CONTROLLER_INDEX, controller_index & 0xFF))
    payload.append(ELLISYS_OBJECT_BITRATE)
    payload.extend(struct.pack("<f", float(bit_rate)))
    payload.extend((ELLISYS_OBJECT_HCI_PACKET_TYPE, packet_type & 0xFF))
    payload.append(ELLISYS_OBJECT_HCI_PACKET_DATA)
    payload.extend(packet_data)
    return bytes(payload)


@dataclass(frozen=True)
class CaptureConfig:
    serial_port: str
    serial_baud: int
    ellisys_host: str
    ellisys_port: int
    hci_bitrate: float
    controller_index: int
    raw_capture_path: Optional[Path]


class CaptureWorker:
    def __init__(self, config: CaptureConfig, events: "queue.Queue[tuple]") -> None:
        self.config = config
        self.events = events
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._serial: Optional[serial.Serial] = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="ellisys-hci-bridge", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._serial:
            try:
                self._serial.cancel_read()
            except (AttributeError, OSError, serial.SerialException):
                pass

    def join(self, timeout: float = 1.0) -> None:
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout)

    def is_running(self) -> bool:
        return bool(self._thread and self._thread.is_alive())

    def _run(self) -> None:
        parser = BtsnoopStreamParser()
        udp_socket: Optional[socket.socket] = None
        raw_file: Optional[BinaryIO] = None
        packet_count = 0
        hci_bytes = 0
        udp_errors = 0
        first_timestamp_us: Optional[int] = None
        first_packet_time = dt.datetime.now(dt.timezone.utc)
        last_stats_time = 0.0
        last_record_event_time = 0.0

        try:
            endpoint = socket.getaddrinfo(
                self.config.ellisys_host,
                self.config.ellisys_port,
                socket.AF_INET,
                socket.SOCK_DGRAM,
            )[0][-1]
            udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp_socket.connect(endpoint)

            self._serial = serial.Serial(
                port=self.config.serial_port,
                baudrate=self.config.serial_baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                rtscts=False,
                dsrdtr=False,
                xonxoff=False,
            )
            self.events.put(("started", endpoint))

            while not self._stop_event.is_set():
                chunk = self._serial.read(self._serial.in_waiting or 1)
                if not chunk:
                    continue

                result = parser.feed(chunk)
                if result.new_headers:
                    first_timestamp_us = None
                    first_packet_time = dt.datetime.now(dt.timezone.utc)
                    self.events.put(("header", parser.header_count))
                    if self.config.raw_capture_path:
                        if raw_file:
                            raw_file.flush()
                            raw_file.close()
                        raw_path = self.config.raw_capture_path
                        if parser.header_count > 1:
                            raw_path = raw_path.with_name(
                                f"{raw_path.stem}_part{parser.header_count}{raw_path.suffix}"
                            )
                        raw_path.parent.mkdir(parents=True, exist_ok=True)
                        raw_file = raw_path.open("wb")
                        raw_file.write(BTSNOOP_MAGIC + struct.pack(">II", BTSNOOP_VERSION, BTSNOOP_DLT_H4))
                        self.events.put(("raw_file", raw_path))

                for record in result.records:
                    if first_timestamp_us is None or record.timestamp_us < first_timestamp_us:
                        first_timestamp_us = record.timestamp_us
                        first_packet_time = dt.datetime.now(dt.timezone.utc)

                    relative_us = record.timestamp_us - first_timestamp_us
                    packet_time = first_packet_time + dt.timedelta(microseconds=relative_us)
                    injected_type = ellisys_packet_type(record.h4_type, record.direction_to_host)
                    injection_packet = build_ellisys_hci_packet(
                        packet_time,
                        self.config.hci_bitrate,
                        injected_type,
                        record.hci_data,
                        self.config.controller_index,
                    )
                    if raw_file:
                        raw_file.write(
                            struct.pack(
                                ">IIIIQ",
                                record.original_length,
                                record.included_length,
                                record.flags,
                                record.cumulative_drops,
                                record.timestamp_us,
                            )
                        )
                        raw_file.write(record.packet_data)

                    try:
                        udp_socket.send(injection_packet)
                    except OSError as exc:
                        udp_errors += 1
                        if udp_errors <= 3:
                            self.events.put(("warning", f"UDP 发送失败: {exc}"))
                        continue

                    packet_count += 1
                    hci_bytes += len(record.hci_data)
                    now = time.monotonic()
                    if now - last_record_event_time >= 0.01:
                        last_record_event_time = now
                        self.events.put(("record", record, packet_time))

                now = time.monotonic()
                if now - last_stats_time >= 0.2:
                    last_stats_time = now
                    self.events.put(
                        (
                            "stats",
                            packet_count,
                            hci_bytes,
                            parser.invalid_records,
                            parser.discarded_bytes,
                            udp_errors,
                        )
                    )

        except (OSError, ValueError, serial.SerialException) as exc:
            if not self._stop_event.is_set():
                self.events.put(("fatal", str(exc)))
        finally:
            if raw_file:
                try:
                    raw_file.flush()
                    raw_file.close()
                except OSError:
                    pass
            if self._serial:
                try:
                    self._serial.close()
                except serial.SerialException:
                    pass
            self._serial = None
            if udp_socket:
                udp_socket.close()
            self.events.put(("stopped",))


def find_ellisys_executable() -> Optional[Path]:
    candidates = [
        Path(r"C:\Program Files (x86)\Ellisys\Ellisys Bluetooth Analyzer\Ellisys.BluetoothAnalyzer.exe"),
        Path(r"D:\Program Files (x86)\Ellisys\Ellisys Bluetooth Analyzer\Ellisys.BluetoothAnalyzer.exe"),
    ]

    if os.name == "nt":
        try:
            import winreg

            registry_paths = (
                r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
                r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
            )
            for registry_path in registry_paths:
                try:
                    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, registry_path) as root:
                        for index in range(winreg.QueryInfoKey(root)[0]):
                            try:
                                with winreg.OpenKey(root, winreg.EnumKey(root, index)) as item:
                                    name = winreg.QueryValueEx(item, "DisplayName")[0]
                                    if name != "Ellisys Bluetooth Analyzer":
                                        continue
                                    install_dir = Path(winreg.QueryValueEx(item, "InstallLocation")[0])
                                    candidates.insert(0, install_dir / "Ellisys.BluetoothAnalyzer.exe")
                            except OSError:
                                continue
                except OSError:
                    continue
        except (ImportError, OSError):
            pass

    return next((path for path in candidates if path.is_file()), None)


class EllisysHciBridgeApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(f"{APP_NAME} v{APP_VERSION}")
        self.geometry("1120x720")
        self.minsize(980, 620)

        self.events: "queue.Queue[tuple]" = queue.Queue()
        self.worker: Optional[CaptureWorker] = None
        self._stopping = False

        self.port_var = tk.StringVar()
        self.serial_baud_var = tk.StringVar(value=str(DEFAULT_SERIAL_BAUD))
        self.host_var = tk.StringVar(value=DEFAULT_ELLISYS_HOST)
        self.udp_port_var = tk.StringVar(value=str(DEFAULT_ELLISYS_PORT))
        self.hci_bitrate_var = tk.StringVar(value=str(DEFAULT_HCI_BITRATE))
        self.controller_index_var = tk.StringVar(value="0")
        self.open_ellisys_var = tk.BooleanVar(value=True)
        self.ellisys_path_var = tk.StringVar()
        self.save_raw_var = tk.BooleanVar(value=True)
        self.capture_dir_var = tk.StringVar(value=str(application_dir() / "captures"))
        self.status_var = tk.StringVar(value="未启动")
        self.packet_count_var = tk.StringVar(value="0")
        self.byte_count_var = tk.StringVar(value="0 B")
        self.drop_count_var = tk.StringVar(value="0")
        self.error_count_var = tk.StringVar(value="0")

        self._configure_style()
        self._build_widgets()
        self._refresh_ports()
        self._load_config()
        self.after(80, self._process_events)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Capture.TButton", padding=(12, 8), font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Status.TLabel", font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("MetricValue.TLabel", font=("Segoe UI", 15, "bold"))

    def _build_widgets(self) -> None:
        root_pane = ttk.Panedwindow(self, orient=tk.HORIZONTAL)
        root_pane.pack(fill="both", expand=True, padx=10, pady=10)

        left = ttk.Frame(root_pane, width=270)
        root_pane.add(left, weight=0)
        left.columnconfigure(0, weight=1)

        serial_frame = ttk.LabelFrame(left, text="BTSnoop 串口输入")
        serial_frame.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        serial_frame.columnconfigure(0, weight=1)

        ttk.Label(serial_frame, text="串口").grid(row=0, column=0, sticky="w", padx=8, pady=(10, 2))
        port_row = ttk.Frame(serial_frame)
        port_row.grid(row=1, column=0, sticky="ew", padx=8)
        port_row.columnconfigure(0, weight=1)
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, state="readonly")
        self.port_combo.grid(row=0, column=0, sticky="ew")
        ttk.Button(port_row, text="刷新", command=self._refresh_ports, width=6).grid(row=0, column=1, padx=(5, 0))

        ttk.Label(serial_frame, text="波特率 (8N1)").grid(row=2, column=0, sticky="w", padx=8, pady=(8, 2))
        self.baud_combo = ttk.Combobox(serial_frame, textvariable=self.serial_baud_var, values=BAUD_RATES)
        self.baud_combo.grid(row=3, column=0, sticky="ew", padx=8, pady=(0, 10))

        ellisys_frame = ttk.LabelFrame(left, text="Ellisys HCI Injection")
        ellisys_frame.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        ellisys_frame.columnconfigure(1, weight=1)

        ttk.Label(ellisys_frame, text="目标 IP").grid(row=0, column=0, sticky="w", padx=(8, 5), pady=(10, 4))
        ttk.Entry(ellisys_frame, textvariable=self.host_var).grid(row=0, column=1, sticky="ew", padx=(0, 8), pady=(10, 4))
        ttk.Label(ellisys_frame, text="UDP 端口").grid(row=1, column=0, sticky="w", padx=(8, 5), pady=4)
        ttk.Entry(ellisys_frame, textvariable=self.udp_port_var).grid(row=1, column=1, sticky="ew", padx=(0, 8), pady=4)
        ttk.Label(ellisys_frame, text="HCI 速率").grid(row=2, column=0, sticky="w", padx=(8, 5), pady=4)
        ttk.Entry(ellisys_frame, textvariable=self.hci_bitrate_var).grid(row=2, column=1, sticky="ew", padx=(0, 8), pady=4)
        ttk.Label(ellisys_frame, text="控制器索引").grid(row=3, column=0, sticky="w", padx=(8, 5), pady=(4, 10))
        ttk.Spinbox(ellisys_frame, from_=0, to=255, textvariable=self.controller_index_var, width=8).grid(
            row=3, column=1, sticky="w", padx=(0, 8), pady=(4, 10)
        )

        output_frame = ttk.LabelFrame(left, text="抓取输出")
        output_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        output_frame.columnconfigure(0, weight=1)
        ttk.Checkbutton(output_frame, text="启动时打开 Ellisys", variable=self.open_ellisys_var).grid(
            row=0, column=0, sticky="w", padx=8, pady=(8, 2)
        )
        ellisys_path_row = ttk.Frame(output_frame)
        ellisys_path_row.grid(row=1, column=0, sticky="ew", padx=8, pady=2)
        ellisys_path_row.columnconfigure(0, weight=1)
        ttk.Entry(ellisys_path_row, textvariable=self.ellisys_path_var).grid(row=0, column=0, sticky="ew")
        ttk.Button(ellisys_path_row, text="浏览", width=6, command=self._browse_ellisys).grid(
            row=0, column=1, padx=(5, 0)
        )
        ttk.Button(ellisys_path_row, text="保存", width=6, command=self._save_ellisys_path).grid(
            row=0, column=2, padx=(5, 0)
        )
        ttk.Checkbutton(output_frame, text="保存原始 BTSnoop", variable=self.save_raw_var).grid(
            row=2, column=0, sticky="w", padx=8, pady=2
        )
        path_row = ttk.Frame(output_frame)
        path_row.grid(row=3, column=0, sticky="ew", padx=8, pady=(4, 8))
        path_row.columnconfigure(0, weight=1)
        ttk.Entry(path_row, textvariable=self.capture_dir_var).grid(row=0, column=0, sticky="ew")
        ttk.Button(path_row, text="...", width=3, command=self._choose_capture_dir).grid(row=0, column=1, padx=(5, 0))

        self.capture_button = ttk.Button(
            left,
            text="启动 Ellisys 实时抓取",
            style="Capture.TButton",
            command=self._toggle_capture,
        )
        self.capture_button.grid(row=3, column=0, sticky="ew", pady=(2, 6))
        ttk.Button(left, text="仅打开 Ellisys", command=self._open_ellisys).grid(row=4, column=0, sticky="ew")

        status_frame = ttk.LabelFrame(left, text="状态")
        status_frame.grid(row=5, column=0, sticky="ew", pady=(8, 0))
        ttk.Label(status_frame, textvariable=self.status_var, style="Status.TLabel", wraplength=235).pack(
            anchor="w", padx=8, pady=8
        )

        right = ttk.Frame(root_pane)
        root_pane.add(right, weight=1)
        right.columnconfigure(0, weight=1)
        right.rowconfigure(1, weight=4)
        right.rowconfigure(2, weight=2)

        metrics = ttk.Frame(right)
        metrics.grid(row=0, column=0, sticky="ew", padx=(10, 0), pady=(0, 8))
        for column in range(4):
            metrics.columnconfigure(column, weight=1)
        self._metric(metrics, 0, "已转发包", self.packet_count_var)
        self._metric(metrics, 1, "HCI 数据量", self.byte_count_var)
        self._metric(metrics, 2, "设备累计丢包", self.drop_count_var)
        self._metric(metrics, 3, "解析/UDP 错误", self.error_count_var)

        packets_frame = ttk.LabelFrame(right, text="实时 HCI 数据")
        packets_frame.grid(row=1, column=0, sticky="nsew", padx=(10, 0), pady=(0, 8))
        packets_frame.columnconfigure(0, weight=1)
        packets_frame.rowconfigure(0, weight=1)
        columns = ("time", "direction", "type", "length", "drops")
        self.packet_tree = ttk.Treeview(packets_frame, columns=columns, show="headings", height=14)
        headings = {
            "time": "时间",
            "direction": "方向",
            "type": "H4 类型",
            "length": "长度",
            "drops": "累计丢包",
        }
        widths = {"time": 130, "direction": 130, "type": 110, "length": 80, "drops": 90}
        for column in columns:
            self.packet_tree.heading(column, text=headings[column])
            self.packet_tree.column(column, width=widths[column], anchor="center", stretch=True)
        scrollbar = ttk.Scrollbar(packets_frame, orient=tk.VERTICAL, command=self.packet_tree.yview)
        self.packet_tree.configure(yscrollcommand=scrollbar.set)
        self.packet_tree.grid(row=0, column=0, sticky="nsew", padx=(5, 0), pady=5)
        scrollbar.grid(row=0, column=1, sticky="ns", padx=(0, 5), pady=5)

        log_frame = ttk.LabelFrame(right, text="运行日志")
        log_frame.grid(row=2, column=0, sticky="nsew", padx=(10, 0))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(1, weight=1)
        toolbar = ttk.Frame(log_frame)
        toolbar.grid(row=0, column=0, sticky="e", padx=5, pady=(4, 0))
        ttk.Button(toolbar, text="清空", command=self._clear_log).pack(side="left", padx=(0, 5))
        ttk.Button(toolbar, text="保存日志", command=self._save_log).pack(side="left")
        self.log_text = scrolledtext.ScrolledText(log_frame, height=8, wrap="word", state="disabled")
        self.log_text.grid(row=1, column=0, sticky="nsew", padx=5, pady=5)
        self.log_text.tag_config("error", foreground="#b42318")
        self.log_text.tag_config("ok", foreground="#157f3b")

    @staticmethod
    def _metric(parent: ttk.Frame, column: int, title: str, variable: tk.StringVar) -> None:
        frame = ttk.Frame(parent, padding=(8, 4))
        frame.grid(row=0, column=column, sticky="ew")
        ttk.Label(frame, text=title).pack(anchor="w")
        ttk.Label(frame, textvariable=variable, style="MetricValue.TLabel").pack(anchor="w")

    def _refresh_ports(self) -> None:
        current = self.port_var.get()
        ports = list(serial.tools.list_ports.comports())
        devices = [port.device for port in ports]
        self.port_combo.configure(values=devices)
        if current in devices:
            self.port_var.set(current)
        elif devices:
            self.port_var.set(devices[0])
        else:
            self.port_var.set("")

    def _choose_capture_dir(self) -> None:
        path = filedialog.askdirectory(title="选择 BTSnoop 保存目录", initialdir=self.capture_dir_var.get())
        if path:
            self.capture_dir_var.set(path)

    def _load_config(self) -> None:
        data = load_config()
        path = data.get("ellisys_path", "")
        if path:
            self.ellisys_path_var.set(path)

    def _browse_ellisys(self) -> None:
        current = self.ellisys_path_var.get().strip()
        initialdir = str(Path(current).parent) if current else None
        path = filedialog.askopenfilename(
            title="选择 Ellisys Bluetooth Analyzer 可执行文件",
            initialdir=initialdir,
            filetypes=(("可执行文件", "*.exe"), ("所有文件", "*.*")),
        )
        if path:
            self.ellisys_path_var.set(path)

    def _save_ellisys_path(self) -> None:
        data = load_config()
        data["ellisys_path"] = self.ellisys_path_var.get().strip()
        try:
            save_config(data)
        except OSError as exc:
            messagebox.showerror("保存失败", str(exc))
            return
        self._log("已保存 Ellisys 路径配置", "ok")

    def _toggle_capture(self) -> None:
        if self.worker and self.worker.is_running():
            self._stop_capture()
        else:
            self._start_capture()

    def _start_capture(self) -> None:
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning("提示", "请选择 BTSnoop 串口")
            return

        try:
            serial_baud = int(self.serial_baud_var.get())
            udp_port = int(self.udp_port_var.get())
            hci_bitrate = float(self.hci_bitrate_var.get())
            controller_index = int(self.controller_index_var.get())
            if serial_baud <= 0 or hci_bitrate <= 0:
                raise ValueError
            if not 1 <= udp_port <= 65535 or not 0 <= controller_index <= 255:
                raise ValueError
        except ValueError:
            messagebox.showerror("配置错误", "请检查波特率、UDP 端口、HCI 速率和控制器索引")
            return

        raw_path = None
        if self.save_raw_var.get():
            capture_dir = Path(self.capture_dir_var.get().strip() or application_dir() / "captures")
            raw_path = capture_dir / f"hci_{time.strftime('%Y%m%d_%H%M%S')}.btsnoop"

        if self.open_ellisys_var.get():
            self._open_ellisys(show_error=False)

        self.packet_count_var.set("0")
        self.byte_count_var.set("0 B")
        self.drop_count_var.set("0")
        self.error_count_var.set("0")
        for item in self.packet_tree.get_children():
            self.packet_tree.delete(item)

        config = CaptureConfig(
            serial_port=port,
            serial_baud=serial_baud,
            ellisys_host=self.host_var.get().strip(),
            ellisys_port=udp_port,
            hci_bitrate=hci_bitrate,
            controller_index=controller_index,
            raw_capture_path=raw_path,
        )
        self.worker = CaptureWorker(config, self.events)
        self.worker.start()
        self.capture_button.configure(text="停止实时抓取")
        self.status_var.set("正在打开串口...")
        self._log(f"准备连接 {port} @ {serial_baud}，目标 UDP {config.ellisys_host}:{udp_port}")
        if raw_path:
            self._log(f"BTSnoop 保存到 {raw_path}")

    def _stop_capture(self) -> None:
        if self.worker:
            self.status_var.set("正在停止...")
            self.worker.stop()

    def _open_ellisys(self, show_error: bool = True) -> None:
        configured = self.ellisys_path_var.get().strip()
        executable: Optional[Path] = None

        if configured:
            candidate = Path(configured)
            if candidate.is_file():
                executable = candidate
            else:
                if show_error:
                    messagebox.showerror("路径无效", f"配置的 Ellisys 路径不存在：\n{configured}")
                self._log("配置的 Ellisys 路径不存在，请检查", "error")
                return
        else:
            executable = find_ellisys_executable()
            if executable:
                self.ellisys_path_var.set(str(executable))
                self._log(f"已自动定位 Ellisys: {executable}", "ok")

        if not executable:
            if show_error:
                messagebox.showerror("未找到 Ellisys", "未找到 Ellisys.BluetoothAnalyzer.exe，请在界面中填写其路径")
            self._log("未找到 Ellisys Bluetooth Analyzer，请配置路径", "error")
            return

        try:
            creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
            running = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq Ellisys.BluetoothAnalyzer.exe", "/NH"],
                capture_output=True,
                text=True,
                creationflags=creation_flags,
                check=False,
            )
            if "Ellisys.BluetoothAnalyzer.exe" not in running.stdout:
                subprocess.Popen([str(executable)], cwd=str(executable.parent))
                self._log(f"已打开 Ellisys: {executable}", "ok")
        except OSError as exc:
            if show_error:
                messagebox.showerror("启动失败", str(exc))
            self._log(f"启动 Ellisys 失败: {exc}", "error")

    def _process_events(self) -> None:
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break

            kind = event[0]
            if kind == "started":
                self.status_var.set("等待 BTSnoop 文件头，请复位设备")
                self._log(f"串口和 UDP 已就绪，Ellisys 目标 {event[1][0]}:{event[1][1]}", "ok")
            elif kind == "header":
                self.status_var.set("正在实时转发到 Ellisys")
                self._log(f"识别到标准 BTSnoop H4 文件头（第 {event[1]} 次）", "ok")
            elif kind == "raw_file":
                self._log(f"正在保存有效 BTSnoop: {event[1]}")
            elif kind == "record":
                self._show_record(event[1], event[2])
            elif kind == "stats":
                _, packets, byte_count, invalid, _discarded, udp_errors = event
                self.packet_count_var.set(f"{packets:,}")
                self.byte_count_var.set(self._format_bytes(byte_count))
                self.error_count_var.set(str(invalid + udp_errors))
            elif kind == "warning":
                self._log(event[1], "error")
            elif kind == "fatal":
                self.status_var.set("抓取失败")
                self._log(f"抓取失败: {event[1]}", "error")
                messagebox.showerror("抓取失败", event[1])
            elif kind == "stopped":
                self.capture_button.configure(text="启动 Ellisys 实时抓取")
                if self.status_var.get() not in ("抓取失败",):
                    self.status_var.set("已停止")
                self._log("实时抓取已停止")

        if not self._stopping:
            self.after(80, self._process_events)

    def _show_record(self, record: BtsnoopRecord, packet_time: dt.datetime) -> None:
        direction = "Controller -> Host" if record.direction_to_host else "Host -> Controller"
        values = (
            packet_time.astimezone().strftime("%H:%M:%S.%f")[:-3],
            direction,
            PACKET_TYPE_NAMES.get(record.h4_type, f"0x{record.h4_type:02X}"),
            len(record.hci_data),
            record.cumulative_drops,
        )
        self.packet_tree.insert("", "end", values=values)
        children = self.packet_tree.get_children()
        if len(children) > 1000:
            self.packet_tree.delete(*children[:100])
        self.packet_tree.yview_moveto(1.0)
        self.drop_count_var.set(str(record.cumulative_drops))

    @staticmethod
    def _format_bytes(value: int) -> str:
        if value < 1024:
            return f"{value} B"
        if value < 1024 * 1024:
            return f"{value / 1024:.1f} KB"
        return f"{value / (1024 * 1024):.1f} MB"

    def _log(self, text: str, tag: Optional[str] = None) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.configure(state="normal")
        self.log_text.insert(tk.END, f"[{timestamp}] {text}\n", tag or ())
        self.log_text.see(tk.END)
        self.log_text.configure(state="disabled")

    def _clear_log(self) -> None:
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state="disabled")

    def _save_log(self) -> None:
        content = self.log_text.get("1.0", tk.END).strip()
        if not content:
            return
        path = filedialog.asksaveasfilename(
            title="保存运行日志",
            defaultextension=".txt",
            initialfile=f"ellisys_bridge_{time.strftime('%Y%m%d_%H%M%S')}.txt",
            filetypes=(("Text", "*.txt"), ("All files", "*.*")),
        )
        if path:
            try:
                Path(path).write_text(content, encoding="utf-8")
            except OSError as exc:
                messagebox.showerror("保存失败", str(exc))

    def _on_close(self) -> None:
        self._stopping = True
        if self.worker:
            self.worker.stop()
            self.worker.join(1.0)
        self.destroy()


def main() -> None:
    app = EllisysHciBridgeApp()
    app.mainloop()


if __name__ == "__main__":
    main()
