#include "StageQuips.h"

#include <QCoreApplication>
#include <QRandomGenerator>

namespace gigchain::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("StageQuips", text);
}

} // namespace

QStringList StageQuips::lines(Step step)
{
    switch (step) {
    case Step::Connecting:
        return {tr("Plugging in the DI boxes…"),
                tr("Running cables to the patch bay…"),
                tr("Untangling the cable spaghetti…"),
                tr("Taping the cables to the floor…"),
                tr("Finding a free power socket…"),
                tr("Checking the batteries in the DI box…"),
                tr("Coiling the spare cable (over-under, of course)…"),
                tr("Hunting for the XLR that went missing…"),
                tr("Plugging in the power board…"),
                tr("Setting up the keyboard stand…")};
    case Step::Unpacking:
        return {tr("Unpacking the gear…"),
                tr("Opening the road cases…"),
                tr("Counting the gear against the rider…"),
                tr("Wheeling in the keyboard rig…"),
                tr("Checking every patch lead…"),
                tr("Dusting off the synths…"),
                tr("Unrolling the stage rug…"),
                tr("Labelling everything with gaffer tape…"),
                tr("Lining up the pedals…"),
                tr("Carrying in one more road case…")};
    case Step::Setlist:
        return {tr("Taping the setlist to the floor…"),
                tr("Finding the setlist (it was under the coffee)…"),
                tr("Arguing about the encore…"),
                tr("Writing the setlist in big letters…"),
                tr("Moving the slow one to the middle…"),
                tr("Checking which key the chorus is in…"),
                tr("Printing a spare setlist…"),
                tr("Scribbling notes on the setlist…"),
                tr("Counting how many songs fit before curfew…"),
                tr("Double-checking the song order…")};
    case Step::WarmingUp:
        return {tr("Warming up the keys…"),
                tr("Tuning up…"),
                tr("Dialling in the patches…"),
                tr("Warming up the valves…"),
                tr("Stretching the fingers…"),
                tr("Blowing the dust off the presets…"),
                tr("Finding the perfect piano sound…"),
                tr("Nudging the reverb…"),
                tr("Checking the sustain pedal…"),
                tr("Setting the levels…")};
    case Step::LineCheck:
        return {tr("Line-checking the rig…"),
                tr("Sound-checking every instrument…"),
                tr("Checking each channel with the sound guy…"),
                tr("Playing a quick riff on everything…"),
                tr("Making sure everything makes noise…"),
                tr("Checking every instrument is plugged in…"),
                tr("Line check, one by one…"),
                tr("Running through the rig…"),
                tr("Poking every sound once…"),
                tr("Testing, testing…")};
    case Step::SoundGuy:
        return {tr("Hollering at the sound guy…"),
                tr("Waving at the lighting desk…"),
                tr("Asking for more keys in the monitor…"),
                tr("Saying \"check, one, two\" into the mic…"),
                tr("Getting a thumbs-up from front of house…"),
                tr("Waiting for the house lights to drop…"),
                tr("Adjusting the stool height…"),
                tr("Grabbing a bottle of water…"),
                tr("Checking the monitor mix…"),
                tr("Taking a deep breath…")};
    case Step::Ready:
        return {tr("Check, one, two… Ready!"),
                tr("Ready to rock!"),
                tr("Doors are open!"),
                tr("The crowd is waiting!"),
                tr("Showtime!"),
                tr("Lights down, let's go!"),
                tr("The stage is set!"),
                tr("All plugged in. Let's play!"),
                tr("The sound guy gave a thumbs-up!"),
                tr("Ready when you are!")};
    case Step::Count:
        break;
    }
    return {};
}

QString StageQuips::line(Step step)
{
    auto& picked = m_picked[static_cast<std::size_t>(step)];
    if (!picked) {
        const QStringList all = lines(step);
        picked = all.isEmpty() ? QString() : all.at(QRandomGenerator::global()->bounded(all.size()));
    }
    return *picked;
}

} // namespace gigchain::ui
