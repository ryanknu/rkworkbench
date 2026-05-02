use std::cell::{Ref, RefCell};
use std::cmp::PartialEq;
use std::env::home_dir;
use std::fmt::{format, Debug};
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
    pub(crate) file_backed_titles: RefCell<Vec<FileBackedTitle>>,
    pub(crate) films: RefCell<Vec<Film>>,
    pub(crate) film_videos: RefCell<Vec<FilmVideo>>,
    pub(crate) tv_shows: RefCell<Vec<TvShow>>,
    pub(crate) tv_show_episodes: RefCell<Vec<TvShowEpisode>>,
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

        // Read api key
        let tmdb_key_file = config_dir.join("tmdb.key");
        let tmdb_api_key = if tmdb_key_file.exists() {
            std::fs::read_to_string(&tmdb_key_file).map(|n| Some(n)).unwrap_or(None)
        } else {
            None
        };

        Self {
            tmdb_cache: TmdbCache {
                dir: config_dir.clone(),
                tmdb_base_url: "https://api.themoviedb.org/3".to_string(),
            },
            tmdb_api_key,
            media_dir,
            config_dir,
            file_backed_titles: Default::default(),
            films: Default::default(),
            film_videos: Default::default(),
            tv_shows: Default::default(),
            tv_show_episodes: Default::default(),
        }
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
    pub first_air_date: String,
    pub show_key: String,
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
    pub show_name: String,
    pub show_id: TvShowId,
    pub series_key: String,
}

#[derive(Debug, Clone)]
pub struct Film {
    pub id: FilmId,
    pub tmdb_id: usize,
    pub name: String,
    pub release_date: String,
    pub film_key: String,
}

#[derive(Clone)]

pub struct FilmVideo {
    pub(crate) id: FilmVideoId,
    pub(crate) film_id: FilmId,
    // RK: tmdb_id for FilmVideo is likely an Option<String>, feature presentations don't have one.
    pub(crate) tmdb_id: String,
    pub(crate) name: String,
    pub(crate) ty: String,
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
    fn is_in_output_dir(&self, e: &walkdir::DirEntry) -> bool {
        let out_dir = self.media_dir.join("output");
        e.path().starts_with(out_dir)
    }

    fn is_mkv(&self, e: walkdir::DirEntry) -> Option<walkdir::DirEntry> {
        if e.path().extension().map_or(false, |ext| ext == "mkv") {
            Some(e)
        } else {
            None
        }
    }

    pub fn read_local_media(&self) {
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

            // If it's in the output directory, we know that it's named to follow semantic conventions.
            let mapped_media = if self.is_in_output_dir(&entry) {
                let file_name = file_name.to_owned().replace(".mkv", "");
                Some(MediaId::SemanticNameKey(format!("{folder_name} - {file_name}")))
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

    pub fn marked_for_deletion(&self) -> bool {
        self.file_name.contains(".d")
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
    fn feature_presentation_video(&self) -> FilmVideo {
        FilmVideo {
            id: FilmVideoId(format!("fp.{}", self.id())),
            film_id: FilmId(self.id().to_owned()),
            tmdb_id: self.tmdb_id.to_string(),
            name: "Feature Presentation".to_string(),
            ty: "FeaturePresentation".to_string(),
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
        // This is for MKV's of FP only
        vec![
            self.film_key.to_owned(),
            format!("{}.mkv", self.film_key)
        ]
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