use std::cell::{Ref, RefCell};
use std::cmp::PartialEq;
use std::env::home_dir;
use std::fmt::Debug;
use std::fs::DirEntry;
use std::path::PathBuf;
use std::str::FromStr;
use serde::{Deserialize, Serialize};
use ulid::Ulid;
use walkdir::WalkDir;

pub struct MediaState {
    tmdb_api_key: Option<String>,
    media_dir: PathBuf,
    config_dir: PathBuf,
    file_backed_titles: RefCell<Vec<FileBackedTitle>>,
    tv_shows: RefCell<Vec<TvShow>>,
    tv_show_episodes: RefCell<Vec<TvShowEpisode>>,
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
            tmdb_api_key,
            media_dir,
            config_dir,
            file_backed_titles: Default::default(),
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
}

#[derive(Clone, Deserialize, Serialize)]
pub enum ConstMediaId {
    TvShow(TvShowId),
    TvEpisode(TvEpisodeId),
}

#[derive(Clone, Deserialize, Serialize)]
pub struct TvShowId(String);
#[derive(Clone, Deserialize, Serialize)]
pub struct TvEpisodeId(pub String);
#[derive(Clone, Deserialize, PartialEq, Serialize)]
pub struct FileBackedTitleId(pub String);

pub struct TvShow {
    id: TvShowId,
    tmdb_id: usize,
    name: String,
    first_air_date: String,
    path: PathBuf,
    show_key: String,
}

pub struct TvShowSeason {
    key: String,
    path: PathBuf,
}

pub struct TvShowEpisode {
    id: TvEpisodeId,
    tmdb_id: usize,
    season_number: usize,
    season_path: PathBuf,
    number: usize,
    name: String,
    show_name: String,
    show_id: TvShowId,
    series_key: String,
}

#[derive(Clone)]
pub struct FileBackedTitle {
    id: FileBackedTitleId,
    format: AvFormat,
    path: PathBuf,
    mapped_media: Option<MediaId>,
    collection: String,
    file_name: String,
    file_size: u64,
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

    /// Reads the local media metadata.
    /// Stored in your XDG_CONFIG_HOME/rkworkbench/<media type> directories.
    /// For TV, file locations are:
    ///   tv/{id}.json -- series data
    ///   tv/{id}-SXX.json -- episode data for season XX.
    pub fn read_local_media_metadata(&self) {
        println!("[rust] scanning local media metadata in {:?}", self.config_dir);

        #[derive(Deserialize)]
        struct SeriesFileSeasonsVec {
            season_number: usize,
        }

        #[derive(Deserialize)]
        struct SeriesFileContents {
            id: usize,
            first_air_date: String,
            name: String,
            seasons: Vec<SeriesFileSeasonsVec>,
        }

        #[derive(Deserialize)]
        struct SeasonFileEpisodeContents {
            id: usize,
            name: String,
            episode_number: usize,
            season_number: usize,
            show_id: usize,
        }

        #[derive(Deserialize)]
        struct SeasonFileContents {
            episodes: Vec<SeasonFileEpisodeContents>,
        }

        // I need to buffer episodes while reading them in case we encounter them before their
        // associated series.
        let mut episodes_vec: Vec<SeasonFileEpisodeContents> = Vec::new();

        for entry in WalkDir::new(&self.config_dir).into_iter().filter_map(|e| e.ok()) {
            if entry.path().is_dir() {
                continue;
            }

            // Only scan the "tv" folder
            if !entry.path().components().any(|c| c.as_os_str() == "tv") {
                continue;
            }

            let data = std::fs::read(&entry.path()).unwrap();
            let series = serde_json::from_slice::<SeriesFileContents>(&data);
            if let Ok(series) = series {
                let year = &series.first_air_date[0..4];
                let show_key = format!("{} ({}) [tmdb={}]", series.name, year, series.id);
                let show = TvShow {
                    id: TvShowId(series.id.to_string()),
                    tmdb_id: series.id,
                    name: series.name,
                    first_air_date: series.first_air_date,
                    path: entry.path().to_owned(),
                    show_key,
                };

                let mut shows = self.tv_shows.borrow_mut();
                shows.push(show);
                continue;
            }

            let season = serde_json::from_slice::<SeasonFileContents>(&data);
            if let Ok(season) = season {
                episodes_vec.extend(season.episodes);
                continue;
            }

            println!("Unreadable: {}", entry.path().display());
        }

        let tv_shows = self.tv_shows.borrow();
        let mut tv_show_episodes = self.tv_show_episodes.borrow_mut();
        tv_show_episodes.extend(episodes_vec.iter().filter_map(|episode| {
            let Some(show) = tv_shows.iter().find(|show| show.tmdb_id == episode.show_id) else {
                return None;
            };

            Some(TvShowEpisode {
                id: TvEpisodeId(episode.id.to_string()),
                tmdb_id: episode.id,
                season_number: episode.season_number,
                season_path: Default::default(), // Hm.
                number: episode.episode_number,
                name: episode.name.clone(),
                show_name: show.name.clone(),
                show_id: show.id.clone(),
                series_key: format!("S{:0>2}E{:0>2}", episode.season_number, episode.episode_number),
            })
        }));

        tv_show_episodes.sort_by_key(|title| format!("{}{}", title.show_name, title.series_key));
    }

    pub fn file_backed_titles(&self) -> Ref<'_, [FileBackedTitle]> {
        Ref::map(self.file_backed_titles.borrow(), |v| v.as_slice())
    }

    pub fn tv_show_episodes(&self) -> Ref<'_, [TvShowEpisode]> {
        Ref::map(self.tv_show_episodes.borrow(), |v| v.as_slice())
    }

    pub fn tv_show_key(&self, id: &TvShowId) -> Option<String> {
        self.tv_shows.borrow().iter().find(|show| show.id.0 == id.0).map(|show| show.show_key.to_owned())
    }

    pub fn map_media(&self, from: FileBackedTitleId, to: MediaId) -> bool {
        let mut titles = self.file_backed_titles.borrow_mut();

        for title in titles.iter_mut() {
            if title.id == from {
                let mut new_title = title.to_owned();
                new_title.mapped_media = Some(to);
                *title = new_title;
                return true;
            }
        }

        false
    }

    pub fn has_confirmed_tmdb_api_key(&self) -> bool {
        self.tmdb_api_key.is_some()
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

impl ConstMediaId {
    pub fn id(&self) -> &str {
        match self {
            ConstMediaId::TvEpisode(id) => id.0.as_str(),
            ConstMediaId::TvShow(id) => id.0.as_str(),
        }
    }
}