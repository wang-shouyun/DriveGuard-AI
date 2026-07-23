# DriveGuard-AI 开源课程项目交付指南

DriveGuard-AI 是具备企业交付风格的开源课程设计和软件工程原型。

## 交付物

- Qt 桌面程序；
- Python 检测脚本；
- 本地 WAV 语音提醒；
- SQLite runtime 目录；
- HTML/CSV 报告；
- README、设计文档、测试记录、隐私和限制说明；
- Windows 发布包和 SHA-256 校验文件。

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -QtRoot "C:\Qt\6.x.x\mingw_64"
```

## 打包

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

发布目录默认在 `dist/DriveGuard-AI-1.0.0`。

## 演示流程

1. 启动程序。
2. 先运行虚拟场景仿真，展示四级疲劳变化。
3. 展示事件中心、确认和误报标记。
4. 导出 HTML 报告。
5. 如果摄像头可用，再展示摄像头模式。

## 验收清单

- 版本显示为 1.0.0。
- 模拟模式能覆盖四级状态。
- 事件不会大量刷屏。
- 报告图片可脱离 runtime 查看。
- 语音提醒文件存在。
- runtime 用户数据不进入发布包。

## 限制

本项目不承诺量产车载安全能力，不编造准确率，不依赖独立显卡，不上传云端。
