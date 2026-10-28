//! ImageViewer's SVG renderer contract (`imageviewer_svg.h`, decision D-90), implemented with resvg.
//!
//! Why our own layer and not resvg's C API: resvg's C API builds `usvg::Options::default()`, whose
//! image resolver reads any file an `<image>` names (on Windows, `\\server\share` too, which hands
//! the user's credentials to that server), and an `assert` or `unwrap` there aborts the process.
//! Here the resolver accepts embedded data only, every call catches a panic and returns a code,
//! and the caller sees six plain C functions that another renderer could implement as well.
//!
//! Everything of resvg's API used below, the list to check at an update (an update whose API
//! differs fails to compile in its own CI, so the pinned release stays in use):
//! - `usvg::Options`: `default()`, `resources_dir`, `image_href_resolver`, `fontdb_mut()`
//! - `usvg::ImageHrefResolver { resolve_data, resolve_string }` and its `default_data_resolver()`
//! - `usvg::fontdb::Database::load_system_fonts()`
//! - `usvg::Tree::from_data()`, `Tree::size()`
//! - `resvg::render()`, `tiny_skia::PixmapMut::from_bytes()`, `tiny_skia::Transform::from_row()`
//!
//! The crate's version is the resvg release it wraps (Cargo.toml pins it), and names it in
//! `iv_svg_renderer()`.

use resvg::{tiny_skia, usvg};
use std::ffi::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};

const CONTRACT_VERSION: i32 = 1; // IV_SVG_CONTRACT_VERSION
const OK: i32 = 0;
const BAD_ARGUMENT: i32 = 1;
const UNREADABLE: i32 = 2;
const FAILED: i32 = 3;

/// A parsed document (`iv_svg` in the header).
pub struct Document {
    tree: usvg::Tree,
}

/// Runs `body`, turning a panic into `FAILED`: nothing unwinds into C.
fn guarded(body: impl FnOnce() -> i32) -> i32 {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or(FAILED)
}

#[unsafe(no_mangle)]
pub extern "C" fn iv_svg_contract_version() -> i32 {
    CONTRACT_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn iv_svg_renderer() -> *const c_char {
    concat!("resvg ", env!("CARGO_PKG_VERSION"), "\0").as_ptr().cast()
}

/// # Safety
/// `data` points to `size` readable bytes; `out` to a writable pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iv_svg_parse(data: *const u8, size: usize, load_fonts: i32, out: *mut *mut Document) -> i32 {
    guarded(|| {
        if data.is_null() || out.is_null() || size == 0 {
            return BAD_ARGUMENT;
        }
        // SAFETY: the caller's contract above.
        let bytes = unsafe { std::slice::from_raw_parts(data, size) };
        let mut options = usvg::Options::default();
        // Nothing outside the document: no directory for relative paths, and an image is an
        // embedded one (data: URL) or none, whatever its href names.
        options.resources_dir = None;
        options.image_href_resolver = usvg::ImageHrefResolver {
            resolve_data: usvg::ImageHrefResolver::default_data_resolver(),
            resolve_string: Box::new(|_, _| None),
        };
        if load_fonts != 0 {
            options.fontdb_mut().load_system_fonts();
        }
        match usvg::Tree::from_data(bytes, &options) {
            Ok(tree) => {
                // SAFETY: `out` is writable (checked above for null).
                unsafe { *out = Box::into_raw(Box::new(Document { tree })) };
                OK
            }
            Err(_) => UNREADABLE,
        }
    })
}

/// # Safety
/// `svg` comes from `iv_svg_parse()`; `width` and `height` are writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iv_svg_size(svg: *const Document, width: *mut f64, height: *mut f64) -> i32 {
    guarded(|| {
        if svg.is_null() || width.is_null() || height.is_null() {
            return BAD_ARGUMENT;
        }
        // SAFETY: the caller's contract above.
        let size = unsafe { &*svg }.tree.size(); // usvg guarantees both sides > 0
        unsafe {
            *width = f64::from(size.width());
            *height = f64::from(size.height());
        }
        OK
    })
}

/// # Safety
/// `svg` comes from `iv_svg_parse()`; `pixels` points to `width` * `height` * 4 writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iv_svg_render(svg: *const Document, scale: f64, left: f64, top: f64, width: u32, height: u32,
                                       pixels: *mut u8) -> i32 {
    guarded(|| {
        let Some(bytes) = (width as usize).checked_mul(height as usize).and_then(|n| n.checked_mul(4)) else {
            return BAD_ARGUMENT;
        };
        if svg.is_null() || pixels.is_null() || bytes == 0 || !(scale.is_finite() && scale > 0.0)
            || !left.is_finite() || !top.is_finite() {
            return BAD_ARGUMENT;
        }
        // SAFETY: the caller's contract above.
        let buffer = unsafe { std::slice::from_raw_parts_mut(pixels, bytes) };
        buffer.fill(0); // resvg draws over what is there: transparent first
        // None when tiny-skia cannot address it (a row of more than i32::MAX bytes).
        let Some(mut pixmap) = tiny_skia::PixmapMut::from_bytes(buffer, width, height) else {
            return BAD_ARGUMENT;
        };
        let (scale, left, top) = (scale as f32, left as f32, top as f32);
        let transform = tiny_skia::Transform::from_row(scale, 0.0, 0.0, scale, -left, -top);
        resvg::render(&unsafe { &*svg }.tree, transform, &mut pixmap);
        OK
    })
}

/// # Safety
/// `svg` comes from `iv_svg_parse()` and is not used afterwards, or is null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iv_svg_free(svg: *mut Document) {
    if !svg.is_null() {
        // A panic while dropping would unwind into C: caught, and the document leaked instead.
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(svg) })));
    }
}
