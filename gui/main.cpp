// Zoundream API test client - GUI version.
//
// This is a thin GUI shell around the same session logic used by the command line
// client (client_core.c): the UI collects a queue of audio files, runs client_run()
// for each of them on a worker thread, and displays per-file progress and results.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
#endif

#include "tinyfiledialogs.h"

// The OpenGL 1.x entry points we call ourselves (the ImGui backend loads its own).
extern "C" {
typedef float GLclampf;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClear(GLbitfield mask);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
}
#define GL_COLOR_BUFFER_BIT 0x00004000

extern "C" {
#include "client_core.h"
#include "api.h"
#include "zc_io.h"
#include "zc_log.h"
}

#define MAX_LOOPS 3

static const char* ENDPOINT_NAMES[] = { "Europe", "China", "Other" };
static const char* ENDPOINT_URLS[] = {
    "https://stage-znd-eu.zoundream-api.com/audio",
    "https://demo.zoundream.cn/audio",
};
#define ENDPOINT_CHINA 1
#define ENDPOINT_OTHER 2

struct Translation {
    Answer answer;
    Reason reason;
};

struct FileEntry {
    std::string path;
    std::string name; // just the file name, for display

    enum State { Pending, Playing, Done } state = Pending;
    float progress = 0.0f;       // 0..1, position within the file (written by the worker)
    float shown_progress = 0.0f; // smoothed value actually drawn (only touched by the UI thread)
    int loop_number = 1;
    RunResult result = RunFinished;
    std::vector<Translation> translations;
    std::string reject_reason; // why the file was rejected before playing, when it was
};

struct AppState {
    // Run options edited in the UI
    // China by default: that is where nearly every user of this client is, and since the app
    // deliberately persists nothing, whatever is set here is what they get on every launch.
    int endpoint_choice = ENDPOINT_CHINA;
    char endpoint_custom[512] = "";
    char api_key[256] = "";
    bool show_api_key = false;

    // Worker state
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};

    // Data shared between the worker and the UI (guarded by mutex)
    std::mutex mutex;
    std::vector<FileEntry> files;
    size_t current_index = 0;
    std::vector<std::string> log;

    bool batch_done = false; // true once at least one batch has finished
    std::string error;       // validation error shown next to the Start button
};

static AppState app;

/* ------------------------------ Worker callbacks ----------------------------------- */

static void log_sink(void* ctx, const char* line)
{
    AppState* state = (AppState*)ctx;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->log.push_back(line);
}

static int should_stop(void* ctx)
{
    return ((AppState*)ctx)->stop_requested ? 1 : 0;
}

static void on_translation(void* ctx, Answer answer, Reason reason)
{
    AppState* state = (AppState*)ctx;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->files[state->current_index].translations.push_back({answer, reason});
}

static void on_rejected(void* ctx, const char* reason)
{
    AppState* state = (AppState*)ctx;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->files[state->current_index].reject_reason = reason;
}

static void on_progress(void* ctx, const RunProgress* progress)
{
    AppState* state = (AppState*)ctx;
    std::lock_guard<std::mutex> lock(state->mutex);
    FileEntry& entry = state->files[state->current_index];
    entry.progress = progress->total_seconds > 0 ? (float)(progress->position_seconds / progress->total_seconds) : 0.0f;
    entry.loop_number = progress->loop_number;
}

/* ------------------------------ File list ----------------------------------- */

static std::string base_name(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static void add_file(const std::string& path)
{
    for (const FileEntry& entry : app.files) {
        if (entry.path == path) return; // already in the list
    }
    FileEntry entry;
    entry.path = path;
    entry.name = base_name(path);
    app.files.push_back(entry);
}

static void add_files_dialog()
{
    const char* patterns[] = { "*.wav" };
    const char* picked = tinyfd_openFileDialog("Choose WAV files", "", 1, patterns, "WAV files", 1);
    if (!picked) return; // cancelled, or no dialog tool available (drag & drop still works)

    // Multiple selections are returned as a single '|' separated string
    std::string all = picked;
    size_t start = 0;
    while (start <= all.size()) {
        size_t sep = all.find('|', start);
        if (sep == std::string::npos) sep = all.size();
        if (sep > start) add_file(all.substr(start, sep - start));
        start = sep + 1;
    }
}

/* ------------------------------ Run control ----------------------------------- */

static void join_worker()
{
    if (app.worker.joinable()) app.worker.join();
}

static const char* selected_endpoint()
{
    if (app.endpoint_choice == ENDPOINT_OTHER) return app.endpoint_custom;
    return ENDPOINT_URLS[app.endpoint_choice];
}

static void start_run()
{
    app.error.clear();
    if (app.api_key[0] == 0) { app.error = "Please enter your API key."; return; }
    if (app.endpoint_choice == ENDPOINT_OTHER && app.endpoint_custom[0] == 0) { app.error = "Please enter the endpoint URL."; return; }
    if (app.files.empty()) { app.error = "Please add at least one audio file."; return; }

    join_worker();

    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.log.clear();
        for (FileEntry& entry : app.files) {
            entry.state = FileEntry::Pending;
            entry.progress = 0.0f;
            entry.shown_progress = 0.0f;
            entry.loop_number = 1;
            entry.translations.clear();
            entry.reject_reason.clear();
        }
        app.current_index = 0;
    }
    app.batch_done = false;
    app.stop_requested = false;
    app.running = true;

    // Copies for the worker, so the UI buffers are never read from another thread.
    // The user id is derived from the API key: its first 3 characters are the base id
    // (so all traffic from this tool can be filtered by it) and api_init appends a
    // millisecond timestamp for uniqueness.
    std::string endpoint = selected_endpoint();
    std::string api_key = app.api_key;
    std::string base_user_id = api_key.substr(0, std::min<size_t>(3, api_key.size()));

    app.worker = std::thread([endpoint, api_key, base_user_id]() {
        size_t count = app.files.size(); // the list is not modified while running
        for (size_t i = 0; i < count; i++) {
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.current_index = i;
                app.files[i].state = FileEntry::Playing;
            }

            RunOptions options = {};
            options.endpoint_url = endpoint.c_str();
            options.api_key = api_key.c_str();
            options.user_id = base_user_id.c_str();
            options.audio_file_path = app.files[i].path.c_str();
            options.max_loops = MAX_LOOPS;

            RunCallbacks callbacks = {};
            callbacks.should_stop = should_stop;
            callbacks.on_translation = on_translation;
            callbacks.on_progress = on_progress;
            callbacks.on_rejected = on_rejected;
            callbacks.ctx = &app;

            RunResult result = client_run(&options, &callbacks);

            {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.files[i].state = FileEntry::Done;
                app.files[i].result = result;
                if (result == RunFinished) app.files[i].progress = 1.0f;
            }

            if (app.stop_requested) break;
            if (result == RunAuthFailed) break; // every request would keep failing
        }
        app.batch_done = true;
        app.running = false;
    });
}

static void stop_run()
{
    app.stop_requested = true;
}

static void save_log_dialog()
{
    const char* patterns[] = { "*.txt" };
    const char* path = tinyfd_saveFileDialog("Save log", "zoundream_log.txt", 1, patterns, "Text files");
    if (!path) return;
    FILE* f = zc_fopen(path, "w"); // not fopen: the path may contain non-ASCII characters
    if (!f) {
        zc_log("Could not save the log to that location.");
        return;
    }
    std::lock_guard<std::mutex> lock(app.mutex);
    for (const std::string& line : app.log) {
        fprintf(f, "%s\n", line.c_str());
    }
    fclose(f);
}

/* ------------------------------ UI ----------------------------------- */

static void drop_callback(GLFWwindow* window, int count, const char** paths)
{
    (void)window;
    if (app.running) return;
    for (int i = 0; i < count; i++) {
        add_file(paths[i]);
    }
}

static ImVec4 answer_color(Answer answer)
{
    switch (answer) {
        case AnswerBurp: return ImVec4(0.35f, 0.80f, 0.80f, 1.0f);
        case AnswerSleep: return ImVec4(0.50f, 0.65f, 0.95f, 1.0f);
        case AnswerHungry: return ImVec4(0.95f, 0.65f, 0.25f, 1.0f);
        case AnswerUncomfortable: return ImVec4(0.90f, 0.80f, 0.35f, 1.0f);
        case AnswerPain: return ImVec4(0.90f, 0.40f, 0.40f, 1.0f);
        default: return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
    }
}

// Status text and color for one entry. Must be called with app.mutex held.
static void entry_status(const FileEntry& entry, std::string* text, ImVec4* color)
{
    const ImVec4 red(0.90f, 0.35f, 0.35f, 1.0f);
    const ImVec4 gray(0.60f, 0.60f, 0.60f, 1.0f);

    if (entry.state == FileEntry::Pending) {
        *text = "queued";
        *color = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        return;
    }
    if (entry.state == FileEntry::Playing) {
        if (entry.loop_number > 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "loop %d/%d", entry.loop_number - 1, MAX_LOOPS);
            *text = buffer;
        } else {
            *text = "";
        }
        *color = gray;
        return;
    }

    switch (entry.result) {
        case RunFinished:
            if (entry.translations.empty()) {
                *text = "no translation";
                *color = gray;
            } else {
                for (const Translation& t : entry.translations) {
                    if (!text->empty()) *text += ", ";
                    *text += api_answer_name(t.answer);
                }
                *color = answer_color(entry.translations.front().answer);
            }
            break;
        case RunBadFormat:
        case RunTooShort:
        case RunFileError:
            // Show the specific reason reported when the file was rejected
            *text = entry.reject_reason.empty() ? "invalid audio file" : entry.reject_reason;
            *color = red;
            break;
        case RunNetworkError: *text = "network error"; *color = red; break;
        case RunAuthFailed: *text = "authentication failed"; *color = red; break;
        case RunInitFailed: *text = "network setup failed"; *color = red; break;
        case RunCancelled: *text = "stopped"; *color = gray; break;
    }
}

static void draw_file_rows()
{
    std::lock_guard<std::mutex> lock(app.mutex);

    if (app.files.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No files yet. Use \"Add files...\" or drop WAV files onto this window.");
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    float row_height = ImGui::GetTextLineHeight() + style.FramePadding.y * 2 + 4;
    float delta = ImGui::GetIO().DeltaTime;

    for (FileEntry& entry : app.files) {
        // Smooth the progress bar towards the value reported by the worker; snap
        // backwards immediately (that is the bar resetting when the file loops).
        if (entry.progress < entry.shown_progress) entry.shown_progress = entry.progress;
        else entry.shown_progress += (entry.progress - entry.shown_progress) * std::min(1.0f, 6.0f * delta);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;

        // Row background, then the progress fill over it
        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + row_height), IM_COL32(128, 128, 128, 24), 3.0f);
        if (entry.state != FileEntry::Pending && entry.shown_progress > 0.0f) {
            ImU32 fill = entry.state == FileEntry::Playing ? IM_COL32(70, 130, 200, 90) : IM_COL32(70, 130, 200, 45);
            draw_list->AddRectFilled(pos, ImVec2(pos.x + width * entry.shown_progress, pos.y + row_height), fill, 3.0f);
        }

        float text_y = pos.y + (row_height - ImGui::GetTextLineHeight()) / 2;
        std::string status;
        ImVec4 status_color;
        entry_status(entry, &status, &status_color);

        // File name on the left, clipped so it never overlaps the right-aligned status
        float status_width = ImGui::CalcTextSize(status.c_str()).x;
        ImVec4 clip(pos.x + 8, pos.y, pos.x + width - status_width - 16, pos.y + row_height);
        draw_list->AddText(nullptr, 0.0f, ImVec2(pos.x + 8, text_y), ImGui::GetColorU32(ImGuiCol_Text),
                           entry.name.c_str(), nullptr, 0.0f, &clip);
        if (!status.empty()) {
            draw_list->AddText(ImVec2(pos.x + width - status_width - 8, text_y),
                               ImGui::GetColorU32(status_color), status.c_str());
        }

        ImGui::Dummy(ImVec2(width, row_height));
        ImGui::Spacing();
    }
}

static void draw_summary_line()
{
    const ImVec4 green(0.30f, 0.80f, 0.40f, 1.0f);
    const ImVec4 red(0.90f, 0.35f, 0.35f, 1.0f);
    const ImVec4 gray(0.60f, 0.60f, 0.60f, 1.0f);

    std::lock_guard<std::mutex> lock(app.mutex);

    if (app.running) {
        int dots = 1 + ((int)(ImGui::GetTime() * 2.0)) % 3;
        ImGui::Text("Running file %d/%d%.*s", (int)app.current_index + 1, (int)app.files.size(), dots, "...");
        return;
    }
    if (!app.batch_done) {
        ImGui::TextColored(gray, "Idle - add files and press Start.");
        return;
    }

    int translated = 0, played = 0;
    bool auth_failed = false, stopped = false;
    for (const FileEntry& entry : app.files) {
        if (entry.state != FileEntry::Done) continue;
        played++;
        if (!entry.translations.empty()) translated++;
        if (entry.result == RunAuthFailed) auth_failed = true;
        if (entry.result == RunCancelled) stopped = true;
    }
    if (auth_failed) {
        ImGui::TextColored(red, "Authentication failed - check your API key.");
    } else {
        ImGui::TextColored(translated > 0 ? green : gray, "Finished%s: %d/%d files translated.",
                           stopped ? " (stopped early)" : "", translated, played);
    }
}

static void draw_ui()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SeparatorText("Zoundream Cry Translation demo");

    ImGui::BeginDisabled(app.running);

    float label_width = ImGui::CalcTextSize("Endpoint").x + ImGui::GetStyle().ItemSpacing.x * 3;

    auto field_label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label_width);
    };

    field_label("Endpoint");
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("##endpoint", &app.endpoint_choice, ENDPOINT_NAMES, 3);
    if (app.endpoint_choice == ENDPOINT_OTHER) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##endpoint_custom", "https://...", app.endpoint_custom, sizeof(app.endpoint_custom));
    }

    field_label("API key");
    // Keys are short (~16-24 characters), no need for a full-width field
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("znd-0000000000000000000000000").x);
    ImGui::InputText("##api_key", app.api_key, sizeof(app.api_key),
                     app.show_api_key ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::Checkbox("Show", &app.show_api_key);

    field_label("Files");
    if (ImGui::Button("Add files...")) add_files_dialog();
    ImGui::SameLine();
    if (ImGui::Button("Clear list")) {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.files.clear();
        app.batch_done = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("or drop WAV files onto this window");

    ImGui::EndDisabled();

    ImGui::Spacing();

    if (app.running) {
        if (ImGui::Button("Stop", ImVec2(120, 0))) stop_run();
    } else {
        if (ImGui::Button("Start", ImVec2(120, 0))) start_run();
    }
    ImGui::SameLine();
    draw_summary_line();

    if (!app.running && app.batch_done) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Save log...").x);
        if (ImGui::SmallButton("Save log...")) save_log_dialog();
        ImGui::SetItemTooltip("Save the detailed log of the last run, e.g. to send to Zoundream.");
    }

    if (!app.error.empty()) {
        ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "%s", app.error.c_str());
    }

    ImGui::Spacing();

    if (ImGui::BeginChild("files", ImVec2(0, 0))) {
        draw_file_rows();
    }
    ImGui::EndChild();

    ImGui::End();
}

/* ------------------------------ Fonts ----------------------------------- */

/* Merges a system font covering Chinese into the default UI font.
 *
 * Dear ImGui's built-in font only has Latin glyphs, so a file named e.g. 宝宝哭声.wav would
 * show up as a row of empty boxes - and most of our users are in China. A CJK font is several
 * megabytes, far too much to embed in a binary we send over chat, so we borrow the one the
 * system already has. In merge mode the Latin UI keeps the look it has everywhere else and
 * only the missing glyphs come from this font; ImGui rasterizes glyphs on demand, so a font
 * this large costs nothing until a non-ASCII character is actually drawn.
 */
static void merge_cjk_font()
{
    static const char* const font_paths[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\msyh.ttc",    // Microsoft YaHei, the system UI font since Windows 8.1
        "C:\\Windows\\Fonts\\msyh.ttf",    // the same, as shipped by Vista and 7
        "C:\\Windows\\Fonts\\simhei.ttf",  // SimHei
        "C:\\Windows\\Fonts\\simsun.ttc",  // SimSun, present on every Chinese Windows
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
#endif
    };

    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    fonts->AddFontDefault(); // the default UI font, unchanged

    ImFontConfig config;
    config.MergeMode = true;                 // add these glyphs to the default font
    config.Flags |= ImFontFlags_NoLoadError; // a font that is not installed is not an error, just try the next one
    for (const char* path : font_paths) {
        // Size 0 means "the size of the font being merged into": ImGui rejects merging a
        // source with an explicit size into a font that has none, which is the default one.
        if (fonts->AddFontFromFileTTF(path, 0.0f, &config) != nullptr) return;
    }
    zc_log("No Chinese font found on this system: file names in Chinese may show as empty boxes.");
}

/* ------------------------------ Main ----------------------------------- */

/* Returns the audio file paths passed on the command line, UTF-8 encoded.
 *
 * On Windows the argv strings the C runtime builds for main() are encoded in the process ANSI
 * code page (GBK on a Chinese system), not UTF-8 like every other path in this program, so a
 * file with a Chinese name dropped onto the .exe in Explorer would arrive mis-encoded and fail
 * to open. The wide command line does not have that problem, so take the paths from there.
 */
static std::vector<std::string> command_line_files(int argc, char** argv)
{
#ifdef _WIN32
    (void) argc;
    (void) argv;
    std::vector<std::string> files;
    int count = 0;
    wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide_argv == nullptr) return files;

    for (int i = 1; i < count; i++) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) continue; // 1 == just the terminator, i.e. an empty argument
        std::string path((size_t) size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, &path[0], size, nullptr, nullptr);
        files.push_back(path);
    }
    LocalFree(wide_argv);
    return files;
#else
    return std::vector<std::string>(argv + 1, argv + argc);
#endif
}


int main(int argc, char** argv)
{
    zc_set_log_sink(log_sink, &app);
    api_set_abort_check(should_stop, &app);

    // Audio files can also be passed on the command line, or dropped onto the executable
    for (const std::string& path : command_line_files(argc, argv)) {
        add_file(path);
    }

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // Undocumented, for scripted testing: override the window title (and with it, which
    // window a screenshot tool picks up when several instances are running)
    const char* title = getenv("ZOUNDREAM_GUI_TITLE");
    GLFWwindow* window = glfwCreateWindow(900, 700, title ? title : "Zoundream Cry Translation demo", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the app deliberately writes no files on its own
    ImGui::StyleColorsDark();

    // Scale the UI with the monitor content scale (HiDPI)
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float scale = std::max(1.0f, xscale);
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    merge_cjk_font();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Undocumented, for scripted testing: pre-fill the key/endpoint and start the run immediately
    if (const char* key = getenv("ZOUNDREAM_GUI_KEY")) snprintf(app.api_key, sizeof(app.api_key), "%s", key);
    if (const char* endpoint = getenv("ZOUNDREAM_GUI_ENDPOINT")) {
        app.endpoint_choice = ENDPOINT_OTHER;
        snprintf(app.endpoint_custom, sizeof(app.endpoint_custom), "%s", endpoint);
    }
    if (getenv("ZOUNDREAM_GUI_AUTOSTART") && !app.files.empty()) start_run();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Collect the worker thread as soon as it is done
        if (!app.running && app.worker.joinable()) join_worker();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_ui();

        ImGui::Render();
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Shut down cleanly even if a run is still in progress
    app.stop_requested = true;
    join_worker();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
