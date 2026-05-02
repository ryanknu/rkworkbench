use crate::media::{MappableMediaId, FileBackedTitleId, Film, FilmId, MediaId, TvShow, TvShowEpisode, TvShowId};
use crate::tmdb::{TmdbItem, TmdbTvShow, TmdbTvShowSeason};
use crate::ui::{build_files_tree, build_films_tree, build_tv_shows_tree, get_add_tree_item_for_film, get_add_tree_item_for_tv_show, get_garbage_size, get_tmdb_key_event, get_tree_change_action_for_mappable, get_tree_change_action_for_mapping_file, Tree, UiEvent};
use serde::{Deserialize, Serialize};
use walkdir::WalkDir;
use std::fs;
use std::io::Write;
use std::sync::{Mutex, OnceLock};
use crate::convert::{FilmVideoBuilder, TvShowEpisodeBuilder};

type MediaState = OnceLock<Mutex<crate::media::MediaState>>;

#[derive(Clone, Serialize, Deserialize)]
pub enum IncomingRequest {
    LookupFilm(String, Option<String>),
    LookupTv(String, Option<String>),
    MapMedia(FileBackedTitleId, MappableMediaId),
    PerformInitialLoad,
    RenameIdentified,
    RsyncRequest(String),
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

pub fn read_local_media(media: &MediaState) -> Vec<UiEvent> {
    let media = unlock_media!(media);

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

    let mut events = build_files_tree(&media);
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

    let Some(mapping) = media.map_media(&from, &to) else {
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
        get_add_tree_item_for_film(&media, film.id.0.to_string(), film.id.0.to_string(), film.name.clone(), "Feature Presentation".to_owned())
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

pub fn rsync_show(media: &MediaState, id: String) -> Vec<UiEvent> {
    let media = unlock_media!(media);

    let folder_name = if let Some(show) = media.get_show_by_id(&TvShowId(id.clone())) {
        Some(show.show_key.clone())
    } else if let Some(film) = media.get_film_by_id(&FilmId(id.clone())) {
        Some(film.film_key().to_owned())
    } else {
        None
    };

    let Some(folder_name) = folder_name else {
        println!("Media not found for rsync: {:?}", id);
        return vec![];
    };

    let source = media.media_dir.join("output").join(&folder_name);
    let destination = format!("root@10.4.6.2:/mnt/user/emby/tv/{}/", folder_name);

    println!("[rust] rsyncing {:?} to {:?}", source, destination);

    // Spawn rsync
    let status = std::process::Command::new("rsync")
        .arg("-a")
        .arg(format!("{}/", source.to_string_lossy()))
        .arg(&destination)
        .status();

    match status {
        Ok(s) if s.success() => {
            println!("[rust] rsync successful for {}", folder_name);
            // Log rsynced files
            let log_file = media.config_dir.join("rsynced_files.txt");
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
        }
        Err(e) => {
            println!("[rust] failed to execute rsync: {:?}", e);
        }
    }

    vec![]
}
