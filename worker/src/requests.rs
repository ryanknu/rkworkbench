use crate::media::{MappableMediaId, FileBackedTitleId, Film, FilmId, FilmVideoId, MediaId, TvShow, TvShowEpisode, TvShowId, TvEpisodeId};
use crate::tmdb::{TmdbItem, TmdbTvShow, TmdbTvShowSeason};
use crate::ui::{build_files_tree, build_films_tree, build_tv_shows_tree, get_add_tree_item_for_film, get_add_tree_item_for_tv_show, get_garbage_size, get_tmdb_key_event, get_tree_change_action_for_mappable, get_tree_change_action_for_mapping_file, MatchResult, Tree, TreeItem, TreeItemChange::ChangeColor, UiEvent};
use serde::{Deserialize, Serialize};
use walkdir::WalkDir;
use std::fs;
use std::io::{BufReader, Read, Write};
use std::thread;
use std::process::Stdio;
use std::sync::{Mutex, OnceLock};
use rayon::prelude::*;
use crate::convert::{FilmVideoBuilder, TvShowEpisodeBuilder};

type MediaState = OnceLock<Mutex<crate::media::MediaState>>;

#[derive(Clone, Serialize, Deserialize)]
pub enum IncomingRequest {
    LookupFilm(String, Option<String>),
    LookupTv(String, Option<String>),
    MapMedia(FileBackedTitleId, MappableMediaId),
    PerformInitialLoad,
    RenameIdentified,
    RsyncRequest(String, String, String),
    ConfirmPlay(MappableMediaId),
    DeleteTvShow(String),
    DeleteTvSeason(String, usize),
    DeleteFilm(String),
    DeleteFilmVideo(String),
    DeleteTitle(FileBackedTitleId),
    UndeleteTitle(FileBackedTitleId),
    Unidentify(TvEpisodeId),
    UnidentifyFilm(FilmVideoId),
    ReencodeRequest(TvEpisodeId, String),
    ReencodeFilmRequest(FilmVideoId, String),
    CollectGarbage,
    RestoreOriginal(MappableMediaId),
    MatchScan(FileBackedTitleId, String),
    FetchTmdbStill(MappableMediaId),
}

/// Macro to help conveniently unlock the media state. I used a single expression over let-else
/// because single expressions tend to play nicer with macros.
macro_rules! unlock_media {
    ($m: expr) => {
        match (match $m.get() {
            Some(m) => m.lock(),
            None => {
                println!("Media state not initialized, aborting request.");
                return vec![];
            },
        }) {
            Ok(m) => m,
            Err(e) => {
                println!("Error locking media state: {:?}", e);
                return vec![];
            }
        }
    };
}

/// Adds an item to the media library. If the item cannot be added, it returns the item back to the
/// sender, to transfer ownership back. I admit this is a gross pattern.
/// Internal monologue: I'd like this function to exist on MediaState, but that'd require MediaState
/// to accept Tmdb* objects. Need to think through the cleanest way to do this.
fn add_to_library(media: &crate::media::MediaState, item: TmdbItem) -> Option<TmdbItem> {
    match item {
        TmdbItem::Film(film) => {
            media.push_film_owned(film.into());
            None
        },
        TmdbItem::FilmVideos(videos) => {
            // TODO: Rust 1.95 adds let bindings in match guards. That'll clean this way up.
            if let Some(film) = media.get_film_by_tmdb_id(videos.id) {
                let film_videos = videos.results.into_iter().map(|v| {
                    let builder = FilmVideoBuilder::from(v);
                    builder.build_with_film(&film)
                }).collect();
                media.push_film_videos(film_videos);
                None
            } else {
                Some(TmdbItem::FilmVideos(videos))
            }
        },
        TmdbItem::TvShow(tv_show) => {
            media.push_tv_show(tv_show.into());
            None
        }
        TmdbItem::TvShowSeason(season) if !&season.episodes.is_empty() => {
            // TODO: Rust 1.95 adds let bindings in match guards. That'll clean this way up.
            if let Some(show) = media.get_show_by_tmdb_id(season.episodes[0].show_id) {
                let episodes = season.episodes.into_iter().map(|v| {
                    let builder = TvShowEpisodeBuilder::from(v);
                    builder.build_with_show(&show)
                }).collect();
                media.push_tv_show_episodes(episodes);
                None
            } else {
                Some(TmdbItem::TvShowSeason(season))
            }
        },
        _ => Some(item),
    }
}

fn strip_id_prefix(id: &str) -> String {
    if id.starts_with("show.") {
        id[5..].to_owned()
    } else if id.starts_with("ep.") {
        id[3..].to_owned()
    } else if id.starts_with("film.") {
        id[5..].to_owned()
    } else {
        id.to_owned()
    }
}

pub fn read_local_media(media: &MediaState) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    media.load_confirmed_plays();

    // This should be moved.
    media.read_local_media();

    // We need to do 2 passes through the cache. Iteration order is not guaranteed so we may
    // encounter an episode before its associated series. If we do that, we can't properly construct
    // the object, at least, not without hurting ergonomics later (such as needing to query 2
    // objects to construct that episode). Also, even if we found a way to guarantee the order,
    // nothing stops a series file being deleted and leaving behind its season file. This would
    // simply pass over those.
    let mut unprocessable = Vec::new();
    for entry in media.tmdb().iterate_cache() {
        println!("Processing cache entry (1/2): {:?}", entry);
        if let Some(entry) = add_to_library(&media, entry) {
            unprocessable.push(entry);
        }
    }

    for entry in unprocessable {
        println!("Processing cache entry (2/2): {:?}", entry);
        if let Some(entry) = add_to_library(&media, entry) {
            println!("Failed to add item to media library: {:?}", entry);
        }
    }

    media.sort_collections();

    let mut events = vec![UiEvent::ClearTrees];
    events.extend(build_files_tree(&media));
    events.extend(build_films_tree(&media));
    events.extend(build_tv_shows_tree(&media));
    events.push(get_garbage_size(&media));

    if media.has_confirmed_tmdb_api_key() {
        events.push(get_tmdb_key_event());
    }

    events
}

pub fn map_media(media: &MediaState, from: FileBackedTitleId, to: MappableMediaId) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    let Some(_mapping) = media.map_media(&from, &to) else {
        println!("Mapping failed for file {:?} to {:?}", from, to);
        return vec![];
    };

    // TODO: Rename the file here.

    vec![
        get_tree_change_action_for_mapping_file(from, true),
    ].into_iter().chain(get_tree_change_action_for_mappable(&media, to)).collect()
}

pub fn lookup_film(media: &MediaState, tmdb_id: String, tmdb_api_key: Option<String>) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    if tmdb_api_key.is_none() && !media.has_confirmed_tmdb_api_key() {
        println!("No API key found, aborting film lookup");
        return vec![];
    }

    let tmdb_api_key = tmdb_api_key.as_ref();
    let api_key = tmdb_api_key.map(|s| &s[..]).unwrap_or_else(|| media.get_tmdb_api_key());

    let Ok((film, videos)) = media.tmdb().query_film(api_key, &tmdb_id) else {
        println!("Error querying TMDB for film: {}", tmdb_id);
        return vec![];
    };

    let film: Film = film.into();

    media.push_film(&film);

    let mut results = vec![
        get_add_tree_item_for_film(&media, film.feature_presentation_video().id.0, film.id.0.to_string(), film.name.clone(), "Feature Presentation".to_owned())
    ];

    results.extend(
        videos.results.into_iter().map(|video| get_add_tree_item_for_film(&media, video.id, film.id.0.to_string(), film.name.clone(), format!("{} - {}", video.r#type, video.name)))
    );
    
    results
}

pub fn lookup_tv(media: &MediaState, tmdb_id: String, tmdb_api_key: Option<String>) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    if tmdb_api_key.is_none() && !media.has_confirmed_tmdb_api_key() {
        println!("No API key found, aborting TV lookup");
        return vec![];
    }

    let tmdb_api_key = tmdb_api_key.as_ref();
    let api_key = tmdb_api_key.map(|s| &s[..]).unwrap_or_else(|| media.get_tmdb_api_key());

    let Ok((show_json, seasons_json)) = media.tmdb().query_tv(api_key, &tmdb_id) else {
        println!("Error querying TMDB for TV show: {}", tmdb_id);
        return vec![];
    };

    let show: TmdbTvShow = serde_json::from_slice(&show_json).unwrap();
    let show: TvShow = show.into();
    media.push_tv_show(show.clone());

    let mut results = Vec::new();

    for season_json in seasons_json {
        let season: TmdbTvShowSeason = serde_json::from_slice(&season_json).unwrap();
        if season.episodes.is_empty() { continue; }

        let episodes: Vec<TvShowEpisode> = season.episodes.into_iter().map(|v| {
            let builder = TvShowEpisodeBuilder::from(v);
            builder.build_with_show(&show)
        }).collect();

        for episode in &episodes {
            results.push(get_add_tree_item_for_tv_show(
                &media,
                episode.id.0.to_string(),
                show.id.0.to_string(),
                show.name.clone(),
                format!("{} - {}", episode.series_key, episode.name)
            ));
        }
        media.push_tv_show_episodes(episodes);
    }

    results
}

pub fn rename_identified(media: &MediaState) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let media_dir = media.media_dir.clone();
    let output_dir = media_dir.join("output");

    let mut to_rename = Vec::new();

    {
        let titles = media.file_backed_titles.borrow();
        for title in titles.iter() {
            if let Some(mapped_id) = &title.mapped_media {
                let target_rel_path = match mapped_id {
                    MediaId::TvEpisode(ep_id) => {
                        let episodes = media.tv_show_episodes.borrow();
                        if let Some(episode) = episodes.iter().find(|e| e.id == *ep_id) {
                            if let Some(show_key) = media.tv_show_key(&episode.show_id) {
                                Some(vec![show_key, format!("{}.mkv", episode.series_key)])
                            } else {
                                None
                            }
                        } else {
                            None
                        }
                    }
                    MediaId::FilmVideo(fv_id) => {
                        let videos = media.film_videos.borrow();
                        if let Some(video) = videos.iter().find(|v| v.id == *fv_id) {
                            Some(video.get_ideal_storage_path())
                        } else {
                            None
                        }
                    }
                    _ => None,
                };

                if let Some(rel_path) = target_rel_path {
                    let mut target_path = output_dir.clone();
                    for part in rel_path {
                        target_path = target_path.join(part);
                    }
                    to_rename.push((title.id.clone(), title.path.clone(), target_path));
                }
            }
        }
    }

    for (id, source, dest) in to_rename {
        if let Some(parent) = dest.parent() {
            if let Err(e) = fs::create_dir_all(parent) {
                println!("Error creating directory {:?}: {:?}", parent, e);
                continue;
            }
        }

        println!("Renaming {:?} to {:?}", source, dest);
        if let Err(e) = fs::rename(&source, &dest) {
            println!("Error renaming file {:?} to {:?}: {:?}", source, dest, e);
            continue;
        }

        // Successfully renamed. Update path in media state.
        let mut mapped_id = None;
        if let Some(title) = media.file_backed_titles.borrow_mut().iter_mut().find(|t| t.id == id) {
            title.path = dest.clone();
            mapped_id = title.mapped_media.clone();
        }

        if let Some(MediaId::TvEpisode(eid)) = mapped_id {
            events.extend(get_tree_change_action_for_mappable(&media, MappableMediaId::TvEpisode(eid)));
        } else if let Some(MediaId::FilmVideo(fvid)) = mapped_id {
            events.extend(get_tree_change_action_for_mappable(&media, MappableMediaId::FilmVideo(fvid)));
        }

        events.push(UiEvent::RemoveTreeItemById {
            tree: Tree::Files,
            id: id.0,
        });
    }

    events.push(get_garbage_size(&media));
    events
}

pub fn rsync_show(media: &MediaState, id: String, tv_loc: String, movie_loc: String, on_progress: impl Fn(UiEvent)) -> Vec<UiEvent> {
    let raw_id = strip_id_prefix(&id);
    let (folder_name, source, destination, config_dir) = {
        let media = unlock_media!(media);

        let (folder_name, remote_base) = if let Some(show) = media.get_show_by_id(&TvShowId(raw_id.clone())) {
            (show.show_key.clone(), tv_loc)
        } else if let Some(film) = media.get_film_by_id(&FilmId(raw_id.clone())) {
            (film.film_key().to_owned(), movie_loc)
        } else {
            println!("Media not found for rsync: {:?}", raw_id);
            return vec![];
        };

        let source = media.media_dir.join("output").join(&folder_name);
        
        // Ensure trailing slash is handled for remote_base
        let remote_base = if remote_base.ends_with('/') {
            remote_base
        } else {
            format!("{}/", remote_base)
        };
        
        let destination = format!("{}{}/", remote_base, folder_name);
        (folder_name, source, destination, media.config_dir.clone())
    };

    if !source.exists() {
        println!("[rust] rsync source directory does not exist: {:?}", source);
        return vec![];
    }

    println!("[rust] rsyncing {:?} to {:?}", source, destination);

    // Spawn rsync
    let child_res = std::process::Command::new("rsync")
        .arg("-aP")
        .arg(format!("{}/", source.to_string_lossy()))
        .arg(&destination)
        .stdout(Stdio::piped())
        .spawn();

    match child_res {
        Ok(mut child) => {
            if let Some(stdout) = child.stdout.take() {
                handle_rsync_progress(stdout, &on_progress);
            }

            let status = child.wait();

            match status {
                Ok(s) if s.success() => {
                    println!("[rust] rsync successful for {}", folder_name);
                    // Log rsynced files
                    let log_file = config_dir.join("rsynced_files.txt");
                    if let Ok(mut file) = std::fs::OpenOptions::new()
                        .create(true)
                        .append(true)
                        .open(log_file)
                    {
                        // We should log all files in the source directory
                        for entry in WalkDir::new(&source).into_iter().filter_map(|e| e.ok()) {
                            if entry.path().is_file() {
                                let _ = writeln!(file, "{}", entry.path().to_string_lossy());
                            }
                        }
                    }
                }
                Ok(s) => {
                    println!("[rust] rsync failed for {} with status: {:?}", folder_name, s);
                    on_progress(UiEvent::RsyncOutput(format!("Error: rsync exited with status {:?}", s)));
                }
                Err(e) => {
                    println!("[rust] failed to wait for rsync: {:?}", e);
                    on_progress(UiEvent::RsyncOutput(format!("Error: failed to wait for rsync: {:?}", e)));
                }
            }
        }
        Err(e) => {
            println!("[rust] failed to execute rsync: {:?}", e);
            on_progress(UiEvent::RsyncOutput(format!("Error: failed to execute rsync: {:?}", e)));
        }
    }

    vec![]
}

pub fn confirm_play(media: &MediaState, id: MappableMediaId) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    let path_to_confirm = media.get_title_id_for_mappable(&id).and_then(|tid| {
        let titles = media.file_backed_titles.borrow();
        titles.iter().find(|t| t.id == tid).map(|t| t.path.clone())
    });

    if let Some(path) = path_to_confirm {
        media.add_confirmed_play(path);
        
        let mut events = get_tree_change_action_for_mappable(&media, id);
        events.push(get_garbage_size(&media));

        // Update Files tree for any titles that are now marked for deletion (confirmed or its original)
        let titles = media.file_backed_titles.borrow();
        let confirmed = media.confirmed_plays.borrow();
        for title in titles.iter() {
            if !title.is_mapped() && title.marked_for_deletion(&confirmed, &media.media_dir) {
                events.push(UiEvent::ChangeTreeItem {
                    tree: Tree::Files,
                    id: title.id.0.clone(),
                    change: ChangeColor(if title.file_name.contains(".d") { "red".to_owned() } else { "cyan".to_owned() })
                });
            }
        }
        events
    } else {
        println!("Failed to find file to confirm play for {:?}", id.id());
        vec![]
    }
}

pub fn delete_tv_show(media: &MediaState, id: String) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let raw_id = strip_id_prefix(&id);
    let show_id = TvShowId(raw_id.clone());

    let mut events = Vec::new();

    // 1. Identify all episodes to be removed and their mappings
    let mut episodes_to_remove = Vec::new();
    {
        let episodes = media.tv_show_episodes.borrow();
        for ep in episodes.iter() {
            if ep.show_id == show_id {
                episodes_to_remove.push(ep.id.clone());
            }
        }
    }

    // 2. Unmap files in Files tree
    {
        let mut titles = media.file_backed_titles.borrow_mut();
        for title in titles.iter_mut() {
            if let Some(MediaId::TvEpisode(ep_id)) = &title.mapped_media {
                if episodes_to_remove.contains(ep_id) {
                    title.mapped_media = None;
                    events.push(get_tree_change_action_for_mapping_file(title.id.clone(), false));
                }
            }
        }
    }

    // 3. Remove show from tree (this removes all episodes in Qt)
    events.push(UiEvent::RemoveTreeItemById {
        tree: Tree::TvShows,
        id: raw_id,
    });

    media.delete_tv_show(&show_id);
    events.push(get_garbage_size(&media));
    events
}

pub fn delete_tv_season(media: &MediaState, id: String, season: usize) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let raw_id = strip_id_prefix(&id);
    let show_id = TvShowId(raw_id);

    let mut events = Vec::new();
    let mut episodes_to_remove = Vec::new();
    {
        let episodes = media.tv_show_episodes.borrow();
        for ep in episodes.iter() {
            if ep.show_id == show_id && ep.season_number == season {
                episodes_to_remove.push(ep.id.clone());
                events.push(UiEvent::RemoveTreeItemById {
                    tree: Tree::TvShows,
                    id: ep.id.0.clone(),
                });
            }
        }
    }

    // Unmap files
    {
        let mut titles = media.file_backed_titles.borrow_mut();
        for title in titles.iter_mut() {
            if let Some(MediaId::TvEpisode(ep_id)) = &title.mapped_media {
                if episodes_to_remove.contains(ep_id) {
                    title.mapped_media = None;
                    events.push(get_tree_change_action_for_mapping_file(title.id.clone(), false));
                }
            }
        }
    }

    media.delete_tv_season(&show_id, season);
    events.push(get_garbage_size(&media));
    events
}

pub fn delete_title(media: &MediaState, id: FileBackedTitleId) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let mut title_to_update = None;
    {
        let titles = media.file_backed_titles.borrow();
        if let Some(title) = titles.iter().find(|t| t.id == id) {
            title_to_update = Some((title.path.clone(), title.file_name.clone()));
        }
    }

    if let Some((old_path, old_file_name)) = title_to_update {
        let mut new_file_name = old_file_name;
        new_file_name.push_str(".d");
        let mut new_path = old_path.clone();
        new_path.set_file_name(&new_file_name);

        println!("[rust] Renaming {:?} to {:?}", old_path, new_path);
        if let Err(e) = fs::rename(&old_path, &new_path) {
            println!("Error renaming file: {:?}", e);
            return vec![];
        }

        // Update state
        if let Some(title) = media.file_backed_titles.borrow_mut().iter_mut().find(|t| t.id == id) {
            title.path = new_path;
            title.file_name = new_file_name;
        }

        events.push(UiEvent::ChangeTreeItem {
            tree: Tree::Files,
            id: id.0.clone(),
            change: ChangeColor("red".to_owned()),
        });
    }

    events.push(get_garbage_size(&media));
    events
}

pub fn undelete_title(media: &MediaState, id: FileBackedTitleId) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let mut title_to_update = None;
    {
        let titles = media.file_backed_titles.borrow();
        if let Some(title) = titles.iter().find(|t| t.id == id) {
            if title.file_name.ends_with(".d") {
                title_to_update = Some((title.path.clone(), title.file_name.clone(), title.is_mapped()));
            }
        }
    }

    if let Some((old_path, old_file_name, is_mapped)) = title_to_update {
        let new_file_name = old_file_name[..old_file_name.len() - 2].to_owned();
        let mut new_path = old_path.clone();
        new_path.set_file_name(&new_file_name);

        println!("[rust] Renaming {:?} to {:?}", old_path, new_path);
        if let Err(e) = fs::rename(&old_path, &new_path) {
            println!("Error renaming file: {:?}", e);
            return vec![];
        }

        // Update state
        if let Some(title) = media.file_backed_titles.borrow_mut().iter_mut().find(|t| t.id == id) {
            title.path = new_path;
            title.file_name = new_file_name;
        }

        events.push(UiEvent::ChangeTreeItem {
            tree: Tree::Files,
            id: id.0.clone(),
            change: ChangeColor(if is_mapped { "orange".to_owned() } else { "Default".to_owned() }),
        });
    }

    events.push(get_garbage_size(&media));
    events
}

pub fn unidentify_tv_episode(media: &MediaState, id: TvEpisodeId) -> Vec<UiEvent> {
    let id = TvEpisodeId(strip_id_prefix(&id.0));
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let mut info = None;
    {
        let mappable_id = MappableMediaId::TvEpisode(id.clone());
        if let Some(title_id) = media.get_title_id_for_mappable(&mappable_id) {
            let episodes = media.tv_show_episodes.borrow();
            if let Some(episode) = episodes.iter().find(|e| e.id == id) {
                let titles = media.file_backed_titles.borrow();
                if let Some(title) = titles.iter().find(|t| t.id == title_id) {
                    info = Some((title.id.clone(), title.path.clone(), episode.show_name.clone(), episode.series_key.clone()));
                }
            }
        }
    }

    if let Some((title_id, source_path, show_name, series_key)) = info {
        let dest_folder = media.media_dir.join("Lost & Found");
        if let Err(e) = fs::create_dir_all(&dest_folder) {
            println!("Error creating directory {:?}: {:?}", dest_folder, e);
            return vec![];
        }

        let dest_file_name = format!("{} - {}.mkv", show_name, series_key);
        let dest_path = dest_folder.join(dest_file_name);

        println!("Moving {:?} to {:?}", source_path, dest_path);
        if let Err(e) = fs::rename(&source_path, &dest_path) {
            println!("Error moving file {:?} to {:?}: {:?}", source_path, dest_path, e);
            return vec![];
        }

        // Update state
        if let Some(title) = media.file_backed_titles.borrow_mut().iter_mut().find(|t| t.id == title_id) {
            title.path = dest_path;
            title.mapped_media = None;
            title.collection = "Lost & Found".to_owned();
            title.file_name = format!("{} - {}.mkv", show_name, series_key);
        }

        // UI events
        events.push(UiEvent::RemoveTreeItemById {
            tree: Tree::TvShows,
            id: id.0.clone(),
        });

        if let Some(title) = media.file_backed_titles.borrow().iter().find(|t| t.id == title_id) {
             let confirmed = media.confirmed_plays.borrow();
             events.push(UiEvent::AddTreeItem {
                tree: Tree::Files,
                item: TreeItem {
                    id: title.id.0.clone(),
                    parent_id: None,
                    parent_text: title.collection.clone(),
                    text: title.file_name.clone(),
                    color: if title.marked_for_deletion(&confirmed, &media.media_dir) { "red".to_owned() } else { "Default".to_owned() },
                },
                after: None,
            });
        }
    }

    events.push(get_garbage_size(&media));
    events
}

fn split_shell_command(cmd: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    let mut quote_char = None;
    let mut escaped = false;

    for c in cmd.chars() {
        if escaped {
            current.push(c);
            escaped = false;
        } else if c == '\\' {
            escaped = true;
        } else if in_quotes {
            if Some(c) == quote_char {
                in_quotes = false;
                quote_char = None;
            } else {
                current.push(c);
            }
        } else if c == '"' || c == '\'' {
            in_quotes = true;
            quote_char = Some(c);
        } else if c.is_whitespace() {
            if !current.is_empty() {
                args.push(current.clone());
                current.clear();
            }
        } else {
            current.push(c);
        }
    }
    if !current.is_empty() {
        args.push(current);
    }
    args
}

fn handle_ffmpeg_progress(stderr: std::process::ChildStderr, on_progress: &impl Fn(UiEvent)) {
    let mut reader = BufReader::new(stderr);
    let mut current_line = Vec::new();
    let mut buffer = [0u8; 1024];

    loop {
        match reader.read(&mut buffer) {
            Ok(0) => break,
            Ok(n) => {
                for &b in &buffer[..n] {
                    if b == b'\r' || b == b'\n' {
                        if !current_line.is_empty() {
                            let line = String::from_utf8_lossy(&current_line).trim().to_string();
                            if (line.contains("frame=") || line.contains("size=")) && line.contains("time=") {
                                on_progress(UiEvent::FfmpegOutput(line));
                            } else if !line.starts_with("ffmpeg version") && !line.starts_with("built with") && !line.starts_with("configuration:") && !line.starts_with("lib") {
                                // If it's not version spam, send it. It might be an error or useful info.
                                if !line.is_empty() {
                                    on_progress(UiEvent::FfmpegOutput(line));
                                }
                            }
                            current_line.clear();
                        }
                    } else {
                        current_line.push(b);
                    }
                }
            }
            Err(_) => break,
        }
    }

    if !current_line.is_empty() {
        let line = String::from_utf8_lossy(&current_line).trim().to_string();
        if (line.contains("frame=") || line.contains("size=")) && line.contains("time=") {
            on_progress(UiEvent::FfmpegOutput(line));
        } else if !line.starts_with("ffmpeg version") && !line.starts_with("built with") && !line.starts_with("configuration:") && !line.starts_with("lib") {
            if !line.is_empty() {
                on_progress(UiEvent::FfmpegOutput(line));
            }
        }
    }
}

fn handle_rsync_progress(stdout: std::process::ChildStdout, on_progress: &impl Fn(UiEvent)) {
    let mut reader = BufReader::new(stdout);
    let mut current_line = Vec::new();
    let mut buffer = [0u8; 1024];

    loop {
        match reader.read(&mut buffer) {
            Ok(0) => break,
            Ok(n) => {
                for &b in &buffer[..n] {
                    if b == b'\r' || b == b'\n' {
                        if !current_line.is_empty() {
                            let line = String::from_utf8_lossy(&current_line).trim().to_string();
                            if !line.is_empty() {
                                on_progress(UiEvent::RsyncOutput(line));
                            }
                            current_line.clear();
                        }
                    } else {
                        current_line.push(b);
                    }
                }
            }
            Err(_) => break,
        }
    }

    if !current_line.is_empty() {
        let line = String::from_utf8_lossy(&current_line).trim().to_string();
        if !line.is_empty() {
            on_progress(UiEvent::RsyncOutput(line));
        }
    }
}

pub fn reencode_tv_episode(media: &MediaState, id: TvEpisodeId, command_str: String, on_progress: impl Fn(UiEvent)) -> Vec<UiEvent> {
    let id = TvEpisodeId(strip_id_prefix(&id.0));
    let (current_path, originals_path) = {
        let media = unlock_media!(media);

        // 1. Find the episode and its current path
        let episode_info = {
            let episodes = media.tv_show_episodes.borrow();
            let tv_shows = media.tv_shows.borrow();
            episodes.iter().find(|e| e.id == id).and_then(|e| {
                tv_shows.iter().find(|s| s.id == e.show_id).map(|s| (s.show_key.clone(), e.series_key.clone()))
            })
        };

        let Some((show_key, series_key)) = episode_info else {
            println!("Episode info not found for reencode: {:?}", id);
            return vec![];
        };

        let current_rel_path = vec![show_key, format!("{}.mkv", series_key)];
        let mut current_path = media.media_dir.join("output");
        for part in &current_rel_path {
            current_path = current_path.join(part);
        }

        if !current_path.exists() {
            println!("File not found for reencode: {:?}", current_path);
            return vec![];
        }

        // 2. Determine the "originals" path
        let mut originals_path = media.media_dir.join("originals");
        for part in &current_rel_path {
            originals_path = originals_path.join(part);
        }

        if originals_path.exists() {
            println!("File already encoded (original exists): {:?}", originals_path);
            on_progress(UiEvent::FfmpegOutput(format!("Error: File already encoded (original exists)")));
            return vec![];
        }

        // 3. Move file to originals
        if let Some(parent) = originals_path.parent() {
            if let Err(e) = fs::create_dir_all(parent) {
                println!("Error creating originals directory {:?}: {:?}", parent, e);
                return vec![];
            }
        }

        println!("Moving {:?} to {:?}", current_path, originals_path);
        if let Err(e) = fs::rename(&current_path, &originals_path) {
            println!("Error moving file to originals: {:?}", e);
            return vec![];
        }
        (current_path, originals_path)
    };

    // 4. Run custom command
    println!("Running reencode on {:?}", originals_path);
    let args = split_shell_command(&command_str);
    if args.is_empty() {
        println!("Empty command provided for reencode");
        return vec![];
    }

    let mut cmd = std::process::Command::new(&args[0]);
    for arg in &args[1..] {
        let replaced = arg.replace("${in}", &originals_path.to_string_lossy())
                          .replace("${out}", &current_path.to_string_lossy());
        cmd.arg(replaced);
    }

    // Ensure -stats is present for progress reporting if it's ffmpeg
    if args[0].contains("ffmpeg") {
        if !args.iter().any(|a| a == "-stats") {
            cmd.arg("-stats");
        }
        if !args.iter().any(|a| a == "-y") {
            cmd.arg("-y");
        }
    }

    let child_res = cmd.stderr(Stdio::piped()).spawn();

    match child_res {
        Ok(mut child) => {
            if let Some(stderr) = child.stderr.take() {
                handle_ffmpeg_progress(stderr, &on_progress);
            }

            let status = child.wait();

            match status {
                Ok(s) if s.success() => {
                    println!("ffmpeg successful for {:?}", current_path);
                    // 5. Update state (file size)
                    if let Ok(metadata) = fs::metadata(&current_path) {
                        let media = unlock_media!(media);
                        let mut titles = media.file_backed_titles.borrow_mut();
                        if let Some(title) = titles.iter_mut().find(|t| t.path == current_path) {
                            title.file_size = metadata.len();
                        }
                    }
                }
                Ok(s) => {
                    println!("ffmpeg failed with status: {:?}", s);
                    on_progress(UiEvent::FfmpegOutput(format!("Error: ffmpeg exited with status {:?}", s)));
                    // Restore original
                    let _ = fs::rename(&originals_path, &current_path);
                }
                Err(e) => {
                    println!("failed to execute ffmpeg: {:?}", e);
                    on_progress(UiEvent::FfmpegOutput(format!("Error: failed to wait for ffmpeg: {:?}", e)));
                    // Restore original
                    let _ = fs::rename(&originals_path, &current_path);
                }
            }
        }
        Err(e) => {
            println!("failed to execute ffmpeg spawn: {:?}", e);
            // Restore the file to original location if spawn failed
            let _ = fs::rename(&originals_path, &current_path);
        }
    }

    let media = unlock_media!(media);
    let mut events = vec![get_garbage_size(&media)];
    events.extend(get_tree_change_action_for_mappable(&media, MappableMediaId::TvEpisode(id)));
    events
}

pub fn unidentify_film_video(media: &MediaState, id: FilmVideoId) -> Vec<UiEvent> {
    let id = FilmVideoId(strip_id_prefix(&id.0));
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let mut info = None;
    {
        let mappable_id = MappableMediaId::FilmVideo(id.clone());
        if let Some(title_id) = media.get_title_id_for_mappable(&mappable_id) {
            let videos = media.film_videos.borrow();
            if let Some(video) = videos.iter().find(|v| v.id == id) {
                let titles = media.file_backed_titles.borrow();
                if let Some(title) = titles.iter().find(|t| t.id == title_id) {
                    info = Some((title.id.clone(), title.path.clone(), video.film_name.clone(), video.name.clone()));
                }
            }
        }
    }

    if let Some((title_id, source_path, film_name, video_name)) = info {
        let dest_folder = media.media_dir.join("Lost & Found");
        if let Err(e) = fs::create_dir_all(&dest_folder) {
            println!("Error creating directory {:?}: {:?}", dest_folder, e);
            return vec![];
        }

        let dest_file_name = if video_name == "Feature Presentation" {
            format!("{} - {}.mkv", film_name, film_name)
        } else {
            format!("{} - {}.mkv", film_name, video_name)
        };
        let dest_path = dest_folder.join(dest_file_name.clone());

        println!("Moving {:?} to {:?}", source_path, dest_path);
        if let Err(e) = fs::rename(&source_path, &dest_path) {
            println!("Error moving file {:?} to {:?}: {:?}", source_path, dest_path, e);
            return vec![];
        }

        // Update state
        if let Some(title) = media.file_backed_titles.borrow_mut().iter_mut().find(|t| t.id == title_id) {
            title.path = dest_path;
            title.mapped_media = None;
            title.collection = "Lost & Found".to_owned();
            title.file_name = dest_file_name;
        }

        // UI events
        events.push(UiEvent::RemoveTreeItemById {
            tree: Tree::Films,
            id: id.0.clone(),
        });

        if let Some(title) = media.file_backed_titles.borrow().iter().find(|t| t.id == title_id) {
             let confirmed = media.confirmed_plays.borrow();
             events.push(UiEvent::AddTreeItem {
                tree: Tree::Files,
                item: TreeItem {
                    id: title.id.0.clone(),
                    parent_id: None,
                    parent_text: title.collection.clone(),
                    text: title.file_name.clone(),
                    color: if title.marked_for_deletion(&confirmed, &media.media_dir) { "red".to_owned() } else { "Default".to_owned() },
                },
                after: None,
            });
        }
    }

    events.push(get_garbage_size(&media));
    events
}

pub fn reencode_film_video(media: &MediaState, id: FilmVideoId, command_str: String, on_progress: impl Fn(UiEvent)) -> Vec<UiEvent> {
    let id = FilmVideoId(strip_id_prefix(&id.0));
    let (current_path, originals_path) = {
        let media = unlock_media!(media);

        // 1. Find the video and its current path
        let video_info = {
            let videos = media.film_videos.borrow();
            videos.iter().find(|v| v.id == id).map(|v| v.get_ideal_storage_path())
        };

        let Some(current_rel_path) = video_info else {
            println!("Video info not found for reencode: {:?}", id);
            return vec![];
        };

        let mut current_path = media.media_dir.join("output");
        for part in &current_rel_path {
            current_path = current_path.join(part);
        }

        if !current_path.exists() {
            println!("File not found for reencode: {:?}", current_path);
            return vec![];
        }

        // 2. Determine the "originals" path
        let mut originals_path = media.media_dir.join("originals");
        for part in &current_rel_path {
            originals_path = originals_path.join(part);
        }

        if originals_path.exists() {
            println!("File already encoded (original exists): {:?}", originals_path);
            on_progress(UiEvent::FfmpegOutput(format!("Error: File already encoded (original exists)")));
            return vec![];
        }

        // 3. Move file to originals
        if let Some(parent) = originals_path.parent() {
            if let Err(e) = fs::create_dir_all(parent) {
                println!("Error creating originals directory {:?}: {:?}", parent, e);
                return vec![];
            }
        }

        println!("Moving {:?} to {:?}", current_path, originals_path);
        if let Err(e) = fs::rename(&current_path, &originals_path) {
            println!("Error moving file to originals: {:?}", e);
            return vec![];
        }
        (current_path, originals_path)
    };

    // 4. Run custom command
    println!("Running reencode on {:?}", originals_path);
    let args = split_shell_command(&command_str);
    if args.is_empty() {
        println!("Empty command provided for reencode");
        return vec![];
    }

    let mut cmd = std::process::Command::new(&args[0]);
    for arg in &args[1..] {
        let replaced = arg.replace("${in}", &originals_path.to_string_lossy())
                          .replace("${out}", &current_path.to_string_lossy());
        cmd.arg(replaced);
    }

    // Ensure -stats is present for progress reporting if it's ffmpeg
    if args[0].contains("ffmpeg") {
        if !args.iter().any(|a| a == "-stats") {
            cmd.arg("-stats");
        }
        if !args.iter().any(|a| a == "-y") {
            cmd.arg("-y");
        }
    }

    let child_res = cmd.stderr(Stdio::piped()).spawn();

    match child_res {
        Ok(mut child) => {
            if let Some(stderr) = child.stderr.take() {
                handle_ffmpeg_progress(stderr, &on_progress);
            }

            let status = child.wait();

            match status {
                Ok(s) if s.success() => {
                    println!("ffmpeg successful for {:?}", current_path);
                    if let Ok(metadata) = fs::metadata(&current_path) {
                        let media = unlock_media!(media);
                        let mut titles = media.file_backed_titles.borrow_mut();
                        if let Some(title) = titles.iter_mut().find(|t| t.path == current_path) {
                            title.file_size = metadata.len();
                        }
                    }
                }
                Ok(s) => {
                    println!("ffmpeg failed with status: {:?}", s);
                }
                Err(e) => {
                    println!("failed to execute ffmpeg: {:?}", e);
                }
            }
        }
        Err(e) => {
            println!("failed to execute ffmpeg spawn: {:?}", e);
            // Restore the file to original location if spawn failed
            let _ = fs::rename(&originals_path, &current_path);
        }
    }

    let media = unlock_media!(media);
    let mut events = vec![get_garbage_size(&media)];
    events.extend(get_tree_change_action_for_mappable(&media, MappableMediaId::FilmVideo(id)));
    events
}


fn get_hash_from_url(url: &str) -> Option<Vec<u8>> {
    let output = std::process::Command::new("ffmpeg")
        .arg("-i").arg(url)
        .arg("-vf").arg("scale=32:32:force_original_aspect_ratio=increase,crop=32:32,format=rgb24")
        .arg("-frames:v").arg("1")
        .arg("-f").arg("rawvideo")
        .arg("-")
        .output().ok()?;

    if output.status.success() && output.stdout.len() >= 3072 {
        Some(output.stdout[0..3072].to_vec())
    } else {
        None
    }
}


#[derive(Clone, Copy)]
struct HashStats {
    mean: f64,
    std_dev: f64,
}

fn get_hash_stats(h: &[u8]) -> HashStats {
    if h.is_empty() { return HashStats { mean: 0.0, std_dev: 0.0 }; }
    let m = h.iter().map(|&x| x as i32).sum::<i32>() as f64 / h.len() as f64;
    let v = h.iter().map(|&x| (x as f64 - m).powi(2)).sum::<f64>() / h.len() as f64;
    HashStats { mean: m, std_dev: v.sqrt() }
}


fn compare_hashes(h1: &[u8], stats1: HashStats, h2: &[u8], stats2: HashStats) -> u32 {
    if h1.len() != 3072 || h2.len() != 3072 { return u32::MAX; }

    let m1 = stats1.mean;
    let s1 = stats1.std_dev;
    let m2 = stats2.mean;
    let s2 = stats2.std_dev;

    if s1 < 0.1 || s2 < 0.1 {
        if s1 < 0.1 && s2 < 0.1 { return 0; }
        return 1000000;
    }

    let mut diff = 0.0;
    for i in 0..3072 {
        let n1 = (h1[i] as f64 - m1) / s1;
        let n2 = (h2[i] as f64 - m2) / s2;
        diff += (n1 - n2).abs();
    }

    (diff * 100.0 / 3.0) as u32
}

fn parse_dar(s: &str) -> Option<f64> {
    if s == "N/A" || s.is_empty() || s == "0:1" { return None; }
    if let Some((w, h)) = s.split_once(':') {
        if let (Ok(w), Ok(h)) = (w.parse::<f64>(), h.parse::<f64>()) {
            if h > 0.0 { return Some(w / h); }
        }
    }
    if let Ok(v) = s.parse::<f64>() {
        if v > 0.1 && v < 10.0 { return Some(v); }
    }
    None
}

fn get_image_aspect_ratio(path_or_url: &str) -> f64 {
    let output = std::process::Command::new("ffprobe")
        .arg("-v").arg("error")
        .arg("-select_streams").arg("v:0")
        .arg("-show_entries").arg("stream=display_aspect_ratio,width,height")
        .arg("-of").arg("json")
        .arg(path_or_url)
        .output();

    if let Ok(output) = output {
        if let Ok(json) = serde_json::from_slice::<serde_json::Value>(&output.stdout) {
            if let Some(stream) = json["streams"].as_array().and_then(|a| a.get(0)) {
                if let Some(dar_str) = stream["display_aspect_ratio"].as_str() {
                    if let Some(dar) = parse_dar(dar_str) {
                        return dar;
                    }
                }
                let w = stream["width"].as_f64().unwrap_or(0.0);
                let h = stream["height"].as_f64().unwrap_or(0.0);
                if h > 0.0 {
                    return w / h;
                }
            }
        }
    }
    1.77777777
}

struct VideoInfo {
    width: u64,
    height: u64,
    codec_name: String,
    dar: f64,
    start_time_ms: u64,
}

fn probe_video(path: &std::path::Path) -> Option<VideoInfo> {
    let output = std::process::Command::new("ffprobe")
        .arg("-v").arg("error")
        .arg("-select_streams").arg("v:0")
        .arg("-show_entries").arg("stream=width,height,codec_name,display_aspect_ratio,start_time")
        .arg("-of").arg("json")
        .arg(path)
        .output();

    if let Ok(output) = output {
        if let Ok(json) = serde_json::from_slice::<serde_json::Value>(&output.stdout) {
            if let Some(stream) = json["streams"].as_array().and_then(|a| a.get(0)) {
                let width = stream["width"].as_u64().unwrap_or(0);
                let height = stream["height"].as_u64().unwrap_or(0);
                let codec_name = stream["codec_name"].as_str().unwrap_or("unknown").to_string();
                let dar_str = stream["display_aspect_ratio"].as_str().unwrap_or("");
                let dar = parse_dar(dar_str).unwrap_or_else(|| {
                    if height > 0 { width as f64 / height as f64 } else { 1.77777777 }
                });
                let start_time_secs = stream["start_time"].as_str()
                    .and_then(|s| s.parse::<f64>().ok())
                    .or_else(|| stream["start_time"].as_f64())
                    .unwrap_or(0.0);
                let start_time_ms = (start_time_secs * 1000.0) as u64;

                return Some(VideoInfo {
                    width,
                    height,
                    codec_name,
                    dar,
                    start_time_ms,
                });
            }
        }
    }
    None
}

pub fn match_scan(media: &MediaState, id: FileBackedTitleId, command_str: String, on_progress: impl Fn(UiEvent)) -> Vec<UiEvent> {
    let path = {
        let media = unlock_media!(media);
        let titles = media.file_backed_titles.borrow();
        titles.iter().find(|t| t.id() == id.0).map(|t| t.path.clone())
    };

    let Some(path) = path else { return vec![]; };

    let episodes_with_stills = {
        let media = unlock_media!(media);
        let episodes = media.tv_show_episodes.borrow();
        episodes.iter().filter(|e| e.still_path.is_some()).cloned().collect::<Vec<_>>()
    };

    if episodes_with_stills.is_empty() {
        on_progress(UiEvent::FfmpegOutput("Match Scan: No episodes with screenshots found".to_owned()));
        return vec![];
    }

    on_progress(UiEvent::FfmpegOutput("Match Scan: Probing video...".to_owned()));
    let video_info = probe_video(&path);
    let (video_dar, start_time_ms) = if let Some(ref info) = video_info {
        (info.dar, info.start_time_ms)
    } else {
        let dar = get_image_aspect_ratio(&path.to_string_lossy());
        (dar, 0)
    };
    on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Video aspect ratio: {:.2}", video_dar)));

    on_progress(UiEvent::FfmpegOutput("Match Scan: Probing stills...".to_owned()));
    let target_ar = if let Some(ep) = episodes_with_stills.iter().find(|e| e.still_path.is_some()) {
        let still_url = format!("https://image.tmdb.org/t/p/original{}", ep.still_path.as_ref().unwrap());
        get_image_aspect_ratio(&still_url)
    } else {
        1.77777777
    };
    on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Target aspect ratio: {:.2}", target_ar)));

    on_progress(UiEvent::FfmpegOutput("Match Scan: Extracting frames (10fps)...".to_owned()));
    let mut sample_hashes = Vec::new();

    let mut cmd = std::process::Command::new("ffmpeg");
    let vf;
    if command_str.contains("nvenc") {
        let cuvid_decoder = video_info.as_ref().and_then(|info| {
            match info.codec_name.as_str() {
                "h264" => Some("h264_cuvid"),
                "hevc" => Some("hevc_cuvid"),
                "mpeg2video" => Some("mpeg2_cuvid"),
                "mpeg4" => Some("mpeg4_cuvid"),
                "vc1" => Some("vc1_cuvid"),
                "vp8" => Some("vp8_cuvid"),
                "vp9" => Some("vp9_cuvid"),
                _ => None,
            }
        });

        cmd.arg("-hwaccel").arg("cuda");
        if let Some(decoder) = cuvid_decoder {
            cmd.arg("-c:v").arg(decoder);
        }
        cmd.arg("-hwaccel_output_format").arg("cuda");
        vf = format!("setpts=PTS-STARTPTS,scale_cuda=32:32:force_original_aspect_ratio=increase,hwdownload,format=nv12,fps=10,crop=32:32,format=rgb24");
    } else {
        cmd.arg("-hwaccel").arg("auto");
        vf = format!("setpts=PTS-STARTPTS,fps=10,scale=32:32:force_original_aspect_ratio=increase,crop=32:32,format=rgb24");
    }

    let child_proc = cmd
        .arg("-ss").arg("0")
        .arg("-i").arg(&path)
        .arg("-map").arg("0:v:0")
        .arg("-vf").arg(&vf)
        .arg("-f").arg("rawvideo")
        .arg("-")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn();

    match child_proc {
        Ok(mut child) => {
            let mut stdout = child.stdout.take().unwrap();
            let stderr = child.stderr.take().unwrap();

            let stderr_handle = thread::spawn(move || {
                let mut s = String::new();
                let mut reader = BufReader::new(stderr);
                let _ = reader.read_to_string(&mut s);
                s
            });

            let mut buffer = [0u8; 3072];
            while stdout.read_exact(&mut buffer).is_ok() {
                sample_hashes.push(buffer.to_vec());
                if sample_hashes.len() % 500 == 0 {
                    on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Extracted {} frames ({:.2}s)...", sample_hashes.len(), (sample_hashes.len() as f64) * 0.1)));
                }
            }
            let _ = child.wait();
            let stderr_output = stderr_handle.join().unwrap_or_default();

            if sample_hashes.is_empty() {
                on_progress(UiEvent::FfmpegOutput("Match Scan: Failed to extract frames".to_owned()));
                if !stderr_output.is_empty() {
                    for line in stderr_output.lines() {
                        if !line.trim().is_empty() {
                            on_progress(UiEvent::FfmpegOutput(format!("ffmpeg: {}", line)));
                        }
                    }
                }
                return vec![];
            }
        }
        Err(e) => {
            on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Failed to spawn ffmpeg: {}", e)));
            return vec![];
        }
    }

    let mut best_match = None;
    let mut best_sample_index = 0;
    let mut min_diff = u32::MAX;
    let mut all_results = std::collections::HashMap::new();

    let total_episodes = episodes_with_stills.len();
    on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Comparing against {} episodes...", total_episodes)));

    on_progress(UiEvent::FfmpegOutput("Match Scan: Pre-calculating video frame statistics...".to_owned()));
    let sample_stats: Vec<_> = sample_hashes.par_iter().map(|h| get_hash_stats(h)).collect();

    let mut episode_targets = Vec::new();
    for (idx, ep) in episodes_with_stills.into_iter().enumerate() {
        let cached_hash = {
            let hash_lock = ep.still_hash.lock().unwrap();
            hash_lock.clone()
        };

        let target_hash = if let Some(h) = cached_hash {
            Some(h)
        } else {
            let still_path = ep.still_path.as_ref().unwrap();

            let disk_hash = {
                let m = unlock_media!(media);
                m.get_still_hash(still_path)
            };

            if let Some(h) = disk_hash {
                let mut hash_lock = ep.still_hash.lock().unwrap();
                *hash_lock = Some(h.clone());
                Some(h)
            } else {
                on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Hashing episode {}/{} ({})...", idx + 1, total_episodes, ep.series_key())));
                let url = format!("https://image.tmdb.org/t/p/w500{}", still_path);
                if let Some(h) = get_hash_from_url(&url) {
                    let mut hash_lock = ep.still_hash.lock().unwrap();
                    *hash_lock = Some(h.clone());

                    let m = unlock_media!(media);
                    m.save_still_hash(still_path, &h);

                    Some(h)
                } else {
                    None
                }
            }
        };

        if let Some(target_hash) = target_hash {
            let stats = get_hash_stats(&target_hash);
            episode_targets.push((ep, target_hash, stats));
        }
    }

    on_progress(UiEvent::FfmpegOutput("Match Scan: Comparing in parallel...".to_owned()));
    let results: Vec<_> = episode_targets.into_par_iter().map(|(ep, target_hash, target_stats)| {
        let mut ep_min_diff = u32::MAX;
        let mut ep_best_sample_index = 0;

        for (s_idx, (sample_hash, sample_stats)) in sample_hashes.iter().zip(sample_stats.iter()).enumerate() {
            let diff = compare_hashes(&target_hash, target_stats, sample_hash, *sample_stats);
            if diff < ep_min_diff {
                ep_min_diff = diff;
                ep_best_sample_index = s_idx;
            }
        }
        (ep, ep_min_diff, ep_best_sample_index)
    }).collect();

    for (ep, ep_min_diff, ep_best_sample_index) in results {
        all_results.insert(ep.id.0.clone(), MatchResult {
            diff: ep_min_diff,
            position_ms: start_time_ms + (ep_best_sample_index as u64) * 100,
        });

        if ep_min_diff < min_diff {
            min_diff = ep_min_diff;
            best_match = Some(ep.id.clone());
            best_sample_index = ep_best_sample_index;
        }
    }

    if let Some(ep_id) = best_match {
        on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Finished. Best diff: {} at index {} ({:.2}s)", min_diff, best_sample_index, (best_sample_index as f64) * 0.1)));

        let mut events = vec![UiEvent::MatchResults {
            tree: Tree::TvShows,
            results: all_results,
        }];

        let pos_ms = start_time_ms + (best_sample_index as u64) * 100;
        events.push(UiEvent::SelectTreeItem {
            tree: Tree::TvShows,
            id: ep_id.0.clone(),
        });

        // threshold for "good enough" match for seeking.
        if min_diff < 40000 {
            on_progress(UiEvent::FfmpegOutput(format!("Match Scan: Match confirmed! Seeking to {}ms", pos_ms)));
            events.push(UiEvent::SeekPlayer {
                position_ms: pos_ms,
            });
        }
        return events;
    } else {
        on_progress(UiEvent::FfmpegOutput("Match Scan: No match found".to_owned()));
    }

    vec![]
}

pub fn fetch_tmdb_still(media: &MediaState, id: MappableMediaId) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let (still_path, name) = match &id {
        MappableMediaId::TvEpisode(eid) => {
            let episodes = media.tv_show_episodes.borrow();
            let Some(ep) = episodes.iter().find(|e| e.id == *eid) else { return vec![]; };
            (ep.still_path.clone(), format!("S{:0>2}E{:0>2}.jpg", ep.season_number, ep.number))
        }
        MappableMediaId::FilmVideo(fvid) => {
            let videos = media.film_videos.borrow();
            let Some(video) = videos.iter().find(|v| v.id == *fvid) else { return vec![]; };
            let films = media.films.borrow();
            let Some(film) = films.iter().find(|f| f.id == video.film_id) else { return vec![]; };
            (film.poster_path.clone(), "poster.jpg".to_string())
        }
    };

    let Some(path) = still_path else { return vec![]; };

    let filename = format!("{}_{}", id.id(), name);
    let local_path = media.stills_dir.join(&filename);

    if !local_path.exists() {
        let full_url = format!("https://image.tmdb.org/t/p/w500{}", path);
        if let Ok(buf) = ureq::get(&full_url).call().and_then(|res| res.into_body().read_to_vec()) {
            let _ = std::fs::write(&local_path, buf);
        } else {
            return vec![];
        }
    }

    vec![UiEvent::SetTmdbStill {
        path: local_path.to_string_lossy().into_owned(),
    }]
}

pub fn delete_film(media: &MediaState, id: String) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let raw_id = strip_id_prefix(&id);
    let film_id = FilmId(raw_id.clone());

    let mut events = Vec::new();

    // 1. Identify all videos to be removed and their mappings
    let mut videos_to_remove = Vec::new();
    {
        let videos = media.film_videos.borrow();
        for v in videos.iter() {
            if v.film_id == film_id {
                videos_to_remove.push(v.id.clone());
            }
        }
    }

    // 2. Unmap files in Files tree
    {
        let mut titles = media.file_backed_titles.borrow_mut();
        for title in titles.iter_mut() {
            if let Some(MediaId::FilmVideo(v_id)) = &title.mapped_media {
                if videos_to_remove.contains(v_id) {
                    title.mapped_media = None;
                    events.push(get_tree_change_action_for_mapping_file(title.id.clone(), false));
                }
            }
        }
    }

    // 3. Remove film from tree
    events.push(UiEvent::RemoveTreeItemById {
        tree: Tree::Films,
        id: raw_id,
    });

    media.delete_film(&film_id);
    events.push(get_garbage_size(&media));
    events
}

pub fn delete_film_video(media: &MediaState, id: String) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let raw_id = strip_id_prefix(&id);
    let video_id = FilmVideoId(raw_id.clone());

    let mut events = Vec::new();

    // 1. Unmap files
    {
        let mut titles = media.file_backed_titles.borrow_mut();
        for title in titles.iter_mut() {
            if let Some(MediaId::FilmVideo(v_id)) = &title.mapped_media {
                if *v_id == video_id {
                    title.mapped_media = None;
                    events.push(get_tree_change_action_for_mapping_file(title.id.clone(), false));
                }
            }
        }
    }

    // 2. Remove from tree
    events.push(UiEvent::RemoveTreeItemById {
        tree: Tree::Films,
        id: raw_id,
    });

    media.delete_film_video(&video_id);
    events.push(get_garbage_size(&media));
    events
}

pub fn collect_garbage(media: &MediaState) -> Vec<UiEvent> {
    let media = unlock_media!(media);
    let mut events = Vec::new();

    let to_delete = {
        let confirmed = media.confirmed_plays.borrow();
        let titles = media.file_backed_titles.borrow();
        titles.iter()
            .filter(|t| t.marked_for_deletion(&confirmed, &media.media_dir))
            .map(|t| (t.id.clone(), t.path.clone()))
            .collect::<Vec<_>>()
    };

    let mut confirmed_plays_changed = false;

    for (id, path) in to_delete {
        let mappables = media.get_mappables_for_title(&id);

        println!("[rust] Garbage collecting: {:?}", path);
        if path.exists() {
            if let Err(e) = fs::remove_file(&path) {
                println!("Error removing file {:?}: {:?}", path, e);
                continue;
            }
        }

        // If it was a confirmed play, remove it from the set
        if media.confirmed_plays.borrow_mut().remove(&path) {
            confirmed_plays_changed = true;
        }

        // Remove from memory
        media.file_backed_titles.borrow_mut().retain(|t| t.id != id);

        // Remove from Files tree
        events.push(UiEvent::RemoveTreeItemById {
            tree: Tree::Files,
            id: id.0,
        });

        // Update colors for mappables
        for mappable in mappables {
            let (tree, raw_id) = match mappable {
                MappableMediaId::TvEpisode(id) => (Tree::TvShows, id.0),
                MappableMediaId::FilmVideo(id) => (Tree::Films, id.0),
            };
            events.push(UiEvent::ChangeTreeItem {
                tree,
                id: raw_id,
                change: ChangeColor("Default".to_owned()),
            });
        }
    }

    if confirmed_plays_changed {
        media.save_confirmed_plays();
    }

    // Now remove empty folders in media_dir, except output and originals
    remove_empty_folders(&media.media_dir, false);

    events.push(get_garbage_size(&media));
    events
}

pub fn restore_original(media: &crate::requests::MediaState, id: MappableMediaId) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    let titles = media.file_backed_titles.borrow();
    let mut encoded_path = None;

    if let Some(tid) = media.get_title_id_for_mappable(&id) {
        if let Some(title) = titles.iter().find(|t| t.id == tid && media.is_in_output_dir(&t.path)) {
            encoded_path = Some(title.path.clone());
        }
    }
    drop(titles);

    let Some(encoded_path) = encoded_path else {
        println!("[rust] Cannot restore original: no encoded file found for {:?}", id);
        return vec![];
    };

    let out_dir = media.media_dir.join("output");
    let Ok(rel) = encoded_path.strip_prefix(&out_dir) else {
        println!("[rust] Cannot restore original: encoded path is not in output dir");
        return vec![];
    };

    let originals_dir = media.media_dir.join("originals");
    let original_path = originals_dir.join(rel);

    if !original_path.exists() {
        println!("[rust] Cannot restore original: original file not found at {:?}", original_path);
        return vec![];
    }

    // 1. Move encoded file to Lost & Found with .d extension
    let lost_found_dir = media.media_dir.join("Lost & Found");
    if !lost_found_dir.exists() {
        let _ = fs::create_dir_all(&lost_found_dir);
    }

    let file_name = encoded_path.file_name().unwrap().to_string_lossy();
    let dest_name = format!("{}.d", file_name);
    let dest_path = lost_found_dir.join(dest_name);

    println!("[rust] Moving encoded file {:?} to {:?}", encoded_path, dest_path);
    if let Err(e) = fs::rename(&encoded_path, &dest_path) {
        println!("[rust] Failed to move encoded file: {:?}", e);
        return vec![];
    }

    // 2. Move original file back to output
    println!("[rust] Moving original file {:?} to {:?}", original_path, encoded_path);
    if let Err(e) = fs::rename(&original_path, &encoded_path) {
        println!("[rust] Failed to move original file: {:?}", e);
        // Try to move back encoded file? Probably better to just leave it as is and report error.
        return vec![];
    }

    media.read_local_media();
    media.sort_collections();

    let mut events = vec![UiEvent::ClearTrees];
    events.extend(build_files_tree(&media));
    events.extend(build_films_tree(&media));
    events.extend(build_tv_shows_tree(&media));
    events.push(get_garbage_size(&media));

    events
}

fn remove_empty_folders(path: &std::path::Path, can_delete: bool) {
    if !path.is_dir() {
        return;
    }

    let entries = match fs::read_dir(path) {
        Ok(e) => e,
        Err(_) => return,
    };

    for entry in entries.filter_map(|e| e.ok()) {
        let p = entry.path();
        if p.is_dir() {
            let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
            let should_exclude = name == "output" || name == "originals";
            remove_empty_folders(&p, !should_exclude);
        }
    }

    if can_delete {
        if let Ok(mut entries) = fs::read_dir(path) {
            if entries.next().is_none() {
                println!("[rust] Removing empty directory: {:?}", path);
                let _ = fs::remove_dir(path);
            }
        }
    }
}
