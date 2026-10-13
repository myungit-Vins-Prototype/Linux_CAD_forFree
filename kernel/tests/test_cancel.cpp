#include "fk_test.h"
#include "fk_cancel.h"
#include "fk_parallel.h"
#include "fk_curve.h"

#include <condition_variable>
#include <mutex>

using namespace ForgeCad::Kernel;

FK_TEST(cancel_scope_and_evaluation) {
    auto token = std::make_shared<std::atomic<bool>>(true);
    {
        CancellationScope scope(token);
        bool cancelled = false;
        try { cancellationPoint(); } catch (const OperationCancelled &) { cancelled = true; }
        FK_CHECK(cancelled);
        cancelled = false;
        Line<3> line(Vec3(0, 0, 0), Vec3(1, 0, 0));
        try { line.point(0.5); } catch (const OperationCancelled &) { cancelled = true; }
        FK_CHECK(cancelled);
        {
            CancellationScope inner({});
            cancellationPoint();
        }
        cancelled = false;
        try { cancellationPoint(); } catch (const OperationCancelled &) { cancelled = true; }
        FK_CHECK(cancelled);
    }
    FK_CHECK(!currentCancellation);
}

FK_TEST(cancel_running_parallel_workers) {
    auto token = std::make_shared<std::atomic<bool>>(false);
    std::mutex mutex;
    std::condition_variable entered;
    int workers = 0;
    std::atomic<int> exited{0};
    std::atomic<bool> cancelled{false};
    std::thread job([&] {
        CancellationScope scope(token);
        try {
            parallelFor(100, 4, [&](std::size_t) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++workers;
                    entered.notify_one();
                }
                try {
                    for (;;) { cancellationPoint(); std::this_thread::yield(); }
                } catch (...) { ++exited; throw; }
            });
        } catch (const OperationCancelled &) { cancelled = true; }
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered.wait(lock, [&] { return workers == 4; });
    }
    token->store(true);
    job.join();
    FK_CHECK(cancelled);
    FK_CHECK(exited == 4);
    FK_CHECK(workers == 4);
    // Pool cancellation cannot leak into unrelated work on the same thread.
    int completed = 0;
    parallelFor(3, 1, [&](std::size_t) { ++completed; });
    FK_CHECK(completed == 3);
}
