// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blockpresentation.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>

#include <algorithm>
#include <array>
#include <string_view>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace blockPresentation
{

namespace
{

/// One block, as a person meets it.
///
/// **The name and the description are written here once.** They are lazy
/// (kli18nc) because the table is built before a translation catalogue is
/// loaded — the same reason the error messages are — and are turned into
/// text only when something is about to show them.
struct Entry {
    std::string_view typeId;
    std::string_view category;
    KLazyLocalizedString name;
    /// One line for a library row: a verb first when the block does
    /// something, a noun phrase when it is a source or an output.
    KLazyLocalizedString description;
    /// The longer form for the tooltip, where the extra room earns
    /// something: what it is good for, or what it is not to be confused with.
    KLazyLocalizedString tooltip;
};

/// One heading in the library.
struct CategoryEntry {
    std::string_view id;
    KLazyLocalizedString name;
};

// **The order the library reads in**, which is the order a session is
// built: what brings sound in, what it goes out to, what happens to it on
// the way, what measures it, the two shelves for everything else, and the
// blocks for listening with one ear at a time before the plugins.
constexpr std::array kCategories{
    CategoryEntry{"sources", kli18nc("@title:group blocks that bring sound in", "Sources")},
    CategoryEntry{"outputs", kli18nc("@title:group blocks that send sound out", "Outputs")},
    CategoryEntry{"effects", kli18nc("@title:group blocks that change the sound", "Built-in Effects")},
    CategoryEntry{"meters", kli18nc("@title:group blocks that measure the sound", "Meters")},
    CategoryEntry{"advanced", kli18nc("@title:group blocks for less usual jobs", "Advanced")},
    CategoryEntry{
        "hearing",
        kli18nc("@title:group blocks that shape the sound for each ear on its own, for listening", "Hearing")},
    CategoryEntry{"plugins", kli18nc("@title:group blocks that host somebody else's plugin", "Plugins")},
};

// The whole catalogue, including the blocks no build has yet, by category
// and then by name. The ids are the ones the engine registers, or will
// register (see the class comment).
constexpr std::array kEntries{
    // ------------------------------------------------------------ sources
    Entry{"source.application",
          "sources",
          kli18nc("@item block name", "Application"),
          kli18nc("@info block description", "Sound from one application"),
          kli18nc("@info:tooltip block description",
                  "Sound from one application, taken as it plays. The application goes on playing to its own "
                  "device, and the microphone it is using can be taken with it.")},
    Entry{"source.device",
          "sources",
          kli18nc("@item block name", "Input Device"),
          kli18nc("@info block description", "A microphone or other recording input"),
          kli18nc("@info:tooltip block description",
                  "A microphone, an audio interface or anything else this machine can record from. Choose which "
                  "of its channels feed the left and the right side.")},
    Entry{"source.system",
          "sources",
          kli18nc("@item block name", "System Audio"),
          kli18nc("@info block description", "Everything this machine is playing"),
          kli18nc("@info:tooltip block description",
                  "Everything this machine is playing, gathered one application at a time. Alert sounds and any "
                  "applications you name are left out.")},

    // ------------------------------------------------------------ outputs
    Entry{"output.broadcast",
          "outputs",
          kli18nc("@item block name", "Broadcast"),
          kli18nc("@info block description", "A stream for an internet radio server"),
          kli18nc("@info:tooltip block description",
                  "Sends the session to an internet radio server, with the station's details and the title of "
                  "whatever is playing.")},
    Entry{"output.livestream",
          "outputs",
          kli18nc("@item block name", "Live Stream"),
          kli18nc("@info block description", "A video stream for a streaming platform"),
          kli18nc("@info:tooltip block description",
                  "Sends the session to a streaming platform as video, behind a picture made here from a title, "
                  "a logo and an optional moving display.")},
    Entry{"output.wave",
          "outputs",
          kli18nc("@item block name", "Wave"),
          kli18nc("@info block description", "A test tone or noise, on either side or both"),
          kli18nc("@info:tooltip block description",
                  "Makes a sound of its own for testing speakers, cables and levels: a sine, square, triangle or "
                  "sawtooth at any frequency, or white or pink noise, at the level you set, on the left, the right "
                  "or both. It starts quiet. Send it to an Output Device to hear it.")},
    Entry{"output.device",
          "outputs",
          kli18nc("@item block name", "Output Device"),
          kli18nc("@info block description", "Speakers, headphones or another output"),
          kli18nc("@info:tooltip block description",
                  "The session's sound on speakers, headphones or anything else that plays, at its own volume. "
                  "It follows the system's choice until you name a device.")},
    Entry{"output.recorder",
          "outputs",
          kli18nc("@item block name", "Recorder"),
          kli18nc("@info block description", "A recording of the session, written to a file"),
          kli18nc("@info:tooltip block description",
                  "Writes the session to a file in the format and quality you choose, named from the date, the "
                  "session and a counter.")},
    Entry{"output.virtualdevice",
          "outputs",
          kli18nc("@item block name", "Virtual Device"),
          kli18nc("@info block description", "The session as an input other apps can choose"),
          kli18nc("@info:tooltip block description",
                  "Publishes the session as a recording device, so a meeting, a streaming tool or a recorder can "
                  "pick it as their microphone.")},

    // ------------------------------------------------------------ effects
    Entry{"effect.equalizer10",
          "effects",
          kli18nc("@item block name", "10-Band EQ"),
          kli18nc("@info block description", "Shapes the tone with ten fixed bands"),
          kli18nc("@info:tooltip block description",
                  "Shapes the tone with ten sliders, from the lowest rumble to the highest air. Presets cover "
                  "the usual jobs, and one action puts every slider back.")},
    Entry{"effect.autolevel",
          "effects",
          kli18nc("@item block name", "Auto Level"),
          kli18nc("@info block description", "Evens out a source whose loudness keeps changing"),
          kli18nc("@info:tooltip block description",
                  "Evens out a source whose loudness keeps changing: quiet passages are lifted towards a steady "
                  "level, and anything already loud enough is left alone.")},
    Entry{"effect.balance",
          "effects",
          kli18nc("@item block name", "Balance"),
          kli18nc("@info block description", "Trims one side of a stereo signal against the other"),
          kli18nc("@info:tooltip block description",
                  "Trims one side of a stereo signal against the other and leaves the stereo picture where it "
                  "is. To move a sound across that picture instead, use Pan.")},
    Entry{"effect.basstreble",
          "effects",
          kli18nc("@item block name", "Bass & Treble"),
          kli18nc("@info block description", "Lifts or cuts the low and the high end"),
          kli18nc("@info:tooltip block description",
                  "Lifts or cuts the low and the high end, with one control for each. The quickest way to warm "
                  "up or brighten a source.")},
    Entry{"effect.channels",
          "effects",
          kli18nc("@item block name", "Channels"),
          kli18nc("@info block description", "Swaps, mutes or folds the left and right channels"),
          kli18nc("@info:tooltip block description",
                  "Swaps, mutes or folds the two channels — one side to both, a fold to mono, a polarity flip — "
                  "and fades from one arrangement to the next.")},
    Entry{"effect.dynamics",
          "effects",
          kli18nc("@item block name", "Dynamics Processor"),
          kli18nc("@info block description", "Holds loud sound back and lifts quiet sound"),
          kli18nc("@info:tooltip block description",
                  "Holds back what is above the threshold and lifts or gates what is below it, along a curve you "
                  "can see and drag.")},
    Entry{"effect.echo",
          "effects",
          kli18nc("@item block name", "Echo"),
          kli18nc("@info block description", "Repeats the sound, each repeat quieter than the last"),
          kli18nc("@info:tooltip block description",
                  "Repeats the sound after a delay you set, each repeat quieter and duller than the one before, "
                  "mixed in behind the original.")},
    Entry{"effect.highpass",
          "effects",
          kli18nc("@item block name", "High Pass Filter"),
          kli18nc("@info block description", "Rolls off everything below a chosen frequency"),
          kli18nc("@info:tooltip block description",
                  "Rolls off everything below a chosen frequency, for rumble, handling noise and the thump of a "
                  "room. The roll-off can be gentle or steep.")},
    Entry{"effect.limiter",
          "effects",
          kli18nc("@item block name", "Limiter"),
          kli18nc("@info block description", "Holds the signal below a ceiling it cannot cross"),
          kli18nc("@info:tooltip block description",
                  "Holds the signal below a ceiling it cannot cross, listening ahead so nothing slips through, "
                  "and shows how much it is holding back.")},
    Entry{"effect.lowpass",
          "effects",
          kli18nc("@item block name", "Low Pass Filter"),
          kli18nc("@info block description", "Rolls off everything above a chosen frequency"),
          kli18nc("@info:tooltip block description",
                  "Rolls off everything above a chosen frequency, for hiss and other high noise. The roll-off "
                  "can be gentle or steep.")},
    Entry{"effect.monoenhancer",
          "effects",
          kli18nc("@item block name", "Mono Enhancer"),
          kli18nc("@info block description", "Widens a mono recording without breaking it"),
          kli18nc("@info:tooltip block description",
                  "Gives a mono recording a sense of width, while the two sides still add back up to the sound "
                  "you started with.")},
    Entry{"effect.multibandcompressor",
          "effects",
          kli18nc("@item block name", "Multiband Compressor"),
          kli18nc("@info block description", "Steadies the low, middle and high ranges apart"),
          kli18nc("@info:tooltip block description",
                  "Steadies the low, the middle and the high range separately, so a loud voice does not pull the "
                  "bass down with it.")},
    Entry{"effect.pan",
          "effects",
          kli18nc("@item block name", "Pan"),
          kli18nc("@info block description", "Places a sound between the left and the right"),
          kli18nc("@info:tooltip block description",
                  "Places a sound anywhere between the left and the right, at the same loudness wherever it "
                  "sits. To trim one side of a stereo pair instead, use Balance.")},
    Entry{"effect.pitchshift",
          "effects",
          kli18nc("@item block name", "Pitch Shift"),
          kli18nc("@info block description", "Moves the pitch up or down, at the same speed"),
          kli18nc("@info:tooltip block description",
                  "Moves the pitch up or down by whole notes and fractions of one, without the sound running "
                  "faster or slower.")},
    Entry{"effect.reverb",
          "effects",
          kli18nc("@item block name", "Reverb"),
          kli18nc("@info block description", "Puts the sound in a room, from a booth to a hall"),
          kli18nc("@info:tooltip block description",
                  "Puts the sound in a room, from a small booth to a hall, with the size, the brightness and how "
                  "much of it is heard all under control.")},
    Entry{"effect.simplecompressor",
          "effects",
          kli18nc("@item block name", "Simple Compressor"),
          kli18nc("@info block description", "Steadies the level from one choice, not six controls"),
          kli18nc("@info:tooltip block description",
                  "Steadies the level by picking how firmly it should work, rather than setting a threshold, a "
                  "ratio and a pair of times yourself.")},
    Entry{"effect.volume",
          "effects",
          kli18nc("@item block name", "Volume"),
          kli18nc("@info block description", "Turns a source up or down to match the others"),
          kli18nc("@info:tooltip block description",
                  "Turns a source up or down, as a percentage of what arrived. A limiter sits at the end of it, "
                  "so turning a source up cannot make it clip.")},

    // ------------------------------------------------------------- meters
    Entry{"meter.peakrms",
          "meters",
          kli18nc("@item block name", "Peak/RMS"),
          kli18nc("@info block description", "A level meter for any point in a lane"),
          kli18nc("@info:tooltip block description",
                  "Shows both the peaks and the average level at this point in the lane, and passes the sound on "
                  "exactly as it arrived.")},
    Entry{"meter.vu",
          "meters",
          kli18nc("@item block name", "VU"),
          kli18nc("@info block description", "A pair of needles following the average level"),
          kli18nc("@info:tooltip block description",
                  "A pair of needles that follow the average level the way a tape machine's meters did, against "
                  "a reference level you choose.")},

    // ----------------------------------------------------------- advanced
    Entry{"advanced.declick",
          "advanced",
          kli18nc("@item block name", "Declick"),
          kli18nc("@info block description", "Takes out clicks, pops and crackle"),
          kli18nc("@info:tooltip block description",
                  "Takes out clicks, pops and crackle — from a record, a bad cable or a dropout — and fills in "
                  "what was underneath them.")},
    Entry{"advanced.dehum",
          "advanced",
          kli18nc("@item block name", "Dehum"),
          kli18nc("@info block description", "Takes out the steady hum of the mains supply"),
          kli18nc("@info:tooltip block description",
                  "Takes out the steady hum a mains supply leaves behind, at 50 or 60 Hz, together with the "
                  "whine that rides above it.")},
    Entry{"advanced.denoise",
          "advanced",
          kli18nc("@item block name", "Denoise"),
          kli18nc("@info block description", "Takes out a steady noise once it has been learned"),
          kli18nc("@info:tooltip block description",
                  "Listens to a few seconds of the noise on its own, then takes that noise out of everything "
                  "that follows. For fans, hiss and air conditioning.")},
    Entry{"advanced.ducking",
          "advanced",
          kli18nc("@item block name", "Ducking"),
          kli18nc("@info block description", "Turns a background source down under a voice"),
          kli18nc("@info:tooltip block description",
                  "Turns the background source down whenever the main one — a voice, usually — is heard, and "
                  "brings it back up when the talking stops.")},
    Entry{"advanced.firfilter",
          "advanced",
          kli18nc("@item block name", "FIR Filter"),
          kli18nc("@info block description", "Applies a correction measured from a file"),
          kli18nc("@info:tooltip block description",
                  "Applies a measurement taken from a file: the sound of a room, of a speaker cabinet, or a "
                  "correction for a particular pair of headphones.")},
    Entry{"advanced.inputswitch",
          "advanced",
          kli18nc("@item block name", "Input Switch"),
          kli18nc("@info block description", "Passes one of two inputs and fades between them"),
          kli18nc("@info:tooltip block description",
                  "Passes one of two inputs and fades from one to the other when you switch, so the change is "
                  "never heard as a jump.")},
    Entry{"advanced.mixer",
          "advanced",
          kli18nc("@item block name", "Mixer"),
          kli18nc("@info block description", "Combines up to five sources, each with its own strip"),
          kli18nc("@info:tooltip block description",
                  "Combines up to five sources, each with a fader, a mute, a solo and a balance, and can bring a "
                  "whole set of levels back at once.")},
    Entry{"advanced.parametriceq",
          "advanced",
          kli18nc("@item block name", "Parametric EQ"),
          kli18nc("@info block description", "Shapes the tone with bands you place yourself"),
          kli18nc("@info:tooltip block description",
                  "Shapes the tone with up to ten bands you place yourself — bells, shelves, pass filters and "
                  "notches — dragged along a curve with the sound that is playing drawn behind it.")},
    Entry{"advanced.speechdenoise",
          "advanced",
          kli18nc("@item block name", "Speech Denoise"),
          kli18nc("@info block description", "Clears noise from speech with nothing to set up"),
          kli18nc("@info:tooltip block description",
                  "Clears the background from speech without learning it first, which suits a voice in a room "
                  "where the noise keeps changing.")},
    Entry{"advanced.sync",
          "advanced",
          kli18nc("@item block name", "Sync"),
          kli18nc("@info block description", "Delays a lane to line it up with another"),
          kli18nc("@info:tooltip block description",
                  "Delays a lane by an exact number of milliseconds, to line the sound up with a picture or with "
                  "another path through the session.")},
    Entry{"advanced.timeshift",
          "advanced",
          kli18nc("@item block name", "Time Shift"),
          kli18nc("@info block description", "Pauses and rewinds sound that is still arriving"),
          kli18nc("@info:tooltip block description",
                  "Holds on to the sound as it arrives, so it can be paused, wound back and searched through, "
                  "and then caught up with again.")},
    Entry{"advanced.transcribe",
          "advanced",
          kli18nc("@item block name", "Transcribe"),
          kli18nc("@info block description", "Writes down what is said, on this machine alone"),
          kli18nc("@info:tooltip block description",
                  "Writes speech to a text file as it is spoken, with times and a label for each speaker. The "
                  "sound never leaves this machine.")},

    // ------------------------------------------------------------ hearing
    Entry{
        "hearing.compensation16",
        "hearing",
        kli18nc("@item block name: a working name, gain for one ear by frequency and level", "Hearing Compensation 16"),
        kli18nc("@info block description", "Lifts quiet sound for one ear, in sixteen bands"),
        kli18nc("@info:tooltip block description",
                "Gives one ear more of what it hears faintly: in each of sixteen bands, soft sound is lifted a "
                "lot and loud sound little, and nothing goes above a ceiling. Its settings can be worked out "
                "from an audiogram.")},
    Entry{"hearing.compensation64",
          "hearing",
          kli18nc("@item block name: a working name, gain for one ear by frequency and level, finely",
                  "Hearing Compensation 64"),
          kli18nc("@info block description", "Lifts quiet sound for one ear, in sixty-four bands set with points"),
          kli18nc("@info:tooltip block description",
                  "Gives one ear more of what it hears faintly, finely enough for a steep slope or a narrow dip: "
                  "place points along the curves and the sixty-four bands follow them. Soft sound is lifted a lot "
                  "and loud sound little, and nothing goes above a ceiling. Its points can be worked out from an "
                  "audiogram.")},
    Entry{"hearing.frequencylowering",
          "hearing",
          kli18nc("@item block name", "Frequency Lowering"),
          kli18nc("@info block description", "Moves high sounds down to where one ear hears them"),
          kli18nc("@info:tooltip block description",
                  "Moves sound from a high range one ear makes no use of into a lower range it hears, so that the "
                  "hiss of s, sh and f is heard again. Up to four moves, each squeezing its range down or copying "
                  "it lower, for one ear; add another for the other ear.")},
    Entry{"hearing.frequencyraising",
          "hearing",
          kli18nc("@item block name", "Frequency Raising"),
          kli18nc("@info block description", "Moves low sounds up to where one ear hears them"),
          kli18nc("@info:tooltip block description",
                  "Moves sound from a low or middle range one ear makes no use of into a higher range it hears. Up "
                  "to four moves, each copying its range up, squeezing it, or adding its harmonics higher up, for "
                  "one ear; add another for the other ear. With Frequency Lowering, it takes a dip in the middle "
                  "apart: its lower half down, its upper half up.")},
    Entry{"hearing.joinlr",
          "hearing",
          kli18nc("@item block name: joins a left and a right lane into one stereo pair", "Join L/R"),
          kli18nc("@info block description", "Puts a left and a right lane back into one stereo pair"),
          kli18nc("@info:tooltip block description",
                  "Takes the left side from its Left input and the right side from its Right input, so that each "
                  "ear can have blocks of its own on a lane of its own. The two are lined up however late either "
                  "lane is.")},

    // ------------------------------------------------------------ plugins
    Entry{"plugin.builtin",
          "plugins",
          kli18nc("@item block name", "Test Plugin"),
          kli18nc("@info block description", "A plugin built in, for checking that plugins run"),
          kli18nc("@info:tooltip block description",
                  "A plain gain that runs the way every plugin does: in a process of its own beside Reverblabr, "
                  "so that a plugin that fails takes nothing else with it. For checking that plugins run on this "
                  "machine.")},
    Entry{"plugin.clap",
          "plugins",
          kli18nc("@item block name", "CLAP"),
          kli18nc("@info block description", "A CLAP plugin installed on this machine"),
          kli18nc("@info:tooltip block description",
                  "A CLAP plugin installed on this machine. It runs beside Reverblabr rather than inside it, so "
                  "a plugin that fails takes nothing else with it.")},
    Entry{"plugin.ladspa",
          "plugins",
          kli18nc("@item block name", "LADSPA"),
          kli18nc("@info block description", "A LADSPA plugin installed on this machine"),
          kli18nc("@info:tooltip block description",
                  "A LADSPA plugin installed on this machine. These have no window of their own, so their "
                  "settings appear in a panel built from what they offer.")},
    Entry{"plugin.lv2",
          "plugins",
          kli18nc("@item block name", "LV2"),
          kli18nc("@info block description", "An LV2 plugin installed on this machine"),
          kli18nc("@info:tooltip block description",
                  "An LV2 plugin installed on this machine, with its own window where it has one and a panel "
                  "built from its settings where it has not.")},
    Entry{"plugin.vst3",
          "plugins",
          kli18nc("@item block name", "VST3"),
          kli18nc("@info block description", "A VST3 plugin installed on this machine"),
          kli18nc("@info:tooltip block description",
                  "A VST3 plugin installed on this machine, with its own editor window and a panel of the same "
                  "settings beside it.")},
};

[[nodiscard]] QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] const Entry *find(const QString &typeId)
{
    const auto it =
        std::ranges::find_if(kEntries, [&typeId](const Entry &entry) { return typeId == toQString(entry.typeId); });
    return it == kEntries.end() ? nullptr : &*it;
}

/// A lazy string as text, in the application's own catalogue. Without the
/// domain, a string from a library-shaped table is looked up in whatever
/// catalogue happens to be current.
[[nodiscard]] QString text(const KLazyLocalizedString &lazy)
{
    KLocalizedString localized = lazy;
#ifdef TRANSLATION_DOMAIN
    localized = localized.withDomain(TRANSLATION_DOMAIN);
#endif
    return localized.toString();
}

} // namespace

QStringList types()
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(kEntries.size()));
    for (const Entry &entry : kEntries) {
        ids.append(toQString(entry.typeId));
    }
    return ids;
}

bool has(const QString &typeId)
{
    return find(typeId) != nullptr;
}

QStringList categories()
{
    QStringList names;
    names.reserve(static_cast<qsizetype>(kCategories.size()));
    for (const CategoryEntry &entry : kCategories) {
        names.append(toQString(entry.id));
    }
    return names;
}

QString laneName(const QString &laneId)
{
    // The lanes this build's blocks have, by the stable ids the engine gives
    // them. Few enough for a switch; a table when a mixer brings five.
    if (laneId == "main"_L1) {
        return i18nc("@label an input of the Ducking block: the voice that turns the other down", "Main");
    }
    if (laneId == "background"_L1) {
        return i18nc("@label an input of the Ducking block: the sound that is turned down", "Background");
    }
    // Join L/R's: the lane each ear comes in on.
    if (laneId == "left"_L1) {
        return i18nc("@label an input of the Join L/R block: the lane the left ear comes in on", "Left");
    }
    if (laneId == "right"_L1) {
        return i18nc("@label an input of the Join L/R block: the lane the right ear comes in on", "Right");
    }
    // The Input Switch's.
    if (laneId == "a"_L1) {
        return i18nc("@label the first input of the Input Switch block", "Input A");
    }
    if (laneId == "b"_L1) {
        return i18nc("@label the second input of the Input Switch block", "Input B");
    }
    // The Mixer's: input1 to input5, said as a person counts them.
    if (laneId.startsWith("input"_L1)) {
        bool isNumber = false;
        const int number = laneId.mid(5).toInt(&isNumber);
        if (isNumber) {
            return i18nc("@label one of the Mixer block's numbered inputs", "Input %1", number);
        }
    }
    return laneId;
}

QString categoryName(const QString &category)
{
    for (const CategoryEntry &entry : kCategories) {
        if (toQString(entry.id) == category) {
            return text(entry.name);
        }
    }
    return {};
}

QString name(const QString &typeId)
{
    const Entry *entry = find(typeId);
    return entry == nullptr ? typeId : text(entry->name);
}

QString description(const QString &typeId)
{
    const Entry *entry = find(typeId);
    return entry == nullptr ? QString{} : text(entry->description);
}

QString tooltip(const QString &typeId)
{
    const Entry *entry = find(typeId);
    return entry == nullptr ? QString{} : text(entry->tooltip);
}

QString category(const QString &typeId)
{
    const Entry *entry = find(typeId);
    return entry == nullptr ? QString{} : toQString(entry->category);
}

QUrl icon(const QString &typeId)
{
    if (find(typeId) == nullptr) {
        return {};
    }
    // The icons travel in the QML module's resources, which is where the
    // module's own files are, so a build that has the interface has them.
    return QUrl(u"qrc:/qt/qml/io/github/reverblabr/icons/%1.svg"_s.arg(typeId));
}

} // namespace blockPresentation

BlockPresentation::BlockPresentation(QObject *parent)
    : QObject(parent)
{
}

QStringList BlockPresentation::types() const
{
    return blockPresentation::types();
}

QStringList BlockPresentation::categories() const
{
    return blockPresentation::categories();
}

QString BlockPresentation::categoryName(const QString &category) const
{
    return blockPresentation::categoryName(category);
}

QString BlockPresentation::laneName(const QString &laneId) const
{
    return blockPresentation::laneName(laneId);
}

QVariantMap BlockPresentation::describe(const QString &typeId) const
{
    if (!blockPresentation::has(typeId)) {
        return {};
    }
    return QVariantMap{
        {u"name"_s, blockPresentation::name(typeId)},
        {u"description"_s, blockPresentation::description(typeId)},
        {u"tooltip"_s, blockPresentation::tooltip(typeId)},
        {u"category"_s, blockPresentation::category(typeId)},
        {u"icon"_s, blockPresentation::icon(typeId)},
    };
}

QString BlockPresentation::name(const QString &typeId) const
{
    return blockPresentation::name(typeId);
}

QString BlockPresentation::description(const QString &typeId) const
{
    return blockPresentation::description(typeId);
}

QString BlockPresentation::tooltip(const QString &typeId) const
{
    return blockPresentation::tooltip(typeId);
}

QString BlockPresentation::category(const QString &typeId) const
{
    return blockPresentation::category(typeId);
}

QUrl BlockPresentation::icon(const QString &typeId) const
{
    return blockPresentation::icon(typeId);
}

} // namespace rb::ui
