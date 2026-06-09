#pragma once

// Tiny CPU job system. One process-wide thread pool + a parallel_for
// primitive used by the Medium-tier particle path (and potentially
// other "iterate millions of items" subsystems later).
//
// Workers spin up lazily on first use and live for the rest of the
// process. parallel_for() splits a range into fixed-size chunks,
// dispatches one job per chunk, blocks until all chunks complete.
// No work-stealing, no priorities -- the simplest thing that meets
// the "60fps at 100k particles" target.
//
// Not exposed via host_api: this is a host-internal helper. Project
// DLLs already get parallelism via the future Heavy GPU tier, not by
// reaching for these threads themselves.

#include <zues/api.h>

#include <functional>
#include <cstdint>

namespace Engine::host {

class TaskRunner {
public:
    using JobFn = std::function<void(int begin, int end)>;

    // Process-wide singleton. First call spins up worker threads.
    static TaskRunner& instance();

    // Run `body` in parallel chunks over [begin, end). The body
    // receives a half-open sub-range. Blocks until every chunk is
    // done -- safe to call from a hot loop. When `end - begin` is
    // small (less than chunk_size) this just runs the body inline
    // on the calling thread, no synchronization cost.
    //
    // `chunk_size` is the minimum chunk; the runner may use slightly
    // larger chunks to balance against the worker count.
    void parallel_for(int begin, int end, int chunk_size, const JobFn& body);

    // Count of WORKER threads (excludes the calling thread). 0 means
    // single-threaded fallback -- parallel_for runs inline.
    int worker_count() const;

    ~TaskRunner();
private:
    TaskRunner();
    TaskRunner(const TaskRunner&)            = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    struct Impl;
    Impl* m_impl;
};

}  // namespace Engine::host
