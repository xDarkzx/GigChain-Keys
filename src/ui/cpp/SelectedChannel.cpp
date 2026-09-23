#include "SelectedChannel.h"

#include "DocumentController.h"

namespace openstage::ui {

SelectedChannel::SelectedChannel(const DocumentController& document, QObject* parent)
    : QObject(parent), m_document(document)
{
    connect(&m_document, &DocumentController::currentChanged, this, &SelectedChannel::changed);
    connect(&m_document, &DocumentController::channelsChanged, this, &SelectedChannel::changed);
    connect(&m_document, &DocumentController::channelUpdated, this, &SelectedChannel::changed);
    connect(&m_document, &DocumentController::selectedChannelChanged, this, &SelectedChannel::changed);
}

const core::Channel* SelectedChannel::channel() const
{
    const core::Patch* patch = m_document.currentPatch();
    const int index = m_document.selectedChannel();
    if (patch == nullptr || index < 0 || static_cast<std::size_t>(index) >= patch->channels.size()) return nullptr;
    return &patch->channels[static_cast<std::size_t>(index)];
}

int SelectedChannel::index() const
{
    return isValid() ? m_document.selectedChannel() : -1;
}

QString SelectedChannel::name() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->name : QString();
}

QString SelectedChannel::instrumentName() const
{
    const core::Channel* c = channel();
    return c != nullptr && c->instrument ? c->instrument->displayName : QString();
}

QStringList SelectedChannel::effectNames() const
{
    QStringList names;
    if (const core::Channel* c = channel()) {
        for (const auto& effect : c->effects) names << effect.displayName;
    }
    return names;
}

int SelectedChannel::keyLow() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->keyLow : 0;
}

int SelectedChannel::keyHigh() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->keyHigh : 127;
}

int SelectedChannel::transpose() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->transpose : 0;
}

int SelectedChannel::midiChannel() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->midiChannel : 0;
}

double SelectedChannel::volumeDb() const
{
    const core::Channel* c = channel();
    return c != nullptr ? c->volumeDb : 0.0;
}

} // namespace openstage::ui
