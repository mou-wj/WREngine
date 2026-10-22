#include "Widgets/ImFileDialogWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace {

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
        std::string normalized = NormalizePath(path).generic_string();
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
        const auto filename = path.filename().string();
        if (!filename.empty())
        {
            return filename;
        }

        const auto rootName = path.root_name().string();
        if (!rootName.empty())
        {
            return rootName;
        }

        return path.generic_string();
    }

    std::string FormatSize(std::uintmax_t size)
    {
        constexpr const char* Units[] = { "B", "KB", "MB", "GB", "TB" };

        double value = static_cast<double>(size);
        std::size_t unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < std::size(Units))
        {
            value /= 1024.0;
            ++unitIndex;
        }

        char buffer[64] = {};
        if (unitIndex == 0)
        {
            std::snprintf(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(size), Units[unitIndex]);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, Units[unitIndex]);
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
            InitialDirectory = NormalizePath(workingDirectory);
            CurrentDirectory = InitialDirectory;
        }

        SyncSelectionText();
    }

    void ImFileDialogWidget::Draw()
    {
        if (_visibility != SlateCore::EVisibility::Visible || !DialogOpen)
        {
            return;
        }

        if (CurrentDirectory.empty())
        {
            NavigateTo(InitialDirectory.empty() ? std::filesystem::current_path() : InitialDirectory, InitialDirectory.empty());
        }

        if (EntriesDirty)
        {
            RefreshEntries();
        }

        CloseReason = PendingCloseReason::None;

        bool open = DialogOpen;
        ImGui::SetNextWindowSize(ImVec2(960.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Title.c_str(), &open))
        {
            DrawPresetPane();
            ImGui::SameLine();
            ImGui::BeginGroup();
            DrawToolbar();
            DrawEntryTable();
            DrawFooter();
            ImGui::EndGroup();
        }
        ImGui::End();

        if (!open && CloseReason == PendingCloseReason::None)
        {
            CloseReason = PendingCloseReason::Cancelled;
        }

        if (CloseReason == PendingCloseReason::Confirmed)
        {
            DialogOpen = false;
        }
        else if (CloseReason == PendingCloseReason::Cancelled)
        {
            DialogOpen = false;
            SelectionConfirmed = false;
            if (OnCancel)
            {
                OnCancel();
            }
        }
        else
        {
            DialogOpen = open;
        }
    }

    void ImFileDialogWidget::Open()
    {
        DialogOpen = true;
        CloseReason = PendingCloseReason::None;
    }

    void ImFileDialogWidget::Close()
    {
        DialogOpen = false;
    }

    bool ImFileDialogWidget::IsOpen() const
    {
        return DialogOpen;
    }

    void ImFileDialogWidget::SetTitle(std::string title)
    {
        Title = std::move(title);
    }

    const std::string& ImFileDialogWidget::GetTitle() const
    {
        return Title;
    }

    void ImFileDialogWidget::SetSelectionMode(SelectionMode mode)
    {
        if (Mode == mode)
        {
            return;
        }

        Mode = mode;
        ClearSelection();
    }

    ImFileDialogWidget::SelectionMode ImFileDialogWidget::GetSelectionMode() const
    {
        return Mode;
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
        return CurrentDirectory;
    }

    void ImFileDialogWidget::SetPresetDirectories(std::vector<PresetDirectory> directories)
    {
        PresetDirectories = std::move(directories);
    }

    void ImFileDialogWidget::AddPresetDirectory(std::string label, std::filesystem::path path)
    {
        PresetDirectories.push_back(PresetDirectory{ std::move(label), NormalizePath(path) });
    }

    void ImFileDialogWidget::ClearPresetDirectories()
    {
        PresetDirectories.clear();
    }

    const std::vector<ImFileDialogWidget::PresetDirectory>& ImFileDialogWidget::GetPresetDirectories() const
    {
        return PresetDirectories;
    }

    const ImFileDialogWidget::SelectionList& ImFileDialogWidget::GetSelectedPaths() const
    {
        return SelectedPaths;
    }

    void ImFileDialogWidget::ClearSelection()
    {
        SelectedPaths.clear();
        SelectionConfirmed = false;
        SyncSelectionText();
    }

    bool ImFileDialogWidget::HasConfirmedSelection() const
    {
        return SelectionConfirmed;
    }

    void ImFileDialogWidget::ResetConfirmation()
    {
        SelectionConfirmed = false;
    }

    void ImFileDialogWidget::SetOnConfirm(ConfirmCallback callback)
    {
        OnConfirm = std::move(callback);
    }

    void ImFileDialogWidget::SetOnCancel(CancelCallback callback)
    {
        OnCancel = std::move(callback);
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
            return;
        }

        CurrentDirectory = targetDirectory;
        if (updateInitialDirectory)
        {
            InitialDirectory = targetDirectory;
        }

        EntriesDirty = true;
        LastError.clear();

        if (Mode == SelectionMode::Directory)
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
        EntriesDirty = false;
        Entries.clear();
        LastError.clear();

        if (CurrentDirectory.empty())
        {
            return;
        }

        std::error_code errorCode;
        std::filesystem::directory_iterator iterator(CurrentDirectory, errorCode);
        if (errorCode)
        {
            LastError = errorCode.message();
            return;
        }

        for (const auto& entry : iterator)
        {
            FileEntry fileEntry;
            fileEntry.Path = NormalizePath(entry.path());
            fileEntry.IsDirectory = entry.is_directory(errorCode);
            if (errorCode)
            {
                errorCode.clear();
                fileEntry.IsDirectory = false;
            }

            fileEntry.DisplayName = GetDisplayName(fileEntry.Path);
            if (!fileEntry.IsDirectory)
            {
                fileEntry.Size = entry.file_size(errorCode);
                if (errorCode)
                {
                    errorCode.clear();
                    fileEntry.Size = 0;
                }
            }

            Entries.push_back(std::move(fileEntry));
        }

        std::sort(
            Entries.begin(),
            Entries.end(),
            [](const FileEntry& lhs, const FileEntry& rhs)
            {
                if (lhs.IsDirectory != rhs.IsDirectory)
                {
                    return lhs.IsDirectory > rhs.IsDirectory;
                }

                std::string leftName = lhs.DisplayName;
                std::string rightName = rhs.DisplayName;
                std::transform(leftName.begin(), leftName.end(), leftName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(rightName.begin(), rightName.end(), rightName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return leftName < rightName;
            });
    }

    void ImFileDialogWidget::DrawPresetPane()
    {
        ImGui::BeginChild("##preset-directories", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Places");
        ImGui::Separator();

        if (!InitialDirectory.empty())
        {
            if (ImGui::Button("Initial Directory", ImVec2(-1.0f, 0.0f)))
            {
                NavigateTo(InitialDirectory, false);
            }
        }

        for (const auto& preset : PresetDirectories)
        {
            const auto label = preset.Label.empty() ? preset.Path.generic_string() : preset.Label;
            if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f)))
            {
                NavigateTo(preset.Path, false);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", preset.Path.generic_string().c_str());
            }
        }

        ImGui::EndChild();
    }

    void ImFileDialogWidget::DrawToolbar()
    {
        if (ImGui::Button("Up") && !CurrentDirectory.empty())
        {
            const auto parent = CurrentDirectory.parent_path();
            if (!parent.empty())
            {
                NavigateTo(parent, false);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            EntriesDirty = true;
        }

        if (!InitialDirectory.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Home"))
            {
                NavigateTo(InitialDirectory, false);
            }
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", CurrentDirectory.generic_string().c_str());
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

                if (!CurrentDirectory.empty())
                {
                    const auto parent = CurrentDirectory.parent_path();
                    if (!parent.empty() && parent != CurrentDirectory)
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

                for (const auto& entry : Entries)
                {
                    const bool selected = IsSelected(entry.Path);
                    const std::string entryLabel = entry.IsDirectory ? "[DIR] " + entry.DisplayName : entry.DisplayName;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool clicked = ImGui::Selectable(
                        entryLabel.c_str(),
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry.IsDirectory ? "Folder" : "File");

                    ImGui::TableSetColumnIndex(2);
                    if (entry.IsDirectory)
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

                    if (entry.IsDirectory)
                    {
                        NavigateTo(entry.Path, false);
                        continue;
                    }

                    if (Mode == SelectionMode::Directory)
                    {
                        continue;
                    }

                    const bool toggleSelection = Mode == SelectionMode::MultipleFiles;
                    SelectFile(entry.Path, toggleSelection);

                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && Mode == SelectionMode::SingleFile)
                    {
                        if (ConfirmSelection())
                        {
                            CloseReason = PendingCloseReason::Confirmed;
                        }
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    void ImFileDialogWidget::DrawFooter()
    {
        if (!LastError.empty())
        {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "%s", LastError.c_str());
        }

        const char* label = Mode == SelectionMode::Directory ? "Directory" : "File name";
        const bool readOnly = Mode != SelectionMode::SingleFile;
        ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
        ImGui::InputText(label, SelectionText.data(), SelectionText.size(), flags);

        if (Mode == SelectionMode::MultipleFiles)
        {
            ImGui::Text("%d item(s) selected", static_cast<int>(SelectedPaths.size()));
        }
        else if (Mode == SelectionMode::Directory)
        {
            ImGui::TextWrapped("Current folder: %s", CurrentDirectory.generic_string().c_str());
        }

        ImGui::Separator();

        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            CloseReason = PendingCloseReason::Cancelled;
        }

        ImGui::SameLine();

        const char* confirmText = Mode == SelectionMode::Directory ? "Select Folder" : "Open";
        if (!CanConfirmSelection())
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button(confirmText, ImVec2(160.0f, 0.0f)))
        {
            if (ConfirmSelection())
            {
                CloseReason = PendingCloseReason::Confirmed;
            }
        }

        if (!CanConfirmSelection())
        {
            ImGui::EndDisabled();
        }
    }

    void ImFileDialogWidget::SelectFile(const std::filesystem::path& path, bool toggleSelection)
    {
        SelectionConfirmed = false;

        if (!toggleSelection)
        {
            SelectedPaths = { NormalizePath(path) };
            SyncSelectionText();
            return;
        }

        const auto normalizedPath = NormalizePath(path);
        const auto normalizedKey = NormalizeForCompare(normalizedPath);
        auto it = std::find_if(
            SelectedPaths.begin(),
            SelectedPaths.end(),
            [&normalizedKey](const std::filesystem::path& selectedPath)
            {
                return NormalizeForCompare(selectedPath) == normalizedKey;
            });

        if (it != SelectedPaths.end())
        {
            SelectedPaths.erase(it);
        }
        else
        {
            SelectedPaths.push_back(normalizedPath);
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

        SelectedPaths = confirmedSelection;
        SelectionConfirmed = true;
        SyncSelectionText();

        if (OnConfirm)
        {
            OnConfirm(SelectedPaths);
        }

        return true;
    }

    bool ImFileDialogWidget::CanConfirmSelection() const
    {
        return !BuildConfirmedSelection().empty();
    }

    ImFileDialogWidget::SelectionList ImFileDialogWidget::BuildConfirmedSelection() const
    {
        if (Mode == SelectionMode::Directory)
        {
            return IsDirectory(CurrentDirectory) ? SelectionList{ CurrentDirectory } : SelectionList{};
        }

        if (!SelectedPaths.empty())
        {
            return SelectedPaths;
        }

        if (Mode != SelectionMode::SingleFile)
        {
            return {};
        }

        const std::string typedValue(SelectionText.data());
        if (typedValue.empty())
        {
            return {};
        }

        std::filesystem::path candidate = typedValue;
        if (!candidate.is_absolute())
        {
            candidate = CurrentDirectory / candidate;
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
            SelectedPaths.begin(),
            SelectedPaths.end(),
            [&normalizedKey](const std::filesystem::path& selectedPath)
            {
                return NormalizeForCompare(selectedPath) == normalizedKey;
            });
    }

    void ImFileDialogWidget::SyncSelectionText()
    {
        if (Mode == SelectionMode::Directory)
        {
            CopyStringToBuffer(CurrentDirectory.generic_string(), SelectionText);
            return;
        }

        if (SelectedPaths.empty())
        {
            CopyStringToBuffer({}, SelectionText);
            return;
        }

        if (Mode == SelectionMode::SingleFile)
        {
            CopyStringToBuffer(GetDisplayName(SelectedPaths.front()), SelectionText);
            return;
        }

        std::string joinedSelection;
        for (std::size_t index = 0; index < SelectedPaths.size(); ++index)
        {
            if (index > 0)
            {
                joinedSelection += "; ";
            }

            joinedSelection += GetDisplayName(SelectedPaths[index]);
        }

        CopyStringToBuffer(joinedSelection, SelectionText);
    }
}