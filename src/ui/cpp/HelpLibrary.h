#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gigchain::ui {

// The user guide inside the app (Help > User guide): Markdown pages from
// docs/help, built in at :/help/<id>.md. A page links to another with
// [text](help:<id>).
class HelpLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // The topics in reading order: [{id, title, group}].
    Q_INVOKABLE [[nodiscard]] static QVariantList topics();
    // A page's Markdown; a page that cannot be read says why.
    Q_INVOKABLE [[nodiscard]] static QString page(const QString& id);
    // A page's Markdown as HTML for a Text item, its links in `linkColour`.
    Q_INVOKABLE [[nodiscard]] static QString toHtml(const QString& markdown, const QString& linkColour);
    // The pages holding every word (any case), those with a word in their
    // title first: [{id, title, snippet}].
    Q_INVOKABLE [[nodiscard]] static QVariantList search(const QString& query);
};

} // namespace gigchain::ui
