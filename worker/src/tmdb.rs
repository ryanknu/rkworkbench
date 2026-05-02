use std::path::PathBuf;
use std::{fs, thread};
use std::str::FromStr;
use std::time::{Duration, Instant};
use serde::Deserialize;
use walkdir::WalkDir;

#[derive(Debug)]
pub enum TmdbItem {
    Film(TmdbFilm),
    FilmVideos(TmdbFilmVideos),
    TvShow(TmdbTvShow),
    TvShowSeason(TmdbTvShowSeason),
}

pub struct TmdbCache {
    pub(crate) dir: PathBuf,
    pub(crate) tmdb_base_url: String,
}

#[derive(Debug, Deserialize)]
pub struct TmdbFilm {
    pub(crate) id: usize,
    pub(crate) title: String,
    pub(crate) release_date: String,
    overview: String,
    poster_path: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct TmdbFilmVideos {
    pub(crate) id: usize, // This is the FilmId
    pub(crate) results: Vec<TmdbFilmVideo>,
}

#[derive(Debug, Deserialize)]
pub struct TmdbFilmVideo {
    pub(crate) name: String,
    pub(crate) r#type: String,
    pub(crate) id: String,
}

#[derive(Deserialize, Debug)]
struct TmdbTvShowSeasonsVec {
    season_number: usize,
}

#[derive(Deserialize, Debug)]
pub struct TmdbTvShow {
    pub(crate) id: usize,
    pub(crate) first_air_date: String,
    pub(crate) name: String,
    seasons: Vec<TmdbTvShowSeasonsVec>,
}

#[derive(Deserialize, Debug)]
pub struct TmdbTvShowEpisode {
    pub(crate) id: usize,
    pub(crate) name: String,
    pub(crate) episode_number: usize,
    pub(crate) season_number: usize,
    pub(crate) show_id: usize,
}

#[derive(Deserialize, Debug)]
pub struct TmdbTvShowSeason {
    pub(crate) episodes: Vec<TmdbTvShowEpisode>,
}

impl TmdbCache {
    /// Iterate over the cache directory and yield each file entry.
    pub fn iterate_cache(&self) -> impl Iterator<Item = TmdbItem> {
        WalkDir::new(&self.dir).into_iter().filter_map(|e| e.ok())
            .filter_map(|e| {
                let path = e.path();
                if path.is_file() {
                    Some(path.to_owned())
                } else {
                    None
                }
            })
            .filter_map(|p| fs::read(&p).ok())
            .filter_map(|data| {
                if let Ok(film) = serde_json::from_slice::<TmdbFilm>(&data) {
                    Some(TmdbItem::Film(film))
                } else if let Ok(videos) = serde_json::from_slice::<TmdbFilmVideos>(&data) {
                    // It would be cool if I could yield each video separately.
                    Some(TmdbItem::FilmVideos(videos))
                } else if let Ok(show) = serde_json::from_slice::<TmdbTvShow>(&data) {
                    Some(TmdbItem::TvShow(show))
                } else if let Ok(season) = serde_json::from_slice::<TmdbTvShowSeason>(&data) {
                    // It would be cool if I could yield each episode separately.
                    Some(TmdbItem::TvShowSeason(season))
                } else {
                    None
                }
            })
    }

    pub fn query_film(&self, api_key: &str, tmdb_id: &str) -> Result<(TmdbFilm, TmdbFilmVideos), Box<dyn std::error::Error>> {
        let film_location = self.get_movie_file_location(&tmdb_id);
        let videos_location = self.get_movie_videos_file_location(&tmdb_id);

        let (film_json, videos_json) = if film_location.exists() && videos_location.exists() {
            // If both movie and movie videos files exist, serve from disk.
            println!("Loading film from disk: {:?}", film_location);
            let film_json = fs::read(&film_location).unwrap();
            let videos_json = fs::read(&videos_location).unwrap();
            (film_json, videos_json)
        } else {
            let mut base_url = format!("{}/movie/{tmdb_id}", self.tmdb_base_url);
            let body = ureq::get(&base_url)
                .header("Authorization", format!("Bearer {api_key}"))
                .call()?
                .body_mut()
                .read_to_vec()?;

            base_url.push_str("/videos");
            let body2 = ureq::get(&base_url)
                .header("Authorization", format!("Bearer {api_key}"))
                .call()?
                .body_mut()
                .read_to_vec()?;

            (body, body2)
        };

        let film: TmdbFilm = serde_json::from_slice(&film_json)?;
        let videos: TmdbFilmVideos = serde_json::from_slice(&videos_json)?;

        Ok((film, videos))
    }

    pub fn query_tv(&self, api_key: &str, tmdb_id: &str) -> Result<(Vec<u8>, Vec<Vec<u8>>), Box<dyn std::error::Error>> {
        let mut base_url = format!("{}/tv/{tmdb_id}", self.tmdb_base_url);
        let body = ureq::get(&base_url)
            .header("Authorization", format!("Bearer {api_key}"))
            .call()?
            .body_mut()
            .read_to_vec()?;

        // Read seasons
        #[derive(Debug, Deserialize)]
        struct Series {
            seasons: Vec<SeriesSeason>,
        }

        #[derive(Debug, Deserialize)]
        struct SeriesSeason {
            season_number: usize,
        }

        let series: Series = serde_json::from_slice(&body).unwrap();
        let season_numbers: Vec<usize> = series.seasons.iter().map(|s| s.season_number).collect();

        base_url.push_str("/season");

        // Complication to implement rate limiting here is important, if we do something like load every
        // season of Saturday Night Live, that would not be friendly to TMDB.
        let mut season_bodies = Vec::new();
        let rate_limit_interval = Duration::from_millis(200);
        let mut last_request_time = Instant::now();

        for (index, season_number) in season_numbers.iter().enumerate() {
            if index > 0 {
                let elapsed = last_request_time.elapsed();
                if elapsed < rate_limit_interval {
                    thread::sleep(rate_limit_interval - elapsed);
                }
            }

            last_request_time = Instant::now();
            let season_url = format!("{}/{}", base_url, season_number);
            let season_body = ureq::get(&season_url)
                .header("Authorization", format!("Bearer {api_key}"))
                .call()?
                .body_mut()
                .read_to_vec()?;
            season_bodies.push(season_body);
        }

        Ok((body, season_bodies))
    }

    fn get_movie_file_location(&self, tmdb_id: &str) -> PathBuf {
        self.dir.join("movies").join(format!("{tmdb_id}.json"))
    }

    fn get_movie_videos_file_location(&self, tmdb_id: &str) -> PathBuf {
        self.dir.join("movies").join(format!("{tmdb_id}-videos.json"))
    }
}
