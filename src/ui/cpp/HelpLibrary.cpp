#include "HelpLibrary.h"

#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

struct Topic
{
    const char* id;
    const char* group;
};

// The guide's pages in reading order (docs/help/<id>.md).
constexpr std::array kTopics{
    Topic{.id = "getting-started", .group = QT_TRANSLATE_NOOP("Help", "Start here")},
    Topic{.id = "audio-and-midi", .group = QT_TRANSLATE_NOOP("Help", "Start here")},
    Topic{.id = "setlists-and-songs", .group = QT_TRANSLATE_NOOP("Help", "Your setlist")},
    Topic{.id = "instruments", .group = QT_TRANSLATE_NOOP("Help", "Your setlist")},
    Topic{.id = "splits-layers-knobs", .group = QT_TRANSLATE_NOOP("Help", "Your setlist")},
    Topic{.id = "charts", .group = QT_TRANSLATE_NOOP("Help", "Songs and charts")},
    Topic{.id = "sections-and-tempo", .group = QT_TRANSLATE_NOOP("Help", "Songs and charts")},
    Topic{.id = "perform", .group = QT_TRANSLATE_NOOP("Help", "On stage")},
    Topic{.id = "looper", .group = QT_TRANSLATE_NOOP("Help", "On stage")},
    Topic{.id = "practice", .group = QT_TRANSLATE_NOOP("Help", "Practice")},
    Topic{.id = "shortcuts", .group = QT_TRANSLATE_NOOP("Help", "Reference")},
    Topic{.id = "troubleshooting", .group = QT_TRANSLATE_NOOP("Help", "Reference")},
};

// The page's file, or why it could not be read.
std::pair<QString, QString> readPage(const QString& id)
{
    QFile file(u":/help/%1.md"_s.arg(id));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {{}, file.errorString()};
    return {QString::fromUtf8(file.readAll()), {}};
}

QString titleOf(const QString& page)
{
    const qsizetype end = page.indexOf(u'\n');
    return page.startsWith(u"# "_s) ? page.mid(2, end < 0 ? -1 : end - 2).trimmed() : QString();
}

// A line of the page around `at`, without the Markdown marks.
QString snippetAround(const QString& page, qsizetype at)
{
    const qsizetype start = page.lastIndexOf(u'\n', at) + 1;
    qsizetype end = page.indexOf(u'\n', at);
    if (end < 0) end = page.size();
    QString line = page.mid(start, end - start);
    static const QRegularExpression links(u"\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    line.replace(links, u"\\1"_s);
    line.remove(u'*').remove(u'#').remove(u'`');
    line = line.trimmed();
    if (line.startsWith(u"- "_s)) line = line.mid(2);
    return line.size() > 160 ? line.left(157) + u"…"_s : line;
}

} // namespace

QVariantList HelpLibrary::topics()
{
    QVariantList list;
    for (const Topic& topic : kTopics) {
        const QString id = QString::fromLatin1(topic.id);
        const auto [text, error] = readPage(id);
        if (!error.isEmpty()) {
            qCWarning(lcUi) << "Help: the page" << id << "could not be read:" << error;
            continue;
        }
        list << QVariantMap{{u"id"_s, id}, {u"title"_s, titleOf(text)},
                            {u"group"_s, QCoreApplication::translate("Help", topic.group)}};
    }
    return list;
}

QString HelpLibrary::page(const QString& id)
{
    const bool known = std::ranges::any_of(kTopics, [&id](const Topic& t) { return id == QLatin1StringView(t.id); });
    if (!known) {
        qCWarning(lcUi) << "Help: there is no page" << id;
        return u"# Page not found\n\nThere is no help page called “%1”. Pick a topic on the left, or search for it.\n"_s.arg(id);
    }
    const auto [text, error] = readPage(id);
    if (!error.isEmpty()) {
        qCWarning(lcUi) << "Help: the page" << id << "could not be read:" << error;
        return u"# Page could not be read\n\nThe help page “%1” could not be read: %2\n"_s.arg(id, error);
    }
    return text;
}

QString HelpLibrary::topicOfLink(const QString& link)
{
    // "charts.md" or "./charts.md" (the guide's own pages, as GitHub links
    // them too), or the older "help:charts".
    static const QRegularExpression guidePage(u"^(?:\\./)?([a-z0-9-]+)\\.md$"_s);
    static const QRegularExpression app(u"^help:([a-z0-9-]+)$"_s);
    QRegularExpressionMatch match = guidePage.match(link);
    if (!match.hasMatch()) match = app.match(link);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString HelpLibrary::toHtml(const QString& markdown, const QString& linkColour)
{
    QTextDocument document;
    document.setMarkdown(markdown);
    // Every link in the colour asked for (Qt gives them its own blue).
    const QColor colour(linkColour);
    if (!colour.isValid()) qCWarning(lcUi) << "Help: the link colour" << linkColour << "is not a colour";
    for (QTextBlock block = document.begin(); block.isValid() && colour.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
            QTextCursor cursor(&document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setForeground(colour);
            cursor.mergeCharFormat(format);
        }
    }
    // The page takes the window's font and size: the body names none.
    QString html = document.toHtml();
    static const QRegularExpression bodyStyle(u"<body style=\"[^\"]*\">"_s);
    html.replace(bodyStyle, u"<body>"_s);
    return html;
}

QVariantList HelpLibrary::search(const QString& query)
{
    const QStringList wanted = query.simplified().split(u' ', Qt::SkipEmptyParts);
    if (wanted.isEmpty()) return {};
    // Whole words, any case ("for" is not in "Perform"; a word may start one: "loop" finds "loops").
    const auto pattern = [](const QString& w) {
        return QRegularExpression(u"\\b%1"_s.arg(QRegularExpression::escape(w)), QRegularExpression::CaseInsensitiveOption);
    };
    std::vector<QRegularExpression> words;
    std::ranges::transform(wanted, std::back_inserter(words), pattern);
    const QRegularExpression phrase = pattern(wanted.join(u' '));

    struct Hit
    {
        int score = 0;
        QVariantMap found;
    };
    std::vector<Hit> hits;
    for (const QVariant& t : topics()) {
        const QVariantMap topic = t.toMap();
        const QString text = page(topic.value(u"id"_s).toString());
        const bool all = std::ranges::all_of(words, [&text](const QRegularExpression& w) { return w.match(text).hasMatch(); });
        if (!all) continue;
        const QString title = topic.value(u"title"_s).toString();
        // Its title holds a word, then the words together, then how often they come.
        int score = std::ranges::any_of(words, [&title](const QRegularExpression& w) { return w.match(title).hasMatch(); }) ? 1000 : 0;
        const QRegularExpressionMatch together = phrase.match(text, text.indexOf(u'\n') + 1);
        if (together.hasMatch()) score += 100;
        for (const QRegularExpression& w : words) {
            auto it = w.globalMatch(text);
            while (it.hasNext() && score % 100 < 99) {
                it.next();
                ++score;
            }
        }
        // Where to quote from: the words together, else the first word past the title.
        qsizetype at = together.hasMatch() ? together.capturedStart() : words.front().match(text, text.indexOf(u'\n') + 1).capturedStart();
        if (at < 0) at = 0;
        hits.push_back({.score = score, .found = {{u"id"_s, topic.value(u"id"_s)}, {u"title"_s, title}, {u"snippet"_s, snippetAround(text, at)}}});
    }
    std::ranges::stable_sort(hits, std::greater<>{}, &Hit::score);
    QVariantList list;
    for (const Hit& hit : hits) list << hit.found;
    return list;
}

} // namespace gigchain::ui
