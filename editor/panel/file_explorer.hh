/**
* file_explorer.hh
*
* It is a panel that shows the current project directory with all associated 
* files and folders
**/
#ifndef PEQUOD_PANEL_FILE_EXPLORER_IMPL_HH_
#define PEQUOD_PANEL_FILE_EXPLORER_IMPL_HH_


#include <panel/panel.hh>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Pequod {
    class FileExplorer : public Panel {
    public:
        FileExplorer();

        void Initialize() override;

        void ReloadFolders();

        void Draw() override;

        void DrawFolder(fs::path root);

        void SetRootDirectory(fs::path init_directory);

        void CreateFile(fs::path file_path);

        std::vector<fs::path> GetItems(fs::path);

    private:
        fs::path root_directory;

        std::unordered_map<std::string, std::vector<fs::directory_entry> > folder_data;
    };
} // namespace Pequod

#endif
