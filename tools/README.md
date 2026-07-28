# tools - 上位机小工具

## file_pull.py — 从板子提取文件

用串口 YMODEM 接收板端 `sy` 发出的文件（对齐 RT-Thread rym）。

### 依赖

```bash
pip install pyserial
```

`tkinter` 一般随 Python 自带。

### 用法

1. **先关掉**占用调试串口的终端（XShell / MobaXterm 等）
2. 运行：

```bash
cd tools
python file_pull.py
```

3. UI 里选：
   - **串口**：板子 msh 控制台（uart1）
   - **板端路径**：默认 `/pcm/last.pcm`
   - **保存到**：本地路径，默认 `tools/pulled/last.pcm`
4. 点 **提取文件**

### 典型流程（录音）

```text
板子长按说话 → 松手
日志出现: pcm saved: /pcm/last.pcm ...
关掉终端独占串口
python file_pull.py → 提取 /pcm/last.pcm
```

也可先提取元信息：

- 板端路径填 `/pcm/last.txt`

### 听 PCM

`last.pcm` 一般是 **s16le / mono / 44100**，无 WAV 头：

```bash
ffmpeg -f s16le -ar 44100 -ac 1 -i last.pcm last.wav
```

或以 `last.txt` 里的 `sample_rate` 为准。

### 注意

- 必须独占串口；传文件时不要同时开终端
- 板端路径必须是已挂载文件系统上的绝对路径
- 每次 PTT 会覆盖 `/pcm/last.pcm`，只保留最新一截
