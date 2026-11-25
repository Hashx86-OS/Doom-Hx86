#include "data/Archive.h"

#include <Hx86/Hx86.h>
#include <Hx86/Hgui/Hgui.h>
#include <stdio.h>
#include <string.h>

HX86_DECLARE_APP(HX86_APP_GUI);

extern "C" void DoomMain(int argc, char* argv[]);

static char* build_argv[2];

static char g_installDir[256];
static Window* g_dialog = nullptr;
static Label* g_progressLabel = nullptr;
static Button* g_downloadButton = nullptr;
static Button* g_exitButton = nullptr;
static volatile bool g_installDone = false;
static bool g_busy = false;

static bool wad_file_exists(const char* path) {
    struct stat st;
    return syscall_stat(path, &st) == 0;
}

static void OnExitClicked() {
    syscall_exit_group(1);
}

static void SetProgress(const char* text) {
    if (g_progressLabel != nullptr) g_progressLabel->setText(text);
}

static void DataInstallProgress(const char* text) {
    SetProgress(text);
}

static void OnDownloadClicked() {
    if (g_busy) return;

    g_busy = true;
    if (g_downloadButton != nullptr) g_downloadButton->setEnabled(false);
    if (g_exitButton != nullptr) g_exitButton->setEnabled(false);
    SetProgress("Starting download...");

    char error[224];
    if (!ArchiveInstallSharewareData(g_installDir, DataInstallProgress, error, sizeof(error))) {
        char message[256];
        if (error[0] != '\0') {
            sprintf(message, "Download failed: %s", error);
        } else {
            strcpy(message, "Download failed.");
        }
        printf("[DOOM] %s\n", message);
        SetProgress(message);
        g_busy = false;
        if (g_downloadButton != nullptr) g_downloadButton->setEnabled(true);
        if (g_exitButton != nullptr) g_exitButton->setEnabled(true);
        return;
    }

    printf("[DOOM] Shareware data installed\n");
    SetProgress("Game data installed. Starting DOOM...");
    g_installDone = true;
}

static void ShowMissingWadDialog(const char* wadpath) {
    init_graphics();

    g_dialog = new Window(desktop, 160, 150, 560, 340);
    g_dialog->setWindowTitle("DOOM - Game Data Required");

    Label* title = new Label(g_dialog, 20, 20, 520, 30, "doom1.wad not found.");
    title->setSize(MEDIUM);

    char expectedText[400];
    sprintf(expectedText, "Expected: %s", wadpath);
    Label* expected = new Label(g_dialog, 20, 56, 520, 22, expectedText);
    expected->setSize(TINY);

    Label* explanation =
        new Label(g_dialog, 20, 92, 520, 22, "DOOM needs the shareware episode game data.");
    explanation->setSize(SMALL);

    Label* source =
        new Label(g_dialog, 20, 120, 520, 22,
                  "Download the original shareware WAD from the idgames mirror?");
    source->setSize(SMALL);

    Label* notice =
        new Label(g_dialog, 20, 146, 520, 40,
                  "Hashx86 does not host it. Shareware only; no commercial use.");
    notice->setSize(TINY);

    g_downloadButton = new Button(g_dialog, 20, 196, 190, 32, "Download Shareware");
    g_exitButton = new Button(g_dialog, 222, 196, 90, 32, "Exit");

    g_progressLabel = new Label(g_dialog, 20, 244, 520, 60, "");
    g_progressLabel->setSize(TINY);

    g_dialog->AddChild(title);
    g_dialog->AddChild(expected);
    g_dialog->AddChild(explanation);
    g_dialog->AddChild(source);
    g_dialog->AddChild(notice);
    g_dialog->AddChild(g_downloadButton);
    g_dialog->AddChild(g_exitButton);
    g_dialog->AddChild(g_progressLabel);

    g_downloadButton->OnClick(OnDownloadClicked);
    g_exitButton->OnClick(OnExitClicked);

    g_dialog->show();

    while (!g_installDone) {
        syscall_sleep(16);
    }

    // Give the GUI event callback a moment to unwind before handing the
    // framebuffer to the engine.
    syscall_sleep(50);
    if (g_dialog != nullptr) {
        g_dialog->Close();
        g_dialog = nullptr;
    }
}

extern "C" void _start(void* arg) {
    init_sys(arg);
    asm volatile("finit");

    // The installer and DOOM's IWAD search both use the process working
    // directory, which normally holds Doom.bin.
    char cwd[256];
    int len = syscall_getcwd(cwd, sizeof(cwd));
    if (len <= 0) strcpy(cwd, "/");
    strcpy(g_installDir, cwd);

    char wadpath[300];
    int n = strlen(cwd);
    if (n > 0 && cwd[n - 1] == '/')
        sprintf(wadpath, "%sdoom1.wad", cwd);
    else
        sprintf(wadpath, "%s/doom1.wad", cwd);

    if (!wad_file_exists(wadpath)) {
        printf("[DOOM] %s not found\n", wadpath);
        ShowMissingWadDialog(wadpath);
        if (!wad_file_exists(wadpath)) {
            printf("[DOOM] Game data is still missing, exiting\n");
            syscall_exit(1);
        }
    }

    build_argv[0] = (char*)"doom";
    build_argv[1] = NULL;

    printf("[DOOM] Starting...\n");
    DoomMain(1, build_argv);
    syscall_exit(0);
}
