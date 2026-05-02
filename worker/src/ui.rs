use serde::Serialize;
use crate::media::{MappableMediaId, FileBackedTitleId, MediaState, TvEpisodeId, FilmVideoId};
use crate::requests::IncomingRequest;
use crate::ui::TreeItemChange::ChangeColor;

#[derive(Serialize)]
pub enum Tree {
    Files,
    TvShows,
    Films,
}

#[derive(Serialize)]
pub struct TreeItem {
    id: String,
    parent_text: String,
    text: String,
    color: String,
}

#[derive(Serialize)]
pub enum TreeItemChange {
    ChangeColor(String),
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
    WorkerReady,
    RecalledConfirmedTmdbApiKey,
    CommandStarted(IncomingRequest),
    CommandCompleted(IncomingRequest),
}

enum Mode {
    Tv,
    Film,
}

/// The files tree is a 2-layer tree view that shows disks > titles, e.g. Rush Hour 2 > JB1_t00.
/// Mapped media shouldn't appear in the media tree but rather a colored entry on the media tree.
pub fn build_files_tree(state: &MediaState) -> Vec<UiEvent> {
    state.file_backed_titles().iter().filter(|n| !n.is_mapped()).map(|file|
        UiEvent::AddTreeItem {
            tree: Tree::Files,
            item: TreeItem {
                id: file.id().to_owned(),
                parent_text: file.collection().to_owned(),
                text: file.name().to_owned(),
                color: if file.marked_for_deletion() { "red".to_owned() } else { "Default".to_owned() },
            },
            after: None,
        }
    ).collect()
}

pub fn build_tv_shows_tree(state: &MediaState) -> Vec<UiEvent> {
    state.tv_show_episodes().iter().map(|episode| {
        let id = MappableMediaId::TvEpisode(episode.id.clone());
        let color = if state.is_on_disk(&id) {
            "green".to_owned()
        } else if state.is_mapped_to_file(&id) {
            "orange".to_owned()
        } else {
            "Default".to_owned()
        };

        UiEvent::AddTreeItem {
            tree: Tree::TvShows,
            item: TreeItem {
                id: episode.id().to_owned(),
                parent_text: state.tv_show_key(episode.show_id()).unwrap_or(String::from("ERROR")),
                text: format!("{} - {}", episode.series_key(), episode.name()),
                color,
            },
            after: None,
        }
    }).collect()
}

pub fn build_films_tree(state: &MediaState) -> Vec<UiEvent> {
    state.film_videos().iter().map(|film| {
        let id = MappableMediaId::FilmVideo(film.id.clone());
        let color = if state.is_on_disk(&id) {
            "green".to_owned()
        } else if state.is_mapped_to_file(&id) {
            "orange".to_owned()
        } else {
            "Default".to_owned()
        };

        UiEvent::AddTreeItem {
            tree: Tree::Films,
            item: TreeItem {
                id: film.id().to_owned(),
                parent_text: film.film_key().to_owned(),
                text: film.name().to_owned(),
                color,
            },
            after: None,
        }
    }).collect()
}

pub fn get_garbage_size(state: &MediaState) -> UiEvent {
    UiEvent::ChangeGarbageSize {
        size: state.file_backed_titles().iter().fold(0u64, |t, a| t + a.size())
    }
}

pub fn get_tree_change_action_for_mappable(state: &MediaState, id: MappableMediaId) -> UiEvent {
    let tree = match &id {
        MappableMediaId::TvEpisode(_) => Tree::TvShows,
        MappableMediaId::FilmVideo(_) => Tree::Films,
    };

    let color = if state.is_on_disk(&id) {
        "green".to_owned()
    } else if state.is_mapped_to_file(&id) {
        "orange".to_owned()
    } else {
        "Default".to_owned()
    };

    UiEvent::ChangeTreeItem {
        tree,
        id: id.id().to_owned(),
        change: ChangeColor(color)
    }
}

pub fn get_tree_change_action_for_mapping_file(id: FileBackedTitleId, is_mapped: bool) -> UiEvent {
    UiEvent::ChangeTreeItem {
        tree: Tree::Files,
        id: id.0.to_owned(),
        change: ChangeColor(if is_mapped {
            "orange".to_owned()
        } else {
            "Default".to_owned()
        })
    }
}

pub fn get_add_tree_item_for_film(state: &MediaState, id: String, film_name: String, description: String,) -> UiEvent {
    let mappable_id = MappableMediaId::FilmVideo(FilmVideoId(id.clone()));
    let color = if state.is_on_disk(&mappable_id) {
        "green".to_owned()
    } else if state.is_mapped_to_file(&mappable_id) {
        "orange".to_owned()
    } else {
        "Default".to_owned()
    };

    UiEvent::AddTreeItem {
        tree: Tree::Films,
        item: TreeItem {
            id,
            parent_text: film_name,
            text: description,
            color,
        },
        after: None
    }
}

pub fn get_add_tree_item_for_tv_show(state: &MediaState, id: String, show_name: String, description: String,) -> UiEvent {
    let mappable_id = MappableMediaId::TvEpisode(TvEpisodeId(id.clone()));
    let color = if state.is_on_disk(&mappable_id) {
        "green".to_owned()
    } else if state.is_mapped_to_file(&mappable_id) {
        "orange".to_owned()
    } else {
        "Default".to_owned()
    };

    UiEvent::AddTreeItem {
        tree: Tree::TvShows,
        item: TreeItem {
            id,
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