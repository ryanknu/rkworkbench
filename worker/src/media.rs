use std::cell::{Ref, RefCell};
use std::cmp::PartialEq;
use std::collections::HashSet;
use std::env::home_dir;
use std::fmt::Debug;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::str::FromStr;
use serde::{Deserialize, Serialize};
use ulid::Ulid;
use walkdir::WalkDir;
use crate::tmdb::TmdbCache;

pub struct MediaState {
    pub(crate) tmdb_cache: TmdbCache,
    pub(crate) tmdb_api_key: Option<String>,
    pub(crate) media_dir: PathBuf,
    pub(crate) config_dir: PathBuf,
    pub(crate) hashes_dir: PathBuf,
    pub(crate) stills_dir: PathBuf,
    pub(crate) file_backed_titles: RefCell<Vec<FileBackedTitle>>,
    pub(crate) films: RefCell<Vec<Film>>,
    pub(crate) film_videos: RefCell<Vec<FilmVideo>>,
    pub(crate) tv_shows: RefCell<Vec<TvShow>>,
    pub(crate) tv_show_episodes: RefCell<Vec<TvShowEpisode>>,
    pub(crate) confirmed_plays: RefCell<HashSet<PathBuf>>,
    pub(crate) stitch_list: RefCell<Vec<String>>,
}

impl Debug for MediaState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("MediaState")
            .field("media_dir", &self.media_dir)
            .field("config_dir", &self.config_dir)
            .finish()
    }
}

impl MediaState {
    fn get_config() -> PathBuf {
        let cfg = std::env::var("XDG_CONFIG_HOME")
            .ok()
            .and_then(|e| PathBuf::from_str(&e).ok());

        if let Some(cfg) = cfg {
            return cfg.join("rkworkbench");
        }

        let Some(dir) = home_dir() else {
            panic!("Can't locate home directory. Sorry.");
        };

        dir.join(".config").join("rkworkbench")
    }

    pub fn new(media_dir: PathBuf) -> Self {
        let mut media_dir = media_dir;
        let config_dir = Self::get_config();
        if !media_dir.exists() {
            println!("Launch media directory does not exist, attempting to load from config file.");
            let wd_file = config_dir.join("wd.txt");
            println!("Config file :{:?}", wd_file);
            if wd_file.exists() {
                if let Ok(wd) = std::fs::read_to_string(&wd_file) {
                    media_dir = PathBuf::from_str(wd.trim()).unwrap();
                }
            }
        } else {
            // Launch media dir exists, we should write it to disk.
            let wd_file = config_dir.join("wd.txt");
            std::fs::create_dir_all(&config_dir).ok();
            std::fs::write(&wd_file, media_dir.to_string_lossy().as_bytes()).ok();
        }

        if !media_dir.exists() {
            panic!("Cannot find media.");
        }

        // Canonicalize to absolute path if possible
        if let Ok(abs) = std::fs::canonicalize(&media_dir) {
            media_dir = abs;
        }

        // Read api key
        let tmdb_key_file = config_dir.join("tmdb.key");
        let tmdb_api_key = if tmdb_key_file.exists() {
            std::fs::read_to_string(&tmdb_key_file).map(|n| Some(n)).unwrap_or(None)
        } else {
            None
        };

        let hashes_dir = config_dir.join("still_hashes_v5");
        std::fs::create_dir_all(&hashes_dir).ok();

        let stills_dir = config_dir.join("stills");
        std::fs::create_dir_all(&stills_dir).ok();

        Self {
            tmdb_cache: TmdbCache {
                dir: config_dir.clone(),
                tmdb_base_url: "https://api.themoviedb.org/3".to_string(),
            },
            tmdb_api_key,
            media_dir,
            config_dir,
            hashes_dir,
            stills_dir,
            file_backed_titles: Default::default(),
            films: Default::default(),
            film_videos: Default::default(),
            tv_shows: Default::default(),
            tv_show_episodes: Default::default(),
            confirmed_plays: Default::default(),
            stitch_list: Default::default(),
        }
    }

    pub fn load_confirmed_plays(&self) {
        let plays_file = self.config_dir.join("plays.txt");
        self.confirmed_plays.borrow_mut().clear();
        if plays_file.exists() {
            if let Ok(content) = std::fs::read_to_string(&plays_file) {
                let mut confirmed = self.confirmed_plays.borrow_mut();
                for line in content.lines() {
                    let path = PathBuf::from(line.trim());
                    if !path.as_os_str().is_empty() {
                        confirmed.insert(path);
                    }
                }
            }
        }
    }

    pub fn get_still_hash(&self, still_path: &str) -> Option<Vec<u8>> {
        let filename = Path::new(still_path).file_name()?.to_str()?;
        let hash_path = self.hashes_dir.join(format!("{}.bin", filename));
        if hash_path.exists() {
            std::fs::read(hash_path).ok()
        } else {
            None
        }
    }

    pub fn save_still_hash(&self, still_path: &str, hash: &[u8]) {
        if let Some(filename) = Path::new(still_path).file_name().and_then(|f| f.to_str()) {
            let hash_path = self.hashes_dir.join(format!("{}.bin", filename));
            std::fs::write(hash_path, hash).ok();
        }
    }

    pub fn add_confirmed_play(&self, path: PathBuf) {
        let plays_file = self.config_dir.join("plays.txt");
        let mut confirmed = self.confirmed_plays.borrow_mut();
        if confirmed.insert(path.clone()) {
            if let Ok(mut file) = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(plays_file)
            {
                let _ = writeln!(file, "{}", path.to_string_lossy());
            }
        }
    }

    pub fn save_confirmed_plays(&self) {
        let plays_file = self.config_dir.join("plays.txt");
        if let Ok(mut file) = std::fs::File::create(plays_file) {
            let confirmed = self.confirmed_plays.borrow();
            for path in confirmed.iter() {
                let _ = writeln!(file, "{}", path.to_string_lossy());
            }
        }
    }

    pub fn get_mappables_for_title(&self, title_id: &FileBackedTitleId) -> Vec<MappableMediaId> {
        let titles = self.file_backed_titles.borrow();
        let Some(title) = titles.iter().find(|t| t.id == *title_id) else {
            return vec![];
        };

        if let Some(mapped) = &title.mapped_media {
            match mapped {
                MediaId::TvEpisode(id) => return vec![MappableMediaId::TvEpisode(id.clone())],
                MediaId::FilmVideo(id) => return vec![MappableMediaId::FilmVideo(id.clone())],
                MediaId::SemanticNameKey(key) => {
                    let mut result = Vec::new();
                    for episode in self.tv_show_episodes.borrow().iter() {
                        if let Some(show_key) = self.tv_show_key(&episode.show_id) {
                            let expected_key = format!("{} - {}", show_key, episode.series_key);
                            if *key == expected_key {
                                result.push(MappableMediaId::TvEpisode(episode.id.clone()));
                                continue;
                            }
                            let expected_key_with_name = format!("{} - {} - {}", show_key, episode.series_key, episode.name);
                            if *key == expected_key_with_name {
                                result.push(MappableMediaId::TvEpisode(episode.id.clone()));
                            }
                        }
                    }
                    for video in self.film_videos.borrow().iter() {
                        if video.ty == "FeaturePresentation" {
                            let expected_key = format!("{} - {}", video.film_key, video.film_key);
                            if *key == expected_key {
                                result.push(MappableMediaId::FilmVideo(video.id.clone()));
                                continue;
                            }
                        }
                        let expected_key_with_name = format!("{} - {}", video.film_key, video.name);
                        if *key == expected_key_with_name {
                            result.push(MappableMediaId::FilmVideo(video.id.clone()));
                        }
                    }
                    return result;
                }
                _ => {}
            }
        }
        vec![]
    }
}

#[derive(Clone)]
pub enum AvFormat {
    Audio,
    Video,
}

#[derive(Clone)]
pub enum MediaType {
    Film,
    TvShow,
    Song,
}

#[derive(Clone)]
pub enum MediaId {
    SemanticNameKey(String),
    TvShow(TvShowId),
    TvEpisode(TvEpisodeId),
    Film(FilmId),
    FilmVideo(FilmVideoId),
}

#[derive(Clone, Deserialize, Serialize, Debug)]
pub enum MappableMediaId {
    TvEpisode(TvEpisodeId),
    FilmVideo(FilmVideoId),
}

/// Represents media items that are actually playable. E.g. TV shows and albums are not directly
/// playable, you must play their episodes or tracks.
pub enum MediaItem {
    FilmVideo(FilmVideo),
    TvShowEpisode(TvShowEpisode),
}

#[derive(Clone, Deserialize, Serialize, PartialEq, Debug)]
pub struct TvShowId(pub String);
#[derive(Clone, Deserialize, Serialize, PartialEq, Debug)]
pub struct FilmId(pub String);
#[derive(Clone, Deserialize, Serialize, PartialEq, Debug)]
pub struct FilmVideoId(pub String);
#[derive(Clone, Deserialize, Serialize, PartialEq, Debug)]
pub struct TvEpisodeId(pub String);
#[derive(Clone, Deserialize, PartialEq, Serialize, Debug)]
pub struct FileBackedTitleId(pub String);

#[derive(Clone)]
pub struct TvShow {
    pub id: TvShowId,
    pub tmdb_id: usize,
    pub name: String,
    pub overview: String,
    pub original_language: String,
    pub first_air_date: String,
    pub runtime: Option<usize>,
    pub show_key: String,
    pub poster_path: Option<String>,
}

pub struct TvShowSeason {
    key: String,
    path: PathBuf,
}

#[derive(Clone)]
pub struct TvShowEpisode {
    pub id: TvEpisodeId,
    pub tmdb_id: usize,
    pub season_number: usize,
    pub number: usize,
    pub name: String,
    pub overview: String,
    pub air_date: Option<String>,
    pub runtime: Option<usize>,
    pub show_name: String,
    pub show_id: TvShowId,
    pub series_key: String,
    pub still_path: Option<String>,
    pub still_hash: std::sync::Arc<std::sync::Mutex<Option<Vec<u8>>>>,
}

#[derive(Debug, Clone)]
pub struct Film {
    pub id: FilmId,
    pub tmdb_id: usize,
    pub name: String,
    pub overview: String,
    pub original_language: String,
    pub release_date: String,
    pub runtime: Option<usize>,
    pub film_key: String,
    pub poster_path: Option<String>,
}

#[derive(Clone)]

pub struct FilmVideo {
    pub(crate) id: FilmVideoId,
    pub(crate) film_id: FilmId,
    // RK: tmdb_id for FilmVideo is likely an Option<String>, feature presentations don't have one.
    pub(crate) tmdb_id: String,
    pub(crate) name: String,
    pub(crate) ty: String,
    pub(crate) film_name: String,
    pub(crate) film_key: String,
}

#[derive(Clone)]
pub struct FileBackedTitle {
    pub(crate) id: FileBackedTitleId,
    pub(crate) format: AvFormat,
    pub(crate) path: PathBuf,
    pub(crate) mapped_media: Option<MediaId>,
    pub(crate) collection: String,
    pub(crate) file_name: String,
    pub(crate) file_size: u64,
}

impl MediaState {
    pub fn is_in_output_dir(&self, path: &Path) -> bool {
        let out_dir = self.media_dir.join("output");
        path.starts_with(out_dir)
    }

    pub fn is_in_originals_dir(&self, path: &Path) -> bool {
        let originals_dir = self.media_dir.join("originals");
        path.starts_with(originals_dir)
    }

    fn is_mkv(&self, e: walkdir::DirEntry) -> Option<walkdir::DirEntry> {
        if e.path().extension().map_or(false, |ext| ext == "mkv") {
            Some(e)
        } else {
            None
        }
    }

    pub fn read_local_media(&self) {
        self.file_backed_titles.borrow_mut().clear();
        self.films.borrow_mut().clear();
        self.film_videos.borrow_mut().clear();
        self.tv_shows.borrow_mut().clear();
        self.tv_show_episodes.borrow_mut().clear();
        println!("[rust] scanning local media in {:?}", self.media_dir);
        for entry in WalkDir::new(&self.media_dir).into_iter().filter_map(|e| e.ok()) {
            if entry.path().is_dir() {
                continue;
            }

            let path = entry.path();

            // get the last two path parts as a tuple
            let mut components = path.components();
            let components = (components.next_back(), components.next_back());

            let (Some(file_name), Some(folder_name)) = components else {
                continue;
            };

            let (file_name, folder_name) = (file_name.as_os_str().to_str().unwrap_or_default(), folder_name.as_os_str().to_str().unwrap_or_default());

            // originals are counted towards garbage but shouldn't be mapped.
            let mapped_media = if self.is_in_output_dir(entry.path()) {
                let file_name_clean = file_name.replace(".mkv", "");
                let out_dir = self.media_dir.join("output");
                if let Ok(rel) = path.strip_prefix(&out_dir) {
                    let rel_parts: Vec<_> = rel.components().map(|c| c.as_os_str().to_str().unwrap_or_default()).collect();
                    if rel_parts.len() == 3 && rel_parts[1] == "extras" {
                        Some(MediaId::SemanticNameKey(format!("{} - {}", rel_parts[0], file_name_clean)))
                    } else {
                        Some(MediaId::SemanticNameKey(format!("{folder_name} - {file_name_clean}")))
                    }
                } else {
                    Some(MediaId::SemanticNameKey(format!("{folder_name} - {file_name_clean}")))
                }
            } else {
                None
            };

            let mut titles = self.file_backed_titles.borrow_mut();
            titles.push(FileBackedTitle {
                id: FileBackedTitleId(Ulid::new().to_string()),
                format: AvFormat::Video,
                path: path.to_owned(),
                mapped_media,
                collection: folder_name.to_owned(),
                file_name: file_name.to_owned(),
                file_size: entry.metadata().map(|m| m.len()).unwrap_or(0),
            })
        }

        let mut titles = self.file_backed_titles.borrow_mut();
        titles.sort_by_key(|title| format!("{}{}", title.collection, title.file_name));
    }

    pub fn file_backed_titles(&self) -> Ref<'_, [FileBackedTitle]> {
        Ref::map(self.file_backed_titles.borrow(), |v| v.as_slice())
    }

    pub fn tv_show_episodes(&self) -> Ref<'_, [TvShowEpisode]> {
        Ref::map(self.tv_show_episodes.borrow(), |v| v.as_slice())
    }

    pub fn films(&self) -> Ref<'_, [Film]> {
        Ref::map(self.films.borrow(), |v| v.as_slice())
    }

    pub fn film_videos(&self) -> Ref<'_, [FilmVideo]> {
        Ref::map(self.film_videos.borrow(), |v| v.as_slice())
    }

    pub fn get_show_by_id(&self, id: &TvShowId) -> Option<TvShow> {
        self.tv_shows.borrow().iter().find(|show| show.id == *id).cloned()
    }

    pub fn get_film_by_id(&self, id: &FilmId) -> Option<Film> {
        self.films.borrow().iter().find(|film| film.id == *id).cloned()
    }

    pub fn tv_show_key(&self, id: &TvShowId) -> Option<String> {
        self.tv_shows.borrow().iter().find(|show| show.id.0 == id.0).map(|show| show.show_key.to_owned())
    }

    /// Maps a file on disk to a verified media item. If the mapping is successful, we return the
    /// item. Otherwise we return None.
    pub fn map_media(&self, from: &FileBackedTitleId, to: &MappableMediaId) -> Option<MediaItem> {
        let mut titles = self.file_backed_titles.borrow_mut();

        for title in titles.iter_mut() {
            if title.id == *from {
                match to {
                    MappableMediaId::TvEpisode(id) => {
                        for item in self.tv_show_episodes.borrow().iter() {
                            if item.id == *id {
                                let mut new_title = title.to_owned();
                                new_title.mapped_media = Some(MediaId::TvEpisode(item.id.clone()));
                                *title = new_title;
                                return Some(MediaItem::TvShowEpisode(item.to_owned()));
                            }
                        }
                    }
                    MappableMediaId::FilmVideo(id) => {
                        for item in self.film_videos.borrow().iter() {
                            if item.id == *id {
                                let mut new_title = title.to_owned();
                                new_title.mapped_media = Some(MediaId::FilmVideo(item.id.clone()));
                                *title = new_title;
                                return Some(MediaItem::FilmVideo(item.to_owned()));
                            }
                        }
                    }
                }
            }
        }

        None
    }

    pub fn is_on_disk(&self, id: &MappableMediaId) -> bool {
        self.get_on_disk_file_size(id).is_some()
    }

    pub fn is_confirmed_play(&self, id: &MappableMediaId) -> bool {
        if let Some(tid) = self.get_title_id_for_mappable(id) {
            let titles = self.file_backed_titles.borrow();
            if let Some(title) = titles.iter().find(|t| t.id == tid) {
                let confirmed = self.confirmed_plays.borrow();
                return confirmed.contains(&title.path);
            }
        }
        false
    }

    pub fn has_original(&self, id: &MappableMediaId) -> bool {
        let titles = self.file_backed_titles.borrow();
        if let Some(tid) = self.get_title_id_for_mappable(id) {
            if let Some(title) = titles.iter().find(|t| t.id == tid && self.is_in_output_dir(&t.path)) {
                let out_dir = self.media_dir.join("output");
                if let Ok(rel) = title.path.strip_prefix(&out_dir) {
                    let originals_dir = self.media_dir.join("originals");
                    return originals_dir.join(rel).exists();
                }
            }
        }
        false
    }

    pub fn get_on_disk_file_size(&self, id: &MappableMediaId) -> Option<u64> {
        let titles = self.file_backed_titles.borrow();
        self.get_title_id_for_mappable(id).and_then(|tid| {
            titles.iter().find(|t| t.id == tid && self.is_in_output_dir(&t.path)).map(|t| t.file_size)
        })
    }

    pub fn get_title_id_for_mappable(&self, id: &MappableMediaId) -> Option<FileBackedTitleId> {
        let titles = self.file_backed_titles.borrow();
        titles.iter().find(|title| {
            match &title.mapped_media {
                Some(MediaId::TvEpisode(eid)) => {
                    if let MappableMediaId::TvEpisode(id) = id {
                        return eid == id;
                    }
                }
                Some(MediaId::FilmVideo(fid)) => {
                    if let MappableMediaId::FilmVideo(id) = id {
                        return fid == id;
                    }
                }
                Some(MediaId::SemanticNameKey(key)) => {
                    match id {
                        MappableMediaId::TvEpisode(episode_id) => {
                            if let Some(episode) = self.tv_show_episodes.borrow().iter().find(|e| e.id == *episode_id) {
                                if let Some(show_key) = self.tv_show_key(&episode.show_id) {
                                    let expected_key = format!("{} - {}", show_key, episode.series_key);
                                    if *key == expected_key { return true; }
                                    let expected_key_with_name = format!("{} - {} - {}", show_key, episode.series_key, episode.name);
                                    return *key == expected_key_with_name;
                                }
                            }
                        }
                        MappableMediaId::FilmVideo(film_video_id) => {
                            if let Some(video) = self.film_videos.borrow().iter().find(|v| v.id == *film_video_id) {
                                if video.ty == "FeaturePresentation" {
                                    let expected_key = format!("{} - {}", video.film_key, video.film_key);
                                    if *key == expected_key { return true; }
                                }
                                let expected_key_with_name = format!("{} - {}", video.film_key, video.name);
                                return *key == expected_key_with_name;
                            }
                        }
                    }
                }
                _ => {}
            }
            false
        }).map(|title| title.id.clone())
    }

    pub fn get_mappable_text(&self, id: &MappableMediaId) -> Option<String> {
        match id {
            MappableMediaId::TvEpisode(eid) => {
                self.tv_show_episodes.borrow().iter().find(|e| e.id == *eid)
                    .map(|e| format!("{} - {}", e.series_key, e.name))
            }
            MappableMediaId::FilmVideo(fvid) => {
                self.film_videos.borrow().iter().find(|v| v.id == *fvid)
                    .map(|v| {
                        if v.ty == "FeaturePresentation" {
                            v.name.clone()
                        } else {
                            format!("{} - {}", v.ty, v.name)
                        }
                    })
            }
        }
    }

    pub fn is_mapped_to_file(&self, id: &MappableMediaId) -> bool {
        self.get_title_id_for_mappable(id).is_some()
    }

    pub fn has_confirmed_tmdb_api_key(&self) -> bool {
        self.tmdb_api_key.is_some()
    }

    pub fn get_tmdb_api_key(&self) -> &str {
        self.tmdb_api_key.as_ref().unwrap()
    }

    pub fn get_file_backed_title_path(&self, id: &FileBackedTitleId) -> Option<PathBuf> {
        self.file_backed_titles
            .borrow()
            .iter()
            .find(|title| title.id == *id)
            .map(|title| title.path.clone())
    }

    pub fn tmdb(&self) -> &TmdbCache {
        &self.tmdb_cache
    }

    pub fn get_film_by_tmdb_id(&self, tmdb_id: usize) -> Option<Film> {
        self.films.borrow().iter().find(|film| film.tmdb_id == tmdb_id).cloned()
    }

    pub fn get_show_by_tmdb_id(&self, tmdb_id: usize) -> Option<TvShow> {
        self.tv_shows.borrow().iter().find(|film| film.tmdb_id == tmdb_id).cloned()
    }

    pub fn push_film_owned(&self, film: Film) {
        self.push_film_videos(vec![film.feature_presentation_video()]);
        self.films.borrow_mut().push(film);
    }

    pub fn push_film_videos(&self, films: Vec<FilmVideo>) {
        self.film_videos.borrow_mut().extend(films);
    }

    pub fn push_tv_show(&self, show: TvShow) {
        self.tv_shows.borrow_mut().push(show);
    }

    pub fn push_tv_show_episodes(&self, episodes: Vec<TvShowEpisode>) {
        self.tv_show_episodes.borrow_mut().extend(episodes);
    }

    pub fn push_film(&self, film: &Film) {
        self.push_film_videos(vec![film.feature_presentation_video()]);
        self.films.borrow_mut().push(film.to_owned());
    }

    pub fn sort_collections(&self) {
        let mut titles = self.file_backed_titles.borrow_mut();
        titles.sort_by_key(|title| title.collection.to_lowercase());

        let mut films = self.films.borrow_mut();
        films.sort_by_key(|film| film.film_key().to_lowercase());

        let mut film_videos = self.film_videos.borrow_mut();
        film_videos.sort_by_key(|video| format!("{}{}", video.film_key, video.name));

        let mut tv_shows = self.tv_shows.borrow_mut();
        tv_shows.sort_by_key(|show| show.show_key.to_lowercase());

        let mut tv_show_episodes = self.tv_show_episodes.borrow_mut();
        tv_show_episodes.sort_by_key(|episode| format!("{}{}", episode.show_name, episode.series_key));
    }

    pub fn delete_tv_show(&self, show_id: &TvShowId) {
        let tmdb_id = &show_id.0;
        let show_file = self.config_dir.join("tv").join(format!("{tmdb_id}.json"));
        if show_file.exists() {
            let _ = std::fs::remove_file(show_file);
        }

        // Delete all seasons
        if let Ok(entries) = std::fs::read_dir(self.config_dir.join("tv")) {
            for entry in entries.filter_map(|e| e.ok()) {
                let filename = entry.file_name().to_string_lossy().into_owned();
                if filename.starts_with(&format!("{}-S", tmdb_id)) && filename.ends_with(".json") {
                    let _ = std::fs::remove_file(entry.path());
                }
            }
        }

        // Remove from memory
        self.tv_shows.borrow_mut().retain(|s| s.id != *show_id);
        self.tv_show_episodes.borrow_mut().retain(|e| e.show_id != *show_id);
    }

    pub fn delete_tv_season(&self, show_id: &TvShowId, season_number: usize) {
        let tmdb_id = &show_id.0;
        let season_file = self.config_dir.join("tv").join(format!("{}-S{}.json", tmdb_id, season_number));
        if season_file.exists() {
            let _ = std::fs::remove_file(season_file);
        }

        // Remove from memory
        self.tv_show_episodes.borrow_mut().retain(|e| !(e.show_id == *show_id && e.season_number == season_number));
    }

    pub fn delete_film(&self, film_id: &FilmId) {
        let tmdb_id = &film_id.0;
        let film_file = self.config_dir.join("films").join(format!("{}.json", tmdb_id));
        if film_file.exists() {
            let _ = std::fs::remove_file(film_file);
        }

        // Also remove videos
        let videos_file = self.config_dir.join("films").join(format!("{}-videos.json", tmdb_id));
        if videos_file.exists() {
            let _ = std::fs::remove_file(videos_file);
        }

        // Remove from memory
        self.films.borrow_mut().retain(|f| f.id != *film_id);
        self.film_videos.borrow_mut().retain(|v| v.film_id != *film_id);
    }

    pub fn delete_film_video(&self, video_id: &FilmVideoId) {
        // Film videos aren't usually stored in individual files, they come from the -videos.json
        // So we just remove from memory for now. 
        // TODO: If we want it to persist, we'd need to rewrite the -videos.json file.
        self.film_videos.borrow_mut().retain(|v| v.id != *video_id);
    }
}

impl FileBackedTitle {
    pub fn id(&self) -> &str {
        self.id.0.as_str()
    }

    pub fn collection(&self) -> &str {
        self.collection.as_str()
    }

    pub fn name(&self) -> &str {
        self.file_name.as_str()
    }

    pub fn size(&self) -> u64 {
        self.file_size
    }

    pub fn marked_for_deletion(&self, confirmed_plays: &HashSet<PathBuf>, media_dir: &Path) -> bool {
        if self.file_name.contains(".d") || confirmed_plays.contains(&self.path) {
            return true;
        }

        let originals_dir = media_dir.join("originals");
        let output_dir = media_dir.join("output");
        if self.path.starts_with(&originals_dir) {
            if let Ok(rel) = self.path.strip_prefix(&originals_dir) {
                let counterpart = output_dir.join(rel);
                if confirmed_plays.contains(&counterpart) {
                    return true;
                }
            }
        }
        false
    }

    pub fn is_mapped(&self) -> bool {
        self.mapped_media.is_some()
    }

    pub fn path(&self) -> &Path {
        &self.path
    }
}

impl TvShowEpisode {
    pub fn id(&self) -> &str {
        self.id.0.as_str()
    }

    pub fn show_id(&self) -> &TvShowId {
        &self.show_id
    }

    pub fn name(&self) -> &str {
        self.name.as_str()
    }

    pub fn series_key(&self) -> &str {
        self.series_key.as_str()
    }
}

impl Film {
    pub fn id(&self) -> &str {
        self.id.0.as_str()
    }

    pub fn name(&self) -> &str {
        self.name.as_str()
    }

    pub fn film_key(&self) -> &str {
        self.film_key.as_str()
    }

    /// Creates the default presentation video for this film.
    pub fn feature_presentation_video(&self) -> FilmVideo {
        FilmVideo {
            id: FilmVideoId(format!("fp.{}", self.id())),
            film_id: FilmId(self.id().to_owned()),
            tmdb_id: self.tmdb_id.to_string(),
            name: "Feature Presentation".to_string(),
            ty: "FeaturePresentation".to_string(),
            film_name: self.name.clone(),
            film_key: self.film_key().to_owned(),
        }
    }
}

impl FilmVideo {
    pub fn id(&self) -> &str {
        self.id.0.as_str()
    }

    pub fn name(&self) -> &str {
        self.name.as_str()
    }

    pub fn film_key(&self) -> &str {
        self.film_key.as_str()
    }

    pub fn get_ideal_storage_path(&self) -> Vec<String> {
        if self.ty == "FeaturePresentation" {
            vec![
                self.film_key.to_owned(),
                format!("{}.mkv", self.film_key)
            ]
        } else {
            vec![
                self.film_key.to_owned(),
                "extras".to_string(),
                format!("{}.mkv", self.name)
            ]
        }
    }
}

impl MappableMediaId {
    pub fn id(&self) -> &str {
        match self {
            MappableMediaId::TvEpisode(id) => id.0.as_str(),
            MappableMediaId::FilmVideo(id) => id.0.as_str(),
        }
    }
}