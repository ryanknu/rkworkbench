use serde::Serialize;
use crate::media::{MappableMediaId, FileBackedTitleId, MediaState, TvEpisodeId, FilmVideoId};
use crate::requests::IncomingRequest;
use crate::ui::TreeItemChange::{ChangeColor, ChangeText};

#[derive(Serialize, Clone, Copy)]
pub enum Tree {
    Files,
    TvShows,
    Films,
}

#[derive(Serialize)]
pub(crate) struct TreeItem {
    pub(crate) id: String,
    pub(crate) parent_id: Option<String>,
    pub(crate) parent_text: String,
    pub(crate) text: String,
    pub(crate) color: String,
}

#[derive(Serialize)]
pub enum TreeItemChange {
    ChangeColor(String),
    ChangeText(String),
}

#[derive(Serialize)]
pub struct MatchResult {
    pub diff: u32,
    pub position_ms: u64,
}

#[derive(Serialize)]
pub enum UiEvent {
    AddTreeItem {
        tree: Tree,
        item: TreeItem,
        after: Option<String>,
    },
    RemoveTreeItemById {
        tree: Tree,
        id: String,
    },
    ChangeTreeItem {
        tree: Tree,
        id: String,
        change: TreeItemChange,
    },
    ChangeGarbageSize {
        size: u64,
    },
    ClearTrees,
    WorkerReady,
    RecalledConfirmedTmdbApiKey,
    CommandStarted(IncomingRequest),
    CommandCompleted(IncomingRequest),
    FfmpegOutput(String),
    RsyncOutput(String),
    CopyOutput(String),
    SelectTreeItem {
        tree: Tree,
        id: String,
    },
    SeekPlayer {
        position_ms: u64,
    },
    MatchResults {
        tree: Tree,
        results: std::collections::HashMap<String, MatchResult>,
    },
    SetTmdbStill {
        path: String,
    },
    SetMetadata {
        metadata: MediaMetadata,
    },
    SetMkvTracks {
        tracks: Vec<MkvTrack>,
    },
    SetStitchList {
        files: Vec<String>,
    },
}

#[derive(Serialize)]
pub struct MediaMetadata {
    pub title: String,
    pub overview: String,
    pub language: String,
    pub release_date: String,
    pub runtime: String,
}

#[derive(Serialize)]
pub struct MkvTrack {
    pub id: u64,
    pub type_: String,
    pub codec: String,
    pub language: String,
    pub name: Option<String>,
    pub is_default: bool,
    pub is_forced: bool,
    pub is_hearing_impaired: bool,
    pub is_commentary: bool,
    pub profile: Option<String>,
    pub bitrate: Option<String>,
}

enum Mode {
    Tv,
    Film,
}

/// The files tree is a 2-layer tree view that shows disks > titles, e.g. Rush Hour 2 > JB1_t00.
/// Mapped media shouldn't appear in the media tree but rather a colored entry on the media tree.
pub fn build_files_tree(state: &MediaState) -> Vec<UiEvent> {
    let confirmed = state.confirmed_plays.borrow();
    state.file_backed_titles().iter().filter(|n| !n.is_mapped() && !state.is_in_originals_dir(n.path())).map(|file| {
        let color = if file.marked_for_deletion(&confirmed, &state.media_dir) {
            if file.file_name.contains(".d") {
                "red".to_owned()
            } else {
                "cyan".to_owned()
            }
        } else {
            "Default".to_owned()
        };

        UiEvent::AddTreeItem {
            tree: Tree::Files,
            item: TreeItem {
                id: file.id().to_owned(),
                parent_id: None,
                parent_text: file.collection().to_owned(),
                text: file.name().to_owned(),
                color,
            },
            after: None,
        }
    }).collect()
}

fn format_gib_size(bytes: u64) -> String {
    let gib = bytes as f64 / (1024.0 * 1024.0 * 1024.0);
    format!("{:.1}G", gib)
}

pub fn build_tv_shows_tree(state: &MediaState) -> Vec<UiEvent> {
    state.tv_show_episodes().iter().map(|episode| {
        let id = MappableMediaId::TvEpisode(episode.id.clone());
        let mut text = format!("{} - {}", episode.series_key(), episode.name());
        let on_disk_size = state.get_on_disk_file_size(&id);
        if let Some(size) = on_disk_size {
            text = format!("{} {}", format_gib_size(size), text);
        }

        let color = if state.is_confirmed_play(&id) {
            "cyan".to_owned()
        } else if on_disk_size.is_some() {
            "green".to_owned()
        } else {
            "Default".to_owned()
        };

        UiEvent::AddTreeItem {
            tree: Tree::TvShows,
            item: TreeItem {
                id: episode.id().to_owned(),
                parent_id: Some(episode.show_id.0.clone()),
                parent_text: episode.show_name.clone(),
                text,
                color,
            },
            after: None,
        }
    }).collect()
}

pub fn build_films_tree(state: &MediaState) -> Vec<UiEvent> {
    state.film_videos().iter().map(|film| {
        let id = MappableMediaId::FilmVideo(film.id.clone());
        let mut text = film.name().to_owned();
        let on_disk_size = state.get_on_disk_file_size(&id);
        if let Some(size) = on_disk_size {
            text = format!("{} {}", format_gib_size(size), text);
        }

        let color = if state.is_confirmed_play(&id) {
            "cyan".to_owned()
        } else if on_disk_size.is_some() {
            "green".to_owned()
        } else {
            "Default".to_owned()
        };

        UiEvent::AddTreeItem {
            tree: Tree::Films,
            item: TreeItem {
                id: film.id().to_owned(),
                parent_id: Some(film.film_id.0.clone()),
                parent_text: film.film_name.clone(),
                text,
                color,
            },
            after: None,
        }
    }).collect()
}

pub fn get_garbage_size(state: &MediaState) -> UiEvent {
    let confirmed = state.confirmed_plays.borrow();
    UiEvent::ChangeGarbageSize {
        size: state.file_backed_titles().iter()
            .filter(|t| t.marked_for_deletion(&confirmed, &state.media_dir))
            .fold(0u64, |t, a| t + a.size())
    }
}

pub fn get_tree_change_action_for_mappable(state: &MediaState, id: MappableMediaId) -> Vec<UiEvent> {
    let tree = match &id {
        MappableMediaId::TvEpisode(_) => Tree::TvShows,
        MappableMediaId::FilmVideo(_) => Tree::Films,
    };

    let mut events = Vec::new();

    let on_disk_size = state.get_on_disk_file_size(&id);
    let color = if state.is_confirmed_play(&id) {
        if let Some(size) = on_disk_size {
            if let Some(base_text) = state.get_mappable_text(&id) {
                events.push(UiEvent::ChangeTreeItem {
                    tree,
                    id: id.id().to_owned(),
                    change: ChangeText(format!("{} {}", format_gib_size(size), base_text))
                });
            }
        }
        "cyan".to_owned()
    } else if let Some(size) = on_disk_size {
        if let Some(base_text) = state.get_mappable_text(&id) {
            events.push(UiEvent::ChangeTreeItem {
                tree,
                id: id.id().to_owned(),
                change: ChangeText(format!("{} {}", format_gib_size(size), base_text))
            });
        }
        "green".to_owned()
    } else {
        if let Some(base_text) = state.get_mappable_text(&id) {
            events.push(UiEvent::ChangeTreeItem {
                tree,
                id: id.id().to_owned(),
                change: ChangeText(base_text)
            });
        }
        "Default".to_owned()
    };

    events.push(UiEvent::ChangeTreeItem {
        tree,
        id: id.id().to_owned(),
        change: ChangeColor(color)
    });

    events
}

pub fn get_tree_change_action_for_mapping_file(id: FileBackedTitleId, _is_mapped: bool) -> UiEvent {
    UiEvent::ChangeTreeItem {
        tree: Tree::Files,
        id: id.0.to_owned(),
        change: ChangeColor("Default".to_owned())
    }
}

pub fn get_add_tree_item_for_film(state: &MediaState, id: String, parent_id: String, film_name: String, mut description: String,) -> UiEvent {
    let mappable_id = MappableMediaId::FilmVideo(FilmVideoId(id.clone()));
    let on_disk_size = state.get_on_disk_file_size(&mappable_id);
    if let Some(size) = on_disk_size {
        description = format!("{} {}", format_gib_size(size), description);
    }

    let color = if state.is_confirmed_play(&mappable_id) {
        "cyan".to_owned()
    } else if on_disk_size.is_some() {
        "green".to_owned()
    } else {
        "Default".to_owned()
    };

    UiEvent::AddTreeItem {
        tree: Tree::Films,
        item: TreeItem {
            id,
            parent_id: Some(parent_id),
            parent_text: film_name,
            text: description,
            color,
        },
        after: None
    }
}

pub fn get_add_tree_item_for_tv_show(state: &MediaState, id: String, parent_id: String, show_name: String, mut description: String,) -> UiEvent {
    let mappable_id = MappableMediaId::TvEpisode(TvEpisodeId(id.clone()));
    let on_disk_size = state.get_on_disk_file_size(&mappable_id);
    if let Some(size) = on_disk_size {
        description = format!("{} {}", format_gib_size(size), description);
    }

    let color = if state.is_confirmed_play(&mappable_id) {
        "cyan".to_owned()
    } else if on_disk_size.is_some() {
        "green".to_owned()
    } else {
        "Default".to_owned()
    };

    UiEvent::AddTreeItem {
        tree: Tree::TvShows,
        item: TreeItem {
            id,
            parent_id: Some(parent_id),
            parent_text: show_name,
            text: description,
            color,
        },
        after: None
    }
}

pub fn get_tmdb_key_event() -> UiEvent {
    UiEvent::RecalledConfirmedTmdbApiKey
}