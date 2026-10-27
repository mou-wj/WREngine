#include "Widgets/ImFileDialogWidget.h"
#include "Widgets/ImCommonWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace {

    std::string PathText(const std::filesystem::path& path)
    {
        const auto text = path.generic_u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    std::filesystem::path NormalizePath(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::error_code errorCode;
        auto absolutePath = path;
        if (!absolutePath.is_absolute())
        {
            absolutePath = std::filesystem::absolute(absolutePath, errorCode);
            if (errorCode)
            {
                errorCode.clear();
                absolutePath = path;
            }
        }

        const auto canonicalPath = std::filesystem::weakly_canonical(absolutePath, errorCode);
        if (!errorCode)
        {
            return canonicalPath;
        }

        return absolutePath.lexically_normal();
    }

    std::string NormalizeForCompare(const std::filesystem::path& path)
    {
        std::string normalized = PathText(NormalizePath(path));
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return normalized;
    }

    bool IsDirectory(const std::filesystem::path& path)
    {
        std::error_code errorCode;
        return std::filesystem::is_directory(path, errorCode);
    }

    bool IsRegularFile(const std::filesystem::path& path)
    {
        std::error_code errorCode;
        return std::filesystem::is_regular_file(path, errorCode);
    }

    std::string GetDisplayName(const std::filesystem::path& path)
    {
        const auto filename = PathText(path.filename());
        if (!filename.empty())
        {
            return filename;
        }

        const auto rootName = PathText(path.root_name());
        if (!rootName.empty())
        {
            return rootName;
        }

        return PathText(path);
    }

    std::string FormatSize(std::uintmax_t size)
    {
        constexpr const char* units[] = { "B", "KB", "MB", "GB", "TB" };

        double value = static_cast<double>(size);
        std::size_t unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < std::size(units))
        {
            value /= 1024.0;
            ++unitIndex;
        }

        char buffer[64] = {};
        if (unitIndex == 0)
        {
            std::snprintf(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(size), units[unitIndex]);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unitIndex]);
        }

        return buffer;
    }

    void CopyStringToBuffer(const std::string& value, std::array<char, 512>& buffer)
    {
        buffer.fill('\0');

        if (value.empty())
        {
            return;
        }

        const auto copyLength = std::min(value.size(), buffer.size() - 1);
        std::memcpy(buffer.data(), value.data(), copyLength);
        buffer[copyLength] = '\0';
    }
}

namespace ImGUISlate {

    ImFileDialogWidget::ImFileDialogWidget()
    {
        std::error_code errorCode;
        auto workingDirectory = std::filesystem::current_path(errorCode);
        if (!errorCode)
        {
            m_InitialDirectory = NormalizePath(workingDirectory);
            m_CurrentDirectory = m_InitialDirectory;
        }

        SyncSelectionText();
    }

    void ImFileDialogWidget::Draw()
    {
        if (_visibility != SlateCore::EVisibility::Visible || !m_bDialogOpen)
        {
            return;
        }

        if (m_CurrentDirectory.empty())
        {
            NavigateTo(m_InitialDirectory.empty() ? std::filesystem::current_path() : m_InitialDirectory, m_InitialDirectory.empty());
        }

        if (m_bEntriesDirty)
        {
            RefreshEntries();
        }
        if (m_bRootsDirty)
        {
            RefreshRootDirectories();
        }

        m_CloseReason = PendingCloseReason::None;

        bool open = m_bDialogOpen;
        ImGui::SetNextWindowSize(ImVec2(960.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(m_Title.c_str(), &open))
        {
            DrawPresetPane();
            ImGui::SameLine();
            ImGui::BeginGroup();
            DrawToolbar();
            DrawEntryTable();
            DrawFooter();
            DrawNewDirectoryPopup();
            ImGui::EndGroup();
        }
        ImGui::End();

        if (!open && m_CloseReason == PendingCloseReason::None)
        {
            m_CloseReason = PendingCloseReason::Cancelled;
        }

        if (m_CloseReason == PendingCloseReason::Confirmed)
        {
            m_bDialogOpen = false;
        }
        else if (m_CloseReason == PendingCloseReason::Cancelled)
        {
            m_bDialogOpen = false;
            m_bSelectionConfirmed = false;
            if (m_OnCancel)
            {
                m_OnCancel();
            }
        }
        else
        {
            m_bDialogOpen = open;
        }
    }

    void ImFileDialogWidget::Open()
    {
        m_bDialogOpen = true;
        m_bRootsDirty = true;
        m_bOpenNewDirectoryPopup = false;
        m_CloseReason = PendingCloseReason::None;
    }

    void ImFileDialogWidget::Close()
    {
        m_bDialogOpen = false;
    }

    bool ImFileDialogWidget::IsOpen() const
    {
        return m_bDialogOpen;
    }

    void ImFileDialogWidget::SetTitle(std::string title)
    {
        m_Title = std::move(title);
    }

    const std::string& ImFileDialogWidget::GetTitle() const
    {
        return m_Title;
    }

    void ImFileDialogWidget::SetSelectionMode(SelectionMode mode)
    {
        if (m_Mode == mode)
        {
            return;
        }

        m_Mode = mode;
        ClearSelection();
    }

    ImFileDialogWidget::SelectionMode ImFileDialogWidget::GetSelectionMode() const
    {
        return m_Mode;
    }

    void ImFileDialogWidget::SetInitialDirectory(const std::filesystem::path& directory)
    {
        NavigateTo(directory, true);
    }

    void ImFileDialogWidget::SetCurrentDirectory(const std::filesystem::path& directory)
    {
        NavigateTo(directory, false);
    }

    const std::filesystem::path& ImFileDialogWidget::GetCurrentDirectory() const
    {
        return m_CurrentDirectory;
    }

    void ImFileDialogWidget::SetPresetDirectories(std::vector<PresetDirectory> directories)
    {
        m_PresetDirectories = std::move(directories);
    }

    void ImFileDialogWidget::AddPresetDirectory(std::string label, std::filesystem::path path)
    {
        m_PresetDirectories.push_back(PresetDirectory{ std::move(label), NormalizePath(path) });
    }

    void ImFileDialogWidget::ClearPresetDirectories()
    {
        m_PresetDirectories.clear();
    }

    const std::vector<ImFileDialogWidget::PresetDirectory>& ImFileDialogWidget::GetPresetDirectories() const
    {
        return m_PresetDirectories;
    }

    const ImFileDialogWidget::SelectionList& ImFileDialogWidget::GetSelectedPaths() const
    {
        return m_SelectedPaths;
    }

    void ImFileDialogWidget::ClearSelection()
    {
        m_SelectedPaths.clear();
        m_bSelectionConfirmed = false;
        SyncSelectionText();
    }

    bool ImFileDialogWidget::HasConfirmedSelection() const
    {
        return m_bSelectionConfirmed;
    }

    void ImFileDialogWidget::ResetConfirmation()
    {
        m_bSelectionConfirmed = false;
    }

    void ImFileDialogWidget::SetOnConfirm(ConfirmCallback callback)
    {
        m_OnConfirm = std::move(callback);
    }

    void ImFileDialogWidget::SetOnCancel(CancelCallback callback)
    {
        m_OnCancel = std::move(callback);
    }

    void ImFileDialogWidget::NavigateTo(const std::filesystem::path& directory, bool updateInitialDirectory)
    {
        if (directory.empty())
        {
            return;
        }

        auto targetDirectory = NormalizePath(directory);
        if (!IsDirectory(targetDirectory) && IsRegularFile(targetDirectory))
        {
            targetDirectory = targetDirectory.parent_path();
        }

        if (!IsDirectory(targetDirectory))
        {
            m_LastError = "Directory is unavailable: " + PathText(targetDirectory);
            return;
        }

        m_CurrentDirectory = targetDirectory;
        if (updateInitialDirectory)
        {
            m_InitialDirectory = targetDirectory;
        }

        m_bEntriesDirty = true;
        m_LastError.clear();

        if (m_Mode == SelectionMode::Directory)
        {
            SyncSelectionText();
        }
        else
        {
            ClearSelection();
        }
    }

    void ImFileDialogWidget::RefreshEntries()
    {
        m_bEntriesDirty = false;
        m_Entries.clear();
        m_LastError.clear();

        if (m_CurrentDirectory.empty())
        {
            return;
        }

        std::error_code errorCode;
        std::filesystem::directory_iterator iterator(m_CurrentDirectory, errorCode);
        if (errorCode)
        {
            m_LastError = errorCode.message();
            return;
        }

        for (const auto& entry : iterator)
        {
            FileEntry fileEntry;
            fileEntry.Path = NormalizePath(entry.path());
            fileEntry.bIsDirectory = entry.is_directory(errorCode);
            if (errorCode)
            {
                errorCode.clear();
                fileEntry.bIsDirectory = false;
            }

            fileEntry.DisplayName = GetDisplayName(fileEntry.Path);
            if (!fileEntry.bIsDirectory)
            {
                fileEntry.Size = entry.file_size(errorCode);
                if (errorCode)
                {
                    errorCode.clear();
                    fileEntry.Size = 0;
                }
            }

            m_Entries.push_back(std::move(fileEntry));
        }

        std::sort(
            m_Entries.begin(),
            m_Entries.end(),
            [](const FileEntry& lhs, const FileEntry& rhs)
            {
                if (lhs.bIsDirectory != rhs.bIsDirectory)
                {
                    return lhs.bIsDirectory > rhs.bIsDirectory;
                }

                std::string leftName = lhs.DisplayName;
                std::string rightName = rhs.DisplayName;
                std::transform(leftName.begin(), leftName.end(), leftName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(rightName.begin(), rightName.end(), rightName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return leftName < rightName;
            });
    }

    void ImFileDialogWidget::RefreshRootDirectories()
    {
        m_bRootsDirty = false;
        m_RootDirectories.clear();
#ifdef _WIN32
        // Probe only when opening or refreshing, not on every UI frame.
        // Filesystem operations stay here; no native window or drive handles are kept.
        for (char letter = 'A'; letter <= 'Z'; ++letter)
        {
            const auto root = std::filesystem::path(std::string(1, letter) + ":/");
            if (IsDirectory(root)) m_RootDirectories.push_back(root);
        }
#else
        m_RootDirectories.emplace_back("/");
#endif
    }

    void ImFileDialogWidget::DrawPresetPane()
    {
        ImGui::BeginChild("##preset-directories", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Places");
        ImGui::Separator();

        if (!m_InitialDirectory.empty())
        {
            if (ImGui::Button("Initial Directory", ImVec2(-1.0f, 0.0f)))
            {
                NavigateTo(m_InitialDirectory, false);
            }
        }

        for (const auto& preset : m_PresetDirectories)
        {
            const auto label = preset.Label.empty() ? PathText(preset.Path) : preset.Label;
            if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f)))
            {
                NavigateTo(preset.Path, false);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", PathText(preset.Path).c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Drives");
        for (const auto& root : m_RootDirectories)
        {
            if (ImGui::Selectable(PathText(root).c_str(), m_CurrentDirectory.root_path() == root))
            {
                NavigateTo(root, false);
            }
        }

        ImGui::EndChild();
    }

    void ImFileDialogWidget::DrawToolbar()
    {
        if (ImGui::Button("Up") && !m_CurrentDirectory.empty())
        {
            const auto parent = m_CurrentDirectory.parent_path();
            if (!parent.empty())
            {
                NavigateTo(parent, false);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            m_bEntriesDirty = true;
            m_bRootsDirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("New folder")) m_bOpenNewDirectoryPopup = true;

        if (!m_InitialDirectory.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Home"))
            {
                NavigateTo(m_InitialDirectory, false);
            }
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", PathText(m_CurrentDirectory).c_str());
        ImGui::Separator();
    }

    void ImFileDialogWidget::DrawEntryTable()
    {
        const float footerReserve = 110.0f;
        if (ImGui::BeginChild("##entries", ImVec2(0.0f, -footerReserve), ImGuiChildFlags_Borders))
        {
            if (ImGui::BeginTable("##file-dialog-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();

                if (!m_CurrentDirectory.empty())
                {
                    const auto parent = m_CurrentDirectory.parent_path();
                    if (!parent.empty() && parent != m_CurrentDirectory)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const bool clicked = ImGui::Selectable("[..]", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted("Parent");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted("-");

                        if (clicked)
                        {
                            NavigateTo(parent, false);
                        }
                    }
                }

                for (const auto& entry : m_Entries)
                {
                    const bool selected = IsSelected(entry.Path);
                    const std::string entryLabel = entry.bIsDirectory ? "[DIR] " + entry.DisplayName : entry.DisplayName;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool clicked = ImGui::Selectable(
                        entryLabel.c_str(),
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry.bIsDirectory ? "Folder" : "File");

                    ImGui::TableSetColumnIndex(2);
                    if (entry.bIsDirectory)
                    {
                        ImGui::TextUnformatted("-");
                    }
                    else
                    {
                        const auto sizeString = FormatSize(entry.Size);
                        ImGui::TextUnformatted(sizeString.c_str());
                    }

                    if (!clicked)
                    {
                        continue;
                    }

                    if (entry.bIsDirectory)
                    {
                        NavigateTo(entry.Path, false);
                        continue;
                    }

                    if (m_Mode == SelectionMode::Directory)
                    {
                        continue;
                    }

                    const bool toggleSelection = m_Mode == SelectionMode::MultipleFiles;
                    SelectFile(entry.Path, toggleSelection);

                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Mode == SelectionMode::SingleFile)
                    {
                        if (ConfirmSelection())
                        {
                            m_CloseReason = PendingCloseReason::Confirmed;
                        }
                    }
                }

                ImGui::EndTable();
            }

            // A scrolling table is a child window. Include it when testing the
            // entry area, so right click works on rows as well as empty space.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                ImGui::OpenPopup("Directory actions");
            if (ImGui::BeginPopup("Directory actions"))
            {
                if (ImGui::MenuItem("New folder")) m_bOpenNewDirectoryPopup = true;
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }

    void ImFileDialogWidget::DrawFooter()
    {
        if (!m_LastError.empty())
        {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "%s", m_LastError.c_str());
        }

        const char* label = m_Mode == SelectionMode::Directory ? "Directory" : "File name";
        const bool readOnly = m_Mode != SelectionMode::SingleFile;
        ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
        ImGui::InputText(label, m_SelectionText.data(), m_SelectionText.size(), flags);

        if (m_Mode == SelectionMode::MultipleFiles)
        {
            ImGui::Text("%d item(s) selected", static_cast<int>(m_SelectedPaths.size()));
        }
        else if (m_Mode == SelectionMode::Directory)
        {
            ImGui::TextWrapped("Current folder: %s", PathText(m_CurrentDirectory).c_str());
        }

        ImGui::Separator();

        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_CloseReason = PendingCloseReason::Cancelled;
        }

        ImGui::SameLine();

        const char* confirmText = m_Mode == SelectionMode::Directory ? "Select Folder" : "Open";
        if (!CanConfirmSelection())
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button(confirmText, ImVec2(160.0f, 0.0f)))
        {
            if (ConfirmSelection())
            {
                m_CloseReason = PendingCloseReason::Confirmed;
            }
        }

        if (!CanConfirmSelection())
        {
            ImGui::EndDisabled();
        }
    }

    void ImFileDialogWidget::DrawNewDirectoryPopup()
    {
        if (m_bOpenNewDirectoryPopup)
        {
            m_bOpenNewDirectoryPopup = false;
            m_NewDirectoryName.clear();
            m_NewDirectoryError.clear();
            ImGui::OpenPopup("New folder");
        }
        if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Create in: %s", PathText(m_CurrentDirectory).c_str());
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImPropertyControls::Text("Name", m_NewDirectoryName);
            if (!m_NewDirectoryError.empty())
                ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "%s", m_NewDirectoryError.c_str());
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Create"))
            {
                // Accept one name only. Never turn this action into a path walk
                // or silently reuse an existing directory/file.
                const auto& name = m_NewDirectoryName;
                if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
                    name.find_first_of("/\\:*?\"<>|") != std::string::npos ||
                    std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
                    m_NewDirectoryError = "Enter a folder name without paths or reserved characters.";
                else
                {
                    std::error_code error;
                    const auto directory = m_CurrentDirectory / std::filesystem::u8path(name);
                    if (std::filesystem::create_directory(directory, error))
                    {
                        NavigateTo(directory, false);
                        ImGui::CloseCurrentPopup();
                    }
                    else m_NewDirectoryError = error ? error.message() : "A file or folder with this name already exists.";
                }
            }
            ImGui::EndPopup();
        }
    }

    void ImFileDialogWidget::SelectFile(const std::filesystem::path& path, bool toggleSelection)
    {
        m_bSelectionConfirmed = false;

        if (!toggleSelection)
        {
            m_SelectedPaths = { NormalizePath(path) };
            SyncSelectionText();
            return;
        }

        const auto normalizedPath = NormalizePath(path);
        const auto normalizedKey = NormalizeForCompare(normalizedPath);
        auto it = std::find_if(
            m_SelectedPaths.begin(),
            m_SelectedPaths.end(),
            [&normalizedKey](const std::filesystem::path& selectedPath)
            {
                return NormalizeForCompare(selectedPath) == normalizedKey;
            });

        if (it != m_SelectedPaths.end())
        {
            m_SelectedPaths.erase(it);
        }
        else
        {
            m_SelectedPaths.push_back(normalizedPath);
        }

        SyncSelectionText();
    }

    bool ImFileDialogWidget::ConfirmSelection()
    {
        const auto confirmedSelection = BuildConfirmedSelection();
        if (confirmedSelection.empty())
        {
            return false;
        }

        m_SelectedPaths = confirmedSelection;
        m_bSelectionConfirmed = true;
        SyncSelectionText();

        if (m_OnConfirm)
        {
            m_OnConfirm(m_SelectedPaths);
        }

        return true;
    }

    bool ImFileDialogWidget::CanConfirmSelection() const
    {
        return !BuildConfirmedSelection().empty();
    }

    ImFileDialogWidget::SelectionList ImFileDialogWidget::BuildConfirmedSelection() const
    {
        if (m_Mode == SelectionMode::Directory)
        {
            return IsDirectory(m_CurrentDirectory) ? SelectionList{ m_CurrentDirectory } : SelectionList{};
        }

        if (!m_SelectedPaths.empty())
        {
            return m_SelectedPaths;
        }

        if (m_Mode != SelectionMode::SingleFile)
        {
            return {};
        }

        const std::string typedValue(m_SelectionText.data());
        if (typedValue.empty())
        {
            return {};
        }

        std::filesystem::path candidate = std::filesystem::u8path(typedValue);
        if (!candidate.is_absolute())
        {
            candidate = m_CurrentDirectory / candidate;
        }

        candidate = NormalizePath(candidate);
        if (!IsRegularFile(candidate))
        {
            return {};
        }

        return { candidate };
    }

    bool ImFileDialogWidget::IsSelected(const std::filesystem::path& path) const
    {
        const auto normalizedKey = NormalizeForCompare(path);
        return std::any_of(
            m_SelectedPaths.begin(),
            m_SelectedPaths.end(),
            [&normalizedKey](const std::filesystem::path& selectedPath)
            {
                return NormalizeForCompare(selectedPath) == normalizedKey;
            });
    }

    void ImFileDialogWidget::SyncSelectionText()
    {
        if (m_Mode == SelectionMode::Directory)
        {
            CopyStringToBuffer(PathText(m_CurrentDirectory), m_SelectionText);
            return;
        }

        if (m_SelectedPaths.empty())
        {
            CopyStringToBuffer({}, m_SelectionText);
            return;
        }

        if (m_Mode == SelectionMode::SingleFile)
        {
            CopyStringToBuffer(GetDisplayName(m_SelectedPaths.front()), m_SelectionText);
            return;
        }

        std::string joinedSelection;
        for (std::size_t index = 0; index < m_SelectedPaths.size(); ++index)
        {
            if (index > 0)
            {
                joinedSelection += "; ";
            }

            joinedSelection += GetDisplayName(m_SelectedPaths[index]);
        }

        CopyStringToBuffer(joinedSelection, m_SelectionText);
    }
}
