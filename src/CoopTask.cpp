#include "CoopTask.h"

ICoopTaskBase::~ICoopTaskBase()
{
    MLG_ASSERT(Stage::Running != m_Stage, "Destroying a running task");
}

bool
ICoopTaskBase::IsRunning() const
{
    if(!MLG_VERIFY(WasStarted(), "Task is not started"))
    {
        return false;
    }

    return m_Stage == Stage::Running;
}

void
ICoopTaskBase::Update()
{
    if(MLG_VERIFY(IsRunning(), "Task is not running"))
    {
        OnUpdate();
    }
}

void
ICoopTaskBase::SetComplete()
{
    if(MLG_VERIFY(m_Stage == Stage::Running, "Task is not running."))
    {
        m_Stage = Stage::Complete;
    }
}

// private:

bool
ICoopTaskBase::WasStarted() const
{
    return m_Stage != Stage::None;
}

void
ICoopTaskBase::SetRunning()
{
    m_Stage = Stage::Running;
}

// CoopTaskBatch

CoopTaskBatch::CoopTaskBatch(std::initializer_list<ICoopTask*> tasks)
    : CoopTaskBatch(std::vector<ICoopTask*>(tasks))
{
}

CoopTaskBatch::CoopTaskBatch(std::span<ICoopTask*> tasks)
    : CoopTaskBatch(std::vector<ICoopTask*>(tasks.begin(), tasks.end()))
{
}

CoopTaskBatch::CoopTaskBatch(std::vector<ICoopTask*> tasks)
    : m_Tasks(std::move(tasks))
{
}

// private:

Result<>
CoopTaskBatch::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task is already in progress");

    MLG_CHECKV(!m_Tasks.empty(), "No tasks provided");

    for(size_t i = 0; i < m_Tasks.size();)
    {
        MLG_ASSERT(m_Tasks[i] != nullptr, "Task is null");
        if(MLG_VERIFY(m_Tasks[i]->WasStarted(), "Task was not started"))
        {
            ++i;
        }
        else
        {
            m_Tasks[i] = m_Tasks.back();
            m_Tasks.pop_back();
        }
    }

    m_Stage = Stage::Running;

    return Result<>::Ok;
}

void
CoopTaskBatch::OnUpdate()
{
    for(size_t i = 0; i < m_Tasks.size();)
    {
        ICoopTask* task = m_Tasks[i];
        if(task->IsRunning())
        {
            task->Update();
            ++i;
        }
        else
        {
            m_Tasks[i] = m_Tasks.back();
            m_Tasks.pop_back();
        }
    }

    if(m_Tasks.empty())
    {
        m_Stage = Stage::Succeeded;
        SetComplete();
    }
}