<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>Color</name>
    <message>
        <source>invalid ICC profile</source>
        <translation>无效的 ICC 配置文件</translation>
    </message>
    <message>
        <source>unsupported ICC color space for RGBA data</source>
        <translation>RGBA 数据不支持该 ICC 色彩空间</translation>
    </message>
    <message>
        <source>cannot build a color transform from the ICC profile</source>
        <translation>无法根据 ICC 配置文件创建颜色转换</translation>
    </message>
</context>
<context>
    <name>Image</name>
    <message>
        <source>image too large for the available memory (needs %1 GB, limit %2 GB)</source>
        <extracomment>GB: gigabytes.</extracomment>
        <translation>图像过大，可用内存不足（需要 %1 GB，上限 %2 GB）</translation>
    </message>
    <message>
        <source>(no description)</source>
        <translation>（无描述）</translation>
    </message>
    <message>
        <source>linear, chromaticities from the file</source>
        <translation>线性，色度坐标取自文件</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed: the file&apos;s chromaticities are invalid)</source>
        <translation>线性 BT.709（假定：文件中的色度坐标无效）</translation>
    </message>
    <message>
        <source>%1 (assigned by the decoder)</source>
        <translation>%1（由解码器指定）</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed)</source>
        <translation>线性 BT.709（假定）</translation>
    </message>
    <message>
        <source>sRGB (assumed)</source>
        <translation>sRGB（假定）</translation>
    </message>
    <message>
        <source>invalid dimensions (%1×%2×%3)</source>
        <translation>无效的尺寸（%1×%2×%3）</translation>
    </message>
    <message>
        <source>%1 (file metadata, via Qt)</source>
        <translation>%1（文件元数据，通过 Qt 读取）</translation>
    </message>
    <message>
        <source>SVG is only decoded in the graphical interface</source>
        <translation>SVG 仅在图形界面中解码</translation>
    </message>
    <message>
        <source>damaged, truncated or unsupported %1 file</source>
        <translation>已损坏、不完整或不受支持的 %1 文件</translation>
    </message>
    <message>
        <source>custom primaries</source>
        <translation>自定义原色</translation>
    </message>
    <message>
        <source>Cannot decode: %1</source>
        <translation>无法解码：%1</translation>
    </message>
    <message>
        <source>%1 (assumed: %2)</source>
        <translation>%1（假定：%2）</translation>
    </message>
    <message>
        <source>Not enough memory to decode the image.</source>
        <translation>内存不足，无法解码图像。</translation>
    </message>
    <message>
        <source>Decoding error: %1</source>
        <translation>解码错误：%1</translation>
    </message>
    <message>
        <source>HEIC images need Microsoft&apos;s “HEIF Image Extensions” and “HEVC Video Extensions”, from the Microsoft Store.</source>
        <translation>HEIC 图像需要 Microsoft Store 中 Microsoft 的“HEIF Image Extensions”和“HEVC Video Extensions”。</translation>
    </message>
    <message>
        <source>HEIC images need an HEVC decoder, which imageViewer does not include on this system (patents); convert them to another format first.</source>
        <translation>HEIC 图像需要 HEVC 解码器，而 imageViewer 在此系统上不包含该解码器（专利原因）；请先将其转换为其他格式。</translation>
    </message>
    <message>
        <source>the decoder took longer than %1 s and was stopped</source>
        <extracomment>%1: seconds, e.g. &quot;30&quot;.</extracomment>
        <translation>解码器耗时超过 %1 秒，已被停止</translation>
    </message>
</context>
<context>
    <name>OpenWith</name>
    <message>
        <source>Choose an Application</source>
        <translation>选择应用程序</translation>
    </message>
    <message>
        <source>Applications (*.app)</source>
        <translation>应用程序 (*.app)</translation>
    </message>
</context>
<context>
    <name>Overlay</name>
    <message>
        <source>File name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>尺寸</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>文件大小</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>缩放</translation>
    </message>
    <message>
        <source>Color space</source>
        <translation>色彩空间</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>修改日期</translation>
    </message>
    <message>
        <source>Position in the folder</source>
        <translation>在文件夹中的位置</translation>
    </message>
    <message>
        <source>Display output</source>
        <translation>显示输出</translation>
    </message>
</context>
<context>
    <name>Renderer</name>
    <message>
        <source>SDR (sRGB)</source>
        <translation>SDR (sRGB)</translation>
    </message>
    <message>
        <source>EDR · headroom %1×</source>
        <extracomment>EDR: Extended Dynamic Range (macOS); %1: how many times SDR white the display can show, e.g. &quot;2.50&quot;.</extracomment>
        <translation>EDR · 余量 %1×</translation>
    </message>
    <message>
        <source>Linear sRGB managed by ColorSync · no HDR headroom</source>
        <translation>由 ColorSync 管理的线性 sRGB · 无 HDR 余量</translation>
    </message>
    <message>
        <source>scRGB · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>scRGB · SDR 参考白 %1 nits · 峰值 %2 nits</translation>
    </message>
    <message>
        <source>HDR10 (PQ) · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>HDR10 (PQ) · SDR 参考白 %1 nits · 峰值 %2 nits</translation>
    </message>
    <message>
        <source>Cannot initialize the GPU (QRhi).</source>
        <translation>无法初始化 GPU（QRhi）。</translation>
    </message>
    <message>
        <source>The GPU does not support RGBA16F textures.</source>
        <translation>GPU 不支持 RGBA16F 纹理。</translation>
    </message>
    <message>
        <source>Cannot create GPU resources.</source>
        <translation>无法创建 GPU 资源。</translation>
    </message>
    <message>
        <source>Cannot create the swapchain.</source>
        <translation>无法创建交换链。</translation>
    </message>
    <message>
        <source>(Qt defaults, not measured)</source>
        <translation>（Qt 默认值，未经测量）</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>系统默认</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>语言：</translation>
    </message>
    <message>
        <source>Languages other than English are machine translations awaiting review by native speakers.</source>
        <translation>除英语外，其他语言均为机器翻译，有待母语人士审校。</translation>
    </message>
    <message>
        <source>Reopen the last image at startup</source>
        <translation>启动时重新打开上次查看的图像</translation>
    </message>
    <message>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <source>Black</source>
        <translation>黑色</translation>
    </message>
    <message>
        <source>Dark gray</source>
        <translation>深灰色</translation>
    </message>
    <message>
        <source>Gray</source>
        <translation>灰色</translation>
    </message>
    <message>
        <source>Light gray</source>
        <translation>浅灰色</translation>
    </message>
    <message>
        <source>White</source>
        <translation>白色</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>自定义…</translation>
    </message>
    <message>
        <source>Background:</source>
        <translation>背景：</translation>
    </message>
    <message>
        <source>Remember the window size and position</source>
        <translation>记住窗口大小和位置</translation>
    </message>
    <message>
        <source>Confirm before moving an image to the trash</source>
        <translation>将图像移到废纸篓前进行确认</translation>
    </message>
    <message>
        <source>Show a checkerboard behind transparent areas</source>
        <translation>在透明区域后显示棋盘格背景</translation>
    </message>
    <message>
        <source>Window</source>
        <translation>窗口</translation>
    </message>
    <message>
        <source>Show the information panel (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;I&quot;.</extracomment>
        <translation>显示信息面板（%1）</translation>
    </message>
    <message>
        <source>Overlay at the top (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;Shift+I&quot;.</extracomment>
        <translation>顶部叠加层（%1）</translation>
    </message>
    <message>
        <source>Always</source>
        <translation>始终</translation>
    </message>
    <message>
        <source>When the pointer is at the top</source>
        <translation>指针位于顶部时</translation>
    </message>
    <message>
        <source>Never</source>
        <translation>从不</translation>
    </message>
    <message>
        <source>In full screen:</source>
        <translation>全屏时：</translation>
    </message>
    <message>
        <source>In a window:</source>
        <translation>窗口模式下：</translation>
    </message>
    <message>
        <source>Fields</source>
        <translation>字段</translation>
    </message>
    <message>
        <source>Move Up</source>
        <translation>上移</translation>
    </message>
    <message>
        <source>Move Down</source>
        <translation>下移</translation>
    </message>
    <message>
        <source>Fields:</source>
        <translation>字段：</translation>
    </message>
    <message>
        <source> %</source>
        <extracomment>Unit after a percentage; keep the leading space if your language separates it.</extracomment>
        <translation>%</translation>
    </message>
    <message>
        <source>Background opacity:</source>
        <translation>背景不透明度：</translation>
    </message>
    <message>
        <source>Text opacity:</source>
        <translation>文字不透明度：</translation>
    </message>
    <message>
        <source>Outline the text</source>
        <translation>为文字添加轮廓</translation>
    </message>
    <message>
        <source> s</source>
        <extracomment>Unit after a number of seconds; keep the leading space if your language separates units.</extracomment>
        <translation>秒</translation>
    </message>
    <message>
        <source>Fit to the window</source>
        <translation>适合窗口</translation>
    </message>
    <message>
        <source>Fit to the width</source>
        <translation>适合宽度</translation>
    </message>
    <message>
        <source>Fit to the height</source>
        <translation>适合高度</translation>
    </message>
    <message>
        <source>Fill the window</source>
        <translation>填满窗口</translation>
    </message>
    <message>
        <source>Zoom of a new image:</source>
        <translation>新图像的缩放：</translation>
    </message>
    <message>
        <source>Enlarge images smaller than the window</source>
        <translation>放大小于窗口的图像</translation>
    </message>
    <message>
        <source>Keep its size</source>
        <translation>保持大小</translation>
    </message>
    <message>
        <source>Fit it to the first image</source>
        <translation>适合第一张图像</translation>
    </message>
    <message>
        <source>Fit it to every image</source>
        <translation>适合每张图像</translation>
    </message>
    <message>
        <source>Window size:</source>
        <translation>窗口大小：</translation>
    </message>
    <message>
        <source> % of the screen</source>
        <extracomment>Unit after a percentage of the screen&apos;s size; keep the leading space if your language separates it.</extracomment>
        <translation>%的屏幕</translation>
    </message>
    <message>
        <source>At most:</source>
        <translation>最大：</translation>
    </message>
    <message>
        <source>The application&apos;s name</source>
        <translation>应用程序名称</translation>
    </message>
    <message>
        <source>The file name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <source>Name, position and dimensions</source>
        <translation>名称、位置和尺寸</translation>
    </message>
    <message>
        <source>Name, position, dimensions, file size and zoom</source>
        <translation>名称、位置、尺寸、文件大小和缩放</translation>
    </message>
    <message>
        <source>Title bar:</source>
        <translation>标题栏：</translation>
    </message>
    <message>
        <source>Hide after:</source>
        <translation>隐藏延迟：</translation>
    </message>
    <message>
        <source>Appearance of the panel and the overlay</source>
        <translation>面板和叠加层的外观</translation>
    </message>
    <message>
        <source>Information</source>
        <translation>信息</translation>
    </message>
    <message>
        <source>After the last image, continue with the first</source>
        <translation>到达最后一张图像后，从第一张继续</translation>
    </message>
    <message>
        <source>Click the left or right side of the window for the previous or next image</source>
        <translation>单击窗口左侧或右侧切换到上一张或下一张图像</translation>
    </message>
    <message>
        <source> px</source>
        <extracomment>Unit after a number of pixels; keep the leading space if your language separates units.</extracomment>
        <translation> px</translation>
    </message>
    <message>
        <source>Width of each side:</source>
        <translation>每侧宽度：</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>修改日期</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <source>Descending</source>
        <translation>降序</translation>
    </message>
    <message>
        <source>Sort images by:</source>
        <translation>图像排序方式：</translation>
    </message>
    <message>
        <source>Load the next and previous images in advance</source>
        <translation>提前加载上一张和下一张图像</translation>
    </message>
    <message>
        <source>Slideshow (%1), time per image:</source>
        <extracomment>%1: the key that starts and stops the slideshow, e.g. &quot;S&quot;.</extracomment>
        <translation>幻灯片放映（%1），每张图像时间：</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>导航</translation>
    </message>
    <message>
        <source>Automatic (HDR when the display supports it)</source>
        <translation>自动（显示器支持时使用 HDR）</translation>
    </message>
    <message>
        <source>SDR (sRGB)</source>
        <translation>SDR (sRGB)</translation>
    </message>
    <message>
        <source>HDR10 (PQ)</source>
        <translation>HDR10 (PQ)</translation>
    </message>
    <message>
        <source>Display output:</source>
        <translation>显示输出：</translation>
    </message>
    <message>
        <source>Tone map HDR images that exceed the display (ITU-R BT.2390)</source>
        <translation>对超出显示器范围的 HDR 图像进行色调映射（ITU-R BT.2390）</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>色彩 &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Command</source>
        <translation>命令</translation>
    </message>
    <message>
        <source>Shortcuts</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <source>Shortcut:</source>
        <translation>快捷键：</translation>
    </message>
    <message>
        <source>Alternative:</source>
        <translation>备选：</translation>
    </message>
    <message>
        <source>Default for This Command</source>
        <translation>此命令的默认值</translation>
    </message>
    <message>
        <source>Defaults for All Commands</source>
        <translation>所有命令的默认值</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>恢复默认值</translation>
    </message>
    <message>
        <source>Keep the zoom for the next images (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;L&quot;. The zoom of the image shown stays for the next ones.</extracomment>
        <translation>后续图像保持当前缩放 (%1)</translation>
    </message>
    <message>
        <source>Taken from: %1</source>
        <extracomment>%1: names of commands, e.g. &quot;Zoom In&quot;; their shortcut now belongs to the selected command.</extracomment>
        <translation>取自：%1</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>背景颜色</translation>
    </message>
</context>
<context>
    <name>ViewerWindow</name>
    <message>
        <source>File not found: %1</source>
        <translation>找不到文件：%1</translation>
    </message>
    <message>
        <source>The folder contains no supported images.</source>
        <translation>该文件夹中没有受支持的图像。</translation>
    </message>
    <message>
        <source>Loading %1…</source>
        <translation>正在加载 %1…</translation>
    </message>
    <message>
        <source>This is the last image.</source>
        <translation>这是最后一张图像。</translation>
    </message>
    <message>
        <source>This is the first image.</source>
        <translation>这是第一张图像。</translation>
    </message>
    <message>
        <source>Reducing the image to fit the GPU (at most %1 px)…</source>
        <extracomment>%1: a size in pixels.</extracomment>
        <translation>正在缩小图像以适应 GPU（最大 %1 px）…</translation>
    </message>
    <message>
        <source>The GPU did not accept the image.</source>
        <translation>GPU 未接受该图像。</translation>
    </message>
    <message>
        <source>Slideshow stopped</source>
        <translation>幻灯片放映已停止</translation>
    </message>
    <message>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>文件夹</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>文件大小</translation>
    </message>
    <message>
        <source>Modified</source>
        <translation>修改时间</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>位置</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <extracomment>Position of the image in its folder, e.g. &quot;3 of 120&quot;.</extracomment>
        <translation>第 %1 张，共 %2 张</translation>
    </message>
    <message>
        <source>%1 MP</source>
        <extracomment>Megapixels, e.g. &quot;24.0 MP&quot;.</extracomment>
        <translation>%1 MP</translation>
    </message>
    <message>
        <source>reduced to %1 × %2 for the GPU</source>
        <translation>已缩小至 %1 × %2 以适应 GPU</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>尺寸</translation>
    </message>
    <message>
        <source>floating point</source>
        <translation>浮点</translation>
    </message>
    <message>
        <source>alpha</source>
        <translation>Alpha 通道</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>格式</translation>
    </message>
    <message>
        <source>frame %1 of %2</source>
        <extracomment>The frame of an animation on screen, e.g. &quot;frame 3 of 24&quot;.</extracomment>
        <translation>第 %1 帧，共 %2 帧</translation>
    </message>
    <message>
        <source>frame %1</source>
        <translation>第 %1 帧</translation>
    </message>
    <message>
        <source>paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <source>Animation</source>
        <translation>动画</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>方向</translation>
    </message>
    <message>
        <source>EXIF %1, applied</source>
        <extracomment>The EXIF orientation tag (2 to 8) of the file, already applied to the image.</extracomment>
        <translation>EXIF %1，已应用</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>色彩</translation>
    </message>
    <message>
        <source>Peak</source>
        <translation>峰值</translation>
    </message>
    <message>
        <source>%1× SDR white (≈%2 nits)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1× SDR 参考白（≈%2 nits）</translation>
    </message>
    <message>
        <source>Decoded in</source>
        <translation>解码耗时</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <extracomment>Unit after a duration in milliseconds.</extracomment>
        <translation>%1 ms</translation>
    </message>
    <message>
        <source>Camera</source>
        <translation>相机</translation>
    </message>
    <message>
        <source>Lens</source>
        <translation>镜头</translation>
    </message>
    <message>
        <source>%1 s</source>
        <extracomment>Exposure time of a photograph, e.g. &quot;1/250 s&quot;.</extracomment>
        <translation>%1 s</translation>
    </message>
    <message>
        <source>%1 mm</source>
        <extracomment>Focal length of the lens, e.g. &quot;50 mm&quot;.</extracomment>
        <translation>%1 mm</translation>
    </message>
    <message>
        <source>Exposure</source>
        <extracomment>Label of the photograph&apos;s shooting settings: exposure time, aperture, ISO, focal length.</extracomment>
        <translation>曝光</translation>
    </message>
    <message>
        <source>Taken</source>
        <extracomment>Label of the date the photograph was taken.</extracomment>
        <translation>拍摄时间</translation>
    </message>
    <message>
        <source>%1 %</source>
        <extracomment>A zoom percentage, e.g. &quot;100 %&quot;; write the percent sign as your language does.</extracomment>
        <translation>%1%</translation>
    </message>
    <message>
        <source>rotated %1°</source>
        <extracomment>The view is rotated clockwise by this many degrees.</extracomment>
        <translation>旋转 %1°</translation>
    </message>
    <message>
        <source>mirrored</source>
        <translation>已镜像</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>BT.2390 色调映射，从 %1 nits 映射到 %2 nits，%3 nits 以下保持不变</translation>
    </message>
    <message>
        <source>clipped above %1 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>高于 %1 nits 的部分被裁剪</translation>
    </message>
    <message>
        <source>Highlights</source>
        <translation>高光</translation>
    </message>
    <message>
        <source>clipped above %1 nits (colors outside the output gamut)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>高于 %1 nits 的部分被裁剪（色彩超出输出色域）</translation>
    </message>
    <message>
        <source>clipped above %1 nits (tone mapping off)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>高于 %1 nits 的部分被裁剪（色调映射已关闭）</translation>
    </message>
    <message>
        <source>exposure %1 EV</source>
        <extracomment>The viewer&apos;s exposure adjustment in EV (photographic stops), e.g. &quot;exposure +1.5 EV&quot;.</extracomment>
        <translation>曝光 %1 EV</translation>
    </message>
    <message>
        <source>altered pixels highlighted</source>
        <translation>已突出显示被改变的像素</translation>
    </message>
    <message>
        <source>%1-bit</source>
        <extracomment>Bits per channel of the image file, e.g. &quot;16-bit&quot;.</extracomment>
        <translation>%1 位</translation>
    </message>
    <message>
        <source>Enter a name.</source>
        <translation>请输入名称。</translation>
    </message>
    <message>
        <source>This name is not allowed.</source>
        <translation>不允许使用此名称。</translation>
    </message>
    <message>
        <source>A name cannot contain “/” or “\”.</source>
        <translation>名称不能包含“/”或“\”。</translation>
    </message>
    <message>
        <source>The name is too long.</source>
        <translation>名称太长。</translation>
    </message>
    <message>
        <source>Windows does not allow this name.</source>
        <extracomment>Windows forbids &lt; &gt; : &quot; | ? *, control characters, device names such as CON, and a final dot or space.</extracomment>
        <translation>Windows 不允许使用此名称。</translation>
    </message>
    <message>
        <source>A file with this name already exists.</source>
        <translation>已存在同名文件。</translation>
    </message>
    <message>
        <source>Open…</source>
        <translation>打开…</translation>
    </message>
    <message>
        <source>Clear Menu</source>
        <extracomment>Empties the Open Recent menu.</extracomment>
        <translation>清除菜单</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>在文件资源管理器中显示</translation>
    </message>
    <message>
        <source>Move to Recycle Bin…</source>
        <translation>移到回收站…</translation>
    </message>
    <message>
        <source>Move to Recycle Bin</source>
        <translation>移到回收站</translation>
    </message>
    <message>
        <source>Undo Move to Recycle Bin</source>
        <translation>撤销移到回收站</translation>
    </message>
    <message>
        <source>Show in Finder</source>
        <translation>在 Finder 中显示</translation>
    </message>
    <message>
        <source>Move to Trash…</source>
        <translation>移到废纸篓…</translation>
    </message>
    <message>
        <source>Move to Trash</source>
        <translation>移到废纸篓</translation>
    </message>
    <message>
        <source>Undo Move to Trash</source>
        <translation>撤销移到废纸篓</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>在文件管理器中显示</translation>
    </message>
    <message>
        <source>Other Application…</source>
        <translation>其他应用程序…</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>重命名…</translation>
    </message>
    <message>
        <source>Delete Permanently…</source>
        <translation>永久删除…</translation>
    </message>
    <message>
        <source>Copy Image</source>
        <translation>复制图像</translation>
    </message>
    <message>
        <source>Copy File Path</source>
        <translation>复制文件路径</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>设置…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <source>Previous Image</source>
        <translation>上一张图像</translation>
    </message>
    <message>
        <source>Next Image</source>
        <translation>下一张图像</translation>
    </message>
    <message>
        <source>First Image</source>
        <translation>第一张图像</translation>
    </message>
    <message>
        <source>Last Image</source>
        <translation>最后一张图像</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>适合窗口</translation>
    </message>
    <message>
        <source>Fit to Width</source>
        <translation>适合宽度</translation>
    </message>
    <message>
        <source>Fit to Height</source>
        <translation>适合高度</translation>
    </message>
    <message>
        <source>Fill Window</source>
        <translation>填满窗口</translation>
    </message>
    <message>
        <source>Lock Zoom</source>
        <translation>锁定缩放</translation>
    </message>
    <message>
        <source>Actual Size (100 %)</source>
        <extracomment>&quot;100 %&quot; is a zoom percentage; write the percent sign as your language does.</extracomment>
        <translation>实际大小（100%）</translation>
    </message>
    <message>
        <source>Full Screen</source>
        <translation>全屏</translation>
    </message>
    <message>
        <source>Information Panel</source>
        <translation>信息面板</translation>
    </message>
    <message>
        <source>Information Overlay</source>
        <translation>信息叠加层</translation>
    </message>
    <message>
        <source>Checkerboard Background</source>
        <translation>棋盘格背景</translation>
    </message>
    <message>
        <source>Rotate Clockwise</source>
        <translation>顺时针旋转</translation>
    </message>
    <message>
        <source>Rotate Counterclockwise</source>
        <translation>逆时针旋转</translation>
    </message>
    <message>
        <source>Flip Horizontally</source>
        <translation>水平翻转</translation>
    </message>
    <message>
        <source>Flip Vertically</source>
        <translation>垂直翻转</translation>
    </message>
    <message>
        <source>Increase Exposure (+½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>增加曝光（+½ EV）</translation>
    </message>
    <message>
        <source>Decrease Exposure (−½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>降低曝光（−½ EV）</translation>
    </message>
    <message>
        <source>Reset Exposure</source>
        <translation>重置曝光</translation>
    </message>
    <message>
        <source>Tone Mapping (BT.2390)</source>
        <translation>色调映射（BT.2390）</translation>
    </message>
    <message>
        <source>Highlight Altered Pixels</source>
        <translation>突出显示被改变的像素</translation>
    </message>
    <message>
        <source>Pause Animation</source>
        <translation>暂停动画</translation>
    </message>
    <message>
        <source>Previous Frame</source>
        <translation>上一帧</translation>
    </message>
    <message>
        <source>Next Frame</source>
        <translation>下一帧</translation>
    </message>
    <message>
        <source>Slideshow</source>
        <translation>幻灯片放映</translation>
    </message>
    <message>
        <source>About imageViewer</source>
        <translation>关于 imageViewer</translation>
    </message>
    <message>
        <source>About Qt</source>
        <translation>关于 Qt</translation>
    </message>
    <message>
        <source>Open Recent</source>
        <translation>打开最近使用的文件</translation>
    </message>
    <message>
        <source>Open With</source>
        <translation>打开方式</translation>
    </message>
    <message>
        <source>Cannot start “%1”.</source>
        <translation>无法启动“%1”。</translation>
    </message>
    <message>
        <source>No applications found</source>
        <translation>未找到应用程序</translation>
    </message>
    <message>
        <source>Zoom locked: the next images keep it</source>
        <translation>缩放已锁定：后续图像保持此缩放</translation>
    </message>
    <message>
        <source>Zoom unlocked</source>
        <translation>缩放已解锁</translation>
    </message>
    <message>
        <source>Delete “%1” permanently?</source>
        <translation>要永久删除“%1”吗？</translation>
    </message>
    <message>
        <source>The file does not go to the trash and cannot be restored.</source>
        <translation>该文件不会移到废纸篓，且无法恢复。</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <source>Cannot delete “%1”.</source>
        <translation>无法删除“%1”。</translation>
    </message>
    <message>
        <source>Deleted “%1”</source>
        <translation>已删除“%1”</translation>
    </message>
    <message>
        <source>“%1” is no longer in the trash.</source>
        <translation>“%1”已不在废纸篓中。</translation>
    </message>
    <message>
        <source>Cannot restore “%1”: a file with that name exists.</source>
        <translation>无法还原“%1”：已存在同名文件。</translation>
    </message>
    <message>
        <source>Cannot restore “%1”.</source>
        <translation>无法还原“%1”。</translation>
    </message>
    <message>
        <source>Restored “%1”</source>
        <translation>已还原“%1”</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation>重命名</translation>
    </message>
    <message>
        <source>New name</source>
        <translation>新名称</translation>
    </message>
    <message>
        <source>New name:</source>
        <translation>新名称：</translation>
    </message>
    <message>
        <source>Cannot rename “%1”.</source>
        <translation>无法重命名“%1”。</translation>
    </message>
    <message>
        <source>Renamed to “%1”</source>
        <translation>已重命名为“%1”</translation>
    </message>
    <message>
        <source>View</source>
        <translation>视图</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>图像</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>色彩 &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Go</source>
        <translation>前往</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Images (%1);;All files (*)</source>
        <extracomment>File dialog filters: keep &quot;%1&quot;, &quot;;;&quot; and &quot;(*)&quot; exactly.</extracomment>
        <translation>图像 (%1);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>打开图像</translation>
    </message>
    <message>
        <source>Copying the image…</source>
        <translation>正在复制图像…</translation>
    </message>
    <message>
        <source>Cannot copy the image.</source>
        <translation>无法复制图像。</translation>
    </message>
    <message>
        <source>Image copied to the clipboard</source>
        <translation>图像已复制到剪贴板</translation>
    </message>
    <message>
        <source>File path copied to the clipboard</source>
        <translation>文件路径已复制到剪贴板</translation>
    </message>
    <message>
        <source>Move “%1” to the Recycle Bin?</source>
        <translation>要将“%1”移到回收站吗？</translation>
    </message>
    <message>
        <source>Move “%1” to the trash?</source>
        <translation>要将“%1”移到废纸篓吗？</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Do not ask again</source>
        <translation>不再询问</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the Recycle Bin.</source>
        <translation>无法将“%1”移到回收站。</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the trash.</source>
        <translation>无法将“%1”移到废纸篓。</translation>
    </message>
    <message>
        <source>Moved “%1” to the Recycle Bin</source>
        <translation>已将“%1”移到回收站</translation>
    </message>
    <message>
        <source>Moved “%1” to the trash</source>
        <translation>已将“%1”移到废纸篓</translation>
    </message>
    <message>
        <source>No images left in this folder.</source>
        <translation>此文件夹中已没有图像。</translation>
    </message>
    <message>
        <source>Image viewer with verifiable SDR and HDR color fidelity.</source>
        <translation>具有可验证的 SDR 与 HDR 色彩保真度的图像查看器。</translation>
    </message>
    <message>
        <source>Licensed under the Apache License, Version 2.0.</source>
        <translation>根据 Apache License, Version 2.0 授权。</translation>
    </message>
    <message>
        <source>The licenses of the third-party components are in the &lt;i&gt;third-party&lt;/i&gt; folder installed with the application.</source>
        <translation>第三方组件的许可证位于随应用程序一同安装的 &lt;i&gt;third-party&lt;/i&gt; 文件夹中。</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Animation paused</source>
        <translation>动画已暂停</translation>
    </message>
    <message>
        <source>Animation playing</source>
        <translation>动画播放中</translation>
    </message>
    <message>
        <source>Slideshow: a new image every %1 s</source>
        <extracomment>%1: seconds between images, e.g. &quot;5&quot;.</extracomment>
        <translation>幻灯片放映：每 %1 秒一张新图像</translation>
    </message>
</context>
</TS>
