# BT-STACK 使用手册

这份文档不讲蓝牙协议理论，重点只讲一件事：

- 在当前工程里，怎么使用这套 `BT-STACK` Host 协议栈

你已经有协议理论基础时，最重要的是先建立一个正确的“使用套路”。
这套组件里，`A2DP / HID / SPP / HFP / BLE GATT` 的接法本质上是同一套模式。

## 一句话先记住

在当前工程里，推荐使用顺序是：

1. `btstack_port_init(NULL)`
2. 你的 `profile_init() / service_register() / packet_handler_register()`
3. `btstack_port_start_thread()`

不要一上来自己手工调 `hci_init()`、`l2cap_init()`、`sdp_init()`。
这些基础层已经被 [port/btstack_port.c](./port/btstack_port.c) 和 [core/src/bt_host.c](./core/src/bt_host.c) 封装掉了。

## 先建立统一心智模型

无论你做的是 `A2DP`、`SPP`、`HID` 还是 `BLE GATT`，基本都是这 5 步：

1. 在 [core/config/bt_config.h](./core/config/bt_config.h) 打开基础能力
2. 调对应 profile 的 `xxx_init()`
3. 注册它需要的“描述”和“入口”
4. 注册事件回调和数据回调
5. 启动协议栈，后续全部按事件驱动工作

其中第 3 步，不同 profile 形式不同：

- Classic SPP / HID / A2DP：
  通常要注册 `SDP` 记录
- RFCOMM 类：
  还要注册 `RFCOMM service`
- A2DP：
  还要创建 `SEP / codec capabilities`
- BLE GATT：
  还要准备 `ATT DB` 或调用对应 `gatt-service` 初始化函数

## 当前工程里，profile 代码应该放在哪

推荐直接放在 [applications/bt_app.c](../applications/bt_app.c) 里，或者从这里拆出去。

推荐结构是：

```c
static int bt_profiles_init(void){
    // 这里放 A2DP / SPP / HID / BLE GATT 的初始化和注册
    return 0;
}

rt_err_t bt__init(void){
    int err;

    err = btstack_port_init(NULL);
    if (err != RT_EOK){
        return RT_ERROR;
    }

    err = bt_profiles_init();
    if (err != 0){
        return RT_ERROR;
    }

    err = btstack_port_start_thread();
    if (err != RT_EOK){
        return RT_ERROR;
    }

    return RT_EOK;
}
```

这个顺序的原因很简单：

- `btstack_port_init()` 已经把基础栈准备好了
- 但还没 `HCI_POWER_ON`
- 这时最适合把 profile、SDP、SEP、ATT DB 都注册好

## A2DP 和 AVDTP 的关系

如果你是做标准 `A2DP Source / Sink`，优先使用：

- [classic/a2dp_source.h](./core/classic/inc/classic/a2dp_source.h)
- [classic/a2dp_sink.h](./core/classic/inc/classic/a2dp_sink.h)

不要一开始就直接裸用 `avdtp_*`。

原因是：

- `a2dp_source_init()` 内部已经调用了 `a2dp_init() + avdtp_source_init()`
- `a2dp_sink_init()` 内部已经调用了 `a2dp_init() + avdtp_sink_init()`

也就是说：

- 你做标准 A2DP，直接用 `a2dp_*`
- 只有你真的想自定义 AVDTP 行为时，才需要直接掉到 `avdtp_*`

## 当前 A2DP 默认是自动配置模式

当前这份组件没有定义 `ENABLE_A2DP_EXPLICIT_CONFIG`。

这意味着默认行为是：

- A2DP 会自动做 SEP 发现
- 自动读取对端 capability
- 自动进入 set-config 流程

也就是说，最小用法下你不需要手工一条条去驱动 `AVDTP discover / get capability / set config`。

如果以后你想完全手工选 codec 配置，再考虑打开 `ENABLE_A2DP_EXPLICIT_CONFIG` 这种高级模式。

## A2DP 启动流转图

下面这部分不讲协议理论，只讲“在当前工程里，代码会怎么走”。

### A2DP 通用接法

```text
[applications/bt_app.c]
bt__init()
    -> btstack_port_init(NULL)
       先把基础 Host 栈准备好
    -> a2dp_xxx_init()
       选择 Source 或 Sink
    -> a2dp_xxx_register_packet_handler()
       注册 A2DP/AVDTP 事件回调
    -> 如果是 Sink:
       a2dp_sink_register_media_handler()
       注册媒体数据接收回调
    -> a2dp_xxx_create_stream_endpoint()
       创建本地 SEP，声明自己支持什么 codec/capability
    -> a2dp_xxx_create_sdp_record()
       生成 SDP 服务记录
    -> sdp_register_service()
       把 SDP 服务发布出去
    -> btstack_port_start_thread()
       启动线程并上电

[协议栈启动后]
HCI_POWER_ON
    -> GAP 进入可发现/可连接状态
    -> 远端设备发现你并查询 SDP
    -> 建立 AVDTP signaling channel
    -> A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED
    -> A2DP_SUBEVENT_STREAM_ESTABLISHED
    -> A2DP_SUBEVENT_STREAM_STARTED
    -> Sink: media_handler() 开始收到音频
    -> Source: 等 CAN_SEND_NOW 事件后开始发音频
```

这张图里最关键的是两点：

- `btstack_port_start_thread()` 之前，做完 profile 初始化、SEP 创建、SDP 注册。
- `btstack_port_start_thread()` 之后，就进入事件驱动模式，不再是顺序执行思维。

### A2DP Sink 典型流转图

适合“手机给你的设备推音频”。

```text
本地设备作为 Sink
    -> a2dp_sink_init()
    -> a2dp_sink_register_packet_handler()
    -> a2dp_sink_register_media_handler()
    -> a2dp_sink_create_stream_endpoint()
    -> a2dp_sink_create_sdp_record()
    -> sdp_register_service()
    -> btstack_port_start_thread()
    -> 等手机发现你
    -> 手机查询 SDP，确认你支持 A2DP Sink
    -> 手机发起 AVDTP signaling 连接
    -> A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED
    -> A2DP_SUBEVENT_STREAM_ESTABLISHED
    -> A2DP_SUBEVENT_STREAM_STARTED
    -> a2dp_sink_media_handler(local_seid, packet, size)
    -> 你在 media_handler 里做 SBC 处理或送音频链路
```

### A2DP Source 典型流转图

适合“你本地编码后往耳机/音箱发音频”。

```text
本地设备作为 Source
    -> a2dp_source_init()
    -> a2dp_source_register_packet_handler()
    -> a2dp_source_create_stream_endpoint()
    -> a2dp_source_create_sdp_record()
    -> sdp_register_service()
    -> btstack_port_start_thread()
    -> 等 HCI_EVENT_STATE == HCI_STATE_WORKING
    -> a2dp_source_establish_stream(remote_addr, &a2dp_cid)
    -> A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED
    -> A2DP_SUBEVENT_STREAM_ESTABLISHED
    -> a2dp_source_start_stream(a2dp_cid, local_seid)
    -> A2DP_SUBEVENT_STREAM_STARTED
    -> A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW
    -> a2dp_source_stream_send_media_payload_rtp(...) 或 send_media_packet(...)
    -> 继续请求 can send now
    -> 循环发音频
```

### 你可以按这个顺序理解 A2DP

```text
基础栈先启动
    -> profile 先注册
    -> SDP 先发布
    -> SEP 先创建
    -> 上电
    -> 远端发现/连接
    -> AVDTP 协商
    -> stream 建立
    -> stream started
    -> 真正开始收发音频
```

如果你后面要看代码，优先盯住这几个点：

- `a2dp_xxx_init()`
- `a2dp_xxx_register_packet_handler()`
- `a2dp_xxx_create_stream_endpoint()`
- `a2dp_xxx_create_sdp_record()`
- `sdp_register_service()`
- `A2DP_SUBEVENT_*`
- `a2dp_sink_media_handler()` 或 `A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW`

## 最小 A2DP Sink 用法

适用场景：

- 你的设备是“音频接收端”
- 例如手机给你推音频

### 需要的基础配置

在 [core/config/bt_config.h](./core/config/bt_config.h) 里至少保证：

```c
#define BT_CFG_ENABLE_CLASSIC         1
#define BT_CFG_CLASSIC_ENABLE_SDP     1
```

注意：

- `A2DP` 依赖的是 `Classic + SDP`
- 它走 `AVDTP/L2CAP`
- 不是 `RFCOMM`

### 最小初始化顺序

1. `a2dp_sink_init()`
2. `a2dp_sink_register_packet_handler()`
3. `a2dp_sink_register_media_handler()`
4. `a2dp_sink_create_stream_endpoint()`
5. `a2dp_sink_create_sdp_record()`
6. `sdp_register_service()`
7. 启动协议栈

### 最小代码骨架

```c
#include "classic/a2dp_sink.h"
#include "classic/sdp_server.h"

static uint8_t a2dp_sink_sdp_record[200];
static avdtp_stream_endpoint_t * a2dp_sink_sep;

static const uint8_t sbc_capabilities[] = {
    /* 按 A2DP SBC capability 格式填写 */
};

static uint8_t sbc_configuration[] = {
    /* 默认 codec 配置，格式同 A2DP SBC config */
};

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) packet_type;
    (void) channel;
    (void) size;

    /* 在这里处理 A2DP_SUBEVENT_* 事件 */
}

static void a2dp_sink_media_handler(uint8_t local_seid, uint8_t * packet, uint16_t size){
    (void) local_seid;

    /* 这里收到的是 AVDTP media 数据 */
    /* 后续你可以在这里做 SBC 解包/解码或送入音频链路 */
    (void) packet;
    (void) size;
}

static int bt_profiles_init(void){
    a2dp_sink_init();

    a2dp_sink_register_packet_handler(a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(a2dp_sink_media_handler);

    a2dp_sink_sep = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        sbc_capabilities, sizeof(sbc_capabilities),
        sbc_configuration, sizeof(sbc_configuration));
    if (a2dp_sink_sep == NULL){
        return -1;
    }

    a2dp_sink_create_sdp_record(
        a2dp_sink_sdp_record,
        0x10001,
        AVDTP_SINK_FEATURE_MASK_HEADPHONE,
        "A2DP Sink",
        "WSOZ");

    if (sdp_register_service(a2dp_sink_sdp_record) != 0){
        return -1;
    }

    return 0;
}
```

### 被动等待连接还是主动连接

如果你的本地设备是 `Sink`，常见有两种方式：

- 被动模式：
  只注册 `SEP + SDP`，等远端 Source 来连你
- 主动模式：
  你主动连远端 Source，调用 `a2dp_sink_establish_stream(remote_addr, &a2dp_cid)`

大多数“手机向设备推音频”的场景，通常是本地 `Sink` 作为被动端，重点是先把 `SDP + SEP` 注册好。

### 你真正要关心的事件

最常用的是这些：

- `A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED`
- `A2DP_SUBEVENT_STREAM_ESTABLISHED`
- `A2DP_SUBEVENT_STREAM_STARTED`
- `A2DP_SUBEVENT_STREAM_SUSPENDED`
- `A2DP_SUBEVENT_STREAM_RELEASED`
- `A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION`

理解方式：

- `signal channel` 建好，不代表 `media stream` 已经开始
- `stream started` 之后，媒体数据才会通过 `media_handler` 进来

## 最小 A2DP Source 用法

适用场景：

- 你的设备是“音频发送端”
- 例如你本地编码后往耳机/音箱发

### 最小初始化顺序

1. `a2dp_source_init()`
2. `a2dp_source_register_packet_handler()`
3. `a2dp_source_create_stream_endpoint()`
4. `a2dp_source_create_sdp_record()`
5. `sdp_register_service()`
6. 启动协议栈
7. 调 `a2dp_source_establish_stream()`
8. 等 `STREAM_ESTABLISHED`
9. 调 `a2dp_source_start_stream()`
10. 等 `A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW`
11. 调 `a2dp_source_stream_send_media_payload_rtp()` 或 `a2dp_source_stream_send_media_packet()`

### 最小代码骨架

```c
#include "classic/a2dp_source.h"
#include "classic/sdp_server.h"

static uint8_t a2dp_source_sdp_record[200];
static avdtp_stream_endpoint_t * a2dp_source_sep;
static uint16_t a2dp_source_cid;
static uint8_t a2dp_local_seid;

static const uint8_t sbc_capabilities[] = {
    /* 按 A2DP SBC capability 格式填写 */
};

static uint8_t sbc_configuration[] = {
    /* 默认 codec 配置 */
};

static void a2dp_source_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) packet_type;
    (void) channel;
    (void) size;

    /* 这里处理 STREAM_ESTABLISHED / STREAM_STARTED / CAN_SEND_NOW 等事件 */
}

static int bt_profiles_init(void){
    a2dp_source_init();
    a2dp_source_register_packet_handler(a2dp_source_packet_handler);

    a2dp_source_sep = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        sbc_capabilities, sizeof(sbc_capabilities),
        sbc_configuration, sizeof(sbc_configuration));
    if (a2dp_source_sep == NULL){
        return -1;
    }

    a2dp_local_seid = avdtp_stream_endpoint_seid(a2dp_source_sep);

    a2dp_source_create_sdp_record(
        a2dp_source_sdp_record,
        0x10002,
        AVDTP_SOURCE_FEATURE_MASK_PLAYER,
        "A2DP Source",
        "WSOZ");

    if (sdp_register_service(a2dp_source_sdp_record) != 0){
        return -1;
    }

    return 0;
}
```

启动后主动建链示意：

```c
bd_addr_t remote_addr = { /* 远端地址 */ };
uint8_t status = a2dp_source_establish_stream(remote_addr, &a2dp_source_cid);
```

当收到 `A2DP_SUBEVENT_STREAM_ESTABLISHED` 之后：

```c
a2dp_source_start_stream(a2dp_source_cid, a2dp_local_seid);
```

当收到 `A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW` 之后：

```c
a2dp_source_stream_send_media_payload_rtp(
    a2dp_source_cid,
    a2dp_local_seid,
    0,
    timestamp,
    payload,
    payload_len);
```

### Source 侧你最该注意的事

- 你发的是“已经编码好的媒体 payload”
- A2DP 信令栈不负责替你做 SBC/AAC 编码
- `a2dp_max_media_payload_size()` 可以用来估计一包最多能塞多大

## 为什么 SPP / HID / A2DP 看起来是一个套路

因为它们在 Host 层的接法本质相同，都是：

1. `profile init`
2. 注册服务描述
3. 注册事件回调
4. 启动栈
5. 进入事件驱动

### SPP 的套路

最小步骤：

1. `rfcomm_register_service()`
2. `spp_create_sdp_record()`
3. `sdp_register_service()`
4. 处理 RFCOMM 事件

关键接口见：

- [classic/rfcomm.h](./core/classic/inc/classic/rfcomm.h)
- [classic/spp_server.h](./core/classic/inc/classic/spp_server.h)

### Classic HID Device 的套路

最小步骤：

1. `hid_device_init()`
2. `hid_create_sdp_record()`
3. `sdp_register_service()`
4. 注册 packet/report callback

关键接口见：

- [classic/hid_device.h](./core/classic/inc/classic/hid_device.h)

### BLE GATT Service 的套路

最小步骤：

1. 打开 `BT_CFG_ENABLE_BLE`
2. 准备 `ATT DB`
3. `bt_host_ble_init_att_server()`
4. 调具体 `gatt-service` 的 `xxx_init()`
5. 广播

注意：

- BLE 不走 `SDP`
- BLE 走 `ATT/GATT`

## 当前工程里最推荐的用法

如果你接下来要开始做具体 profile，建议你按下面这个节奏：

1. 先在 [applications/bt_app.c](../applications/bt_app.c) 增加 `bt_profiles_init()`
2. 先接通一个最小 profile
3. 确认事件链路是通的
4. 再接 codec / report / media data

最合适的第一个 profile 通常有两种：

- 如果你要做音频：
  先做 `A2DP Sink` 或 `A2DP Source`
- 如果你要验证“Classic 业务套路”：
  先做 `SPP`

原因很简单：

- `SPP` 最容易验证 SDP/RFCOMM/事件流
- `A2DP` 最能代表你后面要走的音频主线

## 最后给你一个判断标准

以后你看到一个新 profile，只要先判断这 4 件事，就知道怎么接：

1. 它是 `Classic` 还是 `BLE`
2. 它需要 `SDP`、`RFCOMM`、`ATT DB`、还是 `SEP`
3. 它的入口是 `xxx_init()` 还是 `xxx_register_service()`
4. 它的数据面是“事件回调”还是“媒体/报告/通知回调”

如果这 4 个点你能迅速说清楚，这个 profile 基本就能接起来了。

