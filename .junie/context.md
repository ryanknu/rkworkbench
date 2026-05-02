# RK Workbench - Project Context

## Overview
RK Workbench is an ingest management tool designed to streamline the process of organizing, identifying, and processing ripped media (TV shows and films). It features a hybrid architecture with a C++ Qt frontend and a Rust backend.

## Architecture

### Frontend (C++/Qt)
- **Framework**: Qt 6
- **Language**: C++23
- **Primary Components**:
    - `MainWindow`: Manages the UI state, widget signals, and integrates the Rust backend.
    - `AppModel`: Maintains the frontend application state, handles configuration, and provides data to UI components.
    - `CommandWorker`: A separate thread (using `QThread` and `QObject`) that executes long-running shell commands (e.g., `curl`, `rsync`, `ffmpeg`) without blocking the UI.
    - `callback_wrapper`: An FFI bridge that receives JSON messages from Rust and dispatches them to the Qt event loop.

### Backend (Rust)
- **Crate**: `rkwb` (located in the `worker/` directory)
- **Language**: Rust (2024 edition)
- **Type**: Static library (`staticlib`) linked into the C++ executable.
- **Responsibilities**: Data modeling, media identification, background processing, and OS-level operations.
- **FFI Interface**:
    - `start_rust_processing`: Entry point to start the background worker thread.
    - `initial_load`, `map_tv_episode`, `lookup_film`: Commands sent from C++ to Rust.
    - `get_filename_for_title_id`: Synchronous request for file paths.
- **Communication**: Uses a messaging system. Rust sends JSON-serialized `UiEvent`s to C++ via a callback function.

## Communication Protocol (JSON)
Rust communicates with C++ by sending JSON objects representing `UiEvent`s. Common events include:
- `AddTreeItem`: Add an entry to one of the tree views (`Files`, `TvShows`, `Films`).
- `ChangeTreeItem`: Update properties (e.g., color) of an existing tree item.
- `RemoveTreeItemById`: Remove an item from a tree.
- `ChangeGarbageSize`: Update the reported size of deletable files.
- `WorkerReady`: Signal that the Rust backend is initialized.

## Build System
- **justfile**: The primary build orchestrator.
- **Qt Tools**: Uses `moc` and `uic` for processing Qt header and UI files.
- **Cargo**: Builds the Rust backend.
- **GCC**: Compiles C++ files and links everything into the `rkwb` binary.

## Dependencies
- **System**: Qt 6, `curl`, `rsync`, `ffmpeg`, `ssh`.
- **C++**: `nlohmann_json`.
- **Rust**: `serde`, `serde_json`, `ureq`, `walkdir`, `ulid`.
