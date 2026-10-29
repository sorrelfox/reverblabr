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
#include <cstddef>
#include <span>
#include <vector>

namespace rb::dsp
{

/// Autoregressive (linear-prediction) modelling of a short stretch of
/// sound: each sample predicted from the \a order before it,
///
///     x[n] ≈ a1 x[n−1] + a2 x[n−2] + … + ap x[n−p]
///
/// and what that leaves over, the prediction error, which is small and
/// noise-like for music and speech and large where something the model
/// cannot predict — a click — has been added.
///
/// The model is kept as its **error filter** b = [1, −a1, …, −ap], so that
/// the error is e[n] = Σ b[k] x[n−k].
///
/// Everything is sized in prepare(); the rest allocates nothing and takes
/// no lock, so a block may call it from process().
class Autoregressive
{
public:
    /// Sizes the work space for a model of \a order and stretches of up to
    /// \a longestGap unknown samples. Not on the audio thread.
    void prepare(std::size_t order, std::size_t longestGap)
    {
        m_order = order;
        m_longestGap = longestGap;
        m_correlation.assign(order + 1, 0.0);
        m_filter.assign(order + 1, 0.0);
        m_filter[0] = 1.0;
        m_scratch.assign(order + 1, 0.0);
        m_filterCorrelation.assign(order + 1, 0.0);
        m_matrix.assign(longestGap * longestGap, 0.0);
        m_right.assign(longestGap, 0.0);
        m_known.assign(longestGap + order, 0.0);
    }

    [[nodiscard]] std::size_t order() const noexcept RB_NONBLOCKING
    {
        return m_order;
    }

    [[nodiscard]] std::size_t longestGap() const noexcept RB_NONBLOCKING
    {
        return m_longestGap;
    }

    /// The error filter b, b[0] = 1.
    [[nodiscard]] std::span<const double> filter() const noexcept RB_NONBLOCKING
    {
        return m_filter;
    }

    /// Fits the model to \a signal, weighted by \a window (the same length)
    /// by the autocorrelation method: Levinson–Durbin on the windowed
    /// signal's autocorrelation, which always gives a stable model. False,
    /// and the model left as "no prediction" (b = [1, 0, …]), for silence.
    bool fit(std::span<const float> signal, std::span<const float> window) noexcept RB_NONBLOCKING
    {
        const std::size_t size = std::min(signal.size(), window.size());
        for (std::size_t lag = 0; lag <= m_order; ++lag) {
            double sum = 0.0;
            for (std::size_t n = lag; n < size; ++n) {
                sum +=
                    static_cast<double>(signal[n]) * window[n] * static_cast<double>(signal[n - lag]) * window[n - lag];
            }
            m_correlation[lag] = sum;
        }
        std::fill(m_filter.begin(), m_filter.end(), 0.0);
        m_filter[0] = 1.0;
        // A whisper of white noise under the signal (a 40 dB floor): keeps
        // the recursion well away from a model so sharp it rings on its
        // own, as a pure tone's otherwise would.
        double error = m_correlation[0] * (1.0 + 1.0e-4);
        if (!(error > 1.0e-20)) {
            return false;
        }
        // Levinson–Durbin, on the error filter directly: b[1..i] are the
        // negated predictor coefficients of order i.
        for (std::size_t i = 1; i <= m_order; ++i) {
            double acc = m_correlation[i];
            for (std::size_t k = 1; k < i; ++k) {
                acc += m_filter[k] * m_correlation[i - k];
            }
            const double reflection = -acc / error;
            std::copy(m_filter.begin(), m_filter.begin() + static_cast<std::ptrdiff_t>(i), m_scratch.begin());
            for (std::size_t k = 1; k < i; ++k) {
                m_filter[k] = m_scratch[k] + reflection * m_scratch[i - k];
            }
            m_filter[i] = reflection;
            error *= 1.0 - reflection * reflection;
            if (!(error > 0.0)) {
                break;
            }
        }
        return true;
    }

    /// The prediction error at \a n: Σ b[k] x[n−k]. \a n must be at least
    /// order().
    [[nodiscard]] double error(std::span<const float> x, std::size_t n) const noexcept RB_NONBLOCKING
    {
        double sum = 0.0;
        for (std::size_t k = 0; k <= m_order; ++k) {
            sum += m_filter[k] * static_cast<double>(x[n - k]);
        }
        return sum;
    }

    /// Fills \a x[start, start + length) with the samples that fit the
    /// model best given every other sample: **least-squares autoregressive
    /// interpolation**, which minimises the prediction error over every
    /// row that sees one of the unknowns. It needs order() known samples
    /// on each side of the gap, and a gap no longer than longestGap();
    /// false, and \a x untouched, otherwise.
    ///
    /// With b the error filter, the rows that see the gap give normal
    /// equations whose matrix is the error filter's autocorrelation laid
    /// out as a band of width order() — a banded Cholesky solves them in
    /// O(length × order²).
    bool interpolate(std::span<float> x, std::size_t start, std::size_t length) noexcept RB_NONBLOCKING
    {
        const std::size_t p = m_order;
        if (length == 0 || length > m_longestGap || start < p || start + length + p > x.size()) {
            return false;
        }
        for (std::size_t lag = 0; lag <= p; ++lag) {
            double sum = 0.0;
            for (std::size_t k = 0; k + lag <= p; ++k) {
                sum += m_filter[k] * m_filter[k + lag];
            }
            m_filterCorrelation[lag] = sum;
        }
        // The error the known samples alone leave, over the rows that see
        // the gap: n from start to start + length − 1 + p.
        const std::size_t rows = length + p;
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t n = start + row;
            double sum = 0.0;
            for (std::size_t k = 0; k <= p; ++k) {
                const std::size_t at = n - k;
                if (at < start || at >= start + length) {
                    sum += m_filter[k] * static_cast<double>(x[at]);
                }
            }
            m_known[row] = sum;
        }
        for (std::size_t i = 0; i < length; ++i) {
            double sum = 0.0;
            for (std::size_t k = 0; k <= p; ++k) {
                sum += m_filter[k] * m_known[i + k];
            }
            m_right[i] = -sum;
        }
        // Banded Cholesky, L L^T = M, M[i][j] = R_b(|i − j|), in the lower
        // half of m_matrix.
        const auto at = [this, length](std::size_t row, std::size_t column) -> double & {
            return m_matrix[row * length + column];
        };
        for (std::size_t j = 0; j < length; ++j) {
            const std::size_t from = j > p ? j - p : 0;
            double diagonal = m_filterCorrelation[0];
            for (std::size_t k = from; k < j; ++k) {
                diagonal -= at(j, k) * at(j, k);
            }
            if (!(diagonal > 0.0)) {
                return false;
            }
            const double root = std::sqrt(diagonal);
            at(j, j) = root;
            const std::size_t last = std::min(length - 1, j + p);
            for (std::size_t i = j + 1; i <= last; ++i) {
                const std::size_t start2 = i > p ? i - p : 0;
                double value = m_filterCorrelation[i - j];
                for (std::size_t k = std::max(from, start2); k < j; ++k) {
                    value -= at(i, k) * at(j, k);
                }
                at(i, j) = value / root;
            }
        }
        // Forward, then back.
        for (std::size_t i = 0; i < length; ++i) {
            const std::size_t from = i > p ? i - p : 0;
            double value = m_right[i];
            for (std::size_t k = from; k < i; ++k) {
                value -= at(i, k) * m_right[k];
            }
            m_right[i] = value / at(i, i);
        }
        for (std::size_t i = length; i-- > 0;) {
            const std::size_t last = std::min(length - 1, i + p);
            double value = m_right[i];
            for (std::size_t k = i + 1; k <= last; ++k) {
                value -= at(k, i) * m_right[k];
            }
            m_right[i] = value / at(i, i);
        }
        for (std::size_t i = 0; i < length; ++i) {
            x[start + i] = static_cast<float>(m_right[i]);
        }
        return true;
    }

private:
    std::size_t m_order = 0;
    std::size_t m_longestGap = 0;
    std::vector<double> m_correlation;
    std::vector<double> m_filter;
    std::vector<double> m_scratch;
    std::vector<double> m_filterCorrelation;
    std::vector<double> m_matrix;
    std::vector<double> m_right;
    std::vector<double> m_known;
};

} // namespace rb::dsp
