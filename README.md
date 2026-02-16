# Ingest Workbench

A simple tool to manage an ingest process.

## Building & Running

To build, ensure you've installed dependencies in [justfile](justfile) using your distribution's package manger,
as well as `just`, then run `just build`.

When running, runtime dependencies are `curl`, `rsync`, `ffmpeg`, and `ssh`. If you do not have these installed,
various features won't work, for example without ffmpeg, you cannot re-encode titles. Without rsync, you cannot
upload your videos to a remote server.

To run, simply run the resulting executable in the directory with your titles. It should be a directory that
contains directories that then contain .mkv files.

*Note*: This directory is saved, and reloaded upon subsequent launches. If you do get stuck in the wrong directory
and want to change it, simply run `rkwb .` in the correct directory, or, pass in a path.

Task list:
- [x] TV - Fetch TV show metadata and associate disk titles to numbered episodes
- [x] TV - Upload entire series to server with `rsync`.
- [ ] Titles - Check FS for changes to titles in background.
- [ ] Config - Create and store config for remote server
- [ ] Docker - Manage ripper container
- [ ] TV - Delete season and series metadata.
- [ ] TV - 2-part episodes
- [ ] TV - Episode splitter
- [ ] TV - Title combiner
- [ ] Film - Fetch film metadata
- [ ] Film - Upload film to server
- [ ] Film - Manage extras directory
- [ ] Media - Pre-processor command
- [x] Player - Replace phonon with QMediaPlayer, add start second, and controls.
