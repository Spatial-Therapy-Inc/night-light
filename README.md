# 小夜灯（night-light）

一个 Tikpal 应用（Tikpal Voice 的扩展固件），用 Tikpal 开发包做的。夜里点一下就亮，照见手边和脚下。

- 进入应用就亮灯，30 秒后自己灭，最后 3 秒渐暗。点屏幕、短按侧键、摇一摇、扣下都能关灯。
- 长按屏幕转常开；灯灭后留在应用里黑屏待机，点一下马上亮。
- 光圈和蘑菇灯两种风格，左右滑切换。

规则在 `main/nightlight.c`（纯 C，有主机测试），画面和接线在 `main/main.c`。蘑菇灯的图由 `art/mushroom.svg` 经 `tools/make_art.py` 生成。

```bash
./kit build              # 编译，再跑主机测试
./kit install --slot 2   # USB 写进测试圆屏的位置 2，写完直接打开
./kit logs               # 看日志；./kit snap 截图，./kit heap 看内存
./kit build --release    # 提交用的那份
```

- `devkit/` 是开发包（git 子模块），钉在一个版本上，别改它。`git clone` 这个仓库以后，`./kit` 会自己把它拉下来。
- 应用清单是 `app.json`；逻辑写在 `main/`，能不依赖 ESP-IDF 的部分写成纯 C，`test/run.sh` 在电脑上测。
- 做完、自测过，照 `devkit/AGENTS.md` 开提交单。
