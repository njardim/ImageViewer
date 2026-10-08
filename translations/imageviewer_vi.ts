<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="vi" sourcelanguage="en">
<context>
    <name>Color</name>
    <message>
        <source>invalid ICC profile</source>
        <translation>cấu hình ICC không hợp lệ</translation>
    </message>
    <message>
        <source>unsupported ICC color space for RGBA data</source>
        <translation>không gian màu ICC không được hỗ trợ cho dữ liệu RGBA</translation>
    </message>
    <message>
        <source>cannot build a color transform from the ICC profile</source>
        <translation>không thể tạo phép chuyển đổi màu từ cấu hình ICC</translation>
    </message>
</context>
<context>
    <name>Image</name>
    <message>
        <source>image too large for the available memory (needs %1 GB, limit %2 GB)</source>
        <extracomment>GB: gigabytes.</extracomment>
        <translation>ảnh quá lớn so với bộ nhớ khả dụng (cần %1 GB, giới hạn %2 GB)</translation>
    </message>
    <message>
        <source>(no description)</source>
        <translation>(không có mô tả)</translation>
    </message>
    <message>
        <source>linear, chromaticities from the file</source>
        <translation>tuyến tính, tọa độ sắc độ lấy từ tệp</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed: the file&apos;s chromaticities are invalid)</source>
        <translation>BT.709 tuyến tính (giả định: tọa độ sắc độ của tệp không hợp lệ)</translation>
    </message>
    <message>
        <source>%1 (assigned by the decoder)</source>
        <translation>%1 (do bộ giải mã gán)</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed)</source>
        <translation>BT.709 tuyến tính (giả định)</translation>
    </message>
    <message>
        <source>sRGB (assumed)</source>
        <translation>sRGB (giả định)</translation>
    </message>
    <message>
        <source>invalid dimensions (%1×%2×%3)</source>
        <translation>kích thước không hợp lệ (%1×%2×%3)</translation>
    </message>
    <message>
        <source>%1 (file metadata, via Qt)</source>
        <translation>%1 (siêu dữ liệu tệp, qua Qt)</translation>
    </message>
    <message>
        <source>SVG is only decoded in the graphical interface</source>
        <translation>SVG chỉ được giải mã trong giao diện đồ họa</translation>
    </message>
    <message>
        <source>damaged, truncated or unsupported %1 file</source>
        <translation>tệp %1 bị hỏng, bị cắt cụt hoặc không được hỗ trợ</translation>
    </message>
    <message>
        <source>custom primaries</source>
        <translation>màu cơ bản tùy chỉnh</translation>
    </message>
    <message>
        <source>Cannot decode: %1</source>
        <translation>Không thể giải mã: %1</translation>
    </message>
    <message>
        <source>%1 (assumed: %2)</source>
        <translation>%1 (giả định: %2)</translation>
    </message>
    <message>
        <source>Not enough memory to decode the image.</source>
        <translation>Không đủ bộ nhớ để giải mã ảnh.</translation>
    </message>
    <message>
        <source>Decoding error: %1</source>
        <translation>Lỗi giải mã: %1</translation>
    </message>
    <message>
        <source>HEIC images need Microsoft&apos;s “HEIF Image Extensions” and “HEVC Video Extensions”, from the Microsoft Store.</source>
        <translation>Ảnh HEIC cần “HEIF Image Extensions” và “HEVC Video Extensions” của Microsoft, từ Microsoft Store.</translation>
    </message>
    <message>
        <source>HEIC images need an HEVC decoder, which imageViewer does not include on this system (patents); convert them to another format first.</source>
        <translation>Ảnh HEIC cần bộ giải mã HEVC, mà imageViewer không kèm theo trên hệ thống này (bằng sáng chế); hãy chuyển đổi chúng sang định dạng khác trước.</translation>
    </message>
    <message>
        <source>the decoder took longer than %1 s and was stopped</source>
        <extracomment>%1: seconds, e.g. &quot;30&quot;.</extracomment>
        <translation>bộ giải mã chạy quá %1 giây và đã bị dừng</translation>
    </message>
</context>
<context>
    <name>Overlay</name>
    <message>
        <source>File name</source>
        <translation>Tên tệp</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>Kích thước</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>Dung lượng tệp</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>Thu phóng</translation>
    </message>
    <message>
        <source>Color space</source>
        <translation>Không gian màu</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>Ngày sửa đổi</translation>
    </message>
    <message>
        <source>Position in the folder</source>
        <translation>Vị trí trong thư mục</translation>
    </message>
    <message>
        <source>Display output</source>
        <translation>Đầu ra hiển thị</translation>
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
        <translation>EDR · khoảng dư %1×</translation>
    </message>
    <message>
        <source>Linear sRGB managed by ColorSync · no HDR headroom</source>
        <translation>sRGB tuyến tính do ColorSync quản lý · không có khoảng dư HDR</translation>
    </message>
    <message>
        <source>scRGB · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>scRGB · trắng SDR %1 nits · đỉnh %2 nits</translation>
    </message>
    <message>
        <source>HDR10 (PQ) · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>HDR10 (PQ) · trắng SDR %1 nits · đỉnh %2 nits</translation>
    </message>
    <message>
        <source>Cannot initialize the GPU (QRhi).</source>
        <translation>Không thể khởi tạo GPU (QRhi).</translation>
    </message>
    <message>
        <source>The GPU does not support RGBA16F textures.</source>
        <translation>GPU không hỗ trợ texture RGBA16F.</translation>
    </message>
    <message>
        <source>Cannot create GPU resources.</source>
        <translation>Không thể tạo tài nguyên GPU.</translation>
    </message>
    <message>
        <source>Cannot create the swapchain.</source>
        <translation>Không thể tạo swapchain.</translation>
    </message>
    <message>
        <source>(Qt defaults, not measured)</source>
        <translation>(giá trị mặc định của Qt, chưa đo)</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>Cài đặt</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>Mặc định của hệ thống</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>Ngôn ngữ:</translation>
    </message>
    <message>
        <source>Languages other than English are machine translations awaiting review by native speakers.</source>
        <translation>Các ngôn ngữ khác ngoài tiếng Anh là bản dịch máy, đang chờ người bản ngữ xem xét.</translation>
    </message>
    <message>
        <source>Reopen the last image at startup</source>
        <translation>Mở lại ảnh đã xem lần trước khi khởi động</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Chung</translation>
    </message>
    <message>
        <source>Black</source>
        <translation>Đen</translation>
    </message>
    <message>
        <source>Dark gray</source>
        <translation>Xám đậm</translation>
    </message>
    <message>
        <source>Gray</source>
        <translation>Xám</translation>
    </message>
    <message>
        <source>Light gray</source>
        <translation>Xám nhạt</translation>
    </message>
    <message>
        <source>White</source>
        <translation>Trắng</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>Tùy chỉnh…</translation>
    </message>
    <message>
        <source>Background:</source>
        <translation>Nền:</translation>
    </message>
    <message>
        <source>Remember the window size and position</source>
        <translation>Ghi nhớ kích thước và vị trí cửa sổ</translation>
    </message>
    <message>
        <source>Confirm before moving an image to the trash</source>
        <translation>Xác nhận trước khi chuyển ảnh vào Thùng rác</translation>
    </message>
    <message>
        <source>Show a checkerboard behind transparent areas</source>
        <translation>Hiển thị nền ô vuông phía sau vùng trong suốt</translation>
    </message>
    <message>
        <source>Window</source>
        <translation>Cửa sổ</translation>
    </message>
    <message>
        <source>Show the information panel (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;I&quot;.</extracomment>
        <translation>Hiện bảng thông tin (%1)</translation>
    </message>
    <message>
        <source>Overlay at the top (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;Shift+I&quot;.</extracomment>
        <translation>Lớp phủ ở trên cùng (%1)</translation>
    </message>
    <message>
        <source>Always</source>
        <translation>Luôn luôn</translation>
    </message>
    <message>
        <source>When the pointer is at the top</source>
        <translation>Khi con trỏ ở phía trên</translation>
    </message>
    <message>
        <source>Never</source>
        <translation>Không bao giờ</translation>
    </message>
    <message>
        <source>In full screen:</source>
        <translation>Ở chế độ toàn màn hình:</translation>
    </message>
    <message>
        <source>In a window:</source>
        <translation>Ở chế độ cửa sổ:</translation>
    </message>
    <message>
        <source>Fields</source>
        <translation>Trường</translation>
    </message>
    <message>
        <source>Move Up</source>
        <translation>Di chuyển lên</translation>
    </message>
    <message>
        <source>Move Down</source>
        <translation>Di chuyển xuống</translation>
    </message>
    <message>
        <source>Fields:</source>
        <translation>Trường:</translation>
    </message>
    <message>
        <source> %</source>
        <extracomment>Unit after a percentage; keep the leading space if your language separates it.</extracomment>
        <translation>%</translation>
    </message>
    <message>
        <source>Background opacity:</source>
        <translation>Độ mờ đục của nền:</translation>
    </message>
    <message>
        <source>Text opacity:</source>
        <translation>Độ mờ đục của văn bản:</translation>
    </message>
    <message>
        <source>Outline the text</source>
        <translation>Tạo viền cho văn bản</translation>
    </message>
    <message>
        <source> s</source>
        <extracomment>Unit after a number of seconds; keep the leading space if your language separates units.</extracomment>
        <translation> giây</translation>
    </message>
    <message>
        <source>Hide after:</source>
        <translation>Ẩn sau:</translation>
    </message>
    <message>
        <source>Information</source>
        <translation>Thông tin</translation>
    </message>
    <message>
        <source>After the last image, continue with the first</source>
        <translation>Sau ảnh cuối cùng, tiếp tục từ ảnh đầu tiên</translation>
    </message>
    <message>
        <source>Click the left or right side of the window for the previous or next image</source>
        <translation>Nhấp vào bên trái hoặc bên phải cửa sổ để chuyển đến ảnh trước hoặc ảnh tiếp theo</translation>
    </message>
    <message>
        <source> px</source>
        <extracomment>Unit after a number of pixels; keep the leading space if your language separates units.</extracomment>
        <translation> px</translation>
    </message>
    <message>
        <source>Width of each side:</source>
        <translation>Độ rộng mỗi bên:</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>Tên</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>Ngày sửa đổi</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Dung lượng</translation>
    </message>
    <message>
        <source>Descending</source>
        <translation>Giảm dần</translation>
    </message>
    <message>
        <source>Sort images by:</source>
        <translation>Sắp xếp ảnh theo:</translation>
    </message>
    <message>
        <source>Load the next and previous images in advance</source>
        <translation>Tải trước ảnh trước và ảnh tiếp theo</translation>
    </message>
    <message>
        <source>Slideshow (%1), time per image:</source>
        <extracomment>%1: the key that starts and stops the slideshow, e.g. &quot;S&quot;.</extracomment>
        <translation>Trình chiếu (%1), thời gian mỗi ảnh:</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>Điều hướng</translation>
    </message>
    <message>
        <source>Automatic (HDR when the display supports it)</source>
        <translation>Tự động (HDR khi màn hình hỗ trợ)</translation>
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
        <translation>Đầu ra hiển thị:</translation>
    </message>
    <message>
        <source>Tone map HDR images that exceed the display (ITU-R BT.2390)</source>
        <translation>Ánh xạ tông màu cho ảnh HDR vượt quá khả năng của màn hình (ITU-R BT.2390)</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>Màu sắc &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Hủy</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>Áp dụng</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>Khôi phục mặc định</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>Màu nền</translation>
    </message>
</context>
<context>
    <name>ViewerWindow</name>
    <message>
        <source>File not found: %1</source>
        <translation>Không tìm thấy tệp: %1</translation>
    </message>
    <message>
        <source>The folder contains no supported images.</source>
        <translation>Thư mục không chứa ảnh nào được hỗ trợ.</translation>
    </message>
    <message>
        <source>Loading %1…</source>
        <translation>Đang tải %1…</translation>
    </message>
    <message>
        <source>This is the last image.</source>
        <translation>Đây là ảnh cuối cùng.</translation>
    </message>
    <message>
        <source>This is the first image.</source>
        <translation>Đây là ảnh đầu tiên.</translation>
    </message>
    <message>
        <source>Reducing the image to fit the GPU (at most %1 px)…</source>
        <extracomment>%1: a size in pixels.</extracomment>
        <translation>Đang thu nhỏ ảnh cho vừa GPU (tối đa %1 px)…</translation>
    </message>
    <message>
        <source>The GPU did not accept the image.</source>
        <translation>GPU không chấp nhận ảnh.</translation>
    </message>
    <message>
        <source>Slideshow stopped</source>
        <translation>Đã dừng trình chiếu</translation>
    </message>
    <message>
        <source>File</source>
        <translation>Tệp</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>Thư mục</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>Dung lượng</translation>
    </message>
    <message>
        <source>Modified</source>
        <translation>Sửa đổi</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>Vị trí</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <extracomment>Position of the image in its folder, e.g. &quot;3 of 120&quot;.</extracomment>
        <translation>%1 trên %2</translation>
    </message>
    <message>
        <source>%1 MP</source>
        <extracomment>Megapixels, e.g. &quot;24.0 MP&quot;.</extracomment>
        <translation>%1 MP</translation>
    </message>
    <message>
        <source>reduced to %1 × %2 for the GPU</source>
        <translation>đã thu nhỏ còn %1 × %2 cho GPU</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>Kích thước</translation>
    </message>
    <message>
        <source>floating point</source>
        <translation>dấu phẩy động</translation>
    </message>
    <message>
        <source>alpha</source>
        <translation>kênh alpha</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>Định dạng</translation>
    </message>
    <message>
        <source>frame %1 of %2</source>
        <extracomment>The frame of an animation on screen, e.g. &quot;frame 3 of 24&quot;.</extracomment>
        <translation>khung %1 trên %2</translation>
    </message>
    <message>
        <source>frame %1</source>
        <translation>khung %1</translation>
    </message>
    <message>
        <source>paused</source>
        <translation>tạm dừng</translation>
    </message>
    <message>
        <source>Animation</source>
        <translation>Hoạt ảnh</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>Hướng</translation>
    </message>
    <message>
        <source>EXIF %1, applied</source>
        <extracomment>The EXIF orientation tag (2 to 8) of the file, already applied to the image.</extracomment>
        <translation>EXIF %1, đã áp dụng</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>Màu</translation>
    </message>
    <message>
        <source>Peak</source>
        <translation>Đỉnh</translation>
    </message>
    <message>
        <source>%1× SDR white (≈%2 nits)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1× trắng SDR (≈%2 nits)</translation>
    </message>
    <message>
        <source>Decoded in</source>
        <translation>Giải mã trong</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <extracomment>Unit after a duration in milliseconds.</extracomment>
        <translation>%1 ms</translation>
    </message>
    <message>
        <source>Camera</source>
        <translation>Máy ảnh</translation>
    </message>
    <message>
        <source>Lens</source>
        <translation>Ống kính</translation>
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
        <translation>Phơi sáng</translation>
    </message>
    <message>
        <source>Taken</source>
        <extracomment>Label of the date the photograph was taken.</extracomment>
        <translation>Ngày chụp</translation>
    </message>
    <message>
        <source>%1 %</source>
        <extracomment>A zoom percentage, e.g. &quot;100 %&quot;; write the percent sign as your language does.</extracomment>
        <translation>%1%</translation>
    </message>
    <message>
        <source>rotated %1°</source>
        <extracomment>The view is rotated clockwise by this many degrees.</extracomment>
        <translation>xoay %1°</translation>
    </message>
    <message>
        <source>mirrored</source>
        <translation>lật gương</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>Đầu ra</translation>
    </message>
    <message>
        <source>BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>Ánh xạ tông màu BT.2390 từ %1 đến %2 nits, giữ nguyên đến %3 nits</translation>
    </message>
    <message>
        <source>clipped above %1 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>bị cắt trên %1 nits</translation>
    </message>
    <message>
        <source>Highlights</source>
        <translation>Vùng sáng</translation>
    </message>
    <message>
        <source>clipped above %1 nits (colors outside the output gamut)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>bị cắt trên %1 nits (màu nằm ngoài gam màu đầu ra)</translation>
    </message>
    <message>
        <source>clipped above %1 nits (tone mapping off)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>bị cắt trên %1 nits (đã tắt ánh xạ tông màu)</translation>
    </message>
    <message>
        <source>exposure %1 EV</source>
        <extracomment>The viewer&apos;s exposure adjustment in EV (photographic stops), e.g. &quot;exposure +1.5 EV&quot;.</extracomment>
        <translation>phơi sáng %1 EV</translation>
    </message>
    <message>
        <source>altered pixels highlighted</source>
        <translation>đang làm nổi bật điểm ảnh bị thay đổi</translation>
    </message>
    <message>
        <source>%1-bit</source>
        <extracomment>Bits per channel of the image file, e.g. &quot;16-bit&quot;.</extracomment>
        <translation>%1 bit</translation>
    </message>
    <message>
        <source>Enter a name.</source>
        <translation>Hãy nhập tên.</translation>
    </message>
    <message>
        <source>This name is not allowed.</source>
        <translation>Không cho phép tên này.</translation>
    </message>
    <message>
        <source>A name cannot contain “/” or “\”.</source>
        <translation>Tên không được chứa “/” hoặc “\”.</translation>
    </message>
    <message>
        <source>The name is too long.</source>
        <translation>Tên quá dài.</translation>
    </message>
    <message>
        <source>Windows does not allow this name.</source>
        <extracomment>Windows forbids &lt; &gt; : &quot; | ? *, control characters, device names such as CON, and a final dot or space.</extracomment>
        <translation>Windows không cho phép tên này.</translation>
    </message>
    <message>
        <source>A file with this name already exists.</source>
        <translation>Đã có tệp với tên này.</translation>
    </message>
    <message>
        <source>Open…</source>
        <translation>Mở…</translation>
    </message>
    <message>
        <source>Clear Menu</source>
        <extracomment>Empties the Open Recent menu.</extracomment>
        <translation>Xóa menu</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>Hiển thị trong Trình khám phá Tệp</translation>
    </message>
    <message>
        <source>Move to Recycle Bin…</source>
        <translation>Chuyển vào Thùng Rác…</translation>
    </message>
    <message>
        <source>Move to Recycle Bin</source>
        <translation>Chuyển vào Thùng Rác</translation>
    </message>
    <message>
        <source>Undo Move to Recycle Bin</source>
        <translation>Hoàn tác chuyển vào Thùng Rác</translation>
    </message>
    <message>
        <source>Show in Finder</source>
        <translation>Hiển thị trong Finder</translation>
    </message>
    <message>
        <source>Move to Trash…</source>
        <translation>Chuyển vào Thùng rác…</translation>
    </message>
    <message>
        <source>Move to Trash</source>
        <translation>Chuyển vào Thùng rác</translation>
    </message>
    <message>
        <source>Undo Move to Trash</source>
        <translation>Hoàn tác chuyển vào Thùng rác</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>Hiển thị trong trình quản lý tệp</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>Đổi tên…</translation>
    </message>
    <message>
        <source>Delete Permanently…</source>
        <translation>Xóa vĩnh viễn…</translation>
    </message>
    <message>
        <source>Copy Image</source>
        <translation>Sao chép ảnh</translation>
    </message>
    <message>
        <source>Copy File Path</source>
        <translation>Sao chép đường dẫn tệp</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>Cài đặt…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>Thoát</translation>
    </message>
    <message>
        <source>Previous Image</source>
        <translation>Ảnh trước</translation>
    </message>
    <message>
        <source>Next Image</source>
        <translation>Ảnh tiếp theo</translation>
    </message>
    <message>
        <source>First Image</source>
        <translation>Ảnh đầu tiên</translation>
    </message>
    <message>
        <source>Last Image</source>
        <translation>Ảnh cuối cùng</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>Phóng to</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>Thu nhỏ</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>Vừa với cửa sổ</translation>
    </message>
    <message>
        <source>Actual Size (100 %)</source>
        <extracomment>&quot;100 %&quot; is a zoom percentage; write the percent sign as your language does.</extracomment>
        <translation>Kích thước thực (100%)</translation>
    </message>
    <message>
        <source>Full Screen</source>
        <translation>Toàn màn hình</translation>
    </message>
    <message>
        <source>Information Panel</source>
        <translation>Bảng thông tin</translation>
    </message>
    <message>
        <source>Information Overlay</source>
        <translation>Lớp phủ thông tin</translation>
    </message>
    <message>
        <source>Checkerboard Background</source>
        <translation>Nền ô vuông</translation>
    </message>
    <message>
        <source>Rotate Clockwise</source>
        <translation>Xoay theo chiều kim đồng hồ</translation>
    </message>
    <message>
        <source>Rotate Counterclockwise</source>
        <translation>Xoay ngược chiều kim đồng hồ</translation>
    </message>
    <message>
        <source>Flip Horizontally</source>
        <translation>Lật ngang</translation>
    </message>
    <message>
        <source>Flip Vertically</source>
        <translation>Lật dọc</translation>
    </message>
    <message>
        <source>Increase Exposure (+½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>Tăng phơi sáng (+½ EV)</translation>
    </message>
    <message>
        <source>Decrease Exposure (−½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>Giảm phơi sáng (−½ EV)</translation>
    </message>
    <message>
        <source>Reset Exposure</source>
        <translation>Đặt lại phơi sáng</translation>
    </message>
    <message>
        <source>Tone Mapping (BT.2390)</source>
        <translation>Ánh xạ tông màu (BT.2390)</translation>
    </message>
    <message>
        <source>Highlight Altered Pixels</source>
        <translation>Làm nổi bật điểm ảnh bị thay đổi</translation>
    </message>
    <message>
        <source>Pause Animation</source>
        <translation>Tạm dừng hoạt ảnh</translation>
    </message>
    <message>
        <source>Previous Frame</source>
        <translation>Khung trước</translation>
    </message>
    <message>
        <source>Next Frame</source>
        <translation>Khung tiếp theo</translation>
    </message>
    <message>
        <source>Slideshow</source>
        <translation>Trình chiếu</translation>
    </message>
    <message>
        <source>About imageViewer</source>
        <translation>Giới thiệu về imageViewer</translation>
    </message>
    <message>
        <source>About Qt</source>
        <translation>Giới thiệu về Qt</translation>
    </message>
    <message>
        <source>Open Recent</source>
        <translation>Mở gần đây</translation>
    </message>
    <message>
        <source>Delete “%1” permanently?</source>
        <translation>Xóa vĩnh viễn “%1”?</translation>
    </message>
    <message>
        <source>The file does not go to the trash and cannot be restored.</source>
        <translation>Tệp sẽ không được chuyển vào Thùng rác và không thể khôi phục.</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>Xóa</translation>
    </message>
    <message>
        <source>Cannot delete “%1”.</source>
        <translation>Không thể xóa “%1”.</translation>
    </message>
    <message>
        <source>Deleted “%1”</source>
        <translation>Đã xóa “%1”</translation>
    </message>
    <message>
        <source>“%1” is no longer in the trash.</source>
        <translation>“%1” không còn trong Thùng rác.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”: a file with that name exists.</source>
        <translation>Không thể khôi phục “%1”: đã có tệp cùng tên.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”.</source>
        <translation>Không thể khôi phục “%1”.</translation>
    </message>
    <message>
        <source>Restored “%1”</source>
        <translation>Đã khôi phục “%1”</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation>Đổi tên</translation>
    </message>
    <message>
        <source>New name</source>
        <translation>Tên mới</translation>
    </message>
    <message>
        <source>New name:</source>
        <translation>Tên mới:</translation>
    </message>
    <message>
        <source>Cannot rename “%1”.</source>
        <translation>Không thể đổi tên “%1”.</translation>
    </message>
    <message>
        <source>Renamed to “%1”</source>
        <translation>Đã đổi tên thành “%1”</translation>
    </message>
    <message>
        <source>View</source>
        <translation>Xem</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>Ảnh</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>Màu sắc &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Go</source>
        <translation>Đi</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>Trợ giúp</translation>
    </message>
    <message>
        <source>Images (%1);;All files (*)</source>
        <extracomment>File dialog filters: keep &quot;%1&quot;, &quot;;;&quot; and &quot;(*)&quot; exactly.</extracomment>
        <translation>Ảnh (%1);;Tất cả tệp (*)</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>Mở ảnh</translation>
    </message>
    <message>
        <source>Copying the image…</source>
        <translation>Đang sao chép ảnh…</translation>
    </message>
    <message>
        <source>Cannot copy the image.</source>
        <translation>Không thể sao chép ảnh.</translation>
    </message>
    <message>
        <source>Image copied to the clipboard</source>
        <translation>Đã sao chép ảnh vào bảng tạm</translation>
    </message>
    <message>
        <source>File path copied to the clipboard</source>
        <translation>Đã sao chép đường dẫn tệp vào bảng tạm</translation>
    </message>
    <message>
        <source>Move “%1” to the Recycle Bin?</source>
        <translation>Chuyển “%1” vào Thùng Rác?</translation>
    </message>
    <message>
        <source>Move “%1” to the trash?</source>
        <translation>Chuyển “%1” vào Thùng rác?</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Hủy</translation>
    </message>
    <message>
        <source>Do not ask again</source>
        <translation>Không hỏi lại</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the Recycle Bin.</source>
        <translation>Không thể chuyển “%1” vào Thùng Rác.</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the trash.</source>
        <translation>Không thể chuyển “%1” vào Thùng rác.</translation>
    </message>
    <message>
        <source>Moved “%1” to the Recycle Bin</source>
        <translation>Đã chuyển “%1” vào Thùng Rác</translation>
    </message>
    <message>
        <source>Moved “%1” to the trash</source>
        <translation>Đã chuyển “%1” vào Thùng rác</translation>
    </message>
    <message>
        <source>No images left in this folder.</source>
        <translation>Thư mục này không còn ảnh nào.</translation>
    </message>
    <message>
        <source>Image viewer with verifiable SDR and HDR color fidelity.</source>
        <translation>Trình xem ảnh với độ trung thực màu SDR và HDR có thể kiểm chứng.</translation>
    </message>
    <message>
        <source>Licensed under the Apache License, Version 2.0.</source>
        <translation>Được cấp phép theo Apache License, Version 2.0.</translation>
    </message>
    <message>
        <source>The licenses of the third-party components are in the &lt;i&gt;third-party&lt;/i&gt; folder installed with the application.</source>
        <translation>Giấy phép của các thành phần bên thứ ba nằm trong thư mục &lt;i&gt;third-party&lt;/i&gt; được cài đặt cùng ứng dụng.</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Animation paused</source>
        <translation>Đã tạm dừng hoạt ảnh</translation>
    </message>
    <message>
        <source>Animation playing</source>
        <translation>Đang phát hoạt ảnh</translation>
    </message>
    <message>
        <source>Slideshow: a new image every %1 s</source>
        <extracomment>%1: seconds between images, e.g. &quot;5&quot;.</extracomment>
        <translation>Trình chiếu: ảnh mới mỗi %1 giây</translation>
    </message>
</context>
</TS>
