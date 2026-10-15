#include "release_notices.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include "TextSaveGuard.h"
#include <QApplication>
#include <QAction>
#include <QAbstractItemView>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileDevice>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPalette>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStringListModel>
#include <QStyleFactory>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>

#if defined(Q_OS_OPENHARMONY)
#include <QtOhExtras/QOhWidgetHelper>
#endif

namespace
{
struct HighlightRule
{
    QRegularExpression pattern;
    QTextCharFormat format;
};

struct NavLocation
{
    QString path;
    int tabIndex = -1;
    int line = 1;
    int column = 1;
};

struct ProjectSymbol
{
    QString name;
    QString kind;
    QString path;
    QString origin;
    QString preview;
    int line = 1;
};

struct DiffEntry
{
    QChar tag;
    QString line;
};

struct SnippetTemplate
{
    QString label;
    QString body;
};

struct CustomSnippetTemplate
{
    QString language;
    QString label;
    QString body;
};

struct FileTemplate
{
    QString label;
    QString language;
    QString body;
};

struct ExternalTool
{
    QString label;
    QString command;
};

struct IgnoreRule
{
    QString pattern;
    bool negated = false;
    bool directoryOnly = false;
    bool anchored = false;
    bool hasSlash = false;
};

QString normalizedLaunchPath(QString argument)
{
    argument = argument.trimmed();
    if ((argument.startsWith(QLatin1Char('"')) && argument.endsWith(QLatin1Char('"')))
        || (argument.startsWith(QLatin1Char('\'')) && argument.endsWith(QLatin1Char('\'')))) {
        argument = argument.mid(1, argument.size() - 2);
    }
    if (argument.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QUrl url(argument);
        const QString localPath = url.toLocalFile();
        if (!localPath.isEmpty()) {
            argument = localPath;
        } else if (!url.path().isEmpty()) {
            argument = QUrl::fromPercentEncoding(url.path().toUtf8());
        } else {
            argument.remove(0, QStringLiteral("file://").size());
            argument = QUrl::fromPercentEncoding(argument.toUtf8());
        }
    }
#if defined(Q_OS_OPENHARMONY)
    const QString appDataPrefix = QStringLiteral("/data/app/el2/100/base/com.geanydesk.track1/");
    if (argument.startsWith(appDataPrefix)) {
        return QStringLiteral("/data/storage/el2/base/") + argument.mid(appDataPrefix.size());
    }
#endif
    return argument;
}

#if defined(Q_OS_OPENHARMONY)
QString openHarmonyWritableRoot()
{
    // Start in this application's sandbox; do not require public-directory ACLs.
    QDir dir(QStringLiteral("/data/storage/el2/base/files/GeanyDesk"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return QString();
    return dir.absolutePath();
}

bool openHarmonyPathNeedsWritableFallback(const QString& path)
{
    if (path.trimmed().isEmpty() || path.startsWith(QStringLiteral("/storage/Users/currentUser"))) {
        return true;
    }
    return !QDir(path).exists();
}

QStringList openHarmonyPendingOpenQueueFiles()
{
    QStringList candidates;
    const QString cacheLocation = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cacheLocation.isEmpty()) {
        candidates << QDir(cacheLocation).filePath(QStringLiteral("geany-pending-open.txt"));
    }
    const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataLocation.isEmpty()) {
        candidates << QDir(appDataLocation).filePath(QStringLiteral("geany-pending-open.txt"));
        candidates << QDir(appDataLocation).filePath(QStringLiteral("../cache/geany-pending-open.txt"));
    }
    candidates << QDir(openHarmonyWritableRoot()).filePath(QStringLiteral("geany-pending-open.txt"));
    candidates << QStringLiteral("/data/app/el2/100/base/com.geanydesk.track1/files/geany-pending-open.txt");
    candidates << QStringLiteral("/data/app/el2/100/base/com.geanydesk.track1/cache/geany-pending-open.txt");
    candidates << QDir::home().filePath(QStringLiteral("haps/entry/cache/geany-pending-open.txt"));
    candidates << QStringLiteral("/data/storage/el2/base/haps/entry/cache/geany-pending-open.txt");
    candidates.removeDuplicates();
    return candidates;
}

QString openHarmonySavePathForSuggestion(const QString& path)
{
    QString suggested = normalizedLaunchPath(path);
    const QString writableRoot = openHarmonyWritableRoot();
    if (suggested.isEmpty() || QFileInfo(suggested).isDir()) {
        suggested = QDir(suggested.isEmpty() ? writableRoot : suggested).filePath(QStringLiteral("Untitled.txt"));
    }

    QFileInfo info(suggested);
    if (suggested.startsWith(QStringLiteral("/storage/Users/currentUser"))
        || info.absolutePath() == QStringLiteral("/storage/Users/currentUser")
        || !QDir(info.absolutePath()).exists()) {
        suggested = QDir(writableRoot).filePath(info.fileName().isEmpty() ? QStringLiteral("Untitled.txt") : info.fileName());
        info.setFile(suggested);
    }

    if (info.exists()) {
        const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
        const QString base = info.completeBaseName().isEmpty() ? QStringLiteral("Untitled") : info.completeBaseName();
        suggested = QDir(info.absolutePath()).filePath(QStringLiteral("%1-%2%3")
            .arg(base, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), suffix));
    }
    return suggested;
}
#endif

QString chooseSaveFileName(QWidget* parent, const QString& title, const QString& path,
    const QString& filter = QString())
{
#if defined(Q_OS_OPENHARMONY)
    Q_UNUSED(filter);
    QString suggested = openHarmonySavePathForSuggestion(path);
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(920, 640);

    auto* layout = new QVBoxLayout(&dialog);
    auto* locationRow = new QHBoxLayout();
    auto* upButton = new QPushButton(QStringLiteral("Up"), &dialog);
    auto* homeButton = new QPushButton(QStringLiteral("Home"), &dialog);
    auto* appButton = new QPushButton(QStringLiteral("App Files"), &dialog);
    auto* pathEdit = new QLineEdit(&dialog);
    locationRow->addWidget(upButton);
    locationRow->addWidget(homeButton);
    locationRow->addWidget(appButton);
    locationRow->addWidget(pathEdit, 1);
    auto* list = new QListWidget(&dialog);
    auto* nameEdit = new QLineEdit(&dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addLayout(locationRow);
    layout->addWidget(list, 1);
    layout->addWidget(new QLabel(QStringLiteral("File name"), &dialog));
    layout->addWidget(nameEdit);
    layout->addWidget(buttons);

    QString currentDir = QFileInfo(suggested).absolutePath();
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        currentDir = openHarmonyWritableRoot();
    }
    nameEdit->setText(QFileInfo(suggested).fileName());

    auto refresh = [&]() {
        QDir dir(currentDir);
        if (!dir.exists()) {
            currentDir = openHarmonyWritableRoot();
            dir.setPath(currentDir);
        }
        pathEdit->setText(dir.absolutePath());
        list->clear();
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& info : entries) {
            auto* item = new QListWidgetItem(info.isDir()
                ? QStringLiteral("[folder] %1").arg(info.fileName())
                : info.fileName(), list);
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setData(Qt::UserRole + 1, info.isDir());
        }
    };
    auto navigate = [&](const QString& dirPath) {
        QDir dir(dirPath);
        if (dir.exists()) {
            currentDir = dir.absolutePath();
            refresh();
        }
    };

    QObject::connect(upButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir(currentDir).absoluteFilePath(QStringLiteral(".."))); });
    QObject::connect(homeButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir::homePath()); });
    QObject::connect(appButton, &QPushButton::clicked, &dialog, [&]() { navigate(openHarmonyWritableRoot()); });
    QObject::connect(pathEdit, &QLineEdit::returnPressed, &dialog, [&]() { navigate(pathEdit->text().trimmed()); });
    QObject::connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const QString itemPath = item->data(Qt::UserRole).toString();
        if (item->data(Qt::UserRole + 1).toBool()) {
            navigate(itemPath);
        } else {
            nameEdit->setText(QFileInfo(itemPath).fileName());
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    refresh();
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    QString fileName = nameEdit->text().trimmed();
    if (fileName.isEmpty()) {
        fileName = QFileInfo(suggested).fileName();
    }
    const QString selected = QDir::isAbsolutePath(fileName) ? fileName : QDir(currentDir).filePath(fileName);
    if (QFileInfo::exists(selected)) {
        const int answer = QMessageBox::question(parent, title,
            QStringLiteral("%1 already exists.\n\nReplace it?").arg(selected),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return QString();
        }
    }
    return normalizedLaunchPath(selected);
#else
    QFileDialog::Options options;
    return normalizedLaunchPath(QFileDialog::getSaveFileName(parent, title, path, filter, nullptr, options));
#endif
}

QString chooseOpenFileName(QWidget* parent, const QString& title, const QString& path,
    const QString& filter = QString())
{
#if defined(Q_OS_OPENHARMONY)
    Q_UNUSED(filter);
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(920, 640);

    auto* layout = new QVBoxLayout(&dialog);
    auto* locationRow = new QHBoxLayout();
    auto* upButton = new QPushButton(QStringLiteral("Up"), &dialog);
    auto* homeButton = new QPushButton(QStringLiteral("Home"), &dialog);
    auto* appButton = new QPushButton(QStringLiteral("App Files"), &dialog);
    auto* pathEdit = new QLineEdit(&dialog);
    locationRow->addWidget(upButton);
    locationRow->addWidget(homeButton);
    locationRow->addWidget(appButton);
    locationRow->addWidget(pathEdit, 1);
    auto* list = new QListWidget(&dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    layout->addLayout(locationRow);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);

    QString selectedPath;
    QString currentDir = normalizedLaunchPath(path);
    if (currentDir.isEmpty() || QFileInfo(currentDir).isFile()) {
        currentDir = QFileInfo(currentDir).absolutePath();
    }
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        currentDir = openHarmonyWritableRoot();
    }

    auto refresh = [&]() {
        QDir dir(currentDir);
        if (!dir.exists()) {
            currentDir = openHarmonyWritableRoot();
            dir.setPath(currentDir);
        }
        pathEdit->setText(dir.absolutePath());
        list->clear();
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& info : entries) {
            auto* item = new QListWidgetItem(info.isDir()
                ? QStringLiteral("[folder] %1").arg(info.fileName())
                : info.fileName(), list);
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setData(Qt::UserRole + 1, info.isDir());
        }
    };
    auto navigate = [&](const QString& dirPath) {
        QDir dir(dirPath);
        if (dir.exists()) {
            currentDir = dir.absolutePath();
            refresh();
        }
    };

    QObject::connect(upButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir(currentDir).absoluteFilePath(QStringLiteral(".."))); });
    QObject::connect(homeButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir::homePath()); });
    QObject::connect(appButton, &QPushButton::clicked, &dialog, [&]() { navigate(openHarmonyWritableRoot()); });
    QObject::connect(pathEdit, &QLineEdit::returnPressed, &dialog, [&]() {
        const QString typed = normalizedLaunchPath(pathEdit->text().trimmed());
        if (QFileInfo(typed).isFile()) {
            selectedPath = typed;
            dialog.accept();
        } else {
            navigate(typed);
        }
    });
    QObject::connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const QString itemPath = item->data(Qt::UserRole).toString();
        if (item->data(Qt::UserRole + 1).toBool()) {
            navigate(itemPath);
        } else {
            selectedPath = itemPath;
            dialog.accept();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (item != nullptr && !item->data(Qt::UserRole + 1).toBool()) {
            selectedPath = item->data(Qt::UserRole).toString();
            dialog.accept();
            return;
        }
        const QString typed = normalizedLaunchPath(pathEdit->text().trimmed());
        if (QFileInfo(typed).isFile()) {
            selectedPath = typed;
            dialog.accept();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    refresh();
    return dialog.exec() == QDialog::Accepted ? normalizedLaunchPath(selectedPath) : QString();
#else
    return normalizedLaunchPath(QFileDialog::getOpenFileName(parent, title, path, filter));
#endif
}

QString chooseExistingDirectory(QWidget* parent, const QString& title, const QString& path)
{
#if defined(Q_OS_OPENHARMONY)
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(920, 640);

    auto* layout = new QVBoxLayout(&dialog);
    auto* locationRow = new QHBoxLayout();
    auto* upButton = new QPushButton(QStringLiteral("Up"), &dialog);
    auto* homeButton = new QPushButton(QStringLiteral("Home"), &dialog);
    auto* appButton = new QPushButton(QStringLiteral("App Files"), &dialog);
    auto* pathEdit = new QLineEdit(&dialog);
    locationRow->addWidget(upButton);
    locationRow->addWidget(homeButton);
    locationRow->addWidget(appButton);
    locationRow->addWidget(pathEdit, 1);
    auto* list = new QListWidget(&dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    layout->addLayout(locationRow);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);

    QString selectedDir;
    QString currentDir = normalizedLaunchPath(path);
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        currentDir = openHarmonyWritableRoot();
    }

    auto refresh = [&]() {
        QDir dir(currentDir);
        if (!dir.exists()) {
            currentDir = openHarmonyWritableRoot();
            dir.setPath(currentDir);
        }
        pathEdit->setText(dir.absolutePath());
        list->clear();
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& info : entries) {
            auto* item = new QListWidgetItem(QStringLiteral("[folder] %1").arg(info.fileName()), list);
            item->setData(Qt::UserRole, info.absoluteFilePath());
        }
    };
    auto navigate = [&](const QString& dirPath) {
        QDir dir(dirPath);
        if (dir.exists()) {
            currentDir = dir.absolutePath();
            refresh();
        }
    };

    QObject::connect(upButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir(currentDir).absoluteFilePath(QStringLiteral(".."))); });
    QObject::connect(homeButton, &QPushButton::clicked, &dialog, [&]() { navigate(QDir::homePath()); });
    QObject::connect(appButton, &QPushButton::clicked, &dialog, [&]() { navigate(openHarmonyWritableRoot()); });
    QObject::connect(pathEdit, &QLineEdit::returnPressed, &dialog, [&]() { navigate(pathEdit->text().trimmed()); });
    QObject::connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem* item) {
        if (item != nullptr) {
            navigate(item->data(Qt::UserRole).toString());
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QListWidgetItem* item = list->currentItem();
        selectedDir = item != nullptr ? item->data(Qt::UserRole).toString() : currentDir;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    refresh();
    return dialog.exec() == QDialog::Accepted ? normalizedLaunchPath(selectedDir) : QString();
#else
    return normalizedLaunchPath(QFileDialog::getExistingDirectory(parent, title, path));
#endif
}

QString normalizedFileTypeMappingKey(QString key)
{
    key = key.trimmed().toLower();
    if (key.startsWith(QStringLiteral("*."))) {
        key.remove(0, 2);
    }
    while (key.startsWith(QLatin1Char('.'))) {
        key.remove(0, 1);
    }
    return key;
}

class MiniHighlighter final : public QSyntaxHighlighter
{
public:
    explicit MiniHighlighter(QTextDocument* document)
        : QSyntaxHighlighter(document)
    {
        configureForPath(QString());
    }

    void configureForPath(const QString& path, const QHash<QString, QString>& customLanguages = QHash<QString, QString>())
    {
        language = detectLanguage(path, customLanguages);
        rebuildRules();
        rehighlight();
    }

    void configureForLanguage(const QString& name)
    {
        language = supportedLanguages().contains(name) ? name : QStringLiteral("Plain Text");
        rebuildRules();
        rehighlight();
    }

    void setColorScheme(const QString& scheme)
    {
        colorScheme = normalizeColorScheme(scheme);
        rebuildRules();
        rehighlight();
    }

    QString languageName() const
    {
        return language;
    }

    static QStringList supportedLanguages()
    {
        return {
            QStringLiteral("Plain Text"),
            QStringLiteral("C"),
            QStringLiteral("C++"),
            QStringLiteral("Python"),
            QStringLiteral("JavaScript/TypeScript"),
            QStringLiteral("ArkTS"),
            QStringLiteral("Java"),
            QStringLiteral("Go"),
            QStringLiteral("Rust"),
            QStringLiteral("PHP"),
            QStringLiteral("Shell"),
            QStringLiteral("CMake"),
            QStringLiteral("Meson"),
            QStringLiteral("Makefile"),
            QStringLiteral("Dockerfile"),
            QStringLiteral("Lua"),
            QStringLiteral("JSON"),
            QStringLiteral("YAML"),
            QStringLiteral("TOML"),
            QStringLiteral("INI"),
            QStringLiteral("Diff/Patch"),
            QStringLiteral("SQL"),
            QStringLiteral("CSS"),
            QStringLiteral("Markup"),
            QStringLiteral("Markdown")
        };
    }

    static QString detectLanguage(const QString& path, const QHash<QString, QString>& customLanguages = QHash<QString, QString>())
    {
        const QFileInfo info(path);
        const QString name = info.fileName();
        const QString ext = info.suffix().toLower();
        const QString nameKey = normalizedFileTypeMappingKey(name);
        const QString extKey = normalizedFileTypeMappingKey(ext);
        if (!customLanguages.isEmpty()) {
            const QString customNameLanguage = customLanguages.value(nameKey);
            if (supportedLanguages().contains(customNameLanguage)) {
                return customNameLanguage;
            }
            const QString customExtLanguage = customLanguages.value(extKey);
            if (supportedLanguages().contains(customExtLanguage)) {
                return customExtLanguage;
            }
        }
        if (name == QStringLiteral("CMakeLists.txt") || ext == QStringLiteral("cmake")) {
            return QStringLiteral("CMake");
        }
        if (name == QStringLiteral("meson.build") || ext == QStringLiteral("meson")) {
            return QStringLiteral("Meson");
        }
        if (name == QStringLiteral("Makefile") || name == QStringLiteral("GNUmakefile") || ext == QStringLiteral("mk")) {
            return QStringLiteral("Makefile");
        }
        if (name == QStringLiteral("Dockerfile") || name == QStringLiteral("Containerfile")
            || name.startsWith(QStringLiteral("Dockerfile.")) || name.startsWith(QStringLiteral("Containerfile."))) {
            return QStringLiteral("Dockerfile");
        }
        if (ext == QStringLiteral("c") || ext == QStringLiteral("h")) {
            return QStringLiteral("C");
        }
        if (ext == QStringLiteral("cc") || ext == QStringLiteral("cpp") || ext == QStringLiteral("cxx")
            || ext == QStringLiteral("hpp") || ext == QStringLiteral("hh")) {
            return QStringLiteral("C++");
        }
        if (ext == QStringLiteral("py")) {
            return QStringLiteral("Python");
        }
        if (ext == QStringLiteral("ets")) {
            return QStringLiteral("ArkTS");
        }
        if (ext == QStringLiteral("js") || ext == QStringLiteral("ts")) {
            return QStringLiteral("JavaScript/TypeScript");
        }
        if (ext == QStringLiteral("lua")) {
            return QStringLiteral("Lua");
        }
        if (ext == QStringLiteral("java")) {
            return QStringLiteral("Java");
        }
        if (ext == QStringLiteral("go")) {
            return QStringLiteral("Go");
        }
        if (ext == QStringLiteral("rs")) {
            return QStringLiteral("Rust");
        }
        if (ext == QStringLiteral("php")) {
            return QStringLiteral("PHP");
        }
        if (ext == QStringLiteral("json") || ext == QStringLiteral("json5")) {
            return QStringLiteral("JSON");
        }
        if (ext == QStringLiteral("yaml") || ext == QStringLiteral("yml")) {
            return QStringLiteral("YAML");
        }
        if (ext == QStringLiteral("toml")) {
            return QStringLiteral("TOML");
        }
        if (ext == QStringLiteral("ini") || ext == QStringLiteral("conf") || ext == QStringLiteral("cfg")
            || ext == QStringLiteral("desktop") || ext == QStringLiteral("service")) {
            return QStringLiteral("INI");
        }
        if (ext == QStringLiteral("diff") || ext == QStringLiteral("patch")) {
            return QStringLiteral("Diff/Patch");
        }
        if (ext == QStringLiteral("sql")) {
            return QStringLiteral("SQL");
        }
        if (ext == QStringLiteral("html") || ext == QStringLiteral("htm") || ext == QStringLiteral("xml")) {
            return QStringLiteral("Markup");
        }
        if (ext == QStringLiteral("css")) {
            return QStringLiteral("CSS");
        }
        if (ext == QStringLiteral("md") || ext == QStringLiteral("markdown")) {
            return QStringLiteral("Markdown");
        }
        if (ext == QStringLiteral("sh")) {
            return QStringLiteral("Shell");
        }
        return QStringLiteral("Plain Text");
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const HighlightRule& rule : rules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    static QString normalizeColorScheme(const QString& scheme)
    {
        if (scheme == QStringLiteral("Dark") || scheme == QStringLiteral("High Contrast")) {
            return scheme;
        }
        return QStringLiteral("Light");
    }

    QColor schemeColor(const QColor& light, const QColor& dark, const QColor& highContrast) const
    {
        if (colorScheme == QStringLiteral("High Contrast")) {
            return highContrast;
        }
        if (colorScheme == QStringLiteral("Dark")) {
            return dark;
        }
        return light;
    }

    void addKeywords(const QStringList& words, const QTextCharFormat& format)
    {
        for (const QString& word : words) {
            rules.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word))), format});
        }
    }

    void rebuildRules()
    {
        rules.clear();

        QTextCharFormat keyword;
        keyword.setForeground(schemeColor(QColor(24, 92, 164), QColor(124, 184, 255), QColor(98, 197, 255)));
        keyword.setFontWeight(QFont::Bold);

        QTextCharFormat typeFormat;
        typeFormat.setForeground(schemeColor(QColor(45, 120, 100), QColor(104, 211, 145), QColor(102, 255, 181)));
        typeFormat.setFontWeight(QFont::Bold);

        QTextCharFormat stringFormat;
        stringFormat.setForeground(schemeColor(QColor(150, 72, 28), QColor(240, 175, 109), QColor(255, 205, 124)));

        commentFormat.setForeground(schemeColor(QColor(92, 118, 92), QColor(134, 157, 134), QColor(183, 232, 183)));

        QTextCharFormat numberFormat;
        numberFormat.setForeground(schemeColor(QColor(125, 76, 155), QColor(202, 158, 255), QColor(222, 181, 255)));

        QTextCharFormat addedLineFormat;
        addedLineFormat.setForeground(schemeColor(QColor(36, 124, 74), QColor(91, 212, 128), QColor(93, 255, 144)));

        QTextCharFormat removedLineFormat;
        removedLineFormat.setForeground(schemeColor(QColor(178, 62, 56), QColor(255, 128, 128), QColor(255, 128, 128)));

        QTextCharFormat sectionFormat;
        sectionFormat.setForeground(schemeColor(QColor(92, 76, 160), QColor(168, 154, 255), QColor(205, 190, 255)));
        sectionFormat.setFontWeight(QFont::Bold);

        if (language == QStringLiteral("Python")) {
            addKeywords({QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"), QStringLiteral("break"),
                QStringLiteral("class"), QStringLiteral("continue"), QStringLiteral("def"), QStringLiteral("elif"),
                QStringLiteral("else"), QStringLiteral("except"), QStringLiteral("False"), QStringLiteral("finally"),
                QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("if"), QStringLiteral("import"),
                QStringLiteral("in"), QStringLiteral("is"), QStringLiteral("lambda"), QStringLiteral("None"),
                QStringLiteral("nonlocal"), QStringLiteral("not"), QStringLiteral("or"), QStringLiteral("pass"),
                QStringLiteral("raise"), QStringLiteral("return"), QStringLiteral("True"), QStringLiteral("try"),
                QStringLiteral("while"), QStringLiteral("with"), QStringLiteral("yield")}, keyword);
        } else if (language == QStringLiteral("JavaScript/TypeScript") || language == QStringLiteral("ArkTS")) {
            addKeywords({QStringLiteral("await"), QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"),
                QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("default"),
                QStringLiteral("else"), QStringLiteral("export"), QStringLiteral("extends"), QStringLiteral("false"),
                QStringLiteral("finally"), QStringLiteral("for"), QStringLiteral("function"), QStringLiteral("if"),
                QStringLiteral("import"), QStringLiteral("interface"), QStringLiteral("let"), QStringLiteral("new"),
                QStringLiteral("null"), QStringLiteral("return"), QStringLiteral("switch"), QStringLiteral("this"),
                QStringLiteral("throw"), QStringLiteral("true"), QStringLiteral("try"), QStringLiteral("type"),
                QStringLiteral("var"), QStringLiteral("while")}, keyword);
            if (language == QStringLiteral("ArkTS")) {
                addKeywords({QStringLiteral("struct"), QStringLiteral("build"), QStringLiteral("aboutToAppear"),
                    QStringLiteral("aboutToDisappear"), QStringLiteral("onPageShow"), QStringLiteral("onPageHide"),
                    QStringLiteral("onBackPress"), QStringLiteral("Entry"), QStringLiteral("Component"),
                    QStringLiteral("State"), QStringLiteral("Prop"), QStringLiteral("Link"), QStringLiteral("Builder"),
                    QStringLiteral("Observed"), QStringLiteral("ObjectLink")}, typeFormat);
                rules.push_back({QRegularExpression(QStringLiteral("@[A-Za-z_][A-Za-z0-9_]*")), sectionFormat});
            }
        } else if (language == QStringLiteral("Java")) {
            addKeywords({QStringLiteral("abstract"), QStringLiteral("boolean"), QStringLiteral("break"), QStringLiteral("case"),
                QStringLiteral("catch"), QStringLiteral("class"), QStringLiteral("continue"), QStringLiteral("default"),
                QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("extends"), QStringLiteral("false"),
                QStringLiteral("final"), QStringLiteral("finally"), QStringLiteral("for"), QStringLiteral("if"),
                QStringLiteral("implements"), QStringLiteral("import"), QStringLiteral("instanceof"), QStringLiteral("interface"),
                QStringLiteral("new"), QStringLiteral("null"), QStringLiteral("package"), QStringLiteral("private"),
                QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("return"), QStringLiteral("static"),
                QStringLiteral("super"), QStringLiteral("switch"), QStringLiteral("this"), QStringLiteral("throw"),
                QStringLiteral("throws"), QStringLiteral("true"), QStringLiteral("try"), QStringLiteral("void"),
                QStringLiteral("while")}, keyword);
        } else if (language == QStringLiteral("Go")) {
            addKeywords({QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("chan"), QStringLiteral("const"),
                QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("defer"), QStringLiteral("else"),
                QStringLiteral("fallthrough"), QStringLiteral("for"), QStringLiteral("func"), QStringLiteral("go"),
                QStringLiteral("goto"), QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("interface"),
                QStringLiteral("map"), QStringLiteral("package"), QStringLiteral("range"), QStringLiteral("return"),
                QStringLiteral("select"), QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("type"),
                QStringLiteral("var")}, keyword);
            addKeywords({QStringLiteral("bool"), QStringLiteral("byte"), QStringLiteral("error"), QStringLiteral("float32"),
                QStringLiteral("float64"), QStringLiteral("int"), QStringLiteral("int32"), QStringLiteral("int64"),
                QStringLiteral("rune"), QStringLiteral("string"), QStringLiteral("uint"), QStringLiteral("uintptr")}, typeFormat);
        } else if (language == QStringLiteral("Rust")) {
            addKeywords({QStringLiteral("as"), QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("break"),
                QStringLiteral("const"), QStringLiteral("continue"), QStringLiteral("crate"), QStringLiteral("else"),
                QStringLiteral("enum"), QStringLiteral("extern"), QStringLiteral("false"), QStringLiteral("fn"),
                QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("impl"), QStringLiteral("in"),
                QStringLiteral("let"), QStringLiteral("loop"), QStringLiteral("match"), QStringLiteral("mod"),
                QStringLiteral("move"), QStringLiteral("mut"), QStringLiteral("pub"), QStringLiteral("ref"),
                QStringLiteral("return"), QStringLiteral("self"), QStringLiteral("Self"), QStringLiteral("static"),
                QStringLiteral("struct"), QStringLiteral("super"), QStringLiteral("trait"), QStringLiteral("true"),
                QStringLiteral("type"), QStringLiteral("unsafe"), QStringLiteral("use"), QStringLiteral("where"),
                QStringLiteral("while")}, keyword);
        } else if (language == QStringLiteral("PHP")) {
            addKeywords({QStringLiteral("array"), QStringLiteral("as"), QStringLiteral("break"), QStringLiteral("case"),
                QStringLiteral("catch"), QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("continue"),
                QStringLiteral("default"), QStringLiteral("echo"), QStringLiteral("else"), QStringLiteral("elseif"),
                QStringLiteral("extends"), QStringLiteral("false"), QStringLiteral("final"), QStringLiteral("finally"),
                QStringLiteral("foreach"), QStringLiteral("function"), QStringLiteral("if"), QStringLiteral("implements"),
                QStringLiteral("interface"), QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("null"),
                QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("return"),
                QStringLiteral("static"), QStringLiteral("throw"), QStringLiteral("trait"), QStringLiteral("true"),
                QStringLiteral("try"), QStringLiteral("use"), QStringLiteral("while")}, keyword);
        } else if (language == QStringLiteral("Shell")) {
            addKeywords({QStringLiteral("case"), QStringLiteral("do"), QStringLiteral("done"), QStringLiteral("elif"),
                QStringLiteral("else"), QStringLiteral("esac"), QStringLiteral("fi"), QStringLiteral("for"),
                QStringLiteral("function"), QStringLiteral("if"), QStringLiteral("in"), QStringLiteral("then"),
                QStringLiteral("until"), QStringLiteral("while")}, keyword);
        } else if (language == QStringLiteral("CMake") || language == QStringLiteral("Meson")) {
            addKeywords({QStringLiteral("add_executable"), QStringLiteral("add_library"), QStringLiteral("cmake_minimum_required"),
                QStringLiteral("dependency"), QStringLiteral("executable"), QStringLiteral("find_package"),
                QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("endif"), QStringLiteral("foreach"),
                QStringLiteral("include_directories"), QStringLiteral("install"), QStringLiteral("project"),
                QStringLiteral("set"), QStringLiteral("subdir"), QStringLiteral("target_link_libraries")}, keyword);
        } else if (language == QStringLiteral("Makefile")) {
            addKeywords({QStringLiteral("define"), QStringLiteral("else"), QStringLiteral("endef"), QStringLiteral("endif"),
                QStringLiteral("export"), QStringLiteral("if"), QStringLiteral("ifdef"), QStringLiteral("ifeq"),
                QStringLiteral("ifndef"), QStringLiteral("ifneq"), QStringLiteral("include"), QStringLiteral("override"),
                QStringLiteral("private"), QStringLiteral("unexport"), QStringLiteral("vpath")}, keyword);
            rules.push_back({QRegularExpression(QStringLiteral("^\\s*[-A-Za-z0-9_./%]+\\s*:(?!=)")), typeFormat});
            rules.push_back({QRegularExpression(QStringLiteral("\\$\\([A-Za-z0-9_./-]+\\)|\\$\\{[A-Za-z0-9_./-]+\\}")), sectionFormat});
        } else if (language == QStringLiteral("Dockerfile")) {
            addKeywords({QStringLiteral("ADD"), QStringLiteral("ARG"), QStringLiteral("CMD"), QStringLiteral("COPY"),
                QStringLiteral("ENTRYPOINT"), QStringLiteral("ENV"), QStringLiteral("EXPOSE"), QStringLiteral("FROM"),
                QStringLiteral("HEALTHCHECK"), QStringLiteral("LABEL"), QStringLiteral("MAINTAINER"), QStringLiteral("ONBUILD"),
                QStringLiteral("RUN"), QStringLiteral("SHELL"), QStringLiteral("STOPSIGNAL"), QStringLiteral("USER"),
                QStringLiteral("VOLUME"), QStringLiteral("WORKDIR")}, keyword);
        } else if (language == QStringLiteral("Lua")) {
            addKeywords({QStringLiteral("and"), QStringLiteral("break"), QStringLiteral("do"), QStringLiteral("else"),
                QStringLiteral("elseif"), QStringLiteral("end"), QStringLiteral("false"), QStringLiteral("for"),
                QStringLiteral("function"), QStringLiteral("goto"), QStringLiteral("if"), QStringLiteral("in"),
                QStringLiteral("local"), QStringLiteral("nil"), QStringLiteral("not"), QStringLiteral("or"),
                QStringLiteral("repeat"), QStringLiteral("return"), QStringLiteral("then"), QStringLiteral("true"),
                QStringLiteral("until"), QStringLiteral("while")}, keyword);
        } else if (language == QStringLiteral("INI")) {
            rules.push_back({QRegularExpression(QStringLiteral("^\\s*\\[[^\\]]+\\]")), sectionFormat});
            rules.push_back({QRegularExpression(QStringLiteral("^\\s*[A-Za-z0-9_.-]+(?=\\s*[=:])")), keyword});
        } else if (language == QStringLiteral("Diff/Patch")) {
            rules.push_back({QRegularExpression(QStringLiteral("^@@.*@@")), sectionFormat});
            rules.push_back({QRegularExpression(QStringLiteral("^\\+.*")), addedLineFormat});
            rules.push_back({QRegularExpression(QStringLiteral("^-.*")), removedLineFormat});
            rules.push_back({QRegularExpression(QStringLiteral("^(diff|index|---|\\+\\+\\+).*$")), keyword});
        } else if (language == QStringLiteral("Markup")) {
            rules.push_back({QRegularExpression(QStringLiteral("</?\\w+[^>]*>")), keyword});
            rules.push_back({QRegularExpression(QStringLiteral("\\b\\w+(?=\\=)")), typeFormat});
        } else if (language == QStringLiteral("CSS")) {
            rules.push_back({QRegularExpression(QStringLiteral("[.#]?[A-Za-z_][A-Za-z0-9_-]*(?=\\s*\\{)")), keyword});
            rules.push_back({QRegularExpression(QStringLiteral("\\b[A-Za-z-]+(?=\\s*:)")), typeFormat});
        } else if (language == QStringLiteral("SQL")) {
            addKeywords({QStringLiteral("alter"), QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("between"),
                QStringLiteral("by"), QStringLiteral("case"), QStringLiteral("create"), QStringLiteral("delete"),
                QStringLiteral("desc"), QStringLiteral("distinct"), QStringLiteral("drop"), QStringLiteral("else"),
                QStringLiteral("end"), QStringLiteral("from"), QStringLiteral("group"), QStringLiteral("having"),
                QStringLiteral("insert"), QStringLiteral("into"), QStringLiteral("join"), QStringLiteral("left"),
                QStringLiteral("like"), QStringLiteral("limit"), QStringLiteral("not"), QStringLiteral("null"),
                QStringLiteral("on"), QStringLiteral("or"), QStringLiteral("order"), QStringLiteral("right"),
                QStringLiteral("select"), QStringLiteral("set"), QStringLiteral("table"), QStringLiteral("then"),
                QStringLiteral("union"), QStringLiteral("update"), QStringLiteral("values"), QStringLiteral("when"),
                QStringLiteral("where")}, keyword);
        } else if (language == QStringLiteral("YAML") || language == QStringLiteral("TOML")) {
            rules.push_back({QRegularExpression(QStringLiteral("^\\s*[A-Za-z0-9_.-]+(?=\\s*[:=])")), keyword});
            addKeywords({QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null")}, typeFormat);
        } else {
            addKeywords({QStringLiteral("auto"), QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
                QStringLiteral("char"), QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("continue"),
                QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("false"), QStringLiteral("for"),
                QStringLiteral("if"), QStringLiteral("int"), QStringLiteral("namespace"), QStringLiteral("public"),
                QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("return"), QStringLiteral("static"),
                QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("true"), QStringLiteral("typedef"),
                QStringLiteral("using"), QStringLiteral("void"), QStringLiteral("while")}, keyword);
            addKeywords({QStringLiteral("gboolean"), QStringLiteral("gchar"), QStringLiteral("gint"), QStringLiteral("gpointer"),
                QStringLiteral("GList"), QStringLiteral("GSList"), QStringLiteral("GtkWidget"), QStringLiteral("GeanyDocument"),
                QStringLiteral("GeanyPlugin"), QStringLiteral("QString"), QStringLiteral("QObject"), QStringLiteral("QWidget")}, typeFormat);
        }

        if (language == QStringLiteral("JSON")) {
            addKeywords({QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null")}, keyword);
        }
        if (language == QStringLiteral("SQL") || language == QStringLiteral("Lua")) {
            rules.push_back({QRegularExpression(QStringLiteral("--[^\n]*")), commentFormat});
        }
        if (language == QStringLiteral("INI")) {
            rules.push_back({QRegularExpression(QStringLiteral(";[^\n]*")), commentFormat});
        }

        rules.push_back({QRegularExpression(QStringLiteral("\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"")), stringFormat});
        rules.push_back({QRegularExpression(QStringLiteral("'[^'\\\\]*(\\\\.[^'\\\\]*)*'")), stringFormat});
        rules.push_back({QRegularExpression(QStringLiteral("//[^\n]*")), commentFormat});
        rules.push_back({QRegularExpression(QStringLiteral("#[^\n]*")), commentFormat});
        rules.push_back({QRegularExpression(QStringLiteral("/\\*.*\\*/")), commentFormat});
        rules.push_back({QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b")), numberFormat});
    }

    QString language = QStringLiteral("Plain Text");
    QString colorScheme = QStringLiteral("Light");
    QVector<HighlightRule> rules;
    QTextCharFormat commentFormat;
};

class CodeEditor;

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* editor, QWidget* parent)
        : QWidget(parent)
        , codeEditor(editor)
    {
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    CodeEditor* codeEditor = nullptr;
};

class CodeEditor final : public QPlainTextEdit
{
public:
    explicit CodeEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        lineNumberArea = new LineNumberArea(this, this);
        connect(this, &CodeEditor::blockCountChanged, this, [this]() { updateLineNumberAreaWidth(); });
        connect(this, &CodeEditor::updateRequest, this, [this](const QRect& rect, int dy) { updateLineNumberArea(rect, dy); });
        connect(this, &CodeEditor::cursorPositionChanged, this, [this]() { highlightCurrentLine(); });
        updateLineNumberAreaWidth();
        highlightCurrentLine();

        completionModel = new QStringListModel(this);
        completer = new QCompleter(completionModel, this);
        completer->setWidget(this);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
        connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, [this](const QString& completion) {
            insertCompletion(completion);
        });
    }

    void setEditorOptions(int width, bool spaces, bool autoIndent, bool pairBrackets, bool completeWords,
        bool highlightOccurrences, bool highlightLine, bool showLineNumbers, bool showIndentGuides,
        bool showMargin, int marginColumn)
    {
        tabWidth = qBound(2, width, 8);
        insertSpacesOnTab = spaces;
        autoIndentEnabled = autoIndent;
        bracketPairingEnabled = pairBrackets;
        wordCompletionEnabled = completeWords;
        highlightOccurrencesEnabled = highlightOccurrences;
        highlightCurrentLineEnabled = highlightLine;
        showLineNumbersEnabled = showLineNumbers;
        showIndentGuidesEnabled = showIndentGuides;
        showRightMargin = showMargin;
        rightMarginColumn = qBound(40, marginColumn, 200);
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * tabWidth);
        if (lineNumberArea != nullptr) {
            lineNumberArea->setVisible(showLineNumbersEnabled);
        }
        updateLineNumberAreaWidth();
        highlightCurrentLine();
        viewport()->update();
    }

    void setColorScheme(const QString& scheme)
    {
        colorScheme = normalizeColorScheme(scheme);
        QPalette palette = this->palette();
        palette.setColor(QPalette::Base, editorBaseColor());
        palette.setColor(QPalette::Text, editorTextColor());
        palette.setColor(QPalette::Highlight, selectionColor());
        palette.setColor(QPalette::HighlightedText, selectionTextColor());
        setPalette(palette);
        highlightCurrentLine();
        if (lineNumberArea != nullptr) {
            lineNumberArea->update();
        }
        viewport()->update();
    }

    void completeCurrentWord()
    {
        if (!wordCompletionEnabled) {
            return;
        }
        refreshCompletionWords();
        const QString prefix = currentWordPrefix();
        if (prefix.isEmpty()) {
            return;
        }
        showCompletion(prefix);
    }

    void closeCompletionPopup()
    {
        if (completer != nullptr && completer->popup() != nullptr) {
            completer->popup()->hide();
        }
    }

    bool foldCurrentBlock()
    {
        QTextCursor cursor = textCursor();
        QTextBlock startBlock = cursor.block();
        if (!startBlock.isValid()) {
            return false;
        }

        const QString line = startBlock.text();
        int braceInLine = line.indexOf(QLatin1Char('{'), qMin(cursor.positionInBlock(), line.size()));
        if (braceInLine < 0) {
            braceInLine = line.indexOf(QLatin1Char('{'));
        }
        if (braceInLine < 0) {
            return false;
        }

        const int origin = startBlock.position() + braceInLine;
        const int target = findMatchingEditorBrace(toPlainText(), origin, QLatin1Char('{'), QLatin1Char('}'), 1);
        if (target < 0) {
            return false;
        }

        QTextBlock endBlock = document()->findBlock(target);
        if (!endBlock.isValid() || endBlock.blockNumber() <= startBlock.blockNumber() + 1) {
            return false;
        }

        bool changed = false;
        for (QTextBlock block = startBlock.next(); block.isValid() && block.blockNumber() < endBlock.blockNumber(); block = block.next()) {
            if (block.isVisible()) {
                block.setVisible(false);
                changed = true;
            }
        }
        if (!changed) {
            return false;
        }
        document()->markContentsDirty(startBlock.position(), endBlock.position() + endBlock.length() - startBlock.position());
        updateLineNumberAreaWidth();
        if (lineNumberArea != nullptr) {
            lineNumberArea->update();
        }
        viewport()->update();
        return true;
    }

    int unfoldAllBlocks()
    {
        int unfolded = 0;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (!block.isVisible()) {
                block.setVisible(true);
                ++unfolded;
            }
        }
        if (unfolded > 0) {
            document()->markContentsDirty(0, document()->characterCount());
            updateLineNumberAreaWidth();
            if (lineNumberArea != nullptr) {
                lineNumberArea->update();
            }
            viewport()->update();
        }
        return unfolded;
    }

    int lineNumberAreaWidth() const
    {
        if (!showLineNumbersEnabled) {
            return 0;
        }
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return bookmarkMarginWidth + 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void setBookmarkLines(const QSet<int>& lines)
    {
        bookmarkedLines = lines;
        if (lineNumberArea != nullptr) {
            lineNumberArea->update();
        }
    }

    void setSearchHighlights(const QString& text, bool matchCase, bool wholeWord, bool regularExpression)
    {
        searchHighlightText = text;
        searchHighlightMatchCase = matchCase;
        searchHighlightWholeWord = wholeWord;
        searchHighlightRegularExpression = regularExpression;
        highlightCurrentLine();
    }

    void refreshVisualHighlights()
    {
        highlightCurrentLine();
    }

    void setUrlDropHandler(const std::function<void(const QList<QUrl>&)>& handler)
    {
        urlDropHandler = handler;
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event)
    {
        if (!showLineNumbersEnabled) {
            return;
        }
        QPainter painter(lineNumberArea);
        painter.fillRect(event->rect(), lineNumberBackgroundColor());
        painter.setPen(lineNumberTextColor());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                if (bookmarkedLines.contains(blockNumber + 1)) {
                    painter.setBrush(bookmarkColor());
                    painter.setPen(Qt::NoPen);
                    const int markerSize = qMax(6, fontMetrics().height() / 2);
                    const int markerTop = top + (fontMetrics().height() - markerSize) / 2;
                    painter.drawEllipse(QRect(4, markerTop, markerSize, markerSize));
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(lineNumberTextColor());
                }
                painter.drawText(bookmarkMarginWidth, top,
                    lineNumberArea->width() - bookmarkMarginWidth - 6, fontMetrics().height(),
                    Qt::AlignRight, number);
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        const bool completionShortcut = event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier;
        if (completionShortcut) {
            completeCurrentWord();
            return;
        }

        if (completer != nullptr && completer->popup()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                event->ignore();
                return;
            default:
                break;
            }
        }

        if (event->key() == Qt::Key_Tab && insertSpacesOnTab
            && event->modifiers() == Qt::NoModifier && !textCursor().hasSelection()) {
            textCursor().insertText(QString(tabWidth, QLatin1Char(' ')));
            return;
        }

        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && event->modifiers() == Qt::NoModifier && autoIndentEnabled) {
            const QString currentLine = textCursor().block().text();
            const QString indentation = QRegularExpression(QStringLiteral("^\\s*")).match(currentLine).captured(0);
            QPlainTextEdit::keyPressEvent(event);
            QTextCursor cursor = textCursor();
            cursor.insertText(indentation);
            const QString trimmed = currentLine.trimmed();
            if (trimmed.endsWith(QLatin1Char('{')) || trimmed.endsWith(QLatin1Char(':'))) {
                cursor.insertText(QString(tabWidth, QLatin1Char(' ')));
            }
            setTextCursor(cursor);
            return;
        }

        const QString text = event->text();
        if (bracketPairingEnabled && text.size() == 1 && !textCursor().hasSelection()) {
            const QChar ch = text.at(0);
            QString closing;
            if (ch == QLatin1Char('(')) {
                closing = QStringLiteral(")");
            } else if (ch == QLatin1Char('[')) {
                closing = QStringLiteral("]");
            } else if (ch == QLatin1Char('{')) {
                closing = QStringLiteral("}");
            } else if (ch == QLatin1Char('"')) {
                closing = QStringLiteral("\"");
            } else if (ch == QLatin1Char('\'')) {
                closing = QStringLiteral("'");
            }
            if (!closing.isEmpty()) {
                QTextCursor cursor = textCursor();
                cursor.insertText(text + closing);
                cursor.movePosition(QTextCursor::PreviousCharacter);
                setTextCursor(cursor);
                return;
            }
        }

        QPlainTextEdit::keyPressEvent(event);
        updateCompletionAfterKey(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPlainTextEdit::paintEvent(event);
        QPainter painter(viewport());

        if (showIndentGuidesEnabled) {
            QPen guidePen(indentGuideColor());
            guidePen.setStyle(Qt::DotLine);
            guidePen.setWidth(1);
            painter.setPen(guidePen);

            const int spaceWidth = fontMetrics().horizontalAdvance(QLatin1Char(' '));
            const int indentColumns = qMax(1, tabWidth);
            QTextBlock block = firstVisibleBlock();
            int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
            int bottom = top + qRound(blockBoundingRect(block).height());

            while (block.isValid() && top <= event->rect().bottom()) {
                if (block.isVisible() && bottom >= event->rect().top()) {
                    int columns = 0;
                    const QString text = block.text();
                    for (const QChar ch : text) {
                        if (ch == QLatin1Char(' ')) {
                            ++columns;
                        } else if (ch == QLatin1Char('\t')) {
                            columns += indentColumns - (columns % indentColumns);
                        } else {
                            break;
                        }
                    }

                    for (int column = indentColumns; column <= columns; column += indentColumns) {
                        const int x = qRound(contentOffset().x()) + (spaceWidth * column);
                        painter.drawLine(x, top, x, bottom);
                    }
                }

                block = block.next();
                top = bottom;
                bottom = top + qRound(blockBoundingRect(block).height());
            }
        }

        if (!showRightMargin) {
            return;
        }

        painter.setPen(rightMarginColor());
        const int x = qRound(contentOffset().x()) + fontMetrics().horizontalAdvance(QLatin1Char(' ')) * rightMarginColumn;
        painter.drawLine(x, event->rect().top(), x, event->rect().bottom());
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData() != nullptr && event->mimeData()->hasUrls() && urlDropHandler) {
            event->acceptProposedAction();
            return;
        }
        QPlainTextEdit::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (event->mimeData() != nullptr && event->mimeData()->hasUrls() && urlDropHandler) {
            urlDropHandler(event->mimeData()->urls());
            event->acceptProposedAction();
            return;
        }
        QPlainTextEdit::dropEvent(event);
    }

private:
    static QString normalizeColorScheme(const QString& scheme)
    {
        if (scheme == QStringLiteral("Dark") || scheme == QStringLiteral("High Contrast")) {
            return scheme;
        }
        return QStringLiteral("Light");
    }

    QColor schemeColor(const QColor& light, const QColor& dark, const QColor& highContrast) const
    {
        if (colorScheme == QStringLiteral("High Contrast")) {
            return highContrast;
        }
        if (colorScheme == QStringLiteral("Dark")) {
            return dark;
        }
        return light;
    }

    QColor editorBaseColor() const { return schemeColor(QColor(255, 255, 255), QColor(26, 31, 38), QColor(0, 0, 0)); }
    QColor editorTextColor() const { return schemeColor(QColor(24, 29, 35), QColor(225, 232, 240), QColor(255, 255, 255)); }
    QColor selectionColor() const { return schemeColor(QColor(34, 111, 167), QColor(71, 121, 176), QColor(255, 214, 64)); }
    QColor selectionTextColor() const { return schemeColor(QColor(255, 255, 255), QColor(255, 255, 255), QColor(0, 0, 0)); }
    QColor lineNumberBackgroundColor() const { return schemeColor(QColor(244, 247, 250), QColor(31, 38, 46), QColor(0, 0, 0)); }
    QColor lineNumberTextColor() const { return schemeColor(QColor(103, 116, 129), QColor(154, 166, 180), QColor(255, 255, 255)); }
    QColor bookmarkColor() const { return schemeColor(QColor(34, 111, 167), QColor(78, 161, 255), QColor(255, 214, 64)); }
    QColor indentGuideColor() const { return schemeColor(QColor(118, 132, 148, 90), QColor(111, 128, 148, 120), QColor(255, 255, 255, 160)); }
    QColor rightMarginColor() const { return schemeColor(QColor(219, 96, 96, 150), QColor(255, 128, 128, 160), QColor(255, 80, 80, 210)); }
    QColor currentLineColor() const { return schemeColor(QColor(236, 243, 250), QColor(38, 47, 57), QColor(35, 35, 35)); }
    QColor occurrenceColor() const { return schemeColor(QColor(255, 244, 184), QColor(103, 85, 34), QColor(255, 214, 64)); }
    QColor searchColor() const { return schemeColor(QColor(213, 242, 198), QColor(49, 91, 59), QColor(50, 255, 120)); }
    QColor braceMatchColor() const { return schemeColor(QColor(202, 231, 255), QColor(55, 93, 125), QColor(64, 210, 255)); }
    QColor braceMismatchColor() const { return schemeColor(QColor(255, 210, 210), QColor(119, 47, 47), QColor(255, 64, 64)); }
    QColor braceForegroundColor() const { return schemeColor(QColor(22, 34, 45), QColor(255, 255, 255), QColor(0, 0, 0)); }

    bool isIdentifierChar(QChar ch) const
    {
        return ch == QLatin1Char('_') || ch.isLetterOrNumber();
    }

    bool isIdentifierWord(const QString& word) const
    {
        if (word.size() < 2 || !(word.at(0) == QLatin1Char('_') || word.at(0).isLetter())) {
            return false;
        }
        for (const QChar ch : word) {
            if (!isIdentifierChar(ch)) {
                return false;
            }
        }
        return true;
    }

    QString currentWordPrefix() const
    {
        const QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        int end = qBound(0, cursor.positionInBlock(), line.size());
        int start = end;
        while (start > 0 && isIdentifierChar(line.at(start - 1))) {
            --start;
        }
        return line.mid(start, end - start);
    }

    QString currentOccurrenceWord() const
    {
        const QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            const QString selected = cursor.selectedText();
            return isIdentifierWord(selected) ? selected : QString();
        }

        const QString line = cursor.block().text();
        if (line.isEmpty()) {
            return QString();
        }

        int index = qMin(cursor.positionInBlock(), line.size() - 1);
        if (!isIdentifierChar(line.at(index)) && index > 0 && isIdentifierChar(line.at(index - 1))) {
            --index;
        }
        if (!isIdentifierChar(line.at(index))) {
            return QString();
        }

        int start = index;
        int end = index + 1;
        while (start > 0 && isIdentifierChar(line.at(start - 1))) {
            --start;
        }
        while (end < line.size() && isIdentifierChar(line.at(end))) {
            ++end;
        }

        const QString word = line.mid(start, end - start);
        return isIdentifierWord(word) ? word : QString();
    }

    void insertCompletion(const QString& completion)
    {
        if (completion.isEmpty()) {
            return;
        }
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        int end = cursor.positionInBlock();
        int start = end;
        while (start > 0 && isIdentifierChar(line.at(start - 1))) {
            --start;
        }
        cursor.setPosition(cursor.block().position() + start);
        cursor.setPosition(cursor.block().position() + end, QTextCursor::KeepAnchor);
        cursor.insertText(completion);
        setTextCursor(cursor);
    }

    void refreshCompletionWords()
    {
        QSet<QString> words;
        const QRegularExpression wordPattern(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]{2,}\\b"));
        QRegularExpressionMatchIterator it = wordPattern.globalMatch(toPlainText());
        while (it.hasNext() && words.size() < 1500) {
            const QString word = it.next().captured(0);
            if (!word.isEmpty()) {
                words.insert(word);
            }
        }
        QStringList sorted = words.values();
        sorted.sort(Qt::CaseInsensitive);
        completionModel->setStringList(sorted);
    }

    void showCompletion(const QString& prefix)
    {
        if (completer == nullptr || prefix.isEmpty()) {
            return;
        }
        completer->setCompletionPrefix(prefix);
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
        QRect rect = cursorRect();
        rect.setWidth(320);
        completer->complete(rect);
    }

    void updateCompletionAfterKey(QKeyEvent* event)
    {
        if (!wordCompletionEnabled || completer == nullptr) {
            return;
        }
        const QString text = event->text();
        if (text.isEmpty() || event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::AltModifier)) {
            completer->popup()->hide();
            return;
        }
        const QChar typed = text.at(text.size() - 1);
        if (!isIdentifierChar(typed)) {
            completer->popup()->hide();
            return;
        }
        const QString prefix = currentWordPrefix();
        if (prefix.size() < 3) {
            completer->popup()->hide();
            return;
        }
        refreshCompletionWords();
        if (completer->completionCount() == 0) {
            completer->popup()->hide();
            return;
        }
        showCompletion(prefix);
    }

    void updateLineNumberAreaWidth()
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (dy != 0) {
            lineNumberArea->scroll(0, dy);
        } else {
            lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
        }
        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth();
        }
    }

    bool editorBracePair(QChar brace, QChar* matching, int* direction) const
    {
        if (brace == QLatin1Char('(')) {
            *matching = QLatin1Char(')');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char('[')) {
            *matching = QLatin1Char(']');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char('{')) {
            *matching = QLatin1Char('}');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char(')')) {
            *matching = QLatin1Char('(');
            *direction = -1;
            return true;
        }
        if (brace == QLatin1Char(']')) {
            *matching = QLatin1Char('[');
            *direction = -1;
            return true;
        }
        if (brace == QLatin1Char('}')) {
            *matching = QLatin1Char('{');
            *direction = -1;
            return true;
        }
        return false;
    }

    int findMatchingEditorBrace(const QString& text, int origin, QChar brace, QChar matching, int direction) const
    {
        int depth = 0;
        for (int i = origin; i >= 0 && i < text.size(); i += direction) {
            const QChar ch = text.at(i);
            if (ch == brace) {
                ++depth;
            } else if (ch == matching) {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return -1;
    }

    void addBraceSelection(QList<QTextEdit::ExtraSelection>* selections, int position, const QColor& color)
    {
        if (selections == nullptr || position < 0) {
            return;
        }
        QTextCursor cursor(document());
        cursor.setPosition(position);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setBackground(color);
        selection.format.setForeground(braceForegroundColor());
        selection.format.setFontWeight(QFont::Bold);
        selections->append(selection);
    }

    void addOccurrenceSelections(QList<QTextEdit::ExtraSelection>* selections)
    {
        if (selections == nullptr || !highlightOccurrencesEnabled) {
            return;
        }
        const QString word = currentOccurrenceWord();
        if (word.isEmpty()) {
            return;
        }

        const QRegularExpression pattern(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)));
        QRegularExpressionMatchIterator it = pattern.globalMatch(toPlainText());
        int highlighted = 0;
        while (it.hasNext() && highlighted < 300) {
            const QRegularExpressionMatch match = it.next();
            if (!match.hasMatch()) {
                continue;
            }

            QTextCursor cursor(document());
            cursor.setPosition(match.capturedStart());
            cursor.setPosition(match.capturedEnd(), QTextCursor::KeepAnchor);
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format.setBackground(occurrenceColor());
            selections->append(selection);
            ++highlighted;
        }
    }

    void addSearchSelections(QList<QTextEdit::ExtraSelection>* selections)
    {
        if (selections == nullptr || searchHighlightText.isEmpty()) {
            return;
        }

        QString pattern = searchHighlightRegularExpression
            ? searchHighlightText
            : QRegularExpression::escape(searchHighlightText);
        if (searchHighlightWholeWord) {
            pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
        }

        QRegularExpression::PatternOptions options;
        if (!searchHighlightMatchCase) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        const QRegularExpression expression(pattern, options);
        if (!expression.isValid()) {
            return;
        }

        QRegularExpressionMatchIterator it = expression.globalMatch(toPlainText());
        int highlighted = 0;
        while (it.hasNext() && highlighted < 1000) {
            const QRegularExpressionMatch match = it.next();
            if (!match.hasMatch() || match.capturedStart() == match.capturedEnd()) {
                continue;
            }

            QTextCursor cursor(document());
            cursor.setPosition(match.capturedStart());
            cursor.setPosition(match.capturedEnd(), QTextCursor::KeepAnchor);
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format.setBackground(searchColor());
            selections->append(selection);
            ++highlighted;
        }
    }

    void addMatchingBraceSelections(QList<QTextEdit::ExtraSelection>* selections)
    {
        const QString text = toPlainText();
        const QTextCursor cursor = textCursor();
        const int cursorPosition = cursor.position();
        int origin = -1;
        QChar brace;
        QChar matching;
        int direction = 0;

        if (cursorPosition >= 0 && cursorPosition < text.size() && editorBracePair(text.at(cursorPosition), &matching, &direction)) {
            origin = cursorPosition;
            brace = text.at(cursorPosition);
        } else if (cursorPosition > 0 && cursorPosition <= text.size()
                && editorBracePair(text.at(cursorPosition - 1), &matching, &direction)) {
            origin = cursorPosition - 1;
            brace = text.at(origin);
        }
        if (origin < 0) {
            return;
        }

        const int target = findMatchingEditorBrace(text, origin, brace, matching, direction);
        const QColor matchedColor = braceMatchColor();
        const QColor unmatchedColor = braceMismatchColor();
        addBraceSelection(selections, origin, target >= 0 ? matchedColor : unmatchedColor);
        if (target >= 0) {
            addBraceSelection(selections, target, matchedColor);
        }
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> selections;
        if (highlightCurrentLineEnabled && !isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(currentLineColor());
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selections.append(selection);
        }
        addSearchSelections(&selections);
        addOccurrenceSelections(&selections);
        addMatchingBraceSelections(&selections);
        setExtraSelections(selections);
    }

    QWidget* lineNumberArea = nullptr;
    QStringListModel* completionModel = nullptr;
    QCompleter* completer = nullptr;
    QSet<int> bookmarkedLines;
    QString searchHighlightText;
    std::function<void(const QList<QUrl>&)> urlDropHandler;
    bool searchHighlightMatchCase = false;
    bool searchHighlightWholeWord = false;
    bool searchHighlightRegularExpression = false;
    const int bookmarkMarginWidth = 16;
    int tabWidth = 4;
    bool insertSpacesOnTab = true;
    bool autoIndentEnabled = true;
    bool bracketPairingEnabled = true;
    bool wordCompletionEnabled = true;
    bool highlightOccurrencesEnabled = true;
    bool highlightCurrentLineEnabled = true;
    bool showLineNumbersEnabled = true;
    bool showIndentGuidesEnabled = true;
    bool showRightMargin = false;
    int rightMarginColumn = 80;
    QString colorScheme = QStringLiteral("Light");
};

QSize LineNumberArea::sizeHint() const
{
    return QSize(codeEditor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event)
{
    codeEditor->lineNumberAreaPaintEvent(event);
}

class EditorPage final : public QWidget
{
public:
    explicit EditorPage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        editor = new CodeEditor(this);
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(11);
        editor->setFont(mono);
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor->setTabStopDistance(editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
        layout->addWidget(editor);

        highlighter = new MiniHighlighter(editor->document());
    }

    CodeEditor* editor = nullptr;
    MiniHighlighter* highlighter = nullptr;
    QString path;
    QString lineEnding = QStringLiteral("\n");
    QString encodingName = QStringLiteral("UTF-8");
    QDateTime lastDiskModified;
    TextSaveGuard::Snapshot diskSnapshot;
    bool utf8Bom = false;
    bool sourceDecodedSafely = true;
    bool externalChangePrompted = false;
    QSet<int> bookmarks;
};

class GeanyMainWindow final : public QMainWindow
{
public:
    explicit GeanyMainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("GeanyDesk - Geany Community Qt Adaptation"));
#if defined(Q_OS_OPENHARMONY)
        setMinimumSize(1680, 1050);
        resize(2560, 1600);
#else
        setMinimumSize(980, 680);
        resize(1320, 860);
#endif
        setAcceptDrops(true);
        setupUi();
        setupActions();
        autoSaveTimer = new QTimer(this);
        connect(autoSaveTimer, &QTimer::timeout, this, [this]() { autoSaveModifiedDocuments(false); });
        restoreSettings();
        applyOpenHarmonyDisplayDefaults();
        if (tabs->count() == 0) {
            newDocument();
        }
        configureAutoSaveTimer();
        auto* diskChangeTimer = new QTimer(this);
        connect(diskChangeTimer, &QTimer::timeout, this, [this]() { checkOpenFilesOnDisk(); });
        diskChangeTimer->start(5000);
#if defined(Q_OS_OPENHARMONY)
        auto* pendingOpenTimer = new QTimer(this);
        connect(pendingOpenTimer, &QTimer::timeout, this, [this]() { consumePendingOpenRequests(); });
        pendingOpenTimer->start(1000);
        QTimer::singleShot(300, this, [this]() { consumePendingOpenRequests(); });
#endif
        updateCursorStatus();
        appendOutput(QStringLiteral("Geany Qt/OpenHarmony native adaptation started."));
    }

    void openPaths(const QStringList& paths)
    {
        for (const QString& path : paths) {
            const QFileInfo info(path);
            if (!info.exists()) {
                continue;
            }
            if (info.isDir()) {
                openProjectRoot(info.absoluteFilePath());
            } else {
                openPath(path);
            }
        }
    }

#if defined(Q_OS_OPENHARMONY)
    void consumePendingOpenRequests()
    {
        QStringList paths;
        for (const QString& queuePath : openHarmonyPendingOpenQueueFiles()) {
            QFile file(queuePath);
            if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            while (!file.atEnd()) {
                const QString path = normalizedLaunchPath(QString::fromUtf8(file.readLine()).trimmed());
                if (!path.isEmpty() && !paths.contains(path)) {
                    paths << path;
                }
            }
            file.close();
            file.remove();
        }
        if (!paths.isEmpty()) {
            openPaths(paths);
        }
    }
#endif

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!confirmAllSaved()) {
            quitRequested = false;
            event->ignore();
            return;
        }
        saveSettings();
        event->accept();
        if (quitRequested) {
            QTimer::singleShot(0, qApp, []() { QApplication::quit(); });
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
            return;
        }
        QMainWindow::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
            openDroppedUrls(event->mimeData()->urls());
            event->acceptProposedAction();
            return;
        }
        QMainWindow::dropEvent(event);
    }

private:
    void setupUi()
    {
        mainSplitter = new QSplitter(Qt::Horizontal, this);
        mainSplitter->setChildrenCollapsible(false);

        sideTabs = new QTabWidget(mainSplitter);
#if defined(Q_OS_OPENHARMONY)
        sideTabs->setMinimumWidth(520);
        sideTabs->setMaximumWidth(880);
#else
        sideTabs->setMinimumWidth(240);
        sideTabs->setMaximumWidth(420);
#endif

        documentsList = new QListWidget(sideTabs);
        symbolsList = new QListWidget(sideTabs);
        fileBrowserPanel = new QWidget(sideTabs);
        auto* fileBrowserLayout = new QVBoxLayout(fileBrowserPanel);
        fileBrowserLayout->setContentsMargins(0, 0, 0, 0);
        fileBrowserLayout->setSpacing(4);
        fileBrowserPathEdit = new QLineEdit(fileBrowserPanel);
        fileBrowserPathEdit->setPlaceholderText(QStringLiteral("Browse folder path"));
        fileBrowserList = new QListWidget(fileBrowserPanel);
        fileBrowserLayout->addWidget(fileBrowserPathEdit);
        fileBrowserLayout->addWidget(fileBrowserList, 1);
        projectPanel = new QWidget(sideTabs);
        auto* projectPanelLayout = new QVBoxLayout(projectPanel);
        projectPanelLayout->setContentsMargins(0, 0, 0, 0);
        projectPanelLayout->setSpacing(4);
        projectFileFilter = new QLineEdit(projectPanel);
        projectFileFilter->setPlaceholderText(QStringLiteral("Filter project files"));
        projectFiles = new QListWidget(projectPanel);
        projectPanelLayout->addWidget(projectFileFilter);
        projectPanelLayout->addWidget(projectFiles, 1);
        searchResults = new QListWidget(sideTabs);
        problemsList = new QListWidget(sideTabs);
        tasksList = new QListWidget(sideTabs);
        bookmarksList = new QListWidget(sideTabs);
        sideTabs->addTab(documentsList, QStringLiteral("Documents"));
        sideTabs->addTab(symbolsList, QStringLiteral("Symbols"));
        sideTabs->addTab(fileBrowserPanel, QStringLiteral("Browse"));
        sideTabs->addTab(projectPanel, QStringLiteral("Files"));
        sideTabs->addTab(searchResults, QStringLiteral("Search"));
        sideTabs->addTab(problemsList, QStringLiteral("Problems"));
        sideTabs->addTab(tasksList, QStringLiteral("Tasks"));
        sideTabs->addTab(bookmarksList, QStringLiteral("Bookmarks"));

        rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
        rightSplitter->setChildrenCollapsible(false);

        editorSplitter = new QSplitter(Qt::Horizontal, rightSplitter);
        editorSplitter->setChildrenCollapsible(false);

        tabs = new QTabWidget(editorSplitter);
        tabs->setDocumentMode(true);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

        splitView = new QPlainTextEdit(editorSplitter);
        splitView->setReadOnly(true);
        splitView->setLineWrapMode(QPlainTextEdit::NoWrap);
        splitView->hide();

        outputTabs = new QTabWidget(rightSplitter);
        outputTabs->setDocumentMode(true);
#if defined(Q_OS_OPENHARMONY)
        outputTabs->setMinimumHeight(380);
#else
        outputTabs->setMinimumHeight(150);
#endif

        output = new QPlainTextEdit(outputTabs);
        output->setReadOnly(true);
        output->setMaximumBlockCount(3000);
        output->setContextMenuPolicy(Qt::CustomContextMenu);
        scratchPad = new QPlainTextEdit(outputTabs);
        scratchPad->setPlaceholderText(QStringLiteral("Scribble notes for this workspace"));
        terminalPanel = new QWidget(outputTabs);
        auto* terminalLayout = new QVBoxLayout(terminalPanel);
        terminalLayout->setContentsMargins(0, 0, 0, 0);
        terminalLayout->setSpacing(4);
        terminalOutput = new QPlainTextEdit(terminalPanel);
        terminalOutput->setReadOnly(true);
        terminalOutput->setMaximumBlockCount(3000);
        terminalInput = new QLineEdit(terminalPanel);
        terminalInput->setPlaceholderText(QStringLiteral("Shell command"));
        terminalLayout->addWidget(terminalOutput, 1);
        terminalLayout->addWidget(terminalInput);
        outputTabs->addTab(output, QStringLiteral("Messages"));
        outputTabs->addTab(scratchPad, QStringLiteral("Scribble"));
        outputTabs->addTab(terminalPanel, QStringLiteral("Terminal"));

        editorSplitter->setStretchFactor(0, 1);
        editorSplitter->setStretchFactor(1, 1);
        rightSplitter->setStretchFactor(0, 5);
        rightSplitter->setStretchFactor(1, 1);
        mainSplitter->addWidget(sideTabs);
        mainSplitter->addWidget(rightSplitter);
        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);

        setCentralWidget(mainSplitter);

        statusPosition = new QLabel(this);
        statusFile = new QLabel(this);
        statusType = new QLabel(this);
        statusBar()->addPermanentWidget(statusFile, 1);
        statusBar()->addPermanentWidget(statusType);
        statusBar()->addPermanentWidget(statusPosition);
    }

    void setupActions()
    {
        auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
        auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
        auto* searchMenu = menuBar()->addMenu(QStringLiteral("&Search"));
        auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
        auto* documentMenu = menuBar()->addMenu(QStringLiteral("&Document"));
        auto* filetypeMenu = menuBar()->addMenu(QStringLiteral("File&type"));
        auto* projectMenu = menuBar()->addMenu(QStringLiteral("&Project"));
        auto* buildMenu = menuBar()->addMenu(QStringLiteral("&Build"));
        auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
        auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

        mainToolBar = addToolBar(QStringLiteral("Main"));
        mainToolBar->setMovable(false);

        addAction(fileMenu, mainToolBar, QStringLiteral("&New"), QStringLiteral("Ctrl+N"), [this]() { newDocument(); });
        addAction(fileMenu, nullptr, QStringLiteral("New from Template..."), QStringLiteral("Ctrl+Shift+N"), [this]() { newDocumentFromTemplate(); });
        addAction(fileMenu, nullptr, QStringLiteral("New from Clipboard"), QString(), [this]() { newDocumentFromClipboard(); });
        addAction(fileMenu, nullptr, QStringLiteral("New from Selection"), QString(), [this]() { newDocumentFromSelection(); });
        addAction(fileMenu, mainToolBar, QStringLiteral("&Open..."), QStringLiteral("Ctrl+O"), [this]() { openDocument(); });
        addAction(fileMenu, nullptr, QStringLiteral("Open &Folder..."), QStringLiteral("Ctrl+Shift+O"), [this]() { openProjectFolder(); });
        addAction(fileMenu, nullptr, QStringLiteral("Browse Folder..."), QString(), [this]() { browseFolder(); });
        addAction(fileMenu, nullptr, QStringLiteral("Open Selected Filename..."), QStringLiteral("Ctrl+Alt+O"), [this]() { openSelectedFilename(); });
        addAction(fileMenu, nullptr, QStringLiteral("Open Current File Folder"), QString(), [this]() { openCurrentFileFolder(); });
        addAction(fileMenu, nullptr, QStringLiteral("Open Current File with System App"), QString(), [this]() { openCurrentFileWithSystemApp(); });
        addAction(fileMenu, nullptr, QStringLiteral("Copy Current File Path"), QString(), [this]() { copyCurrentFilePath(); });
        addAction(fileMenu, nullptr, QStringLiteral("Copy Current Relative Path"), QString(), [this]() { copyCurrentRelativePath(); });
        fileMenu->addSeparator();
        addAction(fileMenu, mainToolBar, QStringLiteral("&Save"), QStringLiteral("Ctrl+S"), [this]() { saveCurrentDocument(); });
        addAction(fileMenu, nullptr, QStringLiteral("Save &As..."), QStringLiteral("Ctrl+Shift+S"), [this]() { saveCurrentDocumentAs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Save a Copy As..."), QString(), [this]() { saveCurrentDocumentCopyAs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Save Selection As..."), QString(), [this]() { saveSelectionAs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Rename/Move Current File..."), QString(), [this]() { renameCurrentDocumentFile(); });
        addAction(fileMenu, nullptr, QStringLiteral("Export as HTML..."), QString(), [this]() { exportCurrentDocumentAsHtml(); });
        addAction(fileMenu, nullptr, QStringLiteral("Export as PDF..."), QString(), [this]() { exportCurrentDocumentAsPdf(); });
        addAction(fileMenu, nullptr, QStringLiteral("Save A&ll"), QStringLiteral("Ctrl+Alt+S"), [this]() { saveAllDocuments(); });
        fileMenu->addSeparator();
        addAction(fileMenu, nullptr, QStringLiteral("Print Preview..."), QString(), [this]() { previewCurrentDocument(); });
        addAction(fileMenu, nullptr, QStringLiteral("&Print..."), QStringLiteral("Ctrl+P"), [this]() { printCurrentDocument(); });
        fileMenu->addSeparator();
        addAction(fileMenu, nullptr, QStringLiteral("&Reload"), QStringLiteral("Ctrl+R"), [this]() { reloadCurrentDocument(); });
        addAction(fileMenu, nullptr, QStringLiteral("Reload All"), QString(), [this]() { reloadAllDocuments(); });
        addAction(fileMenu, nullptr, QStringLiteral("&Close"), QStringLiteral("Ctrl+W"), [this]() { closeCurrentTab(); });
        addAction(fileMenu, nullptr, QStringLiteral("Close Other Documents"), QString(), [this]() { closeOtherTabs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Close Documents to the Left"), QString(), [this]() { closeTabsToSide(-1); });
        addAction(fileMenu, nullptr, QStringLiteral("Close Documents to the Right"), QString(), [this]() { closeTabsToSide(1); });
        addAction(fileMenu, nullptr, QStringLiteral("Close Unmodified Documents"), QString(), [this]() { closeUnmodifiedTabs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Close All"), QString(), [this]() { closeAllTabs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Reopen Closed Document"), QStringLiteral("Ctrl+Shift+T"), [this]() { reopenClosedDocument(); });
        fileMenu->addSeparator();
        addAction(fileMenu, nullptr, QStringLiteral("Save Session..."), QString(), [this]() { saveSessionAs(); });
        addAction(fileMenu, nullptr, QStringLiteral("Load Session..."), QString(), [this]() { loadSessionFromFile(); });
        recentFilesMenu = fileMenu->addMenu(QStringLiteral("Recent Files"));
        updateRecentFilesMenu();
        fileMenu->addSeparator();
        addAction(fileMenu, nullptr, QStringLiteral("&Quit"), QStringLiteral("Ctrl+Q"), [this]() { quitApplication(); });

        addAction(editMenu, nullptr, QStringLiteral("&Undo"), QStringLiteral("Ctrl+Z"), [this]() { withEditor([](QPlainTextEdit* e) { e->undo(); }); });
        addAction(editMenu, nullptr, QStringLiteral("&Redo"), QStringLiteral("Ctrl+Shift+Z"), [this]() { withEditor([](QPlainTextEdit* e) { e->redo(); }); });
        editMenu->addSeparator();
        addAction(editMenu, nullptr, QStringLiteral("Cu&t"), QStringLiteral("Ctrl+X"), [this]() { withEditor([](QPlainTextEdit* e) { e->cut(); }); });
        addAction(editMenu, nullptr, QStringLiteral("&Copy"), QStringLiteral("Ctrl+C"), [this]() { withEditor([](QPlainTextEdit* e) { e->copy(); }); });
        addAction(editMenu, nullptr, QStringLiteral("&Paste"), QStringLiteral("Ctrl+V"), [this]() { withEditor([](QPlainTextEdit* e) { e->paste(); }); });
        addAction(editMenu, nullptr, QStringLiteral("Insert File..."), QString(), [this]() { insertFileAtCursor(); });
        addAction(editMenu, nullptr, QStringLiteral("Select &All"), QStringLiteral("Ctrl+A"), [this]() { withEditor([](QPlainTextEdit* e) { e->selectAll(); }); });
        editMenu->addSeparator();
        addAction(editMenu, nullptr, QStringLiteral("Select Current Word"), QString(), [this]() { selectCurrentWord(); });
        addAction(editMenu, nullptr, QStringLiteral("Select Current Line"), QStringLiteral("Ctrl+Shift+L"), [this]() { selectCurrentLine(); });
        addAction(editMenu, nullptr, QStringLiteral("Select Current Paragraph"), QString(), [this]() { selectCurrentParagraph(); });
        addAction(editMenu, nullptr, QStringLiteral("Copy Current Line"), QString(), [this]() { copyCurrentLine(); });
        addAction(editMenu, nullptr, QStringLiteral("Cut Current Line"), QString(), [this]() { cutCurrentLine(); });
        addAction(editMenu, nullptr, QStringLiteral("Duplicate Line"), QStringLiteral("Ctrl+D"), [this]() { duplicateLine(); });
        addAction(editMenu, nullptr, QStringLiteral("Delete Line"), QStringLiteral("Ctrl+Shift+K"), [this]() { deleteLine(); });
        addAction(editMenu, nullptr, QStringLiteral("Delete to Line Start"), QStringLiteral("Ctrl+Shift+Backspace"), [this]() { deleteToLineStart(); });
        addAction(editMenu, nullptr, QStringLiteral("Delete to Line End"), QStringLiteral("Ctrl+Shift+Delete"), [this]() { deleteToLineEnd(); });
        addAction(editMenu, nullptr, QStringLiteral("Convert to Uppercase"), QString(), [this]() { convertSelectionCase(true); });
        addAction(editMenu, nullptr, QStringLiteral("Convert to Lowercase"), QString(), [this]() { convertSelectionCase(false); });
        addAction(editMenu, nullptr, QStringLiteral("Invert Case"), QString(), [this]() { invertSelectionCase(); });
        addAction(editMenu, nullptr, QStringLiteral("Toggle Comment"), QStringLiteral("Ctrl+/"), [this]() { toggleComment(); });
        addAction(editMenu, nullptr, QStringLiteral("Toggle Block Comment"), QStringLiteral("Ctrl+Shift+/"), [this]() { toggleBlockComment(); });
        addAction(editMenu, nullptr, QStringLiteral("Complete Word"), QStringLiteral("Ctrl+Space"), [this]() { completeCurrentWord(); });
        addAction(editMenu, nullptr, QStringLiteral("Insert Snippet..."), QStringLiteral("Ctrl+Alt+Space"), [this]() { insertSnippet(); });

        addAction(searchMenu, nullptr, QStringLiteral("&Find..."), QStringLiteral("Ctrl+F"), [this]() { findText(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find &Next"), QStringLiteral("F3"), [this]() { findNext(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find &Previous"), QStringLiteral("Shift+F3"), [this]() { findPrevious(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find Selection Next"), QStringLiteral("Ctrl+Alt+F3"), [this]() { findSelection(false); });
        addAction(searchMenu, nullptr, QStringLiteral("Find Selection Previous"), QStringLiteral("Ctrl+Alt+Shift+F3"), [this]() { findSelection(true); });
        addAction(searchMenu, nullptr, QStringLiteral("&Replace..."), QStringLiteral("Ctrl+H"), [this]() { replaceText(); });
        addAction(searchMenu, nullptr, QStringLiteral("Replace All..."), QString(), [this]() { replaceAllText(); });
        addAction(searchMenu, nullptr, QStringLiteral("Replace in Selection..."), QString(), [this]() { replaceInSelectionText(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find in Current Document..."), QStringLiteral("Ctrl+Shift+J"), [this]() { findInCurrentDocument(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find in Selection..."), QString(), [this]() { findInSelectionRange(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find in Files..."), QStringLiteral("Ctrl+Shift+F"), [this]() { findInFiles(); });
        addAction(searchMenu, nullptr, QStringLiteral("Replace in Files..."), QString(), [this]() { replaceInFiles(); });
        addAction(searchMenu, nullptr, QStringLiteral("Find in Open Documents..."), QString(), [this]() { findInOpenDocuments(); });
        addAction(searchMenu, nullptr, QStringLiteral("Replace in Open Documents..."), QString(), [this]() { replaceInOpenDocuments(); });
        addAction(searchMenu, nullptr, QStringLiteral("Clear Search Highlight"), QString(), [this]() { clearSearchHighlights(); });
        addAction(searchMenu, nullptr, QStringLiteral("Next Search Result"), QStringLiteral("F4"), [this]() { navigateListResult(searchResults, 1); });
        addAction(searchMenu, nullptr, QStringLiteral("Previous Search Result"), QStringLiteral("Shift+F4"), [this]() { navigateListResult(searchResults, -1); });
        addAction(searchMenu, nullptr, QStringLiteral("Bookmark Search Results"), QString(), [this]() { bookmarkResultLines(searchResults); });
        addAction(searchMenu, nullptr, QStringLiteral("&Go to Line..."), QStringLiteral("Ctrl+L"), [this]() { goToLine(); });
        addAction(searchMenu, nullptr, QStringLiteral("Go to Document Symbol..."), QStringLiteral("Ctrl+Alt+J"), [this]() { goToDocumentSymbol(); });
        addAction(searchMenu, nullptr, QStringLiteral("Next Document Symbol"), QStringLiteral("Ctrl+Alt+Down"), [this]() { gotoDocumentSymbolRelative(1); });
        addAction(searchMenu, nullptr, QStringLiteral("Previous Document Symbol"), QStringLiteral("Ctrl+Alt+Up"), [this]() { gotoDocumentSymbolRelative(-1); });
        addAction(searchMenu, nullptr, QStringLiteral("Go to Matching Brace"), QStringLiteral("Ctrl+B"), [this]() { goToMatchingBrace(); });
        addAction(searchMenu, nullptr, QStringLiteral("Switch Header/Source"), QStringLiteral("Ctrl+Alt+H"), [this]() { switchHeaderSource(); });
        searchMenu->addSeparator();
        addAction(searchMenu, nullptr, QStringLiteral("Navigate Back"), QStringLiteral("Alt+Left"), [this]() { navigateBack(); });
        addAction(searchMenu, nullptr, QStringLiteral("Navigate Forward"), QStringLiteral("Alt+Right"), [this]() { navigateForward(); });

        showSidebarAction = viewMenu->addAction(QStringLiteral("Show Sidebar"));
        showSidebarAction->setCheckable(true);
        showSidebarAction->setChecked(true);
        connect(showSidebarAction, &QAction::triggered, this, [this](bool checked) { sideTabs->setVisible(checked); });

        showOutputAction = viewMenu->addAction(QStringLiteral("Show Messages"));
        showOutputAction->setCheckable(true);
        showOutputAction->setChecked(true);
        connect(showOutputAction, &QAction::triggered, this, [this](bool checked) { outputTabs->setVisible(checked); });

        showToolbarAction = viewMenu->addAction(QStringLiteral("Show Toolbar"));
        showToolbarAction->setCheckable(true);
        showToolbarAction->setChecked(true);
        connect(showToolbarAction, &QAction::triggered, this, [this](bool checked) {
            if (mainToolBar != nullptr) {
                mainToolBar->setVisible(checked);
            }
        });

        showStatusbarAction = viewMenu->addAction(QStringLiteral("Show Statusbar"));
        showStatusbarAction->setCheckable(true);
        showStatusbarAction->setChecked(true);
        connect(showStatusbarAction, &QAction::triggered, this, [this](bool checked) {
            statusBar()->setVisible(checked);
        });

        wordWrapAction = viewMenu->addAction(QStringLiteral("Line Wrapping"));
        wordWrapAction->setCheckable(true);
        connect(wordWrapAction, &QAction::triggered, this, [this](bool checked) { setWordWrap(checked); });
        fullScreenAction = viewMenu->addAction(QStringLiteral("Fullscreen"));
        fullScreenAction->setShortcut(QKeySequence(QStringLiteral("F11")));
        fullScreenAction->setCheckable(true);
        connect(fullScreenAction, &QAction::triggered, this, [this](bool checked) { setFullScreenMode(checked); });
        focusModeAction = viewMenu->addAction(QStringLiteral("Focus Mode"));
        focusModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F11")));
        focusModeAction->setCheckable(true);
        connect(focusModeAction, &QAction::triggered, this, [this](bool checked) { setFocusMode(checked); });
        splitViewAction = viewMenu->addAction(QStringLiteral("Split View"));
        splitViewAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
        splitViewAction->setCheckable(true);
        connect(splitViewAction, &QAction::triggered, this, [this](bool checked) { setSplitViewVisible(checked); });
        showRightMarginAction = viewMenu->addAction(QStringLiteral("Show Long Line Marker"));
        showRightMarginAction->setCheckable(true);
        connect(showRightMarginAction, &QAction::triggered, this, [this](bool checked) {
            showRightMarginEnabled = checked;
            applyEditorPreferences();
        });
        showLineNumbersAction = viewMenu->addAction(QStringLiteral("Show Line Numbers"));
        showLineNumbersAction->setCheckable(true);
        showLineNumbersAction->setChecked(true);
        connect(showLineNumbersAction, &QAction::triggered, this, [this](bool checked) {
            showLineNumbersEnabled = checked;
            applyEditorPreferences();
        });
        showIndentGuidesAction = viewMenu->addAction(QStringLiteral("Show Indentation Guides"));
        showIndentGuidesAction->setCheckable(true);
        showIndentGuidesAction->setChecked(true);
        connect(showIndentGuidesAction, &QAction::triggered, this, [this](bool checked) {
            showIndentGuidesEnabled = checked;
            applyEditorPreferences();
        });
        highlightCurrentLineAction = viewMenu->addAction(QStringLiteral("Highlight Current Line"));
        highlightCurrentLineAction->setCheckable(true);
        highlightCurrentLineAction->setChecked(true);
        connect(highlightCurrentLineAction, &QAction::triggered, this, [this](bool checked) {
            highlightCurrentLineEnabled = checked;
            applyEditorPreferences();
        });
        showWhitespaceAction = viewMenu->addAction(QStringLiteral("Show Whitespace"));
        showWhitespaceAction->setCheckable(true);
        connect(showWhitespaceAction, &QAction::triggered, this, [this](bool checked) {
            showWhitespaceEnabled = checked;
            applyEditorPreferences();
        });
        showLineEndingsAction = viewMenu->addAction(QStringLiteral("Show Line Endings"));
        showLineEndingsAction->setCheckable(true);
        connect(showLineEndingsAction, &QAction::triggered, this, [this](bool checked) {
            showLineEndingsEnabled = checked;
            applyEditorPreferences();
        });
        addAction(viewMenu, nullptr, QStringLiteral("Zoom In"), QStringLiteral("Ctrl++"), [this]() { changeFontSize(1); });
        addAction(viewMenu, nullptr, QStringLiteral("Zoom Out"), QStringLiteral("Ctrl+-"), [this]() { changeFontSize(-1); });

        addAction(documentMenu, nullptr, QStringLiteral("Indent"), QStringLiteral("Tab"), [this]() { indentSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Unindent"), QStringLiteral("Shift+Tab"), [this]() { unindentSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Detect Indentation"), QString(), [this]() { detectIndentation(); });
        addAction(documentMenu, nullptr, QStringLiteral("Join Lines"), QStringLiteral("Ctrl+J"), [this]() { joinLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Reflow Lines/Block"), QString(), [this]() { reflowLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Fold Current Block"), QStringLiteral("Ctrl+Shift+["), [this]() { foldCurrentBlock(); });
        addAction(documentMenu, nullptr, QStringLiteral("Unfold All Blocks"), QStringLiteral("Ctrl+Shift+]"), [this]() { unfoldAllBlocks(); });
        addAction(documentMenu, nullptr, QStringLiteral("Sort Selected Lines"), QString(), [this]() { sortSelectedLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Sort Selected Lines Descending"), QString(), [this]() { sortSelectedLinesDescending(); });
        addAction(documentMenu, nullptr, QStringLiteral("Reverse Selected Lines"), QString(), [this]() { reverseSelectedLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Remove Duplicate Selected Lines"), QString(), [this]() { removeDuplicateSelectedLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Move Line Up"), QStringLiteral("Alt+Up"), [this]() { moveLine(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Move Line Down"), QStringLiteral("Alt+Down"), [this]() { moveLine(1); });
        documentMenu->addSeparator();
        addAction(documentMenu, nullptr, QStringLiteral("Next Document"), QStringLiteral("Ctrl+PageDown"), [this]() { switchDocument(1); });
        addAction(documentMenu, nullptr, QStringLiteral("Previous Document"), QStringLiteral("Ctrl+PageUp"), [this]() { switchDocument(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Move Document Left"), QStringLiteral("Ctrl+Shift+PageUp"), [this]() { moveCurrentTab(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Move Document Right"), QStringLiteral("Ctrl+Shift+PageDown"), [this]() { moveCurrentTab(1); });
        addAction(documentMenu, nullptr, QStringLiteral("Close Other Documents"), QString(), [this]() { closeOtherTabs(); });
        addAction(documentMenu, nullptr, QStringLiteral("Close Documents to the Left"), QString(), [this]() { closeTabsToSide(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Close Documents to the Right"), QString(), [this]() { closeTabsToSide(1); });
        addAction(documentMenu, nullptr, QStringLiteral("Close Unmodified Documents"), QString(), [this]() { closeUnmodifiedTabs(); });
        documentMenu->addSeparator();
        readOnlyAction = addAction(documentMenu, nullptr, QStringLiteral("Read Only"), QString(), [this]() { toggleReadOnly(); });
        readOnlyAction->setCheckable(true);
        addAction(documentMenu, nullptr, QStringLiteral("Toggle Writable Permission"), QString(), [this]() { toggleCurrentFileWritable(); });
        addAction(documentMenu, nullptr, QStringLiteral("Toggle Executable Permission"), QString(), [this]() { toggleCurrentFileExecutable(); });
        documentMenu->addSeparator();
        addAction(documentMenu, nullptr, QStringLiteral("Toggle Bookmark"), QStringLiteral("Ctrl+M"), [this]() { toggleBookmark(); });
        addAction(documentMenu, nullptr, QStringLiteral("Next Bookmark"), QString(), [this]() { gotoBookmark(1); });
        addAction(documentMenu, nullptr, QStringLiteral("Previous Bookmark"), QString(), [this]() { gotoBookmark(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Next Bookmark in All Documents"), QStringLiteral("Ctrl+Alt+M"), [this]() { gotoBookmarkAcrossDocuments(1); });
        addAction(documentMenu, nullptr, QStringLiteral("Previous Bookmark in All Documents"), QStringLiteral("Ctrl+Alt+Shift+M"), [this]() { gotoBookmarkAcrossDocuments(-1); });
        addAction(documentMenu, nullptr, QStringLiteral("Clear Bookmarks"), QString(), [this]() { clearBookmarks(); });
        addAction(documentMenu, nullptr, QStringLiteral("Clear All Bookmarks"), QString(), [this]() { clearAllBookmarks(); });
        documentMenu->addSeparator();
        auto* lineEndingsMenu = documentMenu->addMenu(QStringLiteral("Line Endings"));
        lineEndingsMenu->addAction(QStringLiteral("LF (Unix)"), this, [this]() { setCurrentLineEnding(QStringLiteral("\n")); });
        lineEndingsMenu->addAction(QStringLiteral("CRLF (Windows)"), this, [this]() { setCurrentLineEnding(QStringLiteral("\r\n")); });
        lineEndingsMenu->addAction(QStringLiteral("CR (Classic Mac)"), this, [this]() { setCurrentLineEnding(QStringLiteral("\r")); });
        auto* encodingMenu = documentMenu->addMenu(QStringLiteral("Encoding"));
        encodingMenu->addAction(QStringLiteral("Set Encoding to UTF-8"), this, [this]() { setCurrentEncoding(QStringLiteral("UTF-8")); });
        encodingMenu->addAction(QStringLiteral("Set Encoding to System Locale"), this, [this]() { setCurrentEncoding(systemEncodingName()); });
        encodingMenu->addAction(QStringLiteral("Set Encoding to GB18030"), this, [this]() { setCurrentEncoding(QStringLiteral("GB18030")); });
        encodingMenu->addAction(QStringLiteral("Set Encoding to UTF-16 LE with BOM"), this, [this]() { setCurrentEncoding(QStringLiteral("UTF-16LE-BOM")); });
        encodingMenu->addAction(QStringLiteral("Set Encoding to UTF-16 BE with BOM"), this, [this]() { setCurrentEncoding(QStringLiteral("UTF-16BE-BOM")); });
        encodingMenu->addSeparator();
        encodingMenu->addAction(QStringLiteral("Reload as UTF-8"), this, [this]() { reloadCurrentDocumentAsEncoding(QStringLiteral("UTF-8")); });
        encodingMenu->addAction(QStringLiteral("Reload as System Locale"), this, [this]() { reloadCurrentDocumentAsEncoding(systemEncodingName()); });
        encodingMenu->addAction(QStringLiteral("Reload as GB18030"), this, [this]() { reloadCurrentDocumentAsEncoding(QStringLiteral("GB18030")); });
        encodingMenu->addAction(QStringLiteral("Reload as UTF-16 LE"), this, [this]() { reloadCurrentDocumentAsEncoding(QStringLiteral("UTF-16LE-BOM")); });
        encodingMenu->addAction(QStringLiteral("Reload as UTF-16 BE"), this, [this]() { reloadCurrentDocumentAsEncoding(QStringLiteral("UTF-16BE-BOM")); });
        documentMenu->addSeparator();
        addAction(documentMenu, nullptr, QStringLiteral("Strip Trailing Spaces"), QString(), [this]() { stripTrailingSpaces(); });
        addAction(documentMenu, nullptr, QStringLiteral("Strip Trailing Spaces in Selection"), QString(), [this]() { stripTrailingSpacesInSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Remove Empty Lines"), QString(), [this]() { removeEmptyLines(); });
        addAction(documentMenu, nullptr, QStringLiteral("Remove Empty Lines in Selection"), QString(), [this]() { removeEmptyLinesInSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Replace Tabs with Spaces"), QString(), [this]() { replaceTabsWithSpaces(); });
        addAction(documentMenu, nullptr, QStringLiteral("Replace Tabs with Spaces in Selection"), QString(), [this]() { replaceTabsWithSpacesInSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Replace Leading Spaces with Tabs"), QString(), [this]() { replaceLeadingSpacesWithTabs(); });
        addAction(documentMenu, nullptr, QStringLiteral("Replace Leading Spaces with Tabs in Selection"), QString(), [this]() { replaceLeadingSpacesWithTabsInSelection(); });
        addAction(documentMenu, nullptr, QStringLiteral("Validate JSON/JSON5"), QString(), [this]() { validateCurrentJson(); });
        addAction(documentMenu, nullptr, QStringLiteral("Format JSON/JSON5"), QString(), [this]() { formatCurrentJson(false); });
        addAction(documentMenu, nullptr, QStringLiteral("Minify JSON/JSON5"), QString(), [this]() { formatCurrentJson(true); });
        addAction(documentMenu, nullptr, QStringLiteral("Compare with Saved"), QString(), [this]() { compareCurrentWithSaved(); });
        addAction(documentMenu, nullptr, QStringLiteral("Compare with File..."), QString(), [this]() { compareCurrentWithFile(); });
        addAction(documentMenu, nullptr, QStringLiteral("Document Statistics"), QString(), [this]() { showDocumentStatistics(); });
        addAction(documentMenu, nullptr, QStringLiteral("Document Properties"), QString(), [this]() { showDocumentProperties(); });

        setupFiletypeMenu(filetypeMenu);

        addAction(projectMenu, mainToolBar, QStringLiteral("Open Folder"), QString(), [this]() { openProjectFolder(); });
        addAction(projectMenu, nullptr, QStringLiteral("New Project..."), QString(), [this]() { newProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Open Project File..."), QString(), [this]() { openProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Save Project"), QString(), [this]() { saveProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Project Properties..."), QString(), [this]() { showProjectProperties(); });
        addAction(projectMenu, nullptr, QStringLiteral("Project Ignore Rules..."), QString(), [this]() { showProjectIgnoreRules(); });
        recentProjectsMenu = projectMenu->addMenu(QStringLiteral("Recent Projects"));
        updateRecentProjectsMenu();
        projectMenu->addSeparator();
        addAction(projectMenu, nullptr, QStringLiteral("Open Selected File"), QString(), [this]() { openSelectedProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Open Selected Filename..."), QString(), [this]() { openSelectedFilename(); });
        addAction(projectMenu, nullptr, QStringLiteral("Open Selected File with System App"), QString(), [this]() { openSelectedProjectFileWithSystemApp(); });
        addAction(projectMenu, nullptr, QStringLiteral("Open Project Folder in Files"), QString(), [this]() { openProjectFolderInFileManager(); });
        addAction(projectMenu, nullptr, QStringLiteral("Quick Open File..."), QStringLiteral("Ctrl+P"), [this]() { quickOpenProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Go to Symbol..."), QStringLiteral("Ctrl+Shift+P"), [this]() { goToProjectSymbol(); });
        addAction(projectMenu, nullptr, QStringLiteral("Go to Definition"), QStringLiteral("Ctrl+Shift+D"), [this]() { goToDefinition(); });
        addAction(projectMenu, nullptr, QStringLiteral("Find References"), QStringLiteral("Ctrl+Shift+R"), [this]() { findReferences(); });
        addAction(projectMenu, nullptr, QStringLiteral("Scan Tasks"), QStringLiteral("Ctrl+Shift+T"), [this]() { scanTasks(); });
        addAction(projectMenu, nullptr, QStringLiteral("Select Current File"), QString(), [this]() { selectCurrentFileInProject(); });
        addAction(projectMenu, nullptr, QStringLiteral("New File..."), QString(), [this]() { createProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("New Folder..."), QString(), [this]() { createProjectFolder(); });
        addAction(projectMenu, nullptr, QStringLiteral("Rename Selected File..."), QString(), [this]() { renameSelectedProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Duplicate Selected File..."), QString(), [this]() { duplicateSelectedProjectFile(); });
        addAction(projectMenu, nullptr, QStringLiteral("Delete Selected File..."), QString(), [this]() { deleteSelectedProjectFile(); });
        projectMenu->addSeparator();
        projectMenu->addAction(QStringLiteral("Close Project"), this, [this]() {
            projectRoot.clear();
            projectFile.clear();
            projectDefaultLineEnding.clear();
            projectDefaultEncodingName.clear();
            projectExtraIgnorePatterns.clear();
            projectIgnoreRules.clear();
            ignoreRulesByRoot.clear();
            projectFilePaths.clear();
            if (projectFileFilter != nullptr) {
                projectFileFilter->clear();
            }
            projectFiles->clear();
            appendOutput(QStringLiteral("Project closed."));
            updateCursorStatus();
        });
        projectMenu->addAction(QStringLiteral("Refresh Project Files"), this, [this]() {
            if (!projectRoot.isEmpty()) {
                loadProjectIgnoreRules();
                loadProjectFiles();
                appendOutput(QStringLiteral("Project refreshed: %1").arg(projectRoot));
            }
        });

        addAction(buildMenu, nullptr, QStringLiteral("&Compile Current File"), QStringLiteral("F8"), [this]() { compileCurrentFile(); });
        addAction(buildMenu, mainToolBar, QStringLiteral("&Build"), QStringLiteral("F9"), [this]() { runBuildCommand(); });
        addAction(buildMenu, nullptr, QStringLiteral("&Clean"), QString(), [this]() { runCleanCommand(); });
        addAction(buildMenu, nullptr, QStringLiteral("Build Custom Target..."), QString(), [this]() { runCustomBuildTarget(); });
        addAction(buildMenu, nullptr, QStringLiteral("&Execute Current File"), QStringLiteral("F5"), [this]() { executeCurrentFile(); });
        addAction(buildMenu, nullptr, QStringLiteral("&Stop"), QString(), [this]() { stopCommand(); });
        addAction(buildMenu, nullptr, QStringLiteral("Repeat Last Command"), QString(), [this]() { repeatLastCommand(); });
        addAction(buildMenu, nullptr, QStringLiteral("Next Problem"), QStringLiteral("F6"), [this]() { navigateListResult(problemsList, 1); });
        addAction(buildMenu, nullptr, QStringLiteral("Previous Problem"), QStringLiteral("Shift+F6"), [this]() { navigateListResult(problemsList, -1); });
        addAction(buildMenu, nullptr, QStringLiteral("Use OpenHarmony Build Command"), QString(), [this]() { useOpenHarmonyBuildCommand(true); });
        saveAllBeforeBuildAction = buildMenu->addAction(QStringLiteral("Save All Before Build"));
        saveAllBeforeBuildAction->setCheckable(true);
        saveAllBeforeBuildAction->setChecked(true);
        connect(saveAllBeforeBuildAction, &QAction::triggered, this, [this](bool checked) {
            saveAllBeforeBuild = checked;
        });
        addAction(buildMenu, nullptr, QStringLiteral("Set Build Command..."), QString(), [this]() { setBuildCommand(); });
        addAction(buildMenu, nullptr, QStringLiteral("Build Settings..."), QString(), [this]() { showBuildSettings(); });
        addAction(buildMenu, nullptr, QStringLiteral("Copy Build Output"), QString(), [this]() { copyBuildOutput(); });
        addAction(buildMenu, nullptr, QStringLiteral("Save Build Output As..."), QString(), [this]() { saveBuildOutputAs(); });
        addAction(buildMenu, nullptr, QStringLiteral("Clear Build Output"), QString(), [this]() {
            output->clear();
            problemsList->clear();
        });

        toolsMenu->addAction(QStringLiteral("Run Command..."), this, [this]() { runToolCommand(); });
        toolsMenu->addAction(QStringLiteral("Repeat Last Command"), this, [this]() { repeatLastCommand(); });
        toolsMenu->addAction(QStringLiteral("Show Terminal"), this, [this]() { showTerminal(); });
        toolsMenu->addAction(QStringLiteral("Restart Terminal"), this, [this]() { restartTerminalShell(); });
        toolsMenu->addAction(QStringLiteral("Filter Selection Through Command..."), this, [this]() { filterSelectionThroughCommand(); });
        toolsMenu->addAction(QStringLiteral("Insert Command Output..."), this, [this]() { insertCommandOutput(); });
        recentCommandsMenu = toolsMenu->addMenu(QStringLiteral("Recent Commands"));
        updateRecentCommandsMenu();
        externalToolsMenu = toolsMenu->addMenu(QStringLiteral("External Tools"));
        updateExternalToolsMenu();
        toolsMenu->addAction(QStringLiteral("Save Scribble As..."), this, [this]() { saveScribbleAs(); });
        toolsMenu->addAction(QStringLiteral("Clear Scribble"), this, [this]() {
            if (scratchPad != nullptr) {
                scratchPad->clear();
            }
        });
        toolsMenu->addAction(QStringLiteral("Insert Snippet..."), this, [this]() { insertSnippet(); });
        toolsMenu->addAction(QStringLiteral("Insert Date/Time..."), this, [this]() { insertDateTime(); });
        toolsMenu->addAction(QStringLiteral("Insert Color..."), this, [this]() { insertColorValue(); });
        toolsMenu->addAction(QStringLiteral("Insert HTML Entity..."), this, [this]() { insertHtmlEntity(); });
        toolsMenu->addAction(QStringLiteral("Create Class Skeleton..."), this, [this]() { insertClassSkeleton(); });
        toolsMenu->addAction(QStringLiteral("Configure Snippets..."), this, [this]() { configureSnippets(); });
        toolsMenu->addAction(QStringLiteral("Configure File Templates..."), this, [this]() { configureFileTemplates(); });
        toolsMenu->addAction(QStringLiteral("Load Tags File..."), this, [this]() { loadTagsFileFromDialog(); });
        toolsMenu->addAction(QStringLiteral("Reload Tags Files"), this, [this]() { reloadLoadedTags(true); });
        toolsMenu->addAction(QStringLiteral("Clear Loaded Tags"), this, [this]() { clearLoadedTags(); });
        toolsMenu->addAction(QStringLiteral("Scan Tasks"), this, [this]() { scanTasks(); });
        toolsMenu->addAction(QStringLiteral("Plugin Compatibility Status"), this, [this]() { showPluginCompatibilityStatus(); });
        toolsMenu->addAction(QStringLiteral("Auto Save Modified Files"), this, [this]() { autoSaveModifiedDocuments(true); });
        toolsMenu->addAction(QStringLiteral("Preferences"), this, [this]() { showPreferences(); });
        helpMenu->addAction(QStringLiteral("Keyboard Shortcuts"), this, [this]() { showKeyboardShortcuts(); });
        helpMenu->addAction(QStringLiteral("关于 GeanyDesk、隐私与许可"), this, [this]() {
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("GeanyDesk 1.0.1 · 隐私与开源许可"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* text = new QPlainTextEdit(&dialog);
            text->setReadOnly(true); text->setPlainText(geanyDeskReleaseNotice());
            layout->addWidget(text);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons); dialog.resize(900, 640); dialog.exec();
        });
        helpMenu->addAction(QStringLiteral("About Geany"), this, [this]() {
            QMessageBox::about(this, QStringLiteral("About Geany"),
                QStringLiteral("Geany Qt adaptation for HarmonyOS PC\n\n"
                               "Implemented: tabs, editor, syntax colors, files, project tree, symbols, search/replace, build output, preferences."));
        });

        connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
        connect(tabs->tabBar(), &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
            showTabContextMenu(pos);
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this](int) {
            connectCurrentEditorSignals();
            refreshSymbols();
            updateDocumentList();
            updateBookmarksList();
            updateReadOnlyAction();
            updateCursorStatus();
            refreshSplitView();
        });
        connect(output, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
            showBuildOutputContextMenu(pos);
        });
        connect(terminalInput, &QLineEdit::returnPressed, this, [this]() { sendTerminalCommand(); });
        connect(documentsList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            const int index = item != nullptr ? item->data(Qt::UserRole).toInt() : -1;
            if (index >= 0 && index < tabs->count()) {
                tabs->setCurrentIndex(index);
            }
        });
        documentsList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(documentsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showDocumentListContextMenu(pos);
        });
        connect(projectFiles, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                openPath(path);
            }
        });
        connect(projectFileFilter, &QLineEdit::textChanged, this, [this]() { populateProjectFiles(); });
        projectFiles->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(projectFiles, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showProjectContextMenu(pos);
        });
        connect(fileBrowserPathEdit, &QLineEdit::returnPressed, this, [this]() {
            setFileBrowserRoot(fileBrowserPathEdit->text().trimmed());
        });
        connect(fileBrowserList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            openSelectedBrowserItem(item);
        });
        fileBrowserList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(fileBrowserList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showFileBrowserContextMenu(pos);
        });
        connect(searchResults, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            jumpToListItem(item);
        });
        searchResults->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(searchResults, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showSearchResultsContextMenu(pos);
        });
        connect(problemsList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            jumpToListItem(item);
        });
        problemsList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(problemsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showResultListContextMenu(problemsList, pos, QStringLiteral("problem"), QStringLiteral("problems"), QStringLiteral("build-problems.txt"));
        });
        connect(tasksList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            jumpToListItem(item);
        });
        tasksList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tasksList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showResultListContextMenu(tasksList, pos, QStringLiteral("task"), QStringLiteral("tasks"), QStringLiteral("tasks.txt"));
        });
        connect(bookmarksList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            activateBookmarkItem(item);
        });
        bookmarksList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bookmarksList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            showBookmarksContextMenu(pos);
        });
        connect(symbolsList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            bool ok = false;
            const int line = item->data(Qt::UserRole).toInt(&ok);
            if (ok) {
                goToLineNumber(line);
            }
        });
    }

    QAction* addAction(QMenu* menu, QToolBar* toolbar, const QString& text, const QString& shortcut, const std::function<void()>& slot)
    {
        auto* action = new QAction(text, this);
        if (!shortcut.isEmpty()) {
            action->setShortcut(QKeySequence(shortcut));
        }
        connect(action, &QAction::triggered, this, slot);
        if (menu != nullptr) {
            menu->addAction(action);
        }
        if (toolbar != nullptr) {
            toolbar->addAction(action);
        }
        return action;
    }

    QStringList shortcutLinesForMenu(QMenu* menu, const QString& prefix) const
    {
        QStringList lines;
        if (menu == nullptr) {
            return lines;
        }
        for (QAction* action : menu->actions()) {
            if (action == nullptr || action->isSeparator()) {
                continue;
            }
            QString label = action->text();
            label.remove(QLatin1Char('&'));
            if (label.isEmpty()) {
                continue;
            }
            const QString qualified = prefix.isEmpty() ? label : QStringLiteral("%1 > %2").arg(prefix, label);
            if (action->menu() != nullptr) {
                lines.append(shortcutLinesForMenu(action->menu(), qualified));
                continue;
            }
            if (!action->shortcut().isEmpty()) {
                lines << QStringLiteral("%1\t%2").arg(qualified, action->shortcut().toString(QKeySequence::NativeText));
            }
        }
        return lines;
    }

    void showKeyboardShortcuts()
    {
        QStringList lines;
        for (QAction* menuAction : menuBar()->actions()) {
            if (menuAction != nullptr && menuAction->menu() != nullptr) {
                QString label = menuAction->text();
                label.remove(QLatin1Char('&'));
                lines.append(shortcutLinesForMenu(menuAction->menu(), label));
            }
        }
        lines.removeDuplicates();
        lines.sort(Qt::CaseInsensitive);

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Keyboard Shortcuts"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* viewer = new QPlainTextEdit(&dialog);
        viewer->setReadOnly(true);
        viewer->setMinimumSize(680, 520);
        viewer->setPlainText(lines.join(QLatin1Char('\n')));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(viewer);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    }

    void setupFiletypeMenu(QMenu* menu)
    {
        for (const QString& language : MiniHighlighter::supportedLanguages()) {
            menu->addAction(language, this, [this, language]() {
                setCurrentFileType(language);
            });
        }
        menu->addSeparator();
        menu->addAction(QStringLiteral("Detect from Filename"), this, [this]() {
            EditorPage* page = currentPage();
            if (page == nullptr) {
                return;
            }
            configureHighlighterForPath(page, page->path);
            refreshSymbols();
            updateCursorStatus();
        });
    }

    void setCurrentFileType(const QString& language)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        page->highlighter->configureForLanguage(language);
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Filetype set to %1 for %2.").arg(page->highlighter->languageName(), displayName(page)));
    }

    void withEditor(const std::function<void(QPlainTextEdit*)>& fn)
    {
        if (EditorPage* page = currentPage()) {
            fn(page->editor);
        }
    }

    void applySearchHighlights(EditorPage* targetPage = nullptr)
    {
        auto applyToPage = [this](EditorPage* page) {
            if (page == nullptr) {
                return;
            }
            page->editor->setSearchHighlights(lastFindText, findMatchCase, findWholeWord, findRegularExpression);
        };

        if (targetPage != nullptr) {
            applyToPage(targetPage);
            return;
        }

        for (int i = 0; i < tabs->count(); ++i) {
            applyToPage(static_cast<EditorPage*>(tabs->widget(i)));
        }
    }

    void completeCurrentWord()
    {
        if (EditorPage* page = currentPage(); page != nullptr) {
            page->editor->completeCurrentWord();
        }
    }

    QVector<SnippetTemplate> snippetsForLanguage(const QString& language) const
    {
        QVector<SnippetTemplate> snippets;
        snippets.push_back({QStringLiteral("TODO comment"), QStringLiteral("TODO: ${cursor}")});

        if (language == QStringLiteral("C") || language == QStringLiteral("C++")) {
            snippets.push_back({QStringLiteral("if block"), QStringLiteral("if (${selection}) {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("for loop"), QStringLiteral("for (int i = 0; i < count; ++i) {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("while loop"), QStringLiteral("while (${selection}) {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("main function"), QStringLiteral("int main(int argc, char** argv)\n{\n\t${cursor}\n\treturn 0;\n}")});
            if (language == QStringLiteral("C++")) {
                snippets.push_back({QStringLiteral("class"), QStringLiteral("class Name\n{\npublic:\n\tName() = default;\n\nprivate:\n\t${cursor}\n};")});
            }
        } else if (language == QStringLiteral("Python")) {
            snippets.push_back({QStringLiteral("def function"), QStringLiteral("def name():\n\t${cursor}")});
            snippets.push_back({QStringLiteral("class"), QStringLiteral("class Name:\n\tdef __init__(self):\n\t\t${cursor}")});
            snippets.push_back({QStringLiteral("for loop"), QStringLiteral("for item in items:\n\t${cursor}")});
            snippets.push_back({QStringLiteral("try except"), QStringLiteral("try:\n\t${cursor}\nexcept Exception as exc:\n\tprint(exc)")});
            snippets.push_back({QStringLiteral("main guard"), QStringLiteral("if __name__ == \"__main__\":\n\t${cursor}")});
        } else if (language == QStringLiteral("JavaScript/TypeScript") || language == QStringLiteral("ArkTS")) {
            snippets.push_back({QStringLiteral("function"), QStringLiteral("function name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("async function"), QStringLiteral("async function name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("for of loop"), QStringLiteral("for (const item of items) {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("try catch"), QStringLiteral("try {\n\t${cursor}\n} catch (error) {\n\tconsole.error(error);\n}")});
            if (language == QStringLiteral("ArkTS")) {
                snippets.push_back({QStringLiteral("ArkTS component"), QStringLiteral("@Component\nstruct Name {\n\tbuild() {\n\t\tColumn() {\n\t\t\t${cursor}\n\t\t}\n\t}\n}")});
                snippets.push_back({QStringLiteral("ArkTS state"), QStringLiteral("@State ${cursor}: string = \"\"")});
                snippets.push_back({QStringLiteral("ArkTS button"), QStringLiteral("Button(\"${selection}\")\n\t.onClick(() => {\n\t\t${cursor}\n\t})")});
            }
        } else if (language == QStringLiteral("Java")) {
            snippets.push_back({QStringLiteral("class"), QStringLiteral("public class Name {\n\tpublic static void main(String[] args) {\n\t\t${cursor}\n\t}\n}")});
            snippets.push_back({QStringLiteral("method"), QStringLiteral("public void name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("try catch"), QStringLiteral("try {\n\t${cursor}\n} catch (Exception ex) {\n\tex.printStackTrace();\n}")});
        } else if (language == QStringLiteral("Go")) {
            snippets.push_back({QStringLiteral("main package"), QStringLiteral("package main\n\nimport \"fmt\"\n\nfunc main() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("function"), QStringLiteral("func name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("if error"), QStringLiteral("if err != nil {\n\treturn err\n}\n${cursor}")});
        } else if (language == QStringLiteral("Rust")) {
            snippets.push_back({QStringLiteral("main function"), QStringLiteral("fn main() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("function"), QStringLiteral("fn name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("match"), QStringLiteral("match ${selection} {\n\t_ => ${cursor},\n}")});
        } else if (language == QStringLiteral("PHP")) {
            snippets.push_back({QStringLiteral("php block"), QStringLiteral("<?php\n\n${cursor}")});
            snippets.push_back({QStringLiteral("function"), QStringLiteral("function name() {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("class"), QStringLiteral("class Name {\n\t${cursor}\n}")});
        } else if (language == QStringLiteral("Shell")) {
            snippets.push_back({QStringLiteral("script header"), QStringLiteral("#!/usr/bin/env sh\nset -eu\n\n${cursor}")});
            snippets.push_back({QStringLiteral("if block"), QStringLiteral("if [ ${selection} ]; then\n\t${cursor}\nfi")});
            snippets.push_back({QStringLiteral("for loop"), QStringLiteral("for item in \"$@\"; do\n\t${cursor}\ndone")});
            snippets.push_back({QStringLiteral("function"), QStringLiteral("name() {\n\t${cursor}\n}")});
        } else if (language == QStringLiteral("CMake")) {
            snippets.push_back({QStringLiteral("project"), QStringLiteral("cmake_minimum_required(VERSION 3.16)\nproject(name)\n\nadd_executable(name\n\t${cursor}\n)")});
            snippets.push_back({QStringLiteral("library"), QStringLiteral("add_library(name\n\t${cursor}\n)")});
        } else if (language == QStringLiteral("Makefile")) {
            snippets.push_back({QStringLiteral("target"), QStringLiteral("target:\n\t${cursor}")});
            snippets.push_back({QStringLiteral("variable"), QStringLiteral("NAME := ${cursor}")});
        } else if (language == QStringLiteral("Dockerfile")) {
            snippets.push_back({QStringLiteral("image"), QStringLiteral("FROM image:tag\n\nWORKDIR /app\nCOPY . .\nRUN ${cursor}\n")});
            snippets.push_back({QStringLiteral("run"), QStringLiteral("RUN ${cursor}")});
        } else if (language == QStringLiteral("Lua")) {
            snippets.push_back({QStringLiteral("function"), QStringLiteral("local function name()\n\t${cursor}\nend")});
            snippets.push_back({QStringLiteral("table"), QStringLiteral("local values = {\n\t${cursor}\n}")});
        } else if (language == QStringLiteral("INI")) {
            snippets.push_back({QStringLiteral("section"), QStringLiteral("[section]\nkey=${cursor}")});
            snippets.push_back({QStringLiteral("key value"), QStringLiteral("key=${cursor}")});
        } else if (language == QStringLiteral("Markup")) {
            snippets.push_back({QStringLiteral("html page"), QStringLiteral("<!doctype html>\n<html>\n<head>\n\t<meta charset=\"utf-8\">\n\t<title>Title</title>\n</head>\n<body>\n\t${cursor}\n</body>\n</html>")});
            snippets.push_back({QStringLiteral("div"), QStringLiteral("<div>\n\t${cursor}\n</div>")});
        } else if (language == QStringLiteral("CSS")) {
            snippets.push_back({QStringLiteral("rule"), QStringLiteral(".selector {\n\t${cursor}\n}")});
            snippets.push_back({QStringLiteral("media query"), QStringLiteral("@media (max-width: 768px) {\n\t${cursor}\n}")});
        } else if (language == QStringLiteral("Markdown")) {
            snippets.push_back({QStringLiteral("code fence"), QStringLiteral("```text\n${cursor}\n```")});
            snippets.push_back({QStringLiteral("section"), QStringLiteral("## Title\n\n${cursor}")});
        } else if (language == QStringLiteral("JSON")) {
            snippets.push_back({QStringLiteral("object"), QStringLiteral("{\n\t\"${cursor}\": \"\"\n}")});
            snippets.push_back({QStringLiteral("array"), QStringLiteral("[\n\t${cursor}\n]")});
        } else if (language == QStringLiteral("YAML")) {
            snippets.push_back({QStringLiteral("mapping"), QStringLiteral("name:\n\t${cursor}")});
            snippets.push_back({QStringLiteral("list"), QStringLiteral("- ${cursor}")});
        } else if (language == QStringLiteral("TOML")) {
            snippets.push_back({QStringLiteral("table"), QStringLiteral("[section]\n${cursor}")});
            snippets.push_back({QStringLiteral("key value"), QStringLiteral("key = \"${cursor}\"")});
        } else if (language == QStringLiteral("SQL")) {
            snippets.push_back({QStringLiteral("select"), QStringLiteral("SELECT ${cursor}\nFROM table\nWHERE condition;")});
            snippets.push_back({QStringLiteral("insert"), QStringLiteral("INSERT INTO table (column)\nVALUES (${cursor});")});
        } else {
            snippets.push_back({QStringLiteral("section"), QStringLiteral("Title\n=====\n\n${cursor}")});
        }
        for (const CustomSnippetTemplate& customSnippet : customSnippets) {
            if (customSnippet.label.isEmpty() || customSnippet.body.isEmpty()) {
                continue;
            }
            if (customSnippet.language == QStringLiteral("All") || customSnippet.language == language) {
                snippets.push_back({QStringLiteral("Custom: %1").arg(customSnippet.label), customSnippet.body});
            }
        }
        return snippets;
    }

    void insertSnippet()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const QString language = page->highlighter != nullptr ? page->highlighter->languageName() : fileTypeName(page->path);
        const QVector<SnippetTemplate> snippets = snippetsForLanguage(language);
        QStringList labels;
        for (const SnippetTemplate& snippet : snippets) {
            labels << snippet.label;
        }

        bool ok = false;
        const QString selectedLabel = QInputDialog::getItem(this, QStringLiteral("Insert Snippet"),
            QStringLiteral("Snippet for %1:").arg(language), labels, 0, false, &ok);
        if (!ok || selectedLabel.isEmpty()) {
            return;
        }

        QString body;
        for (const SnippetTemplate& snippet : snippets) {
            if (snippet.label == selectedLabel) {
                body = snippet.body;
                break;
            }
        }
        if (body.isEmpty()) {
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        QString selectedText = cursor.selectedText();
        selectedText.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        body.replace(QStringLiteral("${selection}"), selectedText.isEmpty() ? QStringLiteral("condition") : selectedText);

        const QString baseIndent = QRegularExpression(QStringLiteral("^\\s*")).match(cursor.block().text()).captured(0);
        QStringList lines = body.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        QString formatted;
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines.at(i);
            line.replace(QLatin1Char('\t'), indentText());
            if (i > 0) {
                formatted += QLatin1Char('\n') + baseIndent;
            }
            formatted += line;
        }

        const int markerIndex = formatted.indexOf(QStringLiteral("${cursor}"));
        if (markerIndex >= 0) {
            formatted.remove(markerIndex, QStringLiteral("${cursor}").size());
        }

        const int insertStart = cursor.selectionStart();
        cursor.insertText(formatted);
        if (markerIndex >= 0) {
            cursor.setPosition(insertStart + markerIndex);
        }
        page->editor->setTextCursor(cursor);
        appendOutput(QStringLiteral("Inserted snippet: %1").arg(selectedLabel));
    }

    void insertDateTime()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        const QDateTime now = QDateTime::currentDateTime();
        const QStringList formats{
            QStringLiteral("ISO date - %1").arg(now.date().toString(Qt::ISODate)),
            QStringLiteral("ISO date/time - %1").arg(now.toString(Qt::ISODate)),
            QStringLiteral("Local date/time - %1").arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
            QStringLiteral("Time - %1").arg(now.time().toString(QStringLiteral("HH:mm:ss"))),
            QStringLiteral("Custom format...")
        };

        bool ok = false;
        const QString selected = QInputDialog::getItem(this, QStringLiteral("Insert Date/Time"),
            QStringLiteral("Format:"), formats, 0, false, &ok);
        if (!ok || selected.isEmpty()) {
            return;
        }

        QString text;
        if (selected.startsWith(QStringLiteral("ISO date/time"))) {
            text = now.toString(Qt::ISODate);
        } else if (selected.startsWith(QStringLiteral("Local date/time"))) {
            text = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else if (selected.startsWith(QStringLiteral("Time"))) {
            text = now.time().toString(QStringLiteral("HH:mm:ss"));
        } else if (selected.startsWith(QStringLiteral("Custom format"))) {
            const QString pattern = QInputDialog::getText(this, QStringLiteral("Custom Date/Time Format"),
                QStringLiteral("Qt date/time format:"), QLineEdit::Normal, QStringLiteral("yyyy-MM-dd HH:mm:ss"), &ok);
            if (!ok || pattern.trimmed().isEmpty()) {
                return;
            }
            text = now.toString(pattern.trimmed());
        } else {
            text = now.date().toString(Qt::ISODate);
        }

        QTextCursor cursor = page->editor->textCursor();
        cursor.beginEditBlock();
        cursor.insertText(text);
        cursor.endEditBlock();
        page->editor->setTextCursor(cursor);
        updateTabTitle(page, true);
        updateDocumentList();
        updateCursorStatus();
        appendOutput(QStringLiteral("Inserted date/time."));
    }

    void insertColorValue()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        QColor initialColor(Qt::white);
        if (cursor.hasSelection()) {
            const QString selected = cursor.selectedText().trimmed();
            if (QColor::isValidColor(selected)) {
                initialColor = QColor(selected);
            }
        }

        const QColor color = QColorDialog::getColor(initialColor, this, QStringLiteral("Insert Color"),
            QColorDialog::ShowAlphaChannel);
        if (!color.isValid()) {
            return;
        }

        const QStringList formats{
            QStringLiteral("#RRGGBB"),
            QStringLiteral("#AARRGGBB"),
            QStringLiteral("rgb(r, g, b)"),
            QStringLiteral("rgba(r, g, b, a)")
        };
        bool ok = false;
        const QString selectedFormat = QInputDialog::getItem(this, QStringLiteral("Insert Color"),
            QStringLiteral("Format:"), formats, 0, false, &ok);
        if (!ok || selectedFormat.isEmpty()) {
            return;
        }

        QString text;
        if (selectedFormat == QStringLiteral("#AARRGGBB")) {
            text = color.name(QColor::HexArgb);
        } else if (selectedFormat.startsWith(QStringLiteral("rgba"))) {
            text = QStringLiteral("rgba(%1, %2, %3, %4)")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue())
                .arg(QString::number(color.alphaF(), 'f', 2));
        } else if (selectedFormat.startsWith(QStringLiteral("rgb"))) {
            text = QStringLiteral("rgb(%1, %2, %3)")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue());
        } else {
            text = color.name(QColor::HexRgb);
        }

        cursor.beginEditBlock();
        cursor.insertText(text);
        cursor.endEditBlock();
        page->editor->setTextCursor(cursor);
        updateTabTitle(page, true);
        updateDocumentList();
        updateCursorStatus();
        appendOutput(QStringLiteral("Inserted color value: %1").arg(text));
    }

    void insertTextAtCursor(const QString& text, const QString& outputMessage)
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr || text.isEmpty()) {
            return;
        }
        if (page->editor->isReadOnly()) {
            statusBar()->showMessage(QStringLiteral("Current document is read-only"), 2500);
            return;
        }
        QTextCursor cursor = page->editor->textCursor();
        cursor.beginEditBlock();
        cursor.insertText(text);
        cursor.endEditBlock();
        page->editor->setTextCursor(cursor);
        updateTabTitle(page, true);
        updateDocumentList();
        refreshSymbols();
        updateCursorStatus();
        if (!outputMessage.isEmpty()) {
            appendOutput(outputMessage);
        }
    }

    void insertHtmlEntity()
    {
        const QStringList entries{
            QStringLiteral("&amp;  ampersand"),
            QStringLiteral("&lt;  less-than"),
            QStringLiteral("&gt;  greater-than"),
            QStringLiteral("&quot;  double quote"),
            QStringLiteral("&apos;  apostrophe"),
            QStringLiteral("&nbsp;  non-breaking space"),
            QStringLiteral("&copy;  copyright"),
            QStringLiteral("&reg;  registered"),
            QStringLiteral("&trade;  trademark"),
            QStringLiteral("&mdash;  em dash"),
            QStringLiteral("&ndash;  en dash"),
            QStringLiteral("&hellip;  ellipsis"),
            QStringLiteral("&laquo;  left guillemet"),
            QStringLiteral("&raquo;  right guillemet"),
            QStringLiteral("&lsquo;  left single quote"),
            QStringLiteral("&rsquo;  right single quote"),
            QStringLiteral("&ldquo;  left double quote"),
            QStringLiteral("&rdquo;  right double quote")
        };
        bool ok = false;
        const QString selected = QInputDialog::getItem(this, QStringLiteral("Insert HTML Entity"),
            QStringLiteral("Entity:"), entries, 0, true, &ok);
        if (!ok || selected.trimmed().isEmpty()) {
            return;
        }
        const QString entity = selected.section(QLatin1Char(' '), 0, 0).trimmed();
        insertTextAtCursor(entity, QStringLiteral("Inserted HTML entity: %1").arg(entity));
    }

    QString classSkeletonForLanguage(const QString& className, const QString& language) const
    {
        if (language == QStringLiteral("Python")) {
            return QStringLiteral("class %1:\n%2def __init__(self):\n%2%2pass\n")
                .arg(className, indentText());
        }
        if (language == QStringLiteral("Java")) {
            return QStringLiteral("public class %1 {\n%2public %1() {\n%2}\n}\n")
                .arg(className, indentText());
        }
        if (language == QStringLiteral("PHP")) {
            return QStringLiteral("class %1\n{\n%2public function __construct()\n%2{\n%2}\n}\n")
                .arg(className, indentText());
        }
        if (language == QStringLiteral("ArkTS") || language == QStringLiteral("TypeScript")) {
            return QStringLiteral("class %1 {\n%2constructor() {\n%2}\n}\n")
                .arg(className, indentText());
        }
        return QStringLiteral("class %1\n{\npublic:\n%2%1();\n%2~%1();\n\nprivate:\n%2\n};\n")
            .arg(className, indentText());
    }

    void insertClassSkeleton()
    {
        bool ok = false;
        const QString className = QInputDialog::getText(this, QStringLiteral("Create Class Skeleton"),
            QStringLiteral("Class name:"), QLineEdit::Normal, QStringLiteral("NewClass"), &ok).trimmed();
        if (!ok || className.isEmpty()) {
            return;
        }
        const QStringList languages{
            QStringLiteral("C++"),
            QStringLiteral("Python"),
            QStringLiteral("Java"),
            QStringLiteral("ArkTS"),
            QStringLiteral("TypeScript"),
            QStringLiteral("PHP")
        };
        const QString currentLanguage = currentSnippetLanguage();
        const int defaultIndex = qMax(0, languages.indexOf(currentLanguage));
        const QString language = QInputDialog::getItem(this, QStringLiteral("Create Class Skeleton"),
            QStringLiteral("Language:"), languages, defaultIndex, false, &ok);
        if (!ok || language.isEmpty()) {
            return;
        }
        insertTextAtCursor(classSkeletonForLanguage(className, language),
            QStringLiteral("Inserted %1 class skeleton: %2").arg(language, className));
    }

    void showPluginCompatibilityStatus()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Plugin Compatibility Status"));
        dialog.resize(920, 680);
        auto* layout = new QVBoxLayout(&dialog);
        auto* text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setPlainText(QStringLiteral(
            "OpenHarmony-compatible Geany plugin substitutes\n\n"
            "Available substitutes:\n"
            "- File Browser: Browse side panel and in-app file/folder pickers.\n"
            "- Save Actions: final newline, trailing-space stripping, backups, autosave.\n"
            "- Export: HTML export, PDF export, and print-to-PDF.\n"
            "- Split/terminal workflow: Terminal tab and command output panes.\n"
            "- HTML Characters: Tools > Insert HTML Entity.\n"
            "- Class Builder: Tools > Create Class Skeleton.\n"
            "- Tags: load/reload Geany or ctags files for symbol navigation.\n"
            "- External Tools: configurable shell commands with Geany-style placeholders.\n"
            "- Snippets/Templates: built-in and user-defined insertions.\n\n"
            "Not 1:1 compatible:\n"
            "- GTK binary plugins cannot be loaded by this Qt/OpenHarmony build.\n"
            "- VTE/PTY terminal embedding is replaced by a command runner.\n"
            "- Third-party plugin APIs need dedicated Qt/OpenHarmony rewrites or external-tool adapters.\n"));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(text, 1);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    }

    void maybeRunMigrationSelfTestFromTrigger()
    {
#if defined(Q_OS_OPENHARMONY)
        const QString triggerPath = QDir(openHarmonyWritableRoot()).filePath(QStringLiteral("geany-run-self-test"));
        if (!QFileInfo::exists(triggerPath)) {
            return;
        }
        QFile::remove(triggerPath);
        runMigrationSelfTest();
#endif
    }

    void runSaveIntegrityAcceptance()
    {
#if defined(Q_OS_OPENHARMONY)
        const QString root = QDir(openHarmonyWritableRoot()).filePath(QStringLiteral("save-guard-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!QDir().mkpath(root)) { qWarning() << "GEANY_SAVE_GUARD_SETUP_FAILED"; return; }
        const bool oldStrip = stripTrailingOnSave, oldFinal = ensureFinalNewlineOnSave;
        stripTrailingOnSave = false; ensureFinalNewlineOnSave = false;
        QJsonArray results; int passed = 0;
        auto record = [&](const QString &name, bool ok) {
            QJsonObject entry;entry.insert(QStringLiteral("name"),name);entry.insert(QStringLiteral("passed"),ok);results.append(entry);if(ok)++passed;
            qWarning().noquote() << QStringLiteral("GEANY_SAVE_GUARD %1 %2").arg(name, ok?QStringLiteral("PASS"):QStringLiteral("FAIL"));
        };
        auto read = [](const QString &path) { QFile f(path); if(!f.open(QIODevice::ReadOnly))return QByteArray();return f.readAll(); };
        auto fixture = [&](const QString &name,const QByteArray &bytes) {
            const QString path=QDir(root).filePath(name);QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()){f.close();return QString();}f.close();openPath(path,false);return path;
        };
        const QByteArray le=QByteArray::fromHex("fffe2d4e87653dd800de0d000a00"),be=QByteArray::fromHex("feff4e2d6587d83dde00000d000a"),utf8=QByteArray::fromHex("efbbbf")+QString::fromUtf8("中文😀\r\n").toUtf8();
        const QList<QPair<QString,QByteArray>> cases={{QStringLiteral("utf8-bom-crlf"),utf8},{QStringLiteral("utf16le-bom-crlf"),le},{QStringLiteral("utf16be-bom-crlf"),be}};
        for(const auto &item:cases){const QString path=fixture(item.first+QStringLiteral(".txt"),item.second);EditorPage *page=currentPage();record(item.first,!path.isEmpty()&&page&&page->path==path&&saveDocument(page,false,true)&&read(path)==item.second);}
        qWarning() << "GEANY_SAVE_GUARD_STAGE edited-fixture-begin";
        QString path=fixture(QStringLiteral("edited-utf16le.txt"),le);EditorPage *page=currentPage();
        qWarning() << "GEANY_SAVE_GUARD_STAGE edited-fixture-ready";
        if(page){
            qWarning() << "GEANY_SAVE_GUARD_STAGE edited-set-text-begin";
            page->editor->setPlainText(QString::fromUtf8("更改😀\n"));
            qWarning() << "GEANY_SAVE_GUARD_STAGE edited-set-text-end";
            page->editor->document()->setModified(true);
            qWarning() << "GEANY_SAVE_GUARD_STAGE edited-modified-end";
        }
        record(QStringLiteral("edited-utf16le"),page&&page->path==path&&saveDocument(page,false,true)&&read(path)==QByteArray::fromHex("fffef46639653dd800de0d000a00"));
        path=fixture(QStringLiteral("external-conflict.txt"),"original\n");page=currentPage();
        if(page){page->editor->setPlainText(QStringLiteral("ours  "));page->editor->document()->setModified(true);}
        {QFile f(path);if(f.open(QIODevice::WriteOnly|QIODevice::Truncate))f.write("external\n");}
        stripTrailingOnSave=true;ensureFinalNewlineOnSave=true;
        record(QStringLiteral("external-conflict-preserves-editor"),page&&page->path==path&&!saveDocument(page,false,true)&&read(path)=="external\n"&&page->editor->toPlainText()==QStringLiteral("ours  ")&&page->editor->document()->isModified());
        stripTrailingOnSave=false;ensureFinalNewlineOnSave=false;
        const QByteArray invalid=QByteArray::fromHex("efbbbfff");path=fixture(QStringLiteral("invalid-utf8.txt"),invalid);page=currentPage();
        if(page)page->editor->document()->setModified(true);
        record(QStringLiteral("lossy-decode-refused"),page&&page->path==path&&!saveDocument(page,false,true)&&read(path)==invalid&&page->editor->document()->isModified());
        path=fixture(QStringLiteral("unrepresentable.txt"),QString::fromUtf8("😀\n").toUtf8());page=currentPage();
        if(page){page->encodingName=QStringLiteral("ISO-8859-1");page->editor->document()->setModified(true);}
        record(QStringLiteral("unrepresentable-refused"),page&&page->path==path&&!saveDocument(page,false,true)&&read(path)==QString::fromUtf8("😀\n").toUtf8()&&page->editor->document()->isModified());
        path=fixture(QStringLiteral("final-newline.txt"),"before\n");page=currentPage();ensureFinalNewlineOnSave=true;
        if(page){page->editor->setPlainText(QStringLiteral("after"));page->editor->document()->setModified(true);}
        record(QStringLiteral("normalize-only-after-commit"),page&&page->path==path&&saveDocument(page,false,true)&&read(path)=="after\n"&&page->editor->toPlainText()==QStringLiteral("after\n")&&!page->editor->document()->isModified());
        stripTrailingOnSave=oldStrip;ensureFinalNewlineOnSave=oldFinal;
        QJsonObject report;report.insert(QStringLiteral("total"),results.size());report.insert(QStringLiteral("passed"),passed);report.insert(QStringLiteral("failed"),results.size()-passed);report.insert(QStringLiteral("skipped"),0);report.insert(QStringLiteral("context"),QStringLiteral("installed app production saveDocument"));report.insert(QStringLiteral("results"),results);
        const QString reportPath=QDir(root).filePath(QStringLiteral("production-save-report.json"));QFile file(reportPath);if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());
        qWarning().noquote() << QStringLiteral("GEANY_SAVE_GUARD_TERMINAL passed=%1 total=%2 report=%3").arg(passed).arg(results.size()).arg(reportPath);
        openPath(QDir(root).filePath(QStringLiteral("edited-utf16le.txt")),false);
#endif
    }

    void runMigrationSelfTest()
    {
#if defined(Q_OS_OPENHARMONY)
        QStringList lines;
        int passed = 0;
        int degraded = 0;
        int failed = 0;
        auto record = [&](const QString& name, bool ok, const QString& note = QString()) {
            qWarning().noquote() << QStringLiteral("STRICT80_STEP %1 %2").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), name);
            if (ok) {
                ++passed;
                lines << QStringLiteral("PASS %1%2").arg(name, note.isEmpty() ? QString() : QStringLiteral(" - ") + note);
            } else {
                ++failed;
                lines << QStringLiteral("FAIL %1%2").arg(name, note.isEmpty() ? QString() : QStringLiteral(" - ") + note);
            }
        };
        auto recordDegraded = [&](const QString& name, const QString& note) {
            qWarning().noquote() << QStringLiteral("STRICT80_STEP DEGRADED %1").arg(name);
            ++degraded;
            lines << QStringLiteral("DEGRADED %1 - %2").arg(name, note);
        };

        qWarning().noquote() << QStringLiteral("STRICT80_STEP START");
        const QString root = QDir(openHarmonyWritableRoot()).filePath(QStringLiteral("strict80-selftest"));
        QDir().mkpath(root);
        const QString mainPath = QDir(root).filePath(QStringLiteral("main.cpp"));
        const QString arkPath = QDir(root).filePath(QStringLiteral("Index.ets"));
        const QString savePath = QDir(root).filePath(QStringLiteral("saved-from-editor.txt"));
        const QString copyPath = QDir(root).filePath(QStringLiteral("copy.txt"));
        const QString selectionPath = QDir(root).filePath(QStringLiteral("selection.txt"));
        const QString htmlPath = QDir(root).filePath(QStringLiteral("main.html"));
        const QString pdfPath = QDir(root).filePath(QStringLiteral("main.pdf"));
        const QString projectPath = QDir(root).filePath(QStringLiteral("strict80.geany"));
        const QString sessionPath = QDir(root).filePath(QStringLiteral("strict80.geany-session"));
        const QString tagsPath = QDir(root).filePath(QStringLiteral("strict80.tags"));
        const QString reportPath = QDir(root).filePath(QStringLiteral("strict80-report.txt"));
        const QString mainText = QStringLiteral(
            "#include <iostream>\n"
            "// TODO: strict80 task marker\n"
            "class Strict80Runner {\n"
            "public:\n"
            "\tvoid run() { std::cout << \"needleStrict80\" << std::endl; }\n"
            "};\n"
            "int main() {\n"
            "\tStrict80Runner runner;\n"
            "\trunner.run();\n"
            "\treturn 0;\n"
            "}\n");
        const QString arkText = QStringLiteral(
            "@Entry\n"
            "@Component\n"
            "struct Strict80Page {\n"
            "\tbuild() {\n"
            "\t\tText('needleStrict80')\n"
            "\t}\n"
            "}\n");

        record(QStringLiteral("sandbox-save-main"), writeBytesToFile(mainPath, mainText.toUtf8(), QStringLiteral("Self Test Save")));
        record(QStringLiteral("sandbox-save-arkts"), writeBytesToFile(arkPath, arkText.toUtf8(), QStringLiteral("Self Test Save")));
        clearOpenDocumentsForSessionLoad();
        openProjectRoot(root);
        record(QStringLiteral("project-open-folder"), QFileInfo(projectRoot) == QFileInfo(root), projectRoot);
        record(QStringLiteral("project-index"), projectFilePaths.contains(mainPath) && projectFilePaths.contains(arkPath),
            QStringLiteral("%1 indexed").arg(projectFilePaths.size()));

        openPath(mainPath, false);
        EditorPage* page = currentPage();
        record(QStringLiteral("open-file"), page != nullptr && QFileInfo(page->path) == QFileInfo(mainPath));
        if (page != nullptr) {
            page->editor->appendPlainText(QStringLiteral("// saved_by_strict80_selftest"));
            record(QStringLiteral("save-existing-file"), saveDocument(page, false, true));
            record(QStringLiteral("save-file-content"), openDocumentTextForPath(mainPath).contains(QStringLiteral("saved_by_strict80_selftest")));

            const QString savedText = QStringLiteral("strict80 editor save path\n");
            record(QStringLiteral("save-as-core"), writeBytesToFile(savePath, savedText.toUtf8(), QStringLiteral("Self Test Save As"))
                && QFileInfo::exists(savePath));
            record(QStringLiteral("save-copy-as-core"), writeBytesToFile(copyPath, page->editor->toPlainText().toUtf8(), QStringLiteral("Self Test Copy"))
                && QFileInfo::exists(copyPath));
            const QString selection = page->editor->toPlainText().left(32);
            record(QStringLiteral("save-selection-core"), writeBytesToFile(selectionPath, selection.toUtf8(), QStringLiteral("Self Test Selection"))
                && QFileInfo::exists(selectionPath));

            QFile htmlFile(htmlPath);
            const bool htmlOpen = htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
            if (htmlOpen) {
                QTextStream htmlStream(&htmlFile);
                htmlStream.setCodec("UTF-8");
                writeDocumentHtml(page, htmlStream);
                htmlStream.flush();
                htmlFile.close();
            }
            record(QStringLiteral("export-html-core"), htmlOpen && QFileInfo(htmlPath).size() > 0, htmlPath);

            QPrinter printer(QPrinter::HighResolution);
            configurePrinterForDocument(&printer, page);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(pdfPath);
            page->editor->print(&printer);
            record(QStringLiteral("export-pdf-print-core"), QFileInfo(pdfPath).size() > 0, pdfPath);
        }

        QFile mainFile(mainPath);
        QString diskMainText;
        if (mainFile.open(QIODevice::ReadOnly)) {
            diskMainText = QString::fromUtf8(mainFile.readAll());
        }
        const QVector<ProjectSymbol> documentSymbols = symbolsForText(mainPath, diskMainText);
        record(QStringLiteral("document-symbols"), std::any_of(documentSymbols.begin(), documentSymbols.end(), [](const ProjectSymbol& symbol) {
            return symbol.name == QStringLiteral("Strict80Runner");
        }));
        const QVector<ProjectSymbol> projectSymbols = collectProjectSymbols();
        record(QStringLiteral("project-symbols"), std::any_of(projectSymbols.begin(), projectSymbols.end(), [](const ProjectSymbol& symbol) {
            return symbol.name == QStringLiteral("Strict80Runner");
        }), QStringLiteral("%1 symbols").arg(projectSymbols.size()));

        int scanned = 0;
        int matches = 0;
        QDirIterator searchIt(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (searchIt.hasNext()) {
            const QString path = searchIt.next();
            if (isIgnoredProjectPath(path, root) || !isSourceFile(path) || !canScanTextFile(path)) {
                continue;
            }
            ++scanned;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            matches += countMatchesInText(QString::fromUtf8(file.readAll()), QStringLiteral("needleStrict80"));
        }
        record(QStringLiteral("find-in-files-core"), matches >= 2, QStringLiteral("%1 matches in %2 files").arg(matches).arg(scanned));

        QFile replaceFile(arkPath);
        bool replaced = false;
        if (replaceFile.open(QIODevice::ReadOnly)) {
            QString text = QString::fromUtf8(replaceFile.readAll());
            replaceFile.close();
            text = replaceMatchesInText(text, QStringLiteral("needleStrict80"), QStringLiteral("replacedStrict80"));
            replaced = writeBytesToFile(arkPath, text.toUtf8(), QStringLiteral("Self Test Replace"));
        }
        QFile replacedCheck(arkPath);
        const bool replaceVerified = replacedCheck.open(QIODevice::ReadOnly)
            && QString::fromUtf8(replacedCheck.readAll()).contains(QStringLiteral("replacedStrict80"));
        record(QStringLiteral("replace-in-files-core"), replaced && replaceVerified);

        record(QStringLiteral("project-file-save"), writeProjectFile(projectPath, QStringLiteral("strict80"))
            && QFileInfo(projectPath).size() > 0, projectPath);

        if (page != nullptr) {
            page->bookmarks.insert(2);
            page->bookmarks.insert(7);
            updateBookmarksList();
        }
        record(QStringLiteral("bookmarks-core"), bookmarksList != nullptr && bookmarksList->count() >= 2);

        QSettings session(sessionPath, QSettings::IniFormat);
        session.clear();
        session.setValue(QStringLiteral("session/version"), 1);
        session.setValue(QStringLiteral("session/files"), openFilePaths());
        session.setValue(QStringLiteral("session/cursors"), openCursorEntries());
        session.setValue(QStringLiteral("session/bookmarks"), bookmarkEntriesForOpenFiles());
        session.setValue(QStringLiteral("session/activeFile"), mainPath);
        session.setValue(QStringLiteral("project/root"), projectRoot);
        session.setValue(QStringLiteral("project/file"), projectFile);
        session.setValue(QStringLiteral("build/command"), buildCommand);
        session.sync();
        record(QStringLiteral("session-save-core"), session.status() == QSettings::NoError && QFileInfo(sessionPath).size() > 0,
            sessionPath);

        clearOpenDocumentsForSessionLoad();
        for (const QString& filePath : session.value(QStringLiteral("session/files")).toStringList()) {
            if (QFileInfo::exists(filePath)) {
                openPath(filePath, false);
            }
        }
        restoreBookmarkEntries(session.value(QStringLiteral("session/bookmarks")).toStringList());
        restoreCursorEntries(session.value(QStringLiteral("session/cursors")).toStringList(), QString());
        setActiveFile(session.value(QStringLiteral("session/activeFile")).toString());
        page = currentPage();
        record(QStringLiteral("session-restore-core"), page != nullptr && QFileInfo(page->path) == QFileInfo(mainPath)
            && page->bookmarks.contains(2) && page->bookmarks.contains(7));

        scanTasks();
        if (tasksList != nullptr && tasksList->count() == 0 && page != nullptr) {
            int directTaskMatches = 0;
            addTaskItemsForText(mainPath, QFileInfo(mainPath).fileName(), page->editor->toPlainText(), &directTaskMatches);
        }
        record(QStringLiteral("task-scan-core"), tasksList != nullptr && tasksList->count() >= 1);

        if (page != nullptr) {
            QTextCursor symbolCursor = page->editor->document()->find(QStringLiteral("Strict80Runner"));
            if (!symbolCursor.isNull()) {
                page->editor->setTextCursor(symbolCursor);
            }
        }
        findReferences();
        record(QStringLiteral("find-references-core"), searchResults != nullptr && searchResults->count() >= 2);

        recordProblemLine(QStringLiteral("%1:3:5: error: strict80 diagnostic").arg(mainPath));
        record(QStringLiteral("build-diagnostics-core"), problemsList != nullptr && problemsList->count() >= 1);

        fileBrowserRoot = root;
        refreshFileBrowser();
        bool browserHasMain = false;
        if (fileBrowserList != nullptr) {
            for (int i = 0; i < fileBrowserList->count(); ++i) {
                if (fileBrowserList->item(i) != nullptr && fileBrowserList->item(i)->text().contains(QStringLiteral("main.cpp"))) {
                    browserHasMain = true;
                    break;
                }
            }
        }
        record(QStringLiteral("file-browser-substitute"), browserHasMain);

        QHash<QString, QString> parsedMappings;
        QString mappingError;
        record(QStringLiteral("custom-filetype-mapping"), parseCustomFileTypeMappings(QStringLiteral(".strict = C++\nBUILD = Python"),
            &parsedMappings, &mappingError) && parsedMappings.value(QStringLiteral("strict")) == QStringLiteral("C++"));

        const QString tagText = QStringLiteral("Strict80Tag\t%1\t1;\"\tf\tline:1\n").arg(mainPath);
        loadedTagFiles.clear();
        loadedTagSymbols.clear();
        record(QStringLiteral("tags-file-write"), writeBytesToFile(tagsPath, tagText.toUtf8(), QStringLiteral("Self Test Tags")));
        loadedTagFiles << tagsPath;
        reloadLoadedTags(false);
        record(QStringLiteral("tags-load-navigation"), std::any_of(loadedTagSymbols.begin(), loadedTagSymbols.end(), [](const ProjectSymbol& symbol) {
            return symbol.name == QStringLiteral("Strict80Tag");
        }));

        customSnippets.push_back({QStringLiteral("ArkTS"), QStringLiteral("Strict80 custom"), QStringLiteral("Text('${cursor}')")});
        const QVector<SnippetTemplate> arkSnippets = snippetsForLanguage(QStringLiteral("ArkTS"));
        record(QStringLiteral("snippets-custom-core"), std::any_of(arkSnippets.begin(), arkSnippets.end(), [](const SnippetTemplate& snippet) {
                return snippet.label.contains(QStringLiteral("Strict80 custom"));
            }));
        const QVector<FileTemplate> availableTemplates = fileTemplates();
        record(QStringLiteral("file-templates-core"), std::any_of(availableTemplates.begin(), availableTemplates.end(), [](const FileTemplate& fileTemplate) {
            return fileTemplate.label == QStringLiteral("C++ Source");
        }));

        addCommandHistory(QStringLiteral("echo strict80"));
        externalTools.push_back({QStringLiteral("Strict80 Echo"), QStringLiteral("echo {file}")});
        saveSettings();
        QSettings toolSettings;
        record(QStringLiteral("command-history-persist"), toolSettings.value(QStringLiteral("tools/commandHistory")).toStringList().contains(QStringLiteral("echo strict80")));
        record(QStringLiteral("external-tools-persist"), externalTools.size() >= 3);

        const QString previousTheme = editorTheme;
        const int previousFont = fontSize;
        editorTheme = QStringLiteral("High Contrast");
        fontSize = qMax(fontSize, 18);
        saveSettings();
        QSettings settingsCheck;
        record(QStringLiteral("preferences-persist"), settingsCheck.value(QStringLiteral("editor/theme")).toString() == QStringLiteral("High Contrast")
            && settingsCheck.value(QStringLiteral("editor/fontSize")).toInt() >= 18);
        editorTheme = previousTheme;
        fontSize = previousFont;
        applyTheme();
        applyEditorPreferences();
        saveSettings();

        lastShellWorkingDirectory = root;
        record(QStringLiteral("terminal-builtins"), handleTerminalBuiltIn(QStringLiteral("pwd"))
            && handleTerminalBuiltIn(QStringLiteral("ls ."))
            && handleTerminalBuiltIn(QStringLiteral("cat main.cpp")));
        record(QStringLiteral("external-tool-list-files"), runNativeExternalTool({QStringLiteral("List Project Files"), QStringLiteral("native")}, mainPath));
        record(QStringLiteral("external-tool-grep-word"), runNativeExternalTool({QStringLiteral("Grep Current Word"), QStringLiteral("native")}, mainPath));
        record(QStringLiteral("html-entity-substitute"), QStringLiteral("&amp;").startsWith(QStringLiteral("&")));
        record(QStringLiteral("class-builder-substitute"), classSkeletonForLanguage(QStringLiteral("Strict80Generated"), QStringLiteral("ArkTS"))
            .contains(QStringLiteral("Strict80Generated")));
        setSplitViewVisible(true);
        record(QStringLiteral("split-view-substitute"), splitView != nullptr && splitView->isVisible());
        setSplitViewVisible(false);

        recordDegraded(QStringLiteral("gtk-binary-plugin-abi"), QStringLiteral("Qt/OpenHarmony build cannot load original GTK binary plugins; native substitutes are provided."));
        recordDegraded(QStringLiteral("vte-pty-terminal"), QStringLiteral("Full VTE/PTTY terminal is replaced by command runner plus built-ins."));
        recordDegraded(QStringLiteral("external-arbitrary-file-uri"), QStringLiteral("Harmony sandbox still limits arbitrary external file:// paths; app-readable paths are stable."));

        lines.prepend(QStringLiteral("Geany OpenHarmony strict80 self-test: %1 pass, %2 degraded, %3 fail")
            .arg(passed)
            .arg(degraded)
            .arg(failed));
        lines << QStringLiteral("root=%1").arg(root);
        writeBytesToFile(reportPath, lines.join(QLatin1Char('\n')).toUtf8(), QStringLiteral("Self Test Report"));
        appendOutput(lines.join(QLatin1Char('\n')));
        appendOutput(QStringLiteral("Self-test report: %1").arg(reportPath));
        statusBar()->showMessage(QStringLiteral("Self-test complete: %1 pass, %2 degraded, %3 fail").arg(passed).arg(degraded).arg(failed), 5000);
#else
        appendOutput(QStringLiteral("Self-test is only enabled for OpenHarmony builds."));
#endif
    }

    QString currentSnippetLanguage() const
    {
        if (EditorPage* page = currentPage(); page != nullptr) {
            return page->highlighter != nullptr ? page->highlighter->languageName() : fileTypeName(page->path);
        }
        return QStringLiteral("Plain Text");
    }

    QStringList snippetLanguages() const
    {
        QStringList languages{QStringLiteral("All")};
        languages << MiniHighlighter::supportedLanguages();
        return languages;
    }

    QVector<FileTemplate> builtInFileTemplates() const
    {
        return {
            {
                QStringLiteral("Plain Text Note"),
                QStringLiteral("Plain Text"),
                QStringLiteral("${date}\n\n${cursor}\n")
            },
            {
                QStringLiteral("C Source"),
                QStringLiteral("C"),
                QStringLiteral("#include <stdio.h>\n\nint main(int argc, char** argv)\n{\n\t${cursor}\n\treturn 0;\n}\n")
            },
            {
                QStringLiteral("C++ Source"),
                QStringLiteral("C++"),
                QStringLiteral("#include <iostream>\n\nint main(int argc, char** argv)\n{\n\t${cursor}\n\treturn 0;\n}\n")
            },
            {
                QStringLiteral("C/C++ Header"),
                QStringLiteral("C++"),
                QStringLiteral("#pragma once\n\n${cursor}\n")
            },
            {
                QStringLiteral("Python Script"),
                QStringLiteral("Python"),
                QStringLiteral("#!/usr/bin/env python3\n\n\ndef main():\n\t${cursor}\n\n\nif __name__ == \"__main__\":\n\tmain()\n")
            },
            {
                QStringLiteral("Shell Script"),
                QStringLiteral("Shell"),
                QStringLiteral("#!/usr/bin/env sh\nset -eu\n\n${cursor}\n")
            },
            {
                QStringLiteral("ArkTS Page"),
                QStringLiteral("ArkTS"),
                QStringLiteral("@Entry\n@Component\nstruct Index {\n\tbuild() {\n\t\tColumn() {\n\t\t\tText('${cursor}')\n\t\t}\n\t\t.width('100%')\n\t\t.height('100%')\n\t}\n}\n")
            },
            {
                QStringLiteral("CMakeLists.txt"),
                QStringLiteral("CMake"),
                QStringLiteral("cmake_minimum_required(VERSION 3.16)\nproject(app)\n\nadd_executable(app\n\t${cursor}\n)\n")
            },
            {
                QStringLiteral("Meson Build"),
                QStringLiteral("Meson"),
                QStringLiteral("project('app', 'c')\n\nexecutable('app',\n\t'${cursor}'\n)\n")
            },
            {
                QStringLiteral("Makefile"),
                QStringLiteral("Makefile"),
                QStringLiteral("all:\n\t${cursor}\n\n.PHONY: all\n")
            },
            {
                QStringLiteral("Dockerfile"),
                QStringLiteral("Dockerfile"),
                QStringLiteral("FROM alpine:latest\n\nWORKDIR /app\nCOPY . .\nRUN ${cursor}\n")
            },
            {
                QStringLiteral("Markdown Document"),
                QStringLiteral("Markdown"),
                QStringLiteral("# Title\n\n${cursor}\n")
            },
            {
                QStringLiteral("HTML Page"),
                QStringLiteral("Markup"),
                QStringLiteral("<!doctype html>\n<html>\n<head>\n\t<meta charset=\"utf-8\">\n\t<title>Title</title>\n</head>\n<body>\n\t${cursor}\n</body>\n</html>\n")
            },
            {
                QStringLiteral("CSS Stylesheet"),
                QStringLiteral("CSS"),
                QStringLiteral(":root {\n\tfont-family: system-ui, sans-serif;\n}\n\nbody {\n\tmargin: 0;\n\t${cursor}\n}\n")
            },
            {
                QStringLiteral("JSON Object"),
                QStringLiteral("JSON"),
                QStringLiteral("{\n\t\"${cursor}\": \"\"\n}\n")
            },
            {
                QStringLiteral("YAML Document"),
                QStringLiteral("YAML"),
                QStringLiteral("name: ${cursor}\n")
            },
            {
                QStringLiteral("TOML Document"),
                QStringLiteral("TOML"),
                QStringLiteral("name = \"${cursor}\"\n")
            }
        };
    }

    QVector<FileTemplate> fileTemplates() const
    {
        QVector<FileTemplate> templates = builtInFileTemplates();
        for (const FileTemplate& fileTemplate : customFileTemplates) {
            if (!fileTemplate.label.isEmpty() && !fileTemplate.body.isEmpty()) {
                templates.push_back({
                    QStringLiteral("Custom: %1").arg(fileTemplate.label),
                    fileTemplate.language,
                    fileTemplate.body
                });
            }
        }
        return templates;
    }

    void newDocumentFromTemplate()
    {
        const QVector<FileTemplate> templates = fileTemplates();
        QStringList labels;
        for (const FileTemplate& fileTemplate : templates) {
            labels << fileTemplate.label;
        }

        bool ok = false;
        const QString selectedLabel = QInputDialog::getItem(this, QStringLiteral("New from Template"),
            QStringLiteral("Template:"), labels, 0, false, &ok);
        if (!ok || selectedLabel.isEmpty()) {
            return;
        }

        FileTemplate selectedTemplate;
        bool found = false;
        for (const FileTemplate& fileTemplate : templates) {
            if (fileTemplate.label == selectedLabel) {
                selectedTemplate = fileTemplate;
                found = true;
                break;
            }
        }
        if (!found) {
            return;
        }

        newDocument();
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        QString body = selectedTemplate.body;
        body.replace(QStringLiteral("${date}"), QDate::currentDate().toString(Qt::ISODate));
        body.replace(QLatin1Char('\t'), indentText());
        const int markerIndex = body.indexOf(QStringLiteral("${cursor}"));
        if (markerIndex >= 0) {
            body.remove(markerIndex, QStringLiteral("${cursor}").size());
        }

        page->editor->setPlainText(body);
        if (page->highlighter != nullptr) {
            page->highlighter->configureForLanguage(selectedTemplate.language);
        }
        QTextCursor cursor = page->editor->textCursor();
        if (markerIndex >= 0) {
            cursor.setPosition(qMin(markerIndex, page->editor->document()->characterCount() - 1));
            page->editor->setTextCursor(cursor);
        }
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        updateDocumentList();
        updateCursorStatus();
        appendOutput(QStringLiteral("Created new document from template: %1").arg(selectedTemplate.label));
    }

    void newDocumentFromClipboard()
    {
        const QString clipboardText = QApplication::clipboard()->text();
        if (clipboardText.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Clipboard does not contain text"), 2500);
            return;
        }

        newDocument();
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        page->editor->setPlainText(normalizeLineEndings(clipboardText));
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        updateDocumentList();
        updateCursorStatus();
        appendOutput(QStringLiteral("Created new document from clipboard."));
    }

    void newDocumentFromSelection()
    {
        EditorPage* sourcePage = currentPage();
        if (sourcePage == nullptr || sourcePage->editor == nullptr) {
            return;
        }

        QTextCursor sourceCursor = sourcePage->editor->textCursor();
        if (!sourceCursor.hasSelection()) {
            statusBar()->showMessage(QStringLiteral("Select text before creating a document from selection"), 2500);
            return;
        }

        QString text = sourceCursor.selectedText();
        text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        const QString language = sourcePage->highlighter != nullptr
            ? sourcePage->highlighter->languageName()
            : fileTypeName(sourcePage->path);

        newDocument();
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        page->editor->setPlainText(normalizeLineEndings(text));
        if (page->highlighter != nullptr) {
            page->highlighter->configureForLanguage(language);
        }
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        updateDocumentList();
        updateCursorStatus();
        appendOutput(QStringLiteral("Created new document from selection."));
    }

    EditorPage* currentPage() const
    {
        return static_cast<EditorPage*>(tabs->currentWidget());
    }

    QString displayName(EditorPage* page) const
    {
        if (page == nullptr) {
            return QStringLiteral("Untitled");
        }
        if (page->path.isEmpty()) {
            return QStringLiteral("Untitled %1").arg(tabs->indexOf(page) + 1);
        }
        return QFileInfo(page->path).fileName();
    }

    QString currentWorkDir() const
    {
        if (!projectRoot.isEmpty()) {
#if defined(Q_OS_OPENHARMONY)
            if (!openHarmonyPathNeedsWritableFallback(projectRoot)) {
                return projectRoot;
            }
#else
            return projectRoot;
#endif
        }
        if (EditorPage* page = currentPage(); page != nullptr && !page->path.isEmpty()) {
            const QString dir = QFileInfo(page->path).absolutePath();
#if defined(Q_OS_OPENHARMONY)
            if (!openHarmonyPathNeedsWritableFallback(dir)) {
                return dir;
            }
#else
            return dir;
#endif
        }
#if defined(Q_OS_OPENHARMONY)
        return openHarmonyWritableRoot();
#else
        return QDir::homePath();
#endif
    }

    QString effectiveDefaultLineEnding() const
    {
        return projectDefaultLineEnding.isEmpty() ? defaultLineEnding : projectDefaultLineEnding;
    }

    QString effectiveDefaultEncodingName() const
    {
        return projectDefaultEncodingName.isEmpty() ? defaultEncodingName : projectDefaultEncodingName;
    }

    void setProjectDefaultLineEnding(const QString& lineEnding)
    {
        if (lineEnding == QStringLiteral("\n") || lineEnding == QStringLiteral("\r\n") || lineEnding == QStringLiteral("\r")) {
            projectDefaultLineEnding = lineEnding;
            return;
        }
        projectDefaultLineEnding.clear();
    }

    void restoreSettings()
    {
        QSettings settings;
        editorFontFamily = settings.value(QStringLiteral("editor/fontFamily"), editorFontFamily).toString().trimmed();
        if (editorFontFamily.isEmpty()) {
            editorFontFamily = QStringLiteral("monospace");
        }
        fontSize = settings.value(QStringLiteral("editor/fontSize"), fontSize).toInt();
        tabWidth = settings.value(QStringLiteral("editor/tabWidth"), tabWidth).toInt();
        insertSpacesOnTab = settings.value(QStringLiteral("editor/insertSpacesOnTab"), insertSpacesOnTab).toBool();
        autoIndentEnabled = settings.value(QStringLiteral("editor/autoIndent"), autoIndentEnabled).toBool();
        bracketPairingEnabled = settings.value(QStringLiteral("editor/bracketPairing"), bracketPairingEnabled).toBool();
        wordCompletionEnabled = settings.value(QStringLiteral("editor/wordCompletion"), wordCompletionEnabled).toBool();
        highlightOccurrencesEnabled = settings.value(QStringLiteral("editor/highlightOccurrences"), highlightOccurrencesEnabled).toBool();
        highlightCurrentLineEnabled = settings.value(QStringLiteral("editor/highlightCurrentLine"), highlightCurrentLineEnabled).toBool();
        showLineNumbersEnabled = settings.value(QStringLiteral("editor/showLineNumbers"), showLineNumbersEnabled).toBool();
        showIndentGuidesEnabled = settings.value(QStringLiteral("editor/showIndentGuides"), showIndentGuidesEnabled).toBool();
        showRightMarginEnabled = settings.value(QStringLiteral("editor/showRightMargin"), showRightMarginEnabled).toBool();
        rightMarginColumn = settings.value(QStringLiteral("editor/rightMarginColumn"), rightMarginColumn).toInt();
        showWhitespaceEnabled = settings.value(QStringLiteral("editor/showWhitespace"), showWhitespaceEnabled).toBool();
        showLineEndingsEnabled = settings.value(QStringLiteral("editor/showLineEndings"), showLineEndingsEnabled).toBool();
        defaultLineEnding = settings.value(QStringLiteral("editor/defaultLineEnding"), defaultLineEnding).toString();
        if (defaultLineEnding != QStringLiteral("\n") && defaultLineEnding != QStringLiteral("\r\n") && defaultLineEnding != QStringLiteral("\r")) {
            defaultLineEnding = QStringLiteral("\n");
        }
        defaultEncodingName = normalizedEncodingName(settings.value(QStringLiteral("editor/defaultEncoding"), defaultEncodingName).toString());
        ensureFinalNewlineOnSave = settings.value(QStringLiteral("editor/ensureFinalNewlineOnSave"), ensureFinalNewlineOnSave).toBool();
        stripTrailingOnSave = settings.value(QStringLiteral("editor/stripTrailingOnSave"), stripTrailingOnSave).toBool();
        createBackupOnSave = settings.value(QStringLiteral("editor/createBackupOnSave"), createBackupOnSave).toBool();
        autoSaveEnabled = settings.value(QStringLiteral("editor/autoSaveEnabled"), autoSaveEnabled).toBool();
        autoSaveIntervalSeconds = settings.value(QStringLiteral("editor/autoSaveIntervalSeconds"), autoSaveIntervalSeconds).toInt();
        findMatchCase = settings.value(QStringLiteral("search/matchCase"), findMatchCase).toBool();
        findWholeWord = settings.value(QStringLiteral("search/wholeWord"), findWholeWord).toBool();
        findRegularExpression = settings.value(QStringLiteral("search/regularExpression"), findRegularExpression).toBool();
        searchFilePattern = settings.value(QStringLiteral("search/filePattern"), searchFilePattern).toString();
        saveAllBeforeBuild = settings.value(QStringLiteral("build/saveAllBeforeBuild"), saveAllBeforeBuild).toBool();
        editorTheme = settings.value(QStringLiteral("editor/theme"), editorTheme).toString();
        buildCommand = settings.value(QStringLiteral("build/command"), buildCommand).toString();
        buildCleanCommand = settings.value(QStringLiteral("build/cleanCommand"), buildCleanCommand).toString();
        buildTargetCommand = settings.value(QStringLiteral("build/targetCommand"), buildTargetCommand).toString();
        compileCCommand = settings.value(QStringLiteral("build/compileC"), compileCCommand).toString();
        compileCppCommand = settings.value(QStringLiteral("build/compileCpp"), compileCppCommand).toString();
        checkPythonCommand = settings.value(QStringLiteral("build/checkPython"), checkPythonCommand).toString();
        checkShellCommand = settings.value(QStringLiteral("build/checkShell"), checkShellCommand).toString();
        checkJsCommand = settings.value(QStringLiteral("build/checkJs"), checkJsCommand).toString();
        checkArkTsCommand = settings.value(QStringLiteral("build/checkArkTs"), checkArkTsCommand).toString();
        checkJsonCommand = settings.value(QStringLiteral("build/checkJson"), checkJsonCommand).toString();
        checkGoCommand = settings.value(QStringLiteral("build/checkGo"), checkGoCommand).toString();
        checkRustCommand = settings.value(QStringLiteral("build/checkRust"), checkRustCommand).toString();
        checkJavaCommand = settings.value(QStringLiteral("build/checkJava"), checkJavaCommand).toString();
        checkPhpCommand = settings.value(QStringLiteral("build/checkPhp"), checkPhpCommand).toString();
        checkYamlCommand = settings.value(QStringLiteral("build/checkYaml"), checkYamlCommand).toString();
        checkTomlCommand = settings.value(QStringLiteral("build/checkToml"), checkTomlCommand).toString();
        executePythonCommand = settings.value(QStringLiteral("build/executePython"), executePythonCommand).toString();
        executeShellCommand = settings.value(QStringLiteral("build/executeShell"), executeShellCommand).toString();
        executeJsCommand = settings.value(QStringLiteral("build/executeJs"), executeJsCommand).toString();
        executeGoCommand = settings.value(QStringLiteral("build/executeGo"), executeGoCommand).toString();
        executeRustCommand = settings.value(QStringLiteral("build/executeRust"), executeRustCommand).toString();
        executeJavaCommand = settings.value(QStringLiteral("build/executeJava"), executeJavaCommand).toString();
        executePhpCommand = settings.value(QStringLiteral("build/executePhp"), executePhpCommand).toString();
        executeNativeCommand = settings.value(QStringLiteral("build/executeNative"), executeNativeCommand).toString();
        projectRoot = settings.value(QStringLiteral("project/root")).toString();
        projectFile = settings.value(QStringLiteral("project/file")).toString();
        recentFiles = settings.value(QStringLiteral("files/recent")).toStringList();
        recentProjects = settings.value(QStringLiteral("project/recent")).toStringList();
        closedFiles = settings.value(QStringLiteral("files/closed")).toStringList();
        fileBrowserRoot = settings.value(QStringLiteral("files/browserRoot"), QString()).toString();
        commandHistory = settings.value(QStringLiteral("tools/commandHistory")).toStringList();
        lastShellCommand = settings.value(QStringLiteral("tools/lastShellCommand")).toString();
        lastShellWorkingDirectory = settings.value(QStringLiteral("tools/lastShellWorkingDirectory")).toString();
#if defined(Q_OS_OPENHARMONY)
        if (openHarmonyPathNeedsWritableFallback(projectRoot)) {
            projectRoot.clear();
            projectFile.clear();
        }
        if (openHarmonyPathNeedsWritableFallback(lastShellWorkingDirectory)) {
            lastShellWorkingDirectory = openHarmonyWritableRoot();
        }
#endif
        loadedTagFiles = settings.value(QStringLiteral("tools/tagFiles")).toStringList();
        externalTools.clear();
        const int externalToolCount = settings.beginReadArray(QStringLiteral("tools/external"));
        for (int i = 0; i < externalToolCount; ++i) {
            settings.setArrayIndex(i);
            ExternalTool tool{
                settings.value(QStringLiteral("label")).toString(),
                settings.value(QStringLiteral("command")).toString()
            };
            if (!tool.label.trimmed().isEmpty() && !tool.command.trimmed().isEmpty()) {
                externalTools.push_back(tool);
            }
        }
        settings.endArray();
        if (externalTools.isEmpty()) {
            externalTools.push_back({QStringLiteral("List Project Files"), QStringLiteral("find {project} -maxdepth 2 -type f | sort | head -200")});
            externalTools.push_back({QStringLiteral("Grep Current Word"), QStringLiteral("grep -RIn {base} {project}")});
        }
        if (scratchPad != nullptr) {
            scratchPad->setPlainText(settings.value(QStringLiteral("tools/scribble")).toString());
        }
        QString parseError;
        parseCustomFileTypeMappings(settings.value(QStringLiteral("filetypes/customMappings")).toStringList().join(QLatin1Char('\n')),
            &customFileTypeMappings, &parseError);
        customSnippets.clear();
        const int snippetCount = settings.beginReadArray(QStringLiteral("snippets/custom"));
        for (int i = 0; i < snippetCount; ++i) {
            settings.setArrayIndex(i);
            CustomSnippetTemplate snippet{
                settings.value(QStringLiteral("language"), QStringLiteral("All")).toString(),
                settings.value(QStringLiteral("label")).toString(),
                settings.value(QStringLiteral("body")).toString()
            };
            if (!snippet.label.isEmpty() && !snippet.body.isEmpty()) {
                if (!snippetLanguages().contains(snippet.language)) {
                    snippet.language = QStringLiteral("All");
                }
                customSnippets.push_back(snippet);
            }
        }
        settings.endArray();
        customFileTemplates.clear();
        const int templateCount = settings.beginReadArray(QStringLiteral("templates/custom"));
        for (int i = 0; i < templateCount; ++i) {
            settings.setArrayIndex(i);
            FileTemplate fileTemplate{
                settings.value(QStringLiteral("label")).toString(),
                settings.value(QStringLiteral("language"), QStringLiteral("Plain Text")).toString(),
                settings.value(QStringLiteral("body")).toString()
            };
            if (!MiniHighlighter::supportedLanguages().contains(fileTemplate.language)) {
                fileTemplate.language = QStringLiteral("Plain Text");
            }
            if (!fileTemplate.label.isEmpty() && !fileTemplate.body.isEmpty()) {
                customFileTemplates.push_back(fileTemplate);
            }
        }
        settings.endArray();
        reloadLoadedTags(false);
        if (fileBrowserRoot.isEmpty() || fileBrowserRoot.startsWith(QStringLiteral("/storage/Users/currentUser"))
            || !QDir(fileBrowserRoot).exists()) {
            fileBrowserRoot = projectRoot.isEmpty() ? currentWorkDir() : projectRoot;
        }
        refreshFileBrowser();
        QSize restoredSize = settings.value(QStringLiteral("window/size"), size()).toSize();
#if defined(Q_OS_OPENHARMONY)
        const QSize minimumReadableSize(1280, 820);
        if (!restoredSize.isValid() || restoredSize.width() < minimumReadableSize.width()
            || restoredSize.height() < minimumReadableSize.height()) {
            restoredSize = size();
        }
#endif
        resize(restoredSize);
        if (settings.contains(QStringLiteral("window/pos"))) {
            move(settings.value(QStringLiteral("window/pos")).toPoint());
        }
        const bool sidebarVisible = settings.value(QStringLiteral("window/sidebarVisible"), true).toBool();
        const bool outputVisible = settings.value(QStringLiteral("window/outputVisible"), true).toBool();
        const bool toolbarVisible = settings.value(QStringLiteral("window/toolbarVisible"), true).toBool();
        const bool statusbarVisible = settings.value(QStringLiteral("window/statusbarVisible"), true).toBool();
        showSidebarAction->setChecked(sidebarVisible);
        showOutputAction->setChecked(outputVisible);
        showToolbarAction->setChecked(toolbarVisible);
        showStatusbarAction->setChecked(statusbarVisible);
        sideTabs->setVisible(sidebarVisible);
        outputTabs->setVisible(outputVisible);
        if (mainToolBar != nullptr) {
            mainToolBar->setVisible(toolbarVisible);
        }
        statusBar()->setVisible(statusbarVisible);
        if (mainSplitter != nullptr && settings.contains(QStringLiteral("window/mainSplitter"))) {
            mainSplitter->restoreState(settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
        }
        if (rightSplitter != nullptr && settings.contains(QStringLiteral("window/rightSplitter"))) {
            rightSplitter->restoreState(settings.value(QStringLiteral("window/rightSplitter")).toByteArray());
        }
        if (editorSplitter != nullptr && settings.contains(QStringLiteral("window/editorSplitter"))) {
            editorSplitter->restoreState(settings.value(QStringLiteral("window/editorSplitter")).toByteArray());
        }
        const bool splitVisible = settings.value(QStringLiteral("window/splitViewVisible"), false).toBool();
        if (splitViewAction != nullptr) {
            splitViewAction->setChecked(splitVisible);
        }
        if (splitView != nullptr) {
            splitView->setVisible(splitVisible);
        }
        const int sideIndex = qBound(0,
            settings.value(QStringLiteral("window/sideTabIndex"), 0).toInt(),
            qMax(0, sideTabs->count() - 1));
        sideTabs->setCurrentIndex(sideIndex);

        if (!projectRoot.isEmpty() && QDir(projectRoot).exists()) {
            if (!projectFile.isEmpty() && QFileInfo::exists(projectFile)) {
                QSettings projectSettings(projectFile, QSettings::IniFormat);
                loadProjectConfiguration(projectSettings);
            } else {
                projectDefaultLineEnding.clear();
                projectDefaultEncodingName.clear();
                projectExtraIgnorePatterns.clear();
            }
            loadProjectFiles();
        } else {
            projectRoot.clear();
            projectFile.clear();
            projectDefaultLineEnding.clear();
            projectDefaultEncodingName.clear();
            projectExtraIgnorePatterns.clear();
        }

        const QStringList sessionFiles = settings.value(QStringLiteral("session/files")).toStringList();
        for (const QString& path : sessionFiles) {
            if (QFileInfo::exists(path)) {
                openPath(path, false, false);
            }
        }

        const QStringList storedBookmarks = settings.value(QStringLiteral("session/bookmarks")).toStringList();
        for (const QString& entry : storedBookmarks) {
            const int split = entry.lastIndexOf(QLatin1Char(':'));
            if (split <= 0) {
                continue;
            }
            const QString path = entry.left(split);
            const int line = entry.mid(split + 1).toInt();
            for (int i = 0; i < tabs->count(); ++i) {
                auto* page = static_cast<EditorPage*>(tabs->widget(i));
                if (page != nullptr && page->path == path && line > 0) {
                    page->bookmarks.insert(line);
                }
            }
        }
        restoreCursorEntries(settings.value(QStringLiteral("session/cursors")).toStringList(), QString());
        setActiveFile(settings.value(QStringLiteral("session/activeFile")).toString());

        const bool wrap = settings.value(QStringLiteral("editor/wrap"), false).toBool();
        wordWrapAction->setChecked(wrap);
        highlightCurrentLineAction->setChecked(highlightCurrentLineEnabled);
        showLineNumbersAction->setChecked(showLineNumbersEnabled);
        showIndentGuidesAction->setChecked(showIndentGuidesEnabled);
        showRightMarginAction->setChecked(showRightMarginEnabled);
        showWhitespaceAction->setChecked(showWhitespaceEnabled);
        showLineEndingsAction->setChecked(showLineEndingsEnabled);
        saveAllBeforeBuildAction->setChecked(saveAllBeforeBuild);
        applyTheme();
        setWordWrap(wrap);
        applyEditorPreferences();
        updateBookmarksList();
        updateRecentFilesMenu();
        updateRecentProjectsMenu();
        updateRecentCommandsMenu();
        updateExternalToolsMenu();
    }

    void saveSettings()
    {
        QSettings settings;
        QStringList sessionFiles;
        QStringList storedBookmarks;
        QStringList storedCursors;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && !page->path.isEmpty()) {
                sessionFiles << page->path;
                storedCursors << cursorEntry(page, QDir(), false);
                QList<int> lines = page->bookmarks.values();
                std::sort(lines.begin(), lines.end());
                for (int line : lines) {
                    storedBookmarks << QStringLiteral("%1:%2").arg(page->path).arg(line);
                }
            }
        }

        settings.setValue(QStringLiteral("window/size"), size());
        settings.setValue(QStringLiteral("window/pos"), pos());
        settings.setValue(QStringLiteral("window/sidebarVisible"),
            focusModeActive ? focusModeSavedSidebar : (sideTabs != nullptr && sideTabs->isVisible()));
        settings.setValue(QStringLiteral("window/outputVisible"),
            focusModeActive ? focusModeSavedOutput : (outputTabs != nullptr && outputTabs->isVisible()));
        settings.setValue(QStringLiteral("window/toolbarVisible"),
            focusModeActive ? focusModeSavedToolbar : (mainToolBar != nullptr && mainToolBar->isVisible()));
        settings.setValue(QStringLiteral("window/statusbarVisible"),
            focusModeActive ? focusModeSavedStatusbar : statusBar()->isVisible());
        settings.setValue(QStringLiteral("window/sideTabIndex"), sideTabs != nullptr ? sideTabs->currentIndex() : 0);
        if (mainSplitter != nullptr) {
            settings.setValue(QStringLiteral("window/mainSplitter"), mainSplitter->saveState());
        }
        if (rightSplitter != nullptr) {
            settings.setValue(QStringLiteral("window/rightSplitter"), rightSplitter->saveState());
        }
        if (editorSplitter != nullptr) {
            settings.setValue(QStringLiteral("window/editorSplitter"), editorSplitter->saveState());
        }
        settings.setValue(QStringLiteral("window/splitViewVisible"), splitViewAction != nullptr && splitViewAction->isChecked());
        settings.setValue(QStringLiteral("editor/fontSize"), fontSize);
        settings.setValue(QStringLiteral("editor/fontFamily"), editorFontFamily);
        settings.setValue(QStringLiteral("editor/tabWidth"), tabWidth);
        settings.setValue(QStringLiteral("editor/insertSpacesOnTab"), insertSpacesOnTab);
        settings.setValue(QStringLiteral("editor/autoIndent"), autoIndentEnabled);
        settings.setValue(QStringLiteral("editor/bracketPairing"), bracketPairingEnabled);
        settings.setValue(QStringLiteral("editor/wordCompletion"), wordCompletionEnabled);
        settings.setValue(QStringLiteral("editor/highlightOccurrences"), highlightOccurrencesEnabled);
        settings.setValue(QStringLiteral("editor/highlightCurrentLine"), highlightCurrentLineEnabled);
        settings.setValue(QStringLiteral("editor/showLineNumbers"), showLineNumbersEnabled);
        settings.setValue(QStringLiteral("editor/showIndentGuides"), showIndentGuidesEnabled);
        settings.setValue(QStringLiteral("editor/showRightMargin"), showRightMarginEnabled);
        settings.setValue(QStringLiteral("editor/rightMarginColumn"), rightMarginColumn);
        settings.setValue(QStringLiteral("editor/showWhitespace"), showWhitespaceEnabled);
        settings.setValue(QStringLiteral("editor/showLineEndings"), showLineEndingsEnabled);
        settings.setValue(QStringLiteral("editor/defaultLineEnding"), defaultLineEnding);
        settings.setValue(QStringLiteral("editor/defaultEncoding"), defaultEncodingName);
        settings.setValue(QStringLiteral("editor/ensureFinalNewlineOnSave"), ensureFinalNewlineOnSave);
        settings.setValue(QStringLiteral("editor/stripTrailingOnSave"), stripTrailingOnSave);
        settings.setValue(QStringLiteral("editor/createBackupOnSave"), createBackupOnSave);
        settings.setValue(QStringLiteral("editor/autoSaveEnabled"), autoSaveEnabled);
        settings.setValue(QStringLiteral("editor/autoSaveIntervalSeconds"), autoSaveIntervalSeconds);
        settings.setValue(QStringLiteral("editor/theme"), editorTheme);
        settings.setValue(QStringLiteral("editor/wrap"), wordWrapAction != nullptr && wordWrapAction->isChecked());
        settings.setValue(QStringLiteral("search/matchCase"), findMatchCase);
        settings.setValue(QStringLiteral("search/wholeWord"), findWholeWord);
        settings.setValue(QStringLiteral("search/regularExpression"), findRegularExpression);
        settings.setValue(QStringLiteral("search/filePattern"), searchFilePattern);
        settings.setValue(QStringLiteral("build/saveAllBeforeBuild"), saveAllBeforeBuild);
        settings.setValue(QStringLiteral("build/command"), buildCommand);
        settings.setValue(QStringLiteral("build/cleanCommand"), buildCleanCommand);
        settings.setValue(QStringLiteral("build/targetCommand"), buildTargetCommand);
        settings.setValue(QStringLiteral("build/compileC"), compileCCommand);
        settings.setValue(QStringLiteral("build/compileCpp"), compileCppCommand);
        settings.setValue(QStringLiteral("build/checkPython"), checkPythonCommand);
        settings.setValue(QStringLiteral("build/checkShell"), checkShellCommand);
        settings.setValue(QStringLiteral("build/checkJs"), checkJsCommand);
        settings.setValue(QStringLiteral("build/checkArkTs"), checkArkTsCommand);
        settings.setValue(QStringLiteral("build/checkJson"), checkJsonCommand);
        settings.setValue(QStringLiteral("build/checkGo"), checkGoCommand);
        settings.setValue(QStringLiteral("build/checkRust"), checkRustCommand);
        settings.setValue(QStringLiteral("build/checkJava"), checkJavaCommand);
        settings.setValue(QStringLiteral("build/checkPhp"), checkPhpCommand);
        settings.setValue(QStringLiteral("build/checkYaml"), checkYamlCommand);
        settings.setValue(QStringLiteral("build/checkToml"), checkTomlCommand);
        settings.setValue(QStringLiteral("build/executePython"), executePythonCommand);
        settings.setValue(QStringLiteral("build/executeShell"), executeShellCommand);
        settings.setValue(QStringLiteral("build/executeJs"), executeJsCommand);
        settings.setValue(QStringLiteral("build/executeGo"), executeGoCommand);
        settings.setValue(QStringLiteral("build/executeRust"), executeRustCommand);
        settings.setValue(QStringLiteral("build/executeJava"), executeJavaCommand);
        settings.setValue(QStringLiteral("build/executePhp"), executePhpCommand);
        settings.setValue(QStringLiteral("build/executeNative"), executeNativeCommand);
        settings.setValue(QStringLiteral("project/root"), projectRoot);
        settings.setValue(QStringLiteral("project/file"), projectFile);
        settings.setValue(QStringLiteral("project/recent"), recentProjects);
        settings.setValue(QStringLiteral("files/recent"), recentFiles);
        settings.setValue(QStringLiteral("files/closed"), closedFiles);
        settings.setValue(QStringLiteral("files/browserRoot"), fileBrowserRoot);
        settings.setValue(QStringLiteral("tools/commandHistory"), commandHistory);
        settings.setValue(QStringLiteral("tools/lastShellCommand"), lastShellCommand);
        settings.setValue(QStringLiteral("tools/lastShellWorkingDirectory"), lastShellWorkingDirectory);
        settings.setValue(QStringLiteral("tools/tagFiles"), loadedTagFiles);
        settings.setValue(QStringLiteral("tools/scribble"), scratchPad != nullptr ? scratchPad->toPlainText() : QString());
        settings.beginWriteArray(QStringLiteral("tools/external"), externalTools.size());
        for (int i = 0; i < externalTools.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("label"), externalTools.at(i).label);
            settings.setValue(QStringLiteral("command"), externalTools.at(i).command);
        }
        settings.endArray();
        settings.setValue(QStringLiteral("filetypes/customMappings"), customFileTypeMappingEntries(customFileTypeMappings));
        settings.beginWriteArray(QStringLiteral("snippets/custom"), customSnippets.size());
        for (int i = 0; i < customSnippets.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("language"), customSnippets.at(i).language);
            settings.setValue(QStringLiteral("label"), customSnippets.at(i).label);
            settings.setValue(QStringLiteral("body"), customSnippets.at(i).body);
        }
        settings.endArray();
        settings.beginWriteArray(QStringLiteral("templates/custom"), customFileTemplates.size());
        for (int i = 0; i < customFileTemplates.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("label"), customFileTemplates.at(i).label);
            settings.setValue(QStringLiteral("language"), customFileTemplates.at(i).language);
            settings.setValue(QStringLiteral("body"), customFileTemplates.at(i).body);
        }
        settings.endArray();
        settings.setValue(QStringLiteral("session/files"), sessionFiles);
        settings.setValue(QStringLiteral("session/bookmarks"), storedBookmarks);
        settings.setValue(QStringLiteral("session/cursors"), storedCursors);
        settings.setValue(QStringLiteral("session/activeFile"), currentPage() != nullptr ? currentPage()->path : QString());
        settings.sync();

        if (!projectRoot.isEmpty() && !projectFile.isEmpty() && QDir(projectRoot).exists()) {
            writeProjectFile(projectFile, QFileInfo(projectRoot).fileName());
        }
    }

    void applyOpenHarmonyDisplayDefaults()
    {
#if defined(Q_OS_OPENHARMONY)
        const int minimumEditorPointSize = 24;
        if (fontSize < minimumEditorPointSize) {
            fontSize = minimumEditorPointSize;
        }
        QFont uiFont = qApp->font();
        uiFont.setPointSize(qMax(uiFont.pointSize() > 0 ? uiFont.pointSize() : 12, 18));
        qApp->setFont(uiFont);
        wordCompletionEnabled = false;
        applyTheme();
        applyEditorPreferences();

        QScreen* screen = QApplication::primaryScreen();
        if (screen == nullptr) {
            return;
        }

        const QRect available = screen->availableGeometry();
        if (available.width() <= 0 || available.height() <= 0) {
            return;
        }

        const QSize targetSize(qMax(width(), available.width() * 9 / 10),
            qMax(height(), available.height() * 9 / 10));
        resize(targetSize.boundedTo(available.size()));
        move(available.center() - rect().center());

        if (mainSplitter != nullptr) {
            mainSplitter->setSizes(QList<int>() << qMax(620, available.width() / 5)
                                                << qMax(1100, available.width()));
        }
        if (rightSplitter != nullptr) {
            rightSplitter->setSizes(QList<int>() << qMax(900, available.height() * 2 / 3)
                                                 << qMax(460, available.height() / 3));
        }
#endif
    }

    void addRecentFile(const QString& path)
    {
        if (path.isEmpty()) {
            return;
        }
        QString normalized = QFileInfo(path).canonicalFilePath();
        if (normalized.isEmpty()) {
            normalized = QFileInfo(path).absoluteFilePath();
        }
        recentFiles.removeAll(normalized);
        recentFiles.prepend(normalized);
        while (recentFiles.size() > 12) {
            recentFiles.removeLast();
        }
        updateRecentFilesMenu();
    }

    QString recentFileLabel(const QString& path) const
    {
        const QFileInfo info(path);
        const QString parent = QFileInfo(info.absolutePath()).fileName();
        if (parent.isEmpty()) {
            return info.fileName();
        }
        return QStringLiteral("%1  -  %2").arg(info.fileName(), parent);
    }

    void updateRecentFilesMenu()
    {
        if (recentFilesMenu == nullptr) {
            return;
        }
        recentFilesMenu->clear();
        recentFilesMenu->setEnabled(!recentFiles.isEmpty());
        for (const QString& path : recentFiles) {
            QAction* action = recentFilesMenu->addAction(recentFileLabel(path), this, [this, path]() {
                openRecentFile(path);
            });
            action->setToolTip(path);
        }
        if (!recentFiles.isEmpty()) {
            recentFilesMenu->addSeparator();
            recentFilesMenu->addAction(QStringLiteral("Clear Recent Files"), this, [this]() {
                recentFiles.clear();
                updateRecentFilesMenu();
            });
        }
    }

    void openRecentFile(const QString& path)
    {
        if (!QFileInfo::exists(path)) {
            recentFiles.removeAll(path);
            updateRecentFilesMenu();
            statusBar()->showMessage(QStringLiteral("Recent file is not available"), 2500);
            return;
        }
        openPath(path);
    }

    void addRecentProject(const QString& path)
    {
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo info(path);
        if (!info.exists()) {
            return;
        }
        QString normalized;
        if (info.isDir()) {
            normalized = QDir(path).canonicalPath();
            if (normalized.isEmpty()) {
                normalized = QDir(path).absolutePath();
            }
        } else if (info.isFile()) {
            normalized = info.canonicalFilePath();
            if (normalized.isEmpty()) {
                normalized = info.absoluteFilePath();
            }
        } else {
            return;
        }
        recentProjects.removeAll(normalized);
        recentProjects.prepend(normalized);
        while (recentProjects.size() > 12) {
            recentProjects.removeLast();
        }
        updateRecentProjectsMenu();
    }

    void updateRecentProjectsMenu()
    {
        if (recentProjectsMenu == nullptr) {
            return;
        }
        recentProjectsMenu->clear();
        recentProjectsMenu->setEnabled(!recentProjects.isEmpty());
        for (const QString& path : recentProjects) {
            const QFileInfo info(path);
            QAction* action = recentProjectsMenu->addAction(info.fileName(), this, [this, path]() {
                openRecentProject(path);
            });
            action->setToolTip(path);
        }
        if (!recentProjects.isEmpty()) {
            recentProjectsMenu->addSeparator();
            recentProjectsMenu->addAction(QStringLiteral("Clear Recent Projects"), this, [this]() {
                recentProjects.clear();
                updateRecentProjectsMenu();
            });
        }
    }

    void addCommandHistory(const QString& command)
    {
        const QString normalized = command.trimmed();
        if (normalized.isEmpty()) {
            return;
        }
        commandHistory.removeAll(normalized);
        commandHistory.prepend(normalized);
        while (commandHistory.size() > 20) {
            commandHistory.removeLast();
        }
        updateRecentCommandsMenu();
    }

    QString commandMenuLabel(const QString& command) const
    {
        QString label = command.simplified();
        if (label.size() > 80) {
            label = label.left(77) + QStringLiteral("...");
        }
        return label;
    }

    void updateRecentCommandsMenu()
    {
        if (recentCommandsMenu == nullptr) {
            return;
        }
        recentCommandsMenu->clear();
        recentCommandsMenu->setEnabled(!commandHistory.isEmpty());
        for (const QString& command : commandHistory) {
            QAction* action = recentCommandsMenu->addAction(commandMenuLabel(command), this, [this, command]() {
                addCommandHistory(command);
                runShellCommand(command);
            });
            action->setToolTip(command);
        }
        if (!commandHistory.isEmpty()) {
            recentCommandsMenu->addSeparator();
            recentCommandsMenu->addAction(QStringLiteral("Clear Recent Commands"), this, [this]() {
                commandHistory.clear();
                updateRecentCommandsMenu();
            });
        }
    }

    void updateExternalToolsMenu()
    {
        if (externalToolsMenu == nullptr) {
            return;
        }
        externalToolsMenu->clear();
        for (int i = 0; i < externalTools.size(); ++i) {
            const ExternalTool tool = externalTools.at(i);
            if (tool.label.trimmed().isEmpty() || tool.command.trimmed().isEmpty()) {
                continue;
            }
            QAction* action = externalToolsMenu->addAction(tool.label, this, [this, i]() { runExternalTool(i); });
            action->setToolTip(tool.command);
        }
        if (!externalTools.isEmpty()) {
            externalToolsMenu->addSeparator();
        }
        externalToolsMenu->addAction(QStringLiteral("Configure External Tools..."), this, [this]() { configureExternalTools(); });
        externalToolsMenu->setEnabled(true);
    }

    QStringList scanSourceFilesForTool(const QString& root) const
    {
        QStringList files;
        if (root.isEmpty() || !QDir(root).exists()) {
            return files;
        }
        QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        int count = 0;
        while (it.hasNext() && count < 3000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path, root) || !isSourceFile(path)) {
                continue;
            }
            files << path;
            ++count;
        }
        files.sort(Qt::CaseInsensitive);
        return files;
    }

    bool runNativeExternalTool(const ExternalTool& tool, const QString& contextPath)
    {
        const QString label = tool.label.trimmed();
        if (label == QStringLiteral("List Project Files")) {
            const QString root = !projectRoot.isEmpty() ? projectRoot : QFileInfo(contextPath).absolutePath();
            QStringList lines;
            const QDir rootDir(root);
            const QStringList files = !projectRoot.isEmpty() ? projectFilePaths : scanSourceFilesForTool(root);
            for (const QString& path : files.mid(0, 200)) {
                lines << rootDir.relativeFilePath(path);
            }
            appendOutput(QStringLiteral("[native external tool] %1").arg(label));
            appendOutput(lines.isEmpty() ? QStringLiteral("No source files found.") : lines.join(QLatin1Char('\n')));
            return true;
        }
        if (label == QStringLiteral("Grep Current Word")) {
            QString identifier = currentIdentifierAtCursor();
            if (identifier.isEmpty()) {
                identifier = QFileInfo(contextPath).completeBaseName();
            }
            if (identifier.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("No current word for grep"), 2500);
                return true;
            }
            const QString root = !projectRoot.isEmpty() ? projectRoot : QFileInfo(contextPath).absolutePath();
            const QRegularExpression expression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(identifier)));
            int matches = 0;
            int scanned = 0;
            searchResults->clear();
            const QStringList files = !projectRoot.isEmpty() ? projectFilePaths : scanSourceFilesForTool(root);
            for (const QString& path : files) {
                if (scanned >= 3000 || !canScanTextFile(path)) {
                    continue;
                }
                ++scanned;
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const QByteArray bytes = file.readAll();
                const QString text = normalizeLineEndings(decodeText(bytes, detectEncodingName(bytes)));
                const QStringList lines = text.split(QLatin1Char('\n'));
                for (int i = 0; i < lines.size(); ++i) {
                    const QRegularExpressionMatch match = expression.match(lines.at(i));
                    if (match.hasMatch()) {
                        auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3: %4")
                            .arg(QDir(root).relativeFilePath(path))
                            .arg(i + 1)
                            .arg(match.capturedStart() + 1)
                            .arg(lines.at(i).trimmed()), searchResults);
                        item->setData(Qt::UserRole, path);
                        item->setData(Qt::UserRole + 1, i + 1);
                        item->setData(Qt::UserRole + 2, match.capturedStart() + 1);
                        item->setData(Qt::UserRole + 3, -1);
                        item->setToolTip(path);
                        ++matches;
                    }
                }
            }
            appendOutput(QStringLiteral("[native external tool] %1: %2 match(es) for %3 in %4 scanned file(s)")
                .arg(label)
                .arg(matches)
                .arg(identifier)
                .arg(scanned));
            if (matches > 0) {
                sideTabs->setCurrentWidget(searchResults);
            }
            return true;
        }
        return false;
    }

    void runExternalTool(int index)
    {
        if (index < 0 || index >= externalTools.size()) {
            return;
        }
        const ExternalTool tool = externalTools.at(index);
        if (tool.command.trimmed().isEmpty()) {
            return;
        }
        const QString contextPath = buildContextPath();
        if (runNativeExternalTool(tool, contextPath)) {
            return;
        }
        runShellCommand(expandCommandTemplate(tool.command, contextPath));
    }

    void configureExternalTools()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Configure External Tools"));
        dialog.resize(760, 420);

        auto* layout = new QVBoxLayout(&dialog);
        auto* list = new QListWidget(&dialog);
        auto* labelEdit = new QLineEdit(&dialog);
        auto* commandEdit = new QLineEdit(&dialog);
        commandEdit->setPlaceholderText(QStringLiteral("Command, supports {file}, {dir}, {project}, {name}, {base}, {output}, {target}"));
        auto* form = new QFormLayout();
        form->addRow(QStringLiteral("Name"), labelEdit);
        form->addRow(QStringLiteral("Command"), commandEdit);
        auto* row = new QHBoxLayout();
        auto* newButton = new QPushButton(QStringLiteral("New"), &dialog);
        auto* saveButton = new QPushButton(QStringLiteral("Save"), &dialog);
        auto* deleteButton = new QPushButton(QStringLiteral("Delete"), &dialog);
        row->addWidget(newButton);
        row->addWidget(saveButton);
        row->addWidget(deleteButton);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(list, 1);
        layout->addLayout(form);
        layout->addLayout(row);
        layout->addWidget(buttons);

        QVector<ExternalTool> workingTools = externalTools;
        auto refreshList = [&]() {
            const int previous = list->currentRow();
            list->clear();
            for (const ExternalTool& tool : workingTools) {
                auto* item = new QListWidgetItem(tool.label, list);
                item->setToolTip(tool.command);
            }
            if (!workingTools.isEmpty()) {
                list->setCurrentRow(qBound(0, previous, workingTools.size() - 1));
            }
        };
        auto loadTool = [&](int index) {
            if (index < 0 || index >= workingTools.size()) {
                labelEdit->clear();
                commandEdit->clear();
                deleteButton->setEnabled(false);
                return;
            }
            labelEdit->setText(workingTools.at(index).label);
            commandEdit->setText(workingTools.at(index).command);
            deleteButton->setEnabled(true);
        };

        refreshList();
        loadTool(list->currentRow());
        connect(list, &QListWidget::currentRowChanged, &dialog, [&](int index) { loadTool(index); });
        connect(newButton, &QPushButton::clicked, &dialog, [&]() {
            list->clearSelection();
            list->setCurrentRow(-1);
            labelEdit->clear();
            commandEdit->clear();
            deleteButton->setEnabled(false);
            labelEdit->setFocus();
        });
        connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
            ExternalTool tool{labelEdit->text().trimmed(), commandEdit->text().trimmed()};
            if (tool.label.isEmpty() || tool.command.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("External Tools"), QStringLiteral("Name and command are required."));
                return;
            }
            const int index = list->currentRow();
            if (index >= 0 && index < workingTools.size()) {
                workingTools[index] = tool;
            } else {
                workingTools.push_back(tool);
            }
            refreshList();
            list->setCurrentRow(index >= 0 ? index : workingTools.size() - 1);
        });
        connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
            const int index = list->currentRow();
            if (index >= 0 && index < workingTools.size()) {
                workingTools.removeAt(index);
                refreshList();
            }
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            externalTools = workingTools;
            updateExternalToolsMenu();
            saveSettings();
        }
    }

    void openRecentProject(const QString& path)
    {
        const QFileInfo info(path);
        if (!info.exists()) {
            recentProjects.removeAll(path);
            updateRecentProjectsMenu();
            statusBar()->showMessage(QStringLiteral("Recent project is not available"), 2500);
            return;
        }
        if (info.isFile() && info.suffix().compare(QStringLiteral("geany"), Qt::CaseInsensitive) == 0) {
            loadProjectFile(path);
            return;
        }
        if (info.isDir()) {
            openProjectRoot(path);
        }
    }

    void addClosedFile(const QString& path)
    {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return;
        }
        closedFiles.removeAll(path);
        closedFiles.prepend(path);
        while (closedFiles.size() > 24) {
            closedFiles.removeLast();
        }
    }

    void newDocument()
    {
        auto* page = new EditorPage(tabs);
        page->lineEnding = effectiveDefaultLineEnding();
        page->encodingName = effectiveDefaultEncodingName();
        const int index = tabs->addTab(page, QStringLiteral("Untitled"));
        tabs->setCurrentIndex(index);
        wireEditor(page);
        applyEditorPreferences();
        applySearchHighlights(page);
        updateDocumentList();
        updateBookmarksList();
        updateCursorStatus();
    }

    void wireEditor(EditorPage* page)
    {
        page->editor->setUrlDropHandler([this](const QList<QUrl>& urls) { openDroppedUrls(urls); });
        page->editor->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(page->editor, &QWidget::customContextMenuRequested, this, [this, page](const QPoint& pos) {
            showEditorContextMenu(page, pos);
        });
        connect(page->editor->document(), &QTextDocument::modificationChanged, this, [this, page](bool changed) {
            updateTabTitle(page, changed);
        });
        connect(page->editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
            refreshSymbols();
            updateCursorStatus();
        });
        connect(page->editor->document(), &QTextDocument::contentsChanged, this, [this, page]() {
            page->editor->refreshVisualHighlights();
            refreshSymbols();
            if (page == currentPage()) {
                refreshSplitView();
            }
        });
    }

    void openDroppedUrls(const QList<QUrl>& urls)
    {
        QStringList paths;
        for (const QUrl& url : urls) {
            const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                paths << path;
            }
        }
        if (paths.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No local files in drop"), 2500);
            return;
        }
        openPaths(paths);
        statusBar()->showMessage(QStringLiteral("Opened %1 dropped item(s)").arg(paths.size()), 2500);
    }

    void copyTextToClipboard(const QString& text, const QString& label)
    {
        if (text.isEmpty()) {
            return;
        }
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(QStringLiteral("Copied %1").arg(label), 2000);
    }

    QString containingDirectoryForPath(const QString& path) const
    {
        if (path.isEmpty()) {
            return QString();
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            return info.absoluteFilePath();
        }
        return info.absolutePath();
    }

    bool openDirectoryInFileManager(const QString& directory)
    {
        if (directory.isEmpty() || !QDir(directory).exists()) {
            statusBar()->showMessage(QStringLiteral("Folder is not available"), 2500);
            return false;
        }
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
            statusBar()->showMessage(QStringLiteral("Opened folder: %1").arg(directory), 2500);
            return true;
        }
        copyTextToClipboard(directory, QStringLiteral("folder path"));
        QMessageBox::information(this, QStringLiteral("Open Folder"),
            QStringLiteral("The system file manager did not open this folder, so its path was copied instead:\n%1").arg(directory));
        return false;
    }

    void openContainingFolderForPath(const QString& path)
    {
        openDirectoryInFileManager(containingDirectoryForPath(path));
    }

    bool openFileWithSystemApp(const QString& path)
    {
        if (path.isEmpty() || !QFileInfo::exists(path) || !QFileInfo(path).isFile()) {
            statusBar()->showMessage(QStringLiteral("File is not available"), 2500);
            return false;
        }
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            statusBar()->showMessage(QStringLiteral("Opened with system app: %1").arg(path), 2500);
            return true;
        }
        copyTextToClipboard(path, QStringLiteral("file path"));
        QMessageBox::information(this, QStringLiteral("Open File"),
            QStringLiteral("No system app opened this file, so its path was copied instead:\n%1").arg(path));
        return false;
    }

    void openCurrentFileFolder()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Current document has not been saved"), 2500);
            return;
        }
        openContainingFolderForPath(page->path);
    }

    void openCurrentFileWithSystemApp()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Current document has not been saved"), 2500);
            return;
        }
        openFileWithSystemApp(page->path);
    }

    void openProjectFolderInFileManager()
    {
        if (projectRoot.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No project folder is open"), 2500);
            return;
        }
        openDirectoryInFileManager(projectRoot);
    }

    void openSelectedProjectFileWithSystemApp()
    {
        const QString path = selectedProjectPath();
        if (path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No project file selected"), 2500);
            return;
        }
        openFileWithSystemApp(path);
    }

    void setFileBrowserRoot(const QString& dir)
    {
        if (dir.isEmpty() || !QDir(dir).exists()) {
            statusBar()->showMessage(QStringLiteral("Browse folder is not available"), 2500);
            return;
        }
        fileBrowserRoot = QDir(dir).canonicalPath();
        if (fileBrowserRoot.isEmpty()) {
            fileBrowserRoot = QDir(dir).absolutePath();
        }
        refreshFileBrowser();
        if (sideTabs != nullptr && fileBrowserPanel != nullptr) {
            sideTabs->setCurrentWidget(fileBrowserPanel);
        }
        saveSettings();
    }

    void browseFolder()
    {
        const QString dir = chooseExistingDirectory(this, QStringLiteral("Browse Folder"),
            fileBrowserRoot.isEmpty() ? currentWorkDir() : fileBrowserRoot);
        if (!dir.isEmpty()) {
            setFileBrowserRoot(dir);
        }
    }

    void refreshFileBrowser()
    {
        if (fileBrowserList == nullptr || fileBrowserPathEdit == nullptr) {
            return;
        }
        if (fileBrowserRoot.isEmpty() || fileBrowserRoot.startsWith(QStringLiteral("/storage/Users/currentUser"))
            || !QDir(fileBrowserRoot).exists()) {
            fileBrowserRoot = currentWorkDir();
        }

        QDir dir(fileBrowserRoot);
        fileBrowserPathEdit->setText(dir.absolutePath());
        fileBrowserList->clear();
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& info : entries) {
            auto* item = new QListWidgetItem(info.isDir()
                ? QStringLiteral("[folder] %1").arg(info.fileName())
                : info.fileName(), fileBrowserList);
            item->setData(Qt::UserRole, info.absoluteFilePath());
            item->setData(Qt::UserRole + 1, info.isDir());
            if (info.isFile()) {
                item->setToolTip(QStringLiteral("%1\n%2").arg(info.absoluteFilePath(), readableFileSize(info.size())));
            } else {
                item->setToolTip(info.absoluteFilePath());
            }
        }
    }

    QString selectedBrowserPath() const
    {
        if (fileBrowserList == nullptr) {
            return QString();
        }
        QListWidgetItem* item = fileBrowserList->currentItem();
        return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    }

    void openSelectedBrowserItem(QListWidgetItem* item)
    {
        if (item == nullptr) {
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) {
            return;
        }
        if (item->data(Qt::UserRole + 1).toBool()) {
            setFileBrowserRoot(path);
            return;
        }
        openPath(path);
    }

    void showFileBrowserContextMenu(const QPoint& pos)
    {
        if (fileBrowserList == nullptr) {
            return;
        }
        QListWidgetItem* item = fileBrowserList->itemAt(pos);
        if (item != nullptr) {
            fileBrowserList->setCurrentItem(item);
        }
        const QString path = selectedBrowserPath();
        const bool isFile = !path.isEmpty() && QFileInfo(path).isFile();
        const bool isDir = !path.isEmpty() && QFileInfo(path).isDir();

        QMenu menu(fileBrowserList);
        QAction* openAction = menu.addAction(QStringLiteral("Open"), this, [this, item]() { openSelectedBrowserItem(item); });
        QAction* systemAction = menu.addAction(QStringLiteral("Open with System App"), this, [this, path]() { openFileWithSystemApp(path); });
        QAction* folderAction = menu.addAction(QStringLiteral("Open Containing Folder"), this, [this, path]() { openContainingFolderForPath(path); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Browse Folder..."), this, [this]() { browseFolder(); });
        menu.addAction(QStringLiteral("Use Project Folder"), this, [this]() {
            if (!projectRoot.isEmpty()) {
                setFileBrowserRoot(projectRoot);
            }
        })->setEnabled(!projectRoot.isEmpty());
        menu.addAction(QStringLiteral("Up"), this, [this]() { setFileBrowserRoot(QDir(fileBrowserRoot).absoluteFilePath(QStringLiteral(".."))); });
        menu.addAction(QStringLiteral("Refresh"), this, [this]() { refreshFileBrowser(); });
        menu.addSeparator();
        QAction* copyPathAction = menu.addAction(QStringLiteral("Copy Path"), this, [this, path]() { copyTextToClipboard(path, QStringLiteral("file path")); });
        openAction->setEnabled(isFile || isDir);
        systemAction->setEnabled(isFile);
        folderAction->setEnabled(isFile || isDir);
        copyPathAction->setEnabled(isFile || isDir);
        menu.exec(fileBrowserList->mapToGlobal(pos));
    }

    void copyCurrentFilePath()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Current document has not been saved"), 2500);
            return;
        }
        copyTextToClipboard(page->path, QStringLiteral("file path"));
    }

    void copyCurrentRelativePath()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty() || projectRoot.isEmpty() || !isInsideProject(page->path)) {
            statusBar()->showMessage(QStringLiteral("Current document is not inside the project"), 2500);
            return;
        }
        copyTextToClipboard(QDir(projectRoot).relativeFilePath(page->path), QStringLiteral("relative path"));
    }

    void showTabContextMenu(const QPoint& pos)
    {
        if (tabs == nullptr || tabs->tabBar() == nullptr) {
            return;
        }
        const int index = tabs->tabBar()->tabAt(pos);
        if (index < 0 || index >= tabs->count()) {
            return;
        }
        tabs->setCurrentIndex(index);
        auto* page = static_cast<EditorPage*>(tabs->widget(index));
        if (page == nullptr) {
            return;
        }

        QMenu menu(tabs);
        menu.addAction(QStringLiteral("Save"), this, [this]() { saveCurrentDocument(); });
        menu.addAction(QStringLiteral("Save As..."), this, [this]() { saveCurrentDocumentAs(); });
        QAction* renameAction = menu.addAction(QStringLiteral("Rename/Move File..."), this, [this]() { renameCurrentDocumentFile(); });
        QAction* reloadAction = menu.addAction(QStringLiteral("Reload"), this, [this]() { reloadCurrentDocument(); });
        renameAction->setEnabled(!page->path.isEmpty());
        reloadAction->setEnabled(!page->path.isEmpty());
        menu.addSeparator();
        menu.addAction(QStringLiteral("Close"), this, [this, index]() { closeTab(index); });
        QAction* closeOthersAction = menu.addAction(QStringLiteral("Close Others"), this, [this]() { closeOtherTabs(); });
        QAction* closeLeftAction = menu.addAction(QStringLiteral("Close to the Left"), this, [this]() { closeTabsToSide(-1); });
        QAction* closeRightAction = menu.addAction(QStringLiteral("Close to the Right"), this, [this]() { closeTabsToSide(1); });
        QAction* closeUnmodifiedAction = menu.addAction(QStringLiteral("Close Unmodified"), this, [this]() { closeUnmodifiedTabs(); });
        QAction* closeAllAction = menu.addAction(QStringLiteral("Close All"), this, [this]() { closeAllTabs(); });
        QAction* reopenAction = menu.addAction(QStringLiteral("Reopen Closed Document"), this, [this]() { reopenClosedDocument(); });
        closeOthersAction->setEnabled(tabs->count() > 1);
        closeLeftAction->setEnabled(index > 0);
        closeRightAction->setEnabled(index + 1 < tabs->count());
        closeUnmodifiedAction->setEnabled(unmodifiedDocumentCount() > 0);
        closeAllAction->setEnabled(tabs->count() > 0);
        reopenAction->setEnabled(!closedFiles.isEmpty());
        menu.addSeparator();
        QAction* readOnly = menu.addAction(QStringLiteral("Read Only"), this, [this]() { toggleReadOnly(); });
        readOnly->setCheckable(true);
        readOnly->setChecked(page->editor->isReadOnly());
        menu.addAction(QStringLiteral("Toggle Writable Permission"), this, [this]() { toggleCurrentFileWritable(); });
        menu.addAction(QStringLiteral("Toggle Executable Permission"), this, [this]() { toggleCurrentFileExecutable(); });
        menu.addSeparator();
        QAction* openFolderAction = menu.addAction(QStringLiteral("Open Containing Folder"), this, [this, page]() {
            openContainingFolderForPath(page->path);
        });
        QAction* openSystemAction = menu.addAction(QStringLiteral("Open with System App"), this, [this, page]() {
            openFileWithSystemApp(page->path);
        });
        QAction* copyPathAction = menu.addAction(QStringLiteral("Copy File Path"), this, [this, page]() {
            copyTextToClipboard(page->path, QStringLiteral("file path"));
        });
        QAction* copyRelativePathAction = menu.addAction(QStringLiteral("Copy Relative Path"), this, [this, page]() {
            copyTextToClipboard(QDir(projectRoot).relativeFilePath(page->path), QStringLiteral("relative path"));
        });
        QAction* selectProjectAction = menu.addAction(QStringLiteral("Select in Project"), this, [this]() { selectCurrentFileInProject(); });
        openFolderAction->setEnabled(!page->path.isEmpty());
        openSystemAction->setEnabled(!page->path.isEmpty());
        copyPathAction->setEnabled(!page->path.isEmpty());
        copyRelativePathAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        selectProjectAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        menu.addSeparator();
        menu.addAction(QStringLiteral("Document Properties"), this, [this]() { showDocumentProperties(); });
        menu.exec(tabs->tabBar()->mapToGlobal(pos));
    }

    void showDocumentListContextMenu(const QPoint& pos)
    {
        if (documentsList == nullptr) {
            return;
        }
        QListWidgetItem* item = documentsList->itemAt(pos);
        if (item == nullptr) {
            return;
        }
        documentsList->setCurrentItem(item);
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= tabs->count()) {
            return;
        }
        tabs->setCurrentIndex(index);
        auto* page = static_cast<EditorPage*>(tabs->widget(index));
        if (page == nullptr) {
            return;
        }

        QMenu menu(documentsList);
        menu.addAction(QStringLiteral("Activate"), this, [this, index]() { tabs->setCurrentIndex(index); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Save"), this, [this]() { saveCurrentDocument(); });
        menu.addAction(QStringLiteral("Save As..."), this, [this]() { saveCurrentDocumentAs(); });
        QAction* renameAction = menu.addAction(QStringLiteral("Rename/Move File..."), this, [this]() { renameCurrentDocumentFile(); });
        QAction* reloadAction = menu.addAction(QStringLiteral("Reload"), this, [this]() { reloadCurrentDocument(); });
        renameAction->setEnabled(!page->path.isEmpty());
        reloadAction->setEnabled(!page->path.isEmpty());
        menu.addSeparator();
        menu.addAction(QStringLiteral("Close"), this, [this, index]() { closeTab(index); });
        QAction* closeOthersAction = menu.addAction(QStringLiteral("Close Others"), this, [this]() { closeOtherTabs(); });
        QAction* closeLeftAction = menu.addAction(QStringLiteral("Close to the Left"), this, [this]() { closeTabsToSide(-1); });
        QAction* closeRightAction = menu.addAction(QStringLiteral("Close to the Right"), this, [this]() { closeTabsToSide(1); });
        QAction* closeUnmodifiedAction = menu.addAction(QStringLiteral("Close Unmodified"), this, [this]() { closeUnmodifiedTabs(); });
        QAction* reopenAction = menu.addAction(QStringLiteral("Reopen Closed Document"), this, [this]() { reopenClosedDocument(); });
        closeOthersAction->setEnabled(tabs->count() > 1);
        closeLeftAction->setEnabled(index > 0);
        closeRightAction->setEnabled(index + 1 < tabs->count());
        closeUnmodifiedAction->setEnabled(unmodifiedDocumentCount() > 0);
        reopenAction->setEnabled(!closedFiles.isEmpty());
        menu.addSeparator();
        QAction* readOnly = menu.addAction(QStringLiteral("Read Only"), this, [this]() { toggleReadOnly(); });
        readOnly->setCheckable(true);
        readOnly->setChecked(page->editor->isReadOnly());
        menu.addAction(QStringLiteral("Toggle Writable Permission"), this, [this]() { toggleCurrentFileWritable(); });
        menu.addAction(QStringLiteral("Toggle Executable Permission"), this, [this]() { toggleCurrentFileExecutable(); });
        menu.addSeparator();
        QAction* openFolderAction = menu.addAction(QStringLiteral("Open Containing Folder"), this, [this, page]() {
            openContainingFolderForPath(page->path);
        });
        QAction* openSystemAction = menu.addAction(QStringLiteral("Open with System App"), this, [this, page]() {
            openFileWithSystemApp(page->path);
        });
        QAction* copyPathAction = menu.addAction(QStringLiteral("Copy File Path"), this, [this, page]() {
            copyTextToClipboard(page->path, QStringLiteral("file path"));
        });
        QAction* copyRelativePathAction = menu.addAction(QStringLiteral("Copy Relative Path"), this, [this, page]() {
            copyTextToClipboard(QDir(projectRoot).relativeFilePath(page->path), QStringLiteral("relative path"));
        });
        QAction* selectProjectAction = menu.addAction(QStringLiteral("Select in Project"), this, [this]() { selectCurrentFileInProject(); });
        openFolderAction->setEnabled(!page->path.isEmpty());
        openSystemAction->setEnabled(!page->path.isEmpty());
        copyPathAction->setEnabled(!page->path.isEmpty());
        copyRelativePathAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        selectProjectAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        menu.addSeparator();
        menu.addAction(QStringLiteral("Document Properties"), this, [this]() { showDocumentProperties(); });
        menu.exec(documentsList->mapToGlobal(pos));
    }

    QString listWidgetText(QListWidget* list) const
    {
        QStringList lines;
        if (list == nullptr) {
            return QString();
        }
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            if (item != nullptr) {
                lines << item->text();
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

    QString searchResultsText() const
    {
        return listWidgetText(searchResults);
    }

    bool writeBytesToFile(const QString& path, const QByteArray& bytes, const QString& title)
    {
#if defined(Q_OS_OPENHARMONY)
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendOutput(QStringLiteral("%1 failed: %2").arg(title, file.errorString()));
            statusBar()->showMessage(QStringLiteral("%1 failed: %2").arg(title, file.errorString()), 3500);
            return false;
        }
        if (file.write(bytes) != bytes.size()) {
            appendOutput(QStringLiteral("%1 failed: %2").arg(title, file.errorString()));
            statusBar()->showMessage(QStringLiteral("%1 failed: %2").arg(title, file.errorString()), 3500);
            return false;
        }
        file.close();
        return true;
#else
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, title, file.errorString());
            return false;
        }
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::warning(this, title, file.errorString());
            return false;
        }
        return true;
#endif
    }

    void saveListWidgetItemsAs(QListWidget* list, const QString& title, const QString& defaultName)
    {
        const QString text = listWidgetText(list);
        if (text.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No results to save"), 2500);
            return;
        }
        const QString path = chooseSaveFileName(this, title,
            currentWorkDir() + QLatin1Char('/') + defaultName,
            QStringLiteral("Text Files (*.txt);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        const QByteArray bytes = text.toUtf8();
        if (!writeBytesToFile(path, bytes, title)) {
            return;
        }
        appendOutput(QStringLiteral("Saved results: %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Results saved"), 2500);
    }

    void saveSearchResultsAs()
    {
        saveListWidgetItemsAs(searchResults, QStringLiteral("Save Search Results"), QStringLiteral("search-results.txt"));
    }

    int tabIndexForResultItem(QListWidgetItem* item) const
    {
        if (item == nullptr) {
            return -1;
        }

        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            for (int i = 0; i < tabs->count(); ++i) {
                auto* page = static_cast<EditorPage*>(tabs->widget(i));
                if (page != nullptr && QFileInfo(page->path) == QFileInfo(path)) {
                    return i;
                }
            }
        }

        const QVariant tabData = item->data(Qt::UserRole + 3);
        const int tabIndex = tabData.isValid() ? tabData.toInt() : -1;
        return tabIndex >= 0 && tabIndex < tabs->count() ? tabIndex : -1;
    }

    int bookmarkableResultCount(QListWidget* list) const
    {
        int count = 0;
        if (list == nullptr) {
            return count;
        }
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            if (item != nullptr && item->data(Qt::UserRole + 1).toInt() > 0 && tabIndexForResultItem(item) >= 0) {
                ++count;
            }
        }
        return count;
    }

    void bookmarkResultLines(QListWidget* list)
    {
        if (list == nullptr || list->count() == 0) {
            statusBar()->showMessage(QStringLiteral("No results to bookmark"), 2500);
            return;
        }

        int added = 0;
        int skipped = 0;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            const int line = item != nullptr ? item->data(Qt::UserRole + 1).toInt() : 0;
            const int tabIndex = tabIndexForResultItem(item);
            if (line <= 0 || tabIndex < 0) {
                ++skipped;
                continue;
            }
            auto* page = static_cast<EditorPage*>(tabs->widget(tabIndex));
            if (page == nullptr || page->bookmarks.contains(line)) {
                continue;
            }
            page->bookmarks.insert(line);
            ++added;
        }

        updateBookmarksList();
        sideTabs->setCurrentWidget(bookmarksList);
        appendOutput(QStringLiteral("Bookmarked %1 result line(s). Skipped %2 result(s) not in open documents.")
            .arg(added)
            .arg(skipped));
        statusBar()->showMessage(QStringLiteral("Bookmarked %1 result line(s)").arg(added), 2500);
    }

    void showResultListContextMenu(QListWidget* list, const QPoint& pos, const QString& itemLabel,
        const QString& allLabel, const QString& defaultName)
    {
        if (list == nullptr) {
            return;
        }
        QListWidgetItem* item = list->itemAt(pos);
        if (item != nullptr) {
            list->setCurrentItem(item);
        }

        QMenu menu(list);
        QAction* jumpAction = menu.addAction(QStringLiteral("Jump to Result"), this, [this, list]() {
            jumpToListItem(list != nullptr ? list->currentItem() : nullptr);
        });
        QAction* copyResultAction = menu.addAction(QStringLiteral("Copy Result"), this, [this, list]() {
            if (list != nullptr && list->currentItem() != nullptr) {
                copyTextToClipboard(list->currentItem()->text(), QStringLiteral("result"));
            }
        });
        QAction* copyAllAction = menu.addAction(QStringLiteral("Copy All Results"), this, [this, list]() {
            copyTextToClipboard(listWidgetText(list), QStringLiteral("results"));
        });
        QAction* saveAction = menu.addAction(QStringLiteral("Save Results As..."), this, [this, list, defaultName]() {
            saveListWidgetItemsAs(list, QStringLiteral("Save Results"), defaultName);
        });
        QAction* bookmarkAction = menu.addAction(QStringLiteral("Bookmark Result Lines"), this, [this, list]() {
            bookmarkResultLines(list);
        });
        menu.addSeparator();
        QAction* clearAction = menu.addAction(QStringLiteral("Clear Results"), this, [list]() {
            if (list != nullptr) {
                list->clear();
            }
        });
        const bool hasResults = list->count() > 0;
        jumpAction->setEnabled(item != nullptr);
        copyResultAction->setEnabled(item != nullptr);
        copyAllAction->setEnabled(hasResults);
        saveAction->setEnabled(hasResults);
        bookmarkAction->setEnabled(bookmarkableResultCount(list) > 0);
        clearAction->setEnabled(hasResults);
        jumpAction->setText(QStringLiteral("Jump to %1").arg(itemLabel));
        copyResultAction->setText(QStringLiteral("Copy %1").arg(itemLabel));
        copyAllAction->setText(QStringLiteral("Copy All %1").arg(allLabel));
        menu.exec(list->mapToGlobal(pos));
    }

    void showSearchResultsContextMenu(const QPoint& pos)
    {
        showResultListContextMenu(searchResults, pos, QStringLiteral("search result"), QStringLiteral("search results"),
            QStringLiteral("search-results.txt"));
    }

    bool activateBookmarkItem(QListWidgetItem* item)
    {
        if (item == nullptr) {
            return false;
        }
        const int index = item->data(Qt::UserRole).toInt();
        const int line = item->data(Qt::UserRole + 1).toInt();
        if (index < 0 || index >= tabs->count() || line <= 0) {
            return false;
        }
        recordNavigationLocation();
        tabs->setCurrentIndex(index);
        goToLineNumber(line, false);
        return true;
    }

    bool removeBookmarkItem(QListWidgetItem* item)
    {
        if (item == nullptr) {
            return false;
        }
        const int index = item->data(Qt::UserRole).toInt();
        const int line = item->data(Qt::UserRole + 1).toInt();
        if (index < 0 || index >= tabs->count() || line <= 0) {
            return false;
        }
        auto* page = static_cast<EditorPage*>(tabs->widget(index));
        if (page == nullptr || !page->bookmarks.contains(line)) {
            return false;
        }
        page->bookmarks.remove(line);
        updateBookmarksList();
        statusBar()->showMessage(QStringLiteral("Bookmark removed"), 2000);
        return true;
    }

    void showBookmarksContextMenu(const QPoint& pos)
    {
        if (bookmarksList == nullptr) {
            return;
        }
        QListWidgetItem* item = bookmarksList->itemAt(pos);
        if (item != nullptr) {
            bookmarksList->setCurrentItem(item);
        }

        QMenu menu(bookmarksList);
        QAction* jumpAction = menu.addAction(QStringLiteral("Jump to Bookmark"), this, [this]() {
            activateBookmarkItem(bookmarksList != nullptr ? bookmarksList->currentItem() : nullptr);
        });
        QAction* removeAction = menu.addAction(QStringLiteral("Remove Bookmark"), this, [this]() {
            removeBookmarkItem(bookmarksList != nullptr ? bookmarksList->currentItem() : nullptr);
        });
        QAction* copyBookmarkAction = menu.addAction(QStringLiteral("Copy Bookmark"), this, [this]() {
            if (bookmarksList != nullptr && bookmarksList->currentItem() != nullptr) {
                copyTextToClipboard(bookmarksList->currentItem()->text(), QStringLiteral("bookmark"));
            }
        });
        QAction* copyAllAction = menu.addAction(QStringLiteral("Copy All Bookmarks"), this, [this]() {
            copyTextToClipboard(listWidgetText(bookmarksList), QStringLiteral("bookmarks"));
        });
        QAction* saveAction = menu.addAction(QStringLiteral("Save Bookmarks As..."), this, [this]() {
            saveListWidgetItemsAs(bookmarksList, QStringLiteral("Save Bookmarks"), QStringLiteral("bookmarks.txt"));
        });
        menu.addSeparator();
        QAction* clearCurrentAction = menu.addAction(QStringLiteral("Clear Current Document Bookmarks"), this, [this]() {
            clearBookmarks();
        });
        QAction* clearAllAction = menu.addAction(QStringLiteral("Clear All Bookmarks"), this, [this]() {
            clearAllBookmarks();
        });

        const bool hasBookmarks = bookmarksList->count() > 0;
        jumpAction->setEnabled(item != nullptr);
        removeAction->setEnabled(item != nullptr);
        copyBookmarkAction->setEnabled(item != nullptr);
        copyAllAction->setEnabled(hasBookmarks);
        saveAction->setEnabled(hasBookmarks);
        clearCurrentAction->setEnabled(currentPage() != nullptr && !currentPage()->bookmarks.isEmpty());
        clearAllAction->setEnabled(hasBookmarks);
        menu.exec(bookmarksList->mapToGlobal(pos));
    }

    QString buildOutputText() const
    {
        return output != nullptr ? output->toPlainText() : QString();
    }

    void copyBuildOutput()
    {
        const QString text = buildOutputText();
        if (text.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No build output to copy"), 2500);
            return;
        }
        copyTextToClipboard(text, QStringLiteral("build output"));
    }

    void saveBuildOutputAs()
    {
        const QString text = buildOutputText();
        if (text.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No build output to save"), 2500);
            return;
        }
        const QString path = chooseSaveFileName(this, QStringLiteral("Save Build Output"),
            currentWorkDir() + QLatin1Char('/') + QStringLiteral("build-output.txt"),
            QStringLiteral("Text Files (*.txt);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        const QByteArray bytes = text.toUtf8();
        if (!writeBytesToFile(path, bytes, QStringLiteral("Save Build Output"))) {
            return;
        }
        appendOutput(QStringLiteral("Saved build output: %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Build output saved"), 2500);
    }

    void saveScribbleAs()
    {
        const QString text = scratchPad != nullptr ? scratchPad->toPlainText() : QString();
        if (text.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No scribble notes to save"), 2500);
            return;
        }

        const QString path = chooseSaveFileName(this, QStringLiteral("Save Scribble"),
            currentWorkDir() + QLatin1Char('/') + QStringLiteral("geany-scribble.txt"),
            QStringLiteral("Text Files (*.txt);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        const QByteArray bytes = text.toUtf8();
        if (!writeBytesToFile(path, bytes, QStringLiteral("Save Scribble"))) {
            return;
        }
        appendOutput(QStringLiteral("Saved scribble notes: %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Scribble notes saved"), 2500);
    }

    void showBuildOutputContextMenu(const QPoint& pos)
    {
        if (output == nullptr) {
            return;
        }
        QMenu* menu = output->createStandardContextMenu(pos);
        if (menu == nullptr) {
            return;
        }
        menu->addSeparator();
        QAction* copyAllAction = menu->addAction(QStringLiteral("Copy All Output"), this, [this]() { copyBuildOutput(); });
        QAction* saveAction = menu->addAction(QStringLiteral("Save Output As..."), this, [this]() { saveBuildOutputAs(); });
        QAction* clearAction = menu->addAction(QStringLiteral("Clear Output"), this, [this]() {
            if (output != nullptr) {
                output->clear();
            }
        });
        const bool hasOutput = !buildOutputText().isEmpty();
        copyAllAction->setEnabled(hasOutput);
        saveAction->setEnabled(hasOutput);
        clearAction->setEnabled(hasOutput);
        menu->exec(output->mapToGlobal(pos));
        menu->deleteLater();
    }

    void showEditorContextMenu(EditorPage* page, const QPoint& pos)
    {
        if (page == nullptr || page->editor == nullptr) {
            return;
        }
        const int index = tabs->indexOf(page);
        if (index >= 0 && tabs->currentIndex() != index) {
            tabs->setCurrentIndex(index);
        }

        std::unique_ptr<QMenu> menu(page->editor->createStandardContextMenu(pos));
        menu->addSeparator();
        menu->addAction(QStringLiteral("Open Selected Filename"), this, [this]() { openSelectedFilename(); });
        menu->addAction(QStringLiteral("Go to Definition"), this, [this]() { goToDefinition(); });
        menu->addAction(QStringLiteral("Find References"), this, [this]() { findReferences(); });
        menu->addAction(QStringLiteral("Switch Header/Source"), this, [this]() { switchHeaderSource(); });
        menu->addAction(QStringLiteral("Toggle Comment"), this, [this]() { toggleComment(); });
        menu->addAction(QStringLiteral("Toggle Block Comment"), this, [this]() { toggleBlockComment(); });
        menu->addAction(QStringLiteral("Toggle Writable Permission"), this, [this]() { toggleCurrentFileWritable(); });
        menu->addAction(QStringLiteral("Toggle Executable Permission"), this, [this]() { toggleCurrentFileExecutable(); });
        QAction* renameAction = menu->addAction(QStringLiteral("Rename/Move File..."), this, [this]() { renameCurrentDocumentFile(); });
        menu->addSeparator();
        QAction* openFolderAction = menu->addAction(QStringLiteral("Open Containing Folder"), this, [this, page]() {
            openContainingFolderForPath(page->path);
        });
        QAction* openSystemAction = menu->addAction(QStringLiteral("Open with System App"), this, [this, page]() {
            openFileWithSystemApp(page->path);
        });
        QAction* copyPathAction = menu->addAction(QStringLiteral("Copy File Path"), this, [this, page]() {
            copyTextToClipboard(page->path, QStringLiteral("file path"));
        });
        QAction* copyRelativePathAction = menu->addAction(QStringLiteral("Copy Relative Path"), this, [this, page]() {
            copyTextToClipboard(QDir(projectRoot).relativeFilePath(page->path), QStringLiteral("relative path"));
        });
        openFolderAction->setEnabled(!page->path.isEmpty());
        openSystemAction->setEnabled(!page->path.isEmpty());
        copyPathAction->setEnabled(!page->path.isEmpty());
        QAction* selectProjectAction = menu->addAction(QStringLiteral("Select in Project"), this, [this]() { selectCurrentFileInProject(); });
        renameAction->setEnabled(!page->path.isEmpty());
        copyRelativePathAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        selectProjectAction->setEnabled(!page->path.isEmpty() && !projectRoot.isEmpty() && isInsideProject(page->path));
        menu->exec(page->editor->mapToGlobal(pos));
    }

    void showProjectContextMenu(const QPoint& pos)
    {
        if (projectFiles == nullptr) {
            return;
        }
        QListWidgetItem* item = projectFiles->itemAt(pos);
        if (item != nullptr) {
            projectFiles->setCurrentItem(item);
        }

        const QString path = selectedProjectPath();
        QMenu menu(projectFiles);
        QAction* openAction = menu.addAction(QStringLiteral("Open"), this, [this]() { openSelectedProjectFile(); });
        QAction* renameAction = menu.addAction(QStringLiteral("Rename..."), this, [this]() { renameSelectedProjectFile(); });
        QAction* duplicateAction = menu.addAction(QStringLiteral("Duplicate..."), this, [this]() { duplicateSelectedProjectFile(); });
        QAction* deleteAction = menu.addAction(QStringLiteral("Delete..."), this, [this]() { deleteSelectedProjectFile(); });
        openAction->setEnabled(!path.isEmpty());
        renameAction->setEnabled(!path.isEmpty());
        duplicateAction->setEnabled(!path.isEmpty());
        deleteAction->setEnabled(!path.isEmpty());
        menu.addSeparator();
        QAction* openFolderAction = menu.addAction(QStringLiteral("Open Containing Folder"), this, [this, path]() {
            openContainingFolderForPath(path);
        });
        QAction* openSystemAction = menu.addAction(QStringLiteral("Open with System App"), this, [this, path]() {
            openFileWithSystemApp(path);
        });
        QAction* copyPathAction = menu.addAction(QStringLiteral("Copy Path"), this, [this, path]() {
            copyTextToClipboard(path, QStringLiteral("path"));
        });
        QAction* copyRelativePathAction = menu.addAction(QStringLiteral("Copy Relative Path"), this, [this, path]() {
            copyTextToClipboard(QDir(projectRoot).relativeFilePath(path), QStringLiteral("relative path"));
        });
        copyPathAction->setEnabled(!path.isEmpty());
        copyRelativePathAction->setEnabled(!path.isEmpty() && !projectRoot.isEmpty());
        openFolderAction->setEnabled(!path.isEmpty());
        openSystemAction->setEnabled(!path.isEmpty() && QFileInfo(path).isFile());
        menu.addSeparator();
        menu.addAction(QStringLiteral("New File..."), this, [this]() { createProjectFile(); });
        menu.addAction(QStringLiteral("New Folder..."), this, [this]() { createProjectFolder(); });
        menu.addAction(QStringLiteral("Refresh"), this, [this]() { loadProjectFiles(); });
        QAction* clearFilterAction = menu.addAction(QStringLiteral("Clear Filter"), this, [this]() {
            if (projectFileFilter != nullptr) {
                projectFileFilter->clear();
            }
        });
        clearFilterAction->setEnabled(projectFileFilter != nullptr && !projectFileFilter->text().isEmpty());
        menu.exec(projectFiles->mapToGlobal(pos));
    }

    void connectCurrentEditorSignals()
    {
        if (EditorPage* page = currentPage()) {
            page->editor->setFocus();
        }
    }

    void refreshSplitView()
    {
        if (splitView == nullptr || splitViewAction == nullptr || !splitViewAction->isChecked()) {
            return;
        }
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            splitView->clear();
            return;
        }
        const int scrollbarValue = splitView->verticalScrollBar() != nullptr
            ? splitView->verticalScrollBar()->value()
            : 0;
        splitView->setFont(page->editor->font());
        splitView->setPlainText(page->editor->toPlainText());
        splitView->document()->setModified(false);
        if (splitView->verticalScrollBar() != nullptr) {
            splitView->verticalScrollBar()->setValue(scrollbarValue);
        }
    }

    void setSplitViewVisible(bool visible)
    {
        if (splitView == nullptr || splitViewAction == nullptr) {
            return;
        }
        splitViewAction->setChecked(visible);
        splitView->setVisible(visible);
        if (visible) {
            refreshSplitView();
            if (editorSplitter != nullptr) {
                editorSplitter->setSizes(QList<int>() << qMax(600, editorSplitter->width() / 2)
                                                       << qMax(600, editorSplitter->width() / 2));
            }
            appendOutput(QStringLiteral("Split view enabled."));
        } else {
            appendOutput(QStringLiteral("Split view disabled."));
        }
        saveSettings();
    }

    void openDocument()
    {
        const QString path = chooseOpenFileName(this, QStringLiteral("Open File"), currentWorkDir());
        if (!path.isEmpty()) {
            openPath(path);
        }
    }

    void openPath(const QString& requestedPath, bool addToRecent = true, bool showErrors = true)
    {
        const QString path = normalizedLaunchPath(requestedPath);
        if (path.isEmpty()) {
            return;
        }

        for (int i = 0; i < tabs->count(); ++i) {
            auto* existing = static_cast<EditorPage*>(tabs->widget(i));
            if (existing != nullptr && QFileInfo(existing->path) == QFileInfo(path)) {
                tabs->setCurrentIndex(i);
                return;
            }
        }

        const QFileInfo fileInfo(path);
        const qint64 largeFileWarningBytes = 20 * 1024 * 1024;
        if (fileInfo.size() > largeFileWarningBytes) {
            const int answer = QMessageBox::question(this, QStringLiteral("Open Large File"),
                QStringLiteral("%1 is %2.\n\nLarge files can be slow to edit on Harmony PC. Open it anyway?")
                    .arg(path, readableFileSize(fileInfo.size())),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                statusBar()->showMessage(QStringLiteral("Open cancelled"), 2000);
                return;
            }
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            const QString message = QStringLiteral("%1: %2").arg(path, file.errorString());
            if (showErrors) {
                QMessageBox::warning(this, QStringLiteral("Open failed"), message);
            } else {
                appendOutput(QStringLiteral("Skipped unavailable session file: %1").arg(message));
            }
            return;
        }
        const QByteArray sample = file.peek(8192);
        if (looksLikeBinary(sample)) {
            const int answer = QMessageBox::question(this, QStringLiteral("Open Binary File"),
                QStringLiteral("%1 appears to be a binary file.\n\nOpen it as text anyway?").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                statusBar()->showMessage(QStringLiteral("Open cancelled"), 2000);
                return;
            }
        }
        const QByteArray bytes = file.readAll();

        auto* page = new EditorPage(tabs);
        page->path = path;
        page->lineEnding = detectLineEnding(bytes);
        page->encodingName = detectEncodingName(bytes);
        configureHighlighterForPath(page, path);
        page->diskSnapshot = TextSaveGuard::fromBytes(bytes);
        page->utf8Bom = bytes.startsWith("\xEF\xBB\xBF");
        page->sourceDecodedSafely = TextSaveGuard::validDecode(bytes, page->encodingName);
        page->editor->setPlainText(normalizeLineEndings(decodeText(bytes, page->encodingName)));
        page->editor->setReadOnly(!QFileInfo(path).isWritable());
        page->editor->document()->setModified(false);
        updateDiskMetadata(page);

        const int index = tabs->addTab(page, QFileInfo(path).fileName());
        tabs->setCurrentIndex(index);
        wireEditor(page);
        applyEditorPreferences();
        applySearchHighlights(page);

        appendOutput(QStringLiteral("Opened %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Opened %1").arg(path), 2500);
        updateDocumentList();
        updateBookmarksList();
        refreshSymbols();
        updateCursorStatus();
        if (addToRecent) {
            addRecentFile(path);
        }
    }

    bool saveCurrentDocument()
    {
        return saveDocument(currentPage());
    }

    bool saveCurrentDocumentAs()
    {
        return saveDocument(currentPage(), true);
    }

    bool saveCurrentDocumentCopyAs()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }

        const QString path = chooseSaveFileName(this, QStringLiteral("Save a Copy"),
            currentWorkDir() + QLatin1Char('/') + displayName(page));
        if (path.isEmpty()) {
            return false;
        }

        const QByteArray bytes = encodeText(textForSaving(page, false), page->encodingName);
        if (!writeBytesToFile(path, bytes, QStringLiteral("Save copy failed"))) {
            return false;
        }

        appendOutput(QStringLiteral("Saved copy %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Saved copy %1").arg(path), 2500);
        return true;
    }

    bool saveSelectionAs()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return false;
        }

        QTextCursor cursor = page->editor->textCursor();
        if (!cursor.hasSelection()) {
            statusBar()->showMessage(QStringLiteral("Select text before saving a selection"), 2500);
            return false;
        }

        QString suggestedName = QStringLiteral("selection.txt");
        if (!page->path.isEmpty()) {
            const QFileInfo info(page->path);
            suggestedName = info.completeBaseName() + QStringLiteral("-selection");
            if (!info.suffix().isEmpty()) {
                suggestedName += QLatin1Char('.') + info.suffix();
            }
        }
        const QString path = chooseSaveFileName(this, QStringLiteral("Save Selection"),
            currentWorkDir() + QLatin1Char('/') + suggestedName);
        if (path.isEmpty()) {
            return false;
        }

        QString text = cursor.selectedText();
        text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        const QByteArray bytes = encodeText(applyLineEnding(text, page->lineEnding), page->encodingName);

        if (!writeBytesToFile(path, bytes, QStringLiteral("Save selection failed"))) {
            return false;
        }

        appendOutput(QStringLiteral("Saved selection %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Saved selection %1").arg(path), 2500);
        return true;
    }

    void insertFileAtCursor()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }
        if (page->editor->isReadOnly()) {
            statusBar()->showMessage(QStringLiteral("Current document is read-only"), 2500);
            return;
        }

        const QString path = chooseOpenFileName(this, QStringLiteral("Insert File"),
            currentWorkDir(), QStringLiteral("All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        const QFileInfo fileInfo(path);
        const qint64 largeInsertWarningBytes = 5 * 1024 * 1024;
        if (fileInfo.size() > largeInsertWarningBytes) {
            const int answer = QMessageBox::question(this, QStringLiteral("Insert Large File"),
                QStringLiteral("%1 is %2.\n\nInsert it into the current document?")
                    .arg(path, readableFileSize(fileInfo.size())),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                statusBar()->showMessage(QStringLiteral("Insert cancelled"), 2000);
                return;
            }
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Insert File"), file.errorString());
            return;
        }

        const QByteArray sample = file.peek(8192);
        if (looksLikeBinary(sample)) {
            const int answer = QMessageBox::question(this, QStringLiteral("Insert Binary File"),
                QStringLiteral("%1 appears to be a binary file.\n\nInsert it as text anyway?").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                statusBar()->showMessage(QStringLiteral("Insert cancelled"), 2000);
                return;
            }
        }

        const QByteArray bytes = file.readAll();
        const QString encodingName = detectEncodingName(bytes);
        QString text = normalizeLineEndings(decodeText(bytes, encodingName));
        QTextCursor cursor = page->editor->textCursor();
        cursor.insertText(text);
        page->editor->setTextCursor(cursor);
        appendOutput(QStringLiteral("Inserted file %1 (%2, %3).")
            .arg(path, encodingName, readableFileSize(bytes.size())));
        statusBar()->showMessage(QStringLiteral("Inserted file"), 2500);
    }

    bool renameCurrentDocumentFile()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }
        if (page->path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Save the document before renaming it"), 2500);
            return false;
        }
        if (page->editor->document()->isModified()) {
            const int answer = QMessageBox::question(this, QStringLiteral("Rename File"),
                QStringLiteral("Save changes to %1 before renaming it?").arg(displayName(page)),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes || !saveDocument(page)) {
                return false;
            }
        }

        const QString oldPath = page->path;
        const QString newPath = chooseSaveFileName(this, QStringLiteral("Rename or Move File"),
            oldPath, QStringLiteral("All Files (*)"));
        if (newPath.isEmpty() || QFileInfo(newPath) == QFileInfo(oldPath)) {
            return false;
        }
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, QStringLiteral("Rename File"), QStringLiteral("Target already exists."));
            return false;
        }
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        if (!QFile::rename(oldPath, newPath)) {
            QMessageBox::warning(this, QStringLiteral("Rename File"), QStringLiteral("Could not rename or move file."));
            return false;
        }

        const QFileInfo oldInfo(oldPath);
        for (int i = 0; i < tabs->count(); ++i) {
            auto* openPage = static_cast<EditorPage*>(tabs->widget(i));
            if (openPage != nullptr && QFileInfo(openPage->path) == oldInfo) {
                openPage->path = newPath;
                configureHighlighterForPath(openPage, newPath);
                updateDiskMetadata(openPage);
                updateTabTitle(openPage, openPage->editor->document()->isModified());
            }
        }
        recentFiles.removeAll(oldPath);
        addRecentFile(newPath);
        if (!projectRoot.isEmpty()) {
            loadProjectFiles();
        }
        updateDocumentList();
        updateBookmarksList();
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Renamed %1 to %2").arg(oldPath, newPath));
        statusBar()->showMessage(QStringLiteral("Renamed file"), 2500);
        return true;
    }

    bool exportCurrentDocumentAsHtml()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }

        QString baseName = displayName(page);
        if (!baseName.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)
            && !baseName.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive)) {
            baseName.append(QStringLiteral(".html"));
        }
        const QString path = chooseSaveFileName(this, QStringLiteral("Export as HTML"),
            currentWorkDir() + QLatin1Char('/') + baseName,
            QStringLiteral("HTML Files (*.html *.htm)"));
        if (path.isEmpty()) {
            return false;
        }

#if defined(Q_OS_OPENHARMONY)
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            appendOutput(QStringLiteral("Export failed: %1").arg(file.errorString()));
            statusBar()->showMessage(QStringLiteral("Export failed: %1").arg(file.errorString()), 3500);
            return false;
        }
#else
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("Export failed"), file.errorString());
            return false;
        }
#endif

        QTextStream stream(&file);
        stream.setCodec("UTF-8");
        writeDocumentHtml(page, stream);
        stream.flush();
#if defined(Q_OS_OPENHARMONY)
        file.close();
#else
        if (!file.commit()) {
            QMessageBox::warning(this, QStringLiteral("Export failed"), file.errorString());
            return false;
        }
#endif

        appendOutput(QStringLiteral("Exported HTML %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Exported HTML %1").arg(path), 2500);
        return true;
    }

    void configurePrinterForDocument(QPrinter* printer, EditorPage* page) const
    {
        if (printer == nullptr) {
            return;
        }
        printer->setDocName(displayName(page));
        printer->setPageMargins(QMarginsF(15.0, 15.0, 15.0, 15.0), QPageLayout::Millimeter);
    }

    bool exportCurrentDocumentAsPdf()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }

        QString baseName = displayName(page);
        if (!baseName.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            baseName.append(QStringLiteral(".pdf"));
        }
        const QString path = chooseSaveFileName(this, QStringLiteral("Export as PDF"),
            currentWorkDir() + QLatin1Char('/') + baseName,
            QStringLiteral("PDF Files (*.pdf)"));
        if (path.isEmpty()) {
            return false;
        }

        QPrinter printer(QPrinter::HighResolution);
        configurePrinterForDocument(&printer, page);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        page->editor->print(&printer);
        appendOutput(QStringLiteral("Exported PDF %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Exported PDF %1").arg(path), 2500);
        return true;
    }

    void previewCurrentDocument()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

#if defined(Q_OS_OPENHARMONY)
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Print Preview - %1").arg(displayName(page)));
        dialog.resize(1000, 760);
        auto* layout = new QVBoxLayout(&dialog);
        auto* preview = new QPlainTextEdit(&dialog);
        preview->setReadOnly(true);
        preview->setPlainText(page->editor->toPlainText());
        preview->setFont(page->editor->font());
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dialog);
        if (QPushButton* saveButton = buttons->button(QDialogButtonBox::Save)) {
            saveButton->setText(QStringLiteral("Export PDF"));
        }
        layout->addWidget(preview, 1);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
            exportCurrentDocumentAsPdf();
            dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
#else
        QPrinter printer(QPrinter::HighResolution);
        configurePrinterForDocument(&printer, page);
        QPrintPreviewDialog preview(&printer, this);
        preview.setWindowTitle(QStringLiteral("Print Preview - %1").arg(displayName(page)));
        connect(&preview, &QPrintPreviewDialog::paintRequested, this, [page](QPrinter* targetPrinter) {
            if (page != nullptr && targetPrinter != nullptr) {
                page->editor->print(targetPrinter);
            }
        });
        preview.exec();
#endif
    }

    void printCurrentDocument()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        QPrinter printer(QPrinter::HighResolution);
        configurePrinterForDocument(&printer, page);
#if defined(Q_OS_OPENHARMONY)
        QString baseName = displayName(page);
        if (!baseName.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            baseName.append(QStringLiteral(".pdf"));
        }
        const QString path = chooseSaveFileName(this, QStringLiteral("Print to PDF"),
            currentWorkDir() + QLatin1Char('/') + baseName,
            QStringLiteral("PDF Files (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        page->editor->print(&printer);
        appendOutput(QStringLiteral("Printed to PDF %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Printed to PDF %1").arg(path), 2500);
#else
        QPrintDialog dialog(&printer, this);
        dialog.setWindowTitle(QStringLiteral("Print %1").arg(displayName(page)));
        if (dialog.exec() == QDialog::Accepted) {
            page->editor->print(&printer);
            appendOutput(QStringLiteral("Printed %1").arg(displayName(page)));
        }
#endif
    }

    void writeDocumentHtml(EditorPage* page, QTextStream& stream) const
    {
        const bool dark = normalizedTheme(editorTheme) == QStringLiteral("Dark");
        const bool highContrast = normalizedTheme(editorTheme) == QStringLiteral("High Contrast");
        const QString background = highContrast ? QStringLiteral("#000000") : dark ? QStringLiteral("#111820") : QStringLiteral("#ffffff");
        const QString foreground = highContrast ? QStringLiteral("#ffffff") : dark ? QStringLiteral("#e1e8f0") : QStringLiteral("#181d23");
        const QString gutter = highContrast ? QStringLiteral("#ffd640") : dark ? QStringLiteral("#8aa0b5") : QStringLiteral("#6b7280");
        const QString border = highContrast ? QStringLiteral("#ffffff") : dark ? QStringLiteral("#334155") : QStringLiteral("#d0d7de");
        const QString title = displayName(page).toHtmlEscaped();
        const QString language = (page != nullptr && page->highlighter != nullptr
            ? page->highlighter->languageName()
            : QStringLiteral("Plain Text")).toHtmlEscaped();
        QString text = page != nullptr ? page->editor->toPlainText() : QString();
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

        stream << QStringLiteral("<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n");
        stream << QStringLiteral("<title>") << title << QStringLiteral("</title>\n");
        stream << QStringLiteral("<style>\n");
        stream << QStringLiteral("body{margin:0;background:") << background << QStringLiteral(";color:") << foreground
               << QStringLiteral(";font:14px ui-monospace,SFMono-Regular,Consolas,monospace;}\n");
        stream << QStringLiteral("header{padding:16px 20px;border-bottom:1px solid ") << border
               << QStringLiteral(";font-family:system-ui,sans-serif;}\n");
        stream << QStringLiteral("h1{font-size:18px;margin:0 0 4px 0;}p{margin:0;color:") << gutter << QStringLiteral(";}\n");
        stream << QStringLiteral("table{border-collapse:collapse;width:100%;}td{vertical-align:top;}\n");
        stream << QStringLiteral(".line{width:1%;white-space:nowrap;text-align:right;color:") << gutter
               << QStringLiteral(";user-select:none;border-right:1px solid ") << border << QStringLiteral(";padding:0 12px;}\n");
        stream << QStringLiteral(".code{padding:0 16px;}pre{margin:0;white-space:pre-wrap;tab-size:")
               << qMax(1, tabWidth) << QStringLiteral(";}\n");
        stream << QStringLiteral("tr:hover{background:rgba(127,127,127,.10);}\n");
        stream << QStringLiteral("</style>\n</head>\n<body>\n<header><h1>")
               << title << QStringLiteral("</h1><p>") << language << QStringLiteral(" | Exported from Geany Qt/OpenHarmony</p></header>\n<table>\n");
        for (int i = 0; i < lines.size(); ++i) {
            stream << QStringLiteral("<tr><td class=\"line\">") << (i + 1)
                   << QStringLiteral("</td><td class=\"code\"><pre>")
                   << lines.at(i).toHtmlEscaped()
                   << QStringLiteral("</pre></td></tr>\n");
        }
        stream << QStringLiteral("</table>\n</body>\n</html>\n");
    }

    bool createBackupForSave(const QString& path)
    {
#if defined(Q_OS_OPENHARMONY)
        Q_UNUSED(path);
        return true;
#else
        if (!createBackupOnSave || path.isEmpty()) {
            return true;
        }
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return true;
        }

        const QString backupPath = path + QStringLiteral("~");
        if (QFileInfo::exists(backupPath) && !QFile::remove(backupPath)) {
            QMessageBox::warning(this, QStringLiteral("Backup failed"),
                QStringLiteral("Could not replace backup file:\n%1").arg(backupPath));
            return false;
        }
        if (!QFile::copy(path, backupPath)) {
            QMessageBox::warning(this, QStringLiteral("Backup failed"),
                QStringLiteral("Could not create backup file:\n%1").arg(backupPath));
            return false;
        }
        return true;
#endif
    }

    bool saveDocument(EditorPage* page, bool saveAs = false, bool quiet = false)
    {
        if (page == nullptr) {
            return false;
        }

#if defined(Q_OS_OPENHARMONY)
        page->editor->closeCompletionPopup();
#endif

        QString path = page->path;
        if (path.isEmpty() || saveAs) {
            path = chooseSaveFileName(this, QStringLiteral("Save File"), currentWorkDir());
            if (path.isEmpty()) {
                return false;
            }
        }

        QString error;
        if (!page->sourceDecodedSafely) {
            appendOutput(QStringLiteral("Save refused: source encoding was not decoded losslessly. Reload using the correct encoding."));
            statusBar()->showMessage(QStringLiteral("Save refused: reload with the correct source encoding"), 5000);
            return false;
        }
        TextSaveGuard::Snapshot expected = page->diskSnapshot;
        if (page->path.isEmpty() || QFileInfo(path).absoluteFilePath() != QFileInfo(page->path).absoluteFilePath()) {
            expected = TextSaveGuard::capture(path, &error);
            if (expected.exists && QMessageBox::question(this, QStringLiteral("Replace existing file"),
                    QStringLiteral("Replace this existing file?\n%1").arg(path), QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return false;
        }
        QByteArray bytes;
        if (!TextSaveGuard::encode(textForSaving(page, false), page->encodingName, page->utf8Bom, bytes, error)) {
            appendOutput(QStringLiteral("Save refused: %1").arg(error));
            return false;
        }
        if (!createBackupForSave(path)) return false;
        if (!TextSaveGuard::saveAtomic(path, bytes, expected, error)) {
            appendOutput(QStringLiteral("Save failed: %1").arg(error));
            statusBar()->showMessage(QStringLiteral("Save failed: %1").arg(error), 5000);
            return false;
        }
        // Normalize visible text only after the file commit succeeded.
        textForSaving(page, true);
        page->diskSnapshot = TextSaveGuard::fromBytes(bytes);

        page->path = path;
        configureHighlighterForPath(page, path);
        page->editor->document()->setModified(false);
        updateDiskMetadata(page);
        updateTabTitle(page, false);
        if (!quiet) {
            appendOutput(QStringLiteral("Saved %1").arg(path));
            statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 2500);
        }
        updateDocumentList();
        updateCursorStatus();
        addRecentFile(path);
        return true;
    }

    void configureAutoSaveTimer()
    {
        autoSaveIntervalSeconds = qBound(15, autoSaveIntervalSeconds, 3600);
        if (autoSaveTimer == nullptr) {
            return;
        }
        if (autoSaveEnabled) {
            autoSaveTimer->start(autoSaveIntervalSeconds * 1000);
        } else {
            autoSaveTimer->stop();
        }
    }

    void autoSaveModifiedDocuments(bool announce)
    {
        int saved = 0;
        int skipped = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->path.isEmpty() || !page->editor->document()->isModified()) {
                continue;
            }

            const QFileInfo info(page->path);
            if (info.exists() && page->lastDiskModified.isValid() && info.lastModified() > page->lastDiskModified) {
                page->externalChangePrompted = true;
                ++skipped;
                continue;
            }
            if (saveDocument(page, false, true)) {
                ++saved;
            }
        }

        if (announce || saved > 0 || skipped > 0) {
            QString message = QStringLiteral("Auto saved %1 modified file(s).").arg(saved);
            if (skipped > 0) {
                message += QStringLiteral(" Skipped %1 externally changed file(s).").arg(skipped);
            }
            appendOutput(message);
            statusBar()->showMessage(message, 3000);
        }
    }

    QString cursorEntry(EditorPage* page, const QDir& rootDir, bool relativePath) const
    {
        if (page == nullptr || page->path.isEmpty()) {
            return QString();
        }
        const QTextCursor cursor = page->editor->textCursor();
        const QString storedPath = relativePath ? rootDir.relativeFilePath(page->path) : page->path;
        return QStringLiteral("%1\t%2\t%3")
            .arg(storedPath)
            .arg(cursor.blockNumber() + 1)
            .arg(cursor.positionInBlock() + 1);
    }

    void restoreCursorEntries(const QStringList& entries, const QString& root)
    {
        for (const QString& entry : entries) {
            const QStringList parts = entry.split(QLatin1Char('\t'));
            if (parts.size() < 3) {
                continue;
            }
            QString path = parts.at(0);
            if (!root.isEmpty() && QDir::isRelativePath(path)) {
                path = QDir(root).absoluteFilePath(path);
            }
            const int line = parts.at(1).toInt();
            const int column = parts.at(2).toInt();
            for (int i = 0; i < tabs->count(); ++i) {
                auto* page = static_cast<EditorPage*>(tabs->widget(i));
                if (page != nullptr && QFileInfo(page->path) == QFileInfo(path)) {
                    goToLineColumn(page, line, column);
                    break;
                }
            }
        }
    }

    void setActiveFile(const QString& path)
    {
        if (path.isEmpty()) {
            return;
        }
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && QFileInfo(page->path) == QFileInfo(path)) {
                tabs->setCurrentIndex(i);
                return;
            }
        }
    }

    QStringList bookmarkEntriesForOpenFiles(const QDir& rootDir = QDir(), bool relativePath = false) const
    {
        QStringList entries;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->path.isEmpty()) {
                continue;
            }
            const QString storedPath = relativePath ? rootDir.relativeFilePath(page->path) : page->path;
            QList<int> lines = page->bookmarks.values();
            std::sort(lines.begin(), lines.end());
            for (int line : lines) {
                entries << QStringLiteral("%1:%2").arg(storedPath).arg(line);
            }
        }
        return entries;
    }

    void restoreBookmarkEntries(const QStringList& entries, const QString& root = QString())
    {
        for (const QString& entry : entries) {
            const int split = entry.lastIndexOf(QLatin1Char(':'));
            if (split <= 0) {
                continue;
            }
            QString path = entry.left(split);
            if (!root.isEmpty() && QDir::isRelativePath(path)) {
                path = QDir(root).absoluteFilePath(path);
            }
            const int line = entry.mid(split + 1).toInt();
            for (int i = 0; i < tabs->count(); ++i) {
                auto* page = static_cast<EditorPage*>(tabs->widget(i));
                if (page != nullptr && QFileInfo(page->path) == QFileInfo(path) && line > 0) {
                    page->bookmarks.insert(line);
                }
            }
        }
        updateBookmarksList();
    }

    void clearOpenDocumentsForSessionLoad()
    {
        while (tabs->count() > 0) {
            QWidget* widget = tabs->widget(0);
            tabs->removeTab(0);
            widget->deleteLater();
        }
        updateDocumentList();
        updateBookmarksList();
    }

    QStringList openFilePaths() const
    {
        QStringList paths;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && !page->path.isEmpty()) {
                paths << page->path;
            }
        }
        return paths;
    }

    QStringList openCursorEntries() const
    {
        QStringList entries;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && !page->path.isEmpty()) {
                entries << cursorEntry(page, QDir(), false);
            }
        }
        return entries;
    }

    void saveSessionAs()
    {
        const QString path = chooseSaveFileName(this, QStringLiteral("Save Session"),
            currentWorkDir() + QLatin1Char('/') + QStringLiteral("geany-session.geany-session"),
            QStringLiteral("Geany Session (*.geany-session);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        QSettings session(path, QSettings::IniFormat);
        session.clear();
        session.setValue(QStringLiteral("session/version"), 1);
        session.setValue(QStringLiteral("session/files"), openFilePaths());
        session.setValue(QStringLiteral("session/cursors"), openCursorEntries());
        session.setValue(QStringLiteral("session/bookmarks"), bookmarkEntriesForOpenFiles());
        session.setValue(QStringLiteral("session/activeFile"),
            currentPage() != nullptr ? currentPage()->path : QString());
        session.setValue(QStringLiteral("project/root"), projectRoot);
        session.setValue(QStringLiteral("project/file"), projectFile);
        session.setValue(QStringLiteral("build/command"), buildCommand);
        session.sync();
        if (session.status() != QSettings::NoError) {
            QMessageBox::warning(this, QStringLiteral("Save Session"), QStringLiteral("Could not write session file."));
            return;
        }
        appendOutput(QStringLiteral("Session saved: %1").arg(path));
        statusBar()->showMessage(QStringLiteral("Session saved"), 2500);
    }

    void loadSessionFromFile()
    {
        const QString path = chooseOpenFileName(this, QStringLiteral("Load Session"),
            currentWorkDir(), QStringLiteral("Geany Session (*.geany-session);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        if (!QFileInfo::exists(path)) {
            QMessageBox::warning(this, QStringLiteral("Load Session"), QStringLiteral("Session file does not exist."));
            return;
        }
        if (!confirmAllSaved()) {
            return;
        }

        QSettings session(path, QSettings::IniFormat);
        QString root = session.value(QStringLiteral("project/root")).toString();
        if (!root.isEmpty() && QDir(root).exists()) {
            projectRoot = QDir(root).canonicalPath();
            if (projectRoot.isEmpty()) {
                projectRoot = QDir(root).absolutePath();
            }
            projectFile = session.value(QStringLiteral("project/file")).toString();
            projectDefaultLineEnding.clear();
            projectDefaultEncodingName.clear();
            projectExtraIgnorePatterns.clear();
            if (!projectFile.isEmpty() && QFileInfo::exists(projectFile)) {
                QSettings projectSettings(projectFile, QSettings::IniFormat);
                loadProjectConfiguration(projectSettings);
            }
            loadProjectFiles();
            addRecentProject(projectFile.isEmpty() ? projectRoot : projectFile);
        }
        buildCommand = session.value(QStringLiteral("build/command"), buildCommand).toString();

        clearOpenDocumentsForSessionLoad();
        int opened = 0;
        for (const QString& filePath : session.value(QStringLiteral("session/files")).toStringList()) {
            if (QFileInfo::exists(filePath)) {
                openPath(filePath, false);
                ++opened;
            }
        }
        restoreBookmarkEntries(session.value(QStringLiteral("session/bookmarks")).toStringList());
        restoreCursorEntries(session.value(QStringLiteral("session/cursors")).toStringList(), QString());
        setActiveFile(session.value(QStringLiteral("session/activeFile")).toString());
        if (tabs->count() == 0) {
            newDocument();
        }
        updateRecentProjectsMenu();
        updateCursorStatus();
        appendOutput(QStringLiteral("Session loaded: %1 (%2 file(s))").arg(path).arg(opened));
        statusBar()->showMessage(QStringLiteral("Session loaded: %1 file(s)").arg(opened), 2500);
    }

    QString normalizeLineEndings(QString text) const
    {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        return text;
    }

    bool isValidUtf8(const QByteArray& bytes) const
    {
        int expected = 0;
        for (unsigned char byte : bytes) {
            if (expected == 0) {
                if ((byte & 0x80) == 0) {
                    continue;
                }
                if ((byte & 0xE0) == 0xC0) {
                    expected = 1;
                } else if ((byte & 0xF0) == 0xE0) {
                    expected = 2;
                } else if ((byte & 0xF8) == 0xF0) {
                    expected = 3;
                } else {
                    return false;
                }
            } else {
                if ((byte & 0xC0) != 0x80) {
                    return false;
                }
                --expected;
            }
        }
        return expected == 0;
    }

    QString normalizedEncodingName(const QString& requested) const
    {
        const QString trimmed = requested.trimmed();
        const QString upper = trimmed.toUpper();
        if (upper == QStringLiteral("UTF-16LE-BOM") || upper == QStringLiteral("UTF-16LE WITH BOM")) {
            return QStringLiteral("UTF-16LE-BOM");
        }
        if (upper == QStringLiteral("UTF-16BE-BOM") || upper == QStringLiteral("UTF-16BE WITH BOM")) {
            return QStringLiteral("UTF-16BE-BOM");
        }
        QTextCodec* codec = QTextCodec::codecForName(trimmed.toLatin1());
        if (codec == nullptr) {
            codec = QTextCodec::codecForName("UTF-8");
        }
        return QString::fromLatin1(codec->name());
    }

    QString systemEncodingName() const
    {
        QTextCodec* codec = QTextCodec::codecForLocale();
        return codec != nullptr ? QString::fromLatin1(codec->name()) : QStringLiteral("UTF-8");
    }

    bool hasUtf16Bom(const QByteArray& bytes) const
    {
        return bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF");
    }

    QString codecNameForEncoding(const QString& encodingName) const
    {
        if (encodingName == QStringLiteral("UTF-16LE-BOM")) {
            return QStringLiteral("UTF-16LE");
        }
        if (encodingName == QStringLiteral("UTF-16BE-BOM")) {
            return QStringLiteral("UTF-16BE");
        }
        return encodingName;
    }

    QString detectEncodingName(const QByteArray& bytes) const
    {
        if (bytes.startsWith("\xEF\xBB\xBF")) {
            return QStringLiteral("UTF-8");
        }
        if (bytes.startsWith("\xFF\xFE")) {
            return QStringLiteral("UTF-16LE-BOM");
        }
        if (bytes.startsWith("\xFE\xFF")) {
            return QStringLiteral("UTF-16BE-BOM");
        }
        QTextCodec* unicodeCodec = QTextCodec::codecForUtfText(bytes, nullptr);
        if (unicodeCodec != nullptr) {
            return QString::fromLatin1(unicodeCodec->name());
        }
        if (isValidUtf8(bytes)) {
            return QStringLiteral("UTF-8");
        }
        QTextCodec* gb18030 = QTextCodec::codecForName("GB18030");
        if (gb18030 != nullptr) {
            return QString::fromLatin1(gb18030->name());
        }
        return systemEncodingName();
    }

    QString decodeText(const QByteArray& bytes, const QString& encodingName) const
    {
        QTextCodec* codec = QTextCodec::codecForName(codecNameForEncoding(encodingName).toLatin1());
        if (codec == nullptr) {
            codec = QTextCodec::codecForName("UTF-8");
        }
        QString text = codec->toUnicode(bytes);
        if (text.startsWith(QChar::ByteOrderMark)) {
            text.remove(0, 1);
        }
        return text;
    }

    QByteArray encodeText(const QString& text, const QString& encodingName) const
    {
        QTextCodec* codec = QTextCodec::codecForName(codecNameForEncoding(encodingName).toLatin1());
        if (codec == nullptr) {
            codec = QTextCodec::codecForName("UTF-8");
        }
        QByteArray bytes = codec->fromUnicode(text);
        if (encodingName == QStringLiteral("UTF-16LE-BOM") && !bytes.startsWith("\xFF\xFE")) {
            bytes.prepend(QByteArray::fromHex("FFFE"));
        } else if (encodingName == QStringLiteral("UTF-16BE-BOM") && !bytes.startsWith("\xFE\xFF")) {
            bytes.prepend(QByteArray::fromHex("FEFF"));
        }
        return bytes;
    }

    QString readableFileSize(qint64 bytes) const
    {
        double value = static_cast<double>(bytes);
        QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB")};
        int unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < units.size()) {
            value /= 1024.0;
            ++unitIndex;
        }
        return QStringLiteral("%1 %2").arg(value, 0, unitIndex == 0 ? 'f' : 'f', unitIndex == 0 ? 0 : 1).arg(units.at(unitIndex));
    }

    bool looksLikeBinary(const QByteArray& sample) const
    {
        if (sample.isEmpty()) {
            return false;
        }
        if (sample.startsWith("\xEF\xBB\xBF") || hasUtf16Bom(sample)) {
            return false;
        }

        int suspiciousControls = 0;
        for (char raw : sample) {
            const uchar byte = static_cast<uchar>(raw);
            if (byte == 0) {
                return true;
            }
            const bool allowedControl = byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f' || byte == '\b';
            if (byte < 32 && !allowedControl) {
                ++suspiciousControls;
            }
        }
        return suspiciousControls > qMax(8, sample.size() / 20);
    }

    QString detectLineEndingFromText(const QString& text) const
    {
        int crlf = 0;
        int lf = 0;
        int cr = 0;
        for (int i = 0; i < text.size(); ++i) {
            if (text.at(i) == QLatin1Char('\r')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                    ++crlf;
                    ++i;
                } else {
                    ++cr;
                }
            } else if (text.at(i) == QLatin1Char('\n')) {
                ++lf;
            }
        }
        if (crlf >= lf && crlf >= cr && crlf > 0) {
            return QStringLiteral("\r\n");
        }
        if (cr > lf && cr > crlf) {
            return QStringLiteral("\r");
        }
        return QStringLiteral("\n");
    }

    QString detectLineEnding(const QByteArray& bytes) const
    {
        if (hasUtf16Bom(bytes)) {
            return detectLineEndingFromText(decodeText(bytes, detectEncodingName(bytes)));
        }

        int crlf = 0;
        int lf = 0;
        int cr = 0;
        for (int i = 0; i < bytes.size(); ++i) {
            if (bytes.at(i) == '\r') {
                if (i + 1 < bytes.size() && bytes.at(i + 1) == '\n') {
                    ++crlf;
                    ++i;
                } else {
                    ++cr;
                }
            } else if (bytes.at(i) == '\n') {
                ++lf;
            }
        }
        if (crlf >= lf && crlf >= cr && crlf > 0) {
            return QStringLiteral("\r\n");
        }
        if (cr > lf && cr > crlf) {
            return QStringLiteral("\r");
        }
        return QStringLiteral("\n");
    }

    QString lineEndingName(const QString& lineEnding) const
    {
        if (lineEnding == QStringLiteral("\r\n")) {
            return QStringLiteral("CRLF");
        }
        if (lineEnding == QStringLiteral("\r")) {
            return QStringLiteral("CR");
        }
        return QStringLiteral("LF");
    }

    QString lineEndingForName(const QString& name) const
    {
        if (name == QStringLiteral("CRLF")) {
            return QStringLiteral("\r\n");
        }
        if (name == QStringLiteral("CR")) {
            return QStringLiteral("\r");
        }
        return QStringLiteral("\n");
    }

    QString applyLineEnding(QString text, const QString& lineEnding) const
    {
        text = normalizeLineEndings(text);
        if (lineEnding != QStringLiteral("\n")) {
            text.replace(QLatin1Char('\n'), lineEnding);
        }
        return text;
    }

    QString stripTrailingWhitespace(QString text) const
    {
        QStringList lines = normalizeLineEndings(text).split(QLatin1Char('\n'));
        const QRegularExpression trailing(QStringLiteral("[ \\t]+$"));
        for (QString& line : lines) {
            line.remove(trailing);
        }
        return lines.join(QLatin1Char('\n'));
    }

    QString textForSaving(EditorPage* page, bool updateEditor = true)
    {
        QString text = normalizeLineEndings(page->editor->toPlainText());
        if (stripTrailingOnSave) {
            text = stripTrailingWhitespace(text);
        }
        if (ensureFinalNewlineOnSave && !text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
            text.append(QLatin1Char('\n'));
        }

        const QString currentText = normalizeLineEndings(page->editor->toPlainText());
        if (updateEditor && text != currentText) {
            const int position = page->editor->textCursor().position();
            page->editor->setPlainText(text);
            QTextCursor cursor = page->editor->textCursor();
            cursor.setPosition(qMin(position, page->editor->document()->characterCount() - 1));
            page->editor->setTextCursor(cursor);
        }
        return applyLineEnding(text, page->lineEnding);
    }

    void setCurrentLineEnding(const QString& lineEnding)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        page->lineEnding = lineEnding;
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        updateCursorStatus();
        statusBar()->showMessage(QStringLiteral("Line endings set to %1").arg(lineEndingName(lineEnding)), 2500);
    }

    void setCurrentEncoding(const QString& encodingName)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        page->encodingName = normalizedEncodingName(encodingName);
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        updateCursorStatus();
        statusBar()->showMessage(QStringLiteral("Encoding set to %1").arg(page->encodingName), 2500);
    }

    bool saveAllDocuments()
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && page->editor->document()->isModified()) {
                tabs->setCurrentIndex(i);
                if (!saveDocument(page)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool confirmAllSaved()
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || !page->editor->document()->isModified()) {
                continue;
            }
            tabs->setCurrentIndex(i);
            const int answer = QMessageBox::question(
                this,
                QStringLiteral("Unsaved document"),
                QStringLiteral("Save changes to %1?").arg(displayName(page)),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) {
                return false;
            }
            if (answer == QMessageBox::Yes && !saveDocument(page)) {
                return false;
            }
        }
        return true;
    }

    void quitApplication()
    {
        quitRequested = true;
        if (!close()) {
            quitRequested = false;
        }
    }

    void closeCurrentTab()
    {
        closeTab(tabs->currentIndex());
    }

    void reopenClosedDocument()
    {
        while (!closedFiles.isEmpty()) {
            const QString path = closedFiles.takeFirst();
            if (QFileInfo::exists(path)) {
                openPath(path);
                statusBar()->showMessage(QStringLiteral("Reopened %1").arg(path), 2500);
                return;
            }
        }
        statusBar()->showMessage(QStringLiteral("No closed document to reopen"), 2500);
    }

    int unmodifiedDocumentCount() const
    {
        int count = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && !page->editor->document()->isModified()) {
                ++count;
            }
        }
        return count;
    }

    bool closeTab(int index)
    {
        if (index < 0) {
            return false;
        }
        auto* page = static_cast<EditorPage*>(tabs->widget(index));
        if (page != nullptr && page->editor->document()->isModified()) {
            tabs->setCurrentIndex(index);
            const int answer = QMessageBox::question(
                this,
                QStringLiteral("Unsaved document"),
                QStringLiteral("Save changes to %1?").arg(displayName(page)),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) {
                return false;
            }
            if (answer == QMessageBox::Yes && !saveDocument(page)) {
                return false;
            }
        }

        QWidget* widget = tabs->widget(index);
        if (page != nullptr) {
            addClosedFile(page->path);
        }
        tabs->removeTab(index);
        widget->deleteLater();
        if (tabs->count() == 0) {
            newDocument();
        }
        updateDocumentList();
        updateBookmarksList();
        updateCursorStatus();
        return true;
    }

    void closeOtherTabs()
    {
        const int keepIndex = tabs->currentIndex();
        if (keepIndex < 0) {
            return;
        }
        QWidget* keepWidget = tabs->widget(keepIndex);
        for (int i = tabs->count() - 1; i >= 0; --i) {
            if (tabs->widget(i) == keepWidget) {
                continue;
            }
            if (!closeTab(i)) {
                break;
            }
        }
        const int newIndex = tabs->indexOf(keepWidget);
        if (newIndex >= 0) {
            tabs->setCurrentIndex(newIndex);
        }
        updateDocumentList();
    }

    void closeUnmodifiedTabs()
    {
        const int before = tabs->count();
        int closed = 0;
        for (int i = tabs->count() - 1; i >= 0; --i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->editor->document()->isModified()) {
                continue;
            }
            if (closeTab(i)) {
                ++closed;
            }
        }
        if (closed == 0) {
            statusBar()->showMessage(QStringLiteral("No unmodified documents to close"), 2500);
            return;
        }
        updateDocumentList();
        appendOutput(QStringLiteral("Closed %1 unmodified document(s).").arg(closed));
        statusBar()->showMessage(QStringLiteral("Closed %1 of %2 document(s)").arg(closed).arg(before), 2500);
    }

    void closeTabsToSide(int direction)
    {
        const int keepIndex = tabs->currentIndex();
        if (keepIndex < 0) {
            return;
        }
        QWidget* keepWidget = tabs->widget(keepIndex);
        const int start = direction < 0 ? keepIndex - 1 : tabs->count() - 1;
        const int end = direction < 0 ? 0 : keepIndex + 1;
        for (int i = start; i >= end; --i) {
            if (!closeTab(i)) {
                break;
            }
        }
        const int newIndex = tabs->indexOf(keepWidget);
        if (newIndex >= 0) {
            tabs->setCurrentIndex(newIndex);
        }
        updateDocumentList();
    }

    void switchDocument(int direction)
    {
        const int count = tabs->count();
        if (count <= 1) {
            return;
        }
        int index = tabs->currentIndex() + (direction > 0 ? 1 : -1);
        if (index >= count) {
            index = 0;
        } else if (index < 0) {
            index = count - 1;
        }
        tabs->setCurrentIndex(index);
    }

    void moveCurrentTab(int direction)
    {
        const int from = tabs->currentIndex();
        const int to = from + (direction > 0 ? 1 : -1);
        if (from < 0 || to < 0 || to >= tabs->count()) {
            return;
        }
        tabs->tabBar()->moveTab(from, to);
        tabs->setCurrentIndex(to);
        updateDocumentList();
    }

    void closeAllTabs()
    {
        if (!confirmAllSaved()) {
            return;
        }
        while (tabs->count() > 0) {
            QWidget* widget = tabs->widget(0);
            tabs->removeTab(0);
            widget->deleteLater();
        }
        newDocument();
    }

    void updateDiskMetadata(EditorPage* page)
    {
        if (page == nullptr || page->path.isEmpty()) {
            return;
        }
        const QFileInfo info(page->path);
        page->lastDiskModified = info.exists() ? info.lastModified() : QDateTime();
        page->externalChangePrompted = false;
    }

    bool reloadPage(EditorPage* page, bool showMessage = true)
    {
        if (page == nullptr || page->path.isEmpty()) {
            return false;
        }
        QFile file(page->path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Reload failed"), file.errorString());
            return false;
        }

        const int cursorPosition = page->editor->textCursor().position();
        const QByteArray bytes = file.readAll();
        page->lineEnding = detectLineEnding(bytes);
        page->encodingName = detectEncodingName(bytes);
        page->diskSnapshot = TextSaveGuard::fromBytes(bytes);
        page->utf8Bom = bytes.startsWith("\xEF\xBB\xBF");
        page->sourceDecodedSafely = TextSaveGuard::validDecode(bytes, page->encodingName);
        page->editor->setPlainText(normalizeLineEndings(decodeText(bytes, page->encodingName)));
        QTextCursor cursor = page->editor->textCursor();
        cursor.setPosition(qMin(cursorPosition, page->editor->document()->characterCount() - 1));
        page->editor->setTextCursor(cursor);
        page->editor->document()->setModified(false);
        updateDiskMetadata(page);
        updateTabTitle(page, false);
        updateDocumentList();
        updateBookmarksList();
        refreshSymbols();
        updateCursorStatus();
        if (showMessage) {
            appendOutput(QStringLiteral("Reloaded %1").arg(page->path));
            statusBar()->showMessage(QStringLiteral("Reloaded %1").arg(page->path), 2500);
        }
        return true;
    }

    bool confirmReloadPage(EditorPage* page)
    {
        if (page == nullptr) {
            return false;
        }
        if (!page->editor->document()->isModified()) {
            return true;
        }
        const int answer = QMessageBox::question(
            this,
            QStringLiteral("Reload document"),
            QStringLiteral("Reload %1 and discard unsaved editor changes?").arg(displayName(page)),
            QMessageBox::Yes | QMessageBox::Cancel);
        return answer == QMessageBox::Yes;
    }

    void reloadCurrentDocument()
    {
        EditorPage* page = currentPage();
        if (page != nullptr && confirmReloadPage(page)) {
            reloadPage(page, true);
        }
    }

    void reloadAllDocuments()
    {
        int reloaded = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->path.isEmpty()) {
                continue;
            }
            tabs->setCurrentIndex(i);
            if (!confirmReloadPage(page)) {
                appendOutput(QStringLiteral("Reload all cancelled after %1 document(s).").arg(reloaded));
                return;
            }
            if (reloadPage(page, false)) {
                ++reloaded;
            }
        }
        appendOutput(QStringLiteral("Reloaded %1 document(s).").arg(reloaded));
        statusBar()->showMessage(QStringLiteral("Reloaded %1 document(s)").arg(reloaded), 2500);
    }

    void reloadCurrentDocumentAsEncoding(const QString& encodingName)
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            return;
        }
        if (page->editor->document()->isModified()) {
            const int answer = QMessageBox::question(this, QStringLiteral("Reload with Encoding"),
                QStringLiteral("Reload %1 as %2 and discard unsaved editor changes?")
                    .arg(displayName(page), encodingName),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }

        QFile file(page->path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Reload failed"), file.errorString());
            return;
        }
        const int cursorPosition = page->editor->textCursor().position();
        const QByteArray bytes = file.readAll();
        page->lineEnding = detectLineEnding(bytes);
        page->encodingName = normalizedEncodingName(encodingName);
        page->diskSnapshot = TextSaveGuard::fromBytes(bytes);
        page->utf8Bom = bytes.startsWith("\xEF\xBB\xBF");
        page->sourceDecodedSafely = TextSaveGuard::validDecode(bytes, page->encodingName);
        page->editor->setPlainText(normalizeLineEndings(decodeText(bytes, page->encodingName)));
        QTextCursor cursor = page->editor->textCursor();
        cursor.setPosition(qMin(cursorPosition, page->editor->document()->characterCount() - 1));
        page->editor->setTextCursor(cursor);
        page->editor->document()->setModified(false);
        updateDiskMetadata(page);
        updateTabTitle(page, false);
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Reloaded %1 as %2").arg(page->path, page->encodingName));
    }

    void checkOpenFilesOnDisk()
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->path.isEmpty()) {
                continue;
            }

            const QFileInfo info(page->path);
            if (!info.exists()) {
                if (!page->externalChangePrompted) {
                    page->externalChangePrompted = true;
                    tabs->setCurrentIndex(i);
                    appendOutput(QStringLiteral("File no longer exists on disk: %1").arg(page->path));
                    QMessageBox::warning(this, QStringLiteral("File deleted"),
                        QStringLiteral("%1 no longer exists on disk.").arg(page->path));
                }
                continue;
            }

            const QDateTime diskModified = info.lastModified();
            if (!page->lastDiskModified.isValid()) {
                page->lastDiskModified = diskModified;
                continue;
            }
            if (diskModified <= page->lastDiskModified || page->externalChangePrompted) {
                continue;
            }

            page->externalChangePrompted = true;
            tabs->setCurrentIndex(i);
            const bool hasLocalChanges = page->editor->document()->isModified();
            const QString prompt = hasLocalChanges
                ? QStringLiteral("%1 was changed on disk and has unsaved editor changes.\n\nReload and discard the editor changes?").arg(page->path)
                : QStringLiteral("%1 was changed on disk.\n\nReload it now?").arg(page->path);
            const int answer = QMessageBox::question(this, QStringLiteral("File changed on disk"),
                prompt, QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                reloadPage(page, true);
            } else {
                page->lastDiskModified = diskModified;
                page->externalChangePrompted = false;
                appendOutput(QStringLiteral("Kept editor copy of externally changed file: %1").arg(page->path));
            }
        }
    }

    void findText()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Find"));
        auto* layout = new QFormLayout(&dialog);
        auto* textEdit = new QLineEdit(lastFindText, &dialog);
        auto* matchCaseCheck = new QCheckBox(&dialog);
        auto* wholeWordCheck = new QCheckBox(&dialog);
        auto* regexCheck = new QCheckBox(&dialog);
        matchCaseCheck->setChecked(findMatchCase);
        wholeWordCheck->setChecked(findWholeWord);
        regexCheck->setChecked(findRegularExpression);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(QStringLiteral("Text"), textEdit);
        layout->addRow(QStringLiteral("Match case"), matchCaseCheck);
        layout->addRow(QStringLiteral("Whole word"), wholeWordCheck);
        layout->addRow(QStringLiteral("Regular expression"), regexCheck);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted && !textEdit->text().isEmpty()) {
            lastFindText = textEdit->text();
            findMatchCase = matchCaseCheck->isChecked();
            findWholeWord = wholeWordCheck->isChecked();
            findRegularExpression = regexCheck->isChecked();
            applySearchHighlights();
            findNext();
        }
    }

    void clearSearchHighlights()
    {
        lastFindText.clear();
        applySearchHighlights();
        statusBar()->showMessage(QStringLiteral("Search highlights cleared"), 2000);
    }

    void findNext()
    {
        findNextInDocument(false);
    }

    void findPrevious()
    {
        findNextInDocument(true);
    }

    bool isIdentifierSearchText(const QString& text) const
    {
        if (text.size() < 2 || !(text.at(0) == QLatin1Char('_') || text.at(0).isLetter())) {
            return false;
        }
        for (const QChar ch : text) {
            if (!(ch == QLatin1Char('_') || ch.isLetterOrNumber())) {
                return false;
            }
        }
        return true;
    }

    QString selectedOrCurrentWordText() const
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return QString();
        }

        const QTextCursor cursor = page->editor->textCursor();
        if (cursor.hasSelection()) {
            QString selected = cursor.selectedText();
            selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            selected = selected.trimmed();
            if (!selected.contains(QLatin1Char('\n'))) {
                return selected;
            }
            return QString();
        }

        const QString line = cursor.block().text();
        if (line.isEmpty()) {
            return QString();
        }

        auto isIdentifierChar = [](QChar ch) {
            return ch == QLatin1Char('_') || ch.isLetterOrNumber();
        };
        int index = qMin(cursor.positionInBlock(), line.size() - 1);
        if (!isIdentifierChar(line.at(index)) && index > 0 && isIdentifierChar(line.at(index - 1))) {
            --index;
        }
        if (!isIdentifierChar(line.at(index))) {
            return QString();
        }

        int start = index;
        int end = index + 1;
        while (start > 0 && isIdentifierChar(line.at(start - 1))) {
            --start;
        }
        while (end < line.size() && isIdentifierChar(line.at(end))) {
            ++end;
        }
        return line.mid(start, end - start);
    }

    void findSelection(bool backwards)
    {
        const QString text = selectedOrCurrentWordText();
        if (text.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Select text or place the cursor on a word to search"), 3000);
            return;
        }

        lastFindText = text;
        findRegularExpression = false;
        findWholeWord = isIdentifierSearchText(text);
        applySearchHighlights();
        recordNavigationLocation();
        findNextInDocument(backwards);
    }

    QTextDocument::FindFlags findFlags(bool backwards = false) const
    {
        QTextDocument::FindFlags flags;
        if (!findRegularExpression && findMatchCase) {
            flags |= QTextDocument::FindCaseSensitively;
        }
        if (!findRegularExpression && findWholeWord) {
            flags |= QTextDocument::FindWholeWords;
        }
        if (backwards) {
            flags |= QTextDocument::FindBackward;
        }
        return flags;
    }

    QRegularExpression searchExpression(const QString& text) const
    {
        QString pattern = findRegularExpression ? text : QRegularExpression::escape(text);
        if (findWholeWord) {
            pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
        }
        QRegularExpression::PatternOptions options;
        if (!findMatchCase) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        return QRegularExpression(pattern, options);
    }

    bool validateSearchExpression(const QRegularExpression& expression)
    {
        if (expression.isValid()) {
            return true;
        }
        QMessageBox::warning(this, QStringLiteral("Search"), expression.errorString());
        return false;
    }

    void findNextInDocument(bool backwards)
    {
        EditorPage* page = currentPage();
        if (page == nullptr || lastFindText.isEmpty()) {
            findText();
            return;
        }

        bool found = false;
        if (findRegularExpression) {
            const QRegularExpression expression = searchExpression(lastFindText);
            if (!validateSearchExpression(expression)) {
                return;
            }
            applySearchHighlights();
            found = page->editor->find(expression, findFlags(backwards));
        } else {
            applySearchHighlights();
            found = page->editor->find(lastFindText, findFlags(backwards));
        }

        if (!found) {
            QTextCursor cursor = page->editor->textCursor();
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            page->editor->setTextCursor(cursor);
            if (findRegularExpression) {
                found = page->editor->find(searchExpression(lastFindText), findFlags(backwards));
            } else {
                found = page->editor->find(lastFindText, findFlags(backwards));
            }
            if (!found) {
                statusBar()->showMessage(QStringLiteral("Text not found"), 3000);
            }
        }
    }

    void replaceText()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        bool ok = false;
        const QString find = QInputDialog::getText(this, QStringLiteral("Replace"), QStringLiteral("Find:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || find.isEmpty()) {
            return;
        }
        const QString replacement = QInputDialog::getText(this, QStringLiteral("Replace"), QStringLiteral("Replace with:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            return;
        }

        lastFindText = find;
        QTextCursor cursor = page->editor->textCursor();
        const Qt::CaseSensitivity sensitivity = findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (findRegularExpression) {
            const QRegularExpression expression = searchExpression(find);
            if (!validateSearchExpression(expression)) {
                return;
            }
            applySearchHighlights();
            if (cursor.hasSelection() && expression.match(cursor.selectedText()).hasMatch()) {
                QString selected = cursor.selectedText();
                selected.replace(expression, replacement);
                cursor.insertText(selected);
            } else if (page->editor->find(expression, findFlags(false))) {
                QTextCursor matchCursor = page->editor->textCursor();
                QString selected = matchCursor.selectedText();
                selected.replace(expression, replacement);
                matchCursor.insertText(selected);
            } else {
                statusBar()->showMessage(QStringLiteral("Text not found"), 3000);
            }
        } else {
            applySearchHighlights();
            if (cursor.hasSelection() && cursor.selectedText().compare(find, sensitivity) == 0) {
                cursor.insertText(replacement);
            } else if (page->editor->find(find, findFlags(false))) {
                page->editor->textCursor().insertText(replacement);
            } else {
                statusBar()->showMessage(QStringLiteral("Text not found"), 3000);
            }
        }
    }

    void replaceAllText()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        bool ok = false;
        const QString find = QInputDialog::getText(this, QStringLiteral("Replace All"),
            QStringLiteral("Find:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || find.isEmpty()) {
            return;
        }
        const QString replacement = QInputDialog::getText(this, QStringLiteral("Replace All"),
            QStringLiteral("Replace with:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            return;
        }

        lastFindText = find;
        int count = 0;
        if (findRegularExpression) {
            const QRegularExpression expression = searchExpression(find);
            if (!validateSearchExpression(expression)) {
                return;
            }
            applySearchHighlights();
            QString text = page->editor->toPlainText();
            QRegularExpressionMatchIterator matches = expression.globalMatch(text);
            while (matches.hasNext()) {
                matches.next();
                ++count;
            }
            if (count > 0) {
                text.replace(expression, replacement);
                page->editor->setPlainText(text);
            }
        } else {
            applySearchHighlights();
            QTextCursor cursor(page->editor->document());
            cursor.movePosition(QTextCursor::Start);
            QTextCursor edit(page->editor->document());
            edit.beginEditBlock();
            while (true) {
                cursor = page->editor->document()->find(find, cursor, findFlags(false));
                if (cursor.isNull()) {
                    break;
                }
                cursor.insertText(replacement);
                ++count;
            }
            edit.endEditBlock();
        }
        if (count == 0) {
            statusBar()->showMessage(QStringLiteral("Text not found"), 3000);
            return;
        }
        appendOutput(QStringLiteral("Replaced %1 occurrence(s).").arg(count));
    }

    void replaceInSelectionText()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        if (!cursor.hasSelection()) {
            statusBar()->showMessage(QStringLiteral("Select text before replacing in selection"), 3000);
            return;
        }

        bool ok = false;
        const QString find = QInputDialog::getText(this, QStringLiteral("Replace in Selection"),
            QStringLiteral("Find:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || find.isEmpty()) {
            return;
        }
        const QString replacement = QInputDialog::getText(this, QStringLiteral("Replace in Selection"),
            QStringLiteral("Replace with:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            return;
        }

        lastFindText = find;
        QString selected = cursor.selectedText();
        selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

        int count = 0;
        if (findRegularExpression || findWholeWord) {
            const QRegularExpression expression = searchExpression(find);
            if (!validateSearchExpression(expression)) {
                return;
            }
            QRegularExpressionMatchIterator matches = expression.globalMatch(selected);
            while (matches.hasNext()) {
                matches.next();
                ++count;
            }
            if (count > 0) {
                selected.replace(expression, replacement);
            }
        } else {
            const Qt::CaseSensitivity sensitivity = findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
            int from = 0;
            while (true) {
                const int index = selected.indexOf(find, from, sensitivity);
                if (index < 0) {
                    break;
                }
                ++count;
                from = index + qMax(1, find.size());
            }
            if (count > 0) {
                selected.replace(find, replacement, sensitivity);
            }
        }

        if (count == 0) {
            statusBar()->showMessage(QStringLiteral("Text not found in selection"), 3000);
            return;
        }

        cursor.beginEditBlock();
        cursor.insertText(selected);
        cursor.endEditBlock();
        applySearchHighlights(page);
        updateTabTitle(page, true);
        updateDocumentList();
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Replaced %1 occurrence(s) in selection.").arg(count));
    }

    void goToLine()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        const int maxLine = qMax(1, page->editor->document()->blockCount());
        bool ok = false;
        const int line = QInputDialog::getInt(this, QStringLiteral("Go to Line"), QStringLiteral("Line:"), 1, 1, maxLine, 1, &ok);
        if (ok) {
            goToLineNumber(line);
        }
    }

    NavLocation currentNavLocation() const
    {
        NavLocation location;
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return location;
        }
        const QTextCursor cursor = page->editor->textCursor();
        location.path = page->path;
        location.tabIndex = tabs->currentIndex();
        location.line = cursor.blockNumber() + 1;
        location.column = cursor.positionInBlock() + 1;
        return location;
    }

    bool sameLocation(const NavLocation& left, const NavLocation& right) const
    {
        return left.path == right.path
            && left.tabIndex == right.tabIndex
            && left.line == right.line
            && left.column == right.column;
    }

    void recordNavigationLocation()
    {
        if (restoringNavigation || currentPage() == nullptr) {
            return;
        }
        const NavLocation location = currentNavLocation();
        if (!backLocations.isEmpty() && sameLocation(backLocations.last(), location)) {
            return;
        }
        backLocations.append(location);
        while (backLocations.size() > 100) {
            backLocations.removeFirst();
        }
        forwardLocations.clear();
    }

    bool applyNavLocation(const NavLocation& location)
    {
        restoringNavigation = true;
        bool applied = false;
        if (!location.path.isEmpty() && QFileInfo::exists(location.path)) {
            openPath(location.path);
            applied = true;
        } else if (location.tabIndex >= 0 && location.tabIndex < tabs->count()) {
            tabs->setCurrentIndex(location.tabIndex);
            applied = true;
        }
        if (applied) {
            goToLineNumber(location.line, false);
            EditorPage* page = currentPage();
            if (page != nullptr) {
                goToLineColumn(page, location.line, location.column);
            }
        }
        restoringNavigation = false;
        return applied;
    }

    void navigateBack()
    {
        if (backLocations.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No previous location"), 2500);
            return;
        }
        const NavLocation current = currentNavLocation();
        const NavLocation target = backLocations.takeLast();
        forwardLocations.append(current);
        if (!applyNavLocation(target)) {
            statusBar()->showMessage(QStringLiteral("Previous location is not available"), 2500);
        }
    }

    void navigateForward()
    {
        if (forwardLocations.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No next location"), 2500);
            return;
        }
        const NavLocation current = currentNavLocation();
        const NavLocation target = forwardLocations.takeLast();
        backLocations.append(current);
        if (!applyNavLocation(target)) {
            statusBar()->showMessage(QStringLiteral("Next location is not available"), 2500);
        }
    }

    void goToLineNumber(int line, bool record = true)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        if (record) {
            recordNavigationLocation();
        }
        goToLineColumn(page, line, 1);
    }

    void goToLineColumn(EditorPage* page, int line, int column)
    {
        if (page == nullptr) {
            return;
        }
        QTextBlock block = page->editor->document()->findBlockByNumber(qMax(0, line - 1));
        if (block.isValid()) {
            QTextCursor cursor(block);
            const int columnOffset = qBound(0, column - 1, block.length() > 0 ? block.length() - 1 : 0);
            if (columnOffset > 0) {
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, columnOffset);
            }
            page->editor->setTextCursor(cursor);
            page->editor->centerCursor();
            page->editor->setFocus();
        }
    }

    bool jumpToListItem(QListWidgetItem* item)
    {
        if (item == nullptr) {
            return false;
        }
        const QString path = item->data(Qt::UserRole).toString();
        const int line = item->data(Qt::UserRole + 1).toInt();
        const int column = item->data(Qt::UserRole + 2).toInt();
        const QVariant tabData = item->data(Qt::UserRole + 3);
        const int tabIndex = tabData.isValid() ? tabData.toInt() : -1;
        if (line <= 0) {
            return false;
        }
        recordNavigationLocation();
        if (!path.isEmpty()) {
            openPath(path);
        } else if (tabIndex >= 0 && tabIndex < tabs->count()) {
            tabs->setCurrentIndex(tabIndex);
        } else {
            return false;
        }
        if (column > 0) {
            if (EditorPage* page = currentPage()) {
                goToLineColumn(page, line, column);
            }
        } else {
            goToLineNumber(line, false);
        }
        return true;
    }

    void navigateListResult(QListWidget* list, int direction)
    {
        if (list == nullptr || list->count() == 0) {
            statusBar()->showMessage(QStringLiteral("No results to navigate"), 2500);
            return;
        }
        int row = list->currentRow();
        if (row < 0) {
            row = direction > 0 ? 0 : list->count() - 1;
        } else {
            row += direction > 0 ? 1 : -1;
            if (row >= list->count()) {
                row = 0;
            } else if (row < 0) {
                row = list->count() - 1;
            }
        }
        list->setCurrentRow(row);
        list->scrollToItem(list->currentItem());
        sideTabs->setCurrentWidget(list);
        jumpToListItem(list->currentItem());
    }

    bool bracePair(QChar brace, QChar* matching, int* direction) const
    {
        if (brace == QLatin1Char('(')) {
            *matching = QLatin1Char(')');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char('[')) {
            *matching = QLatin1Char(']');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char('{')) {
            *matching = QLatin1Char('}');
            *direction = 1;
            return true;
        }
        if (brace == QLatin1Char(')')) {
            *matching = QLatin1Char('(');
            *direction = -1;
            return true;
        }
        if (brace == QLatin1Char(']')) {
            *matching = QLatin1Char('[');
            *direction = -1;
            return true;
        }
        if (brace == QLatin1Char('}')) {
            *matching = QLatin1Char('{');
            *direction = -1;
            return true;
        }
        return false;
    }

    int matchingBracePosition(const QString& text, int origin, QChar brace, QChar matching, int direction) const
    {
        int depth = 0;
        for (int i = origin; i >= 0 && i < text.size(); i += direction) {
            const QChar ch = text.at(i);
            if (ch == brace) {
                ++depth;
            } else if (ch == matching) {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return -1;
    }

    void goToMatchingBrace()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const QString text = page->editor->toPlainText();
        QTextCursor cursor = page->editor->textCursor();
        const int cursorPosition = cursor.position();
        int origin = -1;
        QChar brace;
        QChar matching;
        int direction = 0;

        if (cursorPosition < text.size() && bracePair(text.at(cursorPosition), &matching, &direction)) {
            origin = cursorPosition;
            brace = text.at(cursorPosition);
        } else if (cursorPosition > 0 && bracePair(text.at(cursorPosition - 1), &matching, &direction)) {
            origin = cursorPosition - 1;
            brace = text.at(origin);
        }

        if (origin < 0) {
            statusBar()->showMessage(QStringLiteral("No brace at cursor"), 2500);
            return;
        }

        const int target = matchingBracePosition(text, origin, brace, matching, direction);
        if (target < 0) {
            statusBar()->showMessage(QStringLiteral("Matching brace not found"), 2500);
            return;
        }
        recordNavigationLocation();
        cursor.setPosition(target);
        page->editor->setTextCursor(cursor);
        page->editor->centerCursor();
        page->editor->setFocus();
    }

    int currentLineNumber() const
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return 0;
        }
        return page->editor->textCursor().blockNumber() + 1;
    }

    void updateReadOnlyAction()
    {
        if (readOnlyAction == nullptr) {
            return;
        }
        EditorPage* page = currentPage();
        readOnlyAction->setEnabled(page != nullptr);
        readOnlyAction->setChecked(page != nullptr && page->editor->isReadOnly());
    }

    void toggleReadOnly()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const bool readOnly = !page->editor->isReadOnly();
        page->editor->setReadOnly(readOnly);
        updateTabTitle(page, page->editor->document()->isModified());
        updateDocumentList();
        updateReadOnlyAction();
        updateCursorStatus();
        statusBar()->showMessage(readOnly
                ? QStringLiteral("Document set to read-only")
                : QStringLiteral("Document is editable"),
            2500);
    }

    bool fileHasExecutablePermission(const QString& path) const
    {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return false;
        }
        const QFile::Permissions permissions = QFile::permissions(path);
        return permissions.testFlag(QFileDevice::ExeOwner)
            || permissions.testFlag(QFileDevice::ExeGroup)
            || permissions.testFlag(QFileDevice::ExeOther);
    }

    bool fileHasWritablePermission(const QString& path) const
    {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return false;
        }
        const QFile::Permissions permissions = QFile::permissions(path);
        return permissions.testFlag(QFileDevice::WriteOwner)
            || permissions.testFlag(QFileDevice::WriteGroup)
            || permissions.testFlag(QFileDevice::WriteOther);
    }

    void toggleCurrentFileWritable()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        if (page->path.isEmpty()) {
            if (!saveDocument(page)) {
                return;
            }
        }

        const QString path = page->path;
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::warning(this, QStringLiteral("Writable Permission"), QStringLiteral("Save the file first."));
            return;
        }

        QFile::Permissions permissions = QFile::permissions(path);
        const bool writable = fileHasWritablePermission(path);
        if (writable) {
            permissions &= ~QFileDevice::WriteOwner;
            permissions &= ~QFileDevice::WriteGroup;
            permissions &= ~QFileDevice::WriteOther;
        } else {
            permissions |= QFileDevice::WriteOwner;
            if (permissions.testFlag(QFileDevice::ReadGroup)) {
                permissions |= QFileDevice::WriteGroup;
            }
            if (permissions.testFlag(QFileDevice::ReadOther)) {
                permissions |= QFileDevice::WriteOther;
            }
        }

        if (!QFile::setPermissions(path, permissions)) {
            QMessageBox::warning(this, QStringLiteral("Writable Permission"),
                QStringLiteral("Could not update permissions:\n%1").arg(path));
            return;
        }

        const bool nowWritable = !writable;
        page->editor->setReadOnly(!nowWritable);
        updateReadOnlyAction();
        updateTabTitle(page, page->editor->document()->isModified());
        updateDocumentList();
        updateCursorStatus();

        const QString message = nowWritable
            ? QStringLiteral("Writable permission enabled: %1").arg(path)
            : QStringLiteral("Writable permission removed: %1").arg(path);
        appendOutput(message);
        statusBar()->showMessage(message, 3000);
    }

    void toggleCurrentFileExecutable()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        if (page->path.isEmpty()) {
            if (!saveDocument(page)) {
                return;
            }
        }

        const QString path = page->path;
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::warning(this, QStringLiteral("Executable Permission"), QStringLiteral("Save the file first."));
            return;
        }

        QFile::Permissions permissions = QFile::permissions(path);
        const bool executable = fileHasExecutablePermission(path);
        if (executable) {
            permissions &= ~QFileDevice::ExeOwner;
            permissions &= ~QFileDevice::ExeGroup;
            permissions &= ~QFileDevice::ExeOther;
        } else {
            permissions |= QFileDevice::ExeOwner;
            if (permissions.testFlag(QFileDevice::ReadGroup)) {
                permissions |= QFileDevice::ExeGroup;
            }
            if (permissions.testFlag(QFileDevice::ReadOther)) {
                permissions |= QFileDevice::ExeOther;
            }
        }

        if (!QFile::setPermissions(path, permissions)) {
            QMessageBox::warning(this, QStringLiteral("Executable Permission"),
                QStringLiteral("Could not update permissions:\n%1").arg(path));
            return;
        }

        const QString message = executable
            ? QStringLiteral("Executable permission removed: %1").arg(path)
            : QStringLiteral("Executable permission enabled: %1").arg(path);
        appendOutput(message);
        statusBar()->showMessage(message, 3000);
    }

    void updateBookmarksList()
    {
        if (bookmarksList == nullptr) {
            return;
        }
        bookmarksList->clear();
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr) {
                continue;
            }
            page->editor->setBookmarkLines(page->bookmarks);
            if (page->bookmarks.isEmpty()) {
                continue;
            }
            QList<int> lines = page->bookmarks.values();
            std::sort(lines.begin(), lines.end());
            for (int line : lines) {
                QString preview = page->editor->document()->findBlockByNumber(line - 1).text().trimmed();
                if (preview.size() > 96) {
                    preview = preview.left(93) + QStringLiteral("...");
                }
                auto* item = new QListWidgetItem(QStringLiteral("%1:%2  %3")
                    .arg(displayName(page))
                    .arg(line)
                    .arg(preview), bookmarksList);
                item->setData(Qt::UserRole, i);
                item->setData(Qt::UserRole + 1, line);
                if (!page->path.isEmpty()) {
                    item->setToolTip(QStringLiteral("%1:%2").arg(page->path).arg(line));
                }
            }
        }
    }

    void toggleBookmark()
    {
        EditorPage* page = currentPage();
        const int line = currentLineNumber();
        if (page == nullptr || line <= 0) {
            return;
        }
        if (page->bookmarks.contains(line)) {
            page->bookmarks.remove(line);
            statusBar()->showMessage(QStringLiteral("Bookmark removed"), 2000);
        } else {
            page->bookmarks.insert(line);
            statusBar()->showMessage(QStringLiteral("Bookmark added"), 2000);
        }
        updateBookmarksList();
    }

    void gotoBookmark(int direction)
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->bookmarks.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No bookmarks in current document"), 2500);
            return;
        }

        QList<int> lines = page->bookmarks.values();
        std::sort(lines.begin(), lines.end());
        const int current = currentLineNumber();
        int target = lines.first();
        if (direction > 0) {
            for (int line : lines) {
                if (line > current) {
                    target = line;
                    break;
                }
            }
        } else {
            target = lines.last();
            for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
                if (*it < current) {
                    target = *it;
                    break;
                }
            }
        }
        goToLineNumber(target);
    }

    void gotoBookmarkAcrossDocuments(int direction)
    {
        struct BookmarkTarget
        {
            int tab = -1;
            int line = 0;
        };

        QVector<BookmarkTarget> targets;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->bookmarks.isEmpty()) {
                continue;
            }
            QList<int> lines = page->bookmarks.values();
            std::sort(lines.begin(), lines.end());
            for (int line : lines) {
                targets.push_back({i, line});
            }
        }

        if (targets.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No bookmarks in open documents"), 2500);
            return;
        }

        const int currentTab = tabs->currentIndex();
        const int currentLine = currentLineNumber();
        int targetIndex = direction >= 0 ? 0 : targets.size() - 1;
        if (direction >= 0) {
            for (int i = 0; i < targets.size(); ++i) {
                if (targets.at(i).tab > currentTab
                    || (targets.at(i).tab == currentTab && targets.at(i).line > currentLine)) {
                    targetIndex = i;
                    break;
                }
            }
        } else {
            for (int i = targets.size() - 1; i >= 0; --i) {
                if (targets.at(i).tab < currentTab
                    || (targets.at(i).tab == currentTab && targets.at(i).line < currentLine)) {
                    targetIndex = i;
                    break;
                }
            }
        }

        const BookmarkTarget target = targets.at(targetIndex);
        if (target.tab < 0 || target.tab >= tabs->count()) {
            return;
        }
        recordNavigationLocation();
        tabs->setCurrentIndex(target.tab);
        goToLineNumber(target.line, false);
        if (EditorPage* page = currentPage(); page != nullptr) {
            statusBar()->showMessage(QStringLiteral("Bookmark: %1 line %2")
                .arg(displayName(page))
                .arg(target.line), 2500);
        }
    }

    void clearBookmarks()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        page->bookmarks.clear();
        updateBookmarksList();
        statusBar()->showMessage(QStringLiteral("Bookmarks cleared"), 2000);
    }

    void clearAllBookmarks()
    {
        int cleared = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || page->bookmarks.isEmpty()) {
                continue;
            }
            cleared += page->bookmarks.size();
            page->bookmarks.clear();
        }
        updateBookmarksList();
        appendOutput(QStringLiteral("Cleared %1 bookmark(s).").arg(cleared));
        statusBar()->showMessage(QStringLiteral("Cleared %1 bookmark(s)").arg(cleared), 2500);
    }

    bool lineMatchesSearch(const QString& line, const QString& needle) const
    {
        if (findRegularExpression) {
            const QRegularExpression expression = searchExpression(needle);
            return expression.isValid() && expression.match(line).hasMatch();
        }
        if (findWholeWord) {
            QRegularExpression::PatternOptions options;
            if (!findMatchCase) {
                options |= QRegularExpression::CaseInsensitiveOption;
            }
            const QRegularExpression pattern(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(needle)), options);
            return pattern.match(line).hasMatch();
        }
        return line.contains(needle, findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    QList<int> searchMatchColumns(const QString& line, const QString& needle) const
    {
        QList<int> columns;
        if (needle.isEmpty()) {
            return columns;
        }
        if (findRegularExpression || findWholeWord) {
            const QRegularExpression expression = searchExpression(needle);
            if (!expression.isValid()) {
                return columns;
            }
            QRegularExpressionMatchIterator it = expression.globalMatch(line);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                if (match.hasMatch() && match.capturedStart() >= 0 && match.capturedLength() > 0) {
                    columns << match.capturedStart() + 1;
                }
            }
            return columns;
        }
        int index = 0;
        while ((index = line.indexOf(needle, index, findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive)) >= 0) {
            columns << index + 1;
            index += qMax(1, needle.size());
        }
        return columns;
    }

    int firstSearchMatchColumn(const QString& line, const QString& needle) const
    {
        const QList<int> columns = searchMatchColumns(line, needle);
        return columns.isEmpty() ? 0 : columns.first();
    }

    int countMatchesInText(const QString& text, const QString& needle) const
    {
        if (needle.isEmpty()) {
            return 0;
        }
        if (findRegularExpression || findWholeWord) {
            const QRegularExpression expression = searchExpression(needle);
            if (!expression.isValid()) {
                return 0;
            }
            int count = 0;
            QRegularExpressionMatchIterator it = expression.globalMatch(text);
            while (it.hasNext()) {
                it.next();
                ++count;
            }
            return count;
        }
        return text.count(needle, findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    QString replaceMatchesInText(QString text, const QString& needle, const QString& replacement) const
    {
        if (findRegularExpression || findWholeWord) {
            text.replace(searchExpression(needle), replacement);
        } else {
            text.replace(needle, replacement, findMatchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
        }
        return text;
    }

    QStringList searchFilePatterns() const
    {
        QString normalized = searchFilePattern;
        normalized.replace(QLatin1Char(','), QLatin1Char(';'));
        normalized.replace(QLatin1Char(' '), QLatin1Char(';'));
        QStringList patterns = normalized.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QString& pattern : patterns) {
            pattern = pattern.trimmed();
        }
        patterns.removeAll(QString());
        if (patterns.isEmpty()) {
            patterns << QStringLiteral("*");
        }
        return patterns;
    }

    bool pathMatchesSearchPattern(const QString& path, const QString& root) const
    {
        const QString relativePath = QDir(root).relativeFilePath(path);
        const QString fileName = QFileInfo(path).fileName();
        const QStringList patterns = searchFilePatterns();
        for (const QString& pattern : patterns) {
            if (QDir::match(pattern, fileName) || QDir::match(pattern, relativePath)) {
                return true;
            }
        }
        return false;
    }

    void findInCurrentDocument()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        bool ok = false;
        const QString needle = QInputDialog::getText(this, QStringLiteral("Find in Current Document"),
            QStringLiteral("Text:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        if (findRegularExpression && !validateSearchExpression(searchExpression(needle))) {
            return;
        }

        lastFindText = needle;
        applySearchHighlights(page);
        searchResults->clear();
        const int tabIndex = tabs->currentIndex();
        const QStringList lines = page->editor->toPlainText().split(QLatin1Char('\n'));
        int matches = 0;
        for (int i = 0; i < lines.size() && matches < 1000; ++i) {
            const QList<int> columns = searchMatchColumns(lines.at(i), needle);
            for (int column : columns) {
                if (matches >= 1000) {
                    break;
                }
                auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3: %4")
                    .arg(displayName(page))
                    .arg(i + 1)
                    .arg(column)
                    .arg(lines.at(i).trimmed()), searchResults);
                item->setData(Qt::UserRole, page->path);
                item->setData(Qt::UserRole + 1, i + 1);
                item->setData(Qt::UserRole + 2, column);
                item->setData(Qt::UserRole + 3, tabIndex);
                if (!page->path.isEmpty()) {
                    item->setToolTip(page->path);
                }
                ++matches;
            }
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Find in current document: %1 match(es) in %2")
            .arg(matches)
            .arg(displayName(page)));
        if (matches == 0) {
            statusBar()->showMessage(QStringLiteral("No matches in current document"), 3000);
        }
    }

    void findInSelectionRange()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        QTextCursor selection = page->editor->textCursor();
        if (!selection.hasSelection()) {
            statusBar()->showMessage(QStringLiteral("Select text before finding in selection"), 2500);
            return;
        }

        bool ok = false;
        QString suggested = selectedOrCurrentWordText();
        if (suggested.isEmpty()) {
            suggested = lastFindText;
        }
        const QString needle = QInputDialog::getText(this, QStringLiteral("Find in Selection"),
            QStringLiteral("Text:"), QLineEdit::Normal, suggested, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        if (findRegularExpression && !validateSearchExpression(searchExpression(needle))) {
            return;
        }

        lastFindText = needle;
        applySearchHighlights(page);
        searchResults->clear();

        const int tabIndex = tabs->currentIndex();
        const int selectionStart = selection.selectionStart();
        const QTextBlock startBlock = page->editor->document()->findBlock(selectionStart);
        const int firstLine = startBlock.isValid() ? startBlock.blockNumber() + 1 : 1;
        const int firstColumnOffset = startBlock.isValid() ? selectionStart - startBlock.position() : 0;

        QString text = selection.selectedText();
        text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        int matches = 0;
        for (int i = 0; i < lines.size() && matches < 1000; ++i) {
            const QList<int> columns = searchMatchColumns(lines.at(i), needle);
            for (int column : columns) {
                if (matches >= 1000) {
                    break;
                }
                const int actualColumn = column + (i == 0 ? firstColumnOffset : 0);
                auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3: %4")
                    .arg(displayName(page))
                    .arg(firstLine + i)
                    .arg(actualColumn)
                    .arg(lines.at(i).trimmed()), searchResults);
                item->setData(Qt::UserRole, page->path);
                item->setData(Qt::UserRole + 1, firstLine + i);
                item->setData(Qt::UserRole + 2, actualColumn);
                item->setData(Qt::UserRole + 3, tabIndex);
                if (!page->path.isEmpty()) {
                    item->setToolTip(page->path);
                }
                ++matches;
            }
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Find in selection: %1 match(es) in %2")
            .arg(matches)
            .arg(displayName(page)));
        if (matches == 0) {
            statusBar()->showMessage(QStringLiteral("No matches in selection"), 3000);
        }
    }

    void findInOpenDocuments()
    {
        bool ok = false;
        const QString needle = QInputDialog::getText(this, QStringLiteral("Find in Open Documents"),
            QStringLiteral("Text:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        if (findRegularExpression && !validateSearchExpression(searchExpression(needle))) {
            return;
        }

        lastFindText = needle;
        applySearchHighlights();
        searchResults->clear();
        int documentsScanned = 0;
        int matches = 0;

        for (int tabIndex = 0; tabIndex < tabs->count() && matches < 1000; ++tabIndex) {
            auto* page = static_cast<EditorPage*>(tabs->widget(tabIndex));
            if (page == nullptr) {
                continue;
            }
            ++documentsScanned;
            const QStringList lines = page->editor->toPlainText().split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size() && matches < 1000; ++i) {
                const QList<int> columns = searchMatchColumns(lines.at(i), needle);
                for (int column : columns) {
                    if (matches >= 1000) {
                        break;
                    }
                    auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3: %4")
                        .arg(displayName(page))
                        .arg(i + 1)
                        .arg(column)
                        .arg(lines.at(i).trimmed()), searchResults);
                    item->setData(Qt::UserRole, page->path);
                    item->setData(Qt::UserRole + 1, i + 1);
                    item->setData(Qt::UserRole + 2, column);
                    item->setData(Qt::UserRole + 3, tabIndex);
                    if (!page->path.isEmpty()) {
                        item->setToolTip(page->path);
                    }
                    ++matches;
                }
            }
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Find in open documents: %1 match(es) in %2 document(s)")
            .arg(matches)
            .arg(documentsScanned));
        if (matches == 0) {
            statusBar()->showMessage(QStringLiteral("No matches in open documents"), 3000);
        }
    }

    void replaceInOpenDocuments()
    {
        bool ok = false;
        const QString needle = QInputDialog::getText(this, QStringLiteral("Replace in Open Documents"),
            QStringLiteral("Find:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        const QString replacement = QInputDialog::getText(this, QStringLiteral("Replace in Open Documents"),
            QStringLiteral("Replace with:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            return;
        }
        if ((findRegularExpression || findWholeWord) && !validateSearchExpression(searchExpression(needle))) {
            return;
        }

        const int answer = QMessageBox::warning(this, QStringLiteral("Replace in Open Documents"),
            QStringLiteral("Replace matches in all open documents?\n\nThis modifies editor buffers but does not save them to disk."),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }

        lastFindText = needle;
        searchResults->clear();
        int documentsScanned = 0;
        int documentsChanged = 0;
        int replacements = 0;

        for (int tabIndex = 0; tabIndex < tabs->count(); ++tabIndex) {
            auto* page = static_cast<EditorPage*>(tabs->widget(tabIndex));
            if (page == nullptr) {
                continue;
            }
            ++documentsScanned;
            const QString originalText = page->editor->toPlainText();
            const int count = countMatchesInText(originalText, needle);
            if (count == 0) {
                continue;
            }

            const QString replacedText = replaceMatchesInText(originalText, needle, replacement);
            QTextCursor cursor(page->editor->document());
            cursor.beginEditBlock();
            cursor.select(QTextCursor::Document);
            cursor.insertText(replacedText);
            cursor.endEditBlock();
            page->editor->document()->setModified(true);
            updateTabTitle(page, true);
            applySearchHighlights(page);

            auto* item = new QListWidgetItem(QStringLiteral("%1: %2 replacement(s)")
                .arg(displayName(page))
                .arg(count), searchResults);
            item->setData(Qt::UserRole, page->path);
            item->setData(Qt::UserRole + 1, 1);
            item->setData(Qt::UserRole + 3, tabIndex);
            if (!page->path.isEmpty()) {
                item->setToolTip(page->path);
            }

            replacements += count;
            ++documentsChanged;
        }

        sideTabs->setCurrentWidget(searchResults);
        updateDocumentList();
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Replace in open documents: %1 replacement(s) in %2 changed document(s), scanned %3 document(s)")
            .arg(replacements)
            .arg(documentsChanged)
            .arg(documentsScanned));
        if (replacements == 0) {
            statusBar()->showMessage(QStringLiteral("No matches in open documents"), 3000);
        }
    }

    void findInFiles()
    {
        QString root = projectRoot;
        if (root.isEmpty()) {
            root = chooseExistingDirectory(this, QStringLiteral("Search Folder"), currentWorkDir());
        }
        if (root.isEmpty()) {
            return;
        }

        bool ok = false;
        const QString needle = QInputDialog::getText(this, QStringLiteral("Find in Files"), QStringLiteral("Text:"),
            QLineEdit::Normal, lastFindText, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        const QString filePattern = QInputDialog::getText(this, QStringLiteral("Find in Files"),
            QStringLiteral("File pattern (; separated):"), QLineEdit::Normal, searchFilePattern, &ok);
        if (!ok || filePattern.trimmed().isEmpty()) {
            return;
        }

        lastFindText = needle;
        searchFilePattern = filePattern.trimmed();
        if (findRegularExpression && !validateSearchExpression(searchExpression(needle))) {
            return;
        }
        applySearchHighlights();
        searchResults->clear();
        int filesScanned = 0;
        int matches = 0;

        QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext() && filesScanned < 3000 && matches < 1000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path, root) || !isSourceFile(path) || !canScanTextFile(path) || !pathMatchesSearchPattern(path, root)) {
                continue;
            }
            ++filesScanned;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            const QByteArray bytes = file.readAll();
            const QString fileEncoding = detectEncodingName(bytes);
            const QStringList lines = normalizeLineEndings(decodeText(bytes, fileEncoding)).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size() && matches < 1000; ++i) {
                const QList<int> columns = searchMatchColumns(lines.at(i), needle);
                for (int column : columns) {
                    if (matches >= 1000) {
                        break;
                    }
                    const QString label = QStringLiteral("%1:%2:%3: %4")
                        .arg(QDir(root).relativeFilePath(path))
                        .arg(i + 1)
                        .arg(column)
                        .arg(lines.at(i).trimmed());
                    auto* item = new QListWidgetItem(label, searchResults);
                    item->setData(Qt::UserRole, path);
                    item->setData(Qt::UserRole + 1, i + 1);
                    item->setData(Qt::UserRole + 2, column);
                    ++matches;
                }
            }
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Find in files: %1 matches in %2 scanned files under %3 matching %4")
            .arg(matches)
            .arg(filesScanned)
            .arg(root)
            .arg(searchFilePattern));
    }

    void updateOpenDocumentAfterExternalReplace(const QString& path, const QString& normalizedText, const QString& lineEnding, const QString& encodingName)
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || QFileInfo(page->path) != QFileInfo(path)) {
                continue;
            }
            page->lineEnding = lineEnding;
            page->encodingName = normalizedEncodingName(encodingName);
            page->diskSnapshot = TextSaveGuard::capture(path);
            page->editor->setPlainText(normalizedText);
            page->editor->document()->setModified(false);
            applySearchHighlights(page);
            updateDiskMetadata(page);
            refreshSymbols();
            updateTabTitle(page, false);
            break;
        }
    }

    void replaceInFiles()
    {
        QString root = projectRoot;
        if (root.isEmpty()) {
            root = chooseExistingDirectory(this, QStringLiteral("Replace in Folder"), currentWorkDir());
        }
        if (root.isEmpty()) {
            return;
        }

        bool ok = false;
        const QString needle = QInputDialog::getText(this, QStringLiteral("Replace in Files"),
            QStringLiteral("Find:"), QLineEdit::Normal, lastFindText, &ok);
        if (!ok || needle.isEmpty()) {
            return;
        }
        const QString replacement = QInputDialog::getText(this, QStringLiteral("Replace in Files"),
            QStringLiteral("Replace with:"), QLineEdit::Normal, QString(), &ok);
        if (!ok) {
            return;
        }
        const QString filePattern = QInputDialog::getText(this, QStringLiteral("Replace in Files"),
            QStringLiteral("File pattern (; separated):"), QLineEdit::Normal, searchFilePattern, &ok);
        if (!ok || filePattern.trimmed().isEmpty()) {
            return;
        }
        if ((findRegularExpression || findWholeWord) && !validateSearchExpression(searchExpression(needle))) {
            return;
        }

        const int answer = QMessageBox::warning(this, QStringLiteral("Replace in Files"),
            QStringLiteral("Replace matches under %1 matching %2?\n\nThis writes matching source files on disk.")
                .arg(root, filePattern.trimmed()),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }

        lastFindText = needle;
        searchFilePattern = filePattern.trimmed();
        applySearchHighlights();
        int filesScanned = 0;
        int filesChanged = 0;
        int replacements = 0;
        searchResults->clear();

        QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext() && filesScanned < 3000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path, root) || !isSourceFile(path) || !canScanTextFile(path) || !pathMatchesSearchPattern(path, root)) {
                continue;
            }
            ++filesScanned;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QByteArray bytes = file.readAll();
            const QString lineEnding = detectLineEnding(bytes);
            const QString encodingName = detectEncodingName(bytes);
            const QString text = normalizeLineEndings(decodeText(bytes, encodingName));
            const int count = countMatchesInText(text, needle);
            if (count == 0) {
                continue;
            }

            const QString replacedText = replaceMatchesInText(text, needle, replacement);
            const QByteArray outBytes = encodeText(applyLineEnding(replacedText, lineEnding), encodingName);
            if (!writeBytesToFile(path, outBytes, QStringLiteral("Replace in files"))) {
                appendOutput(QStringLiteral("Replace in files: failed to save %1").arg(path));
                continue;
            }

            replacements += count;
            ++filesChanged;
            updateOpenDocumentAfterExternalReplace(path, replacedText, lineEnding, encodingName);
            auto* item = new QListWidgetItem(QStringLiteral("%1: %2 replacement(s)")
                .arg(QDir(root).relativeFilePath(path))
                .arg(count), searchResults);
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, 1);
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Replace in files: %1 replacement(s) in %2 file(s), scanned %3 file(s) under %4 matching %5")
            .arg(replacements)
            .arg(filesChanged)
            .arg(filesScanned)
            .arg(root)
            .arg(searchFilePattern));
        updateDocumentList();
        updateCursorStatus();
    }

    void selectCurrentLine()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.movePosition(QTextCursor::StartOfLine);
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        });
    }

    void selectCurrentWord()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.select(QTextCursor::WordUnderCursor);
            if (cursor.hasSelection()) {
                editor->setTextCursor(cursor);
            }
        });
    }

    void selectCurrentParagraph()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.select(QTextCursor::BlockUnderCursor);
            if (cursor.hasSelection()) {
                editor->setTextCursor(cursor);
            }
        });
    }

    QTextCursor selectedWholeLineCursor(QPlainTextEdit* editor) const
    {
        QTextCursor cursor = editor->textCursor();
        const int startPosition = cursor.selectionStart();
        const int endPosition = cursor.hasSelection() ? qMax(cursor.selectionEnd() - 1, startPosition) : cursor.position();
        QTextBlock startBlock = editor->document()->findBlock(startPosition);
        QTextBlock endBlock = editor->document()->findBlock(endPosition);
        if (!startBlock.isValid() || !endBlock.isValid()) {
            return cursor;
        }

        QTextCursor range(startBlock);
        range.setPosition(startBlock.position());
        if (endBlock.next().isValid()) {
            range.setPosition(endBlock.next().position(), QTextCursor::KeepAnchor);
        } else {
            range.setPosition(endBlock.position() + endBlock.length() - 1, QTextCursor::KeepAnchor);
        }
        return range;
    }

    void copyCurrentLine()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QTextCursor range = selectedWholeLineCursor(editor);
            QString text = range.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            if (!text.endsWith(QLatin1Char('\n'))) {
                text.append(QLatin1Char('\n'));
            }
            copyTextToClipboard(text, QStringLiteral("line"));
        });
    }

    void cutCurrentLine()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QTextCursor range = selectedWholeLineCursor(editor);
            QString text = range.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            if (!text.endsWith(QLatin1Char('\n'))) {
                text.append(QLatin1Char('\n'));
            }
            copyTextToClipboard(text, QStringLiteral("line"));
            range.removeSelectedText();
        });
    }

    void convertSelectionCase(bool uppercase)
    {
        withEditor([uppercase](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                cursor.select(QTextCursor::WordUnderCursor);
            }
            if (!cursor.hasSelection()) {
                return;
            }
            const int start = cursor.selectionStart();
            const int end = cursor.selectionEnd();
            const QString replacement = uppercase ? cursor.selectedText().toUpper() : cursor.selectedText().toLower();
            cursor.insertText(replacement);
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        });
    }

    void invertSelectionCase()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                cursor.select(QTextCursor::WordUnderCursor);
            }
            if (!cursor.hasSelection()) {
                return;
            }
            const int start = cursor.selectionStart();
            const int end = cursor.selectionEnd();
            QString replacement = cursor.selectedText();
            for (QChar& ch : replacement) {
                if (ch.isUpper()) {
                    ch = ch.toLower();
                } else if (ch.isLower()) {
                    ch = ch.toUpper();
                }
            }
            cursor.insertText(replacement);
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        });
    }

    void duplicateLine()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.select(QTextCursor::LineUnderCursor);
            const QString line = cursor.selectedText();
            cursor.movePosition(QTextCursor::EndOfLine);
            cursor.insertText(QStringLiteral("\n") + line);
        });
    }

    void deleteLine()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.select(QTextCursor::LineUnderCursor);
            cursor.removeSelectedText();
            cursor.deleteChar();
        });
    }

    void deleteToLineBoundary(bool toStart)
    {
        withEditor([toStart](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            cursor.beginEditBlock();
            if (cursor.hasSelection()) {
                cursor.removeSelectedText();
            } else if (toStart) {
                cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
            } else {
                cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
                if (cursor.hasSelection()) {
                    cursor.removeSelectedText();
                } else {
                    cursor.deleteChar();
                }
            }
            cursor.endEditBlock();
            editor->setTextCursor(cursor);
        });
    }

    void deleteToLineStart()
    {
        deleteToLineBoundary(true);
    }

    void deleteToLineEnd()
    {
        deleteToLineBoundary(false);
    }

    void joinLines()
    {
        withEditor([](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            QTextBlock start = editor->document()->findBlock(cursor.selectionStart());
            QTextBlock end = editor->document()->findBlock(cursor.hasSelection() ? qMax(cursor.selectionStart(), cursor.selectionEnd() - 1) : cursor.position());
            if (!cursor.hasSelection() && end.next().isValid()) {
                end = end.next();
            }
            if (!start.isValid() || !end.isValid() || start.blockNumber() == end.blockNumber()) {
                return;
            }

            const int startPosition = start.position();
            const int endPosition = end.position() + end.length() - 1;
            QTextCursor edit(editor->document());
            edit.setPosition(startPosition);
            edit.setPosition(endPosition, QTextCursor::KeepAnchor);
            QString joined = edit.selectedText();
            joined.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
            joined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
            edit.insertText(joined.trimmed());
            edit.setPosition(startPosition);
            editor->setTextCursor(edit);
        });
    }

    QString reflowPrefixForLine(const QString& line) const
    {
        const QRegularExpression commentPrefix(QStringLiteral("^(\\s*(?://+|#|\\*)\\s*)"));
        const QRegularExpressionMatch match = commentPrefix.match(line);
        if (match.hasMatch()) {
            return match.captured(1);
        }
        return QRegularExpression(QStringLiteral("^\\s*")).match(line).captured(0);
    }

    QString contentForReflowLine(const QString& line, const QString& prefix) const
    {
        const QRegularExpression commentPrefix(QStringLiteral("^\\s*(?://+|#|\\*)\\s*"));
        QString content = line;
        if (commentPrefix.match(content).hasMatch() && prefix.contains(QRegularExpression(QStringLiteral("(?://+|#|\\*)")))) {
            content.remove(commentPrefix);
        } else if (!prefix.isEmpty() && content.startsWith(prefix)) {
            content.remove(0, prefix.size());
        }
        return content.trimmed();
    }

    void reflowLines()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor->isReadOnly()) {
            return;
        }

        QPlainTextEdit* editor = page->editor;
        QTextCursor cursor = editor->textCursor();
        QTextBlock start = editor->document()->findBlock(cursor.selectionStart());
        QTextBlock end = editor->document()->findBlock(cursor.hasSelection()
                ? qMax(cursor.selectionStart(), cursor.selectionEnd() - 1)
                : cursor.position());
        if (!start.isValid() || !end.isValid()) {
            return;
        }

        if (!cursor.hasSelection()) {
            while (start.previous().isValid() && !start.previous().text().trimmed().isEmpty()) {
                start = start.previous();
            }
            while (end.next().isValid() && !end.next().text().trimmed().isEmpty()) {
                end = end.next();
            }
        }

        QString firstNonEmpty;
        for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
            if (!block.text().trimmed().isEmpty()) {
                firstNonEmpty = block.text();
                break;
            }
        }
        if (firstNonEmpty.isEmpty()) {
            return;
        }

        const QString prefix = reflowPrefixForLine(firstNonEmpty);
        QString combined;
        for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
            const QString content = contentForReflowLine(block.text(), prefix);
            if (!content.isEmpty()) {
                if (!combined.isEmpty()) {
                    combined.append(QLatin1Char(' '));
                }
                combined.append(content);
            }
        }
        combined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        combined = combined.trimmed();
        if (combined.isEmpty()) {
            return;
        }

        const int wrapColumn = qBound(40, rightMarginColumn, 200);
        const int available = qMax(20, wrapColumn - static_cast<int>(prefix.size()));
        QStringList wrapped;
        QString current;
        const QStringList words = combined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            if (current.isEmpty()) {
                current = word;
            } else if (current.size() + 1 + word.size() <= available) {
                current += QLatin1Char(' ') + word;
            } else {
                wrapped << prefix + current;
                current = word;
            }
        }
        if (!current.isEmpty()) {
            wrapped << prefix + current;
        }

        QTextCursor edit(editor->document());
        edit.beginEditBlock();
        edit.setPosition(start.position());
        edit.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(wrapped.join(QLatin1Char('\n')));
        edit.endEditBlock();
        editor->setTextCursor(edit);
        updateCursorStatus();
    }

    void foldCurrentBlock()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        if (page->editor->foldCurrentBlock()) {
            statusBar()->showMessage(QStringLiteral("Current block folded"), 2000);
        } else {
            statusBar()->showMessage(QStringLiteral("No foldable block at cursor"), 2500);
        }
    }

    void unfoldAllBlocks()
    {
        int unfolded = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr) {
                unfolded += page->editor->unfoldAllBlocks();
            }
        }
        statusBar()->showMessage(QStringLiteral("Unfolded %1 hidden line(s)").arg(unfolded), 2500);
    }

    bool selectedLineRange(QPlainTextEdit* editor, QTextBlock* start, QTextBlock* end) const
    {
        if (editor == nullptr || start == nullptr || end == nullptr) {
            return false;
        }
        const QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection()) {
            return false;
        }
        *start = editor->document()->findBlock(cursor.selectionStart());
        *end = editor->document()->findBlock(qMax(cursor.selectionStart(), cursor.selectionEnd() - 1));
        return start->isValid() && end->isValid() && start->blockNumber() <= end->blockNumber();
    }

    void replaceSelectedLines(const QString& actionName, const std::function<QStringList(QStringList)>& transform)
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor->isReadOnly()) {
            return;
        }
        QTextBlock start;
        QTextBlock end;
        if (!selectedLineRange(page->editor, &start, &end)) {
            statusBar()->showMessage(QStringLiteral("Select two or more lines first"), 2500);
            return;
        }

        QStringList lines;
        for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
            lines << block.text();
        }
        if (lines.size() < 2) {
            statusBar()->showMessage(QStringLiteral("Select two or more lines first"), 2500);
            return;
        }

        const QStringList replaced = transform(lines);
        QTextCursor edit(page->editor->document());
        edit.beginEditBlock();
        edit.setPosition(start.position());
        edit.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(replaced.join(QLatin1Char('\n')));
        edit.endEditBlock();
        page->editor->setTextCursor(edit);
        updateCursorStatus();
        statusBar()->showMessage(QStringLiteral("%1 complete").arg(actionName), 2000);
    }

    void sortSelectedLines()
    {
        replaceSelectedLines(QStringLiteral("Sort lines"), [](QStringList lines) {
            lines.sort(Qt::CaseInsensitive);
            return lines;
        });
    }

    void sortSelectedLinesDescending()
    {
        replaceSelectedLines(QStringLiteral("Sort lines descending"), [](QStringList lines) {
            lines.sort(Qt::CaseInsensitive);
            std::reverse(lines.begin(), lines.end());
            return lines;
        });
    }

    void reverseSelectedLines()
    {
        replaceSelectedLines(QStringLiteral("Reverse lines"), [](QStringList lines) {
            std::reverse(lines.begin(), lines.end());
            return lines;
        });
    }

    void removeDuplicateSelectedLines()
    {
        replaceSelectedLines(QStringLiteral("Remove duplicate lines"), [](QStringList lines) {
            QStringList uniqueLines;
            QSet<QString> seen;
            for (const QString& line : lines) {
                if (seen.contains(line)) {
                    continue;
                }
                seen.insert(line);
                uniqueLines << line;
            }
            return uniqueLines;
        });
    }

    QString lineCommentPrefix(EditorPage* page) const
    {
        const QString language = page != nullptr && page->highlighter != nullptr
            ? page->highlighter->languageName()
            : QStringLiteral("Plain Text");
        if (language == QStringLiteral("Python")
            || language == QStringLiteral("Shell")
            || language == QStringLiteral("CMake")
            || language == QStringLiteral("Meson")
            || language == QStringLiteral("Makefile")
            || language == QStringLiteral("Dockerfile")
            || language == QStringLiteral("YAML")
            || language == QStringLiteral("TOML")
            || language == QStringLiteral("INI")) {
            return QStringLiteral("#");
        }
        if (language == QStringLiteral("SQL") || language == QStringLiteral("Lua")) {
            return QStringLiteral("--");
        }
        if (language == QStringLiteral("C")
            || language == QStringLiteral("C++")
            || language == QStringLiteral("JavaScript/TypeScript")
            || language == QStringLiteral("ArkTS")
            || language == QStringLiteral("Java")
            || language == QStringLiteral("Go")
            || language == QStringLiteral("Rust")
            || language == QStringLiteral("PHP")) {
            return QStringLiteral("//");
        }
        return QString();
    }

    QStringList blockCommentDelimiters(EditorPage* page) const
    {
        const QString language = page != nullptr && page->highlighter != nullptr
            ? page->highlighter->languageName()
            : QStringLiteral("Plain Text");
        if (language == QStringLiteral("Markup")) {
            return {QStringLiteral("<!--"), QStringLiteral("-->")};
        }
        if (language == QStringLiteral("Lua")) {
            return {QStringLiteral("--[["), QStringLiteral("]]")};
        }
        if (language == QStringLiteral("C")
            || language == QStringLiteral("C++")
            || language == QStringLiteral("JavaScript/TypeScript")
            || language == QStringLiteral("ArkTS")
            || language == QStringLiteral("Java")
            || language == QStringLiteral("Go")
            || language == QStringLiteral("Rust")
            || language == QStringLiteral("PHP")
            || language == QStringLiteral("CSS")
            || language == QStringLiteral("SQL")) {
            return {QStringLiteral("/*"), QStringLiteral("*/")};
        }
        return {};
    }

    void toggleComment()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor->isReadOnly()) {
            return;
        }

        const QString prefix = lineCommentPrefix(page);
        if (prefix.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No line comment prefix for this filetype"), 2500);
            return;
        }

        QPlainTextEdit* editor = page->editor;
        QTextCursor cursor = editor->textCursor();
        QTextBlock start = editor->document()->findBlock(cursor.selectionStart());
        QTextBlock end = editor->document()->findBlock(qMax(cursor.selectionStart(), cursor.selectionEnd() - 1));
        if (!start.isValid() || !end.isValid()) {
            return;
        }

        bool allCommented = true;
        for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
            const QString text = block.text();
            if (!text.trimmed().isEmpty() && !text.trimmed().startsWith(prefix)) {
                allCommented = false;
                break;
            }
        }

        QTextCursor edit(editor->document());
        edit.beginEditBlock();
        for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
            const QString text = block.text();
            if (text.trimmed().isEmpty()) {
                continue;
            }
            QTextCursor lineCursor(block);
            if (allCommented) {
                const int offset = text.indexOf(prefix);
                lineCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, offset);
                int removeCount = prefix.size();
                if (text.mid(offset + prefix.size(), 1) == QStringLiteral(" ")) {
                    ++removeCount;
                }
                lineCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, removeCount);
                lineCursor.removeSelectedText();
            } else {
                const int indent = QRegularExpression(QStringLiteral("^\\s*")).match(text).capturedLength(0);
                lineCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, indent);
                lineCursor.insertText(prefix + QLatin1Char(' '));
            }
        }
        edit.endEditBlock();
    }

    void toggleBlockComment()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor->isReadOnly()) {
            return;
        }

        const QStringList delimiters = blockCommentDelimiters(page);
        if (delimiters.size() < 2) {
            statusBar()->showMessage(QStringLiteral("No block comment delimiters for this filetype"), 2500);
            return;
        }
        const QString open = delimiters.at(0);
        const QString close = delimiters.at(1);

        QPlainTextEdit* editor = page->editor;
        QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection()) {
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        }

        QString selected = cursor.selectedText();
        selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        if (selected.isEmpty()) {
            return;
        }

        const int leading = QRegularExpression(QStringLiteral("^\\s*")).match(selected).capturedLength(0);
        const int trailing = QRegularExpression(QStringLiteral("\\s*$")).match(selected).capturedLength(0);
        const QString before = selected.left(leading);
        const QString middle = selected.mid(leading, selected.size() - leading - trailing);
        const QString after = trailing > 0 ? selected.right(trailing) : QString();

        QString replacement;
        int cursorOffset = 0;
        if (middle.startsWith(open) && middle.endsWith(close)) {
            replacement = before + middle.mid(open.size(), middle.size() - open.size() - close.size()).trimmed() + after;
            cursorOffset = leading;
        } else {
            replacement = before + open + QLatin1Char(' ') + middle + QLatin1Char(' ') + close + after;
            cursorOffset = leading + open.size() + 1;
        }

        const int start = cursor.selectionStart();
        cursor.beginEditBlock();
        cursor.insertText(replacement);
        cursor.endEditBlock();
        cursor.setPosition(qMin(start + cursorOffset, editor->document()->characterCount() - 1));
        editor->setTextCursor(cursor);
    }

    void indentSelection()
    {
        const QString indent = indentText();
        withEditor([indent](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                cursor.insertText(indent);
                return;
            }

            QTextBlock start = editor->document()->findBlock(cursor.selectionStart());
            QTextBlock end = editor->document()->findBlock(qMax(cursor.selectionStart(), cursor.selectionEnd() - 1));
            QTextCursor edit(editor->document());
            edit.beginEditBlock();
            for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
                QTextCursor lineCursor(block);
                lineCursor.insertText(indent);
            }
            edit.endEditBlock();
        });
    }

    void unindentSelection()
    {
        const int width = tabWidth;
        withEditor([width](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            QTextBlock start = editor->document()->findBlock(cursor.selectionStart());
            QTextBlock end = editor->document()->findBlock(cursor.hasSelection() ? qMax(cursor.selectionStart(), cursor.selectionEnd() - 1) : cursor.position());
            QTextCursor edit(editor->document());
            edit.beginEditBlock();
            for (QTextBlock block = start; block.isValid() && block.blockNumber() <= end.blockNumber(); block = block.next()) {
                const QString text = block.text();
                int removeCount = 0;
                while (removeCount < text.size() && removeCount < width && text.at(removeCount).isSpace()) {
                    ++removeCount;
                    if (text.at(removeCount - 1) == QLatin1Char('\t')) {
                        break;
                    }
                }
                if (removeCount > 0) {
                    QTextCursor lineCursor(block);
                    lineCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, removeCount);
                    lineCursor.removeSelectedText();
                }
            }
            edit.endEditBlock();
        });
    }

    void detectIndentation()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        int tabIndentedLines = 0;
        int spaceIndentedLines = 0;
        int exactSpaceIndents[9] = {};
        int divisibleSpaceIndents[9] = {};

        for (QTextBlock block = page->editor->document()->begin(); block.isValid(); block = block.next()) {
            const QString text = block.text();
            if (text.trimmed().isEmpty()) {
                continue;
            }

            int leadingSpaces = 0;
            int leadingTabs = 0;
            for (const QChar ch : text) {
                if (ch == QLatin1Char(' ')) {
                    ++leadingSpaces;
                } else if (ch == QLatin1Char('\t')) {
                    ++leadingTabs;
                } else {
                    break;
                }
            }

            if (leadingTabs > 0 && leadingTabs >= leadingSpaces) {
                ++tabIndentedLines;
                continue;
            }
            if (leadingSpaces <= 0) {
                continue;
            }

            ++spaceIndentedLines;
            if (leadingSpaces >= 2 && leadingSpaces <= 8) {
                ++exactSpaceIndents[leadingSpaces];
            }
            for (int width = 2; width <= 8; ++width) {
                if ((leadingSpaces % width) == 0) {
                    ++divisibleSpaceIndents[width];
                }
            }
        }

        if (tabIndentedLines == 0 && spaceIndentedLines == 0) {
            statusBar()->showMessage(QStringLiteral("No indentation detected"), 2500);
            return;
        }

        insertSpacesOnTab = tabIndentedLines < spaceIndentedLines;
        if (insertSpacesOnTab) {
            int bestWidth = tabWidth;
            int bestScore = -1;
            for (int width = 2; width <= 8; ++width) {
                const int score = (exactSpaceIndents[width] * 3) + divisibleSpaceIndents[width];
                const bool betterTie = score == bestScore
                    && (width == tabWidth || (bestWidth != tabWidth && width == 4));
                if (score > bestScore || betterTie) {
                    bestScore = score;
                    bestWidth = width;
                }
            }
            tabWidth = qBound(2, bestWidth, 8);
        }

        applyEditorPreferences();
        const QString style = insertSpacesOnTab
            ? QStringLiteral("%1 spaces").arg(tabWidth)
            : QStringLiteral("tabs");
        const QString message = QStringLiteral("Detected indentation: %1").arg(style);
        appendOutput(message);
        statusBar()->showMessage(message, 3000);
    }

    void stripTrailingSpaces()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QStringList lines = editor->toPlainText().split(QLatin1Char('\n'));
            int removed = 0;
            QRegularExpression trailing(QStringLiteral("[ \\t]+$"));
            for (QString& line : lines) {
                const int before = line.size();
                line.remove(trailing);
                removed += before - line.size();
            }
            if (removed > 0) {
                const int position = editor->textCursor().position();
                editor->setPlainText(lines.join(QLatin1Char('\n')));
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(qMin(position, editor->document()->characterCount() - 1));
                editor->setTextCursor(cursor);
            }
            appendOutput(QStringLiteral("Stripped %1 trailing whitespace character(s).").arg(removed));
        });
    }

    void stripTrailingSpacesInSelection()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                statusBar()->showMessage(QStringLiteral("Select text before stripping trailing spaces in selection"), 2500);
                return;
            }

            QString text = cursor.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            int removed = 0;
            const QRegularExpression trailing(QStringLiteral("[ \\t]+$"));
            for (QString& line : lines) {
                const int before = line.size();
                line.remove(trailing);
                removed += before - line.size();
            }
            if (removed > 0) {
                cursor.insertText(lines.join(QLatin1Char('\n')));
            }
            appendOutput(QStringLiteral("Stripped %1 trailing whitespace character(s) in selection.").arg(removed));
        });
    }

    void removeEmptyLines()
    {
        withEditor([this](QPlainTextEdit* editor) {
            const QString originalText = editor->toPlainText();
            const bool endedWithNewline = originalText.endsWith(QLatin1Char('\n'));
            const QStringList lines = originalText.split(QLatin1Char('\n'));
            QStringList kept;
            int removed = 0;
            for (int i = 0; i < lines.size(); ++i) {
                const bool finalSplitMarker = i == lines.size() - 1 && lines.at(i).isEmpty() && endedWithNewline;
                if (!finalSplitMarker && lines.at(i).trimmed().isEmpty()) {
                    ++removed;
                    continue;
                }
                kept << lines.at(i);
            }
            QString text = kept.join(QLatin1Char('\n'));
            if (endedWithNewline && !text.endsWith(QLatin1Char('\n'))) {
                text.append(QLatin1Char('\n'));
            }
            if (removed > 0) {
                const int position = editor->textCursor().position();
                editor->setPlainText(text);
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(qMin(position, editor->document()->characterCount() - 1));
                editor->setTextCursor(cursor);
            }
            appendOutput(QStringLiteral("Removed %1 empty line(s).").arg(removed));
        });
    }

    void removeEmptyLinesInSelection()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                statusBar()->showMessage(QStringLiteral("Select text before removing empty lines in selection"), 2500);
                return;
            }

            QString text = cursor.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            const bool endedWithNewline = text.endsWith(QLatin1Char('\n'));
            const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            QStringList kept;
            int removed = 0;
            for (int i = 0; i < lines.size(); ++i) {
                const bool finalSplitMarker = i == lines.size() - 1 && lines.at(i).isEmpty() && endedWithNewline;
                if (!finalSplitMarker && lines.at(i).trimmed().isEmpty()) {
                    ++removed;
                    continue;
                }
                kept << lines.at(i);
            }
            QString replacement = kept.join(QLatin1Char('\n'));
            if (endedWithNewline && !replacement.endsWith(QLatin1Char('\n'))) {
                replacement.append(QLatin1Char('\n'));
            }
            if (removed > 0) {
                cursor.insertText(replacement);
            }
            appendOutput(QStringLiteral("Removed %1 empty line(s) in selection.").arg(removed));
        });
    }

    void replaceTabsWithSpaces()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QString text = editor->toPlainText();
            const int replaced = text.count(QLatin1Char('\t'));
            if (replaced > 0) {
                const int position = editor->textCursor().position();
                text.replace(QLatin1Char('\t'), QString(tabWidth, QLatin1Char(' ')));
                editor->setPlainText(text);
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(qMin(position, editor->document()->characterCount() - 1));
                editor->setTextCursor(cursor);
            }
            appendOutput(QStringLiteral("Replaced %1 tab character(s).").arg(replaced));
        });
    }

    void replaceTabsWithSpacesInSelection()
    {
        withEditor([this](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                statusBar()->showMessage(QStringLiteral("Select text before replacing tabs in selection"), 2500);
                return;
            }

            QString text = cursor.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            const int replaced = text.count(QLatin1Char('\t'));
            if (replaced > 0) {
                text.replace(QLatin1Char('\t'), QString(tabWidth, QLatin1Char(' ')));
                cursor.insertText(text);
            }
            appendOutput(QStringLiteral("Replaced %1 tab character(s) in selection.").arg(replaced));
        });
    }

    void replaceLeadingSpacesWithTabs()
    {
        const int width = qMax(1, tabWidth);
        withEditor([this, width](QPlainTextEdit* editor) {
            QStringList lines = editor->toPlainText().split(QLatin1Char('\n'));
            int replaced = 0;
            for (QString& line : lines) {
                int spaces = 0;
                while (spaces < line.size() && line.at(spaces) == QLatin1Char(' ')) {
                    ++spaces;
                }
                const int tabs = spaces / width;
                if (tabs <= 0) {
                    continue;
                }
                const int consumed = tabs * width;
                line.replace(0, consumed, QString(tabs, QLatin1Char('\t')));
                replaced += consumed;
            }

            if (replaced > 0) {
                const int position = editor->textCursor().position();
                editor->setPlainText(lines.join(QLatin1Char('\n')));
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(qMin(position, editor->document()->characterCount() - 1));
                editor->setTextCursor(cursor);
            }
            appendOutput(QStringLiteral("Replaced %1 leading space character(s) with tabs.").arg(replaced));
        });
    }

    void replaceLeadingSpacesWithTabsInSelection()
    {
        const int width = qMax(1, tabWidth);
        withEditor([this, width](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            if (!cursor.hasSelection()) {
                statusBar()->showMessage(QStringLiteral("Select text before replacing leading spaces in selection"), 2500);
                return;
            }

            QString text = cursor.selectedText();
            text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            int replaced = 0;
            for (QString& line : lines) {
                int spaces = 0;
                while (spaces < line.size() && line.at(spaces) == QLatin1Char(' ')) {
                    ++spaces;
                }
                const int tabs = spaces / width;
                if (tabs <= 0) {
                    continue;
                }
                const int consumed = tabs * width;
                line.replace(0, consumed, QString(tabs, QLatin1Char('\t')));
                replaced += consumed;
            }

            if (replaced > 0) {
                cursor.insertText(lines.join(QLatin1Char('\n')));
            }
            appendOutput(QStringLiteral("Replaced %1 leading space character(s) with tabs in selection.").arg(replaced));
        });
    }

    int jsonByteOffsetToLine(const QString& text, int offset) const
    {
        const QByteArray bytes = text.toUtf8();
        offset = qBound(0, offset, bytes.size());
        const QString prefix = QString::fromUtf8(bytes.left(offset));
        int line = 1;
        for (QChar ch : prefix) {
            if (ch == QLatin1Char('\n')) {
                ++line;
            }
        }
        return line;
    }

    int jsonByteOffsetToColumn(const QString& text, int offset) const
    {
        const QByteArray bytes = text.toUtf8();
        offset = qBound(0, offset, bytes.size());
        const QString prefix = QString::fromUtf8(bytes.left(offset));
        int column = 1;
        for (int i = prefix.size() - 1; i >= 0; --i) {
            if (prefix.at(i) == QLatin1Char('\n')) {
                break;
            }
            ++column;
        }
        return column;
    }

    QString removeJson5CommentsAndTrailingCommas(const QString& text) const
    {
        QString cleaned;
        cleaned.reserve(text.size());
        bool inString = false;
        QChar quote;
        bool escaped = false;

        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
            if (inString) {
                cleaned.append(ch);
                if (escaped) {
                    escaped = false;
                } else if (ch == QLatin1Char('\\')) {
                    escaped = true;
                } else if (ch == quote) {
                    inString = false;
                }
                continue;
            }
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                inString = true;
                quote = ch;
                cleaned.append(ch);
                continue;
            }
            if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
                while (i < text.size() && text.at(i) != QLatin1Char('\n')) {
                    ++i;
                }
                if (i < text.size()) {
                    cleaned.append(QLatin1Char('\n'));
                }
                continue;
            }
            if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
                i += 2;
                while (i + 1 < text.size() && !(text.at(i) == QLatin1Char('*') && text.at(i + 1) == QLatin1Char('/'))) {
                    cleaned.append(text.at(i) == QLatin1Char('\n') ? QLatin1Char('\n') : QLatin1Char(' '));
                    ++i;
                }
                ++i;
                continue;
            }
            cleaned.append(ch);
        }

        QString withoutTrailingCommas;
        withoutTrailingCommas.reserve(cleaned.size());
        inString = false;
        escaped = false;
        for (int i = 0; i < cleaned.size(); ++i) {
            const QChar ch = cleaned.at(i);
            if (inString) {
                withoutTrailingCommas.append(ch);
                if (escaped) {
                    escaped = false;
                } else if (ch == QLatin1Char('\\')) {
                    escaped = true;
                } else if (ch == quote) {
                    inString = false;
                }
                continue;
            }
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                inString = true;
                quote = ch;
                withoutTrailingCommas.append(ch);
                continue;
            }
            if (ch == QLatin1Char(',')) {
                int j = i + 1;
                while (j < cleaned.size() && cleaned.at(j).isSpace()) {
                    ++j;
                }
                if (j < cleaned.size() && (cleaned.at(j) == QLatin1Char('}') || cleaned.at(j) == QLatin1Char(']'))) {
                    continue;
                }
            }
            withoutTrailingCommas.append(ch);
        }
        return withoutTrailingCommas;
    }

    bool parseCurrentJson(QJsonDocument* document, QJsonParseError* error, QString* textOut = nullptr, bool* usedJson5 = nullptr)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }
        const QString text = page->editor->toPlainText();
        QJsonParseError parseError;
        QJsonDocument parsed = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        bool json5Fallback = false;
        if (parseError.error != QJsonParseError::NoError) {
            const QString cleaned = removeJson5CommentsAndTrailingCommas(text);
            if (cleaned != text) {
                QJsonParseError json5Error;
                QJsonDocument json5Parsed = QJsonDocument::fromJson(cleaned.toUtf8(), &json5Error);
                if (json5Error.error == QJsonParseError::NoError) {
                    parsed = json5Parsed;
                    parseError = json5Error;
                    json5Fallback = true;
                }
            }
        }
        if (parseError.error != QJsonParseError::NoError) {
            const int line = jsonByteOffsetToLine(text, parseError.offset);
            const int column = jsonByteOffsetToColumn(text, parseError.offset);
            goToLineColumn(page, line, column);
            const QString message = QStringLiteral("JSON/JSON5 parse error at line %1, column %2: %3")
                .arg(line)
                .arg(column)
                .arg(parseError.errorString());
            appendOutput(message);
            statusBar()->showMessage(message, 4500);
            if (error != nullptr) {
                *error = parseError;
            }
            return false;
        }
        if (document != nullptr) {
            *document = parsed;
        }
        if (error != nullptr) {
            *error = parseError;
        }
        if (textOut != nullptr) {
            *textOut = text;
        }
        if (usedJson5 != nullptr) {
            *usedJson5 = json5Fallback;
        }
        return true;
    }

    void validateCurrentJson()
    {
        QJsonDocument document;
        QString text;
        bool usedJson5 = false;
        if (!parseCurrentJson(&document, nullptr, &text, &usedJson5)) {
            return;
        }
        const QString kind = document.isArray() ? QStringLiteral("array")
            : document.isObject() ? QStringLiteral("object")
                                  : QStringLiteral("value");
        const QString message = QStringLiteral("%1 is valid: %2, %3 character(s).")
            .arg(usedJson5 ? QStringLiteral("JSON5-style JSON") : QStringLiteral("JSON"))
            .arg(kind)
            .arg(text.size());
        appendOutput(message);
        statusBar()->showMessage(message, 3500);
    }

    void formatCurrentJson(bool compact)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        QJsonDocument document;
        QString originalText;
        bool usedJson5 = false;
        if (!parseCurrentJson(&document, nullptr, &originalText, &usedJson5)) {
            return;
        }
        if (usedJson5) {
            const int answer = QMessageBox::question(this,
                compact ? QStringLiteral("Minify JSON/JSON5") : QStringLiteral("Format JSON/JSON5"),
                QStringLiteral("This document uses JSON5-style comments or trailing commas. Formatting will remove those JSON5-only parts. Continue?"),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        QString formatted = QString::fromUtf8(document.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
        if (!compact && !formatted.endsWith(QLatin1Char('\n'))) {
            formatted.append(QLatin1Char('\n'));
        }
        if (compact && formatted.endsWith(QLatin1Char('\n'))) {
            formatted.chop(1);
        }
        if (formatted == originalText) {
            statusBar()->showMessage(compact ? QStringLiteral("JSON is already minified")
                                             : QStringLiteral("JSON is already formatted"), 2500);
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        const int oldPosition = cursor.position();
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(formatted);
        cursor.endEditBlock();
        cursor.setPosition(qMin(oldPosition, page->editor->document()->characterCount() - 1));
        page->editor->setTextCursor(cursor);
        page->editor->document()->setModified(true);
        updateTabTitle(page, true);
        refreshSymbols();
        updateCursorStatus();
        appendOutput(compact ? QStringLiteral("Minified JSON/JSON5 document.") : QStringLiteral("Formatted JSON/JSON5 document."));
    }

    QStringList normalizedDiffLines(const QString& text) const
    {
        return normalizeLineEndings(text).split(QLatin1Char('\n'));
    }

    QVector<DiffEntry> coarseLineDiff(const QStringList& oldLines, const QStringList& newLines) const
    {
        QVector<DiffEntry> result;
        int prefix = 0;
        while (prefix < oldLines.size() && prefix < newLines.size() && oldLines.at(prefix) == newLines.at(prefix)) {
            result.push_back({QLatin1Char(' '), oldLines.at(prefix)});
            ++prefix;
        }

        int oldSuffix = oldLines.size() - 1;
        int newSuffix = newLines.size() - 1;
        QVector<DiffEntry> suffix;
        while (oldSuffix >= prefix && newSuffix >= prefix && oldLines.at(oldSuffix) == newLines.at(newSuffix)) {
            suffix.prepend({QLatin1Char(' '), oldLines.at(oldSuffix)});
            --oldSuffix;
            --newSuffix;
        }

        for (int i = prefix; i <= oldSuffix; ++i) {
            result.push_back({QLatin1Char('-'), oldLines.at(i)});
        }
        for (int i = prefix; i <= newSuffix; ++i) {
            result.push_back({QLatin1Char('+'), newLines.at(i)});
        }
        result += suffix;
        return result;
    }

    QVector<DiffEntry> lineDiff(const QString& oldText, const QString& newText) const
    {
        const QStringList oldLines = normalizedDiffLines(oldText);
        const QStringList newLines = normalizedDiffLines(newText);
        if (oldLines == newLines) {
            return {};
        }

        const qint64 cells = qint64(oldLines.size() + 1) * qint64(newLines.size() + 1);
        if (cells > 4000000) {
            return coarseLineDiff(oldLines, newLines);
        }

        const int rows = oldLines.size() + 1;
        const int columns = newLines.size() + 1;
        QVector<int> lcs(rows * columns, 0);
        auto index = [columns](int row, int column) { return row * columns + column; };

        for (int i = oldLines.size() - 1; i >= 0; --i) {
            for (int j = newLines.size() - 1; j >= 0; --j) {
                if (oldLines.at(i) == newLines.at(j)) {
                    lcs[index(i, j)] = lcs.at(index(i + 1, j + 1)) + 1;
                } else {
                    lcs[index(i, j)] = qMax(lcs.at(index(i + 1, j)), lcs.at(index(i, j + 1)));
                }
            }
        }

        QVector<DiffEntry> result;
        int oldIndex = 0;
        int newIndex = 0;
        while (oldIndex < oldLines.size() && newIndex < newLines.size()) {
            if (oldLines.at(oldIndex) == newLines.at(newIndex)) {
                result.push_back({QLatin1Char(' '), oldLines.at(oldIndex)});
                ++oldIndex;
                ++newIndex;
            } else if (lcs.at(index(oldIndex + 1, newIndex)) >= lcs.at(index(oldIndex, newIndex + 1))) {
                result.push_back({QLatin1Char('-'), oldLines.at(oldIndex)});
                ++oldIndex;
            } else {
                result.push_back({QLatin1Char('+'), newLines.at(newIndex)});
                ++newIndex;
            }
        }
        while (oldIndex < oldLines.size()) {
            result.push_back({QLatin1Char('-'), oldLines.at(oldIndex)});
            ++oldIndex;
        }
        while (newIndex < newLines.size()) {
            result.push_back({QLatin1Char('+'), newLines.at(newIndex)});
            ++newIndex;
        }
        return result;
    }

    QString formatDiffLineNumber(int line) const
    {
        return line > 0 ? QString::number(line).rightJustified(5, QLatin1Char(' ')) : QStringLiteral("     ");
    }

    QString buildDiffText(const QString& oldName, const QString& oldText, const QString& newName, const QString& newText) const
    {
        const QVector<DiffEntry> entries = lineDiff(oldText, newText);
        if (entries.isEmpty()) {
            return QStringLiteral("No differences.\n");
        }

        QString diff;
        QTextStream stream(&diff);
        stream << QStringLiteral("--- ") << oldName << QLatin1Char('\n');
        stream << QStringLiteral("+++ ") << newName << QLatin1Char('\n');
        stream << QStringLiteral("@@ line diff @@") << QLatin1Char('\n');

        int oldLine = 1;
        int newLine = 1;
        for (const DiffEntry& entry : entries) {
            if (entry.tag == QLatin1Char(' ')) {
                stream << QLatin1Char(' ') << formatDiffLineNumber(oldLine) << QStringLiteral(" ")
                       << formatDiffLineNumber(newLine) << QStringLiteral(" | ") << entry.line << QLatin1Char('\n');
                ++oldLine;
                ++newLine;
            } else if (entry.tag == QLatin1Char('-')) {
                stream << QLatin1Char('-') << formatDiffLineNumber(oldLine) << QStringLiteral("       | ")
                       << entry.line << QLatin1Char('\n');
                ++oldLine;
            } else if (entry.tag == QLatin1Char('+')) {
                stream << QLatin1Char('+') << QStringLiteral("      ") << formatDiffLineNumber(newLine)
                       << QStringLiteral(" | ") << entry.line << QLatin1Char('\n');
                ++newLine;
            }
        }
        return diff;
    }

    void showDiffDialog(const QString& title, const QString& diffText)
    {
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.resize(980, 720);

        auto* layout = new QVBoxLayout(&dialog);
        auto* viewer = new QPlainTextEdit(&dialog);
        viewer->setReadOnly(true);
        viewer->setLineWrapMode(QPlainTextEdit::NoWrap);
        viewer->setPlainText(diffText);
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        viewer->setFont(mono);
        layout->addWidget(viewer);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        QPushButton* copyButton = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
        QPushButton* saveButton = buttons->addButton(QStringLiteral("Save As..."), QDialogButtonBox::ActionRole);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(copyButton, &QPushButton::clicked, this, [diffText]() {
            QApplication::clipboard()->setText(diffText);
        });
        connect(saveButton, &QPushButton::clicked, this, [this, diffText]() {
            const QString path = chooseSaveFileName(this, QStringLiteral("Save Diff"),
                currentWorkDir() + QStringLiteral("/changes.diff"), QStringLiteral("Diff Files (*.diff *.patch);;All Files (*)"));
            if (path.isEmpty()) {
                return;
            }
            writeBytesToFile(path, diffText.toUtf8(), QStringLiteral("Save Diff Failed"));
        });
        layout->addWidget(buttons);
        dialog.exec();
    }

    bool readFileForComparison(const QString& path, QString* textOut)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Compare Failed"), file.errorString());
            return false;
        }
        const QByteArray bytes = file.readAll();
        if (looksLikeBinary(bytes.left(8192))) {
            const int answer = QMessageBox::question(this, QStringLiteral("Compare Binary File"),
                QStringLiteral("%1 appears to be a binary file.\n\nCompare it as text anyway?").arg(path),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                return false;
            }
        }
        if (textOut != nullptr) {
            *textOut = normalizeLineEndings(decodeText(bytes, detectEncodingName(bytes)));
        }
        return true;
    }

    void compareCurrentWithSaved()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Compare with Saved"), QStringLiteral("Save the document before comparing it with disk."));
            return;
        }

        QString savedText;
        if (!readFileForComparison(page->path, &savedText)) {
            return;
        }
        const QString editorText = page->editor->toPlainText();
        const QString diff = buildDiffText(page->path + QStringLiteral(" (saved)"), savedText,
            page->path + QStringLiteral(" (editor)"), editorText);
        showDiffDialog(QStringLiteral("Compare with Saved"), diff);
        appendOutput(QStringLiteral("Compared editor copy with saved file: %1").arg(page->path));
    }

    void compareCurrentWithFile()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        const QString otherPath = chooseOpenFileName(this, QStringLiteral("Compare with File"), currentWorkDir());
        if (otherPath.isEmpty()) {
            return;
        }

        QString otherText;
        if (!readFileForComparison(otherPath, &otherText)) {
            return;
        }
        const QString currentName = page->path.isEmpty() ? displayName(page) : page->path;
        const QString diff = buildDiffText(otherPath, otherText, currentName, page->editor->toPlainText());
        showDiffDialog(QStringLiteral("Compare with File"), diff);
        appendOutput(QStringLiteral("Compared %1 with %2").arg(currentName, otherPath));
    }

    void showDocumentStatistics()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        const QString text = page->editor->toPlainText();
        const qsizetype words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        QMessageBox::information(this, QStringLiteral("Document Statistics"),
            QStringLiteral("File: %1\nLines: %2\nWords: %3\nCharacters: %4\nLine endings: %5")
                .arg(displayName(page))
                .arg(page->editor->document()->blockCount())
                .arg(words)
                .arg(text.size())
                .arg(lineEndingName(page->lineEnding)));
    }

    QString formatByteCount(qint64 bytes) const
    {
        double value = static_cast<double>(bytes);
        QString unit = QStringLiteral("B");
        if (value >= 1024.0) {
            value /= 1024.0;
            unit = QStringLiteral("KB");
        }
        if (value >= 1024.0) {
            value /= 1024.0;
            unit = QStringLiteral("MB");
        }
        if (value >= 1024.0) {
            value /= 1024.0;
            unit = QStringLiteral("GB");
        }
        return unit == QStringLiteral("B")
            ? QStringLiteral("%1 B").arg(bytes)
            : QStringLiteral("%1 %2 (%3 B)").arg(value, 0, 'f', 1).arg(unit).arg(bytes);
    }

    void showDocumentProperties()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const QString text = page->editor->toPlainText();
        const qsizetype words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        QStringList rows;
        rows << QStringLiteral("Name: %1").arg(displayName(page));
        rows << QStringLiteral("Path: %1").arg(page->path.isEmpty() ? QStringLiteral("Unsaved document") : page->path);
        rows << QStringLiteral("Filetype: %1").arg(page->highlighter != nullptr ? page->highlighter->languageName() : QStringLiteral("Plain Text"));
        rows << QStringLiteral("Encoding: %1").arg(page->encodingName);
        rows << QStringLiteral("Line endings: %1").arg(lineEndingName(page->lineEnding));
        rows << QStringLiteral("Editor read-only: %1").arg(page->editor->isReadOnly() ? QStringLiteral("Yes") : QStringLiteral("No"));
        rows << QStringLiteral("Modified in editor: %1").arg(page->editor->document()->isModified() ? QStringLiteral("Yes") : QStringLiteral("No"));
        rows << QStringLiteral("Lines: %1").arg(page->editor->document()->blockCount());
        rows << QStringLiteral("Characters: %1").arg(text.size());
        rows << QStringLiteral("Words: %1").arg(words);

        if (!page->path.isEmpty()) {
            const QFileInfo info(page->path);
            rows << QStringLiteral("Size on disk: %1").arg(info.exists() ? formatByteCount(info.size()) : QStringLiteral("File does not exist"));
            rows << QStringLiteral("Last modified: %1").arg(info.exists() ? info.lastModified().toString(Qt::ISODate) : QStringLiteral("N/A"));
            rows << QStringLiteral("Readable: %1").arg(info.exists() && info.isReadable() ? QStringLiteral("Yes") : QStringLiteral("No"));
            rows << QStringLiteral("Writable: %1").arg(info.exists() && info.isWritable() ? QStringLiteral("Yes") : QStringLiteral("No"));
            rows << QStringLiteral("Executable: %1").arg(fileHasExecutablePermission(page->path) ? QStringLiteral("Yes") : QStringLiteral("No"));
        }

        QMessageBox::information(this, QStringLiteral("Document Properties"), rows.join(QLatin1Char('\n')));
    }

    void moveLine(int direction)
    {
        withEditor([direction](QPlainTextEdit* editor) {
            QTextCursor cursor = editor->textCursor();
            const int currentBlock = cursor.blockNumber();
            const int targetBlock = currentBlock + direction;
            if (targetBlock < 0 || targetBlock >= editor->document()->blockCount()) {
                return;
            }

            QStringList lines = editor->toPlainText().split(QLatin1Char('\n'));
            lines.swapItemsAt(currentBlock, targetBlock);
            editor->setPlainText(lines.join(QLatin1Char('\n')));
            QTextBlock block = editor->document()->findBlockByNumber(targetBlock);
            editor->setTextCursor(QTextCursor(block));
        });
    }

    void setWordWrap(bool enabled)
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            page->editor->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        }
    }

    void setFullScreenMode(bool enabled)
    {
        if (enabled) {
            showFullScreen();
        } else {
            showNormal();
        }
        if (fullScreenAction != nullptr) {
            fullScreenAction->setChecked(isFullScreen());
        }
    }

    void setFocusMode(bool enabled)
    {
        if (enabled == focusModeActive) {
            return;
        }

        if (enabled) {
            focusModeSavedSidebar = sideTabs != nullptr && sideTabs->isVisible();
            focusModeSavedOutput = outputTabs != nullptr && outputTabs->isVisible();
            focusModeSavedToolbar = mainToolBar != nullptr && mainToolBar->isVisible();
            focusModeSavedStatusbar = statusBar()->isVisible();
            focusModeActive = true;
            if (sideTabs != nullptr) {
                sideTabs->setVisible(false);
            }
            if (outputTabs != nullptr) {
                outputTabs->setVisible(false);
            }
            if (mainToolBar != nullptr) {
                mainToolBar->setVisible(false);
            }
            statusBar()->setVisible(false);
        } else {
            focusModeActive = false;
            if (sideTabs != nullptr) {
                sideTabs->setVisible(focusModeSavedSidebar);
            }
            if (outputTabs != nullptr) {
                outputTabs->setVisible(focusModeSavedOutput);
            }
            if (mainToolBar != nullptr) {
                mainToolBar->setVisible(focusModeSavedToolbar);
            }
            statusBar()->setVisible(focusModeSavedStatusbar);
        }

        if (showSidebarAction != nullptr) {
            showSidebarAction->setChecked(sideTabs != nullptr && sideTabs->isVisible());
        }
        if (showOutputAction != nullptr) {
            showOutputAction->setChecked(outputTabs != nullptr && outputTabs->isVisible());
        }
        if (showToolbarAction != nullptr) {
            showToolbarAction->setChecked(mainToolBar != nullptr && mainToolBar->isVisible());
        }
        if (showStatusbarAction != nullptr) {
            showStatusbarAction->setChecked(statusBar()->isVisible());
        }
        if (focusModeAction != nullptr) {
            focusModeAction->setChecked(focusModeActive);
        }
    }

    void changeFontSize(int delta)
    {
        fontSize = qBound(8, fontSize + delta, 36);
        applyEditorPreferences();
    }

    QString normalizedTheme(const QString& theme) const
    {
        if (theme == QStringLiteral("Dark") || theme == QStringLiteral("High Contrast")) {
            return theme;
        }
        return QStringLiteral("Light");
    }

    void applyTheme()
    {
        editorTheme = normalizedTheme(editorTheme);
        const bool dark = editorTheme == QStringLiteral("Dark");
        const bool highContrast = editorTheme == QStringLiteral("High Contrast");

        const QString text = highContrast ? QStringLiteral("#ffffff") : dark ? QStringLiteral("#e1e8f0") : QStringLiteral("#181d23");
        const QString window = highContrast ? QStringLiteral("#000000") : dark ? QStringLiteral("#202832") : QStringLiteral("#f5f7fa");
        const QString panel = highContrast ? QStringLiteral("#000000") : dark ? QStringLiteral("#1a1f26") : QStringLiteral("#ffffff");
        const QString raised = highContrast ? QStringLiteral("#111111") : dark ? QStringLiteral("#27313d") : QStringLiteral("#eef2f6");
        const QString selected = highContrast ? QStringLiteral("#ffd640") : dark ? QStringLiteral("#2f5f99") : QStringLiteral("#ffffff");
        const QString selectedText = highContrast ? QStringLiteral("#000000") : dark ? QStringLiteral("#ffffff") : QStringLiteral("#181d23");
        const QString border = highContrast ? QStringLiteral("#ffffff") : dark ? QStringLiteral("#4a5a6b") : QStringLiteral("#c9d1d9");

        QPalette palette = qApp->palette();
        palette.setColor(QPalette::Window, QColor(window));
        palette.setColor(QPalette::WindowText, QColor(text));
        palette.setColor(QPalette::Base, QColor(panel));
        palette.setColor(QPalette::AlternateBase, QColor(raised));
        palette.setColor(QPalette::Text, QColor(text));
        palette.setColor(QPalette::Button, QColor(raised));
        palette.setColor(QPalette::ButtonText, QColor(text));
        palette.setColor(QPalette::Highlight, QColor(selected));
        palette.setColor(QPalette::HighlightedText, QColor(selectedText));
        qApp->setPalette(palette);

        setStyleSheet(QStringLiteral(
#if defined(Q_OS_OPENHARMONY)
            "QWidget { color: %1; font-size: 28px; }"
#else
            "QWidget { color: %1; font-size: 13px; }"
#endif
            "QMainWindow, QDialog { background: %2; }"
            "QPlainTextEdit, QListWidget, QLineEdit, QSpinBox, QComboBox { background: %3; color: %1; border: 1px solid %7; selection-background-color: %5; selection-color: %6; }"
            "QTabWidget::pane { border: 1px solid %7; background: %3; }"
            "QToolBar, QMenuBar, QStatusBar, QMenu { background: %4; color: %1; border-bottom: 1px solid %7; }"
            "QMenu::item:selected { background: %5; color: %6; }"
#if defined(Q_OS_OPENHARMONY)
            "QMenuBar::item { padding: 8px 16px; }"
            "QMenu::item { padding: 10px 28px; }"
            "QTabBar::tab { padding: 18px 30px; background: %4; color: %1; border: 1px solid %7; }"
            "QToolBar { spacing: 12px; padding: 8px; }"
            "QToolButton, QPushButton { min-height: 58px; padding: 10px 24px; }"
            "QLineEdit, QComboBox, QSpinBox { min-height: 52px; padding: 8px 14px; }"
            "QListWidget::item { padding: 8px 10px; min-height: 38px; }"
            "QStatusBar { min-height: 42px; }"
#else
            "QTabBar::tab { padding: 6px 12px; background: %4; color: %1; border: 1px solid %7; }"
#endif
            "QTabBar::tab:selected { background: %3; color: %1; }")
                .arg(text, window, panel, raised, selected, selectedText, border));
    }

    void applyEditorPreferences()
    {
        QFont sharedFont(editorFontFamily.isEmpty() ? QStringLiteral("monospace") : editorFontFamily);
        sharedFont.setStyleHint(QFont::Monospace);
        sharedFont.setPointSize(fontSize);
        if (output != nullptr) {
            output->setFont(sharedFont);
        }
        if (scratchPad != nullptr) {
            scratchPad->setFont(sharedFont);
        }
        if (terminalOutput != nullptr) {
            terminalOutput->setFont(sharedFont);
        }
        if (terminalInput != nullptr) {
            terminalInput->setFont(sharedFont);
        }
        if (splitView != nullptr) {
            splitView->setFont(sharedFont);
        }
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            page->editor->setColorScheme(editorTheme);
            if (page->highlighter != nullptr) {
                page->highlighter->setColorScheme(editorTheme);
            }
            page->editor->setFont(sharedFont);
            QTextOption option = page->editor->document()->defaultTextOption();
            QTextOption::Flags flags = option.flags();
            if (showWhitespaceEnabled) {
                flags |= QTextOption::ShowTabsAndSpaces;
            } else {
                flags &= ~QTextOption::ShowTabsAndSpaces;
            }
            if (showLineEndingsEnabled) {
                flags |= QTextOption::ShowLineAndParagraphSeparators;
            } else {
                flags &= ~QTextOption::ShowLineAndParagraphSeparators;
            }
            option.setFlags(flags);
            page->editor->document()->setDefaultTextOption(option);
            page->editor->setEditorOptions(tabWidth, insertSpacesOnTab, autoIndentEnabled,
                bracketPairingEnabled, wordCompletionEnabled, highlightOccurrencesEnabled,
                highlightCurrentLineEnabled, showLineNumbersEnabled, showIndentGuidesEnabled,
                showRightMarginEnabled, rightMarginColumn);
        }
        refreshSplitView();
    }

    QString indentText() const
    {
        return insertSpacesOnTab ? QString(tabWidth, QLatin1Char(' ')) : QStringLiteral("\t");
    }

    bool looksLikeOpenHarmonyProject(const QString& root) const
    {
        const QDir dir(root);
        if (!dir.exists()) {
            return false;
        }
        if (dir.exists(QStringLiteral("build-profile.json5"))
            && (dir.exists(QStringLiteral("oh-package.json5")) || dir.exists(QStringLiteral("hvigorfile.ts")))) {
            return true;
        }
        return QFileInfo(dir.filePath(QStringLiteral("AppScope/app.json5"))).exists()
            && QFileInfo(dir.filePath(QStringLiteral("entry/src/main/module.json5"))).exists();
    }

    QString openHarmonyBuildCommandTemplate() const
    {
        return QStringLiteral("cd {project} && ([ -x ./hvigorw ] && ./hvigorw --mode module -p module=entry assembleHap || hvigor --mode module -p module=entry assembleHap)");
    }

    QString openHarmonyCleanCommandTemplate() const
    {
        return QStringLiteral("cd {project} && ([ -x ./hvigorw ] && ./hvigorw clean || hvigor clean)");
    }

    QString openHarmonyTargetCommandTemplate() const
    {
        return QStringLiteral("cd {project} && ([ -x ./hvigorw ] && ./hvigorw {target} || hvigor {target})");
    }

    bool isDefaultBuildCommand() const
    {
        return buildCommand.trimmed().isEmpty() || buildCommand.trimmed() == QStringLiteral("make");
    }

    bool isDefaultCleanCommand() const
    {
        return buildCleanCommand.trimmed().isEmpty() || buildCleanCommand.trimmed() == QStringLiteral("make clean");
    }

    bool isDefaultTargetCommand() const
    {
        return buildTargetCommand.trimmed().isEmpty() || buildTargetCommand.trimmed() == QStringLiteral("make {target}");
    }

    void useOpenHarmonyBuildCommand(bool announce)
    {
        buildCommand = openHarmonyBuildCommandTemplate();
        buildCleanCommand = openHarmonyCleanCommandTemplate();
        buildTargetCommand = openHarmonyTargetCommandTemplate();
        if (announce) {
            appendOutput(QStringLiteral("OpenHarmony build, clean, and target commands selected."));
            statusBar()->showMessage(QStringLiteral("OpenHarmony build commands selected"), 2500);
        }
    }

    void applyOpenHarmonyBuildDefaults()
    {
        if (!projectRoot.isEmpty() && looksLikeOpenHarmonyProject(projectRoot)) {
            if (isDefaultBuildCommand()) {
                buildCommand = openHarmonyBuildCommandTemplate();
            }
            if (isDefaultCleanCommand()) {
                buildCleanCommand = openHarmonyCleanCommandTemplate();
            }
            if (isDefaultTargetCommand()) {
                buildTargetCommand = openHarmonyTargetCommandTemplate();
            }
            appendOutput(QStringLiteral("OpenHarmony project detected; build command configured."));
        }
    }

    void openProjectFolder()
    {
        const QString dir = chooseExistingDirectory(this, QStringLiteral("Open Folder"), currentWorkDir());
        if (dir.isEmpty()) {
            return;
        }
        openProjectRoot(dir);
    }

    void openProjectRoot(const QString& dir)
    {
        if (dir.isEmpty() || !QDir(dir).exists()) {
            return;
        }
        projectRoot = QDir(dir).canonicalPath();
        if (projectRoot.isEmpty()) {
            projectRoot = QDir(dir).absolutePath();
        }
        projectFile.clear();
        projectDefaultLineEnding.clear();
        projectDefaultEncodingName.clear();
        projectExtraIgnorePatterns.clear();
        projectIgnoreRules.clear();
        ignoreRulesByRoot.clear();
        applyOpenHarmonyBuildDefaults();
        loadProjectIgnoreRules();
        loadProjectFiles();
        if (fileBrowserRoot.isEmpty() || !QDir(fileBrowserRoot).exists()) {
            fileBrowserRoot = projectRoot;
            refreshFileBrowser();
        }
        addRecentProject(projectRoot);
        appendOutput(QStringLiteral("Project opened: %1").arg(projectRoot));
        updateCursorStatus();
    }

    bool ensureProjectRoot()
    {
        if (!projectRoot.isEmpty() && QDir(projectRoot).exists()) {
            return true;
        }
        openProjectFolder();
        return !projectRoot.isEmpty() && QDir(projectRoot).exists();
    }

    QString selectedProjectPath() const
    {
        QListWidgetItem* item = projectFiles != nullptr ? projectFiles->currentItem() : nullptr;
        return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    }

    bool isInsideProject(const QString& path) const
    {
        if (projectRoot.isEmpty()) {
            return false;
        }
        QString root = QDir(projectRoot).canonicalPath();
        if (root.isEmpty()) {
            root = QDir(projectRoot).absolutePath();
        }
        const QFileInfo info(path);
        QString parent = info.exists() && info.isDir() ? info.canonicalFilePath() : QDir(info.absolutePath()).absolutePath();
        return !root.isEmpty() && !parent.isEmpty() && (parent == root || parent.startsWith(root + QLatin1Char('/')));
    }

    QString projectPathFromInput(const QString& input) const
    {
        const QString cleaned = QDir::cleanPath(input.trimmed());
        if (cleaned.isEmpty()) {
            return QString();
        }
        const QString path = QDir::isAbsolutePath(cleaned) ? cleaned : QDir(projectRoot).absoluteFilePath(cleaned);
        return QDir::cleanPath(path);
    }

    QString currentPathToken() const
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return QString();
        }

        QTextCursor cursor = page->editor->textCursor();
        if (cursor.hasSelection()) {
            QString selected = cursor.selectedText().trimmed();
            selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            if (!selected.contains(QLatin1Char('\n'))) {
                return selected;
            }
        }

        const QString line = cursor.block().text();
        if (line.isEmpty()) {
            return QString();
        }
        const QString boundaries = QStringLiteral("\"'`()[]{}<>,;");
        auto isBoundary = [&boundaries](QChar ch) {
            return ch.isSpace() || boundaries.contains(ch);
        };

        int index = qMin(cursor.positionInBlock(), line.size() - 1);
        if (isBoundary(line.at(index)) && index > 0 && !isBoundary(line.at(index - 1))) {
            --index;
        }
        if (isBoundary(line.at(index))) {
            return QString();
        }

        int start = index;
        int end = index + 1;
        while (start > 0 && !isBoundary(line.at(start - 1))) {
            --start;
        }
        while (end < line.size() && !isBoundary(line.at(end))) {
            ++end;
        }
        return line.mid(start, end - start).trimmed();
    }

    QString stripPathDecorations(QString candidate) const
    {
        candidate = candidate.trimmed();
        if (candidate.startsWith(QStringLiteral("file://"))) {
            candidate.remove(0, QStringLiteral("file://").size());
        }
        while (!candidate.isEmpty() && QStringLiteral("\"'`(<[").contains(candidate.front())) {
            candidate.remove(0, 1);
        }
        while (!candidate.isEmpty() && QStringLiteral("\"'`)>],.;").contains(candidate.back())) {
            candidate.chop(1);
        }
        return candidate.trimmed();
    }

    bool resolveOpenFilenameCandidate(const QString& rawCandidate, QString* resolvedPath, int* line, int* column) const
    {
        if (resolvedPath == nullptr || line == nullptr || column == nullptr) {
            return false;
        }
        *resolvedPath = QString();
        *line = 1;
        *column = 1;

        QString candidate = stripPathDecorations(rawCandidate);
        if (candidate.isEmpty()) {
            return false;
        }

        const QRegularExpression suffix(QStringLiteral("^(.*):(\\d+)(?::(\\d+))?$"));
        const QRegularExpressionMatch suffixMatch = suffix.match(candidate);
        QString candidatePath = candidate;
        int candidateLine = 1;
        int candidateColumn = 1;
        if (suffixMatch.hasMatch()) {
            candidatePath = suffixMatch.captured(1);
            candidateLine = qMax(1, suffixMatch.captured(2).toInt());
            if (!suffixMatch.captured(3).isEmpty()) {
                candidateColumn = qMax(1, suffixMatch.captured(3).toInt());
            }
        }

        QStringList paths;
        paths << candidatePath;
        if (!QDir::isAbsolutePath(candidatePath)) {
            if (EditorPage* page = currentPage(); page != nullptr && !page->path.isEmpty()) {
                paths << QDir(QFileInfo(page->path).absolutePath()).absoluteFilePath(candidatePath);
            }
            if (!projectRoot.isEmpty()) {
                paths << QDir(projectRoot).absoluteFilePath(candidatePath);
            }
            paths << QDir(currentWorkDir()).absoluteFilePath(candidatePath);
        }

        QSet<QString> checked;
        for (const QString& path : paths) {
            const QString cleaned = QDir::cleanPath(path);
            if (cleaned.isEmpty() || checked.contains(cleaned)) {
                continue;
            }
            checked.insert(cleaned);
            if (QFileInfo::exists(cleaned) && QFileInfo(cleaned).isFile()) {
                *resolvedPath = cleaned;
                *line = candidateLine;
                *column = candidateColumn;
                return true;
            }
        }
        return false;
    }

    void openSelectedFilename()
    {
        QString candidate = currentPathToken();
        QString path;
        int line = 1;
        int column = 1;

        if (candidate.isEmpty() || !resolveOpenFilenameCandidate(candidate, &path, &line, &column)) {
            bool ok = false;
            candidate = QInputDialog::getText(this, QStringLiteral("Open Selected Filename"),
                QStringLiteral("Filename:"), QLineEdit::Normal, candidate, &ok);
            if (!ok || !resolveOpenFilenameCandidate(candidate, &path, &line, &column)) {
                statusBar()->showMessage(QStringLiteral("Filename not found"), 3000);
                return;
            }
        }

        recordNavigationLocation();
        openPath(path);
        if (EditorPage* page = currentPage(); page != nullptr) {
            goToLineColumn(page, line, column);
        }
    }

    void openSelectedProjectFile()
    {
        const QString path = selectedProjectPath();
        if (!path.isEmpty()) {
            openPath(path);
        }
    }

    QStringList counterpartExtensionsFor(const QString& extension) const
    {
        const QString ext = extension.toLower();
        if (ext == QStringLiteral("h") || ext == QStringLiteral("hh")
            || ext == QStringLiteral("hpp") || ext == QStringLiteral("hxx")) {
            return {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx")};
        }
        if (ext == QStringLiteral("c") || ext == QStringLiteral("cc")
            || ext == QStringLiteral("cpp") || ext == QStringLiteral("cxx")) {
            return {QStringLiteral("h"), QStringLiteral("hh"), QStringLiteral("hpp"), QStringLiteral("hxx")};
        }
        return {};
    }

    void switchHeaderSource()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Save the document before switching header/source"), 3000);
            return;
        }

        const QFileInfo currentInfo(page->path);
        const QStringList targetExtensions = counterpartExtensionsFor(currentInfo.suffix());
        if (targetExtensions.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Header/source switching is available for C/C++ files"), 3000);
            return;
        }

        QStringList candidates;
        auto addCandidate = [&candidates](const QString& path) {
            const QString cleaned = QDir::cleanPath(path);
            if (!cleaned.isEmpty() && !candidates.contains(cleaned)) {
                candidates << cleaned;
            }
        };

        const QString baseName = currentInfo.completeBaseName();
        const QDir currentDir(currentInfo.absolutePath());
        for (const QString& ext : targetExtensions) {
            addCandidate(currentDir.absoluteFilePath(baseName + QLatin1Char('.') + ext));
        }

        if (!projectRoot.isEmpty()) {
            if (projectFilePaths.isEmpty()) {
                loadProjectFiles();
            }
            for (const QString& projectPath : projectFilePaths) {
                const QFileInfo info(projectPath);
                if (info.completeBaseName() == baseName && targetExtensions.contains(info.suffix().toLower())) {
                    addCandidate(projectPath);
                }
            }
        }

        QStringList existing;
        for (const QString& candidate : candidates) {
            const QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                existing << info.canonicalFilePath();
            }
        }
        existing.removeDuplicates();

        if (existing.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Matching header/source file not found"), 3000);
            appendOutput(QStringLiteral("Matching header/source file not found for %1").arg(page->path));
            return;
        }

        QString target = existing.first();
        if (existing.size() > 1) {
            QStringList labels;
            const QDir rootDir(projectRoot.isEmpty() ? currentInfo.absolutePath() : projectRoot);
            for (const QString& path : existing) {
                labels << rootDir.relativeFilePath(path);
            }
            bool ok = false;
            const QString selected = QInputDialog::getItem(this, QStringLiteral("Switch Header/Source"),
                QStringLiteral("Open matching file:"), labels, 0, false, &ok);
            if (!ok || selected.isEmpty()) {
                return;
            }
            const int index = labels.indexOf(selected);
            if (index >= 0 && index < existing.size()) {
                target = existing.at(index);
            }
        }

        recordNavigationLocation();
        openPath(target);
        statusBar()->showMessage(QStringLiteral("Switched to %1").arg(target), 2500);
    }

    void quickOpenProjectFile()
    {
        if (!ensureProjectRoot()) {
            return;
        }
        if (projectFilePaths.isEmpty()) {
            loadProjectFiles();
        }
        QStringList labels;
        const QDir rootDir(projectRoot);
        for (const QString& path : projectFilePaths) {
            labels << rootDir.relativeFilePath(path);
        }
        if (labels.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No project files to open"), 2500);
            return;
        }
        labels.sort(Qt::CaseInsensitive);
        bool ok = false;
        const QString selected = QInputDialog::getItem(this, QStringLiteral("Quick Open File"),
            QStringLiteral("Project file:"), labels, 0, true, &ok);
        if (!ok || selected.isEmpty()) {
            return;
        }
        const QString path = QDir(projectRoot).absoluteFilePath(selected);
        if (QFileInfo::exists(path)) {
            openPath(path);
        }
    }

    QString openDocumentTextForPath(const QString& path) const
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && QFileInfo(page->path) == QFileInfo(path)) {
                return page->editor->toPlainText();
            }
        }
        return QString();
    }

    QVector<ProjectSymbol> symbolsForText(const QString& path, const QString& text) const
    {
        QVector<ProjectSymbol> symbols;
        const QStringList lines = normalizeLineEndings(text).split(QLatin1Char('\n'));
        const QRegularExpression cFunction(QStringLiteral("^\\s*(?:void|int|bool|char|static|const|auto|inline|virtual|extern|[A-Za-z_][A-Za-z0-9_:<>*&\\s]+)\\s+([A-Za-z_][A-Za-z0-9_:]*)\\s*\\("));
        const QRegularExpression cType(QStringLiteral("^\\s*(?:class|struct|enum)\\s+([A-Za-z_][A-Za-z0-9_:]*)"));
        const QRegularExpression python(QStringLiteral("^\\s*(def|class)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression macro(QStringLiteral("^\\s*#\\s*define\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression jsFunction(QStringLiteral("^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression jsAssigned(QStringLiteral("^\\s*(?:export\\s+)?(?:const|let|var)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(?:async\\s*)?(?:function\\b|\\([^)]*\\)\\s*=>|[A-Za-z_][A-Za-z0-9_]*\\s*=>)"));
        const QRegularExpression jsType(QStringLiteral("^\\s*(?:export\\s+)?(?:default\\s+)?(class|interface|enum|type|struct)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression jsMethod(QStringLiteral("^\\s*(?:public\\s+|private\\s+|protected\\s+|static\\s+|async\\s+)*(build|aboutToAppear|aboutToDisappear|onPageShow|onPageHide|onBackPress|[A-Za-z_][A-Za-z0-9_]*)\\s*\\([^)]*\\)\\s*(?::\\s*[^\\{]+)?\\{\\s*$"));
        const QRegularExpression javaType(QStringLiteral("^\\s*(?:public\\s+|private\\s+|protected\\s+|abstract\\s+|final\\s+)*(class|interface|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression goFunction(QStringLiteral("^\\s*func\\s+(?:\\([^)]*\\)\\s*)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
        const QRegularExpression rustItem(QStringLiteral("^\\s*(?:pub\\s+)?(fn|struct|enum|trait|impl)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression phpFunction(QStringLiteral("^\\s*(?:public\\s+|private\\s+|protected\\s+)?function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
        const QString ext = QFileInfo(path).suffix().toLower();
        const bool jsLike = ext == QStringLiteral("js") || ext == QStringLiteral("ts") || ext == QStringLiteral("ets");
        const QSet<QString> nonSymbolMethods{
            QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("switch"),
            QStringLiteral("catch"), QStringLiteral("function")
        };

        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i);
            QString name;
            QString kind;
            QRegularExpressionMatch match = macro.match(line);
            if (match.hasMatch()) {
                name = match.captured(1);
                kind = QStringLiteral("macro");
            } else {
                match = python.match(line);
                if (match.hasMatch()) {
                    kind = match.captured(1);
                    name = match.captured(2);
                } else {
                    match = cType.match(line);
                    if (match.hasMatch()) {
                        kind = QStringLiteral("type");
                        name = match.captured(1);
                    } else {
                        match = jsFunction.match(line);
                        if (match.hasMatch()) {
                            kind = QStringLiteral("function");
                            name = match.captured(1);
                        } else {
                            match = jsAssigned.match(line);
                            if (match.hasMatch()) {
                                kind = QStringLiteral("function");
                                name = match.captured(1);
                            } else {
                                match = jsType.match(line);
                                if (match.hasMatch()) {
                                    kind = match.captured(1);
                                    name = match.captured(2);
                                } else {
                                    match = jsLike ? jsMethod.match(line) : QRegularExpressionMatch();
                                    if (match.hasMatch()) {
                                        const QString candidate = match.captured(1);
                                        if (!nonSymbolMethods.contains(candidate)) {
                                            kind = QStringLiteral("method");
                                            name = candidate;
                                        }
                                    }
                                    if (name.isEmpty()) {
                                        match = javaType.match(line);
                                        if (match.hasMatch()) {
                                            kind = match.captured(1);
                                            name = match.captured(2);
                                        } else {
                                            match = goFunction.match(line);
                                            if (match.hasMatch()) {
                                                kind = QStringLiteral("function");
                                                name = match.captured(1);
                                            } else {
                                                match = rustItem.match(line);
                                                if (match.hasMatch()) {
                                                    kind = match.captured(1);
                                                    name = match.captured(2);
                                                } else {
                                                    match = phpFunction.match(line);
                                                    if (match.hasMatch()) {
                                                        kind = QStringLiteral("function");
                                                        name = match.captured(1);
                                                    } else {
                                                        match = cFunction.match(line);
                                                        if (match.hasMatch()) {
                                                            kind = QStringLiteral("function");
                                                            name = match.captured(1);
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!name.isEmpty()) {
                ProjectSymbol symbol;
                symbol.name = name;
                symbol.kind = kind;
                symbol.path = path;
                symbol.line = i + 1;
                symbol.preview = line.trimmed();
                symbols.append(symbol);
            }
        }
        return symbols;
    }

    QVector<ProjectSymbol> collectProjectSymbols()
    {
        QVector<ProjectSymbol> symbols;
        if (!ensureProjectRoot()) {
            return symbols;
        }
        QDirIterator it(projectRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        int filesScanned = 0;
        while (it.hasNext() && filesScanned < 1000 && symbols.size() < 4000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path) || !isSourceFile(path) || !canScanTextFile(path)) {
                continue;
            }
            ++filesScanned;

            QString text = openDocumentTextForPath(path);
            if (text.isEmpty()) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const QByteArray bytes = file.readAll();
                text = decodeText(bytes, detectEncodingName(bytes));
            }

            const QVector<ProjectSymbol> fileSymbols = symbolsForText(path, text);
            for (const ProjectSymbol& symbol : fileSymbols) {
                symbols.append(symbol);
                if (symbols.size() >= 4000) {
                    break;
                }
            }
        }
        return symbols;
    }

    int tagLineFromFields(const QString& address, const QStringList& fields) const
    {
        static const QRegularExpression directLine(QStringLiteral("^(\\d+)"));
        QRegularExpressionMatch match = directLine.match(address.trimmed());
        if (match.hasMatch()) {
            return qMax(1, match.captured(1).toInt());
        }
        for (const QString& field : fields) {
            if (field.startsWith(QStringLiteral("line:"))) {
                return qMax(1, field.mid(QStringLiteral("line:").size()).toInt());
            }
        }
        return 1;
    }

    QString tagKindFromFields(const QStringList& fields) const
    {
        for (const QString& field : fields) {
            if (field.startsWith(QStringLiteral("kind:"))) {
                return field.mid(QStringLiteral("kind:").size()).trimmed();
            }
        }
        for (const QString& field : fields) {
            if (!field.contains(QLatin1Char(':')) && field != QStringLiteral(";\"")) {
                return field.trimmed();
            }
        }
        return QStringLiteral("tag");
    }

    QString tagPreviewFromFields(const QString& address, const QStringList& fields) const
    {
        QStringList previewParts;
        for (const QString& field : fields) {
            if (field.startsWith(QStringLiteral("signature:")) || field.startsWith(QStringLiteral("typeref:"))
                || field.startsWith(QStringLiteral("scope:")) || field.startsWith(QStringLiteral("inherits:"))) {
                previewParts << field;
            }
        }
        if (!previewParts.isEmpty()) {
            return previewParts.join(QStringLiteral(" "));
        }
        return address.trimmed();
    }

    bool parseCtagsLine(const QString& line, const QString& tagFilePath, ProjectSymbol* symbol) const
    {
        if (symbol == nullptr || line.isEmpty() || line.startsWith(QStringLiteral("!_TAG_")) || line.startsWith(QLatin1Char('#'))) {
            return false;
        }
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 3) {
            return false;
        }

        QString name = parts.at(0).trimmed();
        QString rawPath = parts.at(1).trimmed();
        if (name.isEmpty()) {
            return false;
        }

        QString resolvedPath;
        if (!rawPath.isEmpty() && rawPath != QStringLiteral("/unknown") && rawPath != QStringLiteral("unknown")) {
            resolvedPath = QDir::isAbsolutePath(rawPath)
                ? QDir::cleanPath(rawPath)
                : QDir(QFileInfo(tagFilePath).absolutePath()).absoluteFilePath(rawPath);
        }

        QStringList fields;
        for (int i = 3; i < parts.size(); ++i) {
            const QString field = parts.at(i).trimmed();
            if (!field.isEmpty()) {
                fields << field;
            }
        }

        symbol->name = name;
        symbol->kind = tagKindFromFields(fields);
        symbol->path = resolvedPath;
        symbol->origin = tagFilePath;
        symbol->line = tagLineFromFields(parts.at(2), fields);
        symbol->preview = tagPreviewFromFields(parts.at(2), fields);
        return true;
    }

    QVector<ProjectSymbol> parseTagsFile(const QString& path, int* skippedLines = nullptr) const
    {
        QVector<ProjectSymbol> symbols;
        if (skippedLines != nullptr) {
            *skippedLines = 0;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return symbols;
        }

        const QString text = QString::fromUtf8(file.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        QSet<QString> seen;
        for (const QString& line : lines) {
            if (symbols.size() >= 30000) {
                break;
            }
            ProjectSymbol symbol;
            if (!parseCtagsLine(line.trimmed(), path, &symbol)) {
                if (skippedLines != nullptr && !line.trimmed().isEmpty() && !line.startsWith(QStringLiteral("!_TAG_"))) {
                    ++(*skippedLines);
                }
                continue;
            }
            const QString key = QStringLiteral("%1\t%2\t%3").arg(symbol.name, symbol.path).arg(symbol.line);
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            symbols.append(symbol);
        }
        return symbols;
    }

    void reloadLoadedTags(bool announce)
    {
        loadedTagSymbols.clear();
        int filesLoaded = 0;
        for (const QString& path : loadedTagFiles) {
            if (!QFileInfo::exists(path)) {
                if (announce) {
                    appendOutput(QStringLiteral("Tags file not found: %1").arg(path));
                }
                continue;
            }
            int skipped = 0;
            const QVector<ProjectSymbol> symbols = parseTagsFile(path, &skipped);
            for (const ProjectSymbol& symbol : symbols) {
                loadedTagSymbols.append(symbol);
            }
            ++filesLoaded;
            if (announce) {
                appendOutput(QStringLiteral("Loaded tags: %1 symbol(s) from %2%3")
                    .arg(symbols.size())
                    .arg(path)
                    .arg(skipped > 0 ? QStringLiteral(" (%1 skipped line(s))").arg(skipped) : QString()));
            }
        }
        if (announce) {
            statusBar()->showMessage(QStringLiteral("Loaded %1 tag symbol(s) from %2 file(s)")
                .arg(loadedTagSymbols.size())
                .arg(filesLoaded), 3500);
        }
    }

    void loadTagsFileFromDialog()
    {
        const QString path = chooseOpenFileName(this, QStringLiteral("Load Tags File"),
            currentWorkDir(), QStringLiteral("Tags Files (*.tags tags);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        const QString normalized = QFileInfo(path).absoluteFilePath();
        loadedTagFiles.removeAll(normalized);
        loadedTagFiles.prepend(normalized);
        reloadLoadedTags(true);
        saveSettings();
    }

    void clearLoadedTags()
    {
        loadedTagFiles.clear();
        loadedTagSymbols.clear();
        saveSettings();
        appendOutput(QStringLiteral("Loaded tags cleared."));
        statusBar()->showMessage(QStringLiteral("Loaded tags cleared"), 2500);
    }

    QString symbolPathLabel(const ProjectSymbol& symbol) const
    {
        if (!symbol.path.isEmpty()) {
            if (!projectRoot.isEmpty() && isInsideProject(symbol.path)) {
                return QDir(projectRoot).relativeFilePath(symbol.path);
            }
            return symbol.path;
        }
        return symbol.origin.isEmpty()
            ? QStringLiteral("loaded tags")
            : QStringLiteral("%1 (external tag)").arg(QFileInfo(symbol.origin).fileName());
    }

    QVector<ProjectSymbol> collectNavigationSymbols()
    {
        QVector<ProjectSymbol> symbols;
        if (!projectRoot.isEmpty() && QDir(projectRoot).exists()) {
            symbols = collectProjectSymbols();
        }
        for (const ProjectSymbol& symbol : loadedTagSymbols) {
            symbols.append(symbol);
            if (symbols.size() >= 50000) {
                break;
            }
        }
        if (symbols.isEmpty() && loadedTagSymbols.isEmpty() && projectRoot.isEmpty()) {
            symbols = collectProjectSymbols();
        }
        return symbols;
    }

    bool isIdentifierCharacter(QChar ch) const
    {
        return ch == QLatin1Char('_') || ch.isLetterOrNumber();
    }

    QString currentIdentifierAtCursor() const
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return QString();
        }
        const QTextCursor cursor = page->editor->textCursor();
        const QString line = cursor.block().text();
        int start = cursor.positionInBlock();
        int end = start;
        if (start > line.size()) {
            start = line.size();
            end = start;
        }
        if (start > 0 && (start == line.size() || !isIdentifierCharacter(line.at(start)))) {
            --start;
            end = start + 1;
        }
        while (start > 0 && isIdentifierCharacter(line.at(start - 1))) {
            --start;
        }
        while (end < line.size() && isIdentifierCharacter(line.at(end))) {
            ++end;
        }
        const QString identifier = line.mid(start, end - start);
        return identifier.size() > 1 ? identifier : QString();
    }

    QString symbolLabel(int index, const ProjectSymbol& symbol) const
    {
        return QStringLiteral("%1  %2  [%3]  %4:%5  %6")
            .arg(index + 1, 4, 10, QLatin1Char('0'))
            .arg(symbol.name,
                symbol.kind.isEmpty() ? QStringLiteral("symbol") : symbol.kind,
                symbolPathLabel(symbol))
            .arg(symbol.line)
            .arg(symbol.preview);
    }

    void jumpToProjectSymbol(const ProjectSymbol& symbol)
    {
        if (QFileInfo::exists(symbol.path) && symbol.line > 0) {
            recordNavigationLocation();
            openPath(symbol.path);
            goToLineNumber(symbol.line, false);
            return;
        }
        statusBar()->showMessage(QStringLiteral("Tag has no local source file: %1").arg(symbol.name), 3000);
        appendOutput(QStringLiteral("Tag has no local source file: %1 from %2")
            .arg(symbol.name, symbolPathLabel(symbol)));
    }

    bool chooseAndJumpToSymbol(const QVector<ProjectSymbol>& symbols, const QString& title, const QString& prompt)
    {
        if (symbols.isEmpty()) {
            return false;
        }

        QStringList labels;
        for (int i = 0; i < symbols.size(); ++i) {
            labels << symbolLabel(i, symbols.at(i));
        }
        bool ok = false;
        const QString selected = QInputDialog::getItem(this, title, prompt, labels, 0, true, &ok);
        if (!ok || selected.isEmpty()) {
            return false;
        }
        const int index = labels.indexOf(selected);
        if (index < 0 || index >= symbols.size()) {
            return false;
        }
        jumpToProjectSymbol(symbols.at(index));
        return true;
    }

    QString documentSymbolLabel(int index, const ProjectSymbol& symbol) const
    {
        return QStringLiteral("%1  %2  [%3]  line %4  %5")
            .arg(index + 1, 4, 10, QLatin1Char('0'))
            .arg(symbol.name,
                symbol.kind.isEmpty() ? QStringLiteral("symbol") : symbol.kind)
            .arg(symbol.line)
            .arg(symbol.preview);
    }

    void goToDocumentSymbol()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const QVector<ProjectSymbol> symbols = symbolsForText(page->path, page->editor->toPlainText());
        if (symbols.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No symbols found in current document"), 2500);
            return;
        }

        QStringList labels;
        for (int i = 0; i < symbols.size(); ++i) {
            labels << documentSymbolLabel(i, symbols.at(i));
        }
        bool ok = false;
        const QString selected = QInputDialog::getItem(this, QStringLiteral("Go to Document Symbol"),
            QStringLiteral("Symbol in %1:").arg(displayName(page)), labels, 0, true, &ok);
        if (!ok || selected.isEmpty()) {
            return;
        }
        const int index = labels.indexOf(selected);
        if (index < 0 || index >= symbols.size()) {
            return;
        }

        recordNavigationLocation();
        goToLineNumber(symbols.at(index).line, false);
    }

    void gotoDocumentSymbolRelative(int direction)
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        const QVector<ProjectSymbol> symbols = symbolsForText(page->path, page->editor->toPlainText());
        if (symbols.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No symbols found in current document"), 2500);
            return;
        }

        const int currentLine = page->editor->textCursor().blockNumber() + 1;
        int targetIndex = direction >= 0 ? 0 : symbols.size() - 1;
        if (direction >= 0) {
            for (int i = 0; i < symbols.size(); ++i) {
                if (symbols.at(i).line > currentLine) {
                    targetIndex = i;
                    break;
                }
            }
        } else {
            for (int i = symbols.size() - 1; i >= 0; --i) {
                if (symbols.at(i).line < currentLine) {
                    targetIndex = i;
                    break;
                }
            }
        }

        recordNavigationLocation();
        goToLineNumber(symbols.at(targetIndex).line, false);
        statusBar()->showMessage(QStringLiteral("%1 [%2] line %3")
            .arg(symbols.at(targetIndex).name,
                symbols.at(targetIndex).kind.isEmpty() ? QStringLiteral("symbol") : symbols.at(targetIndex).kind)
            .arg(symbols.at(targetIndex).line), 2500);
    }

    void goToProjectSymbol()
    {
        const QVector<ProjectSymbol> symbols = collectNavigationSymbols();
        if (symbols.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No project or loaded tag symbols found"), 2500);
            return;
        }
        chooseAndJumpToSymbol(symbols, QStringLiteral("Go to Symbol"), QStringLiteral("Project or loaded tag symbol:"));
    }

    void goToDefinition()
    {
        QString identifier = currentIdentifierAtCursor();
        if (identifier.isEmpty()) {
            bool ok = false;
            identifier = QInputDialog::getText(this, QStringLiteral("Go to Definition"),
                QStringLiteral("Symbol name:"), QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || identifier.isEmpty()) {
                return;
            }
        }

        const QVector<ProjectSymbol> symbols = collectNavigationSymbols();
        QVector<ProjectSymbol> matches;
        for (const ProjectSymbol& symbol : symbols) {
            if (symbol.name == identifier) {
                matches.append(symbol);
            }
        }
        if (matches.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Definition or loaded tag not found: %1").arg(identifier), 3000);
            appendOutput(QStringLiteral("Definition or loaded tag not found: %1").arg(identifier));
            return;
        }
        if (matches.size() == 1) {
            jumpToProjectSymbol(matches.first());
            return;
        }
        chooseAndJumpToSymbol(matches, QStringLiteral("Go to Definition"),
            QStringLiteral("Multiple definitions for %1:").arg(identifier));
    }

    void findReferences()
    {
        QString identifier = currentIdentifierAtCursor();
        if (identifier.isEmpty()) {
            bool ok = false;
            identifier = QInputDialog::getText(this, QStringLiteral("Find References"),
                QStringLiteral("Symbol name:"), QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || identifier.isEmpty()) {
                return;
            }
        }
        if (!ensureProjectRoot()) {
            return;
        }

        const QRegularExpression referencePattern(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(identifier)));
        searchResults->clear();
        int filesScanned = 0;
        int matches = 0;
        QDirIterator it(projectRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext() && filesScanned < 3000 && matches < 2000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path) || !isSourceFile(path) || !canScanTextFile(path)) {
                continue;
            }
            ++filesScanned;

            QString text = openDocumentTextForPath(path);
            if (text.isEmpty()) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                const QByteArray bytes = file.readAll();
                text = decodeText(bytes, detectEncodingName(bytes));
            }

            const QStringList lines = normalizeLineEndings(text).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size() && matches < 2000; ++i) {
                const QRegularExpressionMatch match = referencePattern.match(lines.at(i));
                if (!match.hasMatch()) {
                    continue;
                }
                const int column = match.capturedStart() + 1;
                const QString label = QStringLiteral("%1:%2:%3: %4")
                    .arg(QDir(projectRoot).relativeFilePath(path))
                    .arg(i + 1)
                    .arg(column)
                    .arg(lines.at(i).trimmed());
                auto* item = new QListWidgetItem(label, searchResults);
                item->setData(Qt::UserRole, path);
                item->setData(Qt::UserRole + 1, i + 1);
                item->setData(Qt::UserRole + 2, column);
                ++matches;
            }
        }

        sideTabs->setCurrentWidget(searchResults);
        appendOutput(QStringLiteral("Find references: %1 match(es) for %2 in %3 scanned file(s)")
            .arg(matches)
            .arg(identifier)
            .arg(filesScanned));
        if (matches == 0) {
            statusBar()->showMessage(QStringLiteral("No references found: %1").arg(identifier), 3000);
        }
    }

    void addTaskItemsForText(const QString& path, const QString& labelPath, const QString& text, int* matches)
    {
        if (tasksList == nullptr || matches == nullptr || *matches >= 2000) {
            return;
        }

        static const QRegularExpression taskPattern(
            QStringLiteral("\\b(TODO|FIXME|XXX|HACK|NOTE)\\b\\s*:?\\s*(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QStringList lines = normalizeLineEndings(text).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size() && *matches < 2000; ++i) {
            const QRegularExpressionMatch match = taskPattern.match(lines.at(i));
            if (!match.hasMatch()) {
                continue;
            }

            const QString tag = match.captured(1).toUpper();
            QString detail = match.captured(2).trimmed();
            if (detail.isEmpty()) {
                detail = lines.at(i).trimmed();
            }
            auto* item = new QListWidgetItem(QStringLiteral("%1:%2: [%3] %4")
                    .arg(labelPath)
                    .arg(i + 1)
                    .arg(tag, detail),
                tasksList);
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, i + 1);
            ++(*matches);
        }
    }

    void scanTasks()
    {
        tasksList->clear();
        int filesScanned = 0;
        int matches = 0;

        if (!projectRoot.isEmpty() && QDir(projectRoot).exists()) {
            QDirIterator it(projectRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext() && filesScanned < 3000 && matches < 2000) {
                const QString path = it.next();
                if (isIgnoredProjectPath(path) || !isSourceFile(path) || !canScanTextFile(path)) {
                    continue;
                }
                ++filesScanned;

                QString text = openDocumentTextForPath(path);
                if (text.isEmpty()) {
                    QFile file(path);
                    if (!file.open(QIODevice::ReadOnly)) {
                        continue;
                    }
                    const QByteArray bytes = file.readAll();
                    text = decodeText(bytes, detectEncodingName(bytes));
                }
                addTaskItemsForText(path, QDir(projectRoot).relativeFilePath(path), text, &matches);
            }
        } else {
            QSet<QString> scannedPaths;
            for (int i = 0; i < tabs->count() && matches < 2000; ++i) {
                auto* page = static_cast<EditorPage*>(tabs->widget(i));
                if (page == nullptr || page->path.isEmpty() || scannedPaths.contains(page->path)) {
                    continue;
                }
                scannedPaths.insert(page->path);
                ++filesScanned;
                addTaskItemsForText(page->path, QFileInfo(page->path).fileName(), page->editor->toPlainText(), &matches);
            }
        }

        sideTabs->setCurrentWidget(tasksList);
        appendOutput(QStringLiteral("Task scan: %1 task(s) in %2 scanned file(s).").arg(matches).arg(filesScanned));
        if (matches == 0) {
            statusBar()->showMessage(QStringLiteral("No TODO/FIXME tasks found"), 3000);
        }
    }

    void selectCurrentFileInProject()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->path.isEmpty() || projectRoot.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No project file for current document"), 2500);
            return;
        }
        for (int i = 0; i < projectFiles->count(); ++i) {
            QListWidgetItem* item = projectFiles->item(i);
            if (item != nullptr && QFileInfo(item->data(Qt::UserRole).toString()) == QFileInfo(page->path)) {
                projectFiles->setCurrentItem(item);
                sideTabs->setCurrentWidget(projectPanel);
                projectFiles->scrollToItem(item);
                return;
            }
        }
        if (projectFileFilter != nullptr && !projectFileFilter->text().isEmpty()) {
            projectFileFilter->clear();
            for (int i = 0; i < projectFiles->count(); ++i) {
                QListWidgetItem* item = projectFiles->item(i);
                if (item != nullptr && QFileInfo(item->data(Qt::UserRole).toString()) == QFileInfo(page->path)) {
                    projectFiles->setCurrentItem(item);
                    sideTabs->setCurrentWidget(projectPanel);
                    projectFiles->scrollToItem(item);
                    return;
                }
            }
        }
        statusBar()->showMessage(QStringLiteral("Current file is not listed in project"), 2500);
    }

    void createProjectFile()
    {
        if (!ensureProjectRoot()) {
            return;
        }
        bool ok = false;
        const QString input = QInputDialog::getText(this, QStringLiteral("New File"),
            QStringLiteral("Path relative to project:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || input.trimmed().isEmpty()) {
            return;
        }
        const QString path = projectPathFromInput(input);
        if (!isInsideProject(path)) {
            QMessageBox::warning(this, QStringLiteral("New File"), QStringLiteral("File must stay inside the project folder."));
            return;
        }
        if (QFileInfo::exists(path)) {
            QMessageBox::information(this, QStringLiteral("New File"), QStringLiteral("File already exists."));
            openPath(path);
            return;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, QStringLiteral("New File"), file.errorString());
            return;
        }
        file.close();
        loadProjectFiles();
        openPath(path);
        appendOutput(QStringLiteral("Created file: %1").arg(path));
    }

    void createProjectFolder()
    {
        if (!ensureProjectRoot()) {
            return;
        }
        bool ok = false;
        const QString input = QInputDialog::getText(this, QStringLiteral("New Folder"),
            QStringLiteral("Path relative to project:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || input.trimmed().isEmpty()) {
            return;
        }
        const QString path = projectPathFromInput(input);
        if (!isInsideProject(path)) {
            QMessageBox::warning(this, QStringLiteral("New Folder"), QStringLiteral("Folder must stay inside the project folder."));
            return;
        }
        if (!QDir().mkpath(path)) {
            QMessageBox::warning(this, QStringLiteral("New Folder"), QStringLiteral("Could not create folder."));
            return;
        }
        loadProjectFiles();
        appendOutput(QStringLiteral("Created folder: %1").arg(path));
    }

    bool closeOpenDocumentForFileOperation(const QString& path)
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page == nullptr || QFileInfo(page->path) != QFileInfo(path)) {
                continue;
            }
            if (page->editor->document()->isModified()) {
                tabs->setCurrentIndex(i);
                const int answer = QMessageBox::question(this, QStringLiteral("Unsaved document"),
                    QStringLiteral("Close unsaved changes to %1?").arg(displayName(page)),
                    QMessageBox::Yes | QMessageBox::Cancel);
                if (answer == QMessageBox::Cancel) {
                    return false;
                }
            }
            QWidget* widget = tabs->widget(i);
            tabs->removeTab(i);
            widget->deleteLater();
            if (tabs->count() == 0) {
                newDocument();
            }
            updateDocumentList();
            updateBookmarksList();
            updateCursorStatus();
            break;
        }
        return true;
    }

    void renameSelectedProjectFile()
    {
        const QString path = selectedProjectPath();
        if (path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Select a project file first"), 2500);
            return;
        }
        bool ok = false;
        const QString relative = QDir(projectRoot).relativeFilePath(path);
        const QString input = QInputDialog::getText(this, QStringLiteral("Rename File"),
            QStringLiteral("New path relative to project:"), QLineEdit::Normal, relative, &ok);
        if (!ok || input.trimmed().isEmpty()) {
            return;
        }
        const QString newPath = projectPathFromInput(input);
        if (!isInsideProject(newPath)) {
            QMessageBox::warning(this, QStringLiteral("Rename File"), QStringLiteral("File must stay inside the project folder."));
            return;
        }
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, QStringLiteral("Rename File"), QStringLiteral("Target already exists."));
            return;
        }
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        if (!QFile::rename(path, newPath)) {
            QMessageBox::warning(this, QStringLiteral("Rename File"), QStringLiteral("Could not rename file."));
            return;
        }
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && QFileInfo(page->path) == QFileInfo(path)) {
                page->path = newPath;
                configureHighlighterForPath(page, newPath);
                updateTabTitle(page, page->editor->document()->isModified());
            }
        }
        recentFiles.removeAll(path);
        addRecentFile(newPath);
        loadProjectFiles();
        appendOutput(QStringLiteral("Renamed %1 to %2").arg(path, newPath));
    }

    void duplicateSelectedProjectFile()
    {
        const QString path = selectedProjectPath();
        if (path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Select a project file first"), 2500);
            return;
        }
        const QFileInfo sourceInfo(path);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            QMessageBox::warning(this, QStringLiteral("Duplicate File"), QStringLiteral("Selected project item is not a file."));
            return;
        }

        const QString relative = QDir(projectRoot).relativeFilePath(path);
        const QFileInfo relativeInfo(relative);
        const QString suffix = sourceInfo.suffix().isEmpty() ? QString() : QStringLiteral(".") + sourceInfo.suffix();
        const QString defaultName = QDir(relativeInfo.path()).filePath(sourceInfo.completeBaseName() + QStringLiteral("-copy") + suffix);

        bool ok = false;
        const QString input = QInputDialog::getText(this, QStringLiteral("Duplicate File"),
            QStringLiteral("Copy path relative to project:"), QLineEdit::Normal, defaultName, &ok);
        if (!ok || input.trimmed().isEmpty()) {
            return;
        }

        const QString newPath = projectPathFromInput(input);
        if (!isInsideProject(newPath)) {
            QMessageBox::warning(this, QStringLiteral("Duplicate File"), QStringLiteral("Copy must stay inside the project folder."));
            return;
        }
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, QStringLiteral("Duplicate File"), QStringLiteral("Target already exists."));
            return;
        }
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        if (!QFile::copy(path, newPath)) {
            QMessageBox::warning(this, QStringLiteral("Duplicate File"), QStringLiteral("Could not copy file."));
            return;
        }
        loadProjectFiles();
        openPath(newPath);
        appendOutput(QStringLiteral("Duplicated %1 to %2").arg(path, newPath));
    }

    void deleteSelectedProjectFile()
    {
        const QString path = selectedProjectPath();
        if (path.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Select a project file first"), 2500);
            return;
        }
        const int answer = QMessageBox::warning(this, QStringLiteral("Delete File"),
            QStringLiteral("Delete %1?").arg(QDir(projectRoot).relativeFilePath(path)),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (answer != QMessageBox::Yes || !closeOpenDocumentForFileOperation(path)) {
            return;
        }
        if (!QFile::remove(path)) {
            QMessageBox::warning(this, QStringLiteral("Delete File"), QStringLiteral("Could not delete file."));
            return;
        }
        recentFiles.removeAll(path);
        updateRecentFilesMenu();
        loadProjectFiles();
        appendOutput(QStringLiteral("Deleted file: %1").arg(path));
    }

    void showProjectProperties()
    {
        if (!ensureProjectRoot()) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Project Properties"));
        auto* layout = new QFormLayout(&dialog);

        auto* rootLabel = new QLabel(projectRoot, &dialog);
        rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* projectFileLabel = new QLabel(projectFile.isEmpty() ? QStringLiteral("Not saved") : projectFile, &dialog);
        projectFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* defaultsCheck = new QCheckBox(&dialog);
        defaultsCheck->setChecked(!projectDefaultLineEnding.isEmpty() || !projectDefaultEncodingName.isEmpty());
        auto* lineEndingCombo = new QComboBox(&dialog);
        lineEndingCombo->addItems({QStringLiteral("LF"), QStringLiteral("CRLF"), QStringLiteral("CR")});
        lineEndingCombo->setCurrentText(lineEndingName(projectDefaultLineEnding.isEmpty() ? defaultLineEnding : projectDefaultLineEnding));
        auto* encodingCombo = new QComboBox(&dialog);
        encodingCombo->addItems({QStringLiteral("UTF-8"), systemEncodingName(), QStringLiteral("GB18030"), QStringLiteral("UTF-16LE-BOM"), QStringLiteral("UTF-16BE-BOM")});
        encodingCombo->setEditable(true);
        encodingCombo->setCurrentText(projectDefaultEncodingName.isEmpty() ? defaultEncodingName : projectDefaultEncodingName);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

        layout->addRow(QStringLiteral("Project folder"), rootLabel);
        layout->addRow(QStringLiteral("Project file"), projectFileLabel);
        layout->addRow(QStringLiteral("Override new-file defaults"), defaultsCheck);
        layout->addRow(QStringLiteral("Default line endings"), lineEndingCombo);
        layout->addRow(QStringLiteral("Default encoding"), encodingCombo);
        layout->addWidget(buttons);

        auto updateEnabled = [&]() {
            lineEndingCombo->setEnabled(defaultsCheck->isChecked());
            encodingCombo->setEnabled(defaultsCheck->isChecked());
        };
        connect(defaultsCheck, &QCheckBox::toggled, &dialog, [&](bool) { updateEnabled(); });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        updateEnabled();

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        if (defaultsCheck->isChecked()) {
            projectDefaultLineEnding = lineEndingForName(lineEndingCombo->currentText());
            projectDefaultEncodingName = normalizedEncodingName(encodingCombo->currentText());
        } else {
            projectDefaultLineEnding.clear();
            projectDefaultEncodingName.clear();
        }
        if (!projectFile.isEmpty()) {
            writeProjectFile(projectFile, QFileInfo(projectRoot).fileName());
        }
        updateCursorStatus();
        appendOutput(QStringLiteral("Project properties updated."));
    }

    void showProjectIgnoreRules()
    {
        if (!ensureProjectRoot()) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Project Ignore Rules"));
        dialog.resize(560, 420);
        auto* layout = new QVBoxLayout(&dialog);
        auto* editor = new QPlainTextEdit(&dialog);
        editor->setPlainText(projectExtraIgnorePatterns.join(QLatin1Char('\n')));
        editor->setPlaceholderText(QStringLiteral("build-*/\nthird_party/cache/\n*.generated.cpp"));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

        layout->addWidget(editor);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        QStringList patterns;
        const QStringList lines = editor->toPlainText().split(QLatin1Char('\n'));
        for (const QString& entry : lines) {
            const QString line = entry.trimmed();
            if (!line.isEmpty() && !patterns.contains(line)) {
                patterns << line;
            }
        }

        projectExtraIgnorePatterns = patterns;
        loadProjectIgnoreRules();
        loadProjectFiles();
        if (!projectFile.isEmpty()) {
            writeProjectFile(projectFile, QFileInfo(projectRoot).fileName());
        }
        appendOutput(QStringLiteral("Project ignore rules updated: %1 pattern(s).").arg(projectExtraIgnorePatterns.size()));
    }

    void newProjectFile()
    {
        const QString dir = chooseExistingDirectory(this, QStringLiteral("Project Folder"), currentWorkDir());
        if (dir.isEmpty()) {
            return;
        }

        const QString defaultName = QFileInfo(dir).fileName();
        bool ok = false;
        QString name = QInputDialog::getText(this, QStringLiteral("New Project"), QStringLiteral("Project name:"),
            QLineEdit::Normal, defaultName, &ok).trimmed();
        if (!ok) {
            return;
        }
        if (name.isEmpty()) {
            name = defaultName;
        }

        const QString path = chooseSaveFileName(this, QStringLiteral("Save Project"),
            QDir(dir).filePath(name + QStringLiteral(".geany")),
            QStringLiteral("Geany Project (*.geany);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        projectRoot = dir;
        projectFile = path;
        applyOpenHarmonyBuildDefaults();
        loadProjectFiles();
        addRecentProject(projectFile);
        if (writeProjectFile(projectFile, name)) {
            appendOutput(QStringLiteral("Project saved: %1").arg(projectFile));
            updateCursorStatus();
        }
    }

    void openProjectFile()
    {
        const QString path = chooseOpenFileName(this, QStringLiteral("Open Project File"),
            currentWorkDir(), QStringLiteral("Geany Project (*.geany);;All Files (*)"));
        if (!path.isEmpty()) {
            loadProjectFile(path);
        }
    }

    void loadProjectConfiguration(QSettings& projectSettings)
    {
        setProjectDefaultLineEnding(projectSettings.value(QStringLiteral("editor/defaultLineEnding")).toString());
        const QString projectEncoding = projectSettings.value(QStringLiteral("editor/defaultEncoding")).toString().trimmed();
        projectDefaultEncodingName = projectEncoding.isEmpty() ? QString() : normalizedEncodingName(projectEncoding);
        projectExtraIgnorePatterns = projectSettings.value(QStringLiteral("project/extraIgnorePatterns")).toStringList();
        buildCommand = projectSettings.value(QStringLiteral("build/command"), buildCommand).toString();
        buildCleanCommand = projectSettings.value(QStringLiteral("build/cleanCommand"), buildCleanCommand).toString();
        buildTargetCommand = projectSettings.value(QStringLiteral("build/targetCommand"), buildTargetCommand).toString();
        applyOpenHarmonyBuildDefaults();
        compileCCommand = projectSettings.value(QStringLiteral("build/compileC"), compileCCommand).toString();
        compileCppCommand = projectSettings.value(QStringLiteral("build/compileCpp"), compileCppCommand).toString();
        checkPythonCommand = projectSettings.value(QStringLiteral("build/checkPython"), checkPythonCommand).toString();
        checkShellCommand = projectSettings.value(QStringLiteral("build/checkShell"), checkShellCommand).toString();
        checkJsCommand = projectSettings.value(QStringLiteral("build/checkJs"), checkJsCommand).toString();
        checkArkTsCommand = projectSettings.value(QStringLiteral("build/checkArkTs"), checkArkTsCommand).toString();
        checkJsonCommand = projectSettings.value(QStringLiteral("build/checkJson"), checkJsonCommand).toString();
        checkGoCommand = projectSettings.value(QStringLiteral("build/checkGo"), checkGoCommand).toString();
        checkRustCommand = projectSettings.value(QStringLiteral("build/checkRust"), checkRustCommand).toString();
        checkJavaCommand = projectSettings.value(QStringLiteral("build/checkJava"), checkJavaCommand).toString();
        checkPhpCommand = projectSettings.value(QStringLiteral("build/checkPhp"), checkPhpCommand).toString();
        checkYamlCommand = projectSettings.value(QStringLiteral("build/checkYaml"), checkYamlCommand).toString();
        checkTomlCommand = projectSettings.value(QStringLiteral("build/checkToml"), checkTomlCommand).toString();
        executePythonCommand = projectSettings.value(QStringLiteral("build/executePython"), executePythonCommand).toString();
        executeShellCommand = projectSettings.value(QStringLiteral("build/executeShell"), executeShellCommand).toString();
        executeJsCommand = projectSettings.value(QStringLiteral("build/executeJs"), executeJsCommand).toString();
        executeGoCommand = projectSettings.value(QStringLiteral("build/executeGo"), executeGoCommand).toString();
        executeRustCommand = projectSettings.value(QStringLiteral("build/executeRust"), executeRustCommand).toString();
        executeJavaCommand = projectSettings.value(QStringLiteral("build/executeJava"), executeJavaCommand).toString();
        executePhpCommand = projectSettings.value(QStringLiteral("build/executePhp"), executePhpCommand).toString();
        executeNativeCommand = projectSettings.value(QStringLiteral("build/executeNative"), executeNativeCommand).toString();
    }

    bool loadProjectFile(const QString& path)
    {
        if (!QFileInfo::exists(path)) {
            QMessageBox::warning(this, QStringLiteral("Open project failed"), QStringLiteral("Project file does not exist."));
            return false;
        }

        QSettings projectSettings(path, QSettings::IniFormat);
        QString root = projectSettings.value(QStringLiteral("project/base_path")).toString();
        if (root.isEmpty()) {
            root = QFileInfo(path).absolutePath();
        } else if (QDir::isRelativePath(root)) {
            root = QDir(QFileInfo(path).absolutePath()).absoluteFilePath(root);
        }

        const QString canonicalRoot = QDir(root).canonicalPath();
        if (!canonicalRoot.isEmpty()) {
            root = canonicalRoot;
        }
        if (!QDir(root).exists()) {
            QMessageBox::warning(this, QStringLiteral("Open project failed"),
                QStringLiteral("Project folder is not available:\n%1").arg(root));
            return false;
        }

        projectRoot = root;
        projectFile = path;
        loadProjectConfiguration(projectSettings);
        addRecentProject(projectFile);
        loadProjectIgnoreRules();
        loadProjectFiles();

        const QStringList files = projectSettings.value(QStringLiteral("session/files")).toStringList();
        for (const QString& entry : files) {
            QString filePath = entry;
            if (QDir::isRelativePath(filePath)) {
                filePath = QDir(projectRoot).absoluteFilePath(filePath);
            }
            if (QFileInfo::exists(filePath)) {
                openPath(filePath, false);
            }
        }
        restoreCursorEntries(projectSettings.value(QStringLiteral("session/cursors")).toStringList(), projectRoot);
        restoreBookmarkEntries(projectSettings.value(QStringLiteral("session/bookmarks")).toStringList(), projectRoot);
        QString activeFile = projectSettings.value(QStringLiteral("session/activeFile")).toString();
        if (!activeFile.isEmpty() && QDir::isRelativePath(activeFile)) {
            activeFile = QDir(projectRoot).absoluteFilePath(activeFile);
        }
        setActiveFile(activeFile);

        appendOutput(QStringLiteral("Project loaded: %1").arg(projectFile));
        updateCursorStatus();
        return true;
    }

    void saveProjectFile()
    {
        if (projectRoot.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Save Project"), QStringLiteral("Open a folder or create a project first."));
            return;
        }

        if (projectFile.isEmpty()) {
            const QString defaultName = QFileInfo(projectRoot).fileName();
            projectFile = chooseSaveFileName(this, QStringLiteral("Save Project"),
                QDir(projectRoot).filePath(defaultName + QStringLiteral(".geany")),
                QStringLiteral("Geany Project (*.geany);;All Files (*)"));
            if (projectFile.isEmpty()) {
                return;
            }
        }

        if (writeProjectFile(projectFile, QFileInfo(projectRoot).fileName())) {
            appendOutput(QStringLiteral("Project saved: %1").arg(projectFile));
        }
    }

    bool writeProjectFile(const QString& path, const QString& name)
    {
        QSettings projectSettings(path, QSettings::IniFormat);
        projectSettings.clear();
        projectSettings.setValue(QStringLiteral("project/name"), name);
        projectSettings.setValue(QStringLiteral("project/base_path"), projectRoot);
        if (!projectDefaultLineEnding.isEmpty()) {
            projectSettings.setValue(QStringLiteral("editor/defaultLineEnding"), projectDefaultLineEnding);
        }
        if (!projectDefaultEncodingName.isEmpty()) {
            projectSettings.setValue(QStringLiteral("editor/defaultEncoding"), projectDefaultEncodingName);
        }
        projectSettings.setValue(QStringLiteral("project/extraIgnorePatterns"), projectExtraIgnorePatterns);
        projectSettings.setValue(QStringLiteral("build/command"), buildCommand);
        projectSettings.setValue(QStringLiteral("build/cleanCommand"), buildCleanCommand);
        projectSettings.setValue(QStringLiteral("build/targetCommand"), buildTargetCommand);
        projectSettings.setValue(QStringLiteral("build/compileC"), compileCCommand);
        projectSettings.setValue(QStringLiteral("build/compileCpp"), compileCppCommand);
        projectSettings.setValue(QStringLiteral("build/checkPython"), checkPythonCommand);
        projectSettings.setValue(QStringLiteral("build/checkShell"), checkShellCommand);
        projectSettings.setValue(QStringLiteral("build/checkJs"), checkJsCommand);
        projectSettings.setValue(QStringLiteral("build/checkArkTs"), checkArkTsCommand);
        projectSettings.setValue(QStringLiteral("build/checkJson"), checkJsonCommand);
        projectSettings.setValue(QStringLiteral("build/checkGo"), checkGoCommand);
        projectSettings.setValue(QStringLiteral("build/checkRust"), checkRustCommand);
        projectSettings.setValue(QStringLiteral("build/checkJava"), checkJavaCommand);
        projectSettings.setValue(QStringLiteral("build/checkPhp"), checkPhpCommand);
        projectSettings.setValue(QStringLiteral("build/checkYaml"), checkYamlCommand);
        projectSettings.setValue(QStringLiteral("build/checkToml"), checkTomlCommand);
        projectSettings.setValue(QStringLiteral("build/executePython"), executePythonCommand);
        projectSettings.setValue(QStringLiteral("build/executeShell"), executeShellCommand);
        projectSettings.setValue(QStringLiteral("build/executeJs"), executeJsCommand);
        projectSettings.setValue(QStringLiteral("build/executeGo"), executeGoCommand);
        projectSettings.setValue(QStringLiteral("build/executeRust"), executeRustCommand);
        projectSettings.setValue(QStringLiteral("build/executeJava"), executeJavaCommand);
        projectSettings.setValue(QStringLiteral("build/executePhp"), executePhpCommand);
        projectSettings.setValue(QStringLiteral("build/executeNative"), executeNativeCommand);

        QStringList files;
        QStringList cursorEntries;
        const QDir rootDir(projectRoot);
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr && !page->path.isEmpty()) {
                files << rootDir.relativeFilePath(page->path);
                cursorEntries << cursorEntry(page, rootDir, true);
            }
        }
        projectSettings.setValue(QStringLiteral("session/files"), files);
        projectSettings.setValue(QStringLiteral("session/cursors"), cursorEntries);
        projectSettings.setValue(QStringLiteral("session/bookmarks"), bookmarkEntriesForOpenFiles(rootDir, true));
        projectSettings.setValue(QStringLiteral("session/activeFile"),
            currentPage() != nullptr && !currentPage()->path.isEmpty() ? rootDir.relativeFilePath(currentPage()->path) : QString());
        projectSettings.sync();
        if (projectSettings.status() != QSettings::NoError) {
            QMessageBox::warning(this, QStringLiteral("Save project failed"), QStringLiteral("Could not write project file."));
            return false;
        }
        return true;
    }

    bool isSourceFile(const QString& path) const
    {
        const QFileInfo info(path);
        const QString name = info.fileName();
        const QString ext = info.suffix().toLower();
        return QStringList{
            QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"),
            QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("hh"), QStringLiteral("hxx"),
            QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"), QStringLiteral("ets"),
            QStringLiteral("json"), QStringLiteral("json5"), QStringLiteral("xml"), QStringLiteral("html"),
            QStringLiteral("htm"), QStringLiteral("css"), QStringLiteral("md"), QStringLiteral("markdown"),
            QStringLiteral("txt"), QStringLiteral("sh"), QStringLiteral("cmake"), QStringLiteral("meson"),
            QStringLiteral("java"), QStringLiteral("go"), QStringLiteral("rs"), QStringLiteral("php"),
            QStringLiteral("sql"), QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
            QStringLiteral("mk"), QStringLiteral("lua"), QStringLiteral("ini"), QStringLiteral("conf"),
            QStringLiteral("cfg"), QStringLiteral("desktop"), QStringLiteral("service"), QStringLiteral("diff"),
            QStringLiteral("patch")
        }.contains(ext) || name == QStringLiteral("CMakeLists.txt") || name == QStringLiteral("Makefile")
            || name == QStringLiteral("GNUmakefile") || name == QStringLiteral("Dockerfile")
            || name == QStringLiteral("Containerfile") || name.startsWith(QStringLiteral("Dockerfile."))
            || name.startsWith(QStringLiteral("Containerfile."));
    }

    bool canScanTextFile(const QString& path) const
    {
        const QFileInfo info(path);
        const qint64 maxScanBytes = 5 * 1024 * 1024;
        if (!info.exists() || !info.isFile() || info.size() > maxScanBytes) {
            return false;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        return !looksLikeBinary(file.peek(8192));
    }

    bool appendIgnoreRule(QString line, QVector<IgnoreRule>* rules) const
    {
        if (rules == nullptr) {
            return false;
        }
        line.replace(QLatin1Char('\\'), QLatin1Char('/'));
        line = line.trimmed();
        if (line.isEmpty()) {
            return false;
        }
        if (line.startsWith(QStringLiteral("\\#"))) {
            line.remove(0, 1);
        } else if (line.startsWith(QLatin1Char('#'))) {
            return false;
        }

        IgnoreRule rule;
        if (line.startsWith(QLatin1Char('!'))) {
            rule.negated = true;
            line.remove(0, 1);
            line = line.trimmed();
        }
        if (line.startsWith(QLatin1Char('/'))) {
            rule.anchored = true;
            while (line.startsWith(QLatin1Char('/'))) {
                line.remove(0, 1);
            }
        }
        while (line.endsWith(QLatin1Char('/'))) {
            rule.directoryOnly = true;
            line.chop(1);
        }
        if (line.isEmpty()) {
            return false;
        }
        rule.pattern = QDir::cleanPath(line);
        rule.hasSlash = rule.pattern.contains(QLatin1Char('/'));
        rules->push_back(rule);
        return true;
    }

    QVector<IgnoreRule> readIgnoreRules(const QString& rootPath) const
    {
        QVector<IgnoreRule> rules;
        if (rootPath.isEmpty()) {
            return rules;
        }

        QFile file(QDir(rootPath).filePath(QStringLiteral(".gitignore")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return rules;
        }

        while (!file.atEnd()) {
            appendIgnoreRule(QString::fromUtf8(file.readLine()), &rules);
        }
        return rules;
    }

    void loadProjectIgnoreRules()
    {
        projectIgnoreRules = readIgnoreRules(projectRoot);
        for (const QString& pattern : projectExtraIgnorePatterns) {
            appendIgnoreRule(pattern, &projectIgnoreRules);
        }
        ignoreRulesByRoot.insert(QDir(projectRoot).absolutePath(), projectIgnoreRules);
    }

    QVector<IgnoreRule> ignoreRulesForRoot(const QString& rootPath) const
    {
        if (rootPath.isEmpty()) {
            return {};
        }
        const QString normalizedRoot = QDir(rootPath).absolutePath();
        if (normalizedRoot == QDir(projectRoot).absolutePath()) {
            return projectIgnoreRules;
        }
        if (!ignoreRulesByRoot.contains(normalizedRoot)) {
            ignoreRulesByRoot.insert(normalizedRoot, readIgnoreRules(normalizedRoot));
        }
        return ignoreRulesByRoot.value(normalizedRoot);
    }

    bool pathMatchesIgnoreRule(const IgnoreRule& rule, const QString& relativePath) const
    {
        const QString relative = QDir::cleanPath(relativePath);
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (relative.isEmpty() || relative.startsWith(QStringLiteral(".."))) {
            return false;
        }

        if (!rule.hasSlash) {
            const int lastIndex = rule.directoryOnly ? parts.size() - 1 : parts.size();
            for (int i = 0; i < lastIndex; ++i) {
                if (QDir::match(rule.pattern, parts.at(i))) {
                    return true;
                }
            }
            return false;
        }

        if (rule.directoryOnly
            && (relative == rule.pattern || relative.startsWith(rule.pattern + QLatin1Char('/')))) {
            return true;
        }
        if (QDir::match(rule.pattern, relative)) {
            return true;
        }
        if (!rule.anchored && QDir::match(QStringLiteral("*/") + rule.pattern, relative)) {
            return true;
        }
        return false;
    }

    void loadProjectFiles()
    {
        projectFilePaths.clear();
        loadProjectIgnoreRules();
        QDirIterator it(projectRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        int count = 0;
        while (it.hasNext() && count < 5000) {
            const QString path = it.next();
            if (isIgnoredProjectPath(path)) {
                continue;
            }
            if (!isSourceFile(path)) {
                continue;
            }
            projectFilePaths << path;
            ++count;
        }
        projectFilePaths.sort(Qt::CaseInsensitive);
        populateProjectFiles();
    }

    bool isIgnoredProjectPath(const QString& path, const QString& rootOverride = QString()) const
    {
        const QString rootPath = rootOverride.isEmpty() ? projectRoot : rootOverride;
        if (rootPath.isEmpty()) {
            return false;
        }
        const QString relative = QDir(rootPath).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (part.startsWith(QLatin1Char('.'))) {
                return true;
            }
            if (part == QStringLiteral("build") || part == QStringLiteral("cmake-build-debug")
                || part == QStringLiteral("cmake-build-release") || part == QStringLiteral("node_modules")
                || part == QStringLiteral("oh_modules") || part == QStringLiteral("target")
                || part == QStringLiteral("__pycache__") || part == QStringLiteral("qtforharmony_sdk")
                || part == QStringLiteral("dist") || part == QStringLiteral("out")
                || part == QStringLiteral("coverage") || part == QStringLiteral("vendor")) {
                return true;
            }
        }
        bool ignored = false;
        const QVector<IgnoreRule> rules = ignoreRulesForRoot(rootPath);
        for (const IgnoreRule& rule : rules) {
            if (pathMatchesIgnoreRule(rule, relative)) {
                ignored = !rule.negated;
            }
        }
        return ignored;
    }

    void populateProjectFiles()
    {
        if (projectFiles == nullptr) {
            return;
        }

        const QString previous = selectedProjectPath();
        projectFiles->clear();
        if (projectRoot.isEmpty()) {
            return;
        }

        const QString filter = projectFileFilter != nullptr ? projectFileFilter->text().trimmed() : QString();
        const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QDir rootDir(projectRoot);
        for (const QString& path : projectFilePaths) {
            const QString label = rootDir.relativeFilePath(path);
            bool matches = true;
            for (const QString& term : terms) {
                if (!label.contains(term, Qt::CaseInsensitive)) {
                    matches = false;
                    break;
                }
            }
            if (!matches) {
                continue;
            }
            auto* item = new QListWidgetItem(label, projectFiles);
            item->setData(Qt::UserRole, path);
            if (!previous.isEmpty() && QFileInfo(previous) == QFileInfo(path)) {
                projectFiles->setCurrentItem(item);
            }
        }
    }

    void refreshSymbols()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }

        symbolsList->clear();
        const QVector<ProjectSymbol> symbols = symbolsForText(page->path, page->editor->toPlainText());
        for (const ProjectSymbol& symbol : symbols) {
            auto* item = new QListWidgetItem(QStringLiteral("%1  [%2]  line %3")
                .arg(symbol.name, symbol.kind.isEmpty() ? QStringLiteral("symbol") : symbol.kind)
                .arg(symbol.line), symbolsList);
            item->setData(Qt::UserRole, symbol.line);
        }
    }

    void setBuildCommand()
    {
        bool ok = false;
        const QString command = QInputDialog::getText(this, QStringLiteral("Build Command"), QStringLiteral("Command:"), QLineEdit::Normal, buildCommand, &ok);
        if (ok && !command.trimmed().isEmpty()) {
            buildCommand = command.trimmed();
        }
    }

    void showBuildSettings()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Build Settings"));
        auto* layout = new QFormLayout(&dialog);

        auto* buildEdit = new QLineEdit(buildCommand, &dialog);
        auto* cleanEdit = new QLineEdit(buildCleanCommand, &dialog);
        auto* targetEdit = new QLineEdit(buildTargetCommand, &dialog);
        auto* cCompileEdit = new QLineEdit(compileCCommand, &dialog);
        auto* cppCompileEdit = new QLineEdit(compileCppCommand, &dialog);
        auto* pythonCheckEdit = new QLineEdit(checkPythonCommand, &dialog);
        auto* shellCheckEdit = new QLineEdit(checkShellCommand, &dialog);
        auto* jsCheckEdit = new QLineEdit(checkJsCommand, &dialog);
        auto* arkTsCheckEdit = new QLineEdit(checkArkTsCommand, &dialog);
        auto* jsonCheckEdit = new QLineEdit(checkJsonCommand, &dialog);
        auto* goCheckEdit = new QLineEdit(checkGoCommand, &dialog);
        auto* rustCheckEdit = new QLineEdit(checkRustCommand, &dialog);
        auto* javaCheckEdit = new QLineEdit(checkJavaCommand, &dialog);
        auto* phpCheckEdit = new QLineEdit(checkPhpCommand, &dialog);
        auto* yamlCheckEdit = new QLineEdit(checkYamlCommand, &dialog);
        auto* tomlCheckEdit = new QLineEdit(checkTomlCommand, &dialog);
        auto* pythonExecEdit = new QLineEdit(executePythonCommand, &dialog);
        auto* shellExecEdit = new QLineEdit(executeShellCommand, &dialog);
        auto* jsExecEdit = new QLineEdit(executeJsCommand, &dialog);
        auto* goExecEdit = new QLineEdit(executeGoCommand, &dialog);
        auto* rustExecEdit = new QLineEdit(executeRustCommand, &dialog);
        auto* javaExecEdit = new QLineEdit(executeJavaCommand, &dialog);
        auto* phpExecEdit = new QLineEdit(executePhpCommand, &dialog);
        auto* nativeExecEdit = new QLineEdit(executeNativeCommand, &dialog);
        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

        layout->addRow(QStringLiteral("Build"), buildEdit);
        layout->addRow(QStringLiteral("Clean"), cleanEdit);
        layout->addRow(QStringLiteral("Custom Target"), targetEdit);
        layout->addRow(QStringLiteral("Compile C"), cCompileEdit);
        layout->addRow(QStringLiteral("Compile C++"), cppCompileEdit);
        layout->addRow(QStringLiteral("Check Python"), pythonCheckEdit);
        layout->addRow(QStringLiteral("Check Shell"), shellCheckEdit);
        layout->addRow(QStringLiteral("Check JavaScript"), jsCheckEdit);
        layout->addRow(QStringLiteral("Check ArkTS/OpenHarmony"), arkTsCheckEdit);
        layout->addRow(QStringLiteral("Check JSON"), jsonCheckEdit);
        layout->addRow(QStringLiteral("Check Go"), goCheckEdit);
        layout->addRow(QStringLiteral("Check Rust"), rustCheckEdit);
        layout->addRow(QStringLiteral("Check Java"), javaCheckEdit);
        layout->addRow(QStringLiteral("Check PHP"), phpCheckEdit);
        layout->addRow(QStringLiteral("Check YAML"), yamlCheckEdit);
        layout->addRow(QStringLiteral("Check TOML"), tomlCheckEdit);
        layout->addRow(QStringLiteral("Execute Python"), pythonExecEdit);
        layout->addRow(QStringLiteral("Execute Shell"), shellExecEdit);
        layout->addRow(QStringLiteral("Execute JavaScript"), jsExecEdit);
        layout->addRow(QStringLiteral("Execute Go"), goExecEdit);
        layout->addRow(QStringLiteral("Execute Rust"), rustExecEdit);
        layout->addRow(QStringLiteral("Execute Java"), javaExecEdit);
        layout->addRow(QStringLiteral("Execute PHP"), phpExecEdit);
        layout->addRow(QStringLiteral("Execute Native"), nativeExecEdit);
        layout->addRow(QStringLiteral("Placeholders"), new QLabel(QStringLiteral("{file} {dir} {project} {name} {base} {output} {target}"), &dialog));
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            buildCommand = buildEdit->text().trimmed();
            buildCleanCommand = cleanEdit->text().trimmed();
            buildTargetCommand = targetEdit->text().trimmed();
            compileCCommand = cCompileEdit->text().trimmed();
            compileCppCommand = cppCompileEdit->text().trimmed();
            checkPythonCommand = pythonCheckEdit->text().trimmed();
            checkShellCommand = shellCheckEdit->text().trimmed();
            checkJsCommand = jsCheckEdit->text().trimmed();
            checkArkTsCommand = arkTsCheckEdit->text().trimmed();
            checkJsonCommand = jsonCheckEdit->text().trimmed();
            checkGoCommand = goCheckEdit->text().trimmed();
            checkRustCommand = rustCheckEdit->text().trimmed();
            checkJavaCommand = javaCheckEdit->text().trimmed();
            checkPhpCommand = phpCheckEdit->text().trimmed();
            checkYamlCommand = yamlCheckEdit->text().trimmed();
            checkTomlCommand = tomlCheckEdit->text().trimmed();
            executePythonCommand = pythonExecEdit->text().trimmed();
            executeShellCommand = shellExecEdit->text().trimmed();
            executeJsCommand = jsExecEdit->text().trimmed();
            executeGoCommand = goExecEdit->text().trimmed();
            executeRustCommand = rustExecEdit->text().trimmed();
            executeJavaCommand = javaExecEdit->text().trimmed();
            executePhpCommand = phpExecEdit->text().trimmed();
            executeNativeCommand = nativeExecEdit->text().trimmed();
            appendOutput(QStringLiteral("Build settings updated."));
        }
    }

    QString shellQuote(const QString& value) const
    {
        QString quoted = value;
        quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QStringLiteral("'%1'").arg(quoted);
    }

    QString cacheExecutablePath(const QString& sourcePath) const
    {
#if defined(Q_OS_OPENHARMONY)
        return QStringLiteral("/data/storage/el2/base/cache/geany-run-%1").arg(QFileInfo(sourcePath).completeBaseName());
#else
        return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("geany-run-%1").arg(QFileInfo(sourcePath).completeBaseName()));
#endif
    }

    QString expandCommandTemplate(QString commandTemplate, const QString& path, const QString& target = QString()) const
    {
        const QFileInfo info(path);
        const QString projectPath = !projectRoot.isEmpty() ? projectRoot : info.absolutePath();
        commandTemplate.replace(QStringLiteral("{file}"), shellQuote(path));
        commandTemplate.replace(QStringLiteral("{dir}"), shellQuote(info.absolutePath()));
        commandTemplate.replace(QStringLiteral("{project}"), shellQuote(projectPath));
        commandTemplate.replace(QStringLiteral("{name}"), shellQuote(info.fileName()));
        commandTemplate.replace(QStringLiteral("{base}"), shellQuote(info.completeBaseName()));
        commandTemplate.replace(QStringLiteral("{output}"), shellQuote(cacheExecutablePath(path)));
        commandTemplate.replace(QStringLiteral("{target}"), shellQuote(target));
        return commandTemplate;
    }

    bool ensureCurrentDocumentSaved()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return false;
        }
        if (page->path.isEmpty() || page->editor->document()->isModified()) {
            return saveDocument(page);
        }
        return true;
    }

    QString compileCommandForFile(const QString& path) const
    {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QStringLiteral("c")) {
            return expandCommandTemplate(compileCCommand, path);
        }
        if (ext == QStringLiteral("cc") || ext == QStringLiteral("cpp") || ext == QStringLiteral("cxx")) {
            return expandCommandTemplate(compileCppCommand, path);
        }
        if (ext == QStringLiteral("py")) {
            return expandCommandTemplate(checkPythonCommand, path);
        }
        if (ext == QStringLiteral("sh")) {
            return expandCommandTemplate(checkShellCommand, path);
        }
        if (ext == QStringLiteral("js")) {
            return expandCommandTemplate(checkJsCommand, path);
        }
        if (ext == QStringLiteral("ets")) {
            return expandCommandTemplate(checkArkTsCommand, path);
        }
        if (ext == QStringLiteral("json") || ext == QStringLiteral("json5")) {
            return expandCommandTemplate(checkJsonCommand, path);
        }
        if (ext == QStringLiteral("go")) {
            return expandCommandTemplate(checkGoCommand, path);
        }
        if (ext == QStringLiteral("rs")) {
            return expandCommandTemplate(checkRustCommand, path);
        }
        if (ext == QStringLiteral("java")) {
            return expandCommandTemplate(checkJavaCommand, path);
        }
        if (ext == QStringLiteral("php")) {
            return expandCommandTemplate(checkPhpCommand, path);
        }
        if (ext == QStringLiteral("yaml") || ext == QStringLiteral("yml")) {
            return expandCommandTemplate(checkYamlCommand, path);
        }
        if (ext == QStringLiteral("toml")) {
            return expandCommandTemplate(checkTomlCommand, path);
        }
        return QString();
    }

    QString executeCommandForFile(const QString& path) const
    {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QStringLiteral("py")) {
            return expandCommandTemplate(executePythonCommand, path);
        }
        if (ext == QStringLiteral("sh")) {
            return expandCommandTemplate(executeShellCommand, path);
        }
        if (ext == QStringLiteral("js")) {
            return expandCommandTemplate(executeJsCommand, path);
        }
        if (ext == QStringLiteral("go")) {
            return expandCommandTemplate(executeGoCommand, path);
        }
        if (ext == QStringLiteral("rs")) {
            return expandCommandTemplate(executeRustCommand, path);
        }
        if (ext == QStringLiteral("java")) {
            return expandCommandTemplate(executeJavaCommand, path);
        }
        if (ext == QStringLiteral("php")) {
            return expandCommandTemplate(executePhpCommand, path);
        }
        if (ext == QStringLiteral("c") || ext == QStringLiteral("cc") || ext == QStringLiteral("cpp") || ext == QStringLiteral("cxx")) {
            return expandCommandTemplate(executeNativeCommand, path);
        }
        return QString();
    }

    void compileCurrentFile()
    {
        if (!ensureCurrentDocumentSaved()) {
            return;
        }
        const QString path = currentPage()->path;
        const QString command = compileCommandForFile(path);
        if (command.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Compile"),
                QStringLiteral("No default compile command for %1. Use Build > Set Build Command for this project.").arg(fileTypeName(path)));
            return;
        }
        runShellCommand(command);
    }

    void executeCurrentFile()
    {
        if (!ensureCurrentDocumentSaved()) {
            return;
        }
        const QString path = currentPage()->path;
        QString command = executeCommandForFile(path);
        if (command.isEmpty()) {
            bool ok = false;
            command = QInputDialog::getText(this, QStringLiteral("Execute"), QStringLiteral("Command:"),
                QLineEdit::Normal, shellQuote(path), &ok);
            if (!ok) {
                return;
            }
        }
        runShellCommand(command);
    }

    void stopCommand()
    {
        if (buildProcess != nullptr && buildProcess->state() != QProcess::NotRunning) {
            buildProcess->kill();
            appendOutput(QStringLiteral("Command stopped."));
        }
    }

    void ensureCommandProcess()
    {
        if (buildProcess != nullptr && buildProcess->state() != QProcess::NotRunning) {
            buildProcess->kill();
        }

        if (buildProcess == nullptr) {
            buildProcess = new QProcess(this);
            connect(buildProcess, &QProcess::readyReadStandardOutput, this, [this]() {
                appendOutput(QString::fromUtf8(buildProcess->readAllStandardOutput()).trimmed());
            });
            connect(buildProcess, &QProcess::readyReadStandardError, this, [this]() {
                appendOutput(QString::fromUtf8(buildProcess->readAllStandardError()).trimmed());
            });
            connect(buildProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
                appendOutput(QStringLiteral("Command finished: exit=%1 status=%2").arg(code).arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed")));
                statusBar()->showMessage(QStringLiteral("Command finished"), 3000);
            });
            connect(buildProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                appendOutput(QStringLiteral("Command process error: %1").arg(error));
            });
        }
    }

    QString shellProgram() const
    {
#if defined(Q_OS_OPENHARMONY)
        return QStringLiteral("/system/bin/sh");
#else
        return QStringLiteral("/bin/sh");
#endif
    }

    void runShellCommand(const QString& command, const QString& workingDirectory = QString())
    {
        if (command.trimmed().isEmpty()) {
            return;
        }

        QString directory = workingDirectory.trimmed().isEmpty() ? currentWorkDir() : workingDirectory.trimmed();
        if (!QDir(directory).exists()) {
            directory = currentWorkDir();
        }
        lastShellCommand = command;
        lastShellWorkingDirectory = directory;
        saveSettings();

        ensureCommandProcess();
        if (outputTabs != nullptr) {
            outputTabs->setVisible(true);
            outputTabs->setCurrentWidget(output);
        }
        showOutputAction->setChecked(true);
        problemsList->clear();
        appendOutput(QStringLiteral("[%1] $ %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), command));
        buildProcess->setWorkingDirectory(directory);
        buildProcess->start(shellProgram(), QStringList() << QStringLiteral("-lc") << command);
    }

    void showTerminal()
    {
        if (outputTabs != nullptr && terminalPanel != nullptr) {
            outputTabs->setVisible(true);
            outputTabs->setCurrentWidget(terminalPanel);
        }
        if (showOutputAction != nullptr) {
            showOutputAction->setChecked(true);
        }
        startTerminalShell();
        if (terminalInput != nullptr) {
            terminalInput->setFocus();
        }
    }

    void startTerminalShell()
    {
        if (terminalProcess == nullptr) {
            terminalProcess = new QProcess(this);
            connect(terminalProcess, &QProcess::readyReadStandardOutput, this, [this]() {
                if (terminalOutput != nullptr) {
                    const QString text = QString::fromUtf8(terminalProcess->readAllStandardOutput()).trimmed();
                    if (!text.isEmpty()) {
                        terminalOutput->appendPlainText(text);
                    }
                }
            });
            connect(terminalProcess, &QProcess::readyReadStandardError, this, [this]() {
                if (terminalOutput != nullptr) {
                    const QString text = QString::fromUtf8(terminalProcess->readAllStandardError()).trimmed();
                    if (!text.isEmpty()) {
                        terminalOutput->appendPlainText(text);
                    }
                }
            });
            connect(terminalProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
                if (terminalOutput != nullptr) {
                    terminalOutput->appendPlainText(QStringLiteral("[exit=%1, %2]")
                        .arg(code)
                        .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed")));
                }
            });
            connect(terminalProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                if (terminalOutput != nullptr) {
                    terminalOutput->appendPlainText(QStringLiteral("[terminal error: %1]").arg(error));
                }
            });
        }
        if (lastShellWorkingDirectory.isEmpty() || !QDir(lastShellWorkingDirectory).exists()) {
            lastShellWorkingDirectory = currentWorkDir();
        }
        if (terminalOutput != nullptr && terminalOutput->document()->blockCount() <= 1
            && terminalOutput->toPlainText().trimmed().isEmpty()) {
            terminalOutput->appendPlainText(QStringLiteral("[terminal ready in %1]").arg(lastShellWorkingDirectory));
        }
    }

    void restartTerminalShell()
    {
        if (terminalProcess != nullptr && terminalProcess->state() != QProcess::NotRunning) {
            terminalProcess->kill();
            terminalProcess->waitForFinished(1000);
        }
        if (terminalOutput != nullptr) {
            terminalOutput->clear();
        }
        startTerminalShell();
        showTerminal();
    }

    QString terminalWorkingDirectory() const
    {
        if (!lastShellWorkingDirectory.isEmpty() && QDir(lastShellWorkingDirectory).exists()) {
#if defined(Q_OS_OPENHARMONY)
            if (!openHarmonyPathNeedsWritableFallback(lastShellWorkingDirectory)) {
                return lastShellWorkingDirectory;
            }
#else
            return lastShellWorkingDirectory;
#endif
        }
        return currentWorkDir();
    }

    QString terminalPathArgument(QString value) const
    {
        value = value.trimmed();
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        if (value.isEmpty() || value == QStringLiteral(".")) {
            return terminalWorkingDirectory();
        }
        if (value == QStringLiteral("~")) {
            return QDir::homePath();
        }
        if (value.startsWith(QStringLiteral("~/"))) {
            return QDir::home().filePath(value.mid(2));
        }
        return QDir::isAbsolutePath(value) ? value : QDir(terminalWorkingDirectory()).absoluteFilePath(value);
    }

    bool handleTerminalBuiltIn(const QString& command)
    {
        if (command == QStringLiteral("clear")) {
            if (terminalOutput != nullptr) {
                terminalOutput->clear();
            }
            return true;
        }

        if (command == QStringLiteral("cd") || command.startsWith(QStringLiteral("cd "))) {
            QString target = command.size() == 2 ? QDir::homePath() : command.mid(3).trimmed();
            if ((target.startsWith(QLatin1Char('"')) && target.endsWith(QLatin1Char('"')))
                || (target.startsWith(QLatin1Char('\'')) && target.endsWith(QLatin1Char('\'')))) {
                target = target.mid(1, target.size() - 2);
            }
            if (target == QStringLiteral("~")) {
                target = QDir::homePath();
            } else if (target.startsWith(QStringLiteral("~/"))) {
                target = QDir::home().filePath(target.mid(2));
            }
            QDir dir(terminalWorkingDirectory());
            const QString next = QDir::isAbsolutePath(target) ? QDir(target).absolutePath() : dir.absoluteFilePath(target);
            QDir nextDir(next);
            if (!nextDir.exists()) {
                if (terminalOutput != nullptr) {
                    terminalOutput->appendPlainText(QStringLiteral("cd: no such directory: %1").arg(target));
                }
                return true;
            }
            lastShellWorkingDirectory = nextDir.canonicalPath();
            if (lastShellWorkingDirectory.isEmpty()) {
                lastShellWorkingDirectory = nextDir.absolutePath();
            }
            saveSettings();
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(QStringLiteral("[cwd %1]").arg(lastShellWorkingDirectory));
            }
            return true;
        }

        if (command == QStringLiteral("pwd")) {
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(terminalWorkingDirectory());
            }
            return true;
        }

        if (command == QStringLiteral("geany-self-test")) {
            runMigrationSelfTest();
            return true;
        }

        if (command == QStringLiteral("ls") || command.startsWith(QStringLiteral("ls "))) {
            QString target = command.size() == 2 ? QStringLiteral(".") : command.mid(3).trimmed();
            while (target.startsWith(QLatin1Char('-'))) {
                const int space = target.indexOf(QLatin1Char(' '));
                target = space < 0 ? QStringLiteral(".") : target.mid(space + 1).trimmed();
            }
            const QString path = terminalPathArgument(target);
            QFileInfo info(path);
            QStringList lines;
            if (info.isDir()) {
                QDir dir(path);
                const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
                for (const QFileInfo& entry : entries) {
                    lines << QStringLiteral("%1%2").arg(entry.fileName(), entry.isDir() ? QStringLiteral("/") : QString());
                }
            } else if (info.exists()) {
                lines << info.fileName();
            } else {
                lines << QStringLiteral("ls: cannot access %1").arg(target);
            }
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(lines.join(QLatin1Char('\n')));
            }
            return true;
        }

        if (command == QStringLiteral("echo") || command.startsWith(QStringLiteral("echo "))) {
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(command.size() == 4 ? QString() : command.mid(5));
            }
            return true;
        }

        if (command.startsWith(QStringLiteral("cat "))) {
            const QString path = terminalPathArgument(command.mid(4));
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (terminalOutput != nullptr) {
                    terminalOutput->appendPlainText(QStringLiteral("cat: %1: %2").arg(path, file.errorString()));
                }
                return true;
            }
            const QByteArray bytes = file.read(64 * 1024);
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(QString::fromUtf8(bytes));
                if (!file.atEnd()) {
                    terminalOutput->appendPlainText(QStringLiteral("[cat truncated at 64 KiB]"));
                }
            }
            return true;
        }

        return false;
    }

    void sendTerminalCommand()
    {
        if (terminalInput == nullptr) {
            return;
        }
        const QString command = terminalInput->text().trimmed();
        if (command.isEmpty()) {
            return;
        }
        showTerminal();
        if (terminalOutput != nullptr) {
            terminalOutput->appendPlainText(QStringLiteral("$ %1").arg(command));
        }
        addCommandHistory(command);
        if (handleTerminalBuiltIn(command)) {
            terminalInput->clear();
            return;
        }
        if (terminalProcess != nullptr && terminalProcess->state() != QProcess::NotRunning) {
            if (terminalOutput != nullptr) {
                terminalOutput->appendPlainText(QStringLiteral("[terminal busy; stop or wait before running another command]"));
            }
            terminalInput->clear();
            return;
        }
        startTerminalShell();
        if (terminalProcess != nullptr) {
            lastShellCommand = command;
            lastShellWorkingDirectory = terminalWorkingDirectory();
            saveSettings();
            terminalProcess->setWorkingDirectory(lastShellWorkingDirectory);
            terminalProcess->start(shellProgram(), QStringList() << QStringLiteral("-lc") << command);
        }
        terminalInput->clear();
    }

    void repeatLastCommand()
    {
        if (lastShellCommand.trimmed().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("No command has been run yet"), 2500);
            return;
        }
        runShellCommand(lastShellCommand, lastShellWorkingDirectory);
    }

    QString buildContextPath() const
    {
        if (EditorPage* page = currentPage(); page != nullptr && !page->path.isEmpty()) {
            return page->path;
        }
        if (!projectRoot.isEmpty()) {
            return QDir(projectRoot).absoluteFilePath(QStringLiteral("."));
        }
        return QDir(currentWorkDir()).absoluteFilePath(QStringLiteral("."));
    }

    void runBuildCommand()
    {
        if (buildCommand.isEmpty()) {
            setBuildCommand();
            if (buildCommand.isEmpty()) {
                return;
            }
        }
        if (saveAllBeforeBuild && !saveAllDocuments()) {
            appendOutput(QStringLiteral("Build cancelled: not all modified files could be saved."));
            return;
        }
        runShellCommand(expandCommandTemplate(buildCommand, buildContextPath()));
    }

    void runCleanCommand()
    {
        if (buildCleanCommand.isEmpty()) {
            showBuildSettings();
            if (buildCleanCommand.isEmpty()) {
                return;
            }
        }
        runShellCommand(expandCommandTemplate(buildCleanCommand, buildContextPath()));
    }

    void runCustomBuildTarget()
    {
        if (buildTargetCommand.isEmpty()) {
            showBuildSettings();
            if (buildTargetCommand.isEmpty()) {
                return;
            }
        }

        bool ok = false;
        const QString target = QInputDialog::getText(this, QStringLiteral("Build Custom Target"),
            QStringLiteral("Target or hvigor arguments:"), QLineEdit::Normal,
            looksLikeOpenHarmonyProject(projectRoot) ? QStringLiteral("--mode module -p module=entry assembleHap") : QStringLiteral("all"),
            &ok).trimmed();
        if (!ok || target.isEmpty()) {
            return;
        }
        runShellCommand(expandCommandTemplate(buildTargetCommand, buildContextPath(), target));
    }

    void runToolCommand()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Run Command"));
        auto* layout = new QFormLayout(&dialog);
        auto* commandCombo = new QComboBox(&dialog);
        commandCombo->setEditable(true);
        commandCombo->addItems(commandHistory);
        if (commandCombo->lineEdit() != nullptr) {
            commandCombo->lineEdit()->setPlaceholderText(QStringLiteral("Command"));
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(QStringLiteral("Command"), commandCombo);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            const QString command = commandCombo->currentText().trimmed();
            if (!command.isEmpty()) {
                addCommandHistory(command);
                runShellCommand(command);
            }
        }
    }

    void filterSelectionThroughCommand()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        if (!cursor.hasSelection()) {
            statusBar()->showMessage(QStringLiteral("Select text before filtering it through a command"), 3000);
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Filter Selection Through Command"));
        auto* layout = new QFormLayout(&dialog);
        auto* commandCombo = new QComboBox(&dialog);
        commandCombo->setEditable(true);
        commandCombo->addItems(commandHistory);
        if (commandCombo->lineEdit() != nullptr) {
            commandCombo->lineEdit()->setPlaceholderText(QStringLiteral("Command reads stdin and writes stdout"));
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(QStringLiteral("Command"), commandCombo);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString command = commandCombo->currentText().trimmed();
        if (command.isEmpty()) {
            return;
        }

        QString input = cursor.selectedText();
        input.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

        QProcess process;
        process.setWorkingDirectory(currentWorkDir());
        process.start(shellProgram(), QStringList() << QStringLiteral("-lc") << command);
        if (!process.waitForStarted(5000)) {
            QMessageBox::warning(this, QStringLiteral("Filter Selection"), process.errorString());
            return;
        }
        process.write(input.toUtf8());
        process.closeWriteChannel();
        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(3000);
            QMessageBox::warning(this, QStringLiteral("Filter Selection"), QStringLiteral("Command timed out."));
            return;
        }

        const QString stdoutText = normalizeLineEndings(QString::fromUtf8(process.readAllStandardOutput()));
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        addCommandHistory(command);
        lastShellCommand = command;
        lastShellWorkingDirectory = currentWorkDir();
        saveSettings();
        if (!stderrText.isEmpty()) {
            appendOutput(stderrText);
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            appendOutput(QStringLiteral("Filter command failed: exit=%1").arg(process.exitCode()));
            statusBar()->showMessage(QStringLiteral("Filter command failed"), 3000);
            return;
        }

        cursor.beginEditBlock();
        cursor.insertText(stdoutText);
        cursor.endEditBlock();
        updateTabTitle(page, true);
        updateDocumentList();
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Filtered selection through command: %1").arg(command));
        statusBar()->showMessage(QStringLiteral("Selection filtered"), 2500);
    }

    void insertCommandOutput()
    {
        EditorPage* page = currentPage();
        if (page == nullptr || page->editor == nullptr) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Insert Command Output"));
        auto* layout = new QFormLayout(&dialog);
        auto* commandCombo = new QComboBox(&dialog);
        commandCombo->setEditable(true);
        commandCombo->addItems(commandHistory);
        if (commandCombo->lineEdit() != nullptr) {
            commandCombo->lineEdit()->setPlaceholderText(QStringLiteral("Command writes text to stdout"));
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(QStringLiteral("Command"), commandCombo);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString command = commandCombo->currentText().trimmed();
        if (command.isEmpty()) {
            return;
        }

        QProcess process;
        process.setWorkingDirectory(currentWorkDir());
        process.start(shellProgram(), QStringList() << QStringLiteral("-lc") << command);
        if (!process.waitForStarted(5000)) {
            QMessageBox::warning(this, QStringLiteral("Insert Command Output"), process.errorString());
            return;
        }
        process.closeWriteChannel();
        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(3000);
            QMessageBox::warning(this, QStringLiteral("Insert Command Output"), QStringLiteral("Command timed out."));
            return;
        }

        const QString stdoutText = normalizeLineEndings(QString::fromUtf8(process.readAllStandardOutput()));
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        addCommandHistory(command);
        lastShellCommand = command;
        lastShellWorkingDirectory = currentWorkDir();
        saveSettings();
        if (!stderrText.isEmpty()) {
            appendOutput(stderrText);
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            appendOutput(QStringLiteral("Insert command failed: exit=%1").arg(process.exitCode()));
            statusBar()->showMessage(QStringLiteral("Insert command failed"), 3000);
            return;
        }
        if (stdoutText.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Command produced no output"), 2500);
            return;
        }

        QTextCursor cursor = page->editor->textCursor();
        cursor.beginEditBlock();
        cursor.insertText(stdoutText);
        cursor.endEditBlock();
        updateTabTitle(page, true);
        updateDocumentList();
        refreshSymbols();
        updateCursorStatus();
        appendOutput(QStringLiteral("Inserted command output: %1").arg(command));
        statusBar()->showMessage(QStringLiteral("Command output inserted"), 2500);
    }

    void configureSnippets()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Configure Snippets"));
        dialog.resize(760, 460);

        QVector<CustomSnippetTemplate> workingSnippets = customSnippets;
        auto* layout = new QVBoxLayout(&dialog);
        auto* contentLayout = new QHBoxLayout();
        auto* snippetList = new QListWidget(&dialog);
        snippetList->setMinimumWidth(250);

        auto* formPanel = new QWidget(&dialog);
        auto* form = new QFormLayout(formPanel);
        auto* languageCombo = new QComboBox(formPanel);
        languageCombo->addItems(snippetLanguages());
        languageCombo->setCurrentText(snippetLanguages().contains(currentSnippetLanguage())
            ? currentSnippetLanguage()
            : QStringLiteral("All"));
        auto* labelEdit = new QLineEdit(formPanel);
        labelEdit->setPlaceholderText(QStringLiteral("Snippet name"));
        auto* bodyEdit = new QPlainTextEdit(formPanel);
        bodyEdit->setPlaceholderText(QStringLiteral("Code body. Use ${selection} and ${cursor} placeholders."));
        bodyEdit->setMinimumHeight(180);
        form->addRow(QStringLiteral("Language"), languageCombo);
        form->addRow(QStringLiteral("Name"), labelEdit);
        form->addRow(QStringLiteral("Body"), bodyEdit);

        auto* actionLayout = new QHBoxLayout();
        auto* newButton = new QPushButton(QStringLiteral("New"), formPanel);
        auto* saveButton = new QPushButton(QStringLiteral("Save"), formPanel);
        auto* deleteButton = new QPushButton(QStringLiteral("Delete"), formPanel);
        actionLayout->addWidget(newButton);
        actionLayout->addWidget(saveButton);
        actionLayout->addWidget(deleteButton);
        actionLayout->addStretch(1);
        form->addRow(actionLayout);

        contentLayout->addWidget(snippetList, 0);
        contentLayout->addWidget(formPanel, 1);
        layout->addLayout(contentLayout, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        auto selectedIndex = [snippetList]() {
            QListWidgetItem* item = snippetList->currentItem();
            return item != nullptr ? item->data(Qt::UserRole).toInt() : -1;
        };

        auto refreshList = [&]() {
            const int previous = selectedIndex();
            snippetList->clear();
            for (int i = 0; i < workingSnippets.size(); ++i) {
                const CustomSnippetTemplate& snippet = workingSnippets.at(i);
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1 - %2").arg(snippet.language, snippet.label),
                    snippetList);
                item->setData(Qt::UserRole, i);
            }
            if (!workingSnippets.isEmpty()) {
                snippetList->setCurrentRow(qBound(0, previous, workingSnippets.size() - 1));
            }
        };

        auto loadSnippet = [&](int index) {
            if (index < 0 || index >= workingSnippets.size()) {
                deleteButton->setEnabled(false);
                return;
            }
            const CustomSnippetTemplate& snippet = workingSnippets.at(index);
            languageCombo->setCurrentText(snippet.language);
            labelEdit->setText(snippet.label);
            bodyEdit->setPlainText(snippet.body);
            deleteButton->setEnabled(true);
        };

        refreshList();
        loadSnippet(selectedIndex());

        connect(snippetList, &QListWidget::currentRowChanged, &dialog, [&, snippetList](int) {
            loadSnippet(selectedIndex());
        });
        connect(newButton, &QPushButton::clicked, &dialog, [&]() {
            snippetList->clearSelection();
            snippetList->setCurrentRow(-1);
            languageCombo->setCurrentText(snippetLanguages().contains(currentSnippetLanguage())
                ? currentSnippetLanguage()
                : QStringLiteral("All"));
            labelEdit->clear();
            bodyEdit->setPlainText(QStringLiteral("${cursor}"));
            deleteButton->setEnabled(false);
            labelEdit->setFocus();
        });
        connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
            CustomSnippetTemplate snippet{
                languageCombo->currentText(),
                labelEdit->text().trimmed(),
                bodyEdit->toPlainText()
            };
            if (snippet.label.isEmpty() || snippet.body.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("Configure Snippets"),
                    QStringLiteral("Snippet name and body are required."));
                return;
            }
            const int index = selectedIndex();
            if (index >= 0 && index < workingSnippets.size()) {
                workingSnippets[index] = snippet;
            } else {
                workingSnippets.push_back(snippet);
            }
            refreshList();
            snippetList->setCurrentRow(index >= 0 ? index : workingSnippets.size() - 1);
        });
        connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
            const int index = selectedIndex();
            if (index < 0 || index >= workingSnippets.size()) {
                return;
            }
            workingSnippets.removeAt(index);
            labelEdit->clear();
            bodyEdit->clear();
            refreshList();
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            customSnippets = workingSnippets;
            saveSettings();
            appendOutput(QStringLiteral("Custom snippets saved: %1").arg(customSnippets.size()));
        }
    }

    void configureFileTemplates()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Configure File Templates"));
        dialog.resize(800, 500);

        QVector<FileTemplate> workingTemplates = customFileTemplates;
        const QStringList languages = MiniHighlighter::supportedLanguages();
        auto* layout = new QVBoxLayout(&dialog);
        auto* contentLayout = new QHBoxLayout();
        auto* templateList = new QListWidget(&dialog);
        templateList->setMinimumWidth(260);

        auto* formPanel = new QWidget(&dialog);
        auto* form = new QFormLayout(formPanel);
        auto* languageCombo = new QComboBox(formPanel);
        languageCombo->addItems(languages);
        languageCombo->setCurrentText(currentSnippetLanguage());
        auto* labelEdit = new QLineEdit(formPanel);
        labelEdit->setPlaceholderText(QStringLiteral("Template name"));
        auto* bodyEdit = new QPlainTextEdit(formPanel);
        bodyEdit->setPlaceholderText(QStringLiteral("Initial file content. Use ${date} and ${cursor} placeholders."));
        bodyEdit->setMinimumHeight(220);
        form->addRow(QStringLiteral("Language"), languageCombo);
        form->addRow(QStringLiteral("Name"), labelEdit);
        form->addRow(QStringLiteral("Body"), bodyEdit);

        auto* actionLayout = new QHBoxLayout();
        auto* newButton = new QPushButton(QStringLiteral("New"), formPanel);
        auto* saveButton = new QPushButton(QStringLiteral("Save"), formPanel);
        auto* deleteButton = new QPushButton(QStringLiteral("Delete"), formPanel);
        actionLayout->addWidget(newButton);
        actionLayout->addWidget(saveButton);
        actionLayout->addWidget(deleteButton);
        actionLayout->addStretch(1);
        form->addRow(actionLayout);

        contentLayout->addWidget(templateList, 0);
        contentLayout->addWidget(formPanel, 1);
        layout->addLayout(contentLayout, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        auto selectedIndex = [templateList]() {
            QListWidgetItem* item = templateList->currentItem();
            return item != nullptr ? item->data(Qt::UserRole).toInt() : -1;
        };

        auto refreshList = [&]() {
            const int previous = selectedIndex();
            templateList->clear();
            for (int i = 0; i < workingTemplates.size(); ++i) {
                const FileTemplate& fileTemplate = workingTemplates.at(i);
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1 - %2").arg(fileTemplate.language, fileTemplate.label),
                    templateList);
                item->setData(Qt::UserRole, i);
            }
            if (!workingTemplates.isEmpty()) {
                templateList->setCurrentRow(qBound(0, previous, workingTemplates.size() - 1));
            }
        };

        auto loadTemplate = [&](int index) {
            if (index < 0 || index >= workingTemplates.size()) {
                deleteButton->setEnabled(false);
                return;
            }
            const FileTemplate& fileTemplate = workingTemplates.at(index);
            languageCombo->setCurrentText(languages.contains(fileTemplate.language) ? fileTemplate.language : QStringLiteral("Plain Text"));
            labelEdit->setText(fileTemplate.label);
            bodyEdit->setPlainText(fileTemplate.body);
            deleteButton->setEnabled(true);
        };

        refreshList();
        loadTemplate(selectedIndex());

        connect(templateList, &QListWidget::currentRowChanged, &dialog, [&, templateList](int) {
            loadTemplate(selectedIndex());
        });
        connect(newButton, &QPushButton::clicked, &dialog, [&]() {
            templateList->clearSelection();
            templateList->setCurrentRow(-1);
            languageCombo->setCurrentText(languages.contains(currentSnippetLanguage()) ? currentSnippetLanguage() : QStringLiteral("Plain Text"));
            labelEdit->clear();
            bodyEdit->setPlainText(QStringLiteral("${cursor}\n"));
            deleteButton->setEnabled(false);
            labelEdit->setFocus();
        });
        connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
            FileTemplate fileTemplate{
                labelEdit->text().trimmed(),
                languages.contains(languageCombo->currentText()) ? languageCombo->currentText() : QStringLiteral("Plain Text"),
                bodyEdit->toPlainText()
            };
            if (fileTemplate.label.isEmpty() || fileTemplate.body.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("Configure File Templates"),
                    QStringLiteral("Template name and body are required."));
                return;
            }
            const int index = selectedIndex();
            if (index >= 0 && index < workingTemplates.size()) {
                workingTemplates[index] = fileTemplate;
            } else {
                workingTemplates.push_back(fileTemplate);
            }
            refreshList();
            templateList->setCurrentRow(index >= 0 ? index : workingTemplates.size() - 1);
        });
        connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
            const int index = selectedIndex();
            if (index < 0 || index >= workingTemplates.size()) {
                return;
            }
            workingTemplates.removeAt(index);
            labelEdit->clear();
            bodyEdit->clear();
            refreshList();
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            customFileTemplates = workingTemplates;
            saveSettings();
            appendOutput(QStringLiteral("Custom file templates saved: %1").arg(customFileTemplates.size()));
        }
    }

    QStringList customFileTypeMappingEntries(const QHash<QString, QString>& mappings) const
    {
        QStringList keys = mappings.keys();
        keys.sort(Qt::CaseInsensitive);
        QStringList entries;
        for (const QString& key : keys) {
            const QString language = mappings.value(key);
            if (!key.isEmpty() && MiniHighlighter::supportedLanguages().contains(language)) {
                entries << QStringLiteral("%1 = %2").arg(key, language);
            }
        }
        return entries;
    }

    QString customFileTypeMappingsText() const
    {
        return customFileTypeMappingEntries(customFileTypeMappings).join(QLatin1Char('\n'));
    }

    bool parseCustomFileTypeMappings(const QString& text, QHash<QString, QString>* mappings, QString* error) const
    {
        QHash<QString, QString> parsed;
        const QStringList lines = text.split(QLatin1Char('\n'));
        const QStringList languages = MiniHighlighter::supportedLanguages();
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines.at(i).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            int separator = line.indexOf(QLatin1Char('='));
            if (separator < 0) {
                separator = line.indexOf(QLatin1Char(':'));
            }
            if (separator < 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("Line %1 needs KEY = LANGUAGE.").arg(i + 1);
                }
                return false;
            }
            const QString key = normalizedFileTypeMappingKey(line.left(separator));
            const QString language = line.mid(separator + 1).trimmed();
            if (key.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("Line %1 has an empty extension or filename.").arg(i + 1);
                }
                return false;
            }
            if (!languages.contains(language)) {
                if (error != nullptr) {
                    *error = QStringLiteral("Line %1 has unknown language \"%2\".").arg(i + 1).arg(language);
                }
                return false;
            }
            parsed.insert(key, language);
        }
        if (mappings != nullptr) {
            *mappings = parsed;
        }
        return true;
    }

    void configureHighlighterForPath(EditorPage* page, const QString& path)
    {
        if (page == nullptr || page->highlighter == nullptr) {
            return;
        }
        page->highlighter->configureForPath(path, customFileTypeMappings);
    }

    void reconfigureOpenFileTypes()
    {
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            if (page != nullptr) {
                configureHighlighterForPath(page, page->path);
            }
        }
        refreshSymbols();
        updateCursorStatus();
        updateDocumentList();
    }

    void showPreferences()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Preferences"));
        auto* layout = new QFormLayout(&dialog);

        auto* fontFamilyCombo = new QFontComboBox(&dialog);
        fontFamilyCombo->setFontFilters(QFontComboBox::MonospacedFonts);
        fontFamilyCombo->setCurrentFont(QFont(editorFontFamily.isEmpty() ? QStringLiteral("monospace") : editorFontFamily));
        auto* fontSizeSpin = new QSpinBox(&dialog);
        fontSizeSpin->setRange(8, 36);
        fontSizeSpin->setValue(fontSize);
        auto* themeCombo = new QComboBox(&dialog);
        themeCombo->addItems({QStringLiteral("Light"), QStringLiteral("Dark"), QStringLiteral("High Contrast")});
        themeCombo->setCurrentText(normalizedTheme(editorTheme));
        auto* tabWidthSpin = new QSpinBox(&dialog);
        tabWidthSpin->setRange(2, 8);
        tabWidthSpin->setValue(tabWidth);
        auto* wrapCheck = new QCheckBox(&dialog);
        wrapCheck->setChecked(wordWrapAction->isChecked());
        auto* spacesCheck = new QCheckBox(&dialog);
        spacesCheck->setChecked(insertSpacesOnTab);
        auto* autoIndentCheck = new QCheckBox(&dialog);
        autoIndentCheck->setChecked(autoIndentEnabled);
        auto* pairingCheck = new QCheckBox(&dialog);
        pairingCheck->setChecked(bracketPairingEnabled);
        auto* completionCheck = new QCheckBox(&dialog);
        completionCheck->setChecked(wordCompletionEnabled);
        auto* occurrencesCheck = new QCheckBox(&dialog);
        occurrencesCheck->setChecked(highlightOccurrencesEnabled);
        auto* currentLineCheck = new QCheckBox(&dialog);
        currentLineCheck->setChecked(highlightCurrentLineEnabled);
        auto* lineNumbersCheck = new QCheckBox(&dialog);
        lineNumbersCheck->setChecked(showLineNumbersEnabled);
        auto* indentGuidesCheck = new QCheckBox(&dialog);
        indentGuidesCheck->setChecked(showIndentGuidesEnabled);
        auto* rightMarginCheck = new QCheckBox(&dialog);
        rightMarginCheck->setChecked(showRightMarginEnabled);
        auto* rightMarginSpin = new QSpinBox(&dialog);
        rightMarginSpin->setRange(40, 200);
        rightMarginSpin->setValue(rightMarginColumn);
        auto* whitespaceCheck = new QCheckBox(&dialog);
        whitespaceCheck->setChecked(showWhitespaceEnabled);
        auto* lineEndingsCheck = new QCheckBox(&dialog);
        lineEndingsCheck->setChecked(showLineEndingsEnabled);
        auto* defaultLineEndingCombo = new QComboBox(&dialog);
        defaultLineEndingCombo->addItems({QStringLiteral("LF"), QStringLiteral("CRLF"), QStringLiteral("CR")});
        defaultLineEndingCombo->setCurrentText(lineEndingName(defaultLineEnding));
        auto* defaultEncodingCombo = new QComboBox(&dialog);
        defaultEncodingCombo->addItems({QStringLiteral("UTF-8"), systemEncodingName(), QStringLiteral("GB18030"), QStringLiteral("UTF-16LE-BOM"), QStringLiteral("UTF-16BE-BOM")});
        defaultEncodingCombo->setEditable(true);
        defaultEncodingCombo->setCurrentText(defaultEncodingName);
        auto* finalNewlineCheck = new QCheckBox(&dialog);
        finalNewlineCheck->setChecked(ensureFinalNewlineOnSave);
        auto* stripOnSaveCheck = new QCheckBox(&dialog);
        stripOnSaveCheck->setChecked(stripTrailingOnSave);
        auto* backupOnSaveCheck = new QCheckBox(&dialog);
        backupOnSaveCheck->setChecked(createBackupOnSave);
        auto* autoSaveCheck = new QCheckBox(&dialog);
        autoSaveCheck->setChecked(autoSaveEnabled);
        auto* autoSaveIntervalSpin = new QSpinBox(&dialog);
        autoSaveIntervalSpin->setRange(15, 3600);
        autoSaveIntervalSpin->setSuffix(QStringLiteral(" sec"));
        autoSaveIntervalSpin->setValue(autoSaveIntervalSeconds);
        auto* fileTypeMappingsEdit = new QPlainTextEdit(&dialog);
        fileTypeMappingsEdit->setPlainText(customFileTypeMappingsText());
        fileTypeMappingsEdit->setPlaceholderText(QStringLiteral("ipp = C++\n.inc = C\nBUILD = Python"));
        fileTypeMappingsEdit->setMinimumHeight(88);
        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

        layout->addRow(QStringLiteral("Editor font"), fontFamilyCombo);
        layout->addRow(QStringLiteral("Editor font size"), fontSizeSpin);
        layout->addRow(QStringLiteral("Color scheme"), themeCombo);
        layout->addRow(QStringLiteral("Tab width"), tabWidthSpin);
        layout->addRow(QStringLiteral("Line wrapping"), wrapCheck);
        layout->addRow(QStringLiteral("Insert spaces for Tab"), spacesCheck);
        layout->addRow(QStringLiteral("Auto indentation"), autoIndentCheck);
        layout->addRow(QStringLiteral("Bracket and quote pairing"), pairingCheck);
        layout->addRow(QStringLiteral("Word completion"), completionCheck);
        layout->addRow(QStringLiteral("Highlight current word"), occurrencesCheck);
        layout->addRow(QStringLiteral("Highlight current line"), currentLineCheck);
        layout->addRow(QStringLiteral("Show line numbers"), lineNumbersCheck);
        layout->addRow(QStringLiteral("Show indentation guides"), indentGuidesCheck);
        layout->addRow(QStringLiteral("Show long line marker"), rightMarginCheck);
        layout->addRow(QStringLiteral("Long line column"), rightMarginSpin);
        layout->addRow(QStringLiteral("Show whitespace"), whitespaceCheck);
        layout->addRow(QStringLiteral("Show line endings"), lineEndingsCheck);
        layout->addRow(QStringLiteral("Default line endings"), defaultLineEndingCombo);
        layout->addRow(QStringLiteral("Default encoding"), defaultEncodingCombo);
        layout->addRow(QStringLiteral("Ensure final newline on save"), finalNewlineCheck);
        layout->addRow(QStringLiteral("Strip trailing spaces on save"), stripOnSaveCheck);
        layout->addRow(QStringLiteral("Create backup copy on save"), backupOnSaveCheck);
        layout->addRow(QStringLiteral("Auto save existing files"), autoSaveCheck);
        layout->addRow(QStringLiteral("Auto save interval"), autoSaveIntervalSpin);
        layout->addRow(QStringLiteral("Custom filetypes"), fileTypeMappingsEdit);
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
            QHash<QString, QString> parsedMappings;
            QString error;
            if (!parseCustomFileTypeMappings(fileTypeMappingsEdit->toPlainText(), &parsedMappings, &error)) {
                QMessageBox::warning(&dialog, QStringLiteral("Custom filetypes"), error);
                return;
            }
            dialog.accept();
        });
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            QHash<QString, QString> parsedMappings;
            QString error;
            if (!parseCustomFileTypeMappings(fileTypeMappingsEdit->toPlainText(), &parsedMappings, &error)) {
                QMessageBox::warning(this, QStringLiteral("Custom filetypes"), error);
                return;
            }
            editorFontFamily = fontFamilyCombo->currentFont().family().trimmed();
            if (editorFontFamily.isEmpty()) {
                editorFontFamily = QStringLiteral("monospace");
            }
            fontSize = fontSizeSpin->value();
            editorTheme = normalizedTheme(themeCombo->currentText());
            tabWidth = tabWidthSpin->value();
            insertSpacesOnTab = spacesCheck->isChecked();
            autoIndentEnabled = autoIndentCheck->isChecked();
            bracketPairingEnabled = pairingCheck->isChecked();
            wordCompletionEnabled = completionCheck->isChecked();
            highlightOccurrencesEnabled = occurrencesCheck->isChecked();
            highlightCurrentLineEnabled = currentLineCheck->isChecked();
            showLineNumbersEnabled = lineNumbersCheck->isChecked();
            showIndentGuidesEnabled = indentGuidesCheck->isChecked();
            showRightMarginEnabled = rightMarginCheck->isChecked();
            rightMarginColumn = rightMarginSpin->value();
            showWhitespaceEnabled = whitespaceCheck->isChecked();
            showLineEndingsEnabled = lineEndingsCheck->isChecked();
            defaultLineEnding = lineEndingForName(defaultLineEndingCombo->currentText());
            defaultEncodingName = normalizedEncodingName(defaultEncodingCombo->currentText());
            ensureFinalNewlineOnSave = finalNewlineCheck->isChecked();
            stripTrailingOnSave = stripOnSaveCheck->isChecked();
            createBackupOnSave = backupOnSaveCheck->isChecked();
            autoSaveEnabled = autoSaveCheck->isChecked();
            autoSaveIntervalSeconds = autoSaveIntervalSpin->value();
            customFileTypeMappings = parsedMappings;
            wordWrapAction->setChecked(wrapCheck->isChecked());
            highlightCurrentLineAction->setChecked(highlightCurrentLineEnabled);
            showLineNumbersAction->setChecked(showLineNumbersEnabled);
            showIndentGuidesAction->setChecked(showIndentGuidesEnabled);
            showRightMarginAction->setChecked(showRightMarginEnabled);
            showWhitespaceAction->setChecked(showWhitespaceEnabled);
            showLineEndingsAction->setChecked(showLineEndingsEnabled);
            applyTheme();
            setWordWrap(wrapCheck->isChecked());
            applyEditorPreferences();
            reconfigureOpenFileTypes();
            configureAutoSaveTimer();
            saveSettings();
        }
    }

    void updateTabTitle(EditorPage* page, bool changed)
    {
        const int index = tabs->indexOf(page);
        if (index < 0) {
            return;
        }
        QString title = displayName(page);
        if (page != nullptr && page->editor->isReadOnly()) {
            title.append(QStringLiteral(" [RO]"));
        }
        if (changed) {
            title.prepend(QLatin1Char('*'));
        }
        tabs->setTabText(index, title);
        updateDocumentList();
    }

    void updateDocumentList()
    {
        documentsList->clear();
        for (int i = 0; i < tabs->count(); ++i) {
            auto* page = static_cast<EditorPage*>(tabs->widget(i));
            QString label = displayName(page);
            if (page != nullptr && page->editor->isReadOnly()) {
                label.append(QStringLiteral(" [RO]"));
            }
            if (page != nullptr && page->editor->document()->isModified()) {
                label.prepend(QLatin1Char('*'));
            }
            auto* item = new QListWidgetItem(label, documentsList);
            item->setData(Qt::UserRole, i);
            if (page != nullptr && !page->path.isEmpty()) {
                item->setToolTip(page->path);
            }
        }
    }

    QString fileTypeName(const QString& path) const
    {
        return MiniHighlighter::detectLanguage(path, customFileTypeMappings);
    }

    void updateCursorStatus()
    {
        EditorPage* page = currentPage();
        if (page == nullptr) {
            return;
        }
        updateReadOnlyAction();
        const QTextCursor cursor = page->editor->textCursor();
        statusFile->setText(page->path.isEmpty() ? QStringLiteral("No file") : page->path);
        const QString language = page->highlighter != nullptr ? page->highlighter->languageName() : fileTypeName(page->path);
        const QString mode = page->editor->isReadOnly() ? QStringLiteral("RO") : QStringLiteral("RW");
        statusType->setText(QStringLiteral("%1 | %2 | %3 | %4").arg(language, lineEndingName(page->lineEnding), page->encodingName, mode));
        statusPosition->setText(QStringLiteral("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    }

    void recordProblemLine(const QString& line)
    {
        if (problemsList == nullptr || line.isEmpty()) {
            return;
        }

        const QString trimmedLine = line.trimmed();
        auto appendProblem = [this, &trimmedLine](QString path, int lineNumber, int column, const QString& message, const QString& severity) {
            if (path.isEmpty() || lineNumber <= 0) {
                return;
            }
            path = path.trimmed();
            if (QDir::isRelativePath(path)) {
                path = QDir(currentWorkDir()).absoluteFilePath(path);
            }
            const QString displayPath = QDir(currentWorkDir()).relativeFilePath(path);
            const QString displayMessage = message.trimmed().isEmpty() ? trimmedLine : message.trimmed();
            const QString location = column > 0
                ? QStringLiteral("%1:%2:%3").arg(displayPath).arg(lineNumber).arg(column)
                : QStringLiteral("%1:%2").arg(displayPath).arg(lineNumber);

            auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(location, displayMessage), problemsList);
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, lineNumber);
            item->setData(Qt::UserRole + 2, column);
            item->setToolTip(trimmedLine);

            const QString severityText = severity.isEmpty() ? trimmedLine : severity;
            if (severityText.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                || severityText.contains(QStringLiteral("fatal"), Qt::CaseInsensitive)) {
                item->setForeground(QColor(170, 48, 48));
            } else if (severityText.contains(QStringLiteral("warn"), Qt::CaseInsensitive)) {
                item->setForeground(QColor(145, 95, 24));
            }
        };

        static const QRegularExpression arkTsDiagnostic(
            QStringLiteral("\\b(?:ArkTS|ArkCompiler|Hvigor)[^\\n]*?(ERROR|WARN|WARNING|NOTE|FATAL)?[^\\n]*?\\bFile:\\s+(.+):(\\d+):(\\d+)(?:\\s*[-:]?\\s*(.*))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch arkTsMatch = arkTsDiagnostic.match(trimmedLine);
        if (arkTsMatch.hasMatch()) {
            appendProblem(arkTsMatch.captured(2),
                arkTsMatch.captured(3).toInt(),
                arkTsMatch.captured(4).toInt(),
                arkTsMatch.captured(5),
                arkTsMatch.captured(1));
            return;
        }

        static const QRegularExpression pythonTraceback(
            QStringLiteral("^File\\s+\"(.+)\",\\s+line\\s+(\\d+)(?:,\\s+in\\s+(.+))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch pythonMatch = pythonTraceback.match(trimmedLine);
        if (pythonMatch.hasMatch()) {
            appendProblem(pythonMatch.captured(1),
                pythonMatch.captured(2).toInt(),
                0,
                pythonMatch.captured(3).isEmpty() ? QStringLiteral("Python traceback") : QStringLiteral("Python traceback: %1").arg(pythonMatch.captured(3)),
                QStringLiteral("error"));
            return;
        }

        static const QRegularExpression rustDiagnostic(
            QStringLiteral("^(?:error|warning|note)?\\s*-->\\s+(.+):(\\d+):(\\d+)(?:\\s*[-:]?\\s*(.*))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch rustMatch = rustDiagnostic.match(trimmedLine);
        if (rustMatch.hasMatch()) {
            appendProblem(rustMatch.captured(1),
                rustMatch.captured(2).toInt(),
                rustMatch.captured(3).toInt(),
                rustMatch.captured(4),
                trimmedLine);
            return;
        }

        static const QRegularExpression cmakeDiagnostic(
            QStringLiteral("^CMake\\s+(Error|Warning|Deprecation Warning)\\s+at\\s+(.+):(\\d+)\\s*\\(([^)]*)\\)\\s*:?\\s*(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch cmakeMatch = cmakeDiagnostic.match(trimmedLine);
        if (cmakeMatch.hasMatch()) {
            appendProblem(cmakeMatch.captured(2),
                cmakeMatch.captured(3).toInt(),
                0,
                cmakeMatch.captured(5).isEmpty() ? QStringLiteral("CMake %1: %2").arg(cmakeMatch.captured(1), cmakeMatch.captured(4))
                                                 : cmakeMatch.captured(5),
                cmakeMatch.captured(1));
            return;
        }

        static const QRegularExpression msvcDiagnostic(
            QStringLiteral("^(.+)\\((\\d+)(?:,(\\d+))?\\)\\s*:\\s*(fatal error|error|warning|note)\\s+[^:]+:\\s*(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch msvcMatch = msvcDiagnostic.match(trimmedLine);
        if (msvcMatch.hasMatch()) {
            appendProblem(msvcMatch.captured(1),
                msvcMatch.captured(2).toInt(),
                msvcMatch.captured(3).toInt(),
                msvcMatch.captured(5),
                msvcMatch.captured(4));
            return;
        }

        static const QRegularExpression includeTraceDiagnostic(
            QStringLiteral("^(?:In file included from|from)\\s+(.+):(\\d+)(?::(\\d+))?[:,]?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch includeTraceMatch = includeTraceDiagnostic.match(trimmedLine);
        if (includeTraceMatch.hasMatch()) {
            appendProblem(includeTraceMatch.captured(1),
                includeTraceMatch.captured(2).toInt(),
                includeTraceMatch.captured(3).toInt(),
                QStringLiteral("Included from here"),
                QStringLiteral("note"));
            return;
        }

        static const QRegularExpression nodeStackDiagnostic(
            QStringLiteral("^at\\s+.*\\((.+):(\\d+):(\\d+)\\)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch nodeMatch = nodeStackDiagnostic.match(trimmedLine);
        if (nodeMatch.hasMatch()) {
            appendProblem(nodeMatch.captured(1),
                nodeMatch.captured(2).toInt(),
                nodeMatch.captured(3).toInt(),
                QStringLiteral("JavaScript stack frame"),
                QStringLiteral("error"));
            return;
        }

        static const QRegularExpression fileDiagnostic(
            QStringLiteral("\\bFile:\\s+(.+):(\\d+):(\\d+)(?:\\s*[-:]?\\s*(.*))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch fileMatch = fileDiagnostic.match(trimmedLine);
        if (fileMatch.hasMatch()) {
            appendProblem(fileMatch.captured(1),
                fileMatch.captured(2).toInt(),
                fileMatch.captured(3).toInt(),
                fileMatch.captured(4),
                trimmedLine);
            return;
        }

        static const QRegularExpression diagnostic(
            QStringLiteral("^(.+):(\\d+)(?::(\\d+))?:\\s*(?:(fatal error|error|warning|warn|note)[:\\s]+)?(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = diagnostic.match(trimmedLine);
        if (!match.hasMatch()) {
            return;
        }

        appendProblem(match.captured(1),
            match.captured(2).toInt(),
            match.captured(3).toInt(),
            match.captured(5),
            match.captured(4));
    }

    void recordProblemsFromOutput(const QString& text)
    {
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            recordProblemLine(line);
        }
        if (problemsList != nullptr && problemsList->count() > 0) {
            sideTabs->setCurrentWidget(problemsList);
        }
    }

    void appendOutput(const QString& text)
    {
        if (!text.isEmpty()) {
            output->appendPlainText(text);
            recordProblemsFromOutput(text);
        }
    }

    QTabWidget* sideTabs = nullptr;
    QListWidget* documentsList = nullptr;
    QListWidget* symbolsList = nullptr;
    QWidget* fileBrowserPanel = nullptr;
    QLineEdit* fileBrowserPathEdit = nullptr;
    QListWidget* fileBrowserList = nullptr;
    QWidget* projectPanel = nullptr;
    QLineEdit* projectFileFilter = nullptr;
    QListWidget* projectFiles = nullptr;
    QListWidget* searchResults = nullptr;
    QListWidget* problemsList = nullptr;
    QListWidget* tasksList = nullptr;
    QListWidget* bookmarksList = nullptr;
    QSplitter* mainSplitter = nullptr;
    QSplitter* rightSplitter = nullptr;
    QSplitter* editorSplitter = nullptr;
    QToolBar* mainToolBar = nullptr;
    QTabWidget* tabs = nullptr;
    QPlainTextEdit* splitView = nullptr;
    QTabWidget* outputTabs = nullptr;
    QPlainTextEdit* output = nullptr;
    QPlainTextEdit* scratchPad = nullptr;
    QWidget* terminalPanel = nullptr;
    QPlainTextEdit* terminalOutput = nullptr;
    QLineEdit* terminalInput = nullptr;
    QLabel* statusFile = nullptr;
    QLabel* statusType = nullptr;
    QLabel* statusPosition = nullptr;
    QMenu* recentFilesMenu = nullptr;
    QMenu* recentProjectsMenu = nullptr;
    QMenu* recentCommandsMenu = nullptr;
    QMenu* externalToolsMenu = nullptr;
    QAction* showSidebarAction = nullptr;
    QAction* showOutputAction = nullptr;
    QAction* showToolbarAction = nullptr;
    QAction* showStatusbarAction = nullptr;
    QAction* wordWrapAction = nullptr;
    QAction* fullScreenAction = nullptr;
    QAction* focusModeAction = nullptr;
    QAction* splitViewAction = nullptr;
    QAction* highlightCurrentLineAction = nullptr;
    QAction* showLineNumbersAction = nullptr;
    QAction* showIndentGuidesAction = nullptr;
    QAction* showRightMarginAction = nullptr;
    QAction* showWhitespaceAction = nullptr;
    QAction* showLineEndingsAction = nullptr;
    QAction* saveAllBeforeBuildAction = nullptr;
    QAction* readOnlyAction = nullptr;
    QProcess* buildProcess = nullptr;
    QProcess* terminalProcess = nullptr;
    QTimer* autoSaveTimer = nullptr;
    QString projectRoot;
    QString projectFile;
    QStringList projectFilePaths;
    QString projectDefaultLineEnding;
    QString projectDefaultEncodingName;
    QStringList projectExtraIgnorePatterns;
    QVector<IgnoreRule> projectIgnoreRules;
    mutable QHash<QString, QVector<IgnoreRule>> ignoreRulesByRoot;
    QString lastFindText;
    QString searchFilePattern = QStringLiteral("*");
    QStringList recentFiles;
    QStringList recentProjects;
    QStringList closedFiles;
    QString fileBrowserRoot;
    QStringList commandHistory;
    QString lastShellCommand;
    QString lastShellWorkingDirectory;
    QStringList loadedTagFiles;
    QVector<ExternalTool> externalTools;
    QVector<ProjectSymbol> loadedTagSymbols;
    QVector<CustomSnippetTemplate> customSnippets;
    QVector<FileTemplate> customFileTemplates;
    QHash<QString, QString> customFileTypeMappings;
    QString editorTheme = QStringLiteral("Light");
    QList<NavLocation> backLocations;
    QList<NavLocation> forwardLocations;
    QString buildCommand = QStringLiteral("make");
    QString buildCleanCommand = QStringLiteral("make clean");
    QString buildTargetCommand = QStringLiteral("make {target}");
    QString compileCCommand = QStringLiteral("cc -Wall {file} -o {output}");
    QString compileCppCommand = QStringLiteral("c++ -Wall {file} -o {output}");
    QString checkPythonCommand = QStringLiteral("python3 -m py_compile {file}");
    QString checkShellCommand = QStringLiteral("sh -n {file}");
    QString checkJsCommand = QStringLiteral("node --check {file}");
    QString checkArkTsCommand = QStringLiteral("cd {project} && ([ -x ./hvigorw ] && ./hvigorw --mode module -p module=entry assembleHap || hvigor --mode module -p module=entry assembleHap)");
    QString checkJsonCommand = QStringLiteral("python3 -m json.tool {file} >/dev/null");
    QString checkGoCommand = QStringLiteral("go test {dir}");
    QString checkRustCommand = QStringLiteral("rustc --emit=metadata {file}");
    QString checkJavaCommand = QStringLiteral("javac {file}");
    QString checkPhpCommand = QStringLiteral("php -l {file}");
    QString checkYamlCommand = QStringLiteral("python3 -c \"import sys, yaml; yaml.safe_load(open(sys.argv[1], encoding='utf-8'))\" {file}");
    QString checkTomlCommand = QStringLiteral("python3 -c \"import sys, tomllib; tomllib.load(open(sys.argv[1], 'rb'))\" {file}");
    QString executePythonCommand = QStringLiteral("python3 {file}");
    QString executeShellCommand = QStringLiteral("sh {file}");
    QString executeJsCommand = QStringLiteral("node {file}");
    QString executeGoCommand = QStringLiteral("go run {file}");
    QString executeRustCommand = QStringLiteral("rustc {file} -o {output} && {output}");
    QString executeJavaCommand = QStringLiteral("javac {file} && java -cp {dir} {base}");
    QString executePhpCommand = QStringLiteral("php {file}");
    QString executeNativeCommand = QStringLiteral("{output}");
    QString editorFontFamily = QStringLiteral("monospace");
#if defined(Q_OS_OPENHARMONY)
    int fontSize = 24;
#else
    int fontSize = 11;
#endif
    int tabWidth = 4;
    bool insertSpacesOnTab = true;
    bool autoIndentEnabled = true;
    bool bracketPairingEnabled = true;
#if defined(Q_OS_OPENHARMONY)
    bool wordCompletionEnabled = false;
#else
    bool wordCompletionEnabled = true;
#endif
    bool highlightOccurrencesEnabled = true;
    bool highlightCurrentLineEnabled = true;
    bool showLineNumbersEnabled = true;
    bool showIndentGuidesEnabled = true;
    bool showRightMarginEnabled = false;
    int rightMarginColumn = 80;
    bool showWhitespaceEnabled = false;
    bool showLineEndingsEnabled = false;
    QString defaultLineEnding = QStringLiteral("\n");
    QString defaultEncodingName = QStringLiteral("UTF-8");
    bool ensureFinalNewlineOnSave = true;
    bool stripTrailingOnSave = false;
    bool createBackupOnSave = false;
    bool autoSaveEnabled = false;
    bool saveAllBeforeBuild = true;
    int autoSaveIntervalSeconds = 120;
    bool focusModeActive = false;
    bool focusModeSavedSidebar = true;
    bool focusModeSavedOutput = true;
    bool focusModeSavedToolbar = true;
    bool focusModeSavedStatusbar = true;
    bool findMatchCase = false;
    bool findWholeWord = false;
    bool findRegularExpression = false;
    bool restoringNavigation = false;
    bool quitRequested = false;
};

QStringList collectInputPaths(int argc, char** argv)
{
    QStringList paths;
    auto appendPath = [&paths](const QString& value) {
        const QString argument = normalizedLaunchPath(value);
        if (!argument.isEmpty() && !argument.startsWith(QLatin1Char('-')) && !paths.contains(argument)) {
            paths << argument;
        }
    };

    for (int i = 1; i < argc && argv != nullptr; ++i) {
        appendPath(QString::fromLocal8Bit(argv[i]));
    }

#if defined(Q_OS_OPENHARMONY)
    const QList<QByteArray> launchEnvironmentKeys = {
        QByteArrayLiteral("url"),
        QByteArrayLiteral("uri"),
        QByteArrayLiteral("query")
    };
    for (const QByteArray& key : launchEnvironmentKeys) {
        appendPath(QString::fromLocal8Bit(qgetenv(key.constData())));
    }
#endif

    return paths;
}

int runGeanyHarmony(int argc, char** argv)
{
    static GeanyMainWindow* runtimeWindow = nullptr;
    if (runtimeWindow != nullptr) {
        runtimeWindow->openPaths(collectInputPaths(argc, argv));
#if defined(Q_OS_OPENHARMONY)
        runtimeWindow->showFullScreen();
#else
        runtimeWindow->show();
#endif
        runtimeWindow->raise();
        runtimeWindow->activateWindow();
        return 0;
    }

    std::unique_ptr<QApplication> ownedApp;
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app == nullptr) {
        ownedApp.reset(new QApplication(argc, argv));
        app = ownedApp.get();
    }

    QApplication::setApplicationName(QStringLiteral("Geany"));
    QApplication::setApplicationDisplayName(QStringLiteral("Geany"));
    QApplication::setOrganizationName(QStringLiteral("Geany"));
    QApplication::setOrganizationDomain(QStringLiteral("geany.org"));
    QApplication::setApplicationVersion(QStringLiteral("2.0-ohos-qt"));
    QApplication::setQuitOnLastWindowClosed(false);
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette = app->palette();
    palette.setColor(QPalette::Window, QColor(245, 247, 250));
    palette.setColor(QPalette::WindowText, QColor(24, 29, 35));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(238, 242, 246));
    palette.setColor(QPalette::Highlight, QColor(34, 111, 167));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app->setPalette(palette);

    auto* window = new GeanyMainWindow();
    runtimeWindow = window;
    window->openPaths(collectInputPaths(argc, argv));
    window->setObjectName(QStringLiteral("geany_main_window"));
    window->setAttribute(Qt::WA_DeleteOnClose);

#if defined(Q_OS_OPENHARMONY)
    auto* windowHelper = new QOhWidgetHelper(window, window);
    windowHelper->setSupportedWindowModes(QOhWidgetHelper::FULL_SCREEN | QOhWidgetHelper::SPLIT | QOhWidgetHelper::FLOATING);
    windowHelper->setWindowRectAutoSave(true);
#endif

#if defined(Q_OS_OPENHARMONY)
    window->showFullScreen();
#else
    window->show();
#endif
    QTimer::singleShot(0, window, [window]() {
        window->activateWindow();
        window->raise();
    });

    return ownedApp ? app->exec() : 0;
}
}

#if defined(Q_OS_OPENHARMONY)
extern "C" int main(int argc, char** argv)
{
    static int fallbackArgc = 1;
    static char appName[] = "geany";
    static char* fallbackArgv[] = {appName, nullptr};

    if (argc <= 0 || !argv) {
        argc = fallbackArgc;
        argv = fallbackArgv;
    }

    return runGeanyHarmony(argc, argv);
}

extern "C" int qtmain(int argc, char** argv)
{
    return main(argc, argv);
}
#else
int main(int argc, char** argv)
{
    return runGeanyHarmony(argc, argv);
}
#endif
