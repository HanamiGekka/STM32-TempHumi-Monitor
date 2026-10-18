QT       += core gui serialport printsupport
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG += c++11
TARGET = UIDesktop
INCLUDEPATH += ./config/
INCLUDEPATH += ./plot/
INCLUDEPATH += ./calc/

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
calc/calc.cpp \
config/settingsdialog.cpp \
main.cpp \
mainwindow.cpp \
plot/qcustomplot.cpp

SOURCES += app_th3.cpp app_file_input.cpp app_session_recorder.cpp app_line_buffer.cpp app_signal_analysis.cpp app_serial_input.cpp
HEADERS += app_th3.h app_file_input.h app_session_recorder.h app_line_buffer.h app_signal_analysis.h app_serial_input.h
SOURCES += app_desktop_settings.cpp
HEADERS += app_desktop_settings.h app_temperature_evidence.h
SOURCES += app_plot_window.cpp
HEADERS += app_plot_window.h
SOURCES += app_session_export.cpp
HEADERS += app_session_export.h
SOURCES += app_serial_recovery.cpp app_window_recovery.cpp app_window_inspection.cpp app_window_restore.cpp
HEADERS += app_serial_recovery.h
SOURCES += app_inspector.cpp
HEADERS += app_inspector.h
SOURCES += app_window_help.cpp
RESOURCES += resources/temphumi.qrc
win32: RC_FILE = resources/temphumi.rc
APP_VERSION_TEXT = $$cat($$PWD/VERSION, lines)
isEmpty(APP_VERSION_TEXT): APP_VERSION_TEXT = $$cat($$PWD/../VERSION, lines)
isEmpty(APP_VERSION_TEXT): error(Missing desktop VERSION or root firmware VERSION)
DEFINES += APP_SOFTWARE_VERSION=\\\"$${APP_VERSION_TEXT}\\\"
CURRENT_GIT_HEAD = $$system(git -C $$shell_quote($$PWD) rev-parse HEAD)
isEmpty(CURRENT_GIT_HEAD): CURRENT_GIT_HEAD = UNKNOWN
DEFINES += APP_GIT_HEAD=\\\"$${CURRENT_GIT_HEAD}\\\"

HEADERS += \
calc/calc.h \
config/settingsdialog.h \
mainwindow.h \
plot/qcustomplot.h

FORMS += \
config/settingsdialog.ui \
mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
serial-and-plot.txt
