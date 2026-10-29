// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The picture a live stream sends: what it is drawn on, the words and the
// fonts they are set in, the pictures read from files, what is measured
// of the sound, and the compositor that puts them together.

#include "stream/canvas.h"
#include "stream/compositor.h"
#include "stream/textpainter.h"
#include "stream/visuals.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <thread>

using namespace rb::stream;
using namespace std::chrono_literals;

namespace
{

/// A folder of its own for each test, gone after it.
class Folder
{
public:
    Folder()
        : m_path(std::filesystem::temp_directory_path()
                 / ("rb-compositor-" + std::to_string(::getpid()) + "-" + std::to_string(++s_count)))
    {
        std::filesystem::create_directories(m_path);
    }

    ~Folder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] std::filesystem::path operator/(const char *name) const
    {
        return m_path / name;
    }

private:
    static inline int s_count = 0;
    std::filesystem::path m_path;
};

/// A PNG of \a width × \a height in \a colour, made with FFmpeg's encoder.
[[nodiscard]] bool
writePng(const std::filesystem::path &path, int width, int height, Rgb colour, std::uint8_t alpha = 255)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (codec == nullptr) {
        return false;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    context->width = width;
    context->height = height;
    context->pix_fmt = AV_PIX_FMT_RGBA;
    context->time_base = AVRational{1, 1};
    bool ok = avcodec_open2(context, codec, nullptr) >= 0;
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    frame->format = AV_PIX_FMT_RGBA;
    frame->width = width;
    frame->height = height;
    ok = ok && av_frame_get_buffer(frame, 0) >= 0;
    if (ok) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                std::uint8_t *pixel = frame->data[0] + y * frame->linesize[0] + x * 4;
                pixel[0] = colour.r;
                pixel[1] = colour.g;
                pixel[2] = colour.b;
                pixel[3] = alpha;
            }
        }
        ok = avcodec_send_frame(context, frame) >= 0 && avcodec_receive_packet(context, packet) >= 0;
    }
    if (ok) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(packet->data), packet->size);
        ok = out.good();
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
    return ok;
}

/// How many pixels of \a area are not \a colour.
[[nodiscard]] int differing(const Canvas &canvas, Rect area, Rgb colour)
{
    int count = 0;
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            count += canvas.at(x, y) == colour ? 0 : 1;
        }
    }
    return count;
}

[[nodiscard]] std::vector<float> sine(double hz, double amplitude, std::size_t frames, std::uint32_t rate = 48000)
{
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * frame / rate));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    return samples;
}

} // namespace

TEST(CanvasTest, ReadsColoursAsSessionsKeepThem)
{
    EXPECT_EQ(parseColour("#16181d"), (Rgb{0x16, 0x18, 0x1d}));
    EXPECT_EQ(parseColour("#FFF"), (Rgb{255, 255, 255}));
    EXPECT_FALSE(parseColour("16181d").has_value());
    EXPECT_FALSE(parseColour("#12345").has_value());
    EXPECT_FALSE(parseColour("#gggggg").has_value());
    EXPECT_NEAR(lumaOf(Rgb{255, 255, 255}), 1.0, 1e-9);
    EXPECT_NEAR(lumaOf(Rgb{0, 0, 0}), 0.0, 1e-9);
}

TEST(CanvasTest, DrawsWithinItselfAndBlends)
{
    Canvas canvas(8, 8);
    canvas.fill(Rgb{0, 0, 0});
    // Half off the edge: only what is inside is drawn.
    canvas.fillRect(Rect{-4, -4, 8, 8}, Rgb{200, 100, 50});
    EXPECT_EQ(canvas.at(3, 3), (Rgb{200, 100, 50}));
    EXPECT_EQ(canvas.at(4, 4), (Rgb{0, 0, 0}));
    canvas.fillRect(Rect{4, 4, 4, 4}, Rgb{200, 200, 200}, 0.5);
    EXPECT_EQ(canvas.at(5, 5), (Rgb{100, 100, 100}));
    Coverage coverage{2, 1, {255, 128}};
    canvas.paint(coverage, 0, 7, Rgb{255, 255, 255});
    EXPECT_EQ(canvas.at(0, 7), (Rgb{255, 255, 255}));
    EXPECT_EQ(canvas.at(1, 7).r, 128);
    EXPECT_GT(canvas.luma(Rect{0, 0, 4, 4}), 0.3);
}

TEST(PictureTest, ReadsAndFitsAnImage)
{
    Folder folder;
    const auto path = folder / "logo.png";
    ASSERT_TRUE(writePng(path, 200, 100, Rgb{10, 200, 30}));
    const std::optional<Picture> contained = loadPicture(path, 100, 100, Fit::Contain);
    ASSERT_TRUE(contained.has_value());
    EXPECT_EQ(contained->width, 100);
    EXPECT_EQ(contained->height, 50);
    EXPECT_NEAR(contained->rgba[1], 200, 2);
    const std::optional<Picture> covered = loadPicture(path, 100, 100, Fit::Cover);
    ASSERT_TRUE(covered.has_value());
    EXPECT_EQ(covered->width, 100);
    EXPECT_EQ(covered->height, 100);
}

// **A thin picture covering a wide frame costs a frame**, not a picture
// as wide as the frame scaled up from it: only the part that covers the
// frame is scaled. And one larger than any frame is refused before
// anything is made for it.
TEST(PictureTest, ScalesOnlyWhatCoversAndRefusesTheHuge)
{
    Folder folder;
    const auto thin = folder / "thin.png";
    ASSERT_TRUE(writePng(thin, 1, 1000, Rgb{200, 10, 10}));
    const auto before = std::chrono::steady_clock::now();
    const std::optional<Picture> covered = loadPicture(thin, 1920, 1080, Fit::Cover);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 2s);
    ASSERT_TRUE(covered.has_value());
    EXPECT_EQ(covered->width, 1920);
    EXPECT_EQ(covered->height, 1080);
    EXPECT_NEAR(covered->rgba[0], 200, 3);
    const std::optional<Picture> contained = loadPicture(thin, 1920, 1080, Fit::Contain);
    ASSERT_TRUE(contained.has_value());
    EXPECT_LE(contained->width, 2);
    EXPECT_EQ(contained->height, 1080);

    const auto wide = folder / "wide.png";
    ASSERT_TRUE(writePng(wide, 9000, 2, Rgb{0, 0, 0}));
    EXPECT_FALSE(loadPicture(wide, 640, 360, Fit::Cover).has_value());
    EXPECT_FALSE(loadPicture(thin, 20000, 360, Fit::Cover).has_value());
}

// **Only a regular file is read**: a pipe nobody writes to would hold the
// stream for ever, and text is not a picture.
TEST(PictureTest, TurnsAwayWhatIsNotAPicture)
{
    Folder folder;
    const auto pipe = folder / "pipe.png";
    ASSERT_EQ(::mkfifo(pipe.c_str(), 0600), 0);
    const auto before = std::chrono::steady_clock::now();
    EXPECT_FALSE(loadPicture(pipe, 100, 100, Fit::Contain).has_value());
    EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);

    const auto text = folder / "playlist.png";
    std::ofstream(text) << "#EXTM3U\nhttp://example.com/a.ts\n";
    EXPECT_FALSE(loadPicture(text, 100, 100, Fit::Contain).has_value());
    EXPECT_FALSE(loadPicture(folder / "missing.png", 100, 100, Fit::Contain).has_value());
    EXPECT_FALSE(loadPicture({}, 100, 100, Fit::Contain).has_value());
}

class TextPainterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        painter = TextPainter::create();
        if (!painter) {
            GTEST_SKIP() << "this system has no fonts";
        }
    }

    std::unique_ptr<TextPainter> painter;
};

TEST_F(TextPainterTest, SetsALineInTheDesktopsFont)
{
    EXPECT_FALSE(painter->fontFile(false).empty());
    const TextLine line = painter->set("Hello, stream", 40, true, 2000, 10);
    ASSERT_FALSE(line.coverage.empty());
    EXPECT_EQ(line.text, "Hello, stream");
    EXPECT_EQ(line.pixelSize, 40);
    EXPECT_GT(line.coverage.width, 100);
    EXPECT_GT(line.coverage.height, 30);
    EXPECT_GT(std::ranges::count_if(line.coverage.values, [](std::uint8_t value) { return value > 128; }), 100);
    EXPECT_TRUE(painter->set("   ", 40, false, 2000, 10).coverage.empty());
}

// **Smaller first, then shorter**: a long title shrinks to fit, and is cut
// with an ellipsis only once it can shrink no further.
TEST_F(TextPainterTest, ALongLineShrinksAndThenIsCut)
{
    const std::string title = "A rather long title for a stream that goes on";
    const TextLine shrunk = painter->set(title, 40, false, 500, 10);
    EXPECT_EQ(shrunk.text, title);
    EXPECT_LT(shrunk.pixelSize, 40);
    EXPECT_LE(shrunk.coverage.width, 500 + shrunk.pixelSize);

    const TextLine cut = painter->set(title, 40, false, 300, 30);
    EXPECT_EQ(cut.pixelSize, 30);
    EXPECT_TRUE(cut.text.ends_with("…")) << cut.text;
    EXPECT_LT(cut.text.size(), title.size());
}

TEST_F(TextPainterTest, ALineIsOneLineWhateverItHolds)
{
    // Line breaks and broken UTF-8 are spaces and replacement characters,
    // not a second line or a crash.
    const TextLine line = painter->set("one\ntwo\xff\xfe", 30, false, 2000, 10);
    EXPECT_EQ(line.text, "one two��");
}

TEST(AudioAnalyserTest, MeasuresWhatItIsFed)
{
    AudioAnalyser analyser(48000);
    const std::vector<float> tone = sine(1000.0, 0.5, 9600);
    analyser.feed(tone, 2);
    const AudioVisuals &seen = analyser.visuals();
    ASSERT_EQ(seen.waveform.size(), AudioAnalyser::kWaveformSamples);
    EXPECT_NEAR(seen.waveform.back(), tone[tone.size() - 2], 1e-6);
    // The RMS of a sine of 0.5 is 0.354, -9 dBFS: 0.85 of the scale.
    EXPECT_NEAR(seen.level[0], (20.0 * std::log10(0.5 / std::sqrt(2.0)) + 60.0) / 60.0, 0.02);
    EXPECT_NEAR(seen.peak[0], (20.0 * std::log10(0.5) + 60.0) / 60.0, 0.02);
    const auto loudest = std::ranges::max_element(seen.spectrum) - seen.spectrum.begin();
    // The band 1 kHz is in: the 48 bands from 40 Hz to 16 kHz are spaced
    // evenly by the logarithm.
    const double expected = std::log(1000.0 / 40.0) / std::log(16000.0 / 40.0) * AudioAnalyser::kSpectrumBands;
    EXPECT_NEAR(static_cast<double>(loudest), expected, 1.5);
}

TEST(AudioAnalyserTest, FallsBackWithTimeNotWithCalls)
{
    AudioAnalyser analyser(48000);
    analyser.feed(sine(1000.0, 1.0, 4800), 2);
    const float peak = analyser.visuals().peak[0];
    // Asking again with nothing new: the same peak, no time has passed.
    EXPECT_FLOAT_EQ(analyser.visuals().peak[0], peak);
    // Half a second of silence: a third of the way down.
    analyser.feed(std::vector<float>(48000, 0.0f), 2);
    EXPECT_NEAR(analyser.visuals().peak[0], peak - 0.5f / 1.5f, 0.01f);
}

class CompositorTest : public ::testing::Test
{
protected:
    [[nodiscard]] Compositor make()
    {
        return Compositor(640, 360, painter.get());
    }

    std::unique_ptr<TextPainter> painter = TextPainter::create();
};

// **The words' colour comes from what is behind them.**
TEST_F(CompositorTest, ChoosesTheTextColourByWhatIsBehindIt)
{
    Compositor compositor = make();
    Scene scene;
    scene.title = "Title";
    scene.background = Rgb{10, 10, 10};
    compositor.setScene(scene);
    EXPECT_GT(lumaOf(compositor.textColour()), 0.8);
    scene.background = Rgb{240, 240, 230};
    compositor.setScene(scene);
    EXPECT_LT(lumaOf(compositor.textColour()), 0.2);
}

TEST_F(CompositorTest, DrawsTheWordsWhereTheyGo)
{
    if (!TextPainter::create()) {
        GTEST_SKIP() << "this system has no fonts";
    }
    Compositor compositor = make();
    Scene scene;
    scene.title = "Title";
    scene.subtitle = "Subtitle";
    scene.track = "Artist - Song";
    scene.visualisation = Visualisation::None;
    compositor.setScene(scene);
    AudioAnalyser analyser(48000);
    const Canvas &frame = compositor.render(analyser.visuals());
    EXPECT_GT(differing(frame, compositor.titleArea(), scene.background), 500);
    // Nothing anywhere else.
    EXPECT_EQ(differing(frame, compositor.visualArea(), scene.background), 0);
    EXPECT_EQ(differing(frame, compositor.logoArea(), scene.background), 0);
}

TEST_F(CompositorTest, EachVisualisationMovesWithTheSound)
{
    Compositor compositor = make();
    Scene scene;
    for (const Visualisation visualisation : {Visualisation::Waveform, Visualisation::Spectrum, Visualisation::Meter}) {
        scene.visualisation = visualisation;
        compositor.setScene(scene);
        AudioAnalyser quiet(48000);
        quiet.feed(std::vector<float>(9600, 0.0f), 2);
        Canvas still(compositor.width(), compositor.height());
        still.copyFrom(compositor.render(quiet.visuals()));
        AudioAnalyser loud(48000);
        loud.feed(sine(440.0, 0.8, 9600), 2);
        const Canvas &moved = compositor.render(loud.visuals());
        const Rect area = compositor.visualArea();
        int changed = 0;
        for (int y = area.y; y < area.y + area.height; ++y) {
            for (int x = area.x; x < area.x + area.width; ++x) {
                changed += moved.at(x, y) == still.at(x, y) ? 0 : 1;
            }
        }
        EXPECT_GT(changed, 500) << static_cast<int>(visualisation);
    }
}

TEST_F(CompositorTest, DrawsTheBackgroundAndTheLogo)
{
    Folder folder;
    ASSERT_TRUE(writePng(folder / "back.png", 64, 36, Rgb{0, 0, 200}));
    ASSERT_TRUE(writePng(folder / "logo.png", 50, 50, Rgb{250, 0, 0}));
    Compositor compositor = make();
    Scene scene;
    scene.visualisation = Visualisation::None;
    scene.backgroundImage = folder / "back.png";
    scene.logo = folder / "logo.png";
    compositor.setScene(scene);
    AudioAnalyser analyser(48000);
    const Canvas &frame = compositor.render(analyser.visuals());
    // The picture covers the frame, the logo sits in its corner.
    EXPECT_NEAR(frame.at(compositor.width() - 2, compositor.height() - 2).b, 200, 3);
    const Rect logo = compositor.logoArea();
    EXPECT_NEAR(frame.at(logo.x + 5, logo.y + logo.height / 2).r, 250, 3);
    // Light words over a dark blue.
    EXPECT_GT(lumaOf(compositor.textColour()), 0.8);
}
