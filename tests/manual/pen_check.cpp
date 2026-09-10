/**
 * {file}
 * {brief} Checks the selected LightPHI backend against a real pen, by hand.
 *
 * A person draws in a window while every delivery is checked with ValidateBatch and timed against
 * the event that produced it. The result is a report tagged with the machine and tablet it came
 * from. It needs a person and a device, so it is never part of the automated suite; the steps are in
 * docs/MANUAL_CHECKS.md.
 */
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <time.h>

import lightphi.backend;

extern "C"
{
    void  lightphi_pen_check_open(const char *recordPath);
    void  lightphi_pen_check_run();
    char *lightphi_pen_check_hardware();
}

namespace
{
    using namespace lightphi::input;

    /** {brief} Samples whose latency is kept; a long session keeps its first ones. */
    constexpr std::size_t MaximumTimedSamples{1U << 20U};

    /** {brief} Judges every delivery and times every sample, without allocating while drawing. */
    class CheckingReceiver final : public IInputReceiver
    {
      public:
        explicit CheckingReceiver(const IInputSource &source) noexcept : _source{source} {}

        [[nodiscard]] DeliveryResult Receive(const InputBatch &batch) noexcept override
        {
            // AppKit stamps events with the uptime clock that excludes sleep, which is this one.
            const std::uint64_t now{clock_gettime_nsec_np(CLOCK_UPTIME_RAW)};
            const auto          capabilities{_source.Capabilities()};
            _seen = _seen | capabilities.Available;
            const auto next{ValidateBatch(batch, capabilities, _state)};
            if (!next && !_violation)
            {
                _violation      = next.error();
                _violationBatch = Batches;
            }
            _state = next.value_or(DeliveryState{});
            ++Batches;
            Contacts += batch.Phase == ContactPhase::Begin ? 1U : 0U;
            Cancelled += batch.Phase == ContactPhase::Cancel ? 1U : 0U;
            for (const InputSample &sample : batch.Samples)
            {
                ++Samples;
                _maximumPressure = std::max(_maximumPressure, sample.Pressure);
                if (sample.TimeNanoseconds != 0U && sample.TimeNanoseconds <= now && _timed < _latencies.size())
                {
                    _latencies[_timed++] = now - sample.TimeNanoseconds;
                }
            }
            return DeliveryResult::Accepted;
        }

        void SourceFailed(const InputSourceError error) noexcept override
        {
            _failure = InputSourceErrorMessage(error);
        }

        void Report(std::ostream &out, const char *hardware) noexcept
        {
            out << "| Hardware | " << hardware << " |\n|---|---|\n";
            out << "| Capabilities seen |";
            for (std::uint16_t bit{1U}; bit != 0U && bit <= static_cast<std::uint16_t>(AllInputCapabilities);
                 bit = static_cast<std::uint16_t>(bit << 1U))
            {
                const auto capability{static_cast<InputCapability>(bit)};
                if ((_seen & capability) == capability)
                {
                    out << ' ' << InputCapabilityName(capability);
                }
            }
            out << " |\n| Highest pressure | " << _maximumPressure << " |\n";
            out << "| Contacts / cancelled / batches / samples | " << Contacts << " / " << Cancelled << " / " << Batches
                << " / " << Samples << " |\n";
            out << "| Delivery rules | ";
            if (_violation)
            {
                out << "broken at batch " << _violationBatch << ": " << BatchValidationMessage(_violation->Reason)
                    << " |\n";
            }
            else
            {
                out << "every batch valid |\n";
            }
            if (!_failure.empty())
            {
                out << "| Source failure | " << _failure << " |\n";
            }
            const std::span timed{_latencies.data(), _timed};
            std::ranges::sort(timed);
            const auto percentile{
                [&timed](const std::size_t percent)
                {
                    return timed.empty() ? 0.0 : static_cast<double>(timed[(timed.size() - 1U) * percent / 100U]) / 1e6;
                }};
            out << "| Event to receiver, ms (p50 / p95 / max over " << timed.size() << ") | " << percentile(50U)
                << " / " << percentile(95U) << " / " << percentile(100U) << " |\n";
        }

        [[nodiscard]] bool Passed() const noexcept
        {
            return !_violation && _failure.empty() && Samples != 0U;
        }

        std::uint64_t Contacts{};
        std::uint64_t Cancelled{};
        std::uint64_t Batches{};
        std::uint64_t Samples{};

      private:
        const IInputSource                            &_source;
        DeliveryState                                  _state{};
        InputCapability                                _seen{};
        double                                         _maximumPressure{};
        std::optional<BatchValidationError>            _violation{};
        std::uint64_t                                  _violationBatch{};
        std::string_view                               _failure{};
        std::array<std::uint64_t, MaximumTimedSamples> _latencies{};
        std::size_t                                    _timed{};
    };
} // namespace

int main(int argc, char **argv)
{
    // --pointer accepts an ordinary mouse; --record FILE saves the raw events as a replayable fixture.
    bool            pointer{};
    const char     *recordPath{};
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    for (std::size_t index{1U}; index < arguments.size(); ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument == "--pointer")
        {
            pointer = true;
        }
        else if (argument == "--record" && index + 1U < arguments.size())
        {
            recordPath = arguments[++index];
        }
    }

    auto created{InputSource::Create({.AcceptPointerFallback = pointer})};
    if (!created)
    {
        std::cerr << InputSourceCreationErrorMessage(created.error()) << '\n';
        return 1;
    }
    auto &source{*created};
    // The receiver keeps a million latencies; it lives on the heap, not the stack.
    const auto receiver{std::make_unique<CheckingReceiver>(*source)};

    lightphi_pen_check_open(recordPath);
    if (const auto started{source->Start(*receiver)}; !started)
    {
        std::cerr << InputSourceErrorMessage(started.error()) << '\n';
        return 1;
    }
    lightphi_pen_check_run();
    source->Stop();

    char *hardware{lightphi_pen_check_hardware()};
    receiver->Report(std::cout, hardware != nullptr ? hardware : "unknown");
    std::free(hardware); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): strdup's string
    return receiver->Passed() ? 0 : 1;
}
