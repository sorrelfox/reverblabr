// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

namespace rb::dsp
{

/// Second-order filters: the shapes every equaliser and filter block is
/// built from.
///
/// **One design, two ways of running it.** A shape is designed once, as the
/// analogue prototype of the well-known cookbook formulas taken through the
/// bilinear transform with the frequency warped to land where it was asked
/// for. That gives the coefficients of an ordinary biquad, and from those
/// the exact response (biquadResponse), which is what a test measures a
/// block against and what a curve on screen draws.
///
/// The same transfer function can be run two ways:
/// - **Svf**, a state-variable filter integrated by the trapezoidal rule
///   (the topology-preserving transform). Its state is the charge on two
///   integrators rather than past outputs, so **its coefficients can change
///   every sample** — a cutoff dragged across its whole range — without the
///   filter blowing up or thumping. This is what a block with a moving
///   frequency uses.
/// - **Biquad**, the transposed direct form II. Cheaper, and just as exact,
///   but only for coefficients that stay put or move slowly: a direct form
///   swapped to a very different filter mid-signal can ring out loud.
///
/// Both give the response biquadResponse() says, to rounding; the tests
/// hold them to it for every shape.
enum class FilterShape : std::uint8_t {
    LowPass,
    HighPass,
    /// A bell: lifts or cuts a band around the frequency, flat elsewhere.
    Peaking,
    /// Lifts or cuts everything below the frequency.
    LowShelf,
    /// Lifts or cuts everything above the frequency.
    HighShelf,
    /// Takes out a narrow band around the frequency and nothing else: a
    /// hum, a whistle. Its gain is ignored.
    Notch,
    /// Keeps a band around the frequency, 0 dB at its centre, and rolls off
    /// either side at 6 dB an octave. Its gain is ignored.
    BandPass,
    /// Keeps every frequency at 0 dB and turns the phase through a half turn
    /// around the frequency. Its gain is ignored. At the Butterworth Q it is
    /// exactly what a fourth-order Linkwitz–Riley crossover's two halves add
    /// up to, which is how a crossover with more than two bands keeps the
    /// bands that did not go through a split in step with those that did.
    AllPass,
};

/// The Q of a second-order Butterworth section: the flattest pass band there
/// is, 3 dB down at the frequency.
inline constexpr double kButterworthQ = 0.70710678118654752;

/// The two Qs that make a fourth-order Butterworth out of two sections in a
/// row, for a 24 dB/octave slope that is still 3 dB down at the frequency
/// and flat below it. They are 1 / (2 cos(π/8)) and 1 / (2 cos(3π/8)).
inline constexpr double kButterworth4Q1 = 0.54119610014619698;
inline constexpr double kButterworth4Q2 = 1.30656296487637653;

/// What a filter is: its shape, its frequency in hertz, its Q, and for the
/// peaking and shelving shapes how far it lifts (positive) or cuts, in dB.
/// A shelf's Q is its slope: 0.7071 is the steepest that does not overshoot.
struct FilterSpec {
    FilterShape shape = FilterShape::LowPass;
    double frequency = 1000.0;
    double q = kButterworthQ;
    double gainDb = 0.0;
};

/// The highest a filter's frequency may be, as a fraction of the sample
/// rate. The warping that puts the frequency where it was asked for goes to
/// infinity at half the rate; a little under it the filter is still sound.
inline constexpr double kMaxFrequencyRatio = 0.49;
/// The lowest, so a zero or negative frequency cannot make a filter that
/// does nothing or divides by zero.
inline constexpr double kMinFrequencyHz = 1.0;

/// \a frequency brought inside what a filter at \a sampleRate can be set to.
[[nodiscard]] inline double clampFilterFrequency(double frequency, double sampleRate) noexcept RB_NONBLOCKING
{
    return std::clamp(frequency, kMinFrequencyHz, sampleRate * kMaxFrequencyRatio);
}

/// A biquad's coefficients, divided through by a0 so that a0 is one.
/// y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2].
struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

/// The coefficients of \a spec at \a sampleRate: the cookbook's formulas,
/// with the frequency clamped to what the rate allows and a Q of at least a
/// small positive number.
[[nodiscard]] inline BiquadCoefficients designBiquad(const FilterSpec &spec, double sampleRate) noexcept RB_NONBLOCKING
{
    const double frequency = clampFilterFrequency(spec.frequency, sampleRate);
    const double q = std::max(spec.q, 1.0e-3);
    const double w0 = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    // The square root of the linear gain: a peak or a shelf is designed
    // around the half-way point in decibels.
    const double a = std::pow(10.0, spec.gainDb / 40.0);

    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    switch (spec.shape) {
    case FilterShape::LowPass:
        b0 = (1.0 - cosW0) / 2.0;
        b1 = 1.0 - cosW0;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case FilterShape::HighPass:
        b0 = (1.0 + cosW0) / 2.0;
        b1 = -(1.0 + cosW0);
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case FilterShape::Peaking:
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW0;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha / a;
        break;
    case FilterShape::LowShelf: {
        const double twoRootAAlpha = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1.0) - (a - 1.0) * cosW0 + twoRootAAlpha);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0);
        b2 = a * ((a + 1.0) - (a - 1.0) * cosW0 - twoRootAAlpha);
        a0 = (a + 1.0) + (a - 1.0) * cosW0 + twoRootAAlpha;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW0);
        a2 = (a + 1.0) + (a - 1.0) * cosW0 - twoRootAAlpha;
        break;
    }
    case FilterShape::HighShelf: {
        const double twoRootAAlpha = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1.0) + (a - 1.0) * cosW0 + twoRootAAlpha);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0);
        b2 = a * ((a + 1.0) + (a - 1.0) * cosW0 - twoRootAAlpha);
        a0 = (a + 1.0) - (a - 1.0) * cosW0 + twoRootAAlpha;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW0);
        a2 = (a + 1.0) - (a - 1.0) * cosW0 - twoRootAAlpha;
        break;
    }
    case FilterShape::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosW0;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case FilterShape::BandPass:
        // The constant-peak form: 0 dB at the centre whatever the Q.
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case FilterShape::AllPass:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cosW0;
        b2 = 1.0 + alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    }
    return BiquadCoefficients{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// What \a coefficients do to a sine at \a frequency: the gain and the phase
/// shift, as one complex number. Exact, not measured.
[[nodiscard]] inline std::complex<double>
biquadResponse(const BiquadCoefficients &coefficients, double frequency, double sampleRate) noexcept
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    // z⁻¹ and z⁻² on the unit circle.
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    const std::complex<double> numerator = coefficients.b0 + coefficients.b1 * z1 + coefficients.b2 * z2;
    const std::complex<double> denominator = 1.0 + coefficients.a1 * z1 + coefficients.a2 * z2;
    return numerator / denominator;
}

/// The gain \a coefficients give a sine at \a frequency, in decibels.
[[nodiscard]] inline double
biquadMagnitudeDb(const BiquadCoefficients &coefficients, double frequency, double sampleRate) noexcept
{
    const double magnitude = std::abs(biquadResponse(coefficients, frequency, sampleRate));
    // Far enough down to mean "nothing" without being minus infinity, which
    // a curve cannot draw and a comparison cannot subtract.
    return 20.0 * std::log10(std::max(magnitude, 1.0e-15));
}

/// The same for \a spec, designed at \a sampleRate.
[[nodiscard]] inline double filterMagnitudeDb(const FilterSpec &spec, double frequency, double sampleRate) noexcept
{
    return biquadMagnitudeDb(designBiquad(spec, sampleRate), frequency, sampleRate);
}

/// A biquad in transposed direct form II, for one channel.
///
/// For filters whose coefficients hold still, or glide slowly; see the note
/// at the top of this file for why a moving cutoff wants Svf instead.
class Biquad
{
public:
    void setCoefficients(const BiquadCoefficients &coefficients) noexcept RB_NONBLOCKING
    {
        m_c = coefficients;
    }

    [[nodiscard]] const BiquadCoefficients &coefficients() const noexcept
    {
        return m_c;
    }

    /// Forgets the signal so far, for a start with nothing ringing.
    void reset() noexcept RB_NONBLOCKING
    {
        m_s1 = 0.0;
        m_s2 = 0.0;
    }

    [[nodiscard]] float process(float input) noexcept RB_NONBLOCKING
    {
        const double x = input;
        const double y = m_c.b0 * x + m_s1;
        m_s1 = m_c.b1 * x - m_c.a1 * y + m_s2;
        m_s2 = m_c.b2 * x - m_c.a2 * y;
        return static_cast<float>(y);
    }

private:
    BiquadCoefficients m_c;
    // Double, so a low frequency at a high rate — where the poles crowd
    // against the unit circle — keeps its precision.
    double m_s1 = 0.0;
    double m_s2 = 0.0;
};

/// A state-variable filter's coefficients: the integrators' gains, and how
/// much of the input, the band-pass and the low-pass make up the output.
struct SvfCoefficients {
    double a1 = 1.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double m0 = 1.0;
    double m1 = 0.0;
    double m2 = 0.0;
};

/// The coefficients that make an Svf give exactly what designBiquad(\a spec)
/// gives. Cheap enough to work out every sample while a frequency moves: one
/// tangent, and a square root and a power for the shapes with a gain.
[[nodiscard]] inline SvfCoefficients designSvf(const FilterSpec &spec, double sampleRate) noexcept RB_NONBLOCKING
{
    const double frequency = clampFilterFrequency(spec.frequency, sampleRate);
    const double q = std::max(spec.q, 1.0e-3);
    // The frequency warped so the digital filter's corner lands on it.
    double g = std::tan(std::numbers::pi * frequency / sampleRate);
    double k = 1.0 / q;
    const double a = std::pow(10.0, spec.gainDb / 40.0);

    SvfCoefficients c;
    switch (spec.shape) {
    case FilterShape::LowPass:
        c.m0 = 0.0;
        c.m1 = 0.0;
        c.m2 = 1.0;
        break;
    case FilterShape::HighPass:
        c.m0 = 1.0;
        c.m1 = -k;
        c.m2 = -1.0;
        break;
    case FilterShape::Peaking:
        k = 1.0 / (q * a);
        c.m0 = 1.0;
        c.m1 = k * (a * a - 1.0);
        c.m2 = 0.0;
        break;
    case FilterShape::LowShelf:
        g /= std::sqrt(a);
        c.m0 = 1.0;
        c.m1 = k * (a - 1.0);
        c.m2 = a * a - 1.0;
        break;
    case FilterShape::HighShelf:
        g *= std::sqrt(a);
        c.m0 = a * a;
        c.m1 = k * (1.0 - a) * a;
        c.m2 = 1.0 - a * a;
        break;
    case FilterShape::Notch:
        c.m0 = 1.0;
        c.m1 = -k;
        c.m2 = 0.0;
        break;
    case FilterShape::BandPass:
        c.m0 = 0.0;
        c.m1 = k;
        c.m2 = 0.0;
        break;
    case FilterShape::AllPass:
        // The input less twice the constant-peak band-pass.
        c.m0 = 1.0;
        c.m1 = -2.0 * k;
        c.m2 = 0.0;
        break;
    }
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

/// A state-variable filter integrated by the trapezoidal rule, for one
/// channel. The coefficients are passed in with each sample, so a block
/// gliding a frequency works them out once for all its channels.
class Svf
{
public:
    /// Forgets the signal so far, for a start with nothing ringing.
    void reset() noexcept RB_NONBLOCKING
    {
        m_ic1 = 0.0;
        m_ic2 = 0.0;
    }

    [[nodiscard]] float process(float input, const SvfCoefficients &c) noexcept RB_NONBLOCKING
    {
        const double v0 = input;
        const double v3 = v0 - m_ic2;
        const double v1 = c.a1 * m_ic1 + c.a2 * v3;
        const double v2 = m_ic2 + c.a2 * m_ic1 + c.a3 * v3;
        m_ic1 = 2.0 * v1 - m_ic1;
        m_ic2 = 2.0 * v2 - m_ic2;
        return static_cast<float>(c.m0 * v0 + c.m1 * v1 + c.m2 * v2);
    }

private:
    // The integrators' state. Double for the same reason as Biquad's.
    double m_ic1 = 0.0;
    double m_ic2 = 0.0;
};

} // namespace rb::dsp
