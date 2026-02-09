OPTIONS := "-fPIC -std=c++20 -fuse-ld=mold"
DEPS := "Qt6Core Qt6Widgets phonon4qt6 nlohmann_json"
CFLAGS := shell(f'pkg-config --cflags {{DEPS}}')
LIBS := shell(f'pkg-config --libs {{DEPS}}')

QTTOOLS := `pkg-config --variable=libexecdir Qt6Core`
MOC := f'{{QTTOOLS}}/moc'
UIC := f'{{QTTOOLS}}/uic'

build:
    {{MOC}} mainwindow.h > moc_mainwindow.cpp
    {{UIC}} mainwindow.ui > ui_mainwindow.h
    g++ *.cpp {{LIBS}} {{CFLAGS}} {{OPTIONS}} -o run
