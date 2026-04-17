# BTstack理解及使用

> 该笔记主要写一下我自己使用和移植BTstack的自己的理解和见解。
>
> ​																																		————by    wsoz



## BTstack介绍

### BTstack是什么

`BTstack` 是 BlueKitchen 提供的一套用 `C` 实现的蓝牙协议栈。严格来说，它更准确的官方写法是 `BTstack`。

它的核心定位不是“蓝牙射频芯片固件”，而是运行在主控 `MCU/MPU` 上的 **Bluetooth Host 协议栈**。也就是说，`BTstack` 主要负责：

- 设备发现、连接建立与断开
- 上层协议处理
- Profile / Service 管理
- 数据收发流程控制
- 与应用层之间的事件交互

而真正负责底层射频收发、基带调度、链路层时序控制的，通常是蓝牙 `Controller`。因此从系统结构上看，`BTstack` 一般工作在 `Host + Controller` 架构中的 `Host` 一侧。



### BTstack支持什么

`BTstack` 是一个 **双模（Dual Mode）蓝牙协议栈**，也就是它既支持：

- **经典蓝牙（BR/EDR）**
- **低功耗蓝牙（BLE / LE）**

并且它可以根据实际项目裁剪成：

- 只支持经典蓝牙
- 只支持 BLE
- 同时支持经典蓝牙 + BLE

从协议支持角度看，`BTstack` 的覆盖范围是比较完整的。

所以从定位上讲，`BTstack` 并不是一个“只适合 BLE”的轻量小栈，它本质上是一套比较完整、可裁剪的蓝牙 `Host` 方案。



### BTstack的核心特点

结合官方文档和实际工程视角来看，`BTstack` 有几个很鲜明的特点。

#### 1. 面向资源受限设备

`BTstack` 一开始就是面向嵌入式设备设计的，因此它比较强调：

- 可裁剪
- 内存占用可控
- 对小型系统友好

对于资源受限的嵌入式场景，这一点很有意义。很多时候我们并不需要一套特别重的通用桌面蓝牙栈，而是更需要一套能按需求裁剪、能落地到具体硬件上的协议栈。

#### 2. 单线程 Run Loop 模型

`BTstack` 的一个很重要的设计思路是 **单线程 + 事件驱动**。

它内部并不强依赖多线程，也不要求一定跑在 `RTOS` 上。很多场景下，它可以通过一个统一的 `run loop` 来处理：

- 数据源事件
- 定时器事件
- HCI 收发事件
- 上层协议事件

这意味着 `BTstack` 的应用写法更像是一个 **状态机驱动模型**，而不是传统那种“调用一个阻塞式 API 然后等结果回来”的写法。

#### 3. 非阻塞、异步事件回调

`BTstack` 的处理思路基本是：

- 发送命令时先返回
- 真正的处理结果通过后续事件上报
- 应用通过注册的 `packet handler` 或回调函数处理各种状态变化

所以后面在实际使用时，我们必须逐步建立一个观念：

> 用 `BTstack` 写应用，核心不是“顺序调用 API”，而是“围绕事件和状态机组织逻辑”。

这也是很多人第一次接触它时最容易不适应的地方。

#### 4. 配置比较明确，静态资源边界清晰

`BTstack` 在配置上比较强调编译期确定能力边界，比如：

- 最大连接数
- 最大通道数
- 支持哪些协议与 Profile
- 缓冲区和内存资源如何分配

这种设计对嵌入式开发是友好的，因为它让资源消耗更可预测，也更方便做裁剪和移植。

#### 5. Host 与 Controller 边界清晰

很多厂商 SDK 会把蓝牙协议栈封装得比较深，开发者只看到一些上层接口；而 `BTstack` 的一个特点就是，它对 `Host`、`HCI`、`Controller` 之间的关系体现得比较清楚。

这对于学习蓝牙协议栈非常有价值，因为你在使用它的时候，会明显感受到：

- 哪些是 Host 该做的事
- 哪些是 Controller 该做的事
- 哪些问题属于 HCI 传输适配问题
- 哪些问题属于上层 Profile 逻辑问题

换句话说，`BTstack` 不只是“拿来用”，它也很适合拿来帮助我们真正理解蓝牙协议栈的分层。

#### 6. 跨平台能力比较强

根据官方资料，`BTstack` 不仅能跑在资源受限的嵌入式平台上，也支持 `RTOS`、桌面系统以及部分嵌入式 Linux 场景。

这意味着在很多项目里，可以先在桌面环境配合标准蓝牙 `USB Dongle` 做功能验证和抓包分析，等逻辑稳定后，再迁移到具体的嵌入式硬件上。

这个特点对学习和调试非常友好，因为它能明显降低前期验证成本。



### 小结

`BTstack` 本质上是一套运行在主控侧的 **双模蓝牙 Host 协议栈**。它的关键特点不是“接口多”，而是：

- 分层清晰
- 可裁剪
- 事件驱动
- 适合嵌入式
- 既能做产品，也很适合做协议栈学习

后面真正进入使用阶段时，我们需要重点关注的就不再只是“它支不支持某个 Profile”，而是：

- 它的初始化链路是什么
- `HCI` 如何对接到底层控制器
- 上层业务状态机如何挂到 `BTstack` 上
- 经典蓝牙与 BLE 在栈内分别如何组织

把这些问题逐步拆开，`BTstack` 就会从“看起来很杂的一堆蓝牙代码”，变成一套非常清晰的工程系统。

------



## BTstack移植对接

BTstack协议栈移植使用时，我们主要需要先对接几个接口: 1.`chipset_driver`  2.`run loop/TLV`  3.`HCI transport`

### chipset_driver对接

`chipset_driver`目的主要是完成对于controller芯片的**特定厂商初始化**：

```c
typedef struct {
    /**
     * chipset driver name
     */
    const char * name;

    /**
     * init driver
     * allows to reset init script index
     * @param transport_config
     */
    void (*init)(const void * transport_config);

    /**
     * support custom init sequences after RESET command
     * @param  hci_cmd_buffer to store generated command
     * @return result see btstack_chipset_result_t
     */
    btstack_chipset_result_t (*next_command)(uint8_t * hci_cmd_buffer); 

    /**
     * provide UART Baud Rate change command.
     * @param baudrate
     * @param hci_cmd_buffer to store generated command
     */
    void (*set_baudrate_command)(uint32_t baudrate, uint8_t *hci_cmd_buffer); 
    
    /** provide Set BD Addr command
     * @param baudrate
     * @param hci_cmd_buffer to store generated command
     */
    void (*set_bd_addr_command)(bd_addr_t addr, uint8_t *hci_cmd_buffer); 

} btstack_chipset_t;
```

具体如何对接可以参考官方的 `BTstack` 提供的像 `CSR` 等示例。我自己目前使用的芯片是ESP32E,**不需要特定的厂商初始化**如下:

```c
#define BTSTACK_FILE__ "btstack_chipset_esp32.c"

#include "btstack_chipset_esp32.h"

#include "btstack_util.h"

static void btstack_chipset_esp32_init(const void * transport_config){
    UNUSED(transport_config);
}

static btstack_chipset_result_t btstack_chipset_esp32_next_command(uint8_t * hci_cmd_buffer){
    UNUSED(hci_cmd_buffer);
    return BTSTACK_CHIPSET_NO_INIT_SCRIPT;
}

static const btstack_chipset_t btstack_chipset_esp32 = {    //controller芯片适配板级初始化
    "ESP32-WROOM-32E",      //controller芯片名称
    &btstack_chipset_esp32_init,    //初始化函数
    &btstack_chipset_esp32_next_command,
    NULL,
    NULL,
};  
/*即是板子的厂商的初始化命令,vendor specific initialization commands
如果芯片需要上电初始化序列，则在 next_command 里实现并返回 BTSTACK_CHIPSET_VALID_COMMAND，
协议栈会在发送完 RESET 命令后调用 next_command 获取并发送这些初始化命令。*/

const btstack_chipset_t * btstack_chipset_esp32_instance(void){
    return &btstack_chipset_esp32;
}
```

------



### run loop/TLV对接

`run loop/TLV`对接的目的就是提供一个协议栈的一个支持：1.`run loop` 主要就是对接像定时器，给协议栈一个基础的调度能力  2.`TLV` 提供一个持久化存储接口

#### run loop对接

`run loop`对接就是需要去提供可以支撑我们协议栈跑起来的一个调度

```c
typedef struct btstack_run_loop {
	void (*init)(void);
	void (*add_data_source)(btstack_data_source_t * data_source);
	bool (*remove_data_source)(btstack_data_source_t * data_source);
	void (*enable_data_source_callbacks)(btstack_data_source_t * data_source, uint16_t callbacks);
	void (*disable_data_source_callbacks)(btstack_data_source_t * data_source, uint16_t callbacks);
	void (*set_timer)(btstack_timer_source_t * timer, uint32_t timeout_in_ms);
	void (*add_timer)(btstack_timer_source_t *timer);
	bool  (*remove_timer)(btstack_timer_source_t *timer);
	void (*execute)(void);
	void (*dump_timer)(void);
	uint32_t (*get_time_ms)(void);
	void (*poll_data_sources_from_irq)(void);
	void (*execute_on_main_thread)(btstack_context_callback_registration_t * callback_registration);
	void (*trigger_exit)(void);
} btstack_run_loop_t;
```

* `init` 主要是完成内部状态的初始化
* `add_data_source` 主要是向 `run loop` 注册一个数据源，后续协议栈才能轮询或监听它的事件
* `remove_data_source` 主要是从 `run loop` 中移除一个数据源，避免后续继续调度它
* `enable_data_source_callbacks` 主要是给某个数据源使能指定的回调类型，例如 `POLL/READ/WRITE/ERROR`
* `disable_data_source_callbacks` 主要是关闭某个数据源对应的回调类型
* `set_timer` 主要是设置一个定时器的超时时间，本质上就是给这个定时器设置到期时刻
* `add_timer` 主要是把一个已经设置好的定时器挂到 `run loop` 的定时器管理中
* `remove_timer` 主要是把一个定时器从 `run loop` 中移除，避免它后续继续超时触发
* `execute` 主要是启动并执行整个 `run loop`，让协议栈进入持续调度状态
* `dump_timer` 主要是用于调试，打印当前已经注册的定时器信息
* `get_time_ms` 主要是给协议栈提供当前系统时间，单位一般是毫秒
* `poll_data_sources_from_irq` 主要是在中断上下文中触发一次数据源轮询请求，让主循环后续去处理这些数据源
* `execute_on_main_thread` 主要是把一个回调投递到 `BTstack` 主线程中执行，常用于跨线程切换
* `trigger_exit` 主要是请求退出当前的 `run loop`

其中对于嵌入式移植来说，通常最核心的是下面几组：

- 数据源相关：`add_data_source/remove_data_source/enable_data_source_callbacks/disable_data_source_callbacks`
- 定时器相关：`set_timer/add_timer/remove_timer/get_time_ms`
- 主循环相关：`execute`
- 特殊场景相关：`poll_data_sources_from_irq/execute_on_main_thread/trigger_exit`

其中最主要的只需要去对接**数据源相关**以及**定时器相关**还有**主循环相关**的即可。

下面讲一下我的理解：

首先先讲一下这个**数据源相关**的回调函数就是负责主要给上层的API提供一个接口，便于上层协议进行注册数据源接口，然后会把这个上层协议的数据源接口以及回调函数挂载到 `run loop` 的链表中去，也可以通过控制是不是开启回调功能。下面为大致的一个数据流：

```c
btstack_data_source_t ds;	//注册数据源

btstack_run_loop_set_data_source_handler(&ds, my_process);	//设置回调函数
btstack_run_loop_add_data_source(&ds);	//添加消息源
btstack_run_loop_enable_data_source_callbacks(&ds, DATA_SOURCE_CALLBACK_POLL);	//开启回调函数
```

**定时器相关**的则是给像 `HCI_Reset` 等命令提供一个超时的机制，来实现重发保护协议栈。同样也是设置一个过期时间的定时器，然后挂载到 `run loop`链表中去，在 `execute` 中会进行遍历查看是否超时。最主要的其实就是提供一个获取时间戳的API给 `get_time_ms`。

```c
btstack_timer_source_t ts;   // 注册一个定时器对象

btstack_run_loop_set_timer_handler(&ts, my_timer_process); // 设置定时器回调
btstack_run_loop_set_timer(&ts, 1000);                    // 设定 1000 ms 后到期
btstack_run_loop_add_timer(&ts);                         // 挂入 run loop
```

**主循环相关**则主要就是可以直接看成在 `while` 循坏中轮询其中的执行主要由三步

```c
 btstack_run_loop_base_execute_callbacks();	
 btstack_run_loop_base_poll_data_sources();
 btstack_run_loop_base_process_timers(btstack_run_loop_embedded_get_time_ms());
```

* `execute_callbacks`：一次性待办队列，执行后出队。eg：UART 收到数据→单词回调出队→H4解析包
* `poll_data_sources`：常驻对象轮询表，对象不出表
* `process_timers`：到期触发器，触发后若不重挂就结束

**注意**：实际上大多数走第一个就差不多本质上就是数据的通知触发回调进行解析。下面为我们在RT-Thread中的示例：

```c
#define BTSTACK_FILE__ "btstack_run_loop_embedded.c"

#include "btstack_run_loop_embedded.h"

#include "btstack_debug.h"

#include <rtthread.h>

static struct rt_semaphore btstack_run_loop_sem;
static rt_bool_t btstack_run_loop_sem_inited = RT_FALSE;
static volatile rt_bool_t btstack_run_loop_exit_requested = RT_FALSE;

static void btstack_run_loop_embedded_notify(void){
    // 定时器、回调队列或数据源状态变化后，都通过信号量把 run loop 唤醒。
    if (btstack_run_loop_sem_inited){
        (void) rt_sem_release(&btstack_run_loop_sem);
    }
}

static void btstack_run_loop_embedded_init(void){
    // 先初始化 BTstack 公共链表和定时器基类，再准备 RT-Thread 的唤醒信号量。
    btstack_run_loop_base_init();   //完成底层的初始化

    //初始化内部状态
    if (!btstack_run_loop_sem_inited){
        rt_sem_init(&btstack_run_loop_sem, "btloop", 0, RT_IPC_FLAG_FIFO);
        btstack_run_loop_sem_inited = RT_TRUE;
    }

    btstack_run_loop_exit_requested = RT_FALSE;
}

static void btstack_run_loop_embedded_add_data_source(btstack_data_source_t * data_source){
    btstack_run_loop_base_add_data_source(data_source);
    btstack_run_loop_embedded_notify();
}

static bool btstack_run_loop_embedded_remove_data_source(btstack_data_source_t * data_source){
    bool removed = btstack_run_loop_base_remove_data_source(data_source);
    if (removed){
        btstack_run_loop_embedded_notify();
    }
    return removed;
}

static void btstack_run_loop_embedded_enable_data_source_callbacks(btstack_data_source_t * data_source, uint16_t callbacks){
    btstack_run_loop_base_enable_data_source_callbacks(data_source, callbacks);
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_disable_data_source_callbacks(btstack_data_source_t * data_source, uint16_t callbacks){
    btstack_run_loop_base_disable_data_source_callbacks(data_source, callbacks);
}

static uint32_t btstack_run_loop_embedded_get_time_ms(void){
    return (uint32_t) rt_tick_get_millisecond();
}

static void btstack_run_loop_embedded_set_timer(btstack_timer_source_t * timer, uint32_t timeout_in_ms){
    timer->timeout = btstack_run_loop_embedded_get_time_ms() + timeout_in_ms;
}

static void btstack_run_loop_embedded_add_timer(btstack_timer_source_t * timer){
    btstack_run_loop_base_add_timer(timer);
    btstack_run_loop_embedded_notify();
}

static bool btstack_run_loop_embedded_remove_timer(btstack_timer_source_t * timer){
    bool removed = btstack_run_loop_base_remove_timer(timer);
    if (removed){
        btstack_run_loop_embedded_notify();
    }
    return removed;
}

static void btstack_run_loop_embedded_poll_data_sources_from_irq(void){
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_execute_on_main_thread(btstack_context_callback_registration_t * callback_registration){
    btstack_run_loop_base_add_callback(callback_registration);
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_trigger_exit(void){
    btstack_run_loop_exit_requested = RT_TRUE;
    btstack_run_loop_embedded_notify();
}
//循环执行函数
static void btstack_run_loop_embedded_execute(void){
    while (!btstack_run_loop_exit_requested){
        int32_t timeout_ms;
        rt_tick_t timeout_tick;

        // 顺序保持和 BTstack 参考实现一致：先回调，再轮询数据源，最后处理超时定时器。
        btstack_run_loop_base_execute_callbacks();
        btstack_run_loop_base_poll_data_sources();
        btstack_run_loop_base_process_timers(btstack_run_loop_embedded_get_time_ms());

        if (btstack_run_loop_exit_requested){
            break;
        }

        timeout_ms = btstack_run_loop_base_get_time_until_timeout(btstack_run_loop_embedded_get_time_ms());
        if (timeout_ms == 0){
            // 已经有定时器到期，立刻进入下一轮，不需要睡眠。
            continue;
        }

        if (timeout_ms < 0){
            // 当前没有等待中的定时器，直接睡眠，直到其他上下文把 run loop 唤醒。
            (void) rt_sem_take(&btstack_run_loop_sem, RT_WAITING_FOREVER);
            continue;
        }

        // 最多睡到下一个定时器超时；如果中途有外部事件，也会提前被唤醒。
        timeout_tick = rt_tick_from_millisecond(timeout_ms);
        if (timeout_tick == 0){
            timeout_tick = 1;
        }
        (void) rt_sem_take(&btstack_run_loop_sem, timeout_tick);
    }
}

const btstack_run_loop_t btstack_run_loop_embedded = {
    &btstack_run_loop_embedded_init,
    &btstack_run_loop_embedded_add_data_source,
    &btstack_run_loop_embedded_remove_data_source,
    &btstack_run_loop_embedded_enable_data_source_callbacks,
    &btstack_run_loop_embedded_disable_data_source_callbacks,
    &btstack_run_loop_embedded_set_timer,
    &btstack_run_loop_embedded_add_timer,
    &btstack_run_loop_embedded_remove_timer,
    &btstack_run_loop_embedded_execute,
    &btstack_run_loop_base_dump_timer,
    &btstack_run_loop_embedded_get_time_ms,
    &btstack_run_loop_embedded_poll_data_sources_from_irq,
    &btstack_run_loop_embedded_execute_on_main_thread,
    &btstack_run_loop_embedded_trigger_exit,
};

const btstack_run_loop_t * btstack_run_loop_embedded_get_instance(void){
    return &btstack_run_loop_embedded;
}
```

------



#### TLV 对接

`TLV` 主要是来存储数据的，需要配合 `Flash` `Eeprom`等来实现。不是刚需，自己根据情况选择去对接即可。

  > TLV 主要在需要持久化存储时对接。典型场景包括经典蓝牙 Link Key 保存、BLE Bonding 信息保存、GATT Server 的 CCC 配置保
  > 存，以及应用层自定义参数保存。如果只是基础联调，如扫描、广播、连接、临时收发，而不要求掉电保存配对和配置，则可以先不
  > 对接 TLV。

------



### HCI transport对接

`HCI transport` 对接就是完成HCI传输工作，最常用的还是H4-UART传输。下面就是传输需要对接的结构体，已经把H5和低功耗的部分删掉了：

```c
typedef struct {
    /**
     * init transport
     * @param uart_config
     */
    int (*init)(const btstack_uart_config_t * uart_config);
    /**
     * open transport connection
     */
    int (*open)(void);
    /**
     * close transport connection
     */
    int (*close)(void);
    /**
     * set callback for block received. NULL disables callback
     */
    void (*set_block_received)(void (*block_handler)(void));
    /**
     * set callback for sent. NULL disables callback
     */
    void (*set_block_sent)(void (*block_handler)(void));
    /**
     * set baudrate
     */
    int (*set_baudrate)(uint32_t baudrate);
    /**
     * set parity
     */
    int  (*set_parity)(int parity);
    /**
     * set flowcontrol
     */
    int  (*set_flowcontrol)(int flowcontrol);

    /**
     * receive block
     */
    void (*receive_block)(uint8_t *buffer, uint16_t len);
    /**
     * send block
     */
    void (*send_block)(const uint8_t *buffer, uint16_t length);

}btstack_uart_t;
```

* `init` 主要是完成底层 `UART/DMA/GPIO` 等硬件资源的初始化，并加载串口配置参数
* `open` 主要是打开当前的 `HCI transport` 通道，让主机和 controller 之间可以正式通信
* `close` 主要是关闭当前的 `HCI transport` 通道
* `set_block_received` 主要是注册接收完成后的回调函数，当一块数据接收完成后通知上层继续处理
* `set_block_sent` 主要是注册发送完成后的回调函数，当一块数据发送完成后通知上层继续处理
* `set_baudrate` 主要是动态修改当前串口波特率，常用于初始化后切换到更高波特率
* `set_parity` 主要是设置串口校验位，一般标准 `H4-UART` 场景中较少改动
* `set_flowcontrol` 主要是设置串口硬件流控，例如 `RTS/CTS`
* `receive_block` 主要是启动一次指定长度的数据接收，把收到的数据放到给定的 buffer 中
* `send_block` 主要是启动一次指定长度的数据发送，把 buffer 中的数据发给 controller

其中对于我们常见的 `H4-UART` 对接来说，最核心的是下面几组：

- 基础控制相关：`init/open/close`
- 数据收发相关：`receive_block/send_block`
- 收发完成通知相关：`set_block_received/set_block_sent`
- 串口配置相关：`set_baudrate/set_flowcontrol`

下面讲一下我的理解：

**基础控制部分**就是负责对于串口的初始化进行配置，比如就对于自己状态机或者内部串口状态进行控制。

**数据收发部分**是接收和发送数据，主要就是和contoller进行交互，对接串口发送和接收函数。

* 串口发送函数主要就是直接对接发送函数即可。
* 接收函数需要注意一下，因为数据是字节流信息没有边界因此，因此就需要我们的HCI层先对数据进行一次包解析用来确定包的准确边界，然后将数据传递给上层协议进行解析。

**收发完成通知相关部分**这个主要就是配合我们的HCI进行拆包的，用于每次事件进行通知

* `set_block_received` 收够一块之后，回调 handler
* `set_block_sent`  发送完成后，回调 handler

**串口配置相关部分**主要是为了让我们可以动态的调整我们的串口，比如再有一些厂商初始化过程中我们可以同时提高波特率来实现高速拆传输。

我个人感觉其实最主要的还有一点搞清楚我们的数据传输的一个流程：

```
[1] H4 先发起一次“我要读多少字节”
[hci_transport_h4.c]
hci_transport_h4_open()
  -> hci_transport_h4_trigger_next_read()
  -> btstack_uart->receive_block(&hci_packet[read_pos], bytes_to_read)

[2] UART 端口层先把这个请求记下来
[btstack_uart_block_embedded.c]
btstack_uart_rtthread_receive_block()
  -> 记录 rx_buffer = &hci_packet[read_pos]
  -> 记录 rx_len    = bytes_to_read

[3] Controller 的原始字节真正到达
Controller raw data
  -> [drv_usart.c] DMA搬到串口驱动缓冲区 rx_fifo->buffer
  -> [btstack_uart_block_embedded.c] btstack_uart_rtthread_rx_indicate()
  -> btstack_uart_rtthread_rx_thread()
  -> rt_device_read(..., rx_tmp_storage, ...)
  -> rt_ringbuffer_put(..., rx_ringbuffer, ...)

[4] UART层发现“已经凑够 H4 这次要的长度了”
[btstack_uart_block_embedded.c]
btstack_uart_rtthread_try_deliver_rx_locked()
  -> rt_ringbuffer_get(..., rx_buffer, rx_len)
  -> btstack_run_loop_execute_on_main_thread(...)
  -> btstack_uart_rtthread_rx_done()
  -> block_received_handler()

[5] 这个 block_received_handler 就是 H4 注册进来的
[hci_transport_h4.c]
block_received_handler = hci_transport_h4_block_read()

所以这里实际变成：
btstack_uart_rtthread_rx_done()
  -> hci_transport_h4_block_read()

[6] H4 开始拆包
[hci_transport_h4.c]
hci_transport_h4_block_read()
  -> 先看 packet type
  -> 再读 header
  -> 再读 payload
  -> 如果还没完整
       -> hci_transport_h4_trigger_next_read()
       -> 再次 receive_block(...)
  -> 如果完整
       -> hci_transport_h4_packet_complete()
       -> hci_transport_h4_packet_handler(packet_type, packet, size)

[7] 进入 HCI Core
[hci.c]
packet_handler(...)
  -> Event -> event_handler(...)
  -> ACL   -> acl_handler(...)

[8] ACL 再往上走
[hci.c]
acl_handler(...)
  -> hci_emit_acl_packet(...)
  -> [l2cap.c] l2cap_acl_handler(...)
  -> 上层协议(AVDTP / SDP / RFCOMM ...)
```

------



## BTstack初始化及使用

我个人理解 `BTstack` 的初始化大致可以分成三层：

1. **底层 port 初始化**
2. **BTstack 核心协议初始化**
3. **上层协议/Profile 使用**

其中前面的 `chipset_driver`、`run loop/TLV`、`HCI transport` 对接，本质上就是在为第一层做准备。只有这些底层接口先接好，后面的协议栈初始化才有意义。



### 底层 port 初始化

`port` 初始化主要就是把平台侧和硬件侧的运行环境准备好，让 `BTstack` 后续可以真正跑起来。

对于我当前的工程来说，这一层主要包括：

- 板级硬件初始化，例如 `UART`、`DMA`、`GPIO`、中断等
- `run loop` 初始化
- 可选的 `TLV` 持久化后端初始化
- 如果需要的话，还包括 `controller` 的上电、复位、唤醒引脚控制

例如我们前面已经对接好的 `run loop` 和 `TLV`，在初始化阶段一般就会先注册给协议栈：

```c
// 1. 注册 run loop
btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

// 2. 配置 TLV 后端
// 如果当前不需要持久化存储，也可以先使用 none 版本
btstack_tlv_set_instance(btstack_tlv_none_init_instance(), NULL);
```

这里要注意一下：

- `btstack_run_loop_init(...)` 是必须的，它是把底层调度器注册给 `BTstack`
- `btstack_tlv_set_instance(...)` 是可选的，只有需要持久化配对信息、Bonding 信息、CCC 配置时才真正重要

所以我个人觉得，这一层的重点不是“协议”，而是**先把 `BTstack` 依赖的平台能力准备好**。

------



### BTstack核心协议初始化

当前面的 `port` 能力准备好之后，下一步才真正进入 `BTstack` 的核心协议初始化阶段。

这一层我个人理解主要就是把协议栈从下往上逐步拉起来，典型包括：

- `HCI`
- `L2CAP`
- 经典蓝牙常用协议：`SDP`、`RFCOMM`
- BLE 常用协议：`SM`、`ATT`、`GATT`

其中 `HCI` 是整个协议栈的入口，`L2CAP` 又是大多数上层协议的基础，所以初始化顺序通常也是围绕这两层往上展开。

**内存池初始化**

在初始化具体协议之前，一般先调用：

```c
btstack_memory_init();
```

这个函数主要是完成 `BTstack` 内部静态内存池的初始化。因为很多协议模块内部都会依赖这些内存池，所以通常要先做这一步。

**HCI初始化**

`HCI` 初始化是核心协议初始化里最底层也最关键的一步，它会把前面已经对接好的 `HCI transport`、`chipset_driver`、`remote_device_db/TLV` 等能力真正挂到协议栈中。

典型调用大致如下：

```c
hci_init(hci_transport_h4_instance(btstack_uart_block_embedded_instance()),
         &transport_config);
```

这里我的理解是：

- `HCI transport` 解决的是底层数据怎么通过 `UART(H4)` 跟 controller 交互
- `hci_init(...)` 解决的是把这条底层传输链路真正接入 `BTstack` 的 `HCI core`

也就是说，前面写的 `HCI transport对接` 是在准备“路”，而这里的 `hci_init(...)` 才是正式把这条路接进协议栈。

如果再直观一点看，`HCI` 这一层初始化完成之后，后面 controller 上报的：

- `HCI Event`
- `ACL Data`
- 以及主机下发的 `HCI Command`

才会真正进入 `BTstack` 内部的协议处理流程。

**L2CAP初始化**

在 `HCI` 初始化完成之后，下一步通常就是初始化 `L2CAP`：

```c
l2cap_init();
```

`L2CAP` 可以理解为 Host 层很多上层协议共同依赖的基础承载层。

比如：

- 经典蓝牙里的 `SDP`
- `RFCOMM`
- BLE 里的 `ATT`
- `SM`

很多都要建立在 `L2CAP` 之上，所以 `L2CAP` 一般也是要优先初始化的。

**经典蓝牙协议初始化**

如果当前项目是经典蓝牙方向，比如做 `SPP`，那么在 `L2CAP` 之后，通常就会继续初始化经典蓝牙相关协议：

```c
// 经典蓝牙常见初始化链路
l2cap_init();
rfcomm_init();
sdp_init();
```

如果是做 `SPP` 这类服务端场景，后面还会继续：

```c
rfcomm_register_service(packet_handler, RFCOMM_SERVER_CHANNEL, 0xffff);

memset(spp_service_buffer, 0, sizeof(spp_service_buffer));
spp_create_sdp_record(spp_service_buffer, 0x10001, RFCOMM_SERVER_CHANNEL, "SPP");
sdp_register_service(spp_service_buffer);
```

所以对于经典蓝牙来说，我个人会把核心协议初始化理解成下面这个顺序：

```text
HCI -> L2CAP -> RFCOMM/SDP -> 具体Profile或业务
```

其中：

- `SDP` 负责服务发现
- `RFCOMM` 负责串口仿真这类数据通道
- 上层 `SPP/HFP/A2DP/HID` 等再建立在这些基础协议之上

**BLE协议初始化**

如果当前项目是 `BLE` 方向，那么在 `L2CAP` 之后，通常走的是另一条初始化链路：

```c
l2cap_init();
le_device_db_init();
sm_init();
att_server_init(profile_data, att_read_callback, att_write_callback);
```

这里可以简单理解成：

- `le_device_db_init()`：准备 BLE 设备数据库，用于保存绑定设备等信息
- `sm_init()`：初始化安全管理协议
- `att_server_init()`：初始化 ATT/GATT 服务端数据库

所以对于 `BLE` 来说，核心协议初始化大致可以理解成：

```text
HCI -> L2CAP -> SM/ATT/GATT -> GAP广播/连接 -> 具体业务
```

这里虽然很多资料会把 `GAP`、`GATT` 一起提，但从工程视角看，真正初始化时我们通常更关注：

- 安全是否初始化好
- ATT/GATT 数据库是否初始化好
- 广播参数和广播数据是否配置好

**注册事件回调并上电启动**

当前面的核心协议初始化完成后，通常还需要注册事件回调，然后再真正让 controller 上电启动：

```c
hci_event_callback_registration.callback = &packet_handler;
hci_add_event_handler(&hci_event_callback_registration);

hci_power_control(HCI_POWER_ON);
```

这里要注意一点：

- `hci_init(...)` 只是完成 `HCI` 模块的初始化
- `hci_power_control(HCI_POWER_ON)` 才是真正让底层 controller 进入工作状态

也就是说，初始化和上电启动不是同一件事。

------



### 上层协议使用

当底层 `port` 和核心协议都初始化完成之后，后面才真正进入上层协议/Profile 的使用阶段。

比如：

- 经典蓝牙 `SPP`
- `HID`
- `A2DP`
- BLE 广播、连接、通知

**协议初始化分两种回调：栈内回调和应用回调。**

栈内回调：一般由协议自己的 init() 自动注册，你不用管。
应用回调：是你业务要接收事件/数据，所以必须自己注册。

对于我们的**核心协议初始化**则不需要进行自己手动注册回调，它是属于**栈内回调**，默认底层自动注册。

我们上层应用层的协议就需要我们自己进行初始化然后绑定回调函数。这里我个人觉得最容易卡住的点就是：

> 到了上层协议/Profile 这一层，到底哪些事情是 `init()` 内部自动做掉的，哪些事情必须应用自己做？

我这里给出一个我目前更清晰的通用理解：

#### 上层协议使用的通用规则

如果是从零开始自己搭 `BTstack`，而不是直接用我当前工程已经封装好的 `btstack_port_init()`，那么我个人理解上层协议/Profile 的使用一般可以拆成下面几类动作：

1. **调用 Profile 自己的初始化函数**
2. **注册应用层回调函数**
3. **注册服务/端点/通道**
4. **如果是 Classic 服务，还要注册 SDP Record**
5. **最后在事件回调里完成真正的数据收发**

也就是说，到上层协议这一步时，不能再只看“调一个 `xxx_init()` 就结束了”，而是要搞清楚这个 `init()` 内部到底做了哪些自动挂接，以及还剩哪些工作需要应用层自己补。

这里我把“自动做的”和“自己做的”区分一下：

**第一类：Profile 内部自动做的事情**

很多 Profile 的 `init()` 内部，已经会帮我们把它自己挂到底层协议上。比如：

- 向 `L2CAP` 注册服务 `PSM`
- 准备内部状态机
- 初始化本协议自己的全局上下文
- 建立一些默认参数

这类工作本质上还是**栈内回调和协议内部挂接**，一般不需要应用自己重复注册。

**第二类：应用必须自己做的事情**

这部分通常包括：

- 注册 `packet_handler`
- 注册媒体回调 / 数据回调 / `read/write callback`
- 创建 `SDP record`
- 注册服务记录
- 创建 `stream endpoint`
- 在连接建立后发数据、收数据、响应事件

这部分本质上就是：

> 协议栈已经帮你把“协议自己”挂到底层了，但它不知道你的业务要怎么处理数据，所以这些业务回调必须你自己注册。

所以我现在觉得可以用一句话总结：

```text
核心协议 init：负责把协议模块挂到下一层协议，通常自动完成。
上层 Profile 使用：除了 init 之外，应用层通常还必须自己注册业务回调和业务对象。
```

------

#### 以 HID Device 为例子理解上层协议使用

我感觉 `HID Device` 很适合拿来理解这一层，因为它同时包含了：

- Profile 初始化
- L2CAP 服务注册
- 应用回调注册
- SDP 服务注册
- 事件驱动发送 Report

下面就按 `HID Device` 这条典型链路来看。

##### 1. 先调用 HID 自己的初始化函数

```c
hid_device_init(boot_protocol_mode_supported,
                hid_descriptor_len,
                hid_descriptor);
```

这个函数是 `HID Device` Profile 自己的初始化入口。  
最关键的一点是，它内部已经帮我们把 `HID` 对应的两个 `L2CAP` 服务注册好了：

```c
l2cap_register_service(packet_handler, PSM_HID_INTERRUPT, 100, gap_get_security_level());
l2cap_register_service(packet_handler, PSM_HID_CONTROL,   100, gap_get_security_level());
```

也就是说，`hid_device_init()` 并不只是保存一下描述符，它还做了：

```text
HID Device -> L2CAP(HID_CONTROL / HID_INTERRUPT)
```

这一层的协议挂接。

所以这里就能看出一个很重要的规律：

> 很多上层协议/Profile 的 `init()` 内部，本身就会把自己注册到底层协议里，并不是完全空函数。

当前源码位置如下：

- `hid_device_init(...)`：`core/classic/src/hid_device.c`
- 其中内部调用了 `l2cap_register_service(...)`

------

##### 2. 但是应用层回调仍然需要自己注册

虽然 `hid_device_init()` 已经把 `HID Device` 挂到 `L2CAP` 了，但应用层还是必须自己注册：

```c
hid_device_register_packet_handler(packet_handler);
```

这个回调是用来让应用接收诸如下面这些事件的：

- `HID_SUBEVENT_CONNECTION_OPENED`
- `HID_SUBEVENT_CONNECTION_CLOSED`
- `HID_SUBEVENT_CAN_SEND_NOW`
- `HID_SUBEVENT_SUSPEND`
- `HID_SUBEVENT_EXIT_SUSPEND`

也就是说：

```text
HID 协议自身怎么挂到 L2CAP
  -> init() 自动做

应用收到 HID 事件后怎么处理
  -> packet_handler 必须自己注册
```

这一点和前面的 `L2CAP`、`RFCOMM`、`A2DP` 是一致的。

------

##### 3. Classic 服务通常还要自己注册 SDP Record

`HID Device` 作为一个 Classic Profile，如果想让对端设备能发现你，还需要自己创建并注册 `SDP Record`。

典型写法大致如下：

```c
uint8_t hid_service_buffer[300];
hid_sdp_record_t hid_params;

memset(&hid_params, 0, sizeof(hid_params));
// 根据实际需求填写 vendor_id / product_id / descriptor 等字段

hid_create_sdp_record(hid_service_buffer, 0x10001, &hid_params);
sdp_register_service(hid_service_buffer);
```

这里体现出来的规则是：

```text
Profile init
  !=
服务发现注册
```

也就是说，哪怕 `hid_device_init()` 已经完成了协议本身初始化和 `L2CAP` 注册，**Classic 场景下的 SDP 广播信息通常仍然要由应用自己注册**。

这一点在 `SPP`、`HID`、`A2DP` 等经典蓝牙 Profile 里都很常见。

------

##### 4. 真正发送 Report 也是事件驱动的，不是随时直接发

`HID Device` 发送报告时，通常也不是一上来就直接发，而是要先请求一次 `can send now`：

```c
hid_device_request_can_send_now_event(hid_cid);
```

然后在 `packet_handler` 里等到：

```text
HID_SUBEVENT_CAN_SEND_NOW
```

之后再发真正的 `input report`：

```c
hid_device_send_interrupt_message(hid_cid, report, report_len);
```

它底层最终还是走：

```c
l2cap_send(hid_device->interrupt_cid, (uint8_t*) message, message_len);
```

也就是说，`HID Device` 只是帮你把更上层的 `HID report` 封装并走到对应的 `L2CAP interrupt channel` 上。

这里同样体现了 `BTstack` 的一个核心思路：

> 上层协议的使用也基本是事件驱动模型，而不是“我想发就马上同步发”。

------

##### 5. 如果从零开始，一个 HID Device 的典型初始化顺序

如果不考虑我当前工程已经封装过的 `btstack_port_init()`，而是站在“通用使用 BTstack”的角度，我个人觉得 `HID Device` 的初始化顺序大致可以理解成：

```text
底层平台能力准备
  -> run loop / transport / chipset / TLV

基础协议初始化
  -> btstack_memory_init()
  -> hci_init()
  -> l2cap_init()
  -> sdp_init()

HID Device Profile 初始化
  -> hid_device_init(...)
  -> hid_device_register_packet_handler(...)
  -> hid_create_sdp_record(...)
  -> sdp_register_service(...)

本地设备参数
  -> local name / class of device / discoverable / connectable

控制器启动
  -> hci_power_control(HCI_POWER_ON)
```

这个顺序里最值得记住的一点是：

```text
hid_device_init() 只是在“协议层”把 HID 挂起来
hid_device_register_packet_handler() 才是在“应用层”把你的业务逻辑挂进去
hid_create_sdp_record() + sdp_register_service() 则是在“服务发现层”把它发布出去
```

------

##### 6. 用 HID 反过来看所有上层协议的共性

我现在觉得，很多上层协议/Profile 的使用，其实都可以套进下面这个模板：

```text
1. 先保证下层基础协议已经 init
2. 调用 Profile 自己的 init()
3. 注册应用 packet_handler / data callback
4. 如果是 Classic 服务，注册 SDP record
5. 如果协议需要额外对象，再创建 channel / endpoint / service
6. 上电启动后，在回调里根据事件驱动业务逻辑
```

例如：

- `SPP`
  - `rfcomm_init()`
  - `rfcomm_register_service(packet_handler, ...)`
  - `sdp_register_service(...)`

- `A2DP Sink`
  - `a2dp_sink_init()`
  - `a2dp_sink_register_packet_handler(...)`
  - `a2dp_sink_register_media_handler(...)`
  - `a2dp_sink_create_stream_endpoint(...)`
  - `sdp_register_service(...)`

- `HID Device`
  - `hid_device_init(...)`
  - `hid_device_register_packet_handler(...)`
  - `hid_create_sdp_record(...)`
  - `sdp_register_service(...)`

所以我个人目前的理解是：

> 上层协议/Profile 不是“只调一个 init 就能直接业务可用”，而是通常还要补上“业务回调注册”和“业务对象/服务注册”这两层。

------

### 小结

我现在对 `BTstack` 初始化和上层协议使用的理解，可以总结成下面两句话：

1. **核心协议初始化主要是自动挂接协议内部链路**  
   例如 `l2cap_init()` 会自动把 `L2CAP` 注册到 `HCI`。

2. **上层协议使用主要是应用自己补业务层回调和服务对象**  
   例如 `HID` 虽然 `hid_device_init()` 会自动把自己挂到 `L2CAP`，但应用仍然要自己注册 `packet_handler`、注册 `SDP record`，并在事件回调里完成真正的业务逻辑。

所以从工程实践角度看，我现在觉得最重要的不是死记每个 API，而是先建立下面这个判断：

```text
这个 API 是在做协议内部挂接？
还是在做应用业务挂接？
```

把这个问题分清，整个 `BTstack` 的初始化链就会清楚很多。

