#pragma once

#include "Containers.h"
#include "CoopTask.h"
#include "FilePath.h"
#include "Result.h"
#include "TextureHelper.h"

#include <cstddef>
#include <optional>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class System;

/// Loads textures. A failed load uses the default texture for that slot.
/// Keep calling FileFetcher::ProcessCompletions() while this task is running.
/// Calling Update() alone does not process file completions.
class TextureFetcher : public ICoopTask<>
{
public:

    TextureFetcher(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const DirectoryPath& parentPath,
        std::vector<RelativeFilePath> texturePaths);
    ~TextureFetcher() override = default;   // The base class will verify completion
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    /// Returns the collection of textures if the task succeeded, otherwise returns an error.
    /// This method will invalidate the task, so it can only be called once.
    Result<std::vector<wgpu::Texture>> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Succeeded,
        Failed,
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    BoundedVector<TextureFetchTask> m_Tasks;
    std::optional<CoopTaskBatch> m_TaskBatch;
    std::vector<wgpu::Texture> m_Textures;
    DirectoryPath m_ParentPath;
    std::vector<RelativeFilePath> m_TexturePaths;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};