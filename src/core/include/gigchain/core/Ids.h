#pragma once

#include <QString>
#include <QUuid>

#include <utility>

namespace gigchain::core {

// A strongly typed, persisted identifier. Different tags cannot be mixed up
// (a PatchId is not a ChannelId). Ids are what crosses module boundaries,
// never pointers into the model.
template <typename Tag>
class Id
{
public:
    Id() = default;
    explicit Id(QString value) : m_value(std::move(value)) {}

    static Id generate() { return Id(QUuid::createUuid().toString(QUuid::WithoutBraces)); }

    [[nodiscard]] const QString& value() const { return m_value; }
    [[nodiscard]] bool isNull() const { return m_value.isEmpty(); }

    friend bool operator==(const Id&, const Id&) = default;

private:
    QString m_value;
};

struct SongTag;
struct PatchTag;
struct ChannelTag;

using SongId = Id<SongTag>;
using PatchId = Id<PatchTag>;
using ChannelId = Id<ChannelTag>;

} // namespace gigchain::core
