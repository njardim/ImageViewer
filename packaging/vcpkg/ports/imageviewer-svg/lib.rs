//! ImageViewer's SVG renderer contract (`imageviewer_svg.h`, decision D-90), implemented with resvg.
//!
//! Why our own layer and not resvg's C API: resvg's C API builds `usvg::Options::default()`, whose
//! image resolver reads any file an `<image>` names (on Windows, `\\server\share` too, which hands
//! the user's credentials to that server), and an `assert` or `unwrap` there aborts the process.
//! Here the resolver accepts embedded data only, every call catches a panic and returns a code, the
//! memory resvg allocates has a limit (a document can declare a filter region many times its canvas,
//! and resvg allocates its layers whole), the generic font families are installed fonts (fontdb may
//! name fonts the system does not have, and the text then vanishes), and the caller sees eight plain
//! C functions that another renderer could implement as well.
//!
//! Everything of resvg's API used below, the list to check at an update (an update whose API
//! differs fails to compile in its own CI, so the pinned release stays in use):
//! - `usvg::Options`: `default()`, `resources_dir`, `image_href_resolver`, `fontdb`
//! - `usvg::ImageHrefResolver { resolve_data, resolve_string }` and its `default_data_resolver()`
//! - `usvg::fontdb::Database`: `new()`, `load_system_fonts()`, `faces()` (`families`, `style`, `weight`,
//!   `monospaced`), `family_name()`, `set_serif_family()` and the four other generic families
//! - Linux: `fontconfig_parser::FontConfig` (`merge_config()`, `aliases`), the crate fontdb reads the
//!   system's font configuration with, at the version resvg's own lock holds
//! - `usvg::Tree::from_data()`, `Tree::size()`, `Tree::root()`, `Group::children()`, `Group::filters()`,
//!   `Group::abs_transform()`, `Node::subroots()`, `filter::Filter::primitives()`,
//!   `filter::Primitive::kind()`, `filter::Turbulence::stitch_tiles()` and the sizes of
//!   `filter::GaussianBlur`, `DropShadow`, `Offset`, `Morphology` and `DisplacementMap`
//! - `resvg::render()`, `tiny_skia::PixmapMut::from_bytes()`, `tiny_skia::Transform::from_row()`
//!
//! The crate's version is the resvg release it wraps (Cargo.toml pins it), and names it in
//! `iv_svg_renderer()`.

use resvg::{tiny_skia, usvg};
use std::alloc::{GlobalAlloc, Layout, System};
use std::ffi::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicUsize, Ordering};

const CONTRACT_VERSION: i32 = 2; // IV_SVG_CONTRACT_VERSION
const OK: i32 = 0;
const BAD_ARGUMENT: i32 = 1;
const UNREADABLE: i32 = 2;
const FAILED: i32 = 3;

/// The memory of everything in this library (resvg's, its crates', Rust's own), counted against
/// `iv_svg_limit_memory()`; the application's C++ allocates through its own allocator. Past the
/// limit an allocation fails and Rust ends the process ("memory allocation of N bytes failed"):
/// a global allocator may not unwind, so there is no error code to return from there.
struct Limited;

static LIMIT: AtomicUsize = AtomicUsize::new(usize::MAX);
static USED: AtomicUsize = AtomicUsize::new(0);

fn reserve(bytes: usize) -> bool {
    let before = USED.fetch_add(bytes, Ordering::Relaxed);
    if before.saturating_add(bytes) <= LIMIT.load(Ordering::Relaxed) {
        return true;
    }
    USED.fetch_sub(bytes, Ordering::Relaxed);
    false
}

fn release(bytes: usize) {
    USED.fetch_sub(bytes, Ordering::Relaxed);
}

// SAFETY: every call forwards to the system allocator with the same layout; the counter only adds
// a condition before an allocation.
unsafe impl GlobalAlloc for Limited {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if !reserve(layout.size()) {
            return std::ptr::null_mut();
        }
        let pointer = unsafe { System.alloc(layout) };
        if pointer.is_null() {
            release(layout.size());
        }
        pointer
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        if !reserve(layout.size()) {
            return std::ptr::null_mut();
        }
        let pointer = unsafe { System.alloc_zeroed(layout) };
        if pointer.is_null() {
            release(layout.size());
        }
        pointer
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        unsafe { System.dealloc(pointer, layout) };
        release(layout.size());
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        if size > layout.size() && !reserve(size - layout.size()) {
            return std::ptr::null_mut();
        }
        let moved = unsafe { System.realloc(pointer, layout, size) };
        if moved.is_null() {
            if size > layout.size() {
                release(size - layout.size());
            }
        } else if size < layout.size() {
            release(layout.size() - size);
        }
        moved
    }
}

#[global_allocator]
static ALLOCATOR: Limited = Limited;

/// The threads a render may use (`iv_svg_limit_threads()`).
static THREADS: AtomicUsize = AtomicUsize::new(1);

/// The fewest rows a band drawn on a thread of its own has: below, a thread costs more than it saves.
const MIN_BAND_ROWS: u32 = 32;

/// A parsed document (`iv_svg` in the header).
pub struct Document {
    tree: usvg::Tree,
    /// Whether it may be drawn in bands: no filter of it depends on its whole region (`feTile`,
    /// `feImage`, stitched `feTurbulence`), which resvg would compute whole for every band, and
    /// differently for a band than for the whole (it bounds such a region by the canvas drawn).
    bands: bool,
    /// How far its filters reach at most, in pixels at scale 1 (`reach()`).
    reach: f32,
}

/// An estimate of how far the filters in `group` and below reach, in pixels at scale 1: a band
/// draws its filtered layers that far beyond its rows, so bands much shorter than that would each
/// compute most of the others' work again, and hold as much memory.
fn reach(group: &usvg::Group) -> f32 {
    use usvg::filter::Kind;
    let (sx, sy) = group.abs_transform().get_scale();
    let mut reach = group
        .filters()
        .iter()
        .map(|filter| {
            let blur = |x: usvg::PositiveF32, y: usvg::PositiveF32| 4.0 * x.get().max(y.get());
            let offset = |dx: f32, dy: f32| dx.abs().max(dy.abs());
            let total: f32 = filter
                .primitives()
                .iter()
                .map(|primitive| match primitive.kind() {
                    Kind::GaussianBlur(fe) => blur(fe.std_dev_x(), fe.std_dev_y()),
                    Kind::DropShadow(fe) => blur(fe.std_dev_x(), fe.std_dev_y()) + offset(fe.dx(), fe.dy()),
                    Kind::Offset(fe) => offset(fe.dx(), fe.dy()),
                    Kind::Morphology(fe) => fe.radius_x().get().max(fe.radius_y().get()),
                    Kind::DisplacementMap(fe) => fe.scale().abs() / 2.0,
                    _ => 0.0,
                })
                .sum();
            total * sx.max(sy)
        })
        .fold(0.0, f32::max);
    for node in group.children() {
        if let usvg::Node::Group(child) = node {
            reach = reach.max(self::reach(child));
        }
        node.subroots(|root| reach = reach.max(self::reach(root)));
    }
    reach
}

/// Whether no filter in `group`, its descendants, masks, clip paths, patterns and embedded
/// documents depends on its whole filter region.
fn local_filters(group: &usvg::Group) -> bool {
    use usvg::filter::Kind;
    let local = group.filters().iter().all(|filter| {
        filter.primitives().iter().all(|primitive| match primitive.kind() {
            Kind::Tile(_) | Kind::Image(_) => false,
            Kind::Turbulence(fe) => !fe.stitch_tiles(),
            _ => true,
        })
    });
    local
        && group.children().iter().all(|node| {
            let mut local = !matches!(node, usvg::Node::Group(child) if !local_filters(child));
            node.subroots(|root| local &= local_filters(root));
            local
        })
}

/// The generic font families (serif, sans-serif, monospace, cursive, fantasy), each named after an
/// installed font, so that a document's text is drawn whenever the system has a font. fontdb names
/// each after the first family that the last of fontconfig's aliases for it prefers, installed or
/// not [code: fontdb 0.24.0 lib.rs load_fontconfig()]: on Ubuntu 24.04 those are 65-nonlatin.conf's
/// "Nachlieli" and "Miriam Mono", seldom installed, and text in them was not drawn at all. Here,
/// where fontconfig configures the fonts, each is the first installed family in the order fontconfig
/// itself tries them (as browsers ask it); elsewhere fontdb's own (Arial, Times New Roman, Courier
/// New...) while installed; failing both, an installed upright face of normal weight (monospaced for
/// monospace), or any face.
fn settle_generic_families(db: &mut usvg::fontdb::Database) {
    use usvg::fontdb::{Database, Family, Style, Weight};
    let installed = |db: &Database, name: &str| db.faces().any(|face| face.families.iter().any(|(f, _)| f == name));
    #[cfg(all(unix, not(any(target_os = "macos", target_os = "android"))))]
    let configured = {
        let config = fontconfig();
        move |generic: &str| fontconfig_order(&config, generic)
    };
    #[cfg(not(all(unix, not(any(target_os = "macos", target_os = "android")))))]
    let configured = |_: &str| Vec::<String>::new();
    for (family, generic) in [
        (Family::Serif, "serif"),
        (Family::SansSerif, "sans-serif"),
        (Family::Monospace, "monospace"),
        (Family::Cursive, "cursive"),
        (Family::Fantasy, "fantasy"),
    ] {
        let mut candidates = configured(generic);
        candidates.push(db.family_name(&family).to_owned());
        let monospace = family == Family::Monospace;
        let Some(name) = candidates.into_iter().find(|name| installed(db, name)).or_else(|| {
            let faces = || db.faces().filter(|face| !face.families.is_empty() && (!monospace || face.monospaced));
            faces()
                .find(|face| face.style == Style::Normal && face.weight == Weight::NORMAL)
                .or_else(|| faces().next())
                .or_else(|| db.faces().find(|face| !face.families.is_empty()))
                .map(|face| face.families[0].0.clone())
        }) else {
            return; // no font at all
        };
        match family {
            Family::Serif => db.set_serif_family(name),
            Family::SansSerif => db.set_sans_serif_family(name),
            Family::Monospace => db.set_monospace_family(name),
            Family::Cursive => db.set_cursive_family(name),
            _ => db.set_fantasy_family(name),
        }
    }
}

/// The font configuration fontdb has just read: the same files, in its order [code: fontdb 0.24.0
/// lib.rs load_fontconfig()].
#[cfg(all(unix, not(any(target_os = "macos", target_os = "android"))))]
fn fontconfig() -> fontconfig_parser::FontConfig {
    use std::path::{Path, PathBuf};
    let mut config = fontconfig_parser::FontConfig::default();
    if let Ok(file) = std::env::var("FONTCONFIG_FILE") {
        let _ = config.merge_config(Path::new(&file));
        return config;
    }
    let user = std::env::var("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|_| std::env::var("HOME").map(|home| Path::new(&home).join(".config")));
    let read_global = match user {
        Ok(directory) => config.merge_config(&directory.join("fontconfig/fonts.conf")).is_err(),
        Err(_) => true,
    };
    if read_global {
        let _ = config.merge_config(Path::new("/etc/fonts/local.conf"));
    }
    let _ = config.merge_config(Path::new("/etc/fonts/fonts.conf"));
    config
}

/// The families fontconfig tries for `generic`, in its order: every alias's preferred families in
/// the configuration's order (each goes just before the generic name), then the accepted ones (each
/// just after it: the last alias's first), then the defaults (at the end of the list, in order).
#[cfg(all(unix, not(any(target_os = "macos", target_os = "android"))))]
fn fontconfig_order(config: &fontconfig_parser::FontConfig, generic: &str) -> Vec<String> {
    let aliases: Vec<_> = config
        .aliases
        .iter()
        .filter(|alias| alias.alias.trim().to_lowercase().replace(' ', "-") == generic)
        .collect();
    let mut order: Vec<String> = aliases.iter().flat_map(|alias| alias.prefer.iter().cloned()).collect();
    order.extend(aliases.iter().rev().flat_map(|alias| alias.accept.iter().cloned()));
    order.extend(aliases.iter().flat_map(|alias| alias.default.iter().cloned()));
    order
}

/// The system's fonts, loaded on the first document with text and kept for the next ones: the
/// worker serves one document after another (D-94).
fn system_fonts() -> std::sync::Arc<usvg::fontdb::Database> {
    static FONTS: std::sync::OnceLock<std::sync::Arc<usvg::fontdb::Database>> = std::sync::OnceLock::new();
    FONTS
        .get_or_init(|| {
            let mut db = usvg::fontdb::Database::new();
            db.load_system_fonts();
            settle_generic_families(&mut db);
            std::sync::Arc::new(db)
        })
        .clone()
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

#[unsafe(no_mangle)]
pub extern "C" fn iv_svg_limit_threads(threads: u32) -> i32 {
    THREADS.store(usize::try_from(threads).unwrap_or(usize::MAX).max(1), Ordering::Relaxed);
    OK
}

#[unsafe(no_mangle)]
pub extern "C" fn iv_svg_limit_memory(bytes: u64) -> i32 {
    LIMIT.store(usize::try_from(bytes).unwrap_or(usize::MAX), Ordering::Relaxed);
    OK
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
            options.fontdb = system_fonts();
        }
        match usvg::Tree::from_data(bytes, &options) {
            Ok(tree) => {
                let (bands, reach) = (local_filters(tree.root()), reach(tree.root()));
                // SAFETY: `out` is writable (checked above for null).
                unsafe { *out = Box::into_raw(Box::new(Document { tree, bands, reach })) };
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
        let document = unsafe { &*svg };
        let (scale, left) = (scale as f32, left as f32);
        let transform = |top: f64| tiny_skia::Transform::from_row(scale, 0.0, 0.0, scale, -left, -(top as f32));
        // Horizontal bands, one per thread: each a part of `pixels` of its own, drawn as the whole
        // would draw it (resvg draws a filtered layer only within reach of the band, patch D-93).
        // None shorter than half the filters' reach: each would repeat more than twice its own work.
        let reach = f64::from(document.reach) * scale as f64;
        let by_reach = if reach >= 1.0 { (2.0 * f64::from(height) / reach) as usize } else { usize::MAX };
        let threads = THREADS
            .load(Ordering::Relaxed)
            .min((height / MIN_BAND_ROWS).max(1) as usize)
            .min(by_reach.max(1));
        if !document.bands || threads < 2 {
            resvg::render(&document.tree, transform(top), &mut pixmap);
            return OK;
        }
        drop(pixmap);
        let rows = height.div_ceil(threads as u32);
        let band_bytes = rows as usize * width as usize * 4;
        std::thread::scope(|scope| {
            for (index, band) in buffer.chunks_mut(band_bytes).enumerate() {
                let band_top = top + f64::from(index as u32 * rows);
                scope.spawn(move || {
                    let band_rows = (band.len() / (width as usize * 4)) as u32;
                    if let Some(mut pixmap) = tiny_skia::PixmapMut::from_bytes(band, width, band_rows) {
                        resvg::render(&document.tree, transform(band_top), &mut pixmap);
                    }
                });
            }
        }); // a panic on a band's thread comes back here, and to guarded()
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
