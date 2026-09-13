# SIM7670G-4G 派生版本

上游：[adlerre/obd2-mqtt](https://github.com/adlerre/obd2-mqtt)，基准提交 `54a950a6bea7600c9faae508bf1f23c8cfc0748e`。本仓库保留上游历史、署名和许可证，合入 Windows 本地适配版本（本地 main `00ce68af397b2dcff0a1237f810e46334b47762d`）。

硬件目标是 Waveshare ESP32-S3-A-SIM7670X-4G-V2 / SIM7670G，默认环境 `WS-SIM7670G-V2_BLE`，详见 [板级说明](SIM7670G-V2.md)。包含 SIM7670G 联网、GNSS、BLE 诊断以及每次查询的 CAN 地址/流控与字段解析修复。ID.4 四项基础读取已在实车验证，长期稳定性及其他车型兼容性仍需测试。

新增 [translations](translations/) 与 [pids](pids/) 用于后续静态下载服务；GitHub 为主仓库，国内 Gitea 可拉取镜像。当前尚未实现网页多语言及在线模板选择。热点不暂停上传、可选蓝牙待机节电已记录方案但未实施；在线固件升级留待正常版本迭代。

本次为源码和资源发布，不编译或烧录固件。原上游 README、构建与发布工作流保留用于溯源，其中上游下载链接不代表本派生版本。不要通过上游 Web Installer 安装本版本。
