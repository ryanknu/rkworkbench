# Ingest Workbench (rkwb)

A tool to manage the media ingest process, specifically for organizing ripped TV shows and films, identifying them via TMDB, and processing them for final storage.

## Architecture

Ingest Workbench uses a hybrid architecture to combine the strengths of different ecosystems:

- **Frontend (C++/Qt 6)**: Handles the user interface, media playback (via `QMediaPlayer`), and high-level application logic. It uses a `CommandWorker` thread to execute shell commands like `ffmpeg` and `rsync`.
- **Backend (Rust)**: Responsible for data modeling, filesystem operations, and background processing. It is built as a static library (`worker/`) and linked into the main C++ application.
- **Messaging System**: C++ and Rust communicate via an asynchronous messaging system. Rust sends JSON-serialized events (like updating a tree view's contents) back to the C++ frontend through a dedicated FFI callback.

## Features

- **Media Management**: Organize ripped titles into TV shows or Films.
- **Metadata Integration & Caching**: Fetch and locally cache show, film, and episode data from The Movie Database (TMDB) for offline access and improved performance.
- **Bulk Renaming & Standardizing**: Automatically rename and reorganize identified media files into a standardized directory structure (e.g., `Title (Year) [tmdb=ID]/SXXEXX.mkv`) in the output directory.
- **Integrated Player**: Preview titles directly within the app to confirm content.
- **Task Queue**: Background execution of long-running tasks like re-encoding or uploading.
- **Filesystem Mapping**: Link local ripped files to identified media metadata.
- **Garbage Collection**: Identify and delete source files once they have been processed.

## Building & Running

### Dependencies

To build the project, you need:
- **Qt 6** (Core, Widgets, Multimedia)
- **Rust** (Cargo)
- **GCC** (with C++23 support)
- **just** (command runner)
- **pkg-config**
- **nlohmann_json** (C++ JSON library)

Runtime dependencies for various features:
- `curl`: For API requests.
- `rsync` & `ssh`: For uploading media to remote servers.
- `ffmpeg`: For media re-encoding.

### Build Instructions

Ensure all dependencies are installed, then run:

```bash
just build
```

This will:
1. Generate Qt MOC and UIC files.
2. Build the Rust backend in release mode.
3. Compile the C++ frontend and link it with the Rust backend.
4. Produce the `rkwb` executable.

### Installation

To install the executable to `/usr/local/bin`:

```bash
just install
```

### Usage

Run the `rkwb` executable in a directory containing your media titles (usually directories containing `.mkv` files).

```bash
./rkwb [path/to/media]
```

The application saves the last used directory and will reload it on subsequent launches.

## Development

- **C++ Source**: Root directory (`*.cpp`, `*.h`, `*.ui`).
- **Rust Source**: `worker/` directory.
- **Build Logic**: `justfile`.

### Messaging Protocol
The communication between Rust and C++ uses a JSON-based protocol defined in `worker/src/ui.rs`. Messages are processed in C++ in `MainWindow::processMessage`.
