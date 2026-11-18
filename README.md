# 声核Windows 98 桌面调音台

把声核通道放进 Windows 98 风格的桌面、窗口和任务栏，用复古桌面布局操作调音台。

当前源码版本：**0.1.9**。原生 Windows 界面，适用于 Windows x64 声核。

![布局预览](assets/preview.png)

## 界面与操作

- 采用 Windows 98 风格的桌面、任务栏和窗口控件。
- 在桌面窗口中查看和操作通道、效果器及路由。
- 使用原生 Windows 绘制，不需要网页皮肤运行环境。

- 界面语言：中文、英文、日文。

## 安装

[先下载声核](https://audcore.utae.cn/download/)

在支持原生皮肤的声核中打开构建生成的 `.skin.audcore` 文件，再到皮肤设置选择「Audcore 98」。

所需原生接口为 ABI 0.2，皮肤在宿主进程内运行。

公开源码不代表已经更新官方在线版本，下载仍以官方签名列表为准。

## 从源码生成安装包

构建工具：

- MSVC 编译器 **19.44.35222**（VC Tools 14.44.35207）。
- Windows SDK **10.0.26100.0**。
- Python **3.14.2**。

```powershell
git clone https://github.com/audcore/audcore-skin-windows98-desktop.git
cd audcore-skin-windows98-desktop
powershell -NoProfile -File build.ps1
```

中间产物为 `build/skin.dll`。最终安装包：

`dist/cn.audcore.skin.win98-v0.1.9.skin.audcore`

原生安装包本质是 DLL，不是独立运行的播放器或调音程序。构建串行执行，使用 BelowNormal 优先级和资源限制。

## 开发与反馈

- [声核官网](https://audcore.utae.cn/)
- [声核使用指北](https://audcore.utae.cn/guide/)
- [skin-dev：SDK、接口资料与模拟开发工具](https://audcore.utae.cn/skin-dev/)
- [皮肤开发指引](https://github.com/audcore/audcore-document/blob/main/docs/皮肤开发指引.md)
- [manifest 与构建发布规范](https://github.com/audcore/audcore-document/blob/main/docs/皮肤发布规范.md)
- [所有皮肤的源码列表](https://github.com/audcore/audcore-document/blob/main/skins.md)
- [此皮肤的问题反馈](https://github.com/audcore/audcore-skin-windows98-desktop/issues)
- [声核本体的问题反馈](https://github.com/audcore/audcore-document/issues)

修改名称、版本、语言或构建规则时编辑根目录 manifest.json；界面源码和资源按清单维护。每款皮肤只在自己的仓库更新，构建输入无变化时跳过。

## 许可证与验证范围

自有代码采用 [MIT 许可证](LICENSE)。系统字体只按名称引用，不随包分发。

- JSON 解析库保留 [nlohmann/json 的原始许可](tools/mixer-console/vendor/LICENSE.json)。
- WebP 固定为 1.6.0，构建脚本核验下载包与静态库 SHA-256。
- [WebP 许可](skin-contract/cpp/win98/libwebp-COPYING.txt)与[专利声明](skin-contract/cpp/win98/libwebp-PATENTS.txt)随源码保留。
- 已使用上述工具链独立编译，并通过最终 PE 产物检查。
- 本轮仓库改名与说明整理没有重新编译或替换在线皮肤包。
- 未重新启动宿主进行窗口、鼠标交互或跨屏验收。
