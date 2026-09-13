# ID.4 Car Scanner 对照查询配置

本配置基于 2026-09-13 本车 iCar Pro 2S 的 `btsnoop_hci.log`，SHA-256：
`fe55783bc2400513d0e9da574b12efea11a04b4f214336feeae7fe896d0fef53`。
配套固件：`v1.0.0-carscanner.1`。本次仅编译，未烧录，未做实车验证。

| 项目 | 请求地址 → 接收地址 | 查询 | 抓包值 |
| --- | --- | --- | --- |
| batterySocObd | 18DB33F1 → 18DAF105 | 015B | 95.69% |
| batteryCurrent | 18DB33F1 → 18DAF105 | 019A | −1.2～−0.6 A |
| batterySoc | 17FC007B → 17FE007B | 22028C | 96.0% |
| hvBatteryVoltage | 17FC007B → 17FE007B | 221E3B | 393.5 V |

SOC 两项来自不同请求，不应当作同一原始值；电流符号对应充电或放电仍待实测。
UDS 换算参考 [MEB 参数表](https://github.com/spot2000/Volkswagen-MEB-EV-CAN-parameters/blob/main/VW%20MEB%20UDS%20PIDs%20list.csv)，具体回复由本车抓包验证。

## 到车上后的验证顺序

1. 备份设备当前配置，再烧录配套固件；保留现有网络、MQTT、蓝牙配对及文件系统配置。
2. 在网页 OBD States 导入同目录的 `states-id4-carscanner.json`。导入会替换状态列表，先导出旧列表。仅更新应用固件不会自动改变设备上的旧 PID 配置。
3. 先只启用 `hvBatteryVoltage`，通过 Debug 日志核对 SH、CRA、FCSH、FCSD、FCSM 和 `221E3B` 的实际应答。成功后依次启用 UDS SOC、015B 和 019A，最后验证混合轮询与重连。
4. 对照仪表与 Car Scanner 读数时顺序连接适配器，避免两个客户端争用。停车 READY 状态下验证；长期稳定性另行测试。

## 查询流程

JSON 数值为十进制，文档中的 CAN 地址和请求为十六进制。
`pid.protocol=0` 继承当前连接协议；6/7/8/9 是每次查询临时使用的 ISO 15765 CAN 协议。
`receiveHeader=0` 自动接收，非零值设置 `AT CRA`。
`flowControlHeader=0` 自动流控；非零值设置 `AT FCSH`、`AT FCSD`、`AT FCSM 1`。
`flowControlData=3145728` 对应 `300000`，即本次抓包使用的流控内容。
此配置显式列出全部参数，不执行旧 B 文件的任意 `initCommands` 数组；请使用本文件导入。

UDS 的典型配置序列（继承/切换至协议 7 后）：

```text
AT SH 17FC007B
AT CRA 17FE007B
AT FCSH 17FC007B
AT FCSD 300000
AT FCSM 1
221E3B
```

每次查询完成或发生错误后恢复自动流控、原协议、自动接收和默认请求地址；恢复不成功会阻止后续车辆查询。
使用完整 8 位 SH 表达 29 位地址，等价于抓包的 CP + 6 位 SH 寻址方式。命令语义依据 [ELM327 官方手册](https://www.elmelectronics.com/wp-content/uploads/2017/01/ELM327DS.pdf)。

本固件保持 ATH0/CAF1 的自动格式回复，与 Car Scanner 的 ATH1 显示格式不同。回归测试使用相同车辆消息转换后的 ATH0 格式；真实适配器的 ATH0 输出仍需到车上验证。
CAN 查询未指定接收过滤，或响应需要多帧时，不添加提前终止接收的数量后缀。此配置所有 `numResponses=0`，优先完整接收。
同次回复中的负响应不会覆盖有效正响应；若出现互相矛盾的多个正响应，会报告歧义而非选择任意值，应配置精确接收地址。

## 电流字段

`419A0600FFFFFFF7` 去掉服务/PID 后为 6 个数据字节。
设置 `numExpectedBytes=6, dataOffset=4, dataLength=2, signedValue=true, scaleFactor=0.1`，
先按整数取出末尾 `FFF7`，再按 16 位有符号数转换成 −9，得到 −0.9 A。
字段偏移从服务/PID 后的第一个数据字节开始计 0；字段长度非零时覆盖 Response Format，使用选定字节、符号、比例和偏移量进行换算。
原始总长度、字段范围、CAN 地址及流控格式均进行校验，不把错误回复当成 0 发布。
