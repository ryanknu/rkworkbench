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
use crate::ui::UiEvent;

static SENDER: OnceLock<Mutex<Sender<IncomingRequest>>> = OnceLock::new();
static FFMPEG_SENDER: OnceLock<Mutex<Sender<IncomingRequest>>> = OnceLock::new();
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

    let (f_send, f_recv) = channel();
    FFMPEG_SENDER.set(Mutex::new(f_send)).unwrap();

    // Initial set up
    let media_dir = cstr(media_dir);
    let path = PathBuf::from_str(&media_dir).unwrap();
    MEDIA.set(Mutex::new(MediaState::new(path))).unwrap();

    // Spawn a new background thread for general requests
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
                RenameIdentified => requests::rename_identified(&MEDIA),
                RsyncRequest(id) => requests::rsync_show(&MEDIA, id),
                ConfirmPlay(id) => requests::confirm_play(&MEDIA, id),
                DeleteTvShow(id) => requests::delete_tv_show(&MEDIA, id),
                DeleteTvSeason(id, season) => requests::delete_tv_season(&MEDIA, id, season),
                DeleteFilm(id) => requests::delete_film(&MEDIA, id),
                DeleteFilmVideo(id) => requests::delete_film_video(&MEDIA, id),
                DeleteTitle(id) => requests::delete_title(&MEDIA, id),
                UndeleteTitle(id) => requests::undelete_title(&MEDIA, id),
                Unidentify(id) => requests::unidentify_tv_episode(&MEDIA, id),
                UnidentifyFilm(id) => requests::unidentify_film_video(&MEDIA, id),
                CollectGarbage => requests::collect_garbage(&MEDIA),
                _ => vec![],
            };

            // It'd be nice to send batches of up to ~20 messages in a JSON array.
            for event in events {
                push!(cb, ptrd, &event);
            }

            push!(cb, ptrd, &UiEvent::CommandCompleted(message));
        }
    });

    // Spawn a new background thread for ffmpeg requests
    thread::spawn(move || {
        loop {
            let message = match f_recv.recv() {
                Ok(message) => message,
                Err(x) => panic!("FFmpeg receiver error. Background worker panic. RecvError: {:?}", x),
            };

            push!(cb, ptrd, &UiEvent::CommandStarted(message.clone()));

            // Process `message`
            let events = match message.clone() {
                ReencodeRequest(id) => requests::reencode_tv_episode(&MEDIA, id),
                ReencodeFilmRequest(id) => requests::reencode_film_video(&MEDIA, id),
                _ => vec![],
            };

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

#[unsafe(no_mangle)]
pub extern "C" fn rename_identified() {
    println!("[rust] rename_identified called");

    SENDER.get().map(|s| s.lock().unwrap().send(RenameIdentified));
}

#[unsafe(no_mangle)]
pub extern "C" fn rsync_show(show_id: *const c_char) {
    println!("[rust] rsync_show called");

    let show_id = cstr(show_id);

    SENDER.get().map(|s| s.lock().unwrap().send(RsyncRequest(show_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn confirm_tv_episode_plays(id: *const c_char) {
    println!("[rust] confirm_tv_episode_plays called");

    let id = MappableMediaId::TvEpisode(TvEpisodeId(cstr(id)));

    SENDER.get().map(|s| s.lock().unwrap().send(ConfirmPlay(id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn confirm_film_video_plays(id: *const c_char) {
    println!("[rust] confirm_film_video_plays called");

    let id = MappableMediaId::FilmVideo(FilmVideoId(cstr(id)));

    SENDER.get().map(|s| s.lock().unwrap().send(ConfirmPlay(id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn delete_tv_show(show_id: *const c_char) {
    println!("[rust] delete_tv_show called");

    let show_id = cstr(show_id);

    SENDER.get().map(|s| s.lock().unwrap().send(DeleteTvShow(show_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn delete_tv_season(show_id: *const c_char, season_number: usize) {
    println!("[rust] delete_tv_season called");

    let show_id = cstr(show_id);

    SENDER.get().map(|s| s.lock().unwrap().send(DeleteTvSeason(show_id, season_number)));
}

#[unsafe(no_mangle)]
pub extern "C" fn delete_film(film_id: *const c_char) {
    println!("[rust] delete_film called");

    let film_id = cstr(film_id);

    SENDER.get().map(|s| s.lock().unwrap().send(DeleteFilm(film_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn delete_film_video(video_id: *const c_char) {
    println!("[rust] delete_film_video called");

    let video_id = cstr(video_id);

    SENDER.get().map(|s| s.lock().unwrap().send(DeleteFilmVideo(video_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn unidentify_film_video(id: *const c_char) {
    println!("[rust] unidentify_film_video called");

    let id = FilmVideoId(cstr(id));

    SENDER.get().map(|s| s.lock().unwrap().send(UnidentifyFilm(id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn reencode_film_video(id: *const c_char) {
    println!("[rust] reencode_film_video called");

    let id = FilmVideoId(cstr(id));

    FFMPEG_SENDER.get().map(|s| s.lock().unwrap().send(ReencodeFilmRequest(id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn unidentify_tv_episode(id: *const c_char) {
    println!("[rust] unidentify_tv_episode called");

    let id = TvEpisodeId(cstr(id));

    SENDER.get().map(|s| s.lock().unwrap().send(Unidentify(id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn reencode_tv_episode(id: *const c_char) {
    println!("[rust] reencode_tv_episode called");

    let id = TvEpisodeId(cstr(id));

    FFMPEG_SENDER.get().map(|s| s.lock().unwrap().send(ReencodeRequest(id)));
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

/// Returns the filename for a given tv episode id.
#[unsafe(no_mangle)]
pub extern "C" fn get_filename_for_tv_episode_id(id: *const c_char) -> *mut c_char {
    let id = TvEpisodeId(cstr(id));
    let media = MEDIA.get().unwrap().lock().unwrap();

    let episode_info = {
        let episodes = media.tv_show_episodes.borrow();
        let tv_shows = media.tv_shows.borrow();
        episodes.iter().find(|e| e.id == id).and_then(|e| {
            tv_shows.iter().find(|s| s.id == e.show_id).map(|s| (s.show_key.clone(), e.series_key.clone()))
        })
    };

    match episode_info {
        Some((_show_key, series_key)) => {
            let s = format!("{}.mkv", series_key);
            let c_str = CString::new(s).unwrap();
            c_str.into_raw()
        }
        None => std::ptr::null_mut(),
    }
}

/// Returns the filename for a given film video id.
#[unsafe(no_mangle)]
pub extern "C" fn get_filename_for_film_video_id(id: *const c_char) -> *mut c_char {
    let id = FilmVideoId(cstr(id));
    let media = MEDIA.get().unwrap().lock().unwrap();

    let video_info = {
        let videos = media.film_videos.borrow();
        videos.iter().find(|v| v.id == id).map(|v| v.get_ideal_storage_path())
    };

    match video_info {
        Some(rel_path) => {
            if let Some(last) = rel_path.last() {
                let c_str = CString::new(last.clone()).unwrap();
                c_str.into_raw()
            } else {
                std::ptr::null_mut()
            }
        }
        None => std::ptr::null_mut(),
    }
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

#[unsafe(no_mangle)]
pub extern "C" fn delete_title(title_id: *const c_char) {
    println!("[rust] delete_title called");

    let title_id = FileBackedTitleId(cstr(title_id));

    SENDER.get().map(|s| s.lock().unwrap().send(DeleteTitle(title_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn undelete_title(title_id: *const c_char) {
    println!("[rust] undelete_title called");

    let title_id = FileBackedTitleId(cstr(title_id));

    SENDER.get().map(|s| s.lock().unwrap().send(UndeleteTitle(title_id)));
}

#[unsafe(no_mangle)]
pub extern "C" fn collect_garbage() {
    println!("[rust] collect_garbage called");

    SENDER.get().map(|s| s.lock().unwrap().send(CollectGarbage));
}
