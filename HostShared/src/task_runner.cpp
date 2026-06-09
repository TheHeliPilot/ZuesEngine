#include <zues/host/task_runner.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Implementation notes:
//
// One pool, lazy worker startup. parallel_for() pushes (worker_count)
// chunks onto a queue, atomically counts down as workers finish, and
// the calling thread also processes one chunk while waiting (steals
// from the front of the queue). On chunk_count <= 1 we just run
// inline -- no condvars, no atomics, no allocs.
//
// Workers idle on a condvar; signaled per-batch (notify_all). Cheap
// when the engine isn't doing parallel work.

namespace Engine::host {

namespace {

struct Job {
    TaskRunner::JobFn const* body;     // shared body (the parallel_for arg)
    int begin;
    int end;
};

}  // namespace

struct TaskRunner::Impl {
    std::vector<std::thread>   workers;
    std::mutex                 mu;
    std::condition_variable    cv_work;
    std::condition_variable    cv_done;
    std::queue<Job>            queue;
    std::atomic<int>           in_flight{0};
    bool                       shutting_down = false;

    void worker_loop() {
        for (;;) {
            Job j{};
            {
                std::unique_lock<std::mutex> lk(mu);
                cv_work.wait(lk, [this] {
                    return shutting_down || !queue.empty();
                });
                if (shutting_down && queue.empty()) return;
                j = queue.front(); queue.pop();
            }
            (*j.body)(j.begin, j.end);
            if (in_flight.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lk(mu);
                cv_done.notify_all();
            }
        }
    }
};

TaskRunner::TaskRunner() : m_impl(new Impl) {
    // Reserve one core for the main + render thread; leave the rest
    // as workers. min 0 (=> inline fallback) on 1-core machines.
    int hw = (int)std::thread::hardware_concurrency();
    if (hw <= 0) hw = 1;
    const int n = std::max(0, hw - 1);
    m_impl->workers.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        m_impl->workers.emplace_back([this] { m_impl->worker_loop(); });
    }
}

TaskRunner::~TaskRunner() {
    {
        std::lock_guard<std::mutex> lk(m_impl->mu);
        m_impl->shutting_down = true;
    }
    m_impl->cv_work.notify_all();
    for (auto& t : m_impl->workers) if (t.joinable()) t.join();
    delete m_impl;
}

TaskRunner& TaskRunner::instance() {
    static TaskRunner g;
    return g;
}

int TaskRunner::worker_count() const {
    return (int)m_impl->workers.size();
}

void TaskRunner::parallel_for(int begin, int end, int chunk_size,
                                const JobFn& body) {
    const int n = end - begin;
    if (n <= 0) return;

    // Inline fallback: tiny range OR no workers (1-core machine).
    if (chunk_size <= 0) chunk_size = 4096;
    if (n <= chunk_size || m_impl->workers.empty()) {
        body(begin, end);
        return;
    }

    // Split into roughly (worker_count + 1) chunks so the calling
    // thread also pulls one. Round chunk size UP so we hit the worker
    // count even with non-divisible ranges.
    const int W       = (int)m_impl->workers.size() + 1;
    const int per     = std::max(chunk_size, (n + W - 1) / W);
    const int n_chunks = (n + per - 1) / per;
    if (n_chunks <= 1) { body(begin, end); return; }

    m_impl->in_flight.store(n_chunks);
    {
        std::lock_guard<std::mutex> lk(m_impl->mu);
        for (int c = 0; c < n_chunks; ++c) {
            const int b = begin + c * per;
            const int e = std::min(end, b + per);
            m_impl->queue.push(Job{ &body, b, e });
        }
    }
    m_impl->cv_work.notify_all();

    // Help out from the calling thread.
    for (;;) {
        Job j{};
        {
            std::unique_lock<std::mutex> lk(m_impl->mu);
            if (m_impl->queue.empty()) break;
            j = m_impl->queue.front(); m_impl->queue.pop();
        }
        (*j.body)(j.begin, j.end);
        if (m_impl->in_flight.fetch_sub(1) == 1) {
            std::lock_guard<std::mutex> lk(m_impl->mu);
            m_impl->cv_done.notify_all();
        }
    }

    // Wait for any chunks workers picked up.
    std::unique_lock<std::mutex> lk(m_impl->mu);
    m_impl->cv_done.wait(lk, [this] {
        return m_impl->in_flight.load() == 0;
    });
}

}  // namespace Engine::host
