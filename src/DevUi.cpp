#include "DevUi.h"

#include "InplaceString.h"
#include "PerfMetrics.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

namespace
{
float
GetStatusBarHeight()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + (style.WindowPadding.y * 2.0f);
}

int
CliInputCallback(ImGuiInputTextCallbackData* data)
{
    auto* state = static_cast<CliState*>(data->UserData);

    if(data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
    {
        return 0;
    }

    std::string replacement;

    if(data->EventKey == ImGuiKey_UpArrow)
    {
        replacement = state->HistoryBack();
    }
    else if(data->EventKey == ImGuiKey_DownArrow)
    {
        replacement = state->HistoryForward();
    }

    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement.c_str());

    return 0;
}
} // namespace

// CliState

void
CliState::ClearInput()
{
    m_Input[0] = '\0';
    m_PendingInput.clear();
}

void
CliState::AddLine(std::string line)
{
    m_Lines.push_back(std::move(line));
}

void
CliState::AddHistory(std::string command)
{
    if(m_History.empty() || m_History.back() != command)
    {
        m_History.push_back(std::move(command));
        m_HistoryIt = m_History.end();
    }
}

/// Moves the history pointer back and returns the command at the new position.
std::string_view
CliState::HistoryBack()
{
    constexpr std::string_view emptyString;

    if(m_History.empty())
    {
        return emptyString;
    }

    if(m_HistoryIt == m_History.end())
    {
        m_PendingInput.assign(m_Input.data(), static_cast<size_t>(strlen(m_Input.data())));
    }

    if(m_HistoryIt != m_History.begin())
    {
        --m_HistoryIt;
    }

    return *m_HistoryIt;
}

/// Moves the history pointer forward and returns the command at the new position.
std::string_view
CliState::HistoryForward()
{
    if(!m_History.empty() && m_HistoryIt != m_History.end())
    {
        ++m_HistoryIt;
    }

    if(m_HistoryIt == m_History.end())
    {
        return m_PendingInput;
    }

    return *m_HistoryIt;
}

// DevUi

Result<>
DevUi::Render()
{
    DrawDockedEditorLayout();

    DrawPerfPanel();
    DrawViewPanel();
    DrawCliPanel();
    DrawStatusBarPanel();

    return Result<>::Ok;
}

// private:

void
DevUi::DrawDockedEditorLayout() const // NOLINT(readability-convert-member-functions-to-static)
{
    const ImGuiID dockspaceId = ImGui::GetID("MainDockspace");

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Dockspace should fill the entire viewport, except for space at the bottom for the status bar.
    ImVec2 workSize = viewport->WorkSize;
    workSize.y -= GetStatusBarHeight();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(workSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("Dockspace Host", nullptr, hostFlags);

    const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
    // ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    static bool dockspaceBuilt = false;

    if(!dockspaceBuilt)
    {
        dockspaceBuilt = true;

        ImGui::DockBuilderRemoveNode(dockspaceId);

        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMainId = dockspaceId;

        const ImGuiID dockLeftId =
            ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);

        const ImGuiID dockBottomId =
            ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);

        ImGui::DockBuilderDockWindow(kViewPanelName, dockMainId);
        ImGui::DockBuilderDockWindow(kPerfPanelName, dockLeftId);
        ImGui::DockBuilderDockWindow(kCliPanelName, dockBottomId);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void
DevUi::DrawPerfPanel() const // NOLINT(readability-convert-member-functions-to-static)
{
    constexpr size_t kMaxPerfStats = 256;

    const PerfStats* perfStats[kMaxPerfStats];
    std::span<const PerfStats*> perfStatsSpan(perfStats);

    // Timers
    size_t counterCount = PerfMetrics::SampleCounters<PerfTimerCategory>(perfStatsSpan);

    std::span<const PerfStats*> sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(kPerfPanelName);
    MLG_DEFER
    {
        ImGui::End();
    };

    auto drawSubTree = [](this auto&& self,
                           const std::string_view prefix,
                           const std::span<const PerfStats*> pss) -> std::span<const PerfStats*>
    {
        bool isOpen = false;
        if(!prefix.empty())
        {
            // Prefix is non empty - render a tree node.
            const InplaceString<> prefixStr(prefix);
            isOpen = ImGui::TreeNode(prefixStr.c_str());
        }

        MLG_DEFER
        {
            if(isOpen)
            {
                // Pop only if the tree node is open
                ImGui::TreePop();
            }
        };

        std::span<const PerfStats*> curPss = pss;

        while(!curPss.empty())
        {
            const PerfStats& ps = *curPss.front();
            const std::string_view curName = ps.GetName();

            MLG_ASSERT(!curName.empty(), "Empty perf counter name");

            if(!curName.starts_with(prefix))
            {
                // Entering a new prefix - return to caller.
                return curPss;
            }

            if(prefix.empty())
            {
                // First time through prefix is empty.
                // Get the prefix and recurse.
                const size_t pos = curName.find_first_of('.');
                const std::string_view nextPrefix = curName.substr(0, pos);
                curPss = self(nextPrefix, curPss);
            }
            else if(!isOpen)
            {
                // Current node is not open.  No need to process subnodes.

                // Advance to the next node in the list.
                curPss = curPss.subspan(1);
            }
            else if(curName.size() >= prefix.size())
            {
                const size_t pos = curName.find_first_of('.', prefix.size() + 1);

                if(pos == std::string_view::npos)
                {
                    // Reached the leaf node.
                    const std::string_view leafName =
                        curName == prefix ? curName : curName.substr(prefix.size() + 1, pos);

                    const char* units = ps.GetCategoryId() == PerfTimerCategory::Id ? "ms" : "";

                    const auto text =
                        InplaceString<>::Format("{}: {:.3f} {}", leafName, ps.GetEMA(), units);
                    ImGui::TreeNodeEx(text.c_str(),
                        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

                    // Advance to the next node in the list.
                    curPss = curPss.subspan(1);
                }
                else
                {
                    // Recurse to the next subnode.
                    const std::string_view nextPrefix = curName.substr(0, pos);
                    curPss = self(nextPrefix, curPss);
                }
            }
        }

        return curPss;
    };

    drawSubTree("", sortedCounters);

    // Other counters
    counterCount = PerfMetrics::SampleCounters<>(perfStatsSpan);

    sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    drawSubTree("", sortedCounters);
}

void
DevUi::DrawViewPanel()
{
    // ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 0.0f);
    ImGui::Begin(kViewPanelName, nullptr, ImGuiWindowFlags_NoBackground);

    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 mousePos = ImGui::GetMousePos();

    m_ViewPanelRect = Rect( //
        {
            .X = static_cast<int>(cursorPos.x),
            .Y = static_cast<int>(cursorPos.y),
            .Width = static_cast<unsigned>(avail.x),
            .Height = static_cast<unsigned>(avail.y),
        });

    m_ViewPanelMousePos = Point2(static_cast<int>(mousePos.x - cursorPos.x),
        static_cast<int>(mousePos.y - cursorPos.y));

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void
DevUi::DrawCliPanel()
{
    ImGui::Begin(kCliPanelName);

    const float input_height = ImGui::GetFrameHeightWithSpacing();

    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild("CliScrollback",
        ImVec2(0.0f, -(input_height + spacing)),
        false,
        ImGuiWindowFlags_HorizontalScrollbar);

    for(const std::string& line : m_CliState.GetLines())
    {
        ImGui::TextUnformatted(line.c_str());
    }

    if(m_CliScrollToBottom)
    {
        ImGui::SetScrollHereY(1.0f);
        m_CliScrollToBottom = false;
    }

    ImGui::EndChild();

    ImGui::Separator();

    ImGui::TextUnformatted(">");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1.0f);

    const bool submitted = ImGui::InputText("##CliInput",
        m_CliState.GetInput().data(),
        m_CliState.GetInput().size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
        CliInputCallback,
        &m_CliState);

    ImGui::SetItemDefaultFocus();

    if(submitted)
    {
        if(m_CliState.GetInput()[0] != '\0')
        {
            constexpr std::string_view prompt = "> ";
            const std::string command = m_CliState.GetInput().data();

            std::string line;

            line.reserve(prompt.size() + command.size());
            line.append(prompt).append(command);

            m_CliState.AddLine(std::move(line));
            m_CliState.AddHistory(command);
            m_CliState.ClearInput();
            m_CliScrollToBottom = true;

            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
}

void
DevUi::DrawStatusBarPanel() const // NOLINT(readability-convert-member-functions-to-static)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float statusBarHeight = GetStatusBarHeight();

    // Position the status bar at the bottom of the viewport, spanning its entire width
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::Begin(kStatusBarPanelName,
        nullptr,
        ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar);

    const auto statusText = InplaceString<>::Format("SPF: {:.3f} ms | FPS: {:.1f} | mouse: {},{}",
        ImGui::GetIO().DeltaTime * 1000.0f,
        1.0f / ImGui::GetIO().DeltaTime,
        m_ViewPanelMousePos.GetX(),
        m_ViewPanelMousePos.GetY());

    ImGui::TextUnformatted(statusText.c_str());

    ImGui::End();
}