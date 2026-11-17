# Endless Sky 中文分支同步工作流

## 分支

- `master`：上游 `endless-sky/endless-sky` 的镜像，只做 fast-forward。
- `zh-cn`：汉化分支，基于 `master`，用 merge 或 rebase 跟进上游。

## 汉化分层

| 层 | 位置 | 说明 |
|---|---|---|
| 引擎 | `source/text/`（Font、TextureAtlas、GlyphCache、WrappedText、Utf8、Clipboard、Localization）、`shaders/font.*`、`source/Edit.cpp`、`DialogPanel`、`ConversationPanel` | FreeType 字体栈、UTF-8 安全的换行/截断/编辑、中文标点禁则、IME 输入 |
| 字体 | `data/Ubuntu-Regular.ttf`、`data/SourceHanSansCN-Regular.otf` | 拉丁字体在前，CJK 回退在后 |
| 数据正文 | `data/**/*.txt` | 对白、任务、新闻、描述等直接译在数据里；根节点 ID、条件、label/goto、phrase 引用名保持英文 |
| 专名 | 各定义块中的 `"display name"` / `"plural"` | 装备、舰船、星球、星系、政府、矿物、虫洞 |
| 兼作标识符的界面文本 | `data/_ui/translations.txt`（`translation` 根节点） | 属性名、分类、货物、设置项与选项值、命令描述、游戏规则、日志分类与标题、悬停提示键等；绘制时由 `Localization::Translate` 整串替换，代码与数据里的键保持英文 |
| 仅用于显示的 C++ 文本 | `source/*.cpp` | 对话框、消息、拼接句子直接改为中文；`Format` 的信用点/吨/时长与 `Date` 使用中文格式 |

显示期词典只对 `Font::Draw`/`Width`/`FormattedWidth` 绘制的**整串**生效，对 `WrappedText`/`TextArea`/对话框正文/`Messages` 无效。

## 跟进上游

1. `git fetch upstream`，`master` 执行 `git merge --ff-only upstream/master`。
2. 在 `zh-cn` 上合并或 rebase `master`，逐项解决冲突，禁止整文件取 ours/theirs。
3. 数据冲突：保留上游的结构与逻辑改动，再把显示文本译回中文；新增英文文本与新专名补译，专名追加 `"display name"`。
4. C++ 冲突：上游改动的显示句子重新汉化；新增的标识符型界面字符串加入 `data/_ui/translations.txt`。
5. 若上游修改了 `Font`、`WrappedText`、`Edit` 或数据加载接口，按语义适配，重点复查 UTF-8 码点边界、CJK 换行与字体着色器匹配。

## 验证

```powershell
cmake --build --preset <preset>-debug --target EndlessSky EndlessSkyTests
ctest --preset <preset>-test
"Endless Sky.exe" --resources <仓库根目录> --config <临时空目录> --parse-assets
ctest --test-dir build/<preset> -C Debug -L "integration$"
```

- `--parse-assets` 必须用独立的临时 `--config`，避免加载真实存档；结果应为 “Parse completed with no error(s).”
- 集成测试并行运行时可能因共用配置目录而偶发失败，失败项需串行重跑确认。
- 手动检查：中英混排换行、标点不出现在行首、输入框中文输入与退格、装备/船坞/地图/任务/设置面板文本不溢出。

## 提交规范

Conventional Commits，例如 `feat(l10n): ...`、`fix(text): ...`。发布 tag：`v<上游版本>-cn`。
