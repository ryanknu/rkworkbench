use serde::Serialize;
use crate::media::{ConstMediaId, FileBackedTitleId, MediaState};
use crate::requests::IncomingRequest;
use crate::ui::TreeItemChange::ChangeColor;

#[derive(Serialize)]
pub enum Tree {
    Files,
    Media,
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

pub fn build_media_tree(state: &MediaState) -> Vec<UiEvent> {
    // TODO: Identify mapped media here.
    state.tv_show_episodes().iter().map(|episode|
        UiEvent::AddTreeItem {
            tree: Tree::Media,
            item: TreeItem {
                id: episode.id().to_owned(),
                parent_text: state.tv_show_key(episode.show_id()).unwrap_or(String::from("ERROR")),
                text: format!("{} - {}", episode.series_key(), episode.name()),
                color: "Default".to_owned(),
            },
            after: None,
        }
    ).collect()
}

pub fn get_garbage_size(state: &MediaState) -> UiEvent {
    UiEvent::ChangeGarbageSize {
        size: state.file_backed_titles().iter().fold(0u64, |t, a| t + a.size())
    }
}

pub fn get_tree_change_action_for_mapping_episode(id: ConstMediaId, is_mapped: bool) -> UiEvent {
    UiEvent::ChangeTreeItem {
        tree: Tree::Media,
        id: id.id().to_owned(),
        change: ChangeColor(if is_mapped {
            "orange".to_owned()
        } else {
            "Default".to_owned()
        })
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

pub fn get_tmdb_key_event() -> UiEvent {
    UiEvent::RecalledConfirmedTmdbApiKey
}