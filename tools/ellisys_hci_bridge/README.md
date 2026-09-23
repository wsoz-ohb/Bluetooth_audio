# Ellisys HCI Bridge

本工具从 UART 接收项目固件输出的标准 BTSnoop H4 字节流，逐条解析记录，并按 Ellisys 官方
HCI Injection API 封装成 UDP 数据报实时发送到 Ellisys Bluetooth Analyzer。

## 默认配置

- BTSnoop 串口：`2,000,000 baud, 8N1`
- Ellisys 地址：`127.0.0.1:24352/UDP`
- HCI 传输速率：`921,600 bit/s`（对应本项目控制器 HCI UART）
- BTSnoop 数据链路类型：`1002`（HCI UART H4）

## Ellisys 路径配置

在「抓取输出」区的 `Ellisys 路径` 输入框中填写 `Ellisys.BluetoothAnalyzer.exe` 的绝对路径（也可点
`浏览` 选择），再点 `保存`，工具会写入程序目录下的 `config.json`，下次启动自动填入。

- 留空时，工具会尝试按常见安装路径及 Windows 注册表自动定位 Ellisys。
- 手动填写的路径优先于自动查找；填写的路径无效时会在启动/打开 Ellisys 时提示。

## Ellisys 设置

1. 在 Ellisys 中打开 `Tools -> Options -> Injection API`。
2. 将 `UDP Listen Port` 设置为 `24352`，启用 `HCI` 服务。
3. 打开 `Record -> Select an analyzer`，选择 `Ellisys Injection API`。
4. 在 Ellisys 中开始 Recording。
5. 启动本工具的实时抓取，再复位开发板。固件只在启动时发送一次 16 字节 BTSnoop 文件头，
   因此工具必须先进入“等待 BTSnoop 文件头”状态。

收到文件头后，状态会变为“正在实时转发到 Ellisys”，数据出现在 Ellisys 的
`HCI Injection Overview` 中。有效 BTSnoop 记录默认同时保存到工具目录的 `captures` 子目录；
设备在一次抓取中再次复位时，工具会创建带 `_partN` 后缀的新文件，避免把两个文件头拼进同一文件。

## 运行源码

```powershell
python -m pip install -r requirements.txt
python .\ellisys_hci_bridge.py
```

## 测试

```powershell
python -m unittest -v test_ellisys_hci_bridge.py
```

## 生成 EXE

```powershell
.\build_exe.ps1
```

协议实现依据 Ellisys Bluetooth Analyzer 5.0.9728 安装包用户手册中的 Injection API 下载，
并与官方 `BtSnoopHciClient` 示例逐字段核对。UDP HCI 数据报结构为：

```text
Service(0x0002, v1)
+ DateTimeNs
+ ControllerIndex
+ Bitrate(float32 LE)
+ HciPacketType
+ HciPacketData（不包含 H4 type 字节）
```

## 许可证

本项目采用 [MIT License](LICENSE)。任何人均可使用、复制、修改和分发本项目，但必须保留项目的
版权声明和许可证文本，以注明来源于 Ellisys HCI Bridge 项目。
