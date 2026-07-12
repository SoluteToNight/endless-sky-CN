# Endless Sky 中文 Fork 同步工作流

## 分支模型

```
master                    ← 本地 upstream 镜像（纯上游代码）
feature/cjk-localization  ← 汉化功能分支（基于 master，定期 rebase）
```

- `master`：只包含上游代码，通过 fast-forward 保持与 upstream 一致
- `feature/cjk-localization`：所有汉化改动在此分支，定期 rebase 到最新 master

## 同步流程（Agent 驱动）

**触发指令**：`同步上游` 或 `sync upstream`等类似指令

Agent 自动执行：

1. 预检工作树、当前分支、未完成的 rebase 和遗留注入状态
2. `git fetch upstream --tags` 与 `git fetch origin`
3. 记录同步前的 master 和 origin 汉化分支 SHA，供摘要与 force-with-lease 使用
4. `git switch master && git merge upstream/master --ff-only`
5. `git switch feature/cjk-localization && git rebase master`
6. 无冲突时由脚本继续；发生冲突时保持 rebase 现场并交给 Agent
7. Agent 逐项解决冲突、执行 `git rebase --continue`，直到 rebase 完成
8. 构建验证；无论成功或失败都安全恢复临时注入的中文源码
9. 验证通过后，原子推送 master 和汉化分支
10. 向用户报告上游更新摘要、冲突处理和验证结果

> 同步前必须保证已跟踪文件没有未提交修改。脚本不会自动 stash、checkout 或删除用户修改；未跟踪文件会被列出并保留，如果它们会被目标分支覆盖，`git switch` 将安全停止。

### 冲突处理

| 场景 | 处理方式 |
|------|----------|
| 上游改动与汉化代码无交集 | 自动保留两边 |
| 上游修改了汉化未触及的区域 | 自动保留上游改动 |
| 上游重构了汉化依赖的接口 | 脚本暂停，Agent 理解语义后适配到新接口 |
| 上游删除了汉化依赖的功能 | 暂停，询问用户 |

Agent 解决冲突时必须对照 `汉化改动复现文档.md` 中的警告项，尤其检查 UTF-8 字符边界、CJK 换行宽度、字体着色器匹配、200% 缩放限制和纹理图集容量。禁止仅按 ours/theirs 整文件覆盖。

## 手动同步脚本

如需手动操作，使用原生 PowerShell 脚本 `scripts/sync-upstream.ps1`：

```powershell
./scripts/sync-upstream.ps1
```

脚本只自动完成无冲突项。遇到冲突时退出码为 2，并保留 rebase 状态。Agent 完成所有 `git rebase --continue` 后执行：

```powershell
./scripts/sync-upstream.ps1 -Finish
```

`-Finish` 使用同步开始时记录的 origin SHA 执行 `--force-with-lease`，避免在冲突处理期间覆盖其他人推送的新提交。构建验证成功后，master 与汉化分支通过 `git push --atomic` 一起发布；任一引用推送失败时两者都不更新。

## Release Tag 同步

上游 tag 通过 `git fetch upstream --tags` 同步到本地，但不会自动推送到 origin。需要发布到 origin 的 tag 必须由 Agent 核对目标提交后显式推送：

```bash
git push origin <tag>
```

汉化版本 tag 命名规则：`v<上游版本号>-cn`

```bash
git checkout feature/cjk-localization
git tag v0.10.16-cn
git push origin v0.10.16-cn
```

## 汉化 Commit 规范

遵循 Conventional Commits 风格：

| 类型 | 用途 |
|------|------|
| `build` | 构建系统/依赖变更 |
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `content` | 内容/资源文件 |
| `docs` | 文档 |

示例：`feat(text): Implement FreeType CJK glyph rendering with texture atlas`

## 汉化文件清单

| Commit | 文件 |
|--------|------|
| 构建依赖 | `CMakeLists.txt`, `source/CMakeLists.txt`, `vcpkg.json` |
| 渲染核心 | `source/text/Font.cpp`, `Font.h`, `GlyphCache.h`, `TextureAtlas.cpp`, `TextureAtlas.h`, `FontSet.cpp`, `FontSet.h` |
| 文本管线 | `source/text/Utf8.cpp`, `Utf8.h`, `WrappedText.cpp`, `WrappedText.h` |
| 着色器 | `shaders/font.vert`, `shaders/font.frag` |
| 数据加载 | `source/GameData.cpp`, `source/Screen.cpp` |
| 字体资源 | `data/SourceHanSansCN-Regular.otf`, `data/Ubuntu-Regular.ttf` |

## 编译与测试

### 环境要求

- MSYS2 ucrt64 工具链（`D:\msys\ucrt64\bin`），需包含 `g++`、`windres`、`objdump`
- 每次构建前确保 PATH 包含 ucrt64：`$env:PATH = "D:\msys\ucrt64\bin;$env:PATH"`

### 首次配置（仅需一次）

```powershell
$env:PATH = "D:\msys\ucrt64\bin;$env:PATH"
cmake --preset mingw
```

### 编译

```powershell
$env:PATH = "D:\msys\ucrt64\bin;$env:PATH"

# 1. 临时注入中文，并保存注入前的精确文件快照
python scripts/inject_hardcoded.py

# 2. 编译
cmake --build build/mingw --config Release

# 3. 还原注入前的文件内容
python scripts/inject_hardcoded.py --restore
```

> 临时硬编码中文是发布构建的一部分，但不得提交到 Git。注入器将原始内容和注入后内容记录到 `.injected_state.json`：恢复时仅处理仍与注入版本完全一致的文件，因此能够保留注入前已有的修改；如果构建期间文件再次变化，恢复会停止且不覆盖文件，交由 Agent 合并。同步脚本通过退出 trap 保证构建失败时也会执行恢复。

### 打包安装

```powershell
$env:PATH = "D:\msys\ucrt64\bin;$env:PATH"
cmake --install build/mingw --config Release
```

产物输出到 `install/mingw/`，包含：exe、DLLs、data、images、shaders、sounds 及根目录文本文件。可直接打包分发。

### 验证清单

每次同步后：

- [ ] Agent 自动验证：`cmake --build build/mingw --config Release` 编译通过
- [ ] Agent 自动验证：临时中文注入已恢复，工作树重新保持干净
- [ ] Agent 自动验证：同步报告中列出上游本次更新内容摘要
- [ ] 用户手动验证：启动游戏，确认中文 UI 显示正常
- [ ] 用户手动验证：CJK 换行无溢出/截断
- [ ] 用户手动验证：Latin + CJK 字体 fallback 正常
