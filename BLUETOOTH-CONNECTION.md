# SIM7670G V2 蓝牙连接页面

适用构建环境：`WS-SIM7670G-V2_BLE`。本次增加独立 BLE 扫描与连接页面，保留原作者的 Protocol 设置方式。

## 使用

1. 给 OBD 适配器供电，关闭手机上占用适配器的诊断应用。
2. 连接设备配置热点，打开 `http://192.168.4.1`。在 Settings 中按原方式选择 Protocol。
3. 在 Info 或 Settings 的 **Bluetooth OBD** 卡片点击 **Choose device**。
4. 点击 **Scan nearby Bluetooth**，约 5 秒后显示附近设备名称、MAC 和信号强度。扫描会断开当前 OBD 蓝牙连接。
5. 选择自己的适配器，例如 IOS-Vlink，点击 **Connect**。页面分别显示蓝牙连接和 ELM327 初始化阶段。
6. 初始化成功并保存后，自动返回进入前的 Info 或 Settings 页面，卡片显示 **Online — ELM327 initialized**、设备名称和 MAC。连接失败留在连接页显示原因。

Settings 页未保存的表单会在往返过程中保留在浏览器内存中。连接成功自动保存选中的蓝牙设备、地址类型、Protocol，并启用 OBD；其他表单改动仍需在 Settings 点击保存。刷新浏览器会丢失未保存的表单改动。

“Online”表示 BLE 连接及 ELM327 初始化成功，不代表车辆支持已配置的 PID。ID.4/MEB 没有返回发动机转速等默认项目时，应另行检查 PID、请求头和协议。

此板只扫描 BLE。未广播 OBD 服务的设备也会列出并允许尝试；实际需要兼容当前 BLESerial 使用的 OBD 串口服务。离开配置热点后，固件按已保存配置自动连接，失败后约 30 秒重试。配置热点使用期间保留 BLE 连接，但暂停车辆 PID 读取。

## Home Assistant

启用 MQTT discovery 后，新增两个诊断实体：

| 名称 | 含义 |
|---|---|
| OBD Adapter Connected | ELM327 初始化成功后为联机，断开后为脱机 |
| OBD Connection Status | disabled、scanning、connecting、initializing、connected、bluetooth_error、elm_error 等连接阶段 |

它们复用现有设备及 MQTT 可用性配置。状态变化时发布，并约每 5 秒补发；不要求 GPS 定位成功。配置热点使用期间会维护已建立的 MQTT 会话及 OBD 状态发布，但普通车辆和定位上报仍沿用原来的暂停行为。

## VS Code 构建与升级

在 VS Code 打开本文件所在的 `obd2-mqtt` 项目目录，选择 PlatformIO 环境 `WS-SIM7670G-V2_BLE`。

- **Build** 编译主固件。
- **Build Filesystem Image** 构建网页文件系统。
- 主固件和网页都更新到设备后，才会出现本次连接页面；仅上传主固件不会更新网页。

**Upload Filesystem Image 会覆盖文件系统内的设置和 PID 配置。升级前导出 Settings 和 OBD 状态配置，升级后恢复，避免丢失现有 APN、MQTT 及自定义 PID。** 沿用现有分区布局，无需擦除整片 Flash。本次未执行任何设备上传。

## 已完成验证

- Angular 测试 10 项通过，包括扫描、Protocol/随机地址类型传递、初始化成功返回、失败停留、旧任务状态防误跳转及表单保留。
- 网页生产构建、主固件构建及 LittleFS 镜像构建通过。
- 原有 GPS 失败不阻断 HA 上报的宿主回归测试通过。
- 本地模拟接口浏览器验证扫描列表、连接中状态、成功返回及联机信息显示；模拟设备数据不是实机测量。
- 尚需更新后验证 Vgate 实机连接、断线重连及车辆数据。未刷写开发板。

编译静态 RAM 约 58 KiB；主程序约 1.30 MiB，3.75 MiB 应用槽剩余约 2.45 MiB。静态 RAM 数字不包含运行时 BLE/WiFi 缓冲、任务栈和堆，不能当作实机剩余 RAM。扫描最多保留 40 个设备，限制扫描内存占用。
