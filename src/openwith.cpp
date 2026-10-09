#include "openwith.h"

#include "viewer.h" // makeTransient

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#elif defined(Q_OS_MACOS)
#include <CoreServices/CoreServices.h>
#include <climits>
#endif

namespace {

// The default application first, the others by name.
void sortApps(QList<OpenWithApp> *apps, const QString &defaultId)
{
    std::stable_sort(apps->begin(), apps->end(), [&defaultId](const OpenWithApp &a, const OpenWithApp &b) {
        if ((a.id == defaultId) != (b.id == defaultId))
            return a.id == defaultId;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// The escapes of a desktop entry's string values (Desktop Entry Specification, "Possible value types").
QString unescapeValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 == value.size()) {
            out += c;
            continue;
        }
        switch (value.at(++i).unicode()) {
        case 's': out += QLatin1Char(' '); break;
        case 'n': out += QLatin1Char('\n'); break;
        case 't': out += QLatin1Char('\t'); break;
        case 'r': out += QLatin1Char('\r'); break;
        default: out += value.at(i); break; // "\\", "\;" and anything else: the character itself
        }
    }
    return out;
}

struct DesktopEntry {
    QString path;
    QString name;
    QString exec;
    QString icon;
    QStringList mimeTypes;
    bool usable = false; // an application that can be started; false also hides a lower-priority entry of the same ID
};

DesktopEntry readDesktopEntry(const QString &path)
{
    DesktopEntry entry;
    entry.path = path;
    QFile file(path);
    if (file.size() > (1 << 20) || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;
    const QString locale = QLocale().name(); // "pt_PT"
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    QString name, localName, tryExec, type;
    bool hidden = false, inMain = false, exactLocale = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inMain = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (!inMain || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Type"))
            type = value;
        else if (key == QLatin1String("Name"))
            name = unescapeValue(value);
        else if (key == QStringLiteral("Name[%1]").arg(locale)) {
            localName = unescapeValue(value);
            exactLocale = true;
        } else if (key == QStringLiteral("Name[%1]").arg(language) && !exactLocale)
            localName = unescapeValue(value);
        else if (key == QLatin1String("Exec"))
            entry.exec = unescapeValue(value); // the string escapes first, then the quoting (desktopEntryCommand)
        else if (key == QLatin1String("TryExec"))
            tryExec = unescapeValue(value);
        else if (key == QLatin1String("Icon"))
            entry.icon = unescapeValue(value);
        else if (key == QLatin1String("MimeType"))
            entry.mimeTypes = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("Hidden"))
            hidden = value == QLatin1String("true"); // "deleted"; NoDisplay entries still open files
    }
    entry.name = localName.isEmpty() ? name : localName;
    const bool installed = tryExec.isEmpty()
                           || (QFileInfo(tryExec).isAbsolute() ? QFileInfo(tryExec).isExecutable()
                                                               : !QStandardPaths::findExecutable(tryExec).isEmpty());
    entry.usable = type == QLatin1String("Application") && !hidden && installed && !entry.name.isEmpty()
                   && !entry.exec.isEmpty();
    return entry;
}

// Every desktop entry by its ID; a directory earlier in the XDG search path wins.
QHash<QString, DesktopEntry> desktopEntries()
{
    QHash<QString, DesktopEntry> entries;
    for (const QString &directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        const QDir dir(directory);
        QDirIterator it(directory, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = dir.relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-'));
            if (!entries.contains(id))
                entries.insert(id, readDesktopEntry(path));
        }
    }
    return entries;
}

// The associations of mimeapps.list files (Association between MIME types and applications
// specification), highest priority first.
struct Associations {
    QStringList defaults, added, removed;
};

Associations associations(const QStringList &mimeTypes)
{
    QStringList files;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))
        files << dir + QStringLiteral("/mimeapps.list");
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation))
        files << dir + QStringLiteral("/mimeapps.list");
    Associations result;
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (file.size() > (1 << 20) || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QStringList *section = nullptr;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                section = line == QLatin1String("[Default Applications]")   ? &result.defaults
                          : line == QLatin1String("[Added Associations]")   ? &result.added
                          : line == QLatin1String("[Removed Associations]") ? &result.removed
                                                                            : nullptr;
                continue;
            }
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (section && eq > 0 && mimeTypes.contains(line.left(eq).trimmed()))
                *section += line.mid(eq + 1).trimmed().split(QLatin1Char(';'), Qt::SkipEmptyParts);
        }
    }
    return result;
}
#endif

#if defined(Q_OS_WIN)
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};

// Calls `visit` with each handler Windows recommends for the file's extension until it returns true.
template <typename Visit>
void forEachHandler(const QString &file, Visit visit)
{
    const QString extension = QLatin1Char('.') + QFileInfo(file).suffix();
    IEnumAssocHandlers *handlers = nullptr;
    if (FAILED(SHAssocEnumHandlers(reinterpret_cast<LPCWSTR>(extension.utf16()), ASSOC_FILTER_RECOMMENDED, &handlers)))
        return;
    IAssocHandler *handler = nullptr;
    ULONG fetched = 0;
    while (handlers->Next(1, &handler, &fetched) == S_OK && fetched == 1) {
        const bool done = visit(handler);
        handler->Release();
        if (done)
            break;
    }
    handlers->Release();
}

QString takeString(LPWSTR text)
{
    const QString result = text ? QString::fromWCharArray(text) : QString();
    CoTaskMemFree(text);
    return result;
}
#endif

} // namespace

QStringList desktopEntryCommand(const QString &exec, const QString &file, const QString &name, const QString &icon,
                                const QString &desktopFile)
{
    // `exec` has its string escapes undone already ("\\\\" in the file is one backslash here).
    // Arguments are separated by spaces; double quotes group one, and inside them a backslash
    // escapes '"', '`', '$' and '\'; outside them it escapes any character, as in GLib. Then the
    // field codes are expanded (Desktop Entry Specification, "The Exec key").
    QStringList arguments;
    QString current;
    bool quoted = false, started = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (quoted) {
            if (c == QLatin1Char('\\') && i + 1 < exec.size() && QStringLiteral("\"`$\\").contains(exec.at(i + 1)))
                current += exec.at(++i);
            else if (c == QLatin1Char('"'))
                quoted = false;
            else
                current += c;
        } else if (c == QLatin1Char('"')) {
            quoted = started = true;
        } else if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            if (started)
                arguments << current;
            current.clear();
            started = false;
        } else if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
            current += exec.at(++i);
            started = true;
        } else {
            current += c;
            started = true;
        }
    }
    if (quoted)
        return {};
    if (started)
        arguments << current;

    const QString url = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
    QStringList command;
    bool fileGiven = false;
    for (const QString &argument : std::as_const(arguments)) {
        if (argument == QLatin1String("%i")) {
            if (!icon.isEmpty())
                command << QStringLiteral("--icon") << icon;
            continue;
        }
        QString expanded;
        bool dropped = false; // an argument made only of a code without a value disappears
        for (qsizetype i = 0; i < argument.size(); ++i) {
            if (argument.at(i) != QLatin1Char('%') || i + 1 == argument.size()) {
                expanded += argument.at(i);
                continue;
            }
            switch (argument.at(++i).unicode()) {
            case '%': expanded += QLatin1Char('%'); break;
            case 'f':
            case 'F':
                expanded += file;
                fileGiven = true;
                break;
            case 'u':
            case 'U':
                expanded += url;
                fileGiven = true;
                break;
            case 'c': expanded += name; break;
            case 'k': expanded += desktopFile; break;
            default: dropped = argument.size() == 2; break; // deprecated or unknown codes
            }
        }
        if (!dropped)
            command << expanded;
    }
    if (command.isEmpty())
        return {};
    if (!fileGiven)
        command << file;
    return command;
}

QList<OpenWithApp> openWithApps(const QString &file)
{
    QList<OpenWithApp> apps;
#if defined(Q_OS_WIN)
    const ComScope com;
    forEachHandler(file, [&apps](IAssocHandler *handler) {
        LPWSTR name = nullptr, program = nullptr;
        handler->GetUIName(&name);
        handler->GetName(&program);
        const OpenWithApp app{takeString(name), takeString(program)};
        if (!app.name.isEmpty() && !app.id.isEmpty())
            apps.append(app);
        return false;
    });
    wchar_t program[MAX_PATH * 2];
    DWORD size = DWORD(sizeof program / sizeof *program);
    const QString extension = QLatin1Char('.') + QFileInfo(file).suffix();
    QString defaultId;
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, reinterpret_cast<LPCWSTR>(extension.utf16()),
                                    L"open", program, &size)))
        defaultId = QString::fromWCharArray(program);
    for (OpenWithApp &app : apps)
        if (app.id.compare(defaultId, Qt::CaseInsensitive) == 0)
            defaultId = app.id;
    sortApps(&apps, defaultId);
#elif defined(Q_OS_MACOS)
    const QByteArray path = QFile::encodeName(QFileInfo(file).absoluteFilePath());
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(path.constData()),
                                                           CFIndex(path.size()), false);
    if (!url)
        return apps;
    // The LaunchServices calls are deprecated in favour of NSWorkspace (Objective-C) but still work.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFURLRef standard = LSCopyDefaultApplicationURLForURL(url, kLSRolesAll, nullptr);
    CFArrayRef all = LSCopyApplicationURLsForURL(url, kLSRolesAll);
#pragma clang diagnostic pop
    QString defaultId;
    const auto add = [&apps](CFURLRef app) {
        char buffer[PATH_MAX];
        if (!CFURLGetFileSystemRepresentation(app, true, reinterpret_cast<UInt8 *>(buffer), sizeof buffer))
            return QString();
        const QString bundle = QFile::decodeName(buffer);
        if (std::none_of(apps.cbegin(), apps.cend(), [&bundle](const OpenWithApp &a) { return a.id == bundle; }))
            apps.append({QFileInfo(bundle).completeBaseName(), bundle});
        return bundle;
    };
    if (standard) {
        defaultId = add(standard);
        CFRelease(standard);
    }
    if (all) {
        for (CFIndex i = 0; i < CFArrayGetCount(all); ++i)
            add(static_cast<CFURLRef>(CFArrayGetValueAtIndex(all, i)));
        CFRelease(all);
    }
    CFRelease(url);
    sortApps(&apps, defaultId);
#else
    const QMimeType type = QMimeDatabase().mimeTypeForFile(file);
    if (!type.isValid())
        return apps;
    const QStringList types = QStringList{type.name()} + type.aliases();
    const QHash<QString, DesktopEntry> entries = desktopEntries();
    const Associations assoc = associations(types);
    QStringList ids;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        if (std::any_of(types.cbegin(), types.cend(), [&it](const QString &t) { return it->mimeTypes.contains(t); }))
            ids << it.key();
    ids += assoc.added;
    QString defaultId;
    for (const QString &id : assoc.defaults) {
        if (entries.value(id).usable) {
            defaultId = id;
            ids << id;
            break;
        }
    }
    for (const QString &id : std::as_const(ids)) {
        const DesktopEntry entry = entries.value(id);
        const bool listed = std::any_of(apps.cbegin(), apps.cend(), [&id](const OpenWithApp &a) { return a.id == id; });
        if (entry.usable && !listed && (id == defaultId || !assoc.removed.contains(id)))
            apps.append({entry.name, id});
    }
    sortApps(&apps, defaultId);
#endif
    return apps;
}

bool openWith(const OpenWithApp &app, const QString &file)
{
#if defined(Q_OS_WIN)
    const ComScope com;
    IShellItem *item = nullptr;
    const QString native = QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath());
    if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, IID_PPV_ARGS(&item))))
        return false;
    IDataObject *data = nullptr;
    const HRESULT bound = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    item->Release();
    if (FAILED(bound))
        return false;
    bool started = false;
    forEachHandler(file, [&](IAssocHandler *handler) {
        LPWSTR program = nullptr;
        handler->GetName(&program);
        if (takeString(program) != app.id)
            return false;
        started = SUCCEEDED(handler->Invoke(data));
        return true;
    });
    data->Release();
    return started;
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), app.id, file});
#else
    const QStringList command = openWithCommand(app, file);
    return !command.isEmpty() && QProcess::startDetached(command.first(), command.mid(1));
#endif
}

QStringList openWithCommand(const OpenWithApp &app, const QString &file)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Q_UNUSED(app);
    Q_UNUSED(file);
    return {};
#else
    const DesktopEntry entry = desktopEntries().value(app.id);
    return entry.usable ? desktopEntryCommand(entry.exec, file, entry.name, entry.icon, entry.path) : QStringList();
#endif
}

bool openWithOther(QWindow *parent, const QString &file)
{
#if defined(Q_OS_WIN)
    const QString native = QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath());
    OPENASINFO info = {};
    info.pcszFile = reinterpret_cast<LPCWSTR>(native.utf16());
    info.oaifInFlags = OAIF_EXEC;
    return SUCCEEDED(SHOpenWithDialog(parent ? reinterpret_cast<HWND>(parent->winId()) : nullptr, &info));
#else
    QFileDialog dialog;
    dialog.setWindowTitle(QCoreApplication::translate("OpenWith", "Choose an Application"));
    dialog.setFileMode(QFileDialog::ExistingFile);
#if defined(Q_OS_MACOS)
    dialog.setDirectory(QStringLiteral("/Applications"));
    dialog.setNameFilter(QCoreApplication::translate("OpenWith", "Applications (*.app)"));
#else
    dialog.setDirectory(QStringLiteral("/usr/bin"));
#endif
    if (parent)
        makeTransient(dialog, parent);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    const QString program = dialog.selectedFiles().constFirst();
#if defined(Q_OS_MACOS)
    return openWith({QFileInfo(program).completeBaseName(), program}, file);
#else
    return QFileInfo(program).isExecutable() && QProcess::startDetached(program, {file});
#endif
#endif
}
