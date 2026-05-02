#[allow(static_mut_refs)]

mod media;
mod ui;
mod requests;
pub mod tmdb;
pub mod convert;

// lib.rs
use std::ffi::{c_char, CStr, CString};
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Mutex, OnceLock};
use std::thread;
use crate::media::{MappableMediaId, FileBackedTitleId, MediaState, TvEpisodeId, FilmVideoId};
use crate::requests::IncomingRequest;
use crate::requests::IncomingRequest::*;
use crate::ui::{build_files_tree, build_tv_shows_tree, get_garbage_size, UiEvent};

static SENDER: OnceLock<Mutex<Sender<IncomingRequest>>> = OnceLock::new();
static MEDIA: OnceLock<Mutex<MediaState>> = OnceLock::new();

// Recreate the C callback type in Rust
type MessageCallback = Option<unsafe extern "C" fn(usize, *const c_char)>;

fn cstr(str: *const c_char) -> String {
    let message = unsafe {
        CStr::from_ptr(str)
            .to_str()
    };

    match message {
        Ok(message) => message.to_owned(),
        Err(e) => {
            panic!("[rust] Malformed incoming message: {:?}", e);
        }
    }
}

macro_rules! push {
    ($cb: expr, $ptrd: expr, $e: expr) => {
        unsafe {
            let str = serde_json::to_string($e).unwrap();
            let str = CString::new(str).unwrap();
            $cb($ptrd, str.as_ptr());
        }
    };
}

#[unsafe(no_mangle)]
pub extern "C" fn start_rust_processing(ptrd: usize, media_dir: *const c_char, callback: MessageCallback) {
    // Check callback
    let Some(cb) = callback else {
        panic!("Callback not initialized. Background worker panic.");
    };

    // Create communication channel.
    let (send, recv) = channel();
    SENDER.set(Mutex::new(send)).unwrap();

    // Initial set up
    let media_dir = cstr(media_dir);
    let path = PathBuf::from_str(&media_dir).unwrap();
    MEDIA.set(Mutex::new(MediaState::new(path))).unwrap();

    // Spawn a new background thread
    thread::spawn(move || {
        push!(cb, ptrd, &UiEvent::WorkerReady);

        loop {
            let message = match recv.recv() {
                Ok(message) => message,
                Err(x) => panic!("Receiver error. Background worker panic. RecvError: {:?}", x),
            };

            // TODO: I shouldn't have to clone this message everywhere. Serializing to JSON doesn't take ownership.
            //       The problem is the CommandStarted struct takes ownership.
            push!(cb, ptrd, &UiEvent::CommandStarted(message.clone()));

            // Process `message`
            let events = match message.clone() {
                PerformInitialLoad => requests::read_local_media(&MEDIA),
                MapMedia(from, to) => requests::map_media(&MEDIA, from, to),
                LookupFilm(tmdb_id, tmdb_api_key) => requests::lookup_film(&MEDIA, tmdb_id, tmdb_api_key),
                LookupTv(tmdb_id, tmdb_api_key) => requests::lookup_tv(&MEDIA, tmdb_id, tmdb_api_key),
            };

            // It'd be nice to send batches of up to ~20 messages in a JSON array.
            for event in events {
                push!(cb, ptrd, &event);
            }

            push!(cb, ptrd, &UiEvent::CommandCompleted(message));
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn initial_load() {
    println!("[rust] initial_load called");

    SENDER.get().map(|s| s.lock().unwrap().send(PerformInitialLoad));
}

#[unsafe(no_mangle)]
pub extern "C" fn map_tv_episode(title_id: *const c_char, media_id: *const c_char) {
    println!("[rust] map_tv_episode called");

    let title_id = FileBackedTitleId(cstr(title_id));
    let media_id = MappableMediaId::TvEpisode(TvEpisodeId(cstr(media_id)));

    SENDER.get().map(|s| s.lock().unwrap().send(MapMedia(title_id, media_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn map_film_video(title_id: *const c_char, media_id: *const c_char) {
    println!("[rust] map_film_video called");

    let title_id = FileBackedTitleId(cstr(title_id));
    let media_id = MappableMediaId::FilmVideo(FilmVideoId(cstr(media_id)));

    SENDER.get().map(|s| s.lock().unwrap().send(MapMedia(title_id, media_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn lookup_film(tmdb_id: *const c_char, tmdb_api_key: *const c_char) {
    println!("[rust] lookup_film called");

    let tmdb_id = cstr(tmdb_id);
    let tmdb_api_key = cstr(tmdb_api_key);

    let tmdb_api_key = if tmdb_api_key.is_empty() {
        None
    } else {
        Some(tmdb_api_key)
    };

    SENDER.get().map(|s| s.lock().unwrap().send(LookupFilm(tmdb_id, tmdb_api_key)));
}

#[unsafe(no_mangle)]
pub extern "C" fn lookup_tv(tmdb_id: *const c_char, tmdb_api_key: *const c_char) {
    println!("[rust] lookup_tv called");

    let tmdb_id = cstr(tmdb_id);
    let tmdb_api_key = cstr(tmdb_api_key);

    let tmdb_api_key = if tmdb_api_key.is_empty() {
        None
    } else {
        Some(tmdb_api_key)
    };

    SENDER.get().map(|s| s.lock().unwrap().send(LookupTv(tmdb_id, tmdb_api_key)));
}

/// Returns the filename for a given id.
#[unsafe(no_mangle)]
pub extern "C" fn get_filename_for_title_id(title_id: *const c_char) -> *mut c_char {
    let media = MEDIA.get().unwrap().lock().unwrap();
    let title_id = FileBackedTitleId(cstr(title_id));
    let Some(path) = media.get_file_backed_title_path(&title_id) else {
        return CString::new("").expect("CString::new failed").into_raw();
    };

    let rust_string = path.as_os_str().to_str().unwrap().to_owned();
    let c_string = CString::new(rust_string).expect("CString::new failed");
    c_string.into_raw()
}

/// Frees the C string allocated in rust land.
///
/// # Safety
/// This function must be called with a valid pointer previously returned by a Rust FFI function
/// and only once for a given pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn free_string(ptr: *mut c_char) {
    if ptr.is_null() {
        return;
    }

    // Take ownership back from C++ and drop the CString to deallocate memory
    unsafe {
        drop(CString::from_raw(ptr));
    }
}
