<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ko" sourcelanguage="en">
<context>
    <name>Color</name>
    <message>
        <source>invalid ICC profile</source>
        <translation>잘못된 ICC 프로파일</translation>
    </message>
    <message>
        <source>unsupported ICC color space for RGBA data</source>
        <translation>RGBA 데이터에 지원되지 않는 ICC 색 공간</translation>
    </message>
    <message>
        <source>cannot build a color transform from the ICC profile</source>
        <translation>ICC 프로파일에서 색 변환을 만들 수 없음</translation>
    </message>
</context>
<context>
    <name>Image</name>
    <message>
        <source>image too large for the available memory (needs %1 GB, limit %2 GB)</source>
        <extracomment>GB: gigabytes.</extracomment>
        <translation>사용 가능한 메모리에 비해 이미지가 너무 큼(필요 %1 GB, 한도 %2 GB)</translation>
    </message>
    <message>
        <source>(no description)</source>
        <translation>(설명 없음)</translation>
    </message>
    <message>
        <source>linear, chromaticities from the file</source>
        <translation>선형, 파일의 색도 좌표 사용</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed: the file&apos;s chromaticities are invalid)</source>
        <translation>선형 BT.709(가정: 파일의 색도 좌표가 올바르지 않음)</translation>
    </message>
    <message>
        <source>%1 (assigned by the decoder)</source>
        <translation>%1(디코더가 지정)</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed)</source>
        <translation>선형 BT.709(가정)</translation>
    </message>
    <message>
        <source>sRGB (assumed)</source>
        <translation>sRGB(가정)</translation>
    </message>
    <message>
        <source>invalid dimensions (%1×%2×%3)</source>
        <translation>잘못된 크기(%1×%2×%3)</translation>
    </message>
    <message>
        <source>%1 (file metadata, via Qt)</source>
        <translation>%1(파일 메타데이터, Qt 경유)</translation>
    </message>
    <message>
        <source>SVG is only decoded in the graphical interface</source>
        <translation>SVG는 그래픽 인터페이스에서만 디코딩됩니다</translation>
    </message>
    <message>
        <source>damaged, truncated or unsupported %1 file</source>
        <translation>손상되었거나 잘렸거나 지원되지 않는 %1 파일</translation>
    </message>
    <message>
        <source>custom primaries</source>
        <translation>사용자 지정 원색</translation>
    </message>
    <message>
        <source>Cannot decode: %1</source>
        <translation>디코딩할 수 없음: %1</translation>
    </message>
    <message>
        <source>%1 (assumed: %2)</source>
        <translation>%1(가정: %2)</translation>
    </message>
    <message>
        <source>Not enough memory to decode the image.</source>
        <translation>이미지를 디코딩하기에 메모리가 부족합니다.</translation>
    </message>
    <message>
        <source>Decoding error: %1</source>
        <translation>디코딩 오류: %1</translation>
    </message>
    <message>
        <source>HEIC images need Microsoft&apos;s “HEIF Image Extensions” and “HEVC Video Extensions”, from the Microsoft Store.</source>
        <translation>HEIC 이미지를 열려면 Microsoft Store의 Microsoft “HEIF Image Extensions”와 “HEVC Video Extensions”가 필요합니다.</translation>
    </message>
    <message>
        <source>HEIC images need an HEVC decoder, which imageViewer does not include on this system (patents); convert them to another format first.</source>
        <translation>HEIC 이미지를 열려면 HEVC 디코더가 필요하지만 imageViewer는 이 시스템에서 이를 포함하지 않습니다(특허). 먼저 다른 형식으로 변환하세요.</translation>
    </message>
    <message>
        <source>the decoder took longer than %1 s and was stopped</source>
        <extracomment>%1: seconds, e.g. &quot;30&quot;.</extracomment>
        <translation>디코더가 %1초를 넘겨 중지되었습니다</translation>
    </message>
</context>
<context>
    <name>Overlay</name>
    <message>
        <source>File name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>이미지 크기</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>파일 크기</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>배율</translation>
    </message>
    <message>
        <source>Color space</source>
        <translation>색 공간</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>수정한 날짜</translation>
    </message>
    <message>
        <source>Position in the folder</source>
        <translation>폴더 내 위치</translation>
    </message>
    <message>
        <source>Display output</source>
        <translation>디스플레이 출력</translation>
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
        <translation>EDR · 헤드룸 %1×</translation>
    </message>
    <message>
        <source>Linear sRGB managed by ColorSync · no HDR headroom</source>
        <translation>ColorSync가 관리하는 선형 sRGB · HDR 헤드룸 없음</translation>
    </message>
    <message>
        <source>scRGB · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>scRGB · SDR 흰색 %1 cd/m² · 최대 %2 cd/m²</translation>
    </message>
    <message>
        <source>HDR10 (PQ) · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>HDR10 (PQ) · SDR 흰색 %1 cd/m² · 최대 %2 cd/m²</translation>
    </message>
    <message>
        <source>Cannot initialize the GPU (QRhi).</source>
        <translation>GPU(QRhi)를 초기화할 수 없습니다.</translation>
    </message>
    <message>
        <source>The GPU does not support RGBA16F textures.</source>
        <translation>GPU가 RGBA16F 텍스처를 지원하지 않습니다.</translation>
    </message>
    <message>
        <source>Cannot create GPU resources.</source>
        <translation>GPU 리소스를 만들 수 없습니다.</translation>
    </message>
    <message>
        <source>Cannot create the swapchain.</source>
        <translation>스왑체인을 만들 수 없습니다.</translation>
    </message>
    <message>
        <source>(Qt defaults, not measured)</source>
        <translation>(Qt 기본값, 측정되지 않음)</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>설정</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>시스템 기본값</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>언어:</translation>
    </message>
    <message>
        <source>Languages other than English are machine translations awaiting review by native speakers.</source>
        <translation>영어 이외의 언어는 기계 번역이며 원어민의 검토를 기다리고 있습니다.</translation>
    </message>
    <message>
        <source>Confirm before moving an image to the trash</source>
        <translation>이미지를 휴지통으로 이동하기 전에 확인</translation>
    </message>
    <message>
        <source>Reopen the last image at startup</source>
        <translation>시작할 때 마지막 이미지 다시 열기</translation>
    </message>
    <message>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <source>Black</source>
        <translation>검정</translation>
    </message>
    <message>
        <source>Dark gray</source>
        <translation>어두운 회색</translation>
    </message>
    <message>
        <source>Gray</source>
        <translation>회색</translation>
    </message>
    <message>
        <source>Light gray</source>
        <translation>밝은 회색</translation>
    </message>
    <message>
        <source>White</source>
        <translation>흰색</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>사용자 지정…</translation>
    </message>
    <message>
        <source>Background:</source>
        <translation>배경:</translation>
    </message>
    <message>
        <source>Show a checkerboard behind transparent areas</source>
        <translation>투명 영역 뒤에 체크무늬 표시</translation>
    </message>
    <message>
        <source>Remember the window size and position</source>
        <translation>창 크기와 위치 기억</translation>
    </message>
    <message>
        <source>Window</source>
        <translation>창</translation>
    </message>
    <message>
        <source>Show the information panel (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;I&quot;.</extracomment>
        <translation>정보 패널 표시(%1)</translation>
    </message>
    <message>
        <source>Overlay at the top (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;Shift+I&quot;.</extracomment>
        <translation>상단 오버레이(%1)</translation>
    </message>
    <message>
        <source>Always</source>
        <translation>항상</translation>
    </message>
    <message>
        <source>When the pointer is at the top</source>
        <translation>포인터가 상단에 있을 때</translation>
    </message>
    <message>
        <source>Never</source>
        <translation>표시 안 함</translation>
    </message>
    <message>
        <source>In full screen:</source>
        <translation>전체 화면에서:</translation>
    </message>
    <message>
        <source>In a window:</source>
        <translation>창에서:</translation>
    </message>
    <message>
        <source>Fields</source>
        <translation>항목</translation>
    </message>
    <message>
        <source>Move Up</source>
        <translation>위로 이동</translation>
    </message>
    <message>
        <source>Move Down</source>
        <translation>아래로 이동</translation>
    </message>
    <message>
        <source>Fields:</source>
        <translation>항목:</translation>
    </message>
    <message>
        <source>Appearance of the panel and the overlay</source>
        <translation>패널 및 오버레이 모양</translation>
    </message>
    <message>
        <source> %</source>
        <extracomment>Unit after a percentage; keep the leading space if your language separates it.</extracomment>
        <translation>%</translation>
    </message>
    <message>
        <source>Background opacity:</source>
        <translation>배경 불투명도:</translation>
    </message>
    <message>
        <source>Text opacity:</source>
        <translation>텍스트 불투명도:</translation>
    </message>
    <message>
        <source>Outline the text</source>
        <translation>텍스트에 윤곽선 넣기</translation>
    </message>
    <message>
        <source> s</source>
        <extracomment>Unit after a number of seconds; keep the leading space if your language separates units.</extracomment>
        <translation>초</translation>
    </message>
    <message>
        <source>Hide after:</source>
        <translation>다음 시간 후 숨기기:</translation>
    </message>
    <message>
        <source>Information</source>
        <translation>정보</translation>
    </message>
    <message>
        <source>After the last image, continue with the first</source>
        <translation>마지막 이미지 다음에는 첫 번째 이미지로 이동</translation>
    </message>
    <message>
        <source>Click the left or right side of the window for the previous or next image</source>
        <translation>창의 왼쪽 또는 오른쪽을 클릭하여 이전 또는 다음 이미지로 이동</translation>
    </message>
    <message>
        <source> px</source>
        <extracomment>Unit after a number of pixels; keep the leading space if your language separates units.</extracomment>
        <translation>px</translation>
    </message>
    <message>
        <source>Width of each side:</source>
        <translation>각 측면 영역의 너비:</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>수정한 날짜</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <source>Descending</source>
        <translation>내림차순</translation>
    </message>
    <message>
        <source>Sort images by:</source>
        <translation>이미지 정렬 기준:</translation>
    </message>
    <message>
        <source>Load the next and previous images in advance</source>
        <translation>다음 및 이전 이미지를 미리 불러오기</translation>
    </message>
    <message>
        <source>Slideshow (%1), time per image:</source>
        <extracomment>%1: the key that starts and stops the slideshow, e.g. &quot;S&quot;.</extracomment>
        <translation>슬라이드 쇼(%1), 이미지당 시간:</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>탐색</translation>
    </message>
    <message>
        <source>Automatic (HDR when the display supports it)</source>
        <translation>자동(디스플레이가 지원하면 HDR)</translation>
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
        <translation>디스플레이 출력:</translation>
    </message>
    <message>
        <source>Tone map HDR images that exceed the display (ITU-R BT.2390)</source>
        <translation>디스플레이 범위를 초과하는 HDR 이미지 톤 매핑(ITU-R BT.2390)</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>색상 &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>적용</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>기본값 복원</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>배경색</translation>
    </message>
</context>
<context>
    <name>ViewerWindow</name>
    <message>
        <source>Reducing the image to fit the GPU (at most %1 px)…</source>
        <extracomment>%1: a size in pixels.</extracomment>
        <translation>GPU에 맞게 이미지를 축소하는 중(최대 %1px)…</translation>
    </message>
    <message>
        <source>The GPU did not accept the image.</source>
        <translation>GPU가 이미지를 받아들이지 않았습니다.</translation>
    </message>
    <message>
        <source>Slideshow stopped</source>
        <translation>슬라이드 쇼 중지됨</translation>
    </message>
    <message>
        <source>File not found: %1</source>
        <translation>파일을 찾을 수 없음: %1</translation>
    </message>
    <message>
        <source>The folder contains no supported images.</source>
        <translation>폴더에 지원되는 이미지가 없습니다.</translation>
    </message>
    <message>
        <source>This is the last image.</source>
        <translation>마지막 이미지입니다.</translation>
    </message>
    <message>
        <source>This is the first image.</source>
        <translation>첫 번째 이미지입니다.</translation>
    </message>
    <message>
        <source>Loading %1…</source>
        <translation>%1 불러오는 중…</translation>
    </message>
    <message>
        <source>No images left in this folder.</source>
        <translation>이 폴더에 남은 이미지가 없습니다.</translation>
    </message>
    <message>
        <source>File</source>
        <translation>파일</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>폴더</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>파일 크기</translation>
    </message>
    <message>
        <source>Modified</source>
        <translation>수정한 날짜</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>위치</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <extracomment>Position of the image in its folder, e.g. &quot;3 of 120&quot;.</extracomment>
        <translation>%2 중 %1</translation>
    </message>
    <message>
        <source>%1 MP</source>
        <extracomment>Megapixels, e.g. &quot;24.0 MP&quot;.</extracomment>
        <translation>%1 MP</translation>
    </message>
    <message>
        <source>reduced to %1 × %2 for the GPU</source>
        <translation>GPU용으로 %1 × %2로 축소됨</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>이미지 크기</translation>
    </message>
    <message>
        <source>%1-bit</source>
        <extracomment>Bits per channel of the image file, e.g. &quot;16-bit&quot;.</extracomment>
        <translation>%1비트</translation>
    </message>
    <message>
        <source>floating point</source>
        <translation>부동 소수점</translation>
    </message>
    <message>
        <source>alpha</source>
        <translation>알파</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>형식</translation>
    </message>
    <message>
        <source>frame %1 of %2</source>
        <extracomment>The frame of an animation on screen, e.g. &quot;frame 3 of 24&quot;.</extracomment>
        <translation>프레임 %1/%2</translation>
    </message>
    <message>
        <source>frame %1</source>
        <translation>프레임 %1</translation>
    </message>
    <message>
        <source>paused</source>
        <translation>일시 정지됨</translation>
    </message>
    <message>
        <source>Animation</source>
        <translation>애니메이션</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>방향</translation>
    </message>
    <message>
        <source>EXIF %1, applied</source>
        <extracomment>The EXIF orientation tag (2 to 8) of the file, already applied to the image.</extracomment>
        <translation>EXIF %1, 적용됨</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>색상</translation>
    </message>
    <message>
        <source>Peak</source>
        <translation>최대 밝기</translation>
    </message>
    <message>
        <source>%1× SDR white (≈%2 nits)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>SDR 흰색의 %1배(≈%2 cd/m²)</translation>
    </message>
    <message>
        <source>Decoded in</source>
        <translation>디코딩 소요 시간</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <extracomment>Unit after a duration in milliseconds.</extracomment>
        <translation>%1 ms</translation>
    </message>
    <message>
        <source>Camera</source>
        <translation>카메라</translation>
    </message>
    <message>
        <source>Lens</source>
        <translation>렌즈</translation>
    </message>
    <message>
        <source>%1 s</source>
        <extracomment>Exposure time of a photograph, e.g. &quot;1/250 s&quot;.</extracomment>
        <translation>%1초</translation>
    </message>
    <message>
        <source>%1 mm</source>
        <extracomment>Focal length of the lens, e.g. &quot;50 mm&quot;.</extracomment>
        <translation>%1 mm</translation>
    </message>
    <message>
        <source>Exposure</source>
        <extracomment>Label of the photograph&apos;s shooting settings: exposure time, aperture, ISO, focal length.</extracomment>
        <translation>노출</translation>
    </message>
    <message>
        <source>Taken</source>
        <extracomment>Label of the date the photograph was taken.</extracomment>
        <translation>촬영한 날짜</translation>
    </message>
    <message>
        <source>%1 %</source>
        <extracomment>A zoom percentage, e.g. &quot;100 %&quot;; write the percent sign as your language does.</extracomment>
        <translation>%1%</translation>
    </message>
    <message>
        <source>rotated %1°</source>
        <extracomment>The view is rotated clockwise by this many degrees.</extracomment>
        <translation>시계 방향으로 %1° 회전됨</translation>
    </message>
    <message>
        <source>mirrored</source>
        <translation>반전됨</translation>
    </message>
    <message>
        <source>exposure %1 EV</source>
        <extracomment>The viewer&apos;s exposure adjustment in EV (photographic stops), e.g. &quot;exposure +1.5 EV&quot;.</extracomment>
        <translation>노출 %1 EV</translation>
    </message>
    <message>
        <source>altered pixels highlighted</source>
        <translation>변경된 픽셀 강조 표시</translation>
    </message>
    <message>
        <source>View</source>
        <translation>보기</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>출력</translation>
    </message>
    <message>
        <source>BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 cd/m²에서 %2 cd/m²로 BT.2390 톤 매핑, %3 cd/m²까지는 변경 없음</translation>
    </message>
    <message>
        <source>clipped above %1 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 cd/m² 초과 클리핑</translation>
    </message>
    <message>
        <source>Highlights</source>
        <translation>하이라이트</translation>
    </message>
    <message>
        <source>clipped above %1 nits (colors outside the output gamut)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 cd/m² 초과 클리핑(출력 색역을 벗어난 색상)</translation>
    </message>
    <message>
        <source>clipped above %1 nits (tone mapping off)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1 cd/m² 초과 클리핑(톤 매핑 꺼짐)</translation>
    </message>
    <message>
        <source>Enter a name.</source>
        <translation>이름을 입력하세요.</translation>
    </message>
    <message>
        <source>This name is not allowed.</source>
        <translation>이 이름은 사용할 수 없습니다.</translation>
    </message>
    <message>
        <source>A name cannot contain “/” or “\”.</source>
        <translation>이름에는 “/” 또는 “\”를 사용할 수 없습니다.</translation>
    </message>
    <message>
        <source>The name is too long.</source>
        <translation>이름이 너무 깁니다.</translation>
    </message>
    <message>
        <source>Windows does not allow this name.</source>
        <extracomment>Windows forbids &lt; &gt; : &quot; | ? *, control characters, device names such as CON, and a final dot or space.</extracomment>
        <translation>Windows에서 허용하지 않는 이름입니다.</translation>
    </message>
    <message>
        <source>A file with this name already exists.</source>
        <translation>같은 이름의 파일이 이미 있습니다.</translation>
    </message>
    <message>
        <source>Open…</source>
        <translation>열기…</translation>
    </message>
    <message>
        <source>Clear Menu</source>
        <extracomment>Empties the Open Recent menu.</extracomment>
        <translation>메뉴 지우기</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>탐색기에서 보기</translation>
    </message>
    <message>
        <source>Move to Recycle Bin…</source>
        <translation>휴지통으로 이동…</translation>
    </message>
    <message>
        <source>Move to Recycle Bin</source>
        <translation>휴지통으로 이동</translation>
    </message>
    <message>
        <source>Undo Move to Recycle Bin</source>
        <translation>휴지통으로 이동 실행 취소</translation>
    </message>
    <message>
        <source>Show in Finder</source>
        <translation>Finder에서 보기</translation>
    </message>
    <message>
        <source>Move to Trash…</source>
        <translation>휴지통으로 이동…</translation>
    </message>
    <message>
        <source>Move to Trash</source>
        <translation>휴지통으로 이동</translation>
    </message>
    <message>
        <source>Undo Move to Trash</source>
        <translation>휴지통으로 이동 실행 취소</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>파일 관리자에서 보기</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>이름 바꾸기…</translation>
    </message>
    <message>
        <source>Delete Permanently…</source>
        <translation>영구 삭제…</translation>
    </message>
    <message>
        <source>Copy Image</source>
        <translation>이미지 복사</translation>
    </message>
    <message>
        <source>Copy File Path</source>
        <translation>파일 경로 복사</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>설정…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>종료</translation>
    </message>
    <message>
        <source>Previous Image</source>
        <translation>이전 이미지</translation>
    </message>
    <message>
        <source>Next Image</source>
        <translation>다음 이미지</translation>
    </message>
    <message>
        <source>First Image</source>
        <translation>첫 번째 이미지</translation>
    </message>
    <message>
        <source>Last Image</source>
        <translation>마지막 이미지</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>확대</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>축소</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>창에 맞추기</translation>
    </message>
    <message>
        <source>Actual Size (100 %)</source>
        <extracomment>&quot;100 %&quot; is a zoom percentage; write the percent sign as your language does.</extracomment>
        <translation>실제 크기(100%)</translation>
    </message>
    <message>
        <source>Full Screen</source>
        <translation>전체 화면</translation>
    </message>
    <message>
        <source>Information Panel</source>
        <translation>정보 패널</translation>
    </message>
    <message>
        <source>Information Overlay</source>
        <translation>정보 오버레이</translation>
    </message>
    <message>
        <source>Checkerboard Background</source>
        <translation>체크무늬 배경</translation>
    </message>
    <message>
        <source>Rotate Clockwise</source>
        <translation>시계 방향으로 회전</translation>
    </message>
    <message>
        <source>Rotate Counterclockwise</source>
        <translation>시계 반대 방향으로 회전</translation>
    </message>
    <message>
        <source>Flip Horizontally</source>
        <translation>좌우 반전</translation>
    </message>
    <message>
        <source>Flip Vertically</source>
        <translation>상하 반전</translation>
    </message>
    <message>
        <source>Increase Exposure (+½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>노출 증가(+½ EV)</translation>
    </message>
    <message>
        <source>Decrease Exposure (−½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>노출 감소(−½ EV)</translation>
    </message>
    <message>
        <source>Reset Exposure</source>
        <translation>노출 초기화</translation>
    </message>
    <message>
        <source>Tone Mapping (BT.2390)</source>
        <translation>톤 매핑(BT.2390)</translation>
    </message>
    <message>
        <source>Highlight Altered Pixels</source>
        <translation>변경된 픽셀 강조 표시</translation>
    </message>
    <message>
        <source>Pause Animation</source>
        <translation>애니메이션 일시 정지</translation>
    </message>
    <message>
        <source>Previous Frame</source>
        <translation>이전 프레임</translation>
    </message>
    <message>
        <source>Next Frame</source>
        <translation>다음 프레임</translation>
    </message>
    <message>
        <source>Slideshow</source>
        <translation>슬라이드 쇼</translation>
    </message>
    <message>
        <source>About imageViewer</source>
        <translation>imageViewer 정보</translation>
    </message>
    <message>
        <source>About Qt</source>
        <translation>Qt 정보</translation>
    </message>
    <message>
        <source>Open Recent</source>
        <translation>최근 사용 항목 열기</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>이미지</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>색상 &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Go</source>
        <translation>이동</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
    <message>
        <source>Images (%1);;All files (*)</source>
        <extracomment>File dialog filters: keep &quot;%1&quot;, &quot;;;&quot; and &quot;(*)&quot; exactly.</extracomment>
        <translation>이미지(%1);;모든 파일(*)</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>이미지 열기</translation>
    </message>
    <message>
        <source>Copying the image…</source>
        <translation>이미지를 복사하는 중…</translation>
    </message>
    <message>
        <source>Cannot copy the image.</source>
        <translation>이미지를 복사할 수 없습니다.</translation>
    </message>
    <message>
        <source>Image copied to the clipboard</source>
        <translation>이미지가 클립보드에 복사되었습니다</translation>
    </message>
    <message>
        <source>File path copied to the clipboard</source>
        <translation>파일 경로가 클립보드에 복사되었습니다</translation>
    </message>
    <message>
        <source>Move “%1” to the Recycle Bin?</source>
        <translation>“%1” 파일을 휴지통으로 이동하시겠습니까?</translation>
    </message>
    <message>
        <source>Move “%1” to the trash?</source>
        <translation>“%1” 파일을 휴지통으로 이동하시겠습니까?</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <source>Do not ask again</source>
        <translation>다시 묻지 않음</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the Recycle Bin.</source>
        <translation>“%1” 파일을 휴지통으로 이동할 수 없습니다.</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the trash.</source>
        <translation>“%1” 파일을 휴지통으로 이동할 수 없습니다.</translation>
    </message>
    <message>
        <source>Moved “%1” to the Recycle Bin</source>
        <translation>“%1” 파일을 휴지통으로 이동했습니다</translation>
    </message>
    <message>
        <source>Moved “%1” to the trash</source>
        <translation>“%1” 파일을 휴지통으로 이동했습니다</translation>
    </message>
    <message>
        <source>Delete “%1” permanently?</source>
        <translation>“%1” 파일을 영구적으로 삭제하시겠습니까?</translation>
    </message>
    <message>
        <source>The file does not go to the trash and cannot be restored.</source>
        <translation>이 파일은 휴지통으로 이동하지 않으며 복원할 수 없습니다.</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>삭제</translation>
    </message>
    <message>
        <source>Cannot delete “%1”.</source>
        <translation>“%1” 파일을 삭제할 수 없습니다.</translation>
    </message>
    <message>
        <source>Deleted “%1”</source>
        <translation>“%1” 파일을 삭제했습니다</translation>
    </message>
    <message>
        <source>“%1” is no longer in the trash.</source>
        <translation>“%1” 파일이 더 이상 휴지통에 없습니다.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”: a file with that name exists.</source>
        <translation>“%1” 파일을 복원할 수 없습니다. 같은 이름의 파일이 있습니다.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”.</source>
        <translation>“%1” 파일을 복원할 수 없습니다.</translation>
    </message>
    <message>
        <source>Restored “%1”</source>
        <translation>“%1” 파일을 복원했습니다</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation>이름 바꾸기</translation>
    </message>
    <message>
        <source>New name</source>
        <translation>새 이름</translation>
    </message>
    <message>
        <source>New name:</source>
        <translation>새 이름:</translation>
    </message>
    <message>
        <source>Cannot rename “%1”.</source>
        <translation>“%1” 파일의 이름을 바꿀 수 없습니다.</translation>
    </message>
    <message>
        <source>Renamed to “%1”</source>
        <translation>이름을 바꿨습니다: “%1”</translation>
    </message>
    <message>
        <source>Image viewer with verifiable SDR and HDR color fidelity.</source>
        <translation>SDR 및 HDR 색상 충실도를 검증할 수 있는 이미지 뷰어입니다.</translation>
    </message>
    <message>
        <source>Licensed under the Apache License, Version 2.0.</source>
        <translation>Apache License, Version 2.0에 따라 라이선스가 부여됩니다.</translation>
    </message>
    <message>
        <source>The licenses of the third-party components are in the &lt;i&gt;third-party&lt;/i&gt; folder installed with the application.</source>
        <translation>서드파티 구성 요소의 라이선스는 애플리케이션과 함께 설치되는 &lt;i&gt;third-party&lt;/i&gt; 폴더에 있습니다.</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <source>Animation paused</source>
        <translation>애니메이션 일시 정지됨</translation>
    </message>
    <message>
        <source>Animation playing</source>
        <translation>애니메이션 재생 중</translation>
    </message>
    <message>
        <source>Slideshow: a new image every %1 s</source>
        <extracomment>%1: seconds between images, e.g. &quot;5&quot;.</extracomment>
        <translation>슬라이드 쇼: %1초마다 새 이미지</translation>
    </message>
</context>
</TS>
