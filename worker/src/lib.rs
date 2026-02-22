#[allow(static_mut_refs)]

mod media;
mod ui;
mod requests;

// lib.rs
use std::ffi::{c_char, CStr, CString};
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Mutex, OnceLock};
use std::thread;
use crate::media::{ConstMediaId, FileBackedTitleId, MediaState, TvEpisodeId};
use crate::requests::IncomingRequest;
use crate::ui::{build_files_tree, build_media_tree, get_garbage_size, UiEvent};

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
                IncomingRequest::PerformInitialLoad => requests::read_local_media(&MEDIA),
                IncomingRequest::MapMedia(from, to) => requests::map_media(&MEDIA, from, to),
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

    SENDER.get().map(|s| s.lock().unwrap().send(IncomingRequest::PerformInitialLoad));
}

#[unsafe(no_mangle)]
pub extern "C" fn map_media(title_id: *const c_char, media_id: *const c_char) {
    println!("[rust] map_media called");

    let title_id = FileBackedTitleId(cstr(title_id));
    let media_id = ConstMediaId::TvEpisode(TvEpisodeId(cstr(media_id)));

    SENDER.get().map(|s| s.lock().unwrap().send(IncomingRequest::MapMedia(title_id, media_id)));
}

// #[unsafe(no_mangle)]
// pub extern "C" fn uc_echo(str: *const c_char) {
//     println!("[rust] uc_echo called");
//
//     let message = unsafe {
//         CStr::from_ptr(str)
//             .to_str()
//     };
//
//     let message = match message {
//         Ok(message) => message.to_owned(),
//         Err(e) => {
//             println!("[rust] Malformed incoming message: {:?}", e);
//             return;
//         }
//     };
//
//     println!("[rust] incoming message: {message}");
//
//     SENDER.get().map(|s| s.lock().unwrap().send(message));
// }

/// Returns the filename for a given id.
#[unsafe(no_mangle)]
pub extern "C" fn get_filename_for_title_id() -> *mut c_char {
    let rust_string = "Hello from Rust!";
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
