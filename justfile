OPTIONS := "-fPIC -std=c++23 -fuse-ld=mold"
DBG_OPTS := f'{{OPTIONS}} -g -fsanitize=address -pthread'
DEPS := "Qt6Core Qt6Widgets Qt6Multimedia Qt6MultimediaWidgets nlohmann_json"
CFLAGS := shell(f'pkg-config --cflags {{DEPS}}')
LIBS := shell(f'pkg-config --libs {{DEPS}}')
EXE := "rkwb"
LOCAL_PATH := f'/usr/local/bin/{{EXE}}'
RUST_LIB := "-Lworker/target/release -lrkwb"

QTTOOLS := `pkg-config --variable=libexecdir Qt6Core`
MOC := f'{{QTTOOLS}}/moc'
UIC := f'{{QTTOOLS}}/uic'

build:
    {{MOC}} mainwindow.h > moc_mainwindow.cpp
    {{MOC}} commandworker.h > moc_commandworker.cpp
    {{UIC}} mainwindow.ui > ui_mainwindow.h
    cargo build --release --manifest-path=worker/Cargo.toml
    g++ *.cpp {{LIBS}} {{CFLAGS}} {{RUST_LIB}} {{OPTIONS}} -o {{EXE}}

install:
    @if [ ! -f {{EXE}} ]; then \
        just build; \
    fi; \
    cp {{EXE}} {{LOCAL_PATH}}
