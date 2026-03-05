# Endless Sky TTF Modernization 总结

本项目对 Endless Sky 的字体系统以及相关的文本布局渲染功能进行了彻底的现代化改造（TTF Modernization）。以下是核心文件修改、更改内容以及目的整理：

### 1. 核心渲染库的引入 (Phase 1)
为了支持动态矢量字体（TTF/OTF）以及随后的多语言、中日韩（CJK）渲染，我们接入了标准的排版库，取代了原版游戏简单的、预制好的静态 PNG 字母表。
* **文件：`CMakeLists.txt`, `vcpkg.json`, `source/CMakeLists.txt`**
  * **更改与目的**：在构建系统中增加了 `Freetype` 库的依赖声明，并让 CMake 链接该库。同时将新增的 `TextureAtlas.cpp` 加入到源码编译列表中。

### 2. 动态纹理图集与字形缓存构建 (Phase 2)
原版游戏只有98个英文字符，所以全部画在了一张图上。现在我们接入了全字符集的 TTF 字体，为了支持庞大的字符集（如中文），我们需要一种**按需渲染并把字符动态打包进纹理**的机制。
* **文件：`source/text/TextureAtlas.h/cpp` (新增)**
  * **更改与目的**：实现了一个基于 Shelf Packing 算法的动态大纹理图集（大小为 4096x4096）。当遇到需要显示的新字符时，它能在显存中动态分配一块矩形区域来存放由 FreeType 光栅化的字形像素（使用 `GL_RED` 格式节省显存）。
* **文件：`source/text/GlyphCache.h` (新增)**
  * **更改与目的**：定义了 `GlyphInfo` 结构体，用来缓存每个已加载字符的各种精准度量值（包括宽、高、推进值 advance、水平/垂直偏移 bearing，以及在动态图集中的 UV 坐标）。**特别注意的是，我们在这里使用了 `float` 浮点数保存尺寸度量**，这是为了在后续的缩放中消除原版 `int` 整除带来的 1px 抖动问题。

### 3. Font 类的完全重构与字体栈架构 (Phase 3)
完全重写了字体加载和绘制核心逻辑。
* **文件：`source/text/Font.h/cpp` 等**
  * **引入字体降级回退（Fallback）**：修改加载逻辑为接受 `vector<path>` 字体路径列表（`FontSet` 和 `GameData.cpp` 也做了相应修改以支持传入多个字体）。优先读取和显示英文主渲染字体（比如 `Ubuntu-Regular.ttf`），当遇到无法渲染的字符（如中文）时，自动回退到降级字体（如 `SourceHanSansCN-Regular.otf` 思源黑体）。
  * **支持 2x 超采样（Supersampling）渲染**：引入了 `renderScale` 参数，指示 FreeType 按照双倍的高分辨率（如将 14px 放大为 28px）来绘制文字并存入图集。在 Shader 进行 UV 屏幕贴图时，将其缩小一倍以适应屏幕原来的 UI 布局点距，利用 OpenGL 自带的线性过滤达到了抗锯齿且极为清晰的显示效果。

### 4. 渲染位置与细节修复 (Phase 4)
为了完美还原原版 PNG 字体的在屏幕上的排版像素对齐效果，修复了引入 TTF 后出现的各项显示偏移问题：
  * **字形尺寸修复**：修复了错误将高分辨率纹理尺寸直接传给屏幕 `glyphSize` 导致字体显示大两倍的问题，改为逻辑尺寸。
  * **基线对齐修复（Baseline Alignment）**：弃用原版基于字符高度的垂直排版，引入排版工业标准的 `ascender`（顶部到基线）概念，确保英文字母与 CJK 字符在同一水平基线上。
  * **消除整除截断抖动**：将 `GlyphInfo` 所有的度量单位改为 `float` 浮点数并做浮点除法，消除了直接 `int` 相除在不同字符上出现的不规则 1px 垂直和水平位置抖动。
  * **行高（Line Height）修复**：弃用包含内部行距（linegap）的 FreeType `height`，改为严谨的 `ascender - descender` 算法，避免了整体 UI 垂向居中时偏下的问题。

### 5. 高级排版和多语言 CJK 换行支持 (Phase 4)
原版游戏的文本包裹器（Word Wrapper）依赖空格或者连字符来决定换行位置，遇到连续的中日韩字符时会直接溢出屏幕外。
* **文件：`source/text/WrappedText.h/cpp` (重构)**
  * **更改与目的**：实现了在无需空格情况下的 CJK 文字自动断行。新增了一个内部函数 `IsCJK(char32_t c)` 判定码位。此外，消除了修改底层缓冲区的行为（移除了 `\0` 截断法），引入字长并使用 `std::string_view` 向底层渲染层传递按确切长度测量的字词，安全地支持了多语言字符串的处理。

### 6. UTF-8 安全审计 (Phase 4)
由于原版代码一直默认是单字节 ASCII 模式，一些代码在处理字符串时存在隐患。
* **文件：`source/ShipInfoDisplay.cpp` 和 `source/text/Utf8.h/cpp` 等**
  * **更改与目的**：原代码（如转换仓位名称大小写的地方）使用了 C 标准库的 `::tolower` 操作。当传入多字节符号组合而成的 UTF-8 时，会引发程序崩溃。我们将其改用了更安全的 `Format::LowerCase`，避免了对多字节编码的破坏行为。同时，让 `Utf8::DecodeCodePoint` 函数全面对接了 `std::string_view`，使其更安全高效。
* **文件：`source/text/Font.cpp` 的截断函数**
  * **更改与目的**：原版的 `Font::TruncateMiddle`/`TruncateFront` 在文本过长时（如过长的装备名或玩家名）使用 `std::string::substr` 按原生字节切割并添加省略号，这极容易从中间破坏一个由3字节组成的汉字。我们引入预先使用 `Utf8::DecodeCodePoint` 记录安全字符边界索引的算法代替原来的原生字节级二分搜索查表，根除了此类引起的乱码崩溃 Bug。

### 7. 极致的顶点批处理渲染性能优化 VBO Batching (Phase 5)
大幅优化了渲染性能，减少了 Draw Call。
* **文件：`shaders/font.vert`**
  * **更改与目的**：废除了原来传入的诸多 Uniform 变量（如 `position`, `uv_rect`, `aspect`, `glyphSize`）。彻底简化了顶点着色器的逻辑，只保留最基础的屏幕坐标系到 OpenGL 标准设备坐标（NDC）的最终缩放。
* **文件：`source/text/Font.cpp` 中的 `DrawAliased` 函数**
  * **更改与目的**：原版循环处理字符串时，**每一个可视字符都会向 GPU 发送一次 `glDrawArrays` 绘制调用**，导致 UI 文本较多时性能断崖式下跌。优化后：在 CPU 端计算每一个字符对应的准确 UV 和屏幕坐标，构成 2 个三角形的 6 个顶点，然后通过 `glBufferData(GL_STREAM_DRAW)` 利用 VBO 将整句话的顶点一次性送给显卡，最后**只需 1 次 Draw Call 即可画出整段字符**。这种批处理方式大幅减少了驱动开销和 CPU 负担。

---

通过以上的一系列重构和优化，现在游戏的文字系统不但具备了工业级的字体排印精度和性能，最重要的是清除了未来制作全中文/日/韩区翻译的最后一道底层代码技术障碍（并且彻底兼容之前的原始像素 UI 观感）。
