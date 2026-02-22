use std::sync::{Mutex, OnceLock};
use serde::{Deserialize, Serialize};
use crate::MEDIA;
use crate::media::{ConstMediaId, FileBackedTitle, FileBackedTitleId, MediaId, TvEpisodeId};
use crate::ui::{build_files_tree, build_media_tree, get_garbage_size, get_tmdb_key_event, get_tree_change_action_for_mapping_episode, get_tree_change_action_for_mapping_file, UiEvent};

type MediaState = OnceLock<Mutex<crate::media::MediaState>>;

#[derive(Clone, Serialize, Deserialize)]
pub enum IncomingRequest {
    PerformInitialLoad,
    MapMedia(FileBackedTitleId, ConstMediaId),
}

pub fn read_local_media(media: &MediaState) -> Vec<UiEvent> {
    let media = MEDIA.get().unwrap().lock().unwrap();

    media.read_local_media();
    media.read_local_media_metadata();

    let mut events = build_files_tree(&media);
    events.extend(build_media_tree(&media));
    events.push(get_garbage_size(&media));

    if media.has_confirmed_tmdb_api_key() {
        events.push(get_tmdb_key_event());
    }

    events
}

pub fn map_media(media: &MediaState, from: FileBackedTitleId, to: ConstMediaId) -> Vec<UiEvent> {
    let mut media = MEDIA.get().unwrap().lock().unwrap();

    let to_episode = match &to {
        ConstMediaId::TvEpisode(episode_id) => MediaId::TvEpisode(episode_id.to_owned()),
        _ => return Vec::new(),
    };

    let is_mapped = media.map_media(from.clone(), to_episode);

    vec![
        get_tree_change_action_for_mapping_file(from, is_mapped),
        get_tree_change_action_for_mapping_episode(to, is_mapped),
    ]
}