# 在线 PID 模板

`catalog.json` 提供在线模板目录，`templates/` 保存多语言模板。新增兼容模板并更新目录后，设备刷新在线目录即可使用，无需重新刷固件。维护步骤见 [templates/README.md](templates/README.md)。

目录包含标准燃油车示例，以及本项目实车验证的 Volkswagen ID.4 四项数据：`batterySocObd`、`batteryCurrent`、`batterySoc`、`hvBatteryVoltage`。ID.4 模板不代表适用于所有电动车；其他年款仍需验证。

支持新界面的固件通过下拉框选择模板，列表只显示当前语言的数据名称和启用复选框。“在线更新”下载并校验缓存，“应用”备份并替换当前 OBD 配置；未勾选项目保留但不查询，可在 OBD 页重新启用。自定义 PID 使用折叠表单编辑，在线模板项目只显示名称和启用框。

`title` 和 `description` 可包含多种语言；`name` 保持稳定，用于公式和 MQTT 标识。缺少当前语言时回退英文。`import/*.en.json`、`import/*.ja.json` 保留为数组格式兼容导入文件。

固件出厂默认查询缩减为发动机转速 `rpm` 和 ID.4 `batterySocObd`，不再把整套测试模板嵌入网页。升级现有设备时应保留其 `states.json`，避免覆盖用户配置。

GitHub 根地址：`https://raw.githubusercontent.com/bassce/sim7670g-4g/main/`。
Gitee 根地址：`https://raw.giteeusercontent.com/bassce/sim7670g-4g/raw/main/`。
目录路径均为 `pids/catalog.json`，模板路径为 `pids/templates/*.json`。发布 GitHub 后，Gitee 需要同步相同文件及校验值。
