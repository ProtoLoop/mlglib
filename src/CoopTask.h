#pragma once

#include "Result.h"

#include <initializer_list>
#include <span>
#include <vector>

template<typename... StartParams>
class ICoopTask;
class CoopTaskBatch;

class ICoopTaskBase
{
public:
    ICoopTaskBase() = default;
    virtual ~ICoopTaskBase();
    ICoopTaskBase(const ICoopTaskBase&) = delete;
    ICoopTaskBase& operator=(const ICoopTaskBase&) = delete;
    ICoopTaskBase(ICoopTaskBase&&) = delete;
    ICoopTaskBase& operator=(ICoopTaskBase&&) = delete;

    /// Call only after Start(). Returns true while the task needs more updates.
    /// False means it has finished, not necessarily that it succeeded.
    bool IsRunning() const;

    /// Calls OnUpdate() once on this thread. Call only while IsRunning() returns true.
    void Update();

    /// Does the work it can and returns without waiting. Update() calls this once.
    /// Call SetComplete() when finished.
    virtual void OnUpdate() = 0;

    /// Marks a running task as finished. IsRunning() will return false.
    /// The derived task keeps track of whether the work succeeded or failed.
    void SetComplete();

private:
    template<typename... StartParams>
    friend class ICoopTask;

    friend class CoopTaskBatch;

    enum class Stage
    {
        None,
        Running,
        Complete
    };

    bool WasStarted() const;

    void SetRunning();

    Stage m_Stage{ Stage::None };
};

/// A cooperative task that executes over multiple frames.
/// Call Start() once, then call Update() while IsRunning() returns true.
/// Let a started task finish before destroying it. An unstarted task can be destroyed.
template<typename... StartParams>
class ICoopTask : public ICoopTaskBase
{
public:
    ICoopTask() = default;
    ~ICoopTask() override = default;
    ICoopTask(const ICoopTask&) = delete;
    ICoopTask& operator=(const ICoopTask&) = delete;
    ICoopTask(ICoopTask&&) = delete;
    ICoopTask& operator=(ICoopTask&&) = delete;

    /// Call once to start the task. Sets it running, calls OnStart(), and returns its result.
    /// If OnStart() fails or calls SetComplete(), the task is complete when Start() returns.
    Result<> Start(StartParams... params)
    {
        MLG_CHECKV(!WasStarted(), "Task has already been started.");

        SetRunning();

        Result<> result = OnStart(params...);
        if(!result)
        {
            SetComplete();
        }
        return result;
    }

protected:
    /// Sets up the work. Start() calls this with the task already running.
    /// Call SetComplete() if the work finishes here.
    /// Do not return failure while child tasks or other work still need updates.
    virtual Result<> OnStart(StartParams... params) = 0;
};

/// Manages a group of tasks until they have all finished.
/// Tasks must be started BEFORE calling Start() on the batch.
/// The task batch DOES NOT call Start() on sub-tasks.
/// Supply at least one task and no null pointers. Keep the tasks alive and at the same
/// addresses until the batch is destroyed. The batch does not own them.
/// The batch will run until all tasks have finished.
/// The caller handles failures and collects any results. Batches can also be tasks.
class CoopTaskBatch : public ICoopTask<>
{
public:
    CoopTaskBatch(std::initializer_list<ICoopTask*> tasks);
    explicit CoopTaskBatch(std::span<ICoopTask*> tasks);
    explicit CoopTaskBatch(std::vector<ICoopTask*> tasks);

    ~CoopTaskBatch() override = default;
    CoopTaskBatch() = default;
    CoopTaskBatch(const CoopTaskBatch&) = delete;
    CoopTaskBatch& operator=(const CoopTaskBatch&) = delete;
    CoopTaskBatch(CoopTaskBatch&&) = delete;
    CoopTaskBatch& operator=(CoopTaskBatch&&) = delete;

private:
    enum class Stage
    {
        None,
        Running,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Stage m_Stage{ Stage::None };
    std::vector<ICoopTask*> m_Tasks;
};