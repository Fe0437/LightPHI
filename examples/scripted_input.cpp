/**
 * {file}
 * {brief} Shows how a consumer tests its receiver against deterministic scripted input.
 *
 * This is the pattern to copy into a consumer's own test suite. No device, no window server, and
 * no operating-system event loop is involved, so it runs anywhere including continuous integration.
 */
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

import lightphi.backend.fake;

using namespace lightphi::input;
using namespace lightphi::input::fake;

namespace
{
    /**
     * {brief} A consumer's receiver: turns delivered batches into one stroke.
     *
     * Busy is returned once to show that a receiver may refuse a delivery without losing it.
     */
    class StrokeRecorder final : public IInputReceiver
    {
      public:
        std::vector<InputSample> Points{};
        bool                     Finished{};
        bool                     RefuseOnce{};

        [[nodiscard]] DeliveryResult Receive(const InputBatch &batch) noexcept override
        {
            if (RefuseOnce)
            {
                RefuseOnce = false;
                return DeliveryResult::Busy;
            }
            // The span is borrowed for this call only, so copy what must outlive it.
            Points.insert(Points.end(), batch.Samples.begin(), batch.Samples.end());
            Finished = batch.Phase == ContactPhase::End || batch.Phase == ContactPhase::Cancel;
            return DeliveryResult::Accepted;
        }

        void SourceFailed(const InputSourceError error) noexcept override
        {
            std::cerr << InputSourceErrorMessage(error) << '\n';
        }
    };

    [[nodiscard]] InputSample Point(std::uint64_t id, double x, double y, double pressure) noexcept
    {
        return InputSample{.Id              = {.Value = id},
                           .Sequence        = id,
                           .TimeNanoseconds = id * 1000U,
                           .X               = x,
                           .Y               = y,
                           .Pressure        = pressure,
                           .Origin          = SampleOrigin::Measured};
    }
} // namespace

int main()
{
    // Declare exactly the capabilities the scripted samples use. The source rejects any sample that
    // reports a value this descriptor does not allow, so a test cannot drift from its own contract.
    const FakeInputSourceDescriptor descriptor{
        .Capabilities         = {.Available           = InputCapability::Pressure | InputCapability::Timestamp,
                                 .MaximumBatchSamples = 8U},
        .MaximumQueuedBatches = 4U};

    auto sourceResult{CreateFakeInputSource(descriptor)};
    if (!sourceResult)
    {
        std::cerr << FakeSourceErrorMessage(sourceResult.error()) << '\n';
        return 1;
    }
    auto &source{*sourceResult};

    StrokeRecorder recorder{};
    recorder.RefuseOnce = true;
    if (!source->Start(recorder))
    {
        std::cerr << "the scripted source could not be started\n";
        return 1;
    }

    // Script one stroke. Every allocation already happened in CreateFakeInputSource; Enqueue only
    // copies into that fixed storage, and nothing is delivered until DispatchNext is called.
    const std::array<InputSample, 1> opening{Point(1U, 10.0, 10.0, 0.25)};
    const std::array<InputSample, 2> middle{Point(2U, 12.0, 14.0, 0.50), Point(3U, 15.0, 19.0, 0.75)};
    const std::array<InputSample, 1> closing{Point(4U, 18.0, 22.0, 0.10)};
    const std::array<InputBatch, 3>  stroke{
        InputBatch{.Contact = {.Value = 1U}, .BatchOrdinal = 1U, .Phase = ContactPhase::Begin, .Samples = opening},
        InputBatch{.Contact = {.Value = 1U}, .BatchOrdinal = 2U, .Phase = ContactPhase::Update, .Samples = middle},
        InputBatch{.Contact = {.Value = 1U}, .BatchOrdinal = 3U, .Phase = ContactPhase::End, .Samples = closing}};

    for (const InputBatch &batch : stroke)
    {
        if (const auto queued{source->Enqueue(batch)}; !queued)
        {
            // A rejected script is a defect in the test, not in the code being tested.
            std::cerr << FakeSourceErrorMessage(queued.error()) << '\n';
            return 1;
        }
    }
    std::cout << "queued batches: " << source->QueuedBatchCount() << '\n';

    // Dispatch is explicit, so the consumer decides when delivery happens. The first attempt is
    // refused by the receiver and the exact batch stays queued for the retry.
    while (source->QueuedBatchCount() != 0U)
    {
        if (const auto dispatched{source->DispatchNext()}; !dispatched)
        {
            if (dispatched.error() == DispatchError::ReceiverBusy)
            {
                std::cout << "receiver was busy, the batch is still queued\n";
                continue;
            }
            std::cerr << DispatchErrorMessage(dispatched.error()) << '\n';
            return 1;
        }
    }
    source->Stop();

    std::cout << "points received: " << recorder.Points.size() << '\n';
    std::cout << "stroke finished: " << (recorder.Finished ? "yes" : "no") << '\n';
    // Four scripted points arrived in order, and the busy retry lost none of them.
    return recorder.Points.size() == 4U && recorder.Finished && recorder.Points.front().Id.Value == 1U &&
                   recorder.Points.back().Id.Value == 4U
               ? 0
               : 1;
}
