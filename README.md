# Audcore 98

独立原生皮肤，版本 **0.1.9**，Windows x64，原生 Skin ABI **0.2**。原生皮肤在宿主进程内执行，请只安装可信来源。

![预览](assets/preview.png)

## 声核与开发

- [声核官网](https://audcore.utae.cn/) · [下载声核](https://audcore.utae.cn/download/) · [使用指北](https://audcore.utae.cn/guide/)
- [skin-dev 皮肤开发入口与 SDK](https://audcore.utae.cn/skin-dev/)
- [皮肤开发与发布规范](https://github.com/audcore/audcore-document/blob/main/docs/皮肤发布规范.md)
- [皮肤列表](https://github.com/audcore/audcore-document/blob/main/skins.md)

## 独立构建与安装

安装 Visual Studio C++ Build Tools（MSVC 19.44.35222、Windows SDK 10.0.26100.0）和 Python。工具版本在 manifest.json 声明；当前是显式脚本 recipe，使用现有低占用、串行构建入口，正式发布仍须人工核对实际工具版本。

```powershell
git clone https://github.com/audcore/audcore-skin-win98.git
cd audcore-skin-win98
powershell -NoProfile -File build.ps1
```

最终输出 `dist/cn.audcore.skin.win98-v0.1.9.skin.audcore`，中间产物 `build/skin.dll`。原生 .skin.audcore 本质为 DLL；在声核中打开安装并选用，不直接运行 DLL。本仓库是皮肤的唯一维护来源，不依赖私有源码。源码迁移不代表已更新官方在线目录。

构建遵守全机单项编译锁、BelowNormal 优先级和资源限制；不并行构建其他项目。第三方依赖和许可随源码保留。WebP 固定为 1.6.0，原构建脚本校验下载包及静态库 SHA-256。

## 反馈与许可

[皮肤问题](https://github.com/audcore/audcore-skin-win98/issues) · [声核本体问题](https://github.com/audcore/audcore-document/issues)。自有代码采用 MIT，第三方代码保留各自许可证。

## 原有使用与测试说明



已用声明的 MSVC、Windows SDK 和 Python 独立编译，并通过最终 PE 产物检查。未进行真实宿主窗口验收。
