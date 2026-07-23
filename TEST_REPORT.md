# DriveGuard-AI 1.0.0 验收测试记录

测试日期：2026-07-23

测试环境：Windows，本地 Python 可用；Qt 构建环境在后续验证中记录实际结果。

## 已执行

```powershell
python -m py_compile scripts\detector.py scripts\smoke_test.py scripts\repo_check.py tests\test_detector_logic.py
```

结果：通过。

```powershell
python -m unittest discover -s tests -v
```

结果：14 项测试通过，覆盖单一 `decide_level`、PERCLOS 时间窗口、眨眼、哈欠、四级仿真、周期重置、字段完整性、无效样本和固定种子复现。

```powershell
python scripts\smoke_test.py
```

结果：`PASS: detector simulation smoke test`。

```powershell
python scripts\detector.py --mode simulation --runtime runtime --interval 0.01 --max-samples 10 --seed 20260723 --no-frame-output
```

结果：正常退出，输出 10 行 JSON Lines。

```powershell
python scripts\repo_check.py
```

结果：`PASS: repository check`。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<QtRoot>
cmake --build build --config Release -j 8
```

结果：配置完成，Qt 构建成功，生成 `build/bin/DriveGuardAI.exe`。配置阶段提示缺少 Vulkan headers，但本项目 Qt Widgets/Charts 构建未受影响。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -QtRoot <QtRoot>
```

结果：生成 `dist/DriveGuard-AI-1.0.0`，包含 exe、Qt DLL、platform 插件、SQLite 驱动、scripts、assets、docs、schemas、LICENSE 和 SHA-256 清单。`windeployqt` 提示缺少 `dxcompiler.dll`/`dxil.dll`，不影响当前 Widgets 演示包生成。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File verify_release.ps1 -Root dist\DriveGuard-AI-1.0.0
```

结果：`Integrity: PASS (65 file(s) checked)`。

## 未在当前环境执行

- 公开数据集离线准确率评估。
- 长时间真实道路测试。
- 车规、安全认证或医学诊断验证。
