# 车型 PID 模板

- `catalog.json`：在线目录，提供多语言模板的相对路径、字节数和 SHA-256。
- `templates/`：服务器格式，一份查询参数包含英文、简体中文、日语 `description`。应由支持在线模板的网页下载并预览，不要将整个多语言对象直接导入 OBD 数据页。
- `import/*.en.json`、`import/*.ja.json`：显示名称已经转换为单一语言的兼容配置，可在 OBD 数据页导入。导入会替换整个状态列表，请先备份。

提供标准燃油车基础测试（转速、车速、冷却液温度）及本项目实测的 ID.4 四项基础数据。标准测试项取决于车辆支持，ID.4 模板不是通用电动车模板。本次 `1.0.1` 仅增加日语显示名称，查询参数、地址、公式和单位保持不变，不含清除故障码或写车辆配置的指令。

`name` 是稳定的公式/MQTT 标识，只翻译 `description`。已接入加载器的固件（例如本机 `v1.0.0-resources.1`）在下载预览时选择当前语言，缺译时使用英文；应用后保存为原有字符串 description 结构。切换界面语言不会改写已安装的自定义条目。

本次只发布资源，不发布尚待测试的加载器源码，也不更新设备固件。设备里的离线内置模板保持原样；要预览日语显示名称，请刷新在线目录并下载在线模板。

GitHub 根地址：`https://raw.githubusercontent.com/bassce/sim7670g-4g/main/`。国内 Gitee 镜像根地址：`https://raw.giteeusercontent.com/bassce/sim7670g-4g/raw/main/`。目录路径均为 `pids/catalog.json`；镜像同步前可选择 GitHub 源。
