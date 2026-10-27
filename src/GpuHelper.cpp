#define MLG_LOGGER_NAME "WGPU"

#include "GpuHelper.h"

#include "Containers.h"
#include "Defer.h"

#include <atomic>
#include <limits>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_video.h>
#include <span>
#include <string_view>

#if !defined(EMSCRIPTEN)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

namespace
{

constexpr std::pair<wgpu::FeatureName, const char*> kRequiredFeatures[] = //
    {
        { wgpu::FeatureName::CoreFeaturesAndLimits, "CoreFeaturesAndLimits" },
        //{wgpu::FeatureName::IndirectFirstInstance, "IndirectFirstInstance"},
        //{wgpu::FeatureName::MultiDrawIndirect, "MultiDrawIndirect"},
    };

constexpr std::pair<wgpu::FeatureName, const char*> kDesiredFeatures[] = //
    {
        { wgpu::FeatureName::TextureCompressionBC, "TextureCompressionBC" },
        { wgpu::FeatureName::TextureCompressionASTC, "TextureCompressionASTC" },
    };

enum class BufferMappedState
{
    Unmapped,
    Mapped,
};

const char*
GetPresentModeString(const wgpu::PresentMode presentMode)
{
    switch(presentMode)
    {
        case wgpu::PresentMode::Undefined:
            return "Undefined";
        case wgpu::PresentMode::Fifo:
            return "Fifo";
        case wgpu::PresentMode::FifoRelaxed:
            return "FifoRelaxed";
        case wgpu::PresentMode::Mailbox:
            return "Mailbox";
        case wgpu::PresentMode::Immediate:
            return "Immediate";
        default:
            return "Unknown";
    }
}

void EnumerateAdapters();
void DumpAdapterInfo(const WGPUAdapterInfo& adapterInfo);
void DumpAdapterFeatures(const wgpu::Adapter& adapter);
void DumpDawnToggles(const wgpu::Device& device);
void DumpDeviceLimits(const wgpu::Device& device);
void DumpDeviceFeatures(const wgpu::Device& device);

Result<SDL_Window*>
CreateSdlWindow(const std::string_view appName)
{
    MLG_CHECK(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    const char* videoDriver = SDL_GetCurrentVideoDriver();
    MLG_INFO("SDL Video Driver: {}", videoDriver ? videoDriver : "<none>");

    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);

    MLG_INFO("SDL Display Count: {}", displayCount);

    const std::span<const SDL_DisplayID> displayIdSpan(displays, static_cast<size_t>(displayCount));

    for(const SDL_DisplayID displayId : displayIdSpan)
    {
        SDL_Rect bounds{};

        if(SDL_GetDisplayBounds(displayId, &bounds))
        {
            MLG_INFO("  SDL Display id={}, bounds={}x{}+{}+{}",
                displayId,
                bounds.w,
                bounds.h,
                bounds.x,
                bounds.y);
        }
    }

    SDL_free(displays);

    SDL_Rect displayRect;
    const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    MLG_CHECK(SDL_GetDisplayUsableBounds(primaryDisplay, &displayRect), SDL_GetError());
    const int winW = displayRect.w * 3 / 4; // 0.75
    const int winH = displayRect.h * 3 / 4; // 0.75

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    windowFlags |= SDL_WINDOW_METAL;
#endif

    const InplaceString<64> windowTitle(appName);
    SDL_Window* window = SDL_CreateWindow(windowTitle.c_str(), winW, winH, windowFlags);
    MLG_CHECK(window, SDL_GetError());

    SDL_ShowWindow(window);
    SDL_SyncWindow(window);

    return window;
}

Result<wgpu::Instance>
CreateInstance()
{
    wgpu::Instance instance = wgpu::CreateInstance(nullptr);

    MLG_CHECK(instance, "Failed to create WGPUInstance");

    return instance;
}

wgpu::PresentMode
ChoosePresentMode(const std::span<const wgpu::PresentMode> availableModes,
    const wgpu::PresentMode preferredMode)
{
    for(const wgpu::PresentMode mode : availableModes)
    {
        if(mode == preferredMode)
        {
            return mode;
        }
    }

    // Find the next best mode.
    constexpr wgpu::PresentMode modePreference[] //
        {
            wgpu::PresentMode::Mailbox,
            wgpu::PresentMode::Fifo,
            wgpu::PresentMode::Immediate,
            wgpu::PresentMode::FifoRelaxed,
        };

    for(const wgpu::PresentMode mode : modePreference)
    {
        for(const wgpu::PresentMode availableMode : availableModes)
        {
            if(mode == availableMode)
            {
                return mode;
            }
        }
    }

    return wgpu::PresentMode::Undefined;
}

wgpu::TextureFormat
ChooseBackbufferFormat(const std::span<const wgpu::TextureFormat> availableFormats)
{
    constexpr wgpu::TextureFormat preferredFormats[] = //
        {
            wgpu::TextureFormat::RGBA8UnormSrgb,
            wgpu::TextureFormat::BGRA8UnormSrgb,
            wgpu::TextureFormat::RGBA8Unorm,
            wgpu::TextureFormat::BGRA8Unorm,
        };

    wgpu::TextureFormat best = wgpu::TextureFormat::Undefined;
    size_t bestIdx = std::numeric_limits<size_t>::max();

    // Prefer BGRA8Unorm if available
    for(const wgpu::TextureFormat format : availableFormats)
    {
        for(size_t i = 0; i < std::size(preferredFormats); ++i)
        {
            if(format == preferredFormats[i] && i < bestIdx)
            {
                best = format;
                bestIdx = i;
            }
        }
    }

    if(best != wgpu::TextureFormat::Undefined)
    {
        return best;
    }

    // Fallback to first available format
    return availableFormats[0];
}

Result<wgpu::Surface>
CreateSurface(const wgpu::Instance& instance,
    [[maybe_unused]] SDL_Window* window,
    [[maybe_unused]] SDL_MetalView metalView)
{
    wgpu::SurfaceDescriptor surfaceDesc{};
    wgpu::Surface surface;

#if defined(__EMSCRIPTEN__)

    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
    canvas_desc.selector = "#canvas";

    surfaceDesc.nextInChain = &canvas_desc;
    surface = instance.CreateSurface(&surfaceDesc);

#elif defined(_WIN32)

    MLG_DEBUG("Creating surface for Win32 HWND");

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

    void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    wgpu::SurfaceSourceWindowsHWND surfaceSrc{};
    surfaceSrc.hinstance = ::GetModuleHandle(NULL);
    surfaceSrc.hwnd = hwnd;
    surfaceDesc.nextInChain = &surfaceSrc;
    surface = instance.CreateSurface(&surfaceDesc);

#elif defined(__linux__)

    const char* sdlDriver = SDL_GetCurrentVideoDriver();
    MLG_DEBUG("Creating surface for Linux - SDL video driver: {}",
        sdlDriver ? sdlDriver : "unknown");

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

    if(sdlDriver && strcmp(sdlDriver, "wayland") == 0)
    {
        wgpu::SurfaceSourceWaylandSurface surfaceSrc{};
        surfaceSrc.display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        surfaceSrc.surface =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        surfaceDesc.nextInChain = &surfaceSrc;
        surface = instance.CreateSurface(&surfaceDesc);
    }
    else if(sdlDriver && strcmp(sdlDriver, "x11") == 0)
    {
        wgpu::SurfaceSourceXlibWindow surfaceSrc{};
        surfaceSrc.display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const Sint64 windowNumber =
            SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, -1);
        surfaceSrc.window = static_cast<uint64_t>(windowNumber);
        surfaceDesc.nextInChain = &surfaceSrc;
        surface = instance.CreateSurface(&surfaceDesc);
    }
    else
    {
        MLG_ERROR("Unsupported SDL video driver: {}", sdlDriver ? sdlDriver : "unknown");
    }
#elif defined(__APPLE__)

    MLG_DEBUG("Creating surface for Metal");

    void* metalLayer = SDL_Metal_GetLayer(metalView);
    MLG_CHECK(metalLayer, SDL_GetError());

    wgpu::SurfaceSourceMetalLayer surfaceSrc{};
    surfaceSrc.layer = metalLayer;
    surfaceDesc.nextInChain = &surfaceSrc;
    surface = instance.CreateSurface(&surfaceDesc);
#else
    MLG_ERROR("Unsupported platform for surface creation");
#endif

    MLG_CHECK(surface, "Failed to create Surface");

    return surface;
}

Result<wgpu::TextureFormat>
ConfigureSurface(const wgpu::Adapter& adapter,
    const wgpu::Device& device,
    const wgpu::Surface& surface,
    const uint32_t width,
    const uint32_t height)
{
    wgpu::SurfaceCapabilities capabilities;
    MLG_CHECK(surface.GetCapabilities(adapter, &capabilities), "surface.GetCapabilities failed");

    const std::span<const wgpu::PresentMode> presentModes(capabilities.presentModes,
        capabilities.presentModeCount);
    const std::span<const wgpu::TextureFormat> formats(capabilities.formats,
        capabilities.formatCount);

    const wgpu::PresentMode presentMode =
        ChoosePresentMode(presentModes, wgpu::PresentMode::Immediate);
    MLG_CHECK(presentMode != wgpu::PresentMode::Undefined, "No supported present mode found");

    MLG_INFO("Present mode: {}", GetPresentModeString(presentMode));

    const wgpu::TextureFormat format = ChooseBackbufferFormat(formats);
    MLG_CHECK(format != wgpu::TextureFormat::Undefined, "No supported backbuffer format found");

    const wgpu::SurfaceConfiguration config //
        {
            .device = device,
            .format = format,
            .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst,
            .width = width,
            .height = height,
            .alphaMode = wgpu::CompositeAlphaMode::Opaque,
            .presentMode = presentMode,
        };

    surface.Configure(&config);

    return format;
}

Result<wgpu::Buffer>
CreateGpuBuffer(const GpuHelper& gpuHelper,
    const wgpu::BufferUsage usage,
    const size_t size,
    BufferMappedState mappedState,
    const std::string_view name)
{
    const wgpu::BufferDescriptor bufferDesc //
        {
            .label = name,
            .usage = usage,
            .size = size,
            .mappedAtCreation = (mappedState == BufferMappedState::Mapped),
        };

    wgpu::Buffer buffer = gpuHelper.GetDevice().CreateBuffer(&bufferDesc);
    MLG_CHECK(buffer, "Failed to create GPU buffer");

    return buffer;
}

Result<wgpu::Texture>
CreateDefaultTexture(GpuHelper& gpuHelper)
{
    constexpr size_t kDefaultTextureWidth = 128;
    constexpr size_t kDefaultTextureHeight = 128;
    constexpr RgbaColoru8 kDefaultTextureColor{ "#FF00FFFF"_rgba }; // Magenta
    constexpr TextureFormat kDefaultTextureFormat = TextureFormat::GetRGBA();
    constexpr uint32_t kMipCount = 1;

    auto texBufResult = gpuHelper.CreateTextureStagingBuffer(kDefaultTextureWidth,
        kDefaultTextureHeight,
        kMipCount,
        kDefaultTextureFormat,
        "DefaultTextureStagingBuffer");
    MLG_CHECK(texBufResult);

    auto mappedRangeResult = texBufResult->GetMappedRange();
    MLG_CHECK(mappedRangeResult);

    void* p = mappedRangeResult->data();
    const std::span<uint8_t> mappedRange(static_cast<uint8_t*>(p), mappedRangeResult->size());

    const size_t dstRowStride = kDefaultTextureFormat.GetBlockAlignedBytesPerRowWithPadding(kDefaultTextureWidth,
        GpuHelper::kTextureStrideAlignment);

    for(size_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        size_t offset = y * dstRowStride;

        for(size_t x = 0; x < kDefaultTextureWidth; ++x, offset += 4)
        {
            // Magenta
            mappedRange[offset + 0] = kDefaultTextureColor.r;
            mappedRange[offset + 1] = kDefaultTextureColor.g;
            mappedRange[offset + 2] = kDefaultTextureColor.b;
            mappedRange[offset + 3] = kDefaultTextureColor.a;
        }
    }

    return texBufResult->Commit();
}

void
RequestAdapterCb(wgpu::RequestAdapterStatus status,
    wgpu::Adapter receivedAdapter,
    wgpu::StringView message,
    GpuHelper::CreateTask::AdapterRequestData* requestData)
{
    if(status != wgpu::RequestAdapterStatus::Success)
    {
        MLG_ERROR("RequestAdapter failed: {}", std::string_view(message.data, message.length));
        requestData->Result = Result<>::Fail;
    }
    else
    {
        requestData->Result = receivedAdapter.MoveToCHandle();
    }

    requestData->IsComplete = true;
}

void
RequestDeviceCb(wgpu::RequestDeviceStatus status,
    wgpu::Device receivedDevice,
    wgpu::StringView message,
    GpuHelper::CreateTask::DeviceRequestData* requestData)
{
    if(status != wgpu::RequestDeviceStatus::Success)
    {
        MLG_ERROR("RequestDevice failed: {}", std::string_view(message.data, message.length));
        requestData->Result = Result<>::Fail;
    }
    else
    {
        requestData->Result = receivedDevice.MoveToCHandle();
    }

    requestData->IsComplete = true;
}

// TODO(KB) - handle device lost.
void
DeviceLostCb(
    const wgpu::Device& /*device*/, wgpu::DeviceLostReason reason, wgpu::StringView message)
{
    MLG_ERROR("Device lost (reason:{}): {}",
        static_cast<int>(reason),
        std::string_view(message.data, message.length));

    // CallbackCancelled indicates intentional device destruction.
    // exit() was probably already called, or program terminated normally.
    if(wgpu::DeviceLostReason::CallbackCancelled != reason)
    {
        exit(0);
    }
}

void
UncapturedErrorCb(
    const wgpu::Device& /*device*/, wgpu::ErrorType errorType, wgpu::StringView message)
{
    MLG_ABORT("Uncaptured error (type:{}): {}",
        static_cast<int>(errorType),
        std::string_view(message.data, message.length));
}

} // namespace

// GpuHelper::CreateTask

GpuHelper::CreateTask::~CreateTask()
{
    if(m_Consumed)
    {
        return;
    }

    if(m_MetalView)
    {
        SDL_Metal_DestroyView(m_MetalView);
        m_MetalView = nullptr;
    }

    if(m_Window)
    {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        SDL_Quit();
    }
}

Result<std::unique_ptr<GpuHelper>>
GpuHelper::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task has not succeeded");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    std::unique_ptr<GpuHelper> gpuHelper(new GpuHelper(m_Window,
        m_MetalView,
        m_Instance,
        m_Adapter,
        m_Device,
        m_Surface,
        m_SurfaceFormat));

    auto cleanup = MLG_MAKE_DEFERRED
    {
        gpuHelper.reset();
    };

    auto defaultTexture = CreateDefaultTexture(*gpuHelper);

    MLG_CHECK(defaultTexture);

    gpuHelper->m_DefaultTexture = *defaultTexture;

    cleanup.release();

    return gpuHelper;
}

// private:

Result<>
GpuHelper::CreateTask::OnStart(const std::string_view appName)
{
    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    MLG_INFO("Creating GpuHelper...");

    auto window = CreateSdlWindow(appName);
    MLG_CHECK(window);
    m_Window = std::move(*window);

#if defined(__APPLE__)
    SDL_MetalView metalView = SDL_Metal_CreateView(*window);
    MLG_CHECK(metalView, SDL_GetError());
    m_MetalView = metalView;
#endif

    auto instance = CreateInstance();
    MLG_CHECK(instance);
    m_Instance = std::move(*instance);

    auto surface = CreateSurface(m_Instance, m_Window, m_MetalView);
    MLG_CHECK(surface);
    m_Surface = std::move(*surface);

    m_Stage = Stage::CreateAdapter;

    return Result<>::Ok;
}

void
GpuHelper::CreateTask::OnUpdate()
{
    // Process events so callbacks get called.
    m_Instance.ProcessEvents();

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::CreateAdapter:
            if(CreateAdapter())
            {
                m_Stage = Stage::CreatingAdapter;
            }
            else
            {
                m_Stage = Stage::Failed;
            }
            break;

        case Stage::CreatingAdapter:
            if(m_AdapterRequestData.IsComplete && m_AdapterRequestData.Result)
            {
                MLG_INFO("Adapter creation succeeded");

                if(FinalizeAdapter() && CreateDevice())
                {
                    m_Stage = Stage::CreatingDevice;
                }
                else
                {
                    m_Stage = Stage::Failed;
                }
            }
            else if(m_AdapterRequestData.IsComplete && !m_AdapterRequestData.Result)
            {
                MLG_ERROR("Adapter creation failed");
                m_Stage = Stage::Failed;
            }

            break;

        case Stage::CreatingDevice:
            if(m_DeviceRequestData.IsComplete && m_DeviceRequestData.Result)
            {
                MLG_INFO("Device creation succeeded");

                if(FinalizeDevice() && Configure())
                {
                    m_Stage = Stage::Succeeded;
                }
                else
                {
                    m_Stage = Stage::Failed;
                }
            }
            else if(m_DeviceRequestData.IsComplete && !m_DeviceRequestData.Result)
            {
                MLG_ERROR("Device creation failed");
                m_Stage = Stage::Failed;
            }

            break;

        case Stage::Failed:
            MLG_ERROR("GpuHelper creation failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

Result<>
GpuHelper::CreateTask::CreateAdapter()
{
    MLG_CHECKV(Stage::CreateAdapter == m_Stage, "Task is not in the correct state");

    MLG_INFO("Creating adapter...");

    EnumerateAdapters();

    const wgpu::RequestAdapterOptions options //
        {
            .nextInChain = nullptr,
            .featureLevel = wgpu::FeatureLevel::Core,
            .powerPreference = wgpu::PowerPreference::HighPerformance,
            .forceFallbackAdapter = false,
#if defined(_WIN32)
            .backendType = wgpu::BackendType::Vulkan,
    //.backendType = wgpu::BackendType::D3D12,
#elif defined(__APPLE__)
            .backendType = wgpu::BackendType::Metal,
#elif defined(__EMSCRIPTEN__)
            .backendType = wgpu::BackendType::WebGPU,
#else
            .backendType = wgpu::BackendType::Vulkan,
#endif
            .compatibleSurface = m_Surface,
        };

    m_Instance.RequestAdapter(&options,
        wgpu::CallbackMode::AllowSpontaneous,
        RequestAdapterCb,
        &m_AdapterRequestData);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTask::FinalizeAdapter()
{
    MLG_CHECKV(Stage::CreatingAdapter == m_Stage, "Task is not in the correct state");

    MLG_CHECK(m_AdapterRequestData.Result, "Failed to create adapter");

    m_Adapter = wgpu::Adapter::Acquire(*m_AdapterRequestData.Result);

    for(const auto& feature : kRequiredFeatures)
    {
        const bool supported = m_Adapter.HasFeature(feature.first);
        MLG_CHECK(supported, "GPU feature not supported: {}", feature.second);
    }

    wgpu::AdapterInfo adapterInfo;
    m_Adapter.GetInfo(&adapterInfo);
    MLG_INFO("Selected adapter:");
    DumpAdapterInfo(adapterInfo);

    DumpAdapterFeatures(m_Adapter);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTask::CreateDevice()
{
    MLG_CHECKV(Stage::CreatingAdapter == m_Stage, "Task is not in the correct state");

    MLG_INFO("Creating device...");

#ifndef __EMSCRIPTEN__
    const char* const enabledToggles[] = { //"skip_validation",
        //"disable_robustness",
        //"allow_unsafe_apis", // Required for MultiDrawIndirect
        //"backend_validation",
        ""
    };

    // const char* disabledToggles[] =
    //{
    //     "lazy_clear_resource_on_first_use",
    // };

    wgpu::DawnTogglesDescriptor toggles;
    toggles.enabledToggleCount = std::size(enabledToggles);
    toggles.enabledToggles = &enabledToggles[0];
    // toggles.disabledToggleCount = std::size(disabledToggles);
    // toggles.disabledToggles = disabledToggles;
#endif

    InplaceVector<wgpu::FeatureName, std::size(kRequiredFeatures) + std::size(kDesiredFeatures)>
        requiredFeatures;
    for(const auto [feature, name] : kRequiredFeatures)
    {
        requiredFeatures.emplace_back(feature);
    }

    for(const auto [feature, name] : kDesiredFeatures)
    {
        if(m_Adapter.HasFeature(feature))
        {
            requiredFeatures.emplace_back(feature);
        }
    }

    const wgpu::Limits requiredLimits{};
    /*requiredLimits.maxTextureDimension2D = 4096;
    requiredLimits.maxBindGroups = 3;
    requiredLimits.maxBindGroupsPlusVertexBuffers = 4;
    requiredLimits.maxBindingsPerBindGroup = 4;
    requiredLimits.maxStorageBuffersPerShaderStage = 3;
    requiredLimits.maxUniformBuffersPerShaderStage = 3;*/

    wgpu::DeviceDescriptor deviceDesc //
        {
            {
#ifndef __EMSCRIPTEN__
    //.nextInChain = &toggles,
#endif
                .nextInChain = nullptr,
                .label = "MainDevice",
                .requiredFeatureCount = requiredFeatures.size(),
                .requiredFeatures = requiredFeatures.data(),
                .requiredLimits = &requiredLimits,
            },
        };
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, DeviceLostCb);
    deviceDesc.SetUncapturedErrorCallback(UncapturedErrorCb);

    m_Adapter.RequestDevice(&deviceDesc,
        wgpu::CallbackMode::AllowSpontaneous,
        RequestDeviceCb,
        &m_DeviceRequestData);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTask::FinalizeDevice()
{
    MLG_CHECKV(Stage::CreatingDevice == m_Stage, "Task is not in the correct state");

    MLG_CHECK(m_DeviceRequestData.Result, "Failed to create device");
    m_Device = wgpu::Device::Acquire(*m_DeviceRequestData.Result);

    DumpDawnToggles(m_Device);
    DumpDeviceLimits(m_Device);
    DumpDeviceFeatures(m_Device);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTask::Configure()
{
    MLG_CHECKV(Stage::CreatingDevice == m_Stage, "Task is not in the correct state");

    int width{ 0 }, height{ 0 };
    SDL_GetWindowSize(m_Window, &width, &height);

    auto surfaceFormat = ConfigureSurface(m_Adapter,
        m_Device,
        m_Surface,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));

    MLG_CHECK(surfaceFormat);

    m_SurfaceFormat = *surfaceFormat;

    return Result<>::Ok;
}

////////// GpuHelper

GpuHelper::~GpuHelper()
{
    if(m_MetalView)
    {
        SDL_Metal_DestroyView(m_MetalView);
        m_MetalView = nullptr;
    }

    if(m_Window)
    {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        SDL_Quit();
    }
}

SDL_Window*
GpuHelper::GetWindow() const
{
    return m_Window;
}

const wgpu::Instance&
GpuHelper::GetInstance() const
{
    return m_Instance;
}

const wgpu::Device&
GpuHelper::GetDevice() const
{
    return m_Device;
}

const wgpu::Texture&
GpuHelper::GetDefaultTexture() const
{
    return m_DefaultTexture;
}

Dimension2
GpuHelper::GetScreenDimensions() const
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(GetWindow(), &width, &height))
    {
        MLG_ERROR("Failed to get window size: {}", SDL_GetError());
    }
    return Dimension2(static_cast<unsigned>(width), static_cast<unsigned>(height));
}

Result<GpuRenderTarget>
GpuHelper::GetSwapChainTexture() const
{
    wgpu::SurfaceTexture surfaceTexture;
    m_Surface.GetCurrentTexture(&surfaceTexture);

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        MLG_WARN(
            "GetCurrentTexture() returned SuccessSuboptimal - the surface may need to be reconfigured");
    }

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        return GpuRenderTarget{ surfaceTexture.texture };
    }

    MLG_CHECK(surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Error,
        "Failed to acquire current surface texture: Unknown error");

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Timeout)
    {
        MLG_WARN("Failed to acquire current surface texture: Timeout");
    }
    else if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated)
    {
        MLG_WARN("Failed to acquire current surface texture: Outdated");
    }
    else if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Lost)
    {
        MLG_WARN("Failed to acquire current surface texture: Lost");
    }

    // Attempt to reconfigure the surface and acquire the texture again
    m_Surface.Unconfigure();

    auto surfaceFormat = ConfigureSurface(m_Adapter,
        GetDevice(),
        m_Surface,
        GetScreenDimensions().GetWidth(),
        GetScreenDimensions().GetHeight());

    MLG_CHECK(surfaceFormat);

    m_SurfaceFormat = *surfaceFormat;

    m_Surface.GetCurrentTexture(&surfaceTexture);

    MLG_CHECK(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
            || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal,
        "Failed to acquire current surface texture after reconfiguration");

    return GpuRenderTarget{ surfaceTexture.texture };
}

wgpu::TextureFormat
GpuHelper::GetSwapChainFormat() const
{
    return m_SurfaceFormat;
}
bool
GpuHelper::HasTextureFormat(TextureFormat::FormatIdentifier formatId) const
{
    switch(formatId)
    {
        case TextureFormat::FormatIdentifier::RGBA:
            return true;
        case TextureFormat::FormatIdentifier::BC7:
            return m_Device.HasFeature(wgpu::FeatureName::TextureCompressionBC);
        case TextureFormat::FormatIdentifier::ASTC:
            return m_Device.HasFeature(wgpu::FeatureName::TextureCompressionASTC);
        case TextureFormat::FormatIdentifier::Unknown:
            return false;
    }
}

Result<>
GpuHelper::Present() const
{
#if !defined(__EMSCRIPTEN__)

    wgpu::SurfaceTexture surfaceTexture;
    m_Surface.GetCurrentTexture(&surfaceTexture);

    auto result = m_Surface.Present();
    MLG_CHECK(result.status == wgpu::Status::Success, "Failed to present current surface texture");
#endif // !defined(__EMSCRIPTEN__)
    return Result<>::Ok;
}

Result<>
GpuHelper::Resize(const uint32_t width, const uint32_t height)
{
    wgpu::SurfaceTexture currentTexture;
    m_Surface.GetCurrentTexture(&currentTexture);
    if(width != currentTexture.texture.GetWidth() || height != currentTexture.texture.GetHeight())
    {
        m_Surface.Unconfigure();

        auto surfaceFormat = ConfigureSurface(m_Adapter, GetDevice(), m_Surface, width, height);
        MLG_CHECK(surfaceFormat);

        m_SurfaceFormat = *surfaceFormat;
    }

    return Result<>::Ok;
}

Result<GpuRenderTarget>
GpuHelper::CreateRenderTarget(
    const uint32_t width, const uint32_t height, const std::string_view name) const
{
    const wgpu::TextureDescriptor desc //
        {
            .label = name,
            .usage = wgpu::TextureUsage::RenderAttachment
                | wgpu::TextureUsage::TextureBinding, // For post-processing and compositing
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = width,
                .height = height,
                .depthOrArrayLayers = 1,
            },
            .format = kRenderTargetFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    const wgpu::Texture texture = GetDevice().CreateTexture(&desc);
    MLG_CHECK(texture, "Failed to create texture");

    return GpuRenderTarget(texture);
}

Result<GpuDepthTarget>
GpuHelper::CreateDepthBuffer(
    const uint32_t width, const uint32_t height, const std::string_view name) const
{
    const wgpu::TextureDescriptor desc //
        {
            .label = name,
            .usage = wgpu::TextureUsage::RenderAttachment,
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = width,
                .height = height,
                .depthOrArrayLayers = 1,
            },
            .format = kDepthBufferFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    const wgpu::Texture depthBuffer = GetDevice().CreateTexture(&desc);
    MLG_CHECK(depthBuffer, "Failed to create depth buffer");

    return GpuDepthTarget(depthBuffer);
}

// A note on using staging buffers to upload data to the GPU:
// When copying data to textures we use a staging buffer.
// When copying data to other buffers we could also use a staging buffer, but it's simpler to use
// Queue::WriteBuffer which doesn't require a staging buffer. The equivalent of WriteBuffer for
// textures is Queue::WriteTexture howerver Queue::WriteTexture contains a bunch of validation and
// is slow compared to using a staging buffer and CopyBufferToTexture.

Result<GpuTextureStagingBuffer>
GpuHelper::CreateTextureStagingBuffer(const uint32_t width,
    const uint32_t height,
    const uint32_t mipCount,
    const TextureFormat& format,
    const std::string_view name) const
{
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format: {}", format.GetName());
    MLG_CHECK(format.CheckDimensions(width, height, mipCount));
    MLG_CHECK(width % format.GetBlockDimension() == 0 && height % format.GetBlockDimension() == 0,
        "Texture dimensions must be block aligned for format: {}", format.GetName());

    MLG_CHECK(HasTextureFormat(format.GetFormatId()),
        "Texture format not supported: {}",
        format.GetName());

    const size_t sizeofBuffer =
        format.GetTotalSizeofMipDataWithPadding(width, height, mipCount, kTextureStrideAlignment);

    const wgpu::BufferUsage usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    auto buffer = CreateGpuBuffer(*this, usage, sizeofBuffer, BufferMappedState::Mapped, name);
    MLG_CHECK(buffer, "Failed to create staging buffer");

    return GpuTextureStagingBuffer(*this, *buffer, width, height, mipCount, format, name);
}

Result<GpuVertexBuffer>
GpuHelper::CreateVertexBuffer(const size_t count, const std::string_view name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    auto buffer =
        CreateGpuBuffer(*this, usage, count * sizeof(Vertex), BufferMappedState::Unmapped, name);

    MLG_CHECK(buffer, "Failed to create vertex buffer");

    return GpuVertexBuffer::Create(GetDevice(), *buffer);
}

Result<GpuIndexBuffer>
GpuHelper::CreateIndexBuffer(const size_t count, const std::string_view name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(*this,
        usage,
        count * sizeof(VertexIndex),
        BufferMappedState::Unmapped,
        name);

    MLG_CHECK(buffer, "Failed to create index buffer");

    return GpuIndexBuffer::Create(GetDevice(), *buffer);
}

// private:
GpuHelper::GpuHelper(SDL_Window* window,
    SDL_MetalView metalView,
    wgpu::Instance instance,
    wgpu::Adapter adapter,
    wgpu::Device device,
    wgpu::Surface surface,
    wgpu::TextureFormat surfaceFormat)
    : m_Window(std::move(window)),
      m_MetalView(std::move(metalView)),
      m_Instance(std::move(instance)),
      m_Adapter(std::move(adapter)),
      m_Device(std::move(device)),
      m_Surface(std::move(surface)),
      m_SurfaceFormat(surfaceFormat)
{
}

Result<wgpu::Buffer>
GpuHelper::CreateIndirectBuffer(const size_t size, const std::string_view name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(*this, usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create indirect buffer");

    return buffer;
}

Result<wgpu::Buffer>
GpuHelper::CreateStorageBuffer(const size_t size, const std::string_view name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(*this, usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create storage buffer");

    return buffer;
}

Result<wgpu::Buffer>
GpuHelper::CreateUniformBuffer(const size_t size, const std::string_view name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(*this, usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create uniform buffer");

    return buffer;
}

#ifndef __EMSCRIPTEN__
#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed
#endif

namespace
{

#ifdef __EMSCRIPTEN__
void
EnumerateAdapters()
{
}

void
DumpAdapterInfo(const WGPUAdapterInfo&)
{
}

void
DumpDawnToggles(const wgpu::Device&)
{
}
#else

const char*
GetBackendTypeString(const WGPUBackendType backendType)
{
    switch(backendType)
    {
        case WGPUBackendType_D3D11:
            return "D3D11";
        case WGPUBackendType_D3D12:
            return "D3D12";
        case WGPUBackendType_Metal:
            return "Metal";
        case WGPUBackendType_Vulkan:
            return "Vulkan";
        case WGPUBackendType_WebGPU:
            return "WebGPU";
        default:
            return "Unknown";
    }
}

const char*
GetAdapterTypeString(const WGPUAdapterType adapterType)
{
    switch(adapterType)
    {
        case WGPUAdapterType_DiscreteGPU:
            return "Discrete GPU";
        case WGPUAdapterType_IntegratedGPU:
            return "Integrated GPU";
        case WGPUAdapterType_CPU:
            return "CPU";
        default:
            return "Unknown";
    }
}

void
EnumerateAdapters()
{
    const dawn::native::Instance instance;

    const std::vector<dawn::native::Adapter> adapters = instance.EnumerateAdapters();

    MLG_INFO("Available adapters ({}):", adapters.size());

    size_t count = 0;
    for(const dawn::native::Adapter& a : adapters)
    {
        WGPUAdapter adapter = a.Get();

        WGPUAdapterInfo info{ .adapterType = WGPUAdapterType_Unknown };
        wgpuAdapterGetInfo(adapter, &info);

        MLG_INFO("Adapter {}:", count++);
        DumpAdapterInfo(info);

        wgpuAdapterInfoFreeMembers(info);
    }
}

void
DumpAdapterInfo(const WGPUAdapterInfo& adapterInfo)
{
    MLG_DEBUG("  Vendor: {}", std::string_view(adapterInfo.vendor.data, adapterInfo.vendor.length));
    MLG_DEBUG("  Architecture: {}",
        std::string_view(adapterInfo.architecture.data, adapterInfo.architecture.length));
    MLG_DEBUG("  Device: {}", std::string_view(adapterInfo.device.data, adapterInfo.device.length));
    MLG_DEBUG("  Description: {}",
        std::string_view(adapterInfo.description.data, adapterInfo.description.length));
    MLG_DEBUG("  Backend Type: {}", GetBackendTypeString(adapterInfo.backendType));
    MLG_DEBUG("  Adapter Type: {}", GetAdapterTypeString(adapterInfo.adapterType));
    MLG_DEBUG("  Vendor ID: {}", adapterInfo.vendorID);
    MLG_DEBUG("  Device ID: {}", adapterInfo.deviceID);
}

void
DumpDawnToggles(const wgpu::Device& device)
{
    auto toggles = dawn::native::GetTogglesUsed(device.Get());
    MLG_DEBUG("Device enabled toggles ({}):", toggles.size());
    for(const char* t : toggles)
    {
        MLG_DEBUG("  {}", t);
    }
}

#endif // __EMSCRIPTEN__

void
DumpDeviceLimits(const wgpu::Device& device)
{
    wgpu::Limits limits;
    device.GetLimits(&limits);
    MLG_DEBUG("Device limits:");
    MLG_DEBUG("  maxTextureDimension1D: {}", limits.maxTextureDimension1D);
    MLG_DEBUG("  maxTextureDimension2D: {}", limits.maxTextureDimension2D);
    MLG_DEBUG("  maxTextureDimension3D: {}", limits.maxTextureDimension3D);
    MLG_DEBUG("  maxTextureArrayLayers: {}", limits.maxTextureArrayLayers);
    MLG_DEBUG("  maxBindGroups: {}", limits.maxBindGroups);
    MLG_DEBUG("  maxDynamicUniformBuffersPerPipelineLayout: {}",
        limits.maxDynamicUniformBuffersPerPipelineLayout);
    MLG_DEBUG("  maxDynamicStorageBuffersPerPipelineLayout: {}",
        limits.maxDynamicStorageBuffersPerPipelineLayout);
    MLG_DEBUG("  maxSampledTexturesPerShaderStage: {}", limits.maxSampledTexturesPerShaderStage);
    MLG_DEBUG("  maxSamplersPerShaderStage: {}", limits.maxSamplersPerShaderStage);
    MLG_DEBUG("  maxStorageBuffersPerShaderStage: {}", limits.maxStorageBuffersPerShaderStage);
    MLG_DEBUG("  maxStorageTexturesPerShaderStage: {}", limits.maxStorageTexturesPerShaderStage);
    MLG_DEBUG("  maxUniformBuffersPerShaderStage: {}", limits.maxUniformBuffersPerShaderStage);
}

[[nodiscard]]
constexpr std::string_view
GetFeatureNameString(wgpu::FeatureName feature) noexcept
{
#define FEATURE_CASE(name)                                                                         \
    case wgpu::FeatureName::name:                                                                  \
        return #name

    switch(feature)
    {
        FEATURE_CASE(CoreFeaturesAndLimits);
        FEATURE_CASE(DepthClipControl);
        FEATURE_CASE(Depth32FloatStencil8);
        FEATURE_CASE(TextureCompressionBC);
        FEATURE_CASE(TextureCompressionBCSliced3D);
        FEATURE_CASE(TextureCompressionETC2);
        FEATURE_CASE(TextureCompressionASTC);
        FEATURE_CASE(TextureCompressionASTCSliced3D);
        FEATURE_CASE(TimestampQuery);
        FEATURE_CASE(IndirectFirstInstance);
        FEATURE_CASE(ShaderF16);
        FEATURE_CASE(RG11B10UfloatRenderable);
        FEATURE_CASE(BGRA8UnormStorage);
        FEATURE_CASE(Float32Filterable);
        FEATURE_CASE(Float32Blendable);
        FEATURE_CASE(ClipDistances);
        FEATURE_CASE(DualSourceBlending);
        FEATURE_CASE(Subgroups);
        FEATURE_CASE(TextureFormatsTier1);
        FEATURE_CASE(TextureFormatsTier2);
        FEATURE_CASE(PrimitiveIndex);
        FEATURE_CASE(TextureComponentSwizzle);
        FEATURE_CASE(DawnInternalUsages);
        FEATURE_CASE(DawnMultiPlanarFormats);
        FEATURE_CASE(DawnNative);
        FEATURE_CASE(ChromiumExperimentalTimestampQueryInsidePasses);
        FEATURE_CASE(ImplicitDeviceSynchronization);
        FEATURE_CASE(TransientAttachments);
        FEATURE_CASE(MSAARenderToSingleSampled);
        FEATURE_CASE(D3D11MultithreadProtected);
        FEATURE_CASE(ANGLETextureSharing);
        FEATURE_CASE(PixelLocalStorageCoherent);
        FEATURE_CASE(PixelLocalStorageNonCoherent);
        FEATURE_CASE(Unorm16TextureFormats);
        FEATURE_CASE(MultiPlanarFormatExtendedUsages);
        FEATURE_CASE(MultiPlanarFormatP010);
        FEATURE_CASE(HostMappedPointer);
        FEATURE_CASE(MultiPlanarRenderTargets);
        FEATURE_CASE(MultiPlanarFormatNv12a);
        FEATURE_CASE(FramebufferFetch);
        FEATURE_CASE(BufferMapExtendedUsages);
        FEATURE_CASE(AdapterPropertiesMemoryHeaps);
        FEATURE_CASE(AdapterPropertiesD3D);
        FEATURE_CASE(AdapterPropertiesVk);
        FEATURE_CASE(DawnFormatCapabilities);
        FEATURE_CASE(DawnDrmFormatCapabilities);
        FEATURE_CASE(MultiPlanarFormatNv16);
        FEATURE_CASE(MultiPlanarFormatNv24);
        FEATURE_CASE(MultiPlanarFormatP210);
        FEATURE_CASE(MultiPlanarFormatP410);
        FEATURE_CASE(SharedTextureMemoryVkDedicatedAllocation);
        FEATURE_CASE(SharedTextureMemoryAHardwareBuffer);
        FEATURE_CASE(SharedTextureMemoryDmaBuf);
        FEATURE_CASE(SharedTextureMemoryOpaqueFD);
        FEATURE_CASE(SharedTextureMemoryZirconHandle);
        FEATURE_CASE(SharedTextureMemoryDXGISharedHandle);
        FEATURE_CASE(SharedTextureMemoryD3D11Texture2D);
        FEATURE_CASE(SharedTextureMemoryIOSurface);
        FEATURE_CASE(SharedTextureMemoryEGLImage);
        FEATURE_CASE(SharedFenceVkSemaphoreOpaqueFD);
        FEATURE_CASE(SharedFenceSyncFD);
        FEATURE_CASE(SharedFenceVkSemaphoreZirconHandle);
        FEATURE_CASE(SharedFenceDXGISharedHandle);
        FEATURE_CASE(SharedFenceMTLSharedEvent);
        FEATURE_CASE(SharedBufferMemoryD3D12Resource);
        FEATURE_CASE(StaticSamplers);
        FEATURE_CASE(YCbCrVulkanSamplers);
        FEATURE_CASE(ShaderModuleCompilationOptions);
        FEATURE_CASE(DawnLoadResolveTexture);
        FEATURE_CASE(DawnPartialLoadResolveTexture);
        FEATURE_CASE(MultiDrawIndirect);
        FEATURE_CASE(DawnTexelCopyBufferRowAlignment);
        FEATURE_CASE(FlexibleTextureViews);
        FEATURE_CASE(ChromiumExperimentalSubgroupMatrix);
        FEATURE_CASE(SharedFenceEGLSync);
        FEATURE_CASE(DawnDeviceAllocatorControl);
        FEATURE_CASE(AdapterPropertiesWGPU);
        FEATURE_CASE(SharedBufferMemoryD3D12SharedMemoryFileMappingHandle);
        FEATURE_CASE(SharedTextureMemoryD3D12Resource);
        FEATURE_CASE(ChromiumExperimentalSamplingResourceTable);
        FEATURE_CASE(ChromiumExperimentalSubgroupSizeControl);
        FEATURE_CASE(AtomicVec2uMinMax);
        FEATURE_CASE(Unorm16FormatsForExternalTexture);
        FEATURE_CASE(OpaqueYCbCrAndroidForExternalTexture);
        FEATURE_CASE(Unorm16Filterable);
        FEATURE_CASE(RenderPassRenderArea);
        FEATURE_CASE(AdapterPropertiesDrm);
    }

#undef FEATURE_CASE

    return "Unknown";
}

void
DumpAdapterFeatures(const wgpu::Adapter& adapter)
{
    wgpu::SupportedFeatures features{};
    adapter.GetFeatures(&features);

    const std::span<const wgpu::FeatureName> featureSpan(features.features, features.featureCount);

    MLG_DEBUG("Adapter features:");

    for(const wgpu::FeatureName feature : featureSpan)
    {
        MLG_DEBUG("  {}", GetFeatureNameString(feature));
    }
}

void
DumpDeviceFeatures(const wgpu::Device& device)
{
    wgpu::SupportedFeatures features{};
    device.GetFeatures(&features);

    const std::span<const wgpu::FeatureName> featureSpan(features.features, features.featureCount);

    MLG_DEBUG("Device features:");

    for(const wgpu::FeatureName feature : featureSpan)
    {
        MLG_DEBUG("  {}", GetFeatureNameString(feature));
    }
}
} // namespace