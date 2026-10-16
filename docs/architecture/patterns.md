# Architecture Patterns

Patterns describe approaches we use repeatedly in this project. They explain
when an approach applies, the rules that matter, and its tradeoffs. A pattern
is not a rule for every situation, but differences should be intentional.

## Cooperative Tasks

Use a cooperative task for work that spans multiple frames. The caller
starts the task and keeps updating it until it is complete.

```cpp
ThingTask task(...);
MLG_CHECK(task.Start());

while(task.IsRunning())
{
    task.Update();

    // Other work can be done here.
}
```

Call `Start()` once, then use `IsRunning()` to decide whether to call `Update()`.
A task must finish before it is destroyed. Destroying a task that was never
started is also valid.

To write a task, derive from [`ICoopTask`](../../src/CoopTask.h) and implement
`OnStart()` and `OnUpdate()`. `Start()` calls `OnStart()`, and `Update()` calls
`OnUpdate()`. The base checks that you start the task only once and update it only
while it is running.

`OnStart()` sets up the work and returns a `Result`. If it fails, the base marks
the task complete. `OnUpdate()` does the work it can and then returns without
waiting. It runs on the caller's thread. It may check whether a file read or
worker job has finished, but it must not wait for it.

Tasks usually track their progress with an enum and a `switch` in `OnUpdate()`.
Call `SetComplete()` when no more updates are needed. This can happen during
`OnStart()`, or when `OnUpdate()` reaches a finished stage. It is also fine to
handle that stage and call `SetComplete()` on the next update.

Completion means the task has finished, whether it succeeded or failed. Some
tasks provide a `Take()` method to return a result. For those tasks, call `Take()`
after `IsRunning()` becomes false and check the result. The result can only be
taken once. `Take()` is not required by `ICoopTask`.

A task can own other tasks and start and update them as part of its own work.
It must let any children it starts finish before it finishes.

Use `CoopTaskBatch` to update several tasks together. Start each child before
starting the batch. Give the batch a nonempty collection of non-null task
pointers. The batch does not own its children, so they must stay at the same
addresses and outlive it. A batch is itself a task and can be included in another
batch.

The batch finishes after all its children finish. The caller handles startup
failures and collects results. If some children fail to start, the caller must
still let successfully started children finish before destroying them.

## Valid Construction

An object should be valid and ready to use after it is constructed. Delete the
default constructor when default construction cannot produce a valid object.

When object creation can fail, instead of exposing a constructor, use a static
`Create()` function that returns a `Result<T>`. Keep the constructor private so
callers cannot bypass the creation path.

### General shape

```cpp
class Thing
{
public:
    Thing() = delete;

    static Result<Thing> Create(...);

private:
    Thing(...);
};
```
