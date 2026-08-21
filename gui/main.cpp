// Zoundream API test client - GUI version.
//
// This is a thin GUI shell around the same session logic used by the command line
// client (client_core.c): the UI collects the run options, runs client_run() on a
// worker thread, and displays the log lines and translations it produces.

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
#include "zc_log.h"
}

#define CONFIG_FILE "zoundream_gui.cfg"

struct Translation {
    Answer answer;
    Reason reason;
};

struct AppState {
    // Run options edited in the UI
    char endpoint[512] = "";
    char api_key[256] = "";
    char user_id[128] = "";
    char audio_path[1024] = "";
    int max_loops = DEFAULT_MAX_LOOPS;
    bool show_api_key = false;

    // Worker state
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};

    // Data shared between the worker and the UI (guarded by mutex)
    std::mutex mutex;
    std::vector<std::string> log;
    std::vector<Translation> translations;

    // Result of the last finished run
    bool has_result = false;
    RunResult result = RunFinished;

    bool autoscroll = true;
    std::string error; // validation error shown next to the Start button
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
    state->translations.push_back({answer, reason});
}

/* ------------------------------ Config persistence ----------------------------------- */

static void copy_value(char* dest, size_t dest_size, const char* value)
{
    snprintf(dest, dest_size, "%s", value);
}

static void load_config()
{
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* sep = strchr(line, '=');
        if (!sep) continue;
        *sep = 0;
        const char* key = line;
        const char* value = sep + 1;
        if (strcmp(key, "endpoint") == 0) copy_value(app.endpoint, sizeof(app.endpoint), value);
        else if (strcmp(key, "api_key") == 0) copy_value(app.api_key, sizeof(app.api_key), value);
        else if (strcmp(key, "user_id") == 0) copy_value(app.user_id, sizeof(app.user_id), value);
        else if (strcmp(key, "audio_path") == 0) copy_value(app.audio_path, sizeof(app.audio_path), value);
        else if (strcmp(key, "max_loops") == 0) app.max_loops = atoi(value);
    }
    fclose(f);
}

static void save_config()
{
    FILE* f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "endpoint=%s\n", app.endpoint);
    fprintf(f, "api_key=%s\n", app.api_key);
    fprintf(f, "user_id=%s\n", app.user_id);
    fprintf(f, "audio_path=%s\n", app.audio_path);
    fprintf(f, "max_loops=%d\n", app.max_loops);
    fclose(f);
}

/* ------------------------------ Run control ----------------------------------- */

static void join_worker()
{
    if (app.worker.joinable()) app.worker.join();
}

static void start_run()
{
    app.error.clear();
    if (app.endpoint[0] == 0) { app.error = "Please enter the endpoint URL."; return; }
    if (app.api_key[0] == 0) { app.error = "Please enter your API key."; return; }
    if (app.user_id[0] == 0) { app.error = "Please enter a user id."; return; }
    if (app.audio_path[0] == 0) { app.error = "Please choose an audio file (drag & drop a WAV onto this window)."; return; }

    save_config();
    join_worker();

    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.log.clear();
        app.translations.clear();
    }
    app.has_result = false;
    app.stop_requested = false;
    app.running = true;

    // Copies for the worker, so the UI buffers are never read from another thread
    std::string endpoint = app.endpoint;
    std::string api_key = app.api_key;
    std::string user_id = app.user_id;
    std::string audio_path = app.audio_path;
    int max_loops = app.max_loops;

    app.worker = std::thread([=]() {
        RunOptions options = {};
        options.endpoint_url = endpoint.c_str();
        options.api_key = api_key.c_str();
        options.user_id = user_id.c_str();
        options.audio_file_path = audio_path.c_str();
        options.max_loops = max_loops;

        RunCallbacks callbacks = {};
        callbacks.should_stop = should_stop;
        callbacks.on_translation = on_translation;
        callbacks.ctx = &app;

        RunResult result = client_run(&options, &callbacks);

        app.result = result;
        app.has_result = true;
        app.running = false;
    });
}

static void stop_run()
{
    app.stop_requested = true;
}

/* ------------------------------ UI ----------------------------------- */

static void drop_callback(GLFWwindow* window, int count, const char** paths)
{
    (void)window;
    if (count > 0 && !app.running) {
        copy_value(app.audio_path, sizeof(app.audio_path), paths[0]);
    }
}

static void draw_result_line()
{
    const ImVec4 green(0.30f, 0.80f, 0.40f, 1.0f);
    const ImVec4 red(0.90f, 0.35f, 0.35f, 1.0f);
    const ImVec4 yellow(0.90f, 0.80f, 0.30f, 1.0f);
    const ImVec4 gray(0.60f, 0.60f, 0.60f, 1.0f);

    if (app.running) {
        int dots = 1 + ((int)(ImGui::GetTime() * 2.0)) % 3;
        ImGui::Text("Running%.*s", dots, "...");
        return;
    }
    if (!app.has_result) {
        ImGui::TextColored(gray, "Idle - configure a run and press Start.");
        return;
    }

    std::lock_guard<std::mutex> lock(app.mutex);
    switch (app.result) {
        case RunFinished:
            if (app.translations.empty()) {
                ImGui::TextColored(yellow, "Finished: no cry was translated.");
            } else {
                std::string all;
                for (const Translation& t : app.translations) {
                    if (!all.empty()) all += ", ";
                    all += api_answer_name(t.answer);
                }
                ImGui::TextColored(green, "Translated: %s", all.c_str());
            }
            break;
        case RunAuthFailed:
            ImGui::TextColored(red, "Authentication failed - check your API key.");
            break;
        case RunFileError:
            ImGui::TextColored(red, "Could not open the audio file (WAV, 1 channel, 16 kHz required).");
            break;
        case RunInitFailed:
            ImGui::TextColored(red, "Failed to initialize the HTTP client.");
            break;
        case RunCancelled:
            ImGui::TextColored(gray, "Stopped.");
            break;
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

    ImGui::SeparatorText("Zoundream API test client");

    ImGui::BeginDisabled(app.running);

    float label_width = ImGui::CalcTextSize("Audio file").x + ImGui::GetStyle().ItemSpacing.x * 3;
    ImGui::PushItemWidth(-1);

    auto field_label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label_width);
    };

    field_label("Endpoint");
    ImGui::InputTextWithHint("##endpoint", "https://...", app.endpoint, sizeof(app.endpoint));

    field_label("API key");
    float show_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                       ImGui::CalcTextSize("Show").x + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(-show_width);
    ImGui::InputText("##api_key", app.api_key, sizeof(app.api_key),
                     app.show_api_key ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::Checkbox("Show", &app.show_api_key);

    field_label("User id");
    ImGui::InputTextWithHint("##user_id", "any value; a run timestamp is appended automatically", app.user_id, sizeof(app.user_id));

    field_label("Audio file");
    ImGui::InputTextWithHint("##audio_path", "path to a WAV file (1 channel, 16 kHz) - or drop a file onto this window", app.audio_path, sizeof(app.audio_path));

    field_label("Max loops");
    ImGui::SetNextItemWidth(220);
    ImGui::InputInt("##max_loops", &app.max_loops);
    app.max_loops = std::max(0, std::min(app.max_loops, 100));
    ImGui::SetItemTooltip("How many times to loop the file back to the start while searching for a translation.");

    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (app.running) {
        if (ImGui::Button("Stop", ImVec2(120, 0))) stop_run();
        if (app.stop_requested) {
            ImGui::SameLine();
            ImGui::TextDisabled("stopping...");
        }
    } else {
        if (ImGui::Button("Start", ImVec2(120, 0))) start_run();
    }
    ImGui::SameLine();
    draw_result_line();

    if (!app.error.empty()) {
        ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "%s", app.error.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Log");

    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.log.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        std::lock_guard<std::mutex> lock(app.mutex);
        std::string all;
        for (const std::string& line : app.log) { all += line; all += '\n'; }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &app.autoscroll);

    if (ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        std::lock_guard<std::mutex> lock(app.mutex);
        for (const std::string& line : app.log) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (app.autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

/* ------------------------------ Main ----------------------------------- */

int main()
{
    load_config();
    zc_set_log_sink(log_sink, &app);
    api_set_abort_check(should_stop, &app);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(900, 700, "Zoundream Test Client", nullptr, nullptr);
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
    ImGui::StyleColorsDark();

    // Scale the UI with the monitor content scale (HiDPI)
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float scale = std::max(1.0f, xscale);
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

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
    save_config();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
