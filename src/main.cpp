#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "TextEditor.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <GL/gl.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <algorithm>
#include <future>
#include <atomic>

namespace fs = std::filesystem;

// --- GLOBAL PATH RESOLVER ---
std::string GetAssetPath(const std::string& filename) {
    if (fs::exists(fs::current_path() / filename)) return (fs::current_path() / filename).string();
    if (fs::exists("/usr/share/featherforge/" + filename)) return "/usr/share/featherforge/" + filename;
    return filename;
}

// --- STATE VARIABLES ---
static bool show_new_folder_popup = false;
static GLuint g_LogoTextureID = 0;
static char new_folder_name[128] = "new_folder";
static std::string console_log = "Welcome to FeatherForge.\n[System]: Ready.\n";
static std::string selected_file = "";
static std::string compile_target = ""; 
static std::string active_workspace_path = "."; 
static TextEditor editor; 

static bool show_new_file_popup = false;
static char new_file_name[128] = "test.cpp";
static bool show_rename_popup = false;
static char rename_buf[256] = "";
static std::string file_to_rename = "";

struct FluxSettings {
    bool enable_clangd = false;
    bool enable_gdb = false;
};
static FluxSettings settings;
static bool show_settings_popup = false;

struct GitFile { std::string status; std::string path; };
static std::vector<GitFile> git_modified_files;
static char git_commit_message[256] = "";
static bool is_git_repo = false;

// --- SETUP WIZARD STATE ---
static bool show_setup_wizard = false;
static bool setup_has_gcc = false;
static bool setup_has_clang = false;

// --- NESTED FILE TREE STATE ---
struct FileNode {
    std::string name;
    std::string fullPath; 
    std::map<std::string, FileNode> children; 
};
static FileNode project_root_node = {"Root", "."};

// --- PENDING DELETE STATE ---
static std::string pending_delete_path = "";
static bool pending_delete_is_folder = false;

// --- BACKGROUND LINTER STATE ---
static std::future<TextEditor::ErrorMarkers> g_LinterFuture;
static std::atomic<bool> g_IsLinterRunning = false;
static std::string g_LinterTargetFile = "";

// --- SMART PARSER & CONSOLE ---
struct GCCErrorEntry {
    std::string filePath; int lineNumber = -1; int columnNumber = -1; 
    std::string severity; std::string message; std::string rawLine;   
};

struct JumpRequest {
    std::string filePath; int line; int column; bool requested = false;
};

class GCCErrorParser {
public:
    void ParseOutput(const std::string& rawOutput) {
        m_Errors.clear();
        std::istringstream stream(rawOutput); std::string line;
        while (std::getline(stream, line)) {
            std::smatch match; if (line.empty()) continue;
            
            if (line.find("error:") == std::string::npos && line.find("warning:") == std::string::npos) continue;
            
            if (std::regex_match(line, match, m_GCCRegex)) {
                GCCErrorEntry entry;
                entry.filePath = match[1].str(); entry.lineNumber = std::stoi(match[2].str());
                entry.columnNumber = std::stoi(match[3].str()); entry.severity = match[4].str();
                entry.message = match[5].str(); entry.rawLine = line; m_Errors.push_back(entry);
            }
        }
    }
    const std::vector<GCCErrorEntry>& GetErrors() const { return m_Errors; }
    void Clear() { m_Errors.clear(); }
private:
    std::vector<GCCErrorEntry> m_Errors;
    std::regex m_GCCRegex = std::regex(R"(^([^:\s]+):(\d+):(\d+):\s*(error|warning|note):\s*(.*)$)");
};

struct ConsoleLine { std::string text; bool isClickable = false; GCCErrorEntry errorData; };

class InteractiveConsole {
public:
    void ProcessBuffer(const std::string& rawOutput, const GCCErrorParser& parser) {
        m_Lines.clear(); m_ScrollToBottom = true;
        std::unordered_map<std::string, GCCErrorEntry> errorMap;
        for (const auto& err : parser.GetErrors()) errorMap[err.rawLine] = err;
        std::istringstream stream(rawOutput); std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            ConsoleLine cLine; cLine.text = line;
            auto it = errorMap.find(line);
            if (it != errorMap.end()) { cLine.isClickable = true; cLine.errorData = it->second; }
            m_Lines.push_back(cLine);
        }
    }
    void Render(JumpRequest& outJumpRequest) {
        if (ImGui::SmallButton("Clear Console")) { m_Lines.clear(); console_log.clear(); }
        ImGui::Separator();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (int i = 0; i < m_Lines.size(); i++) {
            const auto& cLine = m_Lines[i]; ImGui::PushID(i);
            if (cLine.isClickable) {
                ImVec4 textColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                if (cLine.errorData.severity == "error") textColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                else if (cLine.errorData.severity == "warning") textColor = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
                std::string displayText = "[" + cLine.errorData.severity + "] " + cLine.errorData.filePath + ":" + std::to_string(cLine.errorData.lineNumber) + " - " + cLine.errorData.message;
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                if (ImGui::Selectable(displayText.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        outJumpRequest.requested = true; outJumpRequest.filePath = cLine.errorData.filePath;
                        outJumpRequest.line = cLine.errorData.lineNumber; outJumpRequest.column = cLine.errorData.columnNumber;
                    }
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to jump to line %d", cLine.errorData.lineNumber);
            } else { ImGui::TextUnformatted(cLine.text.c_str()); }
            ImGui::PopID();
        }
        if (m_ScrollToBottom) { ImGui::SetScrollHereY(1.0f); m_ScrollToBottom = false; }
        ImGui::EndChild();
    }
private:
    std::vector<ConsoleLine> m_Lines; bool m_ScrollToBottom = false;
};

static GCCErrorParser gcc_parser;
static InteractiveConsole interactive_console;
static JumpRequest pending_jump;

void LogToConsole(const std::string& msg, bool clear = false) {
    if (clear) console_log.clear();
    console_log += msg; gcc_parser.ParseOutput(console_log);
    interactive_console.ProcessBuffer(console_log, gcc_parser);
}

// --- ENGINE: SETUP WIZARD ---
void RunSetupChecks() {
    setup_has_gcc = (system("which g++ > /dev/null 2>&1") == 0);
    setup_has_clang = (system("which clang++ > /dev/null 2>&1") == 0);
}
void SaveSetupComplete() { std::ofstream out(GetAssetPath("featherforge.setup")); out.close(); }
bool IsSetupComplete() { return fs::exists(GetAssetPath("featherforge.setup")); }

// --- ENGINE: CONFIG I/O ---
void SaveSettings() {
    std::ofstream out("featherforge.cfg"); if (out.is_open()) {
        out << "enable_clangd=" << settings.enable_clangd << "\n" << "enable_gdb=" << settings.enable_gdb << "\n"; out.close();
    }
}
void LoadSettings() {
    std::ifstream in("featherforge.cfg"); if (in.is_open()) {
        std::string line; while (std::getline(in, line)) {
            size_t eq_pos = line.find('='); if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos); std::string val = line.substr(eq_pos + 1);
                if (key == "enable_clangd") settings.enable_clangd = (val == "1");
                else if (key == "enable_gdb") settings.enable_gdb = (val == "1");
            }
        } in.close();
    }
}

// --- ENGINE: ASYNC CLANGD SMART LINTER ---
void StartClangDiagnostics(const std::string& filepath)
{
    if (!settings.enable_clangd || filepath.empty())
    {
        editor.SetErrorMarkers({});
        return;
    }

    editor.SetErrorMarkers({});
    g_LinterTargetFile = filepath;
    g_IsLinterRunning = true;

    std::string includes = "-I" + active_workspace_path + "/include ";

    std::string vendor_dir = active_workspace_path + "/vendor";

    if (fs::exists(vendor_dir))
    {
        for (const auto& entry : fs::recursive_directory_iterator(vendor_dir))
        {
            if (entry.is_directory())
                includes += "-I\"" + entry.path().string() + "\" ";
        }
    }

    std::string cmd =
        "clang++ -fsyntax-only -std=c++17 "
        + includes +
        "\"" + filepath + "\" 2>&1";

    g_LinterFuture = std::async(std::launch::async, [cmd]()
    {
        TextEditor::ErrorMarkers markers;

        FILE* pipe = popen(cmd.c_str(), "r");

        if (pipe)
        {
            char buffer[512];

            while (fgets(buffer, sizeof(buffer), pipe))
            {
                std::string line(buffer);

                if (line.find("error:") == std::string::npos &&
                    line.find("warning:") == std::string::npos)
                    continue;

                size_t colon1 = line.find(':');

                if (colon1 == std::string::npos)
                    continue;

                size_t colon2 = line.find(':', colon1 + 1);

                if (colon2 == std::string::npos)
                    continue;

                try
                {
                    int lineNum = std::stoi(
                        line.substr(colon1 + 1,
                                    colon2 - colon1 - 1));

                    size_t msg = line.find(':', colon2 + 1);

                    if (msg != std::string::npos)
                        markers[lineNum] = line.substr(msg + 1);
                }
                catch (...)
                {
                }
            }

            pclose(pipe);
        }

        return markers;
    });
}
void UpdateClangDiagnostics() {
    if (!g_IsLinterRunning) return;
    if (g_LinterFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        TextEditor::ErrorMarkers result = g_LinterFuture.get(); if (g_LinterTargetFile == selected_file) editor.SetErrorMarkers(result); g_IsLinterRunning = false; }
}

// --- ENGINE: FILE I/O OPERATIONS ---
void LoadFileToEditor(const std::string& filepath) {
    std::ifstream infile(filepath); if (infile.is_open()) {
        std::stringstream buffer; buffer << infile.rdbuf(); editor.SetText(buffer.str()); LogToConsole("[Editor]: Loaded " + filepath + "\n");
        std::string ext = fs::path(filepath).extension().string();
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c") { editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus()); StartClangDiagnostics(filepath); }
        else { editor.SetLanguageDefinition(TextEditor::LanguageDefinition()); editor.SetErrorMarkers({}); g_IsLinterRunning = false; }
    } else { LogToConsole("[Error]: Could not open file " + filepath + "\n"); }
}

static std::set<std::string> binary_extensions = {".o", ".a", ".so", ".exe", ".png", ".jpg", ".jpeg", ".gif", ".ttf", ".otf", ".wav", ".mp3", ".zip"};
void ScanDirRecursive(const fs::path& path, FileNode& parentNode) {
    for (const auto& entry : fs::directory_iterator(path)) {
        std::string name = entry.path().filename().string(); if (name == ".git" || name == "build") continue;
        if (entry.is_directory()) { parentNode.children[name] = {name, entry.path().string()}; ScanDirRecursive(entry.path(), parentNode.children[name]); }
        else if (entry.is_regular_file()) { std::string ext = entry.path().extension().string(); if (binary_extensions.find(ext) == binary_extensions.end()) { parentNode.children[name] = {name, entry.path().string()}; } }
    }
}
void ScanProjectDirectory(const std::string& path) {
    project_root_node.children.clear(); project_root_node.fullPath = path; project_root_node.name = (path == ".") ? "Root" : fs::path(path).filename().string(); 
    try { 
        ScanDirRecursive(path, project_root_node); 
        if (!project_root_node.children.empty() && selected_file == "") { 
            for (const auto& [name, child] : project_root_node.children) { 
                if (child.children.empty()) { selected_file = child.fullPath; LoadFileToEditor(selected_file); break; } 
            } 
        } 
    } catch (const fs::filesystem_error& e) {}
}
void SaveCurrentFile()
{
    if (selected_file.empty())
        return;

    std::ofstream outfile(selected_file);

    if (!outfile.is_open())
    {
        LogToConsole("[Error]: Could not open file for writing.\n");
        return;
    }

    outfile << editor.GetText();
    outfile.close();

    LogToConsole("[System]: Saved " + selected_file + " successfully.\n");
    StartClangDiagnostics(selected_file);
}
void CreateNewFile(const std::string& filename) {
    std::string target_path = active_workspace_path + "/" + filename; if (!fs::exists(target_path)) {
        fs::create_directories(fs::path(target_path).parent_path()); std::ofstream outfile(target_path); outfile << "// Start coding here...\n"; outfile.close();
        ScanProjectDirectory(active_workspace_path); selected_file = target_path; LoadFileToEditor(selected_file); 
    }
}

// --- ENGINE: GIT BRIDGE (Forward Declarations) ---
void GitRefreshStatus(); 
void GitInitRepo(); 
void GitCommitChanges(const std::string& msg);

// --- ENGINE: OPEN PROJECT ---
void OpenProjectFolder() {
    FILE* pipe = popen("zenity --file-selection --directory --title=\"Open FeatherForge Project\" 2>/dev/null", "r"); if (!pipe) return;
    char buffer[512]; std::string chosen_path = ""; if (fgets(buffer, sizeof(buffer), pipe) != nullptr) { chosen_path = buffer; if (!chosen_path.empty() && chosen_path.back() == '\n') chosen_path.pop_back(); } pclose(pipe);
    if (!chosen_path.empty() && fs::exists(chosen_path)) { active_workspace_path = chosen_path; selected_file = ""; compile_target = ""; editor.SetText(""); ScanProjectDirectory(active_workspace_path); GitRefreshStatus(); LogToConsole("[System]: Opened Project: " + active_workspace_path + "\n"); }
}

// --- ENGINE: JUMP ACTION ENGINE ---
void ExecuteJumpAction(const JumpRequest& req) {
    if (!req.requested) return; if (req.filePath != selected_file) { if (fs::exists(req.filePath)) { SaveCurrentFile(); selected_file = req.filePath; LoadFileToEditor(selected_file); } else return; }
    int targetLine = std::max(0, req.line - 1); int targetColumn = std::max(0, req.column - 1);
    editor.SetCursorPosition(TextEditor::Coordinates(targetLine, targetColumn)); editor.EnsureCursorVisible(); ImGui::SetWindowFocus("Editor");
}

// --- ENGINE: GIT BRIDGE DEFINITIONS ---
void GitInitRepo() { 
    std::string cmd = "cd \"" + active_workspace_path + "\" && git init 2>&1"; 
    FILE* pipe = popen(cmd.c_str(), "r"); if(pipe) pclose(pipe); GitRefreshStatus(); 
}
void GitRefreshStatus() {
    git_modified_files.clear(); 
    std::string cmd = "cd \"" + active_workspace_path + "\" && git status --porcelain 2>&1"; 
    FILE* pipe = popen(cmd.c_str(), "r"); if (!pipe) return;
    char buffer[512]; std::string output = ""; while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer; pclose(pipe);
    if (output.find("fatal: not a git repository") != std::string::npos) { is_git_repo = false; return; } is_git_repo = true;
    std::istringstream iss(output); std::string line; while (std::getline(iss, line)) { if (line.length() >= 3) { git_modified_files.push_back({line.substr(0, 2), line.substr(3)}); } }
}
void GitCommitChanges(const std::string& msg) {
    if (msg.empty()) return; 
    
    std::string escaped_msg = msg;
    size_t pos = 0;
    while ((pos = escaped_msg.find('"', pos)) != std::string::npos) {
        escaped_msg.replace(pos, 1, "\\\""); 
    }
    
    std::string cmd = "cd \"" + active_workspace_path + "\" && git add -A && git commit -m \"" + escaped_msg + "\" 2>&1"; 
    
    LogToConsole("[Git]: Committing changes...\n");
    FILE* pipe = popen(cmd.c_str(), "r"); 
    if (pipe) { char buffer[256]; while (fgets(buffer, sizeof(buffer), pipe) != nullptr) LogToConsole(buffer); pclose(pipe); } 
    git_commit_message[0] = '\0'; 
    GitRefreshStatus();
}

// --- ENGINE: SMART COMPILER BRIDGE ---
void CompileAndRun(bool debug_mode = false)
{
    std::string file_to_compile = compile_target.empty() ? selected_file : compile_target;

    if (file_to_compile.empty())
        return;

    SaveCurrentFile();

    if (!fs::exists(active_workspace_path + "/build"))
        fs::create_directory(active_workspace_path + "/build");

    fs::path p(file_to_compile);

    std::string exe_name = p.stem().string();
    std::string out_path = active_workspace_path + "/build/" + exe_name;

    // ---------------- Compiler flags ----------------

    std::string flags = debug_mode ? "-g " : "-O2 ";

    flags += "-I\"" + active_workspace_path + "/include\" ";

    std::string vendor_dir = active_workspace_path + "/vendor";

    if (fs::exists(vendor_dir))
    {
        for (const auto& entry : fs::recursive_directory_iterator(vendor_dir))
        {
            if (entry.is_directory())
            {
                flags += "-I\"" + entry.path().string() + "\" ";
            }
        }
    }

    // ---------------- Library detection ----------------

    std::string lib_links;

    std::vector<std::string> lib_dirs =
    {
        active_workspace_path + "/lib",
        active_workspace_path + "/vendor/lib"
    };

    std::unordered_set<std::string> linked_libs;

    for (const auto& lib_dir : lib_dirs)
    {
        if (!fs::exists(lib_dir))
            continue;

        // Tell the linker where to search
        lib_links += "-L\"" + lib_dir + "\" ";

        // -------- Pass 1 : Prefer static libraries --------

        for (const auto& entry : fs::directory_iterator(lib_dir))
        {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().extension() != ".a")
                continue;

            std::string filename = entry.path().stem().string(); // libraylib

            linked_libs.insert(filename);

            lib_links += "\"" + entry.path().string() + "\" ";
        }

        // -------- Pass 2 : Shared libraries --------

        for (const auto& entry : fs::directory_iterator(lib_dir))
        {
            if (!entry.is_regular_file())
                continue;

            std::string filename = entry.path().filename().string();

            // Accept .so, .so.600, .so.600.0.0, etc.
            size_t soPos = filename.find(".so");

            if (soPos == std::string::npos)
                continue;

            std::string stem = filename.substr(0, soPos); // libraylib

            if (linked_libs.count(stem))
                continue;

            linked_libs.insert(stem);

            if (stem.rfind("lib", 0) == 0)
                stem = stem.substr(3);

            lib_links += "-l" + stem + " ";
        }
    }

    // ---------------- System libraries ----------------

    std::string sys_libs =
        "-lglfw "
        "-lGL "
        "-lX11 "
        "-lpthread "
        "-lXrandr "
        "-lXi "
        "-ldl ";

    // ---------------- Compile command ----------------

    std::string compile_cmd =
        "g++ " +
        flags +
        "\"" + file_to_compile + "\" " +
        lib_links +
        sys_libs +
        "-o \"" + out_path + "\" 2>&1";

    LogToConsole("\n[Compiler]: Running " + compile_cmd + "\n", true);

    FILE* pipe = popen(compile_cmd.c_str(), "r");

    if (!pipe)
        return;

    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe))
    {
        LogToConsole(buffer);
    }

    int compile_status = pclose(pipe);

    if (compile_status == 0)
    {
        LogToConsole("[System]: Executing...\n-----------------------------------\n");

        std::string run_cmd = "\"" + out_path + "\" 2>&1";

        FILE* run_pipe = popen(run_cmd.c_str(), "r");

        if (run_pipe)
        {
            while (fgets(buffer, sizeof(buffer), run_pipe))
            {
                LogToConsole(buffer);
            }

            pclose(run_pipe);
        }

        LogToConsole("-----------------------------------\n[System]: Program finished.\n");
    }
    else
    {
        LogToConsole("[Error]: Build failed. Double-click the red errors above to jump to the code.\n");
    }
}
// --- FLUXBUILD UI LAYOUT ---
void RenderFileNode(const FileNode& node) {
    for (const auto& [name, child] : node.children) {
        ImGui::PushID(child.fullPath.c_str()); 
        
        if (!child.children.empty()) {
            bool node_open = ImGui::TreeNode(child.name.c_str());
            
            if (ImGui::BeginPopupContextItem("FolderContextMenu")) { 
                if (ImGui::MenuItem("Delete Folder")) { 
                    ImGui::CloseCurrentPopup(); 
                    pending_delete_path = child.fullPath; 
                    pending_delete_is_folder = true; 
                } 
                ImGui::EndPopup(); 
            }
            
            if (node_open) { 
                RenderFileNode(child); 
                ImGui::TreePop(); 
            }
        } else {
            std::string display_name = child.name; 
            if (child.fullPath == compile_target) display_name = "[*] " + display_name;
            
            bool is_selected = (selected_file == child.fullPath); 
            if (ImGui::Selectable(display_name.c_str(), is_selected)) { 
                if (selected_file != "" && selected_file != child.fullPath) SaveCurrentFile(); 
                selected_file = child.fullPath; 
                LoadFileToEditor(selected_file); 
            }
            
            if (ImGui::BeginPopupContextItem("FileContextMenu")) {
                std::string ext = fs::path(child.fullPath).extension().string();
                if (ext == ".cpp" || ext == ".c" || ext == ".hpp") { 
                    if (ImGui::MenuItem("Set as Compile Target")) { 
                        compile_target = child.fullPath; 
                        LogToConsole("[System]: Compile target set to " + compile_target + "\n"); 
                    } 
                }
                if (ImGui::MenuItem("Rename File")) { 
                    show_rename_popup = true; 
                    file_to_rename = child.fullPath; 
                    strncpy(rename_buf, child.name.c_str(), IM_ARRAYSIZE(rename_buf)); 
                }
                if (ImGui::MenuItem("Delete File")) { 
                    ImGui::CloseCurrentPopup(); 
                    pending_delete_path = child.fullPath; 
                    pending_delete_is_folder = false; 
                } 
                ImGui::EndPopup(); 
            }
        }
        ImGui::PopID();
    }
}

void RenderFluxBuildUI() {
    ImGuiIO& io = ImGui::GetIO(); 
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { SaveCurrentFile(); } 
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) { show_new_file_popup = true; } 
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma)) { show_settings_popup = true; }
    
    if (ImGui::BeginMainMenuBar()) {
        if (g_LogoTextureID) { 
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2); 
            ImGui::Image((void*)(intptr_t)g_LogoTextureID, ImVec2(20, 20)); 
            ImGui::SameLine(); 
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2); 
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New File", "Ctrl+N")) { show_new_file_popup = true; }
            if (ImGui::MenuItem("New Folder")) { show_new_folder_popup = true; new_folder_name[0] = '\0'; }
            if (ImGui::MenuItem("Open Project...")) { OpenProjectFolder(); }
            ImGui::Separator(); 
            if (ImGui::MenuItem("Save", "Ctrl+S")) { SaveCurrentFile(); } 
            ImGui::Separator(); 
            if (ImGui::MenuItem("Exit", "Alt+F4")) { exit(0); } 
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build")) { 
            if (ImGui::MenuItem("Compile & Run", "F5")) { CompileAndRun(false); } 
            if (settings.enable_gdb) { 
                if (ImGui::MenuItem("Debug Crash Catcher (GDB)", "F10")) { CompileAndRun(true); } 
            } 
            ImGui::EndMenu(); 
        }
        if (ImGui::BeginMenu("Settings")) { 
            if (ImGui::MenuItem("Preferences", "Ctrl+,")) { show_settings_popup = true; } 
            ImGui::EndMenu(); 
        }
        
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 250); 
        if (settings.enable_gdb) { 
            if (ImGui::Button("Debug Build")) { CompileAndRun(true); } 
            ImGui::SameLine(); 
        } 
        if (ImGui::Button("Compile & Run")) { CompileAndRun(false); } 
        ImGui::EndMainMenuBar();
    }

    if (show_settings_popup) { ImGui::OpenPopup("FeatherForge Settings"); show_settings_popup = false; } 
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f,0.5f));
    if (ImGui::BeginPopupModal("FeatherForge Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { 
        ImGui::Text("Modular Feature Manager"); 
        ImGui::Separator(); 
        ImGui::Checkbox("Opt-In: Clang Smart Linter", &settings.enable_clangd); 
        ImGui::Checkbox("Opt-In: GDB Crash Catcher", &settings.enable_gdb); 
        ImGui::Separator(); 
        if (ImGui::Button("Save & Close", ImVec2(120, 0))) { 
            SaveSettings(); ImGui::CloseCurrentPopup(); 
        } 
        ImGui::SameLine(); 
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { 
            LoadSettings(); ImGui::CloseCurrentPopup(); 
        } 
        ImGui::EndPopup(); 
    }

    if (show_new_file_popup) { ImGui::OpenPopup("Create New File"); show_new_file_popup = false; } 
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f,0.5f));
    if (ImGui::BeginPopupModal("Create New File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { 
        ImGui::Text("Enter file path (e.g., src/core/utils.cpp):"); 
        ImGui::Separator(); 
        ImGui::InputText("##filename", new_file_name, IM_ARRAYSIZE(new_file_name)); 
        
        if (ImGui::Button("Create", ImVec2(120, 0))) { 
            CreateNewFile(new_file_name); 
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::SameLine(); 
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { 
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::EndPopup(); 
    }
    
    if (show_rename_popup) { 
        ImGui::OpenPopup("Rename File"); 
        show_rename_popup = false; 
    } 
    ImGui::SetNextWindowPos( ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f,0.5f));
    if (ImGui::BeginPopupModal("Rename File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { 
        ImGui::Text("Enter new name:"); 
        ImGui::Separator(); 
        ImGui::InputText("##rename", rename_buf, IM_ARRAYSIZE(rename_buf)); 
        
        if (ImGui::Button("Rename", ImVec2(120, 0))) { 
            fs::path old_path = file_to_rename; 
            fs::path new_path = old_path.parent_path() / rename_buf; 
            
            if (rename_buf[0] != '\0' && old_path != new_path) { 
                std::error_code ec; 
                fs::rename(old_path, new_path, ec); 
                
                if (ec) { 
                    LogToConsole("[Error]: Could not rename file.\n"); 
                } else { 
                    if (selected_file == file_to_rename) selected_file = new_path.string(); 
                    if (compile_target == file_to_rename) compile_target = new_path.string(); 
                    
                    LogToConsole("[System]: Renamed to " + new_path.string() + "\n"); 
                    ScanProjectDirectory(active_workspace_path); 
                } 
            } 
            
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::SameLine(); 
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { 
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::EndPopup(); 
    }

    if (show_new_folder_popup) { 
        ImGui::OpenPopup("Create New Folder"); 
        show_new_folder_popup = false; 
    } 
    
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f,0.5f));
    if (ImGui::BeginPopupModal("Create New Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { 
        ImGui::Text("Enter folder name (e.g., src/assets):"); 
        ImGui::Separator(); 
        ImGui::InputText("##foldername", new_folder_name, IM_ARRAYSIZE(new_folder_name)); 
        
        if (ImGui::Button("Create", ImVec2(120, 0))) { 
            std::string target_path = active_workspace_path + "/" + std::string(new_folder_name); 
            
            if (new_folder_name[0] != '\0') { 
                fs::create_directories(target_path); 
                ScanProjectDirectory(active_workspace_path); 
                LogToConsole("[System]: Created folder " + target_path + "\n"); 
            } 
            
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::SameLine(); 
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { 
            ImGui::CloseCurrentPopup(); 
        } 
        
        ImGui::EndPopup(); 
    }

    float screen_width = io.DisplaySize.x; 
    float screen_height = io.DisplaySize.y; 
    float menu_bar_height = 25.0f; 
    float status_bar_height = 30.0f; 
    float console_height = 220.0f; 
    float sidebar_width = 220.0f; 
    float main_workspace_height = screen_height - menu_bar_height - console_height - status_bar_height; 

    // LEFT SIDEBAR
    ImGui::SetNextWindowPos(ImVec2(0, menu_bar_height)); 
    ImGui::SetNextWindowSize(ImVec2(sidebar_width, main_workspace_height)); 
    ImGui::Begin("Workspace Explorer", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse); 
    
    ImGui::Text("%s", project_root_node.name.c_str()); 
    ImGui::SameLine(ImGui::GetWindowWidth() - 65); 
    if (ImGui::Button("Refresh")) { 
        ScanProjectDirectory(active_workspace_path); 
        GitRefreshStatus(); 
    } 
    
    ImGui::Separator(); 
    RenderFileNode(project_root_node);

    if (!pending_delete_path.empty()) {
        std::string deleted_path = pending_delete_path;
        std::error_code ec;
        
        if (pending_delete_is_folder) fs::remove_all(deleted_path, ec);
        else fs::remove(deleted_path, ec);

        if (selected_file == deleted_path || selected_file.find(deleted_path + "/") == 0) { 
            selected_file = ""; 
            editor.SetText(""); 
        }
        
        if (compile_target == deleted_path || compile_target.find(deleted_path + "/") == 0) { 
            compile_target = ""; 
        }

        LogToConsole("[System]: Deleted " + deleted_path + "\n");
        pending_delete_path.clear();
        ScanProjectDirectory(active_workspace_path);
    }

    if (ImGui::BeginPopupContextWindow("SidebarContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) { 
        if (ImGui::MenuItem("New File")) { 
            show_new_file_popup = true; 
        } 
        if (ImGui::MenuItem("New Folder")) { 
            show_new_folder_popup = true; 
            new_folder_name[0] = '\0'; 
        } 
        ImGui::EndPopup(); 
    }

    ImGui::Separator(); 
    
    if (ImGui::CollapsingHeader("Git Integration", ImGuiTreeNodeFlags_DefaultOpen)) { 
        if (!is_git_repo) { 
            if (ImGui::Button("Initialize Git Repo")) GitInitRepo(); 
        } else { 
            ImGui::TextDisabled("Modified Files (%d):", (int)git_modified_files.size()); 
            ImGui::BeginChild("GitFilesList", ImVec2(0, 80), true); 
            for (const auto& gf : git_modified_files) { 
                ImGui::Text("%s | %s", gf.status.c_str(), gf.path.c_str()); 
            }
            ImGui::EndChild(); 
            
            ImGui::InputText("##commitmsg", git_commit_message, IM_ARRAYSIZE(git_commit_message)); 
            if (ImGui::Button("Commit All", ImVec2(-1, 0))) { 
                GitCommitChanges(git_commit_message); 
            } 
        } 
    } 
    
    ImGui::End();

    // CENTER PANEL
    ImGui::SetNextWindowPos(ImVec2(sidebar_width, menu_bar_height)); 
    ImGui::SetNextWindowSize(ImVec2(screen_width - sidebar_width, main_workspace_height)); 
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar); 
    
    if (selected_file != "") { 
        ImGui::TextDisabled("Editing: %s", selected_file.c_str()); 
        if (!compile_target.empty()) { 
            ImGui::SameLine(); 
            ImGui::TextDisabled("| Target: %s", compile_target.c_str()); 
        } 
        ImGui::Separator(); 
        editor.Render("CodeEditor"); 
    } else { 
        ImGui::TextDisabled("[No File Selected]"); 
    } 
    
    ImGui::End();

    // BOTTOM PANEL (Interactive Console)
    ImGui::SetNextWindowPos(ImVec2(0, menu_bar_height + main_workspace_height)); 
    ImGui::SetNextWindowSize(ImVec2(screen_width, console_height)); 
    ImGui::Begin("Interactive Console", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse); 
    
    interactive_console.Render(pending_jump); 
    
    ImGui::End();

    // STATUS BAR
    ImGui::SetNextWindowPos(ImVec2(0, screen_height - status_bar_height)); 
    ImGui::SetNextWindowSize(ImVec2(screen_width, status_bar_height)); 
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f)); 
    ImGui::Begin("StatusBar", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    
    if (selected_file != "") { 
        auto cursor_pos = editor.GetCursorPosition(); 
        ImGui::TextDisabled("Editing: %s   |   Ln: %d, Col: %d", selected_file.c_str(), cursor_pos.mLine + 1, cursor_pos.mColumn + 1); 
    } else { 
        ImGui::TextDisabled("FeatherForge: Ready"); 
    }
    
    ImGui::SameLine(screen_width - 180); 
    
    if (is_git_repo) { 
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Git: %d modified", (int)git_modified_files.size()); 
    } else { 
        ImGui::TextDisabled("Git: Off"); 
    } 
    
    ImGui::End(); 
    ImGui::PopStyleVar();

    // EXECUTE JUMP & UPDATE LINTER
    if (pending_jump.requested) { 
        ExecuteJumpAction(pending_jump); 
        pending_jump.requested = false; 
    } 
    
    UpdateClangDiagnostics();
}

// --- GRAPHICS/ASSETS ---
GLuint LoadTextureFromFile(const char* filename) {
    int width, height, channels; 
    GLuint tex_id = 0; 
    stbi_set_flip_vertically_on_load(false); 
    
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 0);
    if (data) { 
        glGenTextures(1, &tex_id); 
        glBindTexture(GL_TEXTURE_2D, tex_id); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        if (channels == 4) 
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); 
        else if (channels == 3) 
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data); 
            
        stbi_image_free(data); 
    } 
    
    return tex_id;
}

void ApplyCustomTheme() {
    ImGuiStyle& style = ImGui::GetStyle(); 
    ImVec4* colors = style.Colors; 
    
    style.WindowRounding = 4.0f; 
    style.FrameRounding = 3.0f; 
    style.GrabRounding = 2.0f; 
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f); 
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f); 
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
    
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.25f, 1.00f); 
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f); 
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.28f, 1.0f); 
    
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.24f, 1.00f); 
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.24f, 0.32f, 1.00f); 
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.27f, 1.00f); 
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.35f, 1.00f); 
    
    colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); 
    colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); 
    
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f); 
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f); 
}

static void glfw_error_callback(int error, const char* description) { 
    fprintf(stderr, "GLFW Error %d: %s\n", error, description); 
}

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback); 
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    
    GLFWwindow* window = glfwCreateWindow(1280, 720, "FeatherForge IDE", nullptr, nullptr); 
    glfwMakeContextCurrent(window); 
    glfwSwapInterval(1); 

    GLFWimage icon; 
    icon.pixels = stbi_load(GetAssetPath("logo.png").c_str(), &icon.width, &icon.height, 0, 4);
    if (icon.pixels) { 
        glfwSetWindowIcon(window, 1, &icon); 
        stbi_image_free(icon.pixels); 
    }

    IMGUI_CHECKVERSION(); 
    ImGui::CreateContext(); 
    ImGuiIO& io = ImGui::GetIO(); 
    (void)io; 
    
    ApplyCustomTheme(); 
    
    g_LogoTextureID = LoadTextureFromFile(GetAssetPath("logo.png").c_str());
    
    ImGui_ImplGlfw_InitForOpenGL(window, true); 
    ImGui_ImplOpenGL3_Init("#version 130"); 
    
    editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());

    LoadSettings(); 
    
    if (!IsSetupComplete()) { 
        show_setup_wizard = true; 
        RunSetupChecks(); 
    } else { 
        if (fs::exists("./src") || fs::exists("./include")) { 
            ScanProjectDirectory("."); 
        }
        GitRefreshStatus(); 
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents(); 
        ImGui_ImplOpenGL3_NewFrame(); 
        ImGui_ImplGlfw_NewFrame(); 
        ImGui::NewFrame();
        
        if (show_setup_wizard) {
            ImGui::OpenPopup("FeatherForge Setup"); 
            ImVec2 center = ImGui::GetMainViewport()->GetCenter(); 
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            
            if (ImGui::BeginPopupModal("FeatherForge Setup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (g_LogoTextureID) { 
                    ImGui::Image((void*)(intptr_t)g_LogoTextureID, ImVec2(64, 64)); 
                    ImGui::SameLine(); 
                } 
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15); 
                ImGui::Text("Welcome to FeatherForge!"); 
                ImGui::Separator(); 
                ImGui::Text("We need to verify a few system tools to get started.\n");
                
                ImGui::Text("g++ (Required): "); 
                ImGui::SameLine(); 
                if (setup_has_gcc) { 
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[OK]"); 
                } else { 
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[MISSING]"); 
                    ImGui::SameLine(); 
                    if (ImGui::SmallButton("Copy Fix Command")) { 
                        ImGui::SetClipboardText("sudo apt install build-essential"); 
                    } 
                } 
                
                ImGui::Text("clang++ (Optional): "); 
                ImGui::SameLine(); 
                if (setup_has_clang) { 
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[OK]"); 
                } else { 
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "[MISSING]"); 
                    ImGui::SameLine(); 
                    if (ImGui::SmallButton("Copy Fix Command")) { 
                        ImGui::SetClipboardText("sudo apt install clang"); 
                    } 
                }
                
                ImGui::Separator(); 
                
                if (!setup_has_gcc) { 
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please install missing required tools."); 
                } else { 
                    if (ImGui::Button("Launch FeatherForge", ImVec2(200, 0))) { 
                        SaveSetupComplete(); 
                        show_setup_wizard = false; 
                        ScanProjectDirectory("."); 
                        GitRefreshStatus(); 
                        ImGui::CloseCurrentPopup(); 
                    } 
                } 
                
                ImGui::SameLine(); 
                if (ImGui::Button("Re-check System", ImVec2(150, 0))) { 
                    RunSetupChecks(); 
                } 
                
                ImGui::EndPopup(); 
            }
        } else { 
            RenderFluxBuildUI(); 
        }

        ImGui::Render(); 
        
        int display_w, display_h; 
        glfwGetFramebufferSize(window, &display_w, &display_h); 
        glViewport(0, 0, display_w, display_h); 
        glClearColor(0.10f, 0.10f, 0.14f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT); 
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); 
        glfwSwapBuffers(window);
    }

    if (g_IsLinterRunning) { 
        g_LinterFuture.wait(); 
    }

    ImGui_ImplOpenGL3_Shutdown(); 
    ImGui_ImplGlfw_Shutdown(); 
    ImGui::DestroyContext(); 
    
    glfwDestroyWindow(window); 
    glfwTerminate(); 
    
    return 0;
}
