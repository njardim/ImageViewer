<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ja" sourcelanguage="en">
<context>
    <name>Color</name>
    <message>
        <source>invalid ICC profile</source>
        <translation>無効な ICC プロファイル</translation>
    </message>
    <message>
        <source>unsupported ICC color space for RGBA data</source>
        <translation>RGBA データでサポートされていない ICC カラースペース</translation>
    </message>
    <message>
        <source>cannot build a color transform from the ICC profile</source>
        <translation>ICC プロファイルからカラー変換を作成できません</translation>
    </message>
</context>
<context>
    <name>Image</name>
    <message>
        <source>image too large for the available memory (needs %1 GB, limit %2 GB)</source>
        <extracomment>GB: gigabytes.</extracomment>
        <translation>画像が大きすぎて使用可能なメモリに収まりません（必要量 %1 GB、上限 %2 GB）</translation>
    </message>
    <message>
        <source>(no description)</source>
        <translation>（説明なし）</translation>
    </message>
    <message>
        <source>linear, chromaticities from the file</source>
        <translation>リニア、色度座標はファイルから取得</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed: the file&apos;s chromaticities are invalid)</source>
        <translation>リニア BT.709（仮定：ファイルの色度座標が無効）</translation>
    </message>
    <message>
        <source>%1 (assigned by the decoder)</source>
        <translation>%1（デコーダーによる割り当て）</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed)</source>
        <translation>リニア BT.709（仮定）</translation>
    </message>
    <message>
        <source>sRGB (assumed)</source>
        <translation>sRGB（仮定）</translation>
    </message>
    <message>
        <source>invalid dimensions (%1×%2×%3)</source>
        <translation>無効なサイズ（%1×%2×%3）</translation>
    </message>
    <message>
        <source>%1 (file metadata, via Qt)</source>
        <translation>%1（ファイルのメタデータ、Qt 経由）</translation>
    </message>
    <message>
        <source>SVG is only decoded in the graphical interface</source>
        <translation>SVG はグラフィカルインターフェイスでのみデコードされます</translation>
    </message>
    <message>
        <source>damaged, truncated or unsupported %1 file</source>
        <translation>破損・不完全、または未対応の %1 ファイル</translation>
    </message>
    <message>
        <source>custom primaries</source>
        <translation>カスタム原色</translation>
    </message>
    <message>
        <source>Cannot decode: %1</source>
        <translation>デコードできません：%1</translation>
    </message>
    <message>
        <source>%1 (assumed: %2)</source>
        <translation>%1（仮定：%2）</translation>
    </message>
    <message>
        <source>Not enough memory to decode the image.</source>
        <translation>画像をデコードするためのメモリが不足しています。</translation>
    </message>
    <message>
        <source>Decoding error: %1</source>
        <translation>デコードエラー：%1</translation>
    </message>
    <message>
        <source>HEIC images need Microsoft&apos;s “HEIF Image Extensions” and “HEVC Video Extensions”, from the Microsoft Store.</source>
        <translation>HEIC 画像には、Microsoft Store の Microsoft「HEIF Image Extensions」と「HEVC Video Extensions」が必要です。</translation>
    </message>
    <message>
        <source>HEIC images need an HEVC decoder, which imageViewer does not include on this system (patents); convert them to another format first.</source>
        <translation>HEIC 画像には HEVC デコーダーが必要ですが、このシステムでは imageViewer に含まれていません（特許のため）。先に別の形式に変換してください。</translation>
    </message>
    <message>
        <source>the decoder took longer than %1 s and was stopped</source>
        <extracomment>%1: seconds, e.g. &quot;30&quot;.</extracomment>
        <translation>デコーダーが %1 秒を超えたため停止しました</translation>
    </message>
</context>
<context>
    <name>OpenWith</name>
    <message>
        <source>Choose an Application</source>
        <translation>アプリケーションを選択</translation>
    </message>
    <message>
        <source>Applications (*.app)</source>
        <translation>アプリケーション (*.app)</translation>
    </message>
</context>
<context>
    <name>Overlay</name>
    <message>
        <source>File name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>画像サイズ</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>ファイルサイズ</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>ズーム</translation>
    </message>
    <message>
        <source>Color space</source>
        <translation>カラースペース</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>更新日時</translation>
    </message>
    <message>
        <source>Position in the folder</source>
        <translation>フォルダー内の位置</translation>
    </message>
    <message>
        <source>Display output</source>
        <translation>ディスプレイ出力</translation>
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
        <translation>EDR · ヘッドルーム %1×</translation>
    </message>
    <message>
        <source>Linear sRGB managed by ColorSync · no HDR headroom</source>
        <translation>ColorSync が管理するリニア sRGB · HDR ヘッドルームなし</translation>
    </message>
    <message>
        <source>scRGB · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>scRGB · SDR 基準白 %1 nits · ピーク %2 nits</translation>
    </message>
    <message>
        <source>HDR10 (PQ) · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>HDR10 (PQ) · SDR 基準白 %1 nits · ピーク %2 nits</translation>
    </message>
    <message>
        <source>Cannot initialize the GPU (QRhi).</source>
        <translation>GPU（QRhi）を初期化できません。</translation>
    </message>
    <message>
        <source>The GPU does not support RGBA16F textures.</source>
        <translation>GPU が RGBA16F テクスチャに対応していません。</translation>
    </message>
    <message>
        <source>Cannot create GPU resources.</source>
        <translation>GPU リソースを作成できません。</translation>
    </message>
    <message>
        <source>Cannot create the swapchain.</source>
        <translation>スワップチェーンを作成できません。</translation>
    </message>
    <message>
        <source>(Qt defaults, not measured)</source>
        <translation>（Qt の既定値、未測定）</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>設定</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>システムの既定</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>言語：</translation>
    </message>
    <message>
        <source>Languages other than English are machine translations awaiting review by native speakers.</source>
        <translation>英語以外の言語は機械翻訳であり、ネイティブスピーカーによるレビューを待っています。</translation>
    </message>
    <message>
        <source>Reopen the last image at startup</source>
        <translation>起動時に前回の画像を開く</translation>
    </message>
    <message>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <source>Black</source>
        <translation>黒</translation>
    </message>
    <message>
        <source>Dark gray</source>
        <translation>ダークグレー</translation>
    </message>
    <message>
        <source>Gray</source>
        <translation>グレー</translation>
    </message>
    <message>
        <source>Light gray</source>
        <translation>ライトグレー</translation>
    </message>
    <message>
        <source>White</source>
        <translation>白</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>カスタム…</translation>
    </message>
    <message>
        <source>Background:</source>
        <translation>背景：</translation>
    </message>
    <message>
        <source>Remember the window size and position</source>
        <translation>ウィンドウのサイズと位置を記憶する</translation>
    </message>
    <message>
        <source>Confirm before moving an image to the trash</source>
        <translation>画像をゴミ箱に入れる前に確認する</translation>
    </message>
    <message>
        <source>Show a checkerboard behind transparent areas</source>
        <translation>透明な部分に市松模様を表示する</translation>
    </message>
    <message>
        <source>Window</source>
        <translation>ウィンドウ</translation>
    </message>
    <message>
        <source>Show the information panel (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;I&quot;.</extracomment>
        <translation>情報パネルを表示（%1）</translation>
    </message>
    <message>
        <source>Overlay at the top (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;Shift+I&quot;.</extracomment>
        <translation>上部のオーバーレイ（%1）</translation>
    </message>
    <message>
        <source>Always</source>
        <translation>常に表示</translation>
    </message>
    <message>
        <source>When the pointer is at the top</source>
        <translation>ポインターが上部にあるとき</translation>
    </message>
    <message>
        <source>Never</source>
        <translation>表示しない</translation>
    </message>
    <message>
        <source>In full screen:</source>
        <translation>全画面表示時：</translation>
    </message>
    <message>
        <source>In a window:</source>
        <translation>ウィンドウ表示時：</translation>
    </message>
    <message>
        <source>Fields</source>
        <translation>項目</translation>
    </message>
    <message>
        <source>Move Up</source>
        <translation>上へ移動</translation>
    </message>
    <message>
        <source>Move Down</source>
        <translation>下へ移動</translation>
    </message>
    <message>
        <source>Fields:</source>
        <translation>項目：</translation>
    </message>
    <message>
        <source> %</source>
        <extracomment>Unit after a percentage; keep the leading space if your language separates it.</extracomment>
        <translation>%</translation>
    </message>
    <message>
        <source>Background opacity:</source>
        <translation>背景の不透明度：</translation>
    </message>
    <message>
        <source>Text opacity:</source>
        <translation>文字の不透明度：</translation>
    </message>
    <message>
        <source>Outline the text</source>
        <translation>文字を縁取りする</translation>
    </message>
    <message>
        <source> s</source>
        <extracomment>Unit after a number of seconds; keep the leading space if your language separates units.</extracomment>
        <translation>秒</translation>
    </message>
    <message>
        <source>Fit to the window</source>
        <translation>ウィンドウに合わせる</translation>
    </message>
    <message>
        <source>Fit to the width</source>
        <translation>幅に合わせる</translation>
    </message>
    <message>
        <source>Fit to the height</source>
        <translation>高さに合わせる</translation>
    </message>
    <message>
        <source>Fill the window</source>
        <translation>ウィンドウいっぱいに表示</translation>
    </message>
    <message>
        <source>Zoom of a new image:</source>
        <translation>新しい画像のズーム：</translation>
    </message>
    <message>
        <source>Enlarge images smaller than the window</source>
        <translation>ウィンドウより小さい画像を拡大する</translation>
    </message>
    <message>
        <source>Keep its size</source>
        <translation>サイズを維持</translation>
    </message>
    <message>
        <source>Fit it to the first image</source>
        <translation>最初の画像に合わせる</translation>
    </message>
    <message>
        <source>Fit it to every image</source>
        <translation>画像ごとに合わせる</translation>
    </message>
    <message>
        <source>Window size:</source>
        <translation>ウィンドウサイズ：</translation>
    </message>
    <message>
        <source> % of the screen</source>
        <extracomment>Unit after a percentage of the screen&apos;s size; keep the leading space if your language separates it.</extracomment>
        <translation>%（画面比）</translation>
    </message>
    <message>
        <source>At most:</source>
        <translation>最大：</translation>
    </message>
    <message>
        <source>The application&apos;s name</source>
        <translation>アプリケーション名</translation>
    </message>
    <message>
        <source>The file name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <source>Name, position and dimensions</source>
        <translation>名前、位置、画像サイズ</translation>
    </message>
    <message>
        <source>Name, position, dimensions, file size and zoom</source>
        <translation>名前、位置、画像サイズ、ファイルサイズ、ズーム</translation>
    </message>
    <message>
        <source>Title bar:</source>
        <translation>タイトルバー：</translation>
    </message>
    <message>
        <source>Hide after:</source>
        <translation>非表示にするまでの時間：</translation>
    </message>
    <message>
        <source>Appearance of the panel and the overlay</source>
        <translation>パネルとオーバーレイの外観</translation>
    </message>
    <message>
        <source>Information</source>
        <translation>情報</translation>
    </message>
    <message>
        <source>After the last image, continue with the first</source>
        <translation>最後の画像の次は最初の画像に戻る</translation>
    </message>
    <message>
        <source>Click the left or right side of the window for the previous or next image</source>
        <translation>ウィンドウの左側または右側をクリックして前または次の画像を表示する</translation>
    </message>
    <message>
        <source> px</source>
        <extracomment>Unit after a number of pixels; keep the leading space if your language separates units.</extracomment>
        <translation> px</translation>
    </message>
    <message>
        <source>Width of each side:</source>
        <translation>左右それぞれの幅：</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>更新日時</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <source>Descending</source>
        <translation>降順</translation>
    </message>
    <message>
        <source>Sort images by:</source>
        <translation>画像の並べ替え：</translation>
    </message>
    <message>
        <source>Load the next and previous images in advance</source>
        <translation>前後の画像を先読みする</translation>
    </message>
    <message>
        <source>Slideshow (%1), time per image:</source>
        <extracomment>%1: the key that starts and stops the slideshow, e.g. &quot;S&quot;.</extracomment>
        <translation>スライドショー（%1）、1枚あたりの時間：</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>ナビゲーション</translation>
    </message>
    <message>
        <source>Automatic (HDR when the display supports it)</source>
        <translation>自動（ディスプレイが対応している場合は HDR）</translation>
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
        <translation>ディスプレイ出力：</translation>
    </message>
    <message>
        <source>Tone map HDR images that exceed the display (ITU-R BT.2390)</source>
        <translation>ディスプレイの表示範囲を超える HDR 画像をトーンマッピングする（ITU-R BT.2390）</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>カラー &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Command</source>
        <translation>コマンド</translation>
    </message>
    <message>
        <source>Shortcuts</source>
        <translation>ショートカット</translation>
    </message>
    <message>
        <source>Shortcut:</source>
        <translation>ショートカット：</translation>
    </message>
    <message>
        <source>Alternative:</source>
        <translation>代替：</translation>
    </message>
    <message>
        <source>Default for This Command</source>
        <translation>このコマンドの既定値</translation>
    </message>
    <message>
        <source>Defaults for All Commands</source>
        <translation>すべてのコマンドの既定値</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>適用</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>既定値に戻す</translation>
    </message>
    <message>
        <source>Keep the zoom for the next images (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;L&quot;. The zoom of the image shown stays for the next ones.</extracomment>
        <translation>次の画像でもズームを維持する (%1)</translation>
    </message>
    <message>
        <source>Taken from: %1</source>
        <extracomment>%1: names of commands, e.g. &quot;Zoom In&quot;; their shortcut now belongs to the selected command.</extracomment>
        <translation>割り当て元：%1</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>背景色</translation>
    </message>
</context>
<context>
    <name>ViewerWindow</name>
    <message>
        <source>File not found: %1</source>
        <translation>ファイルが見つかりません：%1</translation>
    </message>
    <message>
        <source>The folder contains no supported images.</source>
        <translation>このフォルダーには対応している画像がありません。</translation>
    </message>
    <message>
        <source>Loading %1…</source>
        <translation>%1 を読み込み中…</translation>
    </message>
    <message>
        <source>This is the last image.</source>
        <translation>これが最後の画像です。</translation>
    </message>
    <message>
        <source>This is the first image.</source>
        <translation>これが最初の画像です。</translation>
    </message>
    <message>
        <source>Reducing the image to fit the GPU (at most %1 px)…</source>
        <extracomment>%1: a size in pixels.</extracomment>
        <translation>GPU に収まるように画像を縮小しています（最大 %1 px）…</translation>
    </message>
    <message>
        <source>The GPU did not accept the image.</source>
        <translation>GPU が画像を受け付けませんでした。</translation>
    </message>
    <message>
        <source>Slideshow stopped</source>
        <translation>スライドショーを停止しました</translation>
    </message>
    <message>
        <source>File</source>
        <translation>ファイル</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>フォルダー</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>ファイルサイズ</translation>
    </message>
    <message>
        <source>Modified</source>
        <translation>更新日時</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>位置</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <extracomment>Position of the image in its folder, e.g. &quot;3 of 120&quot;.</extracomment>
        <translation>%2 枚中 %1 枚目</translation>
    </message>
    <message>
        <source>%1 MP</source>
        <extracomment>Megapixels, e.g. &quot;24.0 MP&quot;.</extracomment>
        <translation>%1 MP</translation>
    </message>
    <message>
        <source>reduced to %1 × %2 for the GPU</source>
        <translation>GPU 向けに %1 × %2 に縮小</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>画像サイズ</translation>
    </message>
    <message>
        <source>floating point</source>
        <translation>浮動小数点</translation>
    </message>
    <message>
        <source>alpha</source>
        <translation>アルファ</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>形式</translation>
    </message>
    <message>
        <source>frame %1 of %2</source>
        <extracomment>The frame of an animation on screen, e.g. &quot;frame 3 of 24&quot;.</extracomment>
        <translation>フレーム %1 / %2</translation>
    </message>
    <message>
        <source>frame %1</source>
        <translation>フレーム %1</translation>
    </message>
    <message>
        <source>paused</source>
        <translation>一時停止中</translation>
    </message>
    <message>
        <source>Animation</source>
        <translation>アニメーション</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>向き</translation>
    </message>
    <message>
        <source>EXIF %1, applied</source>
        <extracomment>The EXIF orientation tag (2 to 8) of the file, already applied to the image.</extracomment>
        <translation>EXIF %1、適用済み</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>カラー</translation>
    </message>
    <message>
        <source>Peak</source>
        <translation>ピーク</translation>
    </message>
    <message>
        <source>%1× SDR white (≈%2 nits)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1× SDR 基準白（≈%2 nits）</translation>
    </message>
    <message>
        <source>Decoded in</source>
        <translation>デコード時間</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <extracomment>Unit after a duration in milliseconds.</extracomment>
        <translation>%1 ms</translation>
    </message>
    <message>
        <source>Camera</source>
        <translation>カメラ</translation>
    </message>
    <message>
        <source>Lens</source>
        <translation>レンズ</translation>
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
        <translation>露出</translation>
    </message>
    <message>
        <source>Taken</source>
        <extracomment>Label of the date the photograph was taken.</extracomment>
        <translation>撮影日時</translation>
    </message>
    <message>
        <source>%1 %</source>
        <extracomment>A zoom percentage, e.g. &quot;100 %&quot;; write the percent sign as your language does.</extracomment>
        <translation>%1%</translation>
    </message>
    <message>
        <source>rotated %1°</source>
        <extracomment>The view is rotated clockwise by this many degrees.</extracomment>
        <translation>%1° 回転</translation>
    </message>
    <message>
        <source>mirrored</source>
        <translation>ミラー反転</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>出力</translation>
    </message>
    <message>
        <source>BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>BT.2390 トーンマッピング：%1 nits から %2 nits へ、%3 nits までは変更なし</translation>
    </message>
    <message>
        <source>clipped above %1 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 nits を超える部分をクリップ</translation>
    </message>
    <message>
        <source>Highlights</source>
        <translation>ハイライト</translation>
    </message>
    <message>
        <source>clipped above %1 nits (colors outside the output gamut)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 nits を超える部分をクリップ（出力色域外の色）</translation>
    </message>
    <message>
        <source>clipped above %1 nits (tone mapping off)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 nits を超える部分をクリップ（トーンマッピング無効）</translation>
    </message>
    <message>
        <source>exposure %1 EV</source>
        <extracomment>The viewer&apos;s exposure adjustment in EV (photographic stops), e.g. &quot;exposure +1.5 EV&quot;.</extracomment>
        <translation>露出 %1 EV</translation>
    </message>
    <message>
        <source>altered pixels highlighted</source>
        <translation>変更されたピクセルを強調表示中</translation>
    </message>
    <message>
        <source>%1-bit</source>
        <extracomment>Bits per channel of the image file, e.g. &quot;16-bit&quot;.</extracomment>
        <translation>%1 ビット</translation>
    </message>
    <message>
        <source>Enter a name.</source>
        <translation>名前を入力してください。</translation>
    </message>
    <message>
        <source>This name is not allowed.</source>
        <translation>この名前は使用できません。</translation>
    </message>
    <message>
        <source>A name cannot contain “/” or “\”.</source>
        <translation>名前に「/」や「\」は使用できません。</translation>
    </message>
    <message>
        <source>The name is too long.</source>
        <translation>名前が長すぎます。</translation>
    </message>
    <message>
        <source>Windows does not allow this name.</source>
        <extracomment>Windows forbids &lt; &gt; : &quot; | ? *, control characters, device names such as CON, and a final dot or space.</extracomment>
        <translation>Windows ではこの名前は使用できません。</translation>
    </message>
    <message>
        <source>A file with this name already exists.</source>
        <translation>この名前のファイルは既に存在します。</translation>
    </message>
    <message>
        <source>Open…</source>
        <translation>開く…</translation>
    </message>
    <message>
        <source>Clear Menu</source>
        <extracomment>Empties the Open Recent menu.</extracomment>
        <translation>メニューを消去</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>エクスプローラーで表示</translation>
    </message>
    <message>
        <source>Move to Recycle Bin…</source>
        <translation>ごみ箱に移動…</translation>
    </message>
    <message>
        <source>Move to Recycle Bin</source>
        <translation>ごみ箱に移動</translation>
    </message>
    <message>
        <source>Undo Move to Recycle Bin</source>
        <translation>ごみ箱への移動を取り消す</translation>
    </message>
    <message>
        <source>Show in Finder</source>
        <translation>Finder で表示</translation>
    </message>
    <message>
        <source>Move to Trash…</source>
        <translation>ゴミ箱に入れる…</translation>
    </message>
    <message>
        <source>Move to Trash</source>
        <translation>ゴミ箱に入れる</translation>
    </message>
    <message>
        <source>Undo Move to Trash</source>
        <translation>ゴミ箱への移動を取り消す</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>ファイルマネージャーで表示</translation>
    </message>
    <message>
        <source>Other Application…</source>
        <translation>他のアプリケーション…</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>名前を変更…</translation>
    </message>
    <message>
        <source>Delete Permanently…</source>
        <translation>完全に削除…</translation>
    </message>
    <message>
        <source>Copy Image</source>
        <translation>画像をコピー</translation>
    </message>
    <message>
        <source>Copy File Path</source>
        <translation>ファイルパスをコピー</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>設定…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>終了</translation>
    </message>
    <message>
        <source>Previous Image</source>
        <translation>前の画像</translation>
    </message>
    <message>
        <source>Next Image</source>
        <translation>次の画像</translation>
    </message>
    <message>
        <source>First Image</source>
        <translation>最初の画像</translation>
    </message>
    <message>
        <source>Last Image</source>
        <translation>最後の画像</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>拡大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>縮小</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>ウィンドウに合わせる</translation>
    </message>
    <message>
        <source>Fit to Width</source>
        <translation>幅に合わせる</translation>
    </message>
    <message>
        <source>Fit to Height</source>
        <translation>高さに合わせる</translation>
    </message>
    <message>
        <source>Fill Window</source>
        <translation>ウィンドウいっぱいに表示</translation>
    </message>
    <message>
        <source>Lock Zoom</source>
        <translation>ズームを固定</translation>
    </message>
    <message>
        <source>Actual Size (100 %)</source>
        <extracomment>&quot;100 %&quot; is a zoom percentage; write the percent sign as your language does.</extracomment>
        <translation>実際のサイズ（100%）</translation>
    </message>
    <message>
        <source>Full Screen</source>
        <translation>全画面表示</translation>
    </message>
    <message>
        <source>Information Panel</source>
        <translation>情報パネル</translation>
    </message>
    <message>
        <source>Information Overlay</source>
        <translation>情報オーバーレイ</translation>
    </message>
    <message>
        <source>Checkerboard Background</source>
        <translation>市松模様の背景</translation>
    </message>
    <message>
        <source>Rotate Clockwise</source>
        <translation>時計回りに回転</translation>
    </message>
    <message>
        <source>Rotate Counterclockwise</source>
        <translation>反時計回りに回転</translation>
    </message>
    <message>
        <source>Flip Horizontally</source>
        <translation>左右反転</translation>
    </message>
    <message>
        <source>Flip Vertically</source>
        <translation>上下反転</translation>
    </message>
    <message>
        <source>Increase Exposure (+½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>露出を上げる（+½ EV）</translation>
    </message>
    <message>
        <source>Decrease Exposure (−½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>露出を下げる（−½ EV）</translation>
    </message>
    <message>
        <source>Reset Exposure</source>
        <translation>露出をリセット</translation>
    </message>
    <message>
        <source>Tone Mapping (BT.2390)</source>
        <translation>トーンマッピング（BT.2390）</translation>
    </message>
    <message>
        <source>Highlight Altered Pixels</source>
        <translation>変更されたピクセルを強調表示</translation>
    </message>
    <message>
        <source>Pause Animation</source>
        <translation>アニメーションを一時停止</translation>
    </message>
    <message>
        <source>Previous Frame</source>
        <translation>前のフレーム</translation>
    </message>
    <message>
        <source>Next Frame</source>
        <translation>次のフレーム</translation>
    </message>
    <message>
        <source>Slideshow</source>
        <translation>スライドショー</translation>
    </message>
    <message>
        <source>About imageViewer</source>
        <translation>imageViewer について</translation>
    </message>
    <message>
        <source>About Qt</source>
        <translation>Qt について</translation>
    </message>
    <message>
        <source>Open Recent</source>
        <translation>最近開いたファイル</translation>
    </message>
    <message>
        <source>Open With</source>
        <translation>アプリケーションで開く</translation>
    </message>
    <message>
        <source>Cannot start “%1”.</source>
        <translation>「%1」を起動できません。</translation>
    </message>
    <message>
        <source>No applications found</source>
        <translation>アプリケーションが見つかりません</translation>
    </message>
    <message>
        <source>Zoom locked: the next images keep it</source>
        <translation>ズームを固定しました：次の画像にも適用されます</translation>
    </message>
    <message>
        <source>Zoom unlocked</source>
        <translation>ズームの固定を解除しました</translation>
    </message>
    <message>
        <source>Delete “%1” permanently?</source>
        <translation>「%1」を完全に削除しますか？</translation>
    </message>
    <message>
        <source>The file does not go to the trash and cannot be restored.</source>
        <translation>ファイルはゴミ箱に移動されず、元に戻すことはできません。</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>削除</translation>
    </message>
    <message>
        <source>Cannot delete “%1”.</source>
        <translation>「%1」を削除できません。</translation>
    </message>
    <message>
        <source>Deleted “%1”</source>
        <translation>「%1」を削除しました</translation>
    </message>
    <message>
        <source>“%1” is no longer in the trash.</source>
        <translation>「%1」はゴミ箱にありません。</translation>
    </message>
    <message>
        <source>Cannot restore “%1”: a file with that name exists.</source>
        <translation>「%1」を元に戻せません：同じ名前のファイルが存在します。</translation>
    </message>
    <message>
        <source>Cannot restore “%1”.</source>
        <translation>「%1」を元に戻せません。</translation>
    </message>
    <message>
        <source>Restored “%1”</source>
        <translation>「%1」を元に戻しました</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation>名前を変更</translation>
    </message>
    <message>
        <source>New name</source>
        <translation>新しい名前</translation>
    </message>
    <message>
        <source>New name:</source>
        <translation>新しい名前：</translation>
    </message>
    <message>
        <source>Cannot rename “%1”.</source>
        <translation>「%1」の名前を変更できません。</translation>
    </message>
    <message>
        <source>Renamed to “%1”</source>
        <translation>「%1」に名前を変更しました</translation>
    </message>
    <message>
        <source>View</source>
        <translation>表示</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>画像</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>カラー &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Go</source>
        <translation>移動</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>ヘルプ</translation>
    </message>
    <message>
        <source>Images (%1);;All files (*)</source>
        <extracomment>File dialog filters: keep &quot;%1&quot;, &quot;;;&quot; and &quot;(*)&quot; exactly.</extracomment>
        <translation>画像 (%1);;すべてのファイル (*)</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>画像を開く</translation>
    </message>
    <message>
        <source>Copying the image…</source>
        <translation>画像をコピー中…</translation>
    </message>
    <message>
        <source>Cannot copy the image.</source>
        <translation>画像をコピーできません。</translation>
    </message>
    <message>
        <source>Image copied to the clipboard</source>
        <translation>画像をクリップボードにコピーしました</translation>
    </message>
    <message>
        <source>File path copied to the clipboard</source>
        <translation>ファイルパスをクリップボードにコピーしました</translation>
    </message>
    <message>
        <source>Move “%1” to the Recycle Bin?</source>
        <translation>「%1」をごみ箱に移動しますか？</translation>
    </message>
    <message>
        <source>Move “%1” to the trash?</source>
        <translation>「%1」をゴミ箱に入れますか？</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <source>Do not ask again</source>
        <translation>今後は確認しない</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the Recycle Bin.</source>
        <translation>「%1」をごみ箱に移動できません。</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the trash.</source>
        <translation>「%1」をゴミ箱に入れられません。</translation>
    </message>
    <message>
        <source>Moved “%1” to the Recycle Bin</source>
        <translation>「%1」をごみ箱に移動しました</translation>
    </message>
    <message>
        <source>Moved “%1” to the trash</source>
        <translation>「%1」をゴミ箱に入れました</translation>
    </message>
    <message>
        <source>No images left in this folder.</source>
        <translation>このフォルダーには画像が残っていません。</translation>
    </message>
    <message>
        <source>Image viewer with verifiable SDR and HDR color fidelity.</source>
        <translation>SDR と HDR の色忠実度を検証できる画像ビューアー。</translation>
    </message>
    <message>
        <source>Licensed under the Apache License, Version 2.0.</source>
        <translation>Apache License, Version 2.0 に基づいてライセンスされています。</translation>
    </message>
    <message>
        <source>The licenses of the third-party components are in the &lt;i&gt;third-party&lt;/i&gt; folder installed with the application.</source>
        <translation>サードパーティ製コンポーネントのライセンスは、アプリケーションと一緒にインストールされる &lt;i&gt;third-party&lt;/i&gt; フォルダーにあります。</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Animation paused</source>
        <translation>アニメーションを一時停止しました</translation>
    </message>
    <message>
        <source>Animation playing</source>
        <translation>アニメーションを再生中</translation>
    </message>
    <message>
        <source>Slideshow: a new image every %1 s</source>
        <extracomment>%1: seconds between images, e.g. &quot;5&quot;.</extracomment>
        <translation>スライドショー：%1 秒ごとに新しい画像</translation>
    </message>
</context>
</TS>
