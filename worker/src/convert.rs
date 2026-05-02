use std::path::PathBuf;
use crate::media::{Film, FilmId, FilmVideo, FilmVideoId, TvEpisodeId, TvShow, TvShowEpisode, TvShowId};
use crate::tmdb::{TmdbFilm, TmdbFilmVideo, TmdbTvShow, TmdbTvShowEpisode};

impl From<TmdbFilm> for Film {
    fn from(value: TmdbFilm) -> Self {
        Film {
            id: FilmId(value.id.to_string()),
            tmdb_id: value.id,
            name: value.title.clone(),
            release_date: value.release_date.clone(),
            film_key: format!("{} ({}) [tmdb={}]", value.title, &value.release_date[0..4], value.id),
        }
    }
}

impl From<TmdbTvShow> for TvShow {
    fn from(value: TmdbTvShow) -> Self {
        TvShow {
            show_key: format!("{} ({}) [tmdb={}]", value.name, &value.first_air_date[0..4], value.id),
            id: TvShowId(value.id.to_string()),
            tmdb_id: value.id,
            name: value.name,
            first_air_date: value.first_air_date,
        }
    }
}

/// Implements a transient struct for building a FilmVideo when you do not have all the data
/// required to build a FilmVideo.
pub struct FilmVideoBuilder {
    id: FilmVideoId,
    tmdb_id: String,
    name: String,
    ty: String,
}

impl From<TmdbFilmVideo> for FilmVideoBuilder {
    fn from(value: TmdbFilmVideo) -> Self {
        FilmVideoBuilder {
            id: FilmVideoId(value.id.to_string()),
            tmdb_id: value.id,
            name: value.name,
            ty: value.r#type,
        }
    }
}

impl FilmVideoBuilder {
    pub fn build_with_film(self, film: &Film) -> FilmVideo {
        FilmVideo {
            id: self.id,
            tmdb_id: self.tmdb_id,
            film_key: film.film_key().to_owned(),
            film_id: film.id.clone(),
            name: self.name,
            ty: self.ty,
        }
    }
}

/// Implements a transient struct for building a TvShowEpisode when you do not have all the data
/// required to build a TvShowEpisode.
pub struct TvShowEpisodeBuilder {
    id: TvEpisodeId,
    tmdb_id: usize,
    season_number: usize,
    number: usize,
    name: String,
    series_key: String,
}

impl From<TmdbTvShowEpisode> for TvShowEpisodeBuilder {
    fn from(value: TmdbTvShowEpisode) -> Self {
        TvShowEpisodeBuilder {
            id: TvEpisodeId(value.id.to_string()),
            tmdb_id: value.id,
            season_number: value.season_number,
            number: value.episode_number,
            name: value.name,
            series_key: format!("S{:0>2}E{:0>2}", value.season_number, value.episode_number),
        }
    }
}

impl TvShowEpisodeBuilder {
    pub fn build_with_show(self, show: &TvShow) -> TvShowEpisode {
        TvShowEpisode {
            id: self.id,
            tmdb_id: self.tmdb_id,
            season_number: self.season_number,
            number: self.number,
            name: self.name,
            show_name: show.name.clone(),
            show_id: show.id.clone(),
            series_key: self.series_key,
        }
    }
}