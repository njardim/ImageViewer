<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ar" sourcelanguage="en">
<context>
    <name>Color</name>
    <message>
        <source>invalid ICC profile</source>
        <translation>ملف تعريف ICC غير صالح</translation>
    </message>
    <message>
        <source>unsupported ICC color space for RGBA data</source>
        <translation>مساحة ألوان ICC غير مدعومة لبيانات RGBA</translation>
    </message>
    <message>
        <source>cannot build a color transform from the ICC profile</source>
        <translation>تعذر إنشاء تحويل ألوان من ملف تعريف ICC</translation>
    </message>
</context>
<context>
    <name>Image</name>
    <message>
        <source>image too large for the available memory (needs %1 GB, limit %2 GB)</source>
        <extracomment>GB: gigabytes.</extracomment>
        <translation>الصورة كبيرة جداً بالنسبة إلى الذاكرة المتاحة (تحتاج إلى %1 غيغابايت، والحد الأقصى %2 غيغابايت)</translation>
    </message>
    <message>
        <source>(no description)</source>
        <translation>(لا يوجد وصف)</translation>
    </message>
    <message>
        <source>linear, chromaticities from the file</source>
        <translation>خطي، الإحداثيات اللونية من الملف</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed: the file&apos;s chromaticities are invalid)</source>
        <translation>BT.709 خطي (مفترض: الإحداثيات اللونية في الملف غير صالحة)</translation>
    </message>
    <message>
        <source>%1 (assigned by the decoder)</source>
        <translation>%1 (حددته وحدة فك الترميز)</translation>
    </message>
    <message>
        <source>linear BT.709 (assumed)</source>
        <translation>BT.709 خطي (مفترض)</translation>
    </message>
    <message>
        <source>sRGB (assumed)</source>
        <translation>sRGB (مفترض)</translation>
    </message>
    <message>
        <source>invalid dimensions (%1×%2×%3)</source>
        <translation>أبعاد غير صالحة (%1×%2×%3)</translation>
    </message>
    <message>
        <source>%1 (file metadata, via Qt)</source>
        <translation>%1 (بيانات تعريف الملف، عبر Qt)</translation>
    </message>
    <message>
        <source>SVG is only decoded in the graphical interface</source>
        <translation>لا يتم فك ترميز SVG إلا في الواجهة الرسومية</translation>
    </message>
    <message>
        <source>damaged, truncated or unsupported %1 file</source>
        <translation>ملف %1 تالف أو مقطوع أو غير مدعوم</translation>
    </message>
    <message>
        <source>custom primaries</source>
        <translation>ألوان أساسية مخصصة</translation>
    </message>
    <message>
        <source>Cannot decode: %1</source>
        <translation>تعذر فك الترميز: %1</translation>
    </message>
    <message>
        <source>%1 (assumed: %2)</source>
        <translation>%1 (مفترض: %2)</translation>
    </message>
    <message>
        <source>Not enough memory to decode the image.</source>
        <translation>لا تتوفر ذاكرة كافية لفك ترميز الصورة.</translation>
    </message>
    <message>
        <source>Decoding error: %1</source>
        <translation>خطأ في فك الترميز: %1</translation>
    </message>
    <message>
        <source>HEIC images need Microsoft&apos;s “HEIF Image Extensions” and “HEVC Video Extensions”, from the Microsoft Store.</source>
        <translation>تحتاج صور HEIC إلى إضافتي Microsoft ‏«HEIF Image Extensions» و«HEVC Video Extensions» من Microsoft Store.</translation>
    </message>
    <message>
        <source>HEIC images need an HEVC decoder, which imageViewer does not include on this system (patents); convert them to another format first.</source>
        <translation>تحتاج صور HEIC إلى مفكّك ترميز HEVC، لا يتضمّنه imageViewer على هذا النظام (براءات اختراع)؛ حوّلها أولاً إلى تنسيق آخر.</translation>
    </message>
    <message>
        <source>the decoder took longer than %1 s and was stopped</source>
        <extracomment>%1: seconds, e.g. &quot;30&quot;.</extracomment>
        <translation>استغرق مفكّك الترميز أكثر من %1 ث فأُوقف</translation>
    </message>
</context>
<context>
    <name>Overlay</name>
    <message>
        <source>File name</source>
        <translation>اسم الملف</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>الأبعاد</translation>
    </message>
    <message>
        <source>File size</source>
        <translation>حجم الملف</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>التكبير</translation>
    </message>
    <message>
        <source>Color space</source>
        <translation>مساحة الألوان</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>تاريخ التعديل</translation>
    </message>
    <message>
        <source>Position in the folder</source>
        <translation>الموضع في المجلد</translation>
    </message>
    <message>
        <source>Display output</source>
        <translation>إخراج الشاشة</translation>
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
        <translation>EDR · هامش السطوع %1×</translation>
    </message>
    <message>
        <source>Linear sRGB managed by ColorSync · no HDR headroom</source>
        <translation>sRGB خطي يديره ColorSync · بلا هامش سطوع HDR</translation>
    </message>
    <message>
        <source>scRGB · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>scRGB · أبيض SDR %1 nits · الذروة %2 nits</translation>
    </message>
    <message>
        <source>HDR10 (PQ) · SDR white %1 nits · peak %2 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>HDR10 (PQ) · أبيض SDR %1 nits · الذروة %2 nits</translation>
    </message>
    <message>
        <source>Cannot initialize the GPU (QRhi).</source>
        <translation>تعذرت تهيئة GPU (QRhi).</translation>
    </message>
    <message>
        <source>The GPU does not support RGBA16F textures.</source>
        <translation>لا يدعم GPU خامات RGBA16F.</translation>
    </message>
    <message>
        <source>Cannot create GPU resources.</source>
        <translation>تعذر إنشاء موارد GPU.</translation>
    </message>
    <message>
        <source>Cannot create the swapchain.</source>
        <translation>تعذر إنشاء سلسلة التبديل (swapchain).</translation>
    </message>
    <message>
        <source>(Qt defaults, not measured)</source>
        <translation>(القيم الافتراضية لـ Qt، غير مقيسة)</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <source>Settings</source>
        <translation>الإعدادات</translation>
    </message>
    <message>
        <source>System default</source>
        <translation>الإعداد الافتراضي للنظام</translation>
    </message>
    <message>
        <source>Language:</source>
        <translation>اللغة:</translation>
    </message>
    <message>
        <source>Languages other than English are machine translations awaiting review by native speakers.</source>
        <translation>اللغات الأخرى غير الإنجليزية ترجمات آلية بانتظار مراجعة الناطقين الأصليين بها.</translation>
    </message>
    <message>
        <source>Reopen the last image at startup</source>
        <translation>إعادة فتح آخر صورة عند بدء التشغيل</translation>
    </message>
    <message>
        <source>General</source>
        <translation>عام</translation>
    </message>
    <message>
        <source>Black</source>
        <translation>أسود</translation>
    </message>
    <message>
        <source>Dark gray</source>
        <translation>رمادي داكن</translation>
    </message>
    <message>
        <source>Gray</source>
        <translation>رمادي</translation>
    </message>
    <message>
        <source>Light gray</source>
        <translation>رمادي فاتح</translation>
    </message>
    <message>
        <source>White</source>
        <translation>أبيض</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>مخصص…</translation>
    </message>
    <message>
        <source>Background:</source>
        <translation>الخلفية:</translation>
    </message>
    <message>
        <source>Remember the window size and position</source>
        <translation>تذكر حجم النافذة وموضعها</translation>
    </message>
    <message>
        <source>Confirm before moving an image to the trash</source>
        <translation>التأكيد قبل نقل صورة إلى سلة المهملات</translation>
    </message>
    <message>
        <source>Show a checkerboard behind transparent areas</source>
        <translation>إظهار نمط رقعة الشطرنج خلف المناطق الشفافة</translation>
    </message>
    <message>
        <source>Window</source>
        <translation>النافذة</translation>
    </message>
    <message>
        <source>Show the information panel (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;I&quot;.</extracomment>
        <translation>إظهار لوحة المعلومات (%1)</translation>
    </message>
    <message>
        <source>Overlay at the top (%1)</source>
        <extracomment>%1: the keyboard shortcut, e.g. &quot;Shift+I&quot;.</extracomment>
        <translation>التراكب في الأعلى (%1)</translation>
    </message>
    <message>
        <source>Always</source>
        <translation>دائماً</translation>
    </message>
    <message>
        <source>When the pointer is at the top</source>
        <translation>عندما يكون المؤشر في الأعلى</translation>
    </message>
    <message>
        <source>Never</source>
        <translation>أبداً</translation>
    </message>
    <message>
        <source>In full screen:</source>
        <translation>في وضع ملء الشاشة:</translation>
    </message>
    <message>
        <source>In a window:</source>
        <translation>في نافذة:</translation>
    </message>
    <message>
        <source>Fields</source>
        <translation>الحقول</translation>
    </message>
    <message>
        <source>Move Up</source>
        <translation>نقل لأعلى</translation>
    </message>
    <message>
        <source>Move Down</source>
        <translation>نقل لأسفل</translation>
    </message>
    <message>
        <source>Fields:</source>
        <translation>الحقول:</translation>
    </message>
    <message>
        <source> %</source>
        <extracomment>Unit after a percentage; keep the leading space if your language separates it.</extracomment>
        <translation> %</translation>
    </message>
    <message>
        <source>Background opacity:</source>
        <translation>عتامة الخلفية:</translation>
    </message>
    <message>
        <source>Text opacity:</source>
        <translation>عتامة النص:</translation>
    </message>
    <message>
        <source>Outline the text</source>
        <translation>إضافة حد خارجي للنص</translation>
    </message>
    <message>
        <source> s</source>
        <extracomment>Unit after a number of seconds; keep the leading space if your language separates units.</extracomment>
        <translation> ث</translation>
    </message>
    <message>
        <source>Hide after:</source>
        <translation>الإخفاء بعد:</translation>
    </message>
    <message>
        <source>Appearance of the panel and the overlay</source>
        <translation>مظهر اللوحة والتراكب</translation>
    </message>
    <message>
        <source>Information</source>
        <translation>المعلومات</translation>
    </message>
    <message>
        <source>After the last image, continue with the first</source>
        <translation>بعد آخر صورة، المتابعة من الصورة الأولى</translation>
    </message>
    <message>
        <source>Click the left or right side of the window for the previous or next image</source>
        <translation>النقر على الجانب الأيسر أو الأيمن من النافذة لعرض الصورة السابقة أو التالية</translation>
    </message>
    <message>
        <source> px</source>
        <extracomment>Unit after a number of pixels; keep the leading space if your language separates units.</extracomment>
        <translation> px</translation>
    </message>
    <message>
        <source>Width of each side:</source>
        <translation>عرض كل جانب:</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>الاسم</translation>
    </message>
    <message>
        <source>Date modified</source>
        <translation>تاريخ التعديل</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>الحجم</translation>
    </message>
    <message>
        <source>Descending</source>
        <translation>تنازلي</translation>
    </message>
    <message>
        <source>Sort images by:</source>
        <translation>ترتيب الصور حسب:</translation>
    </message>
    <message>
        <source>Load the next and previous images in advance</source>
        <translation>تحميل الصورة التالية والسابقة مسبقاً</translation>
    </message>
    <message>
        <source>Slideshow (%1), time per image:</source>
        <extracomment>%1: the key that starts and stops the slideshow, e.g. &quot;S&quot;.</extracomment>
        <translation>عرض الشرائح (%1)، الوقت لكل صورة:</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>التنقل</translation>
    </message>
    <message>
        <source>Automatic (HDR when the display supports it)</source>
        <translation>تلقائي (HDR عندما تدعمه الشاشة)</translation>
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
        <translation>إخراج الشاشة:</translation>
    </message>
    <message>
        <source>Tone map HDR images that exceed the display (ITU-R BT.2390)</source>
        <translation>تطبيق تعيين الدرجات اللونية على صور HDR التي تتجاوز قدرة الشاشة (ITU-R BT.2390)</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>الألوان &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>موافق</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>إلغاء الأمر</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>تطبيق</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>استعادة الإعدادات الافتراضية</translation>
    </message>
    <message>
        <source>Background Color</source>
        <translation>لون الخلفية</translation>
    </message>
</context>
<context>
    <name>ViewerWindow</name>
    <message>
        <source>File not found: %1</source>
        <translation>لم يتم العثور على الملف: %1</translation>
    </message>
    <message>
        <source>The folder contains no supported images.</source>
        <translation>لا يحتوي المجلد على أي صور مدعومة.</translation>
    </message>
    <message>
        <source>Loading %1…</source>
        <translation>جارٍ تحميل %1…</translation>
    </message>
    <message>
        <source>This is the last image.</source>
        <translation>هذه هي الصورة الأخيرة.</translation>
    </message>
    <message>
        <source>This is the first image.</source>
        <translation>هذه هي الصورة الأولى.</translation>
    </message>
    <message>
        <source>Reducing the image to fit the GPU (at most %1 px)…</source>
        <extracomment>%1: a size in pixels.</extracomment>
        <translation>جارٍ تصغير الصورة لتلائم GPU (%1 px كحد أقصى)…</translation>
    </message>
    <message>
        <source>The GPU did not accept the image.</source>
        <translation>لم يقبل GPU الصورة.</translation>
    </message>
    <message>
        <source>Slideshow stopped</source>
        <translation>توقف عرض الشرائح</translation>
    </message>
    <message>
        <source>File</source>
        <translation>الملف</translation>
    </message>
    <message>
        <source>Folder</source>
        <translation>المجلد</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>الحجم</translation>
    </message>
    <message>
        <source>Modified</source>
        <translation>آخر تعديل</translation>
    </message>
    <message>
        <source>Position</source>
        <translation>الموضع</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <extracomment>Position of the image in its folder, e.g. &quot;3 of 120&quot;.</extracomment>
        <translation>%1 من %2</translation>
    </message>
    <message>
        <source>%1 MP</source>
        <extracomment>Megapixels, e.g. &quot;24.0 MP&quot;.</extracomment>
        <translation>%1 ميغابكسل</translation>
    </message>
    <message>
        <source>reduced to %1 × %2 for the GPU</source>
        <translation>تم التصغير إلى %1 × %2 لأجل GPU</translation>
    </message>
    <message>
        <source>Dimensions</source>
        <translation>الأبعاد</translation>
    </message>
    <message>
        <source>floating point</source>
        <translation>فاصلة عائمة</translation>
    </message>
    <message>
        <source>alpha</source>
        <translation>ألفا</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>الصيغة</translation>
    </message>
    <message>
        <source>frame %1 of %2</source>
        <extracomment>The frame of an animation on screen, e.g. &quot;frame 3 of 24&quot;.</extracomment>
        <translation>الإطار %1 من %2</translation>
    </message>
    <message>
        <source>frame %1</source>
        <translation>الإطار %1</translation>
    </message>
    <message>
        <source>paused</source>
        <translation>متوقف مؤقتًا</translation>
    </message>
    <message>
        <source>Animation</source>
        <translation>الرسوم المتحركة</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>الاتجاه</translation>
    </message>
    <message>
        <source>EXIF %1, applied</source>
        <extracomment>The EXIF orientation tag (2 to 8) of the file, already applied to the image.</extracomment>
        <translation>EXIF %1، مطبق</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>اللون</translation>
    </message>
    <message>
        <source>Peak</source>
        <translation>الذروة</translation>
    </message>
    <message>
        <source>%1× SDR white (≈%2 nits)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>%1× أبيض SDR (≈%2 nits)</translation>
    </message>
    <message>
        <source>Decoded in</source>
        <translation>تم فك الترميز خلال</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <extracomment>Unit after a duration in milliseconds.</extracomment>
        <translation>%1 مللي ثانية</translation>
    </message>
    <message>
        <source>Camera</source>
        <translation>الكاميرا</translation>
    </message>
    <message>
        <source>Lens</source>
        <translation>العدسة</translation>
    </message>
    <message>
        <source>%1 s</source>
        <extracomment>Exposure time of a photograph, e.g. &quot;1/250 s&quot;.</extracomment>
        <translation>%1 ث</translation>
    </message>
    <message>
        <source>%1 mm</source>
        <extracomment>Focal length of the lens, e.g. &quot;50 mm&quot;.</extracomment>
        <translation>%1 مم</translation>
    </message>
    <message>
        <source>Exposure</source>
        <extracomment>Label of the photograph&apos;s shooting settings: exposure time, aperture, ISO, focal length.</extracomment>
        <translation>التعريض</translation>
    </message>
    <message>
        <source>Taken</source>
        <extracomment>Label of the date the photograph was taken.</extracomment>
        <translation>تاريخ الالتقاط</translation>
    </message>
    <message>
        <source>%1 %</source>
        <extracomment>A zoom percentage, e.g. &quot;100 %&quot;; write the percent sign as your language does.</extracomment>
        <translation>%1 %</translation>
    </message>
    <message>
        <source>rotated %1°</source>
        <extracomment>The view is rotated clockwise by this many degrees.</extracomment>
        <translation>مدوَّرة %1°</translation>
    </message>
    <message>
        <source>mirrored</source>
        <translation>معكوسة</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>الإخراج</translation>
    </message>
    <message>
        <source>BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>تعيين الدرجات اللونية BT.2390 من %1 إلى %2 nits، دون تغيير حتى %3 nits</translation>
    </message>
    <message>
        <source>clipped above %1 nits</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>مقصوص فوق %1 nits</translation>
    </message>
    <message>
        <source>Highlights</source>
        <translation>الإضاءات العالية</translation>
    </message>
    <message>
        <source>clipped above %1 nits (colors outside the output gamut)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>مقصوص فوق %1 nits (ألوان خارج نطاق ألوان الإخراج)</translation>
    </message>
    <message>
        <source>clipped above %1 nits (tone mapping off)</source>
        <extracomment>nits: candela per square metre, the unit of luminance.</extracomment>
        <translation>مقصوص فوق %1 nits (تعيين الدرجات اللونية متوقف)</translation>
    </message>
    <message>
        <source>exposure %1 EV</source>
        <extracomment>The viewer&apos;s exposure adjustment in EV (photographic stops), e.g. &quot;exposure +1.5 EV&quot;.</extracomment>
        <translation>التعريض %1 EV</translation>
    </message>
    <message>
        <source>altered pixels highlighted</source>
        <translation>وحدات البكسل المعدلة مميزة</translation>
    </message>
    <message>
        <source>%1-bit</source>
        <extracomment>Bits per channel of the image file, e.g. &quot;16-bit&quot;.</extracomment>
        <translation>%1 بت</translation>
    </message>
    <message>
        <source>Enter a name.</source>
        <translation>أدخل اسماً.</translation>
    </message>
    <message>
        <source>This name is not allowed.</source>
        <translation>هذا الاسم غير مسموح به.</translation>
    </message>
    <message>
        <source>A name cannot contain “/” or “\”.</source>
        <translation>لا يمكن أن يحتوي الاسم على «/» أو «\».</translation>
    </message>
    <message>
        <source>The name is too long.</source>
        <translation>الاسم طويل جداً.</translation>
    </message>
    <message>
        <source>Windows does not allow this name.</source>
        <extracomment>Windows forbids &lt; &gt; : &quot; | ? *, control characters, device names such as CON, and a final dot or space.</extracomment>
        <translation>لا يسمح Windows بهذا الاسم.</translation>
    </message>
    <message>
        <source>A file with this name already exists.</source>
        <translation>يوجد ملف بهذا الاسم بالفعل.</translation>
    </message>
    <message>
        <source>Open…</source>
        <translation>فتح…</translation>
    </message>
    <message>
        <source>Clear Menu</source>
        <extracomment>Empties the Open Recent menu.</extracomment>
        <translation>مسح القائمة</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>إظهار في مستكشف الملفات</translation>
    </message>
    <message>
        <source>Move to Recycle Bin…</source>
        <translation>نقل إلى سلة المحذوفات…</translation>
    </message>
    <message>
        <source>Move to Recycle Bin</source>
        <translation>نقل إلى سلة المحذوفات</translation>
    </message>
    <message>
        <source>Undo Move to Recycle Bin</source>
        <translation>التراجع عن النقل إلى سلة المحذوفات</translation>
    </message>
    <message>
        <source>Show in Finder</source>
        <translation>إظهار في Finder</translation>
    </message>
    <message>
        <source>Move to Trash…</source>
        <translation>نقل إلى سلة المهملات…</translation>
    </message>
    <message>
        <source>Move to Trash</source>
        <translation>نقل إلى سلة المهملات</translation>
    </message>
    <message>
        <source>Undo Move to Trash</source>
        <translation>التراجع عن النقل إلى سلة المهملات</translation>
    </message>
    <message>
        <source>Show in File Manager</source>
        <translation>إظهار في مدير الملفات</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>إعادة تسمية…</translation>
    </message>
    <message>
        <source>Delete Permanently…</source>
        <translation>حذف نهائي…</translation>
    </message>
    <message>
        <source>Copy Image</source>
        <translation>نسخ الصورة</translation>
    </message>
    <message>
        <source>Copy File Path</source>
        <translation>نسخ مسار الملف</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>الإعدادات…</translation>
    </message>
    <message>
        <source>Quit</source>
        <translation>إنهاء</translation>
    </message>
    <message>
        <source>Previous Image</source>
        <translation>الصورة السابقة</translation>
    </message>
    <message>
        <source>Next Image</source>
        <translation>الصورة التالية</translation>
    </message>
    <message>
        <source>First Image</source>
        <translation>الصورة الأولى</translation>
    </message>
    <message>
        <source>Last Image</source>
        <translation>الصورة الأخيرة</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>تكبير</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>تصغير</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>ملاءمة النافذة</translation>
    </message>
    <message>
        <source>Actual Size (100 %)</source>
        <extracomment>&quot;100 %&quot; is a zoom percentage; write the percent sign as your language does.</extracomment>
        <translation>الحجم الفعلي (100 %)</translation>
    </message>
    <message>
        <source>Full Screen</source>
        <translation>ملء الشاشة</translation>
    </message>
    <message>
        <source>Information Panel</source>
        <translation>لوحة المعلومات</translation>
    </message>
    <message>
        <source>Information Overlay</source>
        <translation>تراكب المعلومات</translation>
    </message>
    <message>
        <source>Checkerboard Background</source>
        <translation>خلفية رقعة الشطرنج</translation>
    </message>
    <message>
        <source>Rotate Clockwise</source>
        <translation>تدوير باتجاه عقارب الساعة</translation>
    </message>
    <message>
        <source>Rotate Counterclockwise</source>
        <translation>تدوير عكس اتجاه عقارب الساعة</translation>
    </message>
    <message>
        <source>Flip Horizontally</source>
        <translation>قلب أفقياً</translation>
    </message>
    <message>
        <source>Flip Vertically</source>
        <translation>قلب عمودياً</translation>
    </message>
    <message>
        <source>Increase Exposure (+½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>زيادة التعريض (+½ EV)</translation>
    </message>
    <message>
        <source>Decrease Exposure (−½ EV)</source>
        <extracomment>EV: exposure value, photographic stops; ½ EV is half a stop.</extracomment>
        <translation>تقليل التعريض (−½ EV)</translation>
    </message>
    <message>
        <source>Reset Exposure</source>
        <translation>إعادة تعيين التعريض</translation>
    </message>
    <message>
        <source>Tone Mapping (BT.2390)</source>
        <translation>تعيين الدرجات اللونية (BT.2390)</translation>
    </message>
    <message>
        <source>Highlight Altered Pixels</source>
        <translation>تمييز وحدات البكسل المعدلة</translation>
    </message>
    <message>
        <source>Pause Animation</source>
        <translation>إيقاف الرسوم المتحركة مؤقتًا</translation>
    </message>
    <message>
        <source>Previous Frame</source>
        <translation>الإطار السابق</translation>
    </message>
    <message>
        <source>Next Frame</source>
        <translation>الإطار التالي</translation>
    </message>
    <message>
        <source>Slideshow</source>
        <translation>عرض الشرائح</translation>
    </message>
    <message>
        <source>About imageViewer</source>
        <translation>حول imageViewer</translation>
    </message>
    <message>
        <source>About Qt</source>
        <translation>حول Qt</translation>
    </message>
    <message>
        <source>Open Recent</source>
        <translation>فتح الأخيرة</translation>
    </message>
    <message>
        <source>Delete “%1” permanently?</source>
        <translation>هل تريد حذف «%1» نهائياً؟</translation>
    </message>
    <message>
        <source>The file does not go to the trash and cannot be restored.</source>
        <translation>لن يُنقل الملف إلى سلة المهملات ولا يمكن استعادته.</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>حذف</translation>
    </message>
    <message>
        <source>Cannot delete “%1”.</source>
        <translation>تعذر حذف «%1».</translation>
    </message>
    <message>
        <source>Deleted “%1”</source>
        <translation>تم حذف «%1»</translation>
    </message>
    <message>
        <source>“%1” is no longer in the trash.</source>
        <translation>لم يعد «%1» في سلة المهملات.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”: a file with that name exists.</source>
        <translation>تعذرت استعادة «%1»: يوجد ملف بهذا الاسم.</translation>
    </message>
    <message>
        <source>Cannot restore “%1”.</source>
        <translation>تعذرت استعادة «%1».</translation>
    </message>
    <message>
        <source>Restored “%1”</source>
        <translation>تمت استعادة «%1»</translation>
    </message>
    <message>
        <source>Rename</source>
        <translation>إعادة تسمية</translation>
    </message>
    <message>
        <source>New name</source>
        <translation>الاسم الجديد</translation>
    </message>
    <message>
        <source>New name:</source>
        <translation>الاسم الجديد:</translation>
    </message>
    <message>
        <source>Cannot rename “%1”.</source>
        <translation>تعذرت إعادة تسمية «%1».</translation>
    </message>
    <message>
        <source>Renamed to “%1”</source>
        <translation>تمت إعادة التسمية إلى «%1»</translation>
    </message>
    <message>
        <source>View</source>
        <translation>عرض</translation>
    </message>
    <message>
        <source>Image</source>
        <translation>الصورة</translation>
    </message>
    <message>
        <source>Color &amp;&amp; HDR</source>
        <extracomment>&quot;&amp;&amp;&quot; is shown as a single &quot;&amp;&quot;.</extracomment>
        <translation>الألوان &amp;&amp; HDR</translation>
    </message>
    <message>
        <source>Go</source>
        <translation>انتقال</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>تعليمات</translation>
    </message>
    <message>
        <source>Images (%1);;All files (*)</source>
        <extracomment>File dialog filters: keep &quot;%1&quot;, &quot;;;&quot; and &quot;(*)&quot; exactly.</extracomment>
        <translation>الصور (%1);;كافة الملفات (*)</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>فتح صورة</translation>
    </message>
    <message>
        <source>Copying the image…</source>
        <translation>جارٍ نسخ الصورة…</translation>
    </message>
    <message>
        <source>Cannot copy the image.</source>
        <translation>تعذر نسخ الصورة.</translation>
    </message>
    <message>
        <source>Image copied to the clipboard</source>
        <translation>تم نسخ الصورة إلى الحافظة</translation>
    </message>
    <message>
        <source>File path copied to the clipboard</source>
        <translation>تم نسخ مسار الملف إلى الحافظة</translation>
    </message>
    <message>
        <source>Move “%1” to the Recycle Bin?</source>
        <translation>هل تريد نقل «%1» إلى سلة المحذوفات؟</translation>
    </message>
    <message>
        <source>Move “%1” to the trash?</source>
        <translation>هل تريد نقل «%1» إلى سلة المهملات؟</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>إلغاء الأمر</translation>
    </message>
    <message>
        <source>Do not ask again</source>
        <translation>عدم السؤال مرة أخرى</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the Recycle Bin.</source>
        <translation>تعذر نقل «%1» إلى سلة المحذوفات.</translation>
    </message>
    <message>
        <source>Cannot move “%1” to the trash.</source>
        <translation>تعذر نقل «%1» إلى سلة المهملات.</translation>
    </message>
    <message>
        <source>Moved “%1” to the Recycle Bin</source>
        <translation>تم نقل «%1» إلى سلة المحذوفات</translation>
    </message>
    <message>
        <source>Moved “%1” to the trash</source>
        <translation>تم نقل «%1» إلى سلة المهملات</translation>
    </message>
    <message>
        <source>No images left in this folder.</source>
        <translation>لم تعد هناك صور في هذا المجلد.</translation>
    </message>
    <message>
        <source>Image viewer with verifiable SDR and HDR color fidelity.</source>
        <translation>عارض صور بدقة ألوان SDR وHDR قابلة للتحقق.</translation>
    </message>
    <message>
        <source>Licensed under the Apache License, Version 2.0.</source>
        <translation>مرخص بموجب Apache License, Version 2.0.</translation>
    </message>
    <message>
        <source>The licenses of the third-party components are in the &lt;i&gt;third-party&lt;/i&gt; folder installed with the application.</source>
        <translation>توجد تراخيص مكونات الجهات الخارجية في المجلد &lt;i&gt;third-party&lt;/i&gt; المثبت مع التطبيق.</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>موافق</translation>
    </message>
    <message>
        <source>Animation paused</source>
        <translation>الرسوم المتحركة متوقفة مؤقتًا</translation>
    </message>
    <message>
        <source>Animation playing</source>
        <translation>الرسوم المتحركة قيد التشغيل</translation>
    </message>
    <message>
        <source>Slideshow: a new image every %1 s</source>
        <extracomment>%1: seconds between images, e.g. &quot;5&quot;.</extracomment>
        <translation>عرض الشرائح: صورة جديدة كل %1 ث</translation>
    </message>
</context>
</TS>
