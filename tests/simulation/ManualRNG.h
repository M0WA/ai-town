#pragma once
#include "src/interfaces/ISimulationRNG.h"
#include <vector>
#include <initializer_list>
#include <stdexcept>
#include <string>

// ManualRNG — deterministic test double for ISimulationRNG.
// Two separate sequences: one for nextInt(), one for nextFloat().
// Using a single shared sequence causes corruption when code under test calls
// nextInt() and nextFloat() in interleaved order.
//
// floatSeq stores float values directly in [0.0, 1.0) — NOT scaled integers.
// strict=true (default): throws if either sequence is exhausted before the test ends.
// strict=false: wraps around (for tests that intentionally loop a short sequence).
// Always use strict=true in unit tests — wrap-around silently hides bugs where
// code under test calls nextInt()/nextFloat() more times than expected.
class ManualRNG : public ISimulationRNG {
public:
    // Default constructor — int seq = {0}, float seq = {0.9f}, non-strict wrap-around.
    // Used by SimulationTestBase and test fixtures that don't need specific sequences.
    // 0.9f > service_degradation_probability (0.5f) so no service degrades by accident.
    ManualRNG() : m_intSeq{0}, m_floatSeq{0.9f}, m_strict(false) {}

    explicit ManualRNG(std::initializer_list<int>   intSeq,
                       std::initializer_list<float> floatSeq = {0.5f},
                       bool strict = true)
        : m_intSeq(intSeq), m_floatSeq(floatSeq), m_strict(strict)
    {
        if (m_intSeq.empty()) {
            throw std::invalid_argument("ManualRNG: intSeq must not be empty");
        }
        if (m_floatSeq.empty()) {
            throw std::invalid_argument("ManualRNG: floatSeq must not be empty");
        }
        for (float v : m_floatSeq) {
            if (v < 0.0f || v >= 1.0f) {
                throw std::out_of_range(
                    "ManualRNG: floatSeq value out of [0.0, 1.0) range — fix test data");
            }
        }
    }

    // Returns the next value from the int preset sequence.
    // In strict mode (default): throws std::logic_error if the sequence is exhausted.
    // In non-strict mode: wraps around to the beginning of the sequence.
    // CONTRACT: The stored values ARE the literal return values — they are NOT clamped
    // to [min, max]. Tests must ensure stored values are within the expected range.
    int nextInt(int min, int max) override {
        if (m_intIdx >= m_intSeq.size()) {
            if (m_strict)
                throw std::logic_error(
                    "ManualRNG: int sequence exhausted — code under test called nextInt() "
                    "more times than the sequence length. Either add more values to the "
                    "sequence or use ManualRNG({...}, {...}, /*strict=*/false) to allow wrap-around.");
            m_intIdx = 0;  // non-strict: wrap around
        }
        int v = m_intSeq[m_intIdx++];
        if (v < min || v > max)
            throw std::out_of_range(
                "ManualRNG: stored value out of expected [min, max] range — fix test sequence data");
        return v;
    }

    // Returns the next float value directly from the float preset sequence.
    // In strict mode (default): throws std::logic_error if the sequence is exhausted.
    // In non-strict mode: wraps around.
    float nextFloat() override {
        if (m_floatIdx >= m_floatSeq.size()) {
            if (m_strict)
                throw std::logic_error(
                    "ManualRNG: float sequence exhausted — code under test called nextFloat() "
                    "more times than the sequence length.");
            m_floatIdx = 0;  // non-strict: wrap around
        }
        return m_floatSeq[m_floatIdx++];
    }

    // Asserts that both sequences have been fully consumed.
    // Call at the end of a test to catch over-provisioned sequences (test data
    // providing more values than the code under test actually calls).
    // Throws std::logic_error if any sequence has unconsumed values remaining.
    void verifyAllConsumed() const {
        if (m_intIdx != m_intSeq.size()) {
            throw std::logic_error(
                "ManualRNG: int sequence not fully consumed (" +
                std::to_string(m_intIdx) + " of " +
                std::to_string(m_intSeq.size()) + " consumed)");
        }
        if (m_floatIdx != m_floatSeq.size()) {
            throw std::logic_error(
                "ManualRNG: float sequence not fully consumed (" +
                std::to_string(m_floatIdx) + " of " +
                std::to_string(m_floatSeq.size()) + " consumed)");
        }
    }

private:
    std::vector<int>   m_intSeq;
    std::vector<float> m_floatSeq;
    size_t m_intIdx{0};
    size_t m_floatIdx{0};
    bool   m_strict{true};
};
