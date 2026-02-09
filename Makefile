QTINCPATH = /usr/include/qt6
QTTOOLS = /usr/lib/qt6

CC = g++
MOC = ${QTTOOLS}/moc
UIC = ${QTTOOLS}/uic
INCLUDE = -I ${QTINCPATH} \
          -I ${QTINCPATH}/QtCore \
          -I ${QTINCPATH}/QtWidgets \
          -I ${QTINCPATH}/QtGui \
          -I /usr/include/phonon4qt6
QTLIB = -lQt6Widgets \
        -lQt6Core \
        -lQt6Gui \
        -lphonon4qt6
SRC = main.cpp mainwindow.cpp moc_mainwindow.cpp

all: moc ui
	${CC} ${SRC} ${INCLUDE} ${QTLIB} -fPIC -std=c++20 -o run

moc:
	${MOC} mainwindow.h > moc_mainwindow.cpp

ui:
	${UIC} mainwindow.ui > ui_mainwindow.h

clean:
	rm moc_* ui_* run

.PHONY: all moc ui clean
