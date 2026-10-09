// "Open With" (decision D-53): the applications the operating system offers for a file, and
// starting one of them with it. The file is always passed as one argument, never through a shell.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QWindow;

struct OpenWithApp {
    QString name; // as the system shows it
    QString id;   // Linux: the .desktop file; macOS: the application bundle; Windows: the handler's program
};

// The system's default application first, then the others it registers for the file's type.
QList<OpenWithApp> openWithApps(const QString &file);
// Starts `app` with `file`; false when it could not be started.
bool openWith(const OpenWithApp &app, const QString &file);
// The system's own "choose an application" dialog where there is one (Windows); elsewhere an
// application or program picked in a file dialog. False when cancelled or not started.
bool openWithOther(QWindow *parent, const QString &file);

// Linux: the program and arguments openWith() starts; elsewhere empty (the system starts the app).
QStringList openWithCommand(const OpenWithApp &app, const QString &file);
// Linux: the program and arguments a desktop entry's Exec value, its string escapes undone,
// gives for `file` (quoting and field codes as the Desktop Entry Specification says), or empty
// when the value is malformed.
QStringList desktopEntryCommand(const QString &exec, const QString &file, const QString &name, const QString &icon,
                                const QString &desktopFile);
