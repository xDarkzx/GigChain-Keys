#pragma once

#include "openstage/core/Error.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace openstage::ui {

// Native Instruments "ProductHints" metadata (Service Center XML files and
// the XML embedded at the start of every Kontakt library's .nicnt file).
struct NiProductHints
{
    QString name;
    QString company;
    QString regKey;
    QString binName;
};

// Reads the first <Product> of a ProductHints document; nested blocks are
// skipped. Empty fields when absent; a malformed document is logged.
NiProductHints parseProductHints(const QByteArray& xml, const QString& origin);

// A Kontakt library's official banner: the wide PNG the library maker embeds
// in its .nicnt file (the picture Kontakt's Libraries tab shows). The
// .nicnt also embeds the smaller NKS artwork; those are not returned.
// Method verified against KoEd (github.com/zhangdoa/KoEd, FileParser.cpp),
// which locates embedded PNGs by their signature and IEND chunk.
core::Result<QByteArray> extractNicntBanner(const QString& nicntPath);
NiProductHints readNicntProduct(const QString& nicntPath);

struct KontaktLibrary
{
    QString name;
    QString company;
    QString folder;
    QString nicntPath;
    QString bannerPath; // PNG file: a wallpaper.png beside the .nicnt, or the extracted banner
};

// Finds Kontakt libraries (folders with a .nicnt) under `folders` and makes
// each banner available as a PNG file in `cacheDir` (extracted once per
// version of the .nicnt). Sorted by name. Problems are logged; a library
// whose banner cannot be read is still listed, without one.
std::vector<KontaktLibrary> scanKontaktLibraries(const QStringList& folders, const QString& cacheDir);

} // namespace openstage::ui
