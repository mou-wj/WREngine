#pragma once

#include "ImWidget.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ImGUISlate {

    class IMGUISLATE_API ImFileDialogWidget final : public ImWidget
    {
        DECLARE_TYPE_ID_DERIVED_TYPE(ImFileDialogWidget, ImWidget)

    public:
        enum class SelectionMode
        {
            SingleFile,
            MultipleFiles,
            Directory
        };

        struct PresetDirectory
        {
            std::string Label;
            std::filesystem::path Path;
        };

        using SelectionList = std::vector<std::filesystem::path>;
        using ConfirmCallback = std::function<void(const SelectionList&)>;
        using CancelCallback = std::function<void()>;

        ImFileDialogWidget();

        virtual void Draw() override;

        void Open();
        void Close();
        bool IsOpen() const;

        void SetTitle(std::string title);
        const std::string& GetTitle() const;

        void SetSelectionMode(SelectionMode mode);
        SelectionMode GetSelectionMode() const;

        void SetInitialDirectory(const std::filesystem::path& directory);
        void SetCurrentDirectory(const std::filesystem::path& directory);
        const std::filesystem::path& GetCurrentDirectory() const;

        void SetPresetDirectories(std::vector<PresetDirectory> directories);
        void AddPresetDirectory(std::string label, std::filesystem::path path);
        void ClearPresetDirectories();
        const std::vector<PresetDirectory>& GetPresetDirectories() const;

        const SelectionList& GetSelectedPaths() const;
        void ClearSelection();

        bool HasConfirmedSelection() const;
        void ResetConfirmation();

        void SetOnConfirm(ConfirmCallback callback);
        void SetOnCancel(CancelCallback callback);

    private:
        struct FileEntry
        {
            std::filesystem::path Path;
            std::string DisplayName;
            bool bIsDirectory = false;
            std::uintmax_t Size = 0;
        };

        enum class PendingCloseReason
        {
            None,
            Confirmed,
            Cancelled
        };

        void NavigateTo(const std::filesystem::path& directory, bool updateInitialDirectory);
        void RefreshEntries();
        void DrawPresetPane();
        void DrawToolbar();
        void DrawEntryTable();
        void DrawFooter();

        void SelectFile(const std::filesystem::path& path, bool toggleSelection);
        bool ConfirmSelection();
        bool CanConfirmSelection() const;
        SelectionList BuildConfirmedSelection() const;
        bool IsSelected(const std::filesystem::path& path) const;
        void SyncSelectionText();

        std::string m_Title = "Select Files";
        SelectionMode m_Mode = SelectionMode::SingleFile;

        std::filesystem::path m_InitialDirectory;
        std::filesystem::path m_CurrentDirectory;

        std::vector<PresetDirectory> m_PresetDirectories;
        std::vector<FileEntry> m_Entries;
        SelectionList m_SelectedPaths;

        std::array<char, 512> m_SelectionText{};

        std::string m_LastError;

        ConfirmCallback m_OnConfirm;
        CancelCallback m_OnCancel;

        bool m_bDialogOpen = true;
        bool m_bSelectionConfirmed = false;
        bool m_bEntriesDirty = true;
        PendingCloseReason m_CloseReason = PendingCloseReason::None;
    };
}