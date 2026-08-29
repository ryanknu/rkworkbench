# Project Playbook (Linux)

## Environment

### Variables
- `HOME`: User home directory, used for config storage in `~/.config/rkworkbench`.

---

## Modules

### rkworkbench (Main Application)

#### Summary
C++/Qt 6 frontend for managing media ingest. It coordinates the user interface and interacts with the Rust backend.

#### How to Build
`just build`

#### How to Run Tests
TBD (No automated tests discovered for C++ frontend)

#### How to Run Single Test
TBD

#### Run / Check

- Launch App:
  `./rkwb [path/to/media]`

#### Notes
- Requires a display environment (X11/Wayland) to run as it is a GUI application.
- Depends on `ffmpeg`, `rsync`, and `curl` at runtime for various features.
- Build process generates Qt MOC/UIC files and links against the Rust backend.

---

### worker (Rust Backend)

#### Summary
Rust static library responsible for data modeling, filesystem operations, and background processing.

#### How to Build
`cargo build --release --manifest-path=worker/Cargo.toml`

#### How to Run Tests
`cargo test --manifest-path=worker/Cargo.toml`

#### How to Run Single Test
`cargo test <test_name> --manifest-path=worker/Cargo.toml`

#### Run / Check

- Validation:
  `cargo check --manifest-path=worker/Cargo.toml`

#### Notes
- Built as a `staticlib` to be linked into the C++ application.
- Uses the JSON-based messaging protocol for communication with the frontend.
- Currently contains no automated tests (0 tests found in `worker/src`).

---

## Tools
- `just`: Primary command runner and build coordinator.
- `cmake`: Alternative build system (partially implemented).
- `cargo`: Rust package manager and build tool.
- `g++`: C++ compiler (requires C++23 support).
- `Qt 6`: Core, Widgets, Multimedia modules.

## Notes
- The `justfile` is the most reliable way to build the project as it correctly handles the hybrid C++/Rust build pipeline.
- If the application crashes on launch with a filesystem error, ensure the provided path exists and is accessible.
