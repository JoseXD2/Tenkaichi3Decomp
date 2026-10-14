// The settings window of the PC build: Dear ImGui over the finished picture. F1 opens and closes it, or START + SELECT
// of a controller, or the key and button of its Controls list (gs_input.c: PortInput_Hotkey).
// Everything it shows lives elsewhere (ui.h): the renderer's settings in gs_gpu.c, the bindings in gs_input.c.
// It runs on the render thread, which owns the window and its events.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_opengl3.h"
#include "ui.h"
#include "namefont.h"
#include "../port_release.h"
#include "menu_text.h"

// Phones (Android): everything larger, by the display's density, and the window kept inside the screen. On a PC 1.
static float sUiScale = 1.0f;
#define S(x) ((x) * sUiScale)
static int sMeter = -1; // the meter (frame rate, connection): -1 not read yet
static bool meter_on(void);
static int sGL; // 1: the OpenGL back end draws the window (ImGui's OpenGL3 backend, no SDL GPU device)

extern "C" {
extern volatile int gPortNetMenuRequest;
void Port_NetOptions(int rollback, int delay); // gs/net.c
void Port_NetRulesSet(int type, int dp, int time); // gs/net.c: the host's battle type, DP limit row, time limit
void Port_NetSpeedSet(int percent);                // ... and the match's speed, 100..200 (set before the rules)
int Port_NetSpeedStandard(void);                   // what a quick match runs at
void Port_NetNameSet(const char *name);         // gs/net.c: this player's name for the match
int Port_IsWide(void);                         // plat_stub.c: the picture is wider than 4:3
int Port_AspectMilli(void);
void Match_UpdateAsk(void);                    // net_match.c: asks GitHub for the newest release, once
int Match_UpdateLatest(void);                  // ... its number (0.1.33 is 133), 0 while unknown
int Port_Menus43(void);                       // plat_stub.c: the menus are shown at 4:3 in a wider picture
void Port_Menus43Set(int on);
int Port_HudLayout(void);                      // gs_draw.c: 2 = the HUD is about the middle of the screen
int GsDraw_HudShown(void);                     // ... and 1 while the game is drawing the HUD's side panels
const char *Port_NetName(int player);          // the name of the player of side 0 or 1 ("" outside a match)
int Port_HudSwap(void);                        // the two sides of the HUD are in each other's places on this copy
int Port_NetActive(void);
void *BtlChar_FindByObjId(int objId);          // the game: the fighter of a side, NULL outside a fight
int Port_NetStats(int *out);                   // gs/net.c: the meter's numbers of an online match
int Port_LobbyOther(void);
extern unsigned gPortLiveBlanks;               // plat_stub.c: vertical blanks the game has gone through
int Port_RollCan(void);                        // gs/state.c
const char *Port_FileRoot(void);               // plat_file.c: the game's data folder
unsigned GsGl_StripTexture(const void *rgba, int w, int h); // gs_gl.c
void GsGl_StripTextureFree(unsigned id);
const char *PortStages_Dir(void);                          // plat_stages.c
const char *PortStages_Name(int index);
int PortStages_Count(void);
const char *PortSongs_Dir(void);                           // plat_songs.c
const char *PortSongs_Name(int index);
int PortSongs_Count(void);
int Port_Setting(const char *name, int def);   // plat_settings.c
void Port_SettingSave(const char *name, int value);
void Port_SettingText(const char *name, const char *def, char *out, int size);
void Port_SettingTextSave(const char *name, const char *value);
void Port_SettingsWrite(void);
int Port_ClashSquare(void);                    // gs_input.c: Square counts as a direction in clashes
void Port_ClashSquareSet(int on);
int Port_ClashSquareX(void);                   // what a press is worth, in quarters of a point (4, 6, 7, 8)
void Port_ClashSquareXSet(int quarters);
extern int gsInterp;                           // gs_draw.c: in-between pictures on (1) / off (0); -1 before the first frame
void Port_SettingsWrite(void);
int Port_LobbyStart(int host, const char *address, int port); // gs/net.c
int Port_LobbyStartCode(int host, const char *code);         // the same with a room code (the matchmaking service)
int Port_LobbyStartQuick(int queue);                         // a quick match: 0 single, 1 team, 2..4 DP battle of 10 / 15 / 20
void Port_LobbyQueuesAsk(void);                              // asks the service how many wait in each queue
void Port_LobbyHelloAsk(void);                               // asks it whether this release is new enough
int Port_LobbyRelease(int *min, int *latest);                // this release's number; the oldest let in and the newest (0: not known)
int Port_LobbyQueueWaiting(int queue);                       // ... as last heard, -1 if not known
const char *Port_LobbyCode(void);                            // the host's room code, "" until it is known
const char *Port_LobbyError(void);                           // why the matchmaking failed (state -3)
int Port_LobbyRelayed(void);                                 // the other player was reached through the relay
int Port_LobbyPoll(void);
void Port_LobbyCancel(void);
void Port_LobbyLaunch(void);
}
static bool sReady, sOpen;
static bool sNet;                              // the open window is the online screen (Dragon Net Battle), not the settings
static int sForceTab = -1;                     // BT3_UI_OPEN=<tab>: open at start on that tab (testing)
static int sCapKind, sCapPlayer, sCapAction;   // waiting for a key (1) or a controller button (2) to bind
static Uint64 sCapUntil;                       // ... until then: a controller has no button that is not a binding, so the wait ends by itself
static bool sQuit;                             // the question before the game closes is up
static bool sQuitNo;                           // ... and Esc answered it: no
static bool sPadHold;                          // the controller does not work the window: a binding is waited for, or its button is still down
static int sTab;                               // the settings tab that is open (the shoulder buttons go to its neighbours)
static bool sPadTab;                           // the shoulder buttons changed the tab this frame
static SDL_GPUDevice *sDevice;

/* Name strips (gamedata/stages/names.rgba and gamedata/songs/names.rgba): the names of the stages and tracks added
   from outside the disc, drawn by the port over the menu in the game's own lettering. Raw RGBA, a 12-byte header
   (w, h, count) and then `count` images of w x h stacked. */
static SDL_GPUTexture *sNameTex, *sSongTex;
static uint32_t sNameW, sNameH, sNameCount, sSongW, sSongH, sSongCount;

static uint8_t *read_strip(const char *path, size_t *bytes, uint32_t *w, uint32_t *h, uint32_t *count) {
    FILE *fp = fopen(path, "rb");
    uint32_t hdr[3];
    uint8_t *pix;

    if (fp == NULL) {
        return NULL;
    }
    if (fread(hdr, sizeof(hdr), 1, fp) != 1 || hdr[0] == 0 || hdr[1] == 0 || hdr[2] == 0) {
        fclose(fp);
        return NULL;
    }
    *w = hdr[0];
    *h = hdr[1];
    *count = hdr[2];
    *bytes = (size_t)hdr[0] * hdr[1] * hdr[2] * 4;
    pix = (uint8_t *)malloc(*bytes);
    if (pix == NULL || fread(pix, 1, *bytes, fp) != *bytes) {
        free(pix);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return pix;
}

static SDL_GPUTexture *make_strip(uint8_t *pix, size_t bytes, uint32_t w, uint32_t h, uint32_t count) {
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTransferBufferCreateInfo tbi;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_GPUTexture *tex;
    void *map;

    if (sGL) { // the OpenGL back end: a GL texture, its name carried in the pointer (ImGui's OpenGL3 backend takes it so)
        unsigned id = GsGl_StripTexture(pix, (int)w, (int)(h * count));
        free(pix);
        return (SDL_GPUTexture *)(intptr_t)id;
    }
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.width = w;
    ci.height = h * count;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex = SDL_CreateGPUTexture(sDevice, &ci);
    if (tex == NULL) {
        free(pix);
        return NULL;
    }
    SDL_zero(tbi);
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = (Uint32)bytes;
    tb = SDL_CreateGPUTransferBuffer(sDevice, &tbi);
    map = SDL_MapGPUTransferBuffer(sDevice, tb, false);
    memcpy(map, pix, bytes);
    SDL_UnmapGPUTransferBuffer(sDevice, tb);
    free(pix);
    cmd = SDL_AcquireGPUCommandBuffer(sDevice);
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src);
    src.transfer_buffer = tb;
    src.pixels_per_row = w;
    src.rows_per_layer = h * count;
    SDL_zero(dst);
    dst.texture = tex;
    dst.w = w;
    dst.h = h * count;
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sDevice, tb);
    return tex;
}

static SDL_GPUTexture *load_strip(const char *env, const char *rel, uint32_t *w, uint32_t *h, uint32_t *count,
                                  const char *what) {
    const char *paths[3];
    uint8_t *pix = NULL;
    size_t bytes = 0;
    SDL_GPUTexture *tex;
    uint32_t i;

    char inData[600];
    // in the stages / songs folder (plat_extras.c); the environment variable names another file
    snprintf(inData, sizeof(inData), "%s", rel);
    paths[0] = getenv(env);
    paths[1] = inData;
    paths[2] = NULL;
    for (i = 0; i < 3 && pix == NULL; i++) {
        if (paths[i] != NULL) {
            pix = read_strip(paths[i], &bytes, w, h, count);
        }
    }
    if (pix == NULL) {
        return NULL;
    }
    tex = make_strip(pix, bytes, *w, *h, *count);
    if (tex != NULL) {
        fprintf(stderr, "bt3: %s overlay: %u names, %ux%u each\n", what, *count, *w, *h);
    }
    return tex;
}

// ---- the side bars ----
// Where the picture is narrower than the window (a 4:3 picture, or "Menus in 4:3", in a wide window) the sides are
// black, or with "Side bars: Border" a panel with a border along the picture. Two pictures: the border (trim.png),
// kept in proportion at the picture's edge and as high as the picture, and the plain panel (fill.png), stretched
// over the rest, so a bar of any width looks right. Both are drawn as they are on the left and mirrored on the right.
// The program carries a gold set (port/assets/bars, art by the project's owner: 34 x 720 and 126 x 720). A player
// replaces it by putting a trim.png and / or a fill.png of any size into a folder "bars" next to the game.
#include "bars_png.h"
static SDL_GPUTexture *sBarTrim, *sBarFill;
static int sBars = -1; // 0 black, 1 the border; -1 not read yet
static int sBarsTried;

static float sBarTrimShape = 34.0f / 720.0f; // the border's width over its height

static SDL_GPUTexture *bar_texture(const char *file, const unsigned char *png, size_t size, float *shape) {
    SDL_Surface *s = SDL_LoadPNG(file), *rgba; // the player's own picture, if there is one
    if (s != NULL) {
        fprintf(stderr, "bt3: side bars: %s, %d x %d\n", file, s->w, s->h);
    } else {
        s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    }
    uint8_t *pix;
    size_t bytes;
    if (s == NULL) {
        return NULL;
    }
    rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == NULL) {
        return NULL;
    }
    if (shape != NULL && rgba->h > 0) {
        *shape = (float)rgba->w / (float)rgba->h;
    }
    bytes = (size_t)rgba->w * rgba->h * 4;
    pix = (uint8_t *)malloc(bytes);
    for (int y = 0; pix != NULL && y < rgba->h; y++) {
        memcpy(pix + (size_t)y * rgba->w * 4, (const uint8_t *)rgba->pixels + (size_t)y * rgba->pitch, (size_t)rgba->w * 4);
    }
    SDL_GPUTexture *tex = pix != NULL ? make_strip(pix, bytes, (uint32_t)rgba->w, (uint32_t)rgba->h, 1) : NULL;
    SDL_DestroySurface(rgba);
    return tex;
}

static bool bars_on(void) {
    if (sBars < 0) {
        sBars = Port_Setting("side_bars", 0) != 0;
    }
    return sBars != 0 && gUiPresentX > 0 && gUiPresentW > 0 && gUiPresentH > 0;
}

static void bars_draw(void) {
    if (!sBarsTried) {
        sBarsTried = 1;
        sBarTrim = bar_texture("bars/trim.png", kBarsTrimPng, sizeof(kBarsTrimPng), &sBarTrimShape);
        sBarFill = bar_texture("bars/fill.png", kBarsFillPng, sizeof(kBarsFillPng), NULL);
    }
    if (sBarTrim == NULL || sBarFill == NULL) {
        return;
    }
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    float y0 = (float)gUiPresentY, y1 = y0 + (float)gUiPresentH;
    float bar = (float)gUiPresentX; // the width of each side
    float trim = (float)gUiPresentH * sBarTrimShape;
    float u0 = 0.0f;
    if (trim > bar) { // a bar narrower than the border: the border's inner part
        u0 = 1.0f - bar / trim;
        trim = bar;
    }
    float lx = (float)gUiPresentX, rx = lx + (float)gUiPresentW; // the picture's two edges
    ImTextureRef tt((ImTextureID)(intptr_t)sBarTrim), ft((ImTextureID)(intptr_t)sBarFill);
    if (bar > trim) {
        dl->AddImage(ft, ImVec2(lx - bar, y0), ImVec2(lx - trim, y1), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        dl->AddImage(ft, ImVec2(rx + trim, y0), ImVec2(rx + bar, y1), ImVec2(1.0f, 0.0f), ImVec2(0.0f, 1.0f));
    }
    dl->AddImage(tt, ImVec2(lx - trim, y0), ImVec2(lx, y1), ImVec2(u0, 0.0f), ImVec2(1.0f, 1.0f));
    dl->AddImage(tt, ImVec2(rx, y0), ImVec2(rx + trim, y1), ImVec2(1.0f, 0.0f), ImVec2(u0, 1.0f));
}

static void load_strips(void) {
    char path[600];
    // The names of added stages and songs are drawn by this overlay, over the menu: from a strip of pre-rendered
    // names if the install script made one (names.rgba in the folder), else as text (frame_build). Either way the
    // game leaves the name to the overlay, so the flags are set whenever the overlay exists.
    snprintf(path, sizeof(path), "%s/names.rgba", PortStages_Dir());
    sNameTex = PortStages_Dir()[0] != '\0' ? load_strip("BT3_STAGE_NAMES", path, &sNameW, &sNameH, &sNameCount, "stage-name") : NULL;
    snprintf(path, sizeof(path), "%s/names.rgba", PortSongs_Dir());
    sSongTex = PortSongs_Dir()[0] != '\0' ? load_strip("BT3_SONG_NAMES", path, &sSongW, &sSongH, &sSongCount, "song-name") : NULL;
    if (sNameTex == NULL) { sNameCount = 0; }
    if (sSongTex == NULL) { sSongCount = 0; }
    gUiNameReady = 1;
    gUiSongReady = 1;
}

static void style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 10.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.TabRounding = 6.0f;
    st.PopupRounding = 6.0f;
    st.WindowPadding = ImVec2(18.0f, 16.0f);
    st.FramePadding = ImVec2(10.0f, 6.0f);
    st.ItemSpacing = ImVec2(10.0f, 9.0f);
    st.WindowBorderSize = 0.0f;
    st.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImVec4 *c = st.Colors;
    const ImVec4 accent(0.96f, 0.55f, 0.13f, 1.0f), accentDim(0.96f, 0.55f, 0.13f, 0.55f);
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 0.96f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.17f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.32f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered] = accentDim;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Header] = ImVec4(0.96f, 0.55f, 0.13f, 0.30f);
    c[ImGuiCol_HeaderHovered] = accentDim;
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.20f, 1.0f);
    c[ImGuiCol_TabHovered] = accentDim;
    c[ImGuiCol_TabSelected] = ImVec4(0.96f, 0.55f, 0.13f, 0.80f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.14f, 0.98f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
}

int Ui_Init(SDL_Window *window, SDL_GPUDevice *device, void *gl_context) {
    static const char *fonts[] = { // a proportional system font if there is one; else the library's built-in font
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
        "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf",
    };
    ImGui_ImplSDLGPU3_InitInfo info;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL; // no window-layout file next to the game
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad; // (a controller opens it too: START + SELECT)
    style();
#ifdef __ANDROID__
    {   // the menu on a phone: a controller moves through it, a finger taps it; sizes by the screen's density
        float dpi = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
        sUiScale = dpi > 0.0f ? dpi * 0.75f : 2.0f;
        sUiScale = sUiScale < 1.0f ? 1.0f : sUiScale > 4.0f ? 4.0f : sUiScale;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetStyle().ScaleAllSizes(sUiScale);
        ImGui::GetStyle().FontScaleDpi = sUiScale;
        ImGui::GetStyle().TouchExtraPadding = ImVec2(4.0f * sUiScale, 4.0f * sUiScale);
    }
#endif
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        FILE *fp = fopen(fonts[i], "rb");
        if (fp != NULL) {
            fclose(fp);
            io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f);
            break;
        }
    }
    if (device == NULL) { // the OpenGL back end (gs_gl.c), with its context current
        sGL = 1;
        if (!ImGui_ImplSDL3_InitForOpenGL(window, (SDL_GLContext)gl_context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) {
            return 0;
        }
    } else {
        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            return 0;
        }
        info.Device = device;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info)) {
            return 0;
        }
    }
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll, NULL, 0); // any connected controller works the window
    sDevice = device;
    sReady = true;
    load_strips(); /* the stage- and song-name overlays; the ready flags stay 0 if there is no strip */
    if (getenv("BT3_UI_OPEN") != NULL) {
        sForceTab = atoi(getenv("BT3_UI_OPEN"));
        if (sForceTab == 9) { // testing: the online screen
            sForceTab = -1;
            gPortNetMenuRequest = 1;
        }
        if (sForceTab == 8) { // testing: the exit question over the settings
            sForceTab = -1;
            sQuit = true;
        }
        sOpen = true;
        gPortOverlayOpen = 1;
    }
    return 1;
}

// The game gets nothing from keyboard or controller while the window or the exit question is up; and the button
// that closed either is not the game's.
static void input_owner(void) {
    int up = sOpen || sQuit;
    if (gPortOverlayOpen && !up) {
        PortInput_Swallow();
    }
    gPortOverlayOpen = up;
}

static void set_open(bool open) {
    sOpen = open;
    sNet = false;
    sCapKind = 0;
    gPortInputCapture = 0;
    input_owner();
}

static void capture(int kind, int player, int action) {
    sCapKind = kind; sCapPlayer = player; sCapAction = action; gPortInputCapture = 1;
    sCapUntil = SDL_GetTicks() + 5000;
    sPadHold = true;
}

// A controller button pressed in the window (not while it waits for a binding, a list is open or a field is edited).
static bool pad_pressed(ImGuiKey key) {
    return sCapKind == 0 && !sPadHold && !sQuit && !ImGui::IsAnyItemActive() &&
           !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && ImGui::IsKeyPressed(key, false);
}

int Ui_QuitAsk(void) {
    if (!sReady) {
        return 0;
    }
    sQuit = true;
    input_owner();
    return 1;
}

// The screen reader (a11y.c): it cannot see this window's question, which is drawn and not one of the game's screens,
// so the question, the button under the keyboard's focus and the keys are said here. Nothing while it is off.
extern "C" {
void PortA11y_Say(const char *text, int flags); // flags 1: cuts what is being said
const char *PortA11y_Label(const char *label);  // the label in the reader's language
}

static void say_quit_button(int which, int flags) {
    char text[128], pos[32];
    snprintf(pos, sizeof(pos), PortA11y_Label(", %d of %d"), which + 1, 2);
    snprintf(text, sizeof(text), "%s%s", PortA11y_Label(which == 0 ? "Exit" : "Cancel"), pos);
    PortA11y_Say(text, flags);
}

static void build_quit(void) {
    static int said = -1; // the button last said: 0 Exit, 1 Cancel
    ImGuiIO &io = ImGui::GetIO();
    bool stay = sQuitNo;
    int focus = -1;

    sQuitNo = false;
    if (!ImGui::IsPopupOpen("Exit game")) {
        ImGui::OpenPopup("Exit game");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Exit game", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        static const char *const kAsk = "Close the game? Progress that was not saved is lost.";
        // the two buttons under the middle of the question (the window is as wide as its text)
        float line = ImGui::CalcTextSize(kAsk).x, pair = 140.0f * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::TextUnformatted(kAsk);
        ImGui::Spacing();
        if (line > pair) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (line - pair) * 0.5f);
        }
        if (ImGui::Button("Exit", ImVec2(140.0f, 0.0f))) {
            exit(0);
        }
        focus = ImGui::IsItemFocused() ? 0 : focus;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            stay = true;
        }
        focus = ImGui::IsItemFocused() ? 1 : focus;
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus(); // (on Cancel: a second press of the same button does not close the game)
            PortA11y_Say(PortA11y_Label(kAsk), 1);
            say_quit_button(1, 0);
            PortA11y_Say(PortA11y_Label("Left and right choose, Enter confirms, Escape goes back to the game"), 0);
            said = 1;
        } else if (focus >= 0 && focus != said) {
            say_quit_button(focus, 1);
            said = focus;
        }
        if (stay) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (stay) {
        sQuit = false;
        input_owner();
        PortA11y_Say(PortA11y_Label("Back to the game"), 1);
    }
}

// ---- Touch controls (phones): a PS2 controller drawn over the picture --------------------------------------------
// Shown by default when no controller is connected (Controls tab: off / automatic / always, size, opacity, and an
// editor where each part is dragged to its place and made larger or smaller). The fingers are read from SDL's touch
// events here (render thread); gs_input.c asks Ui_TouchPad for player 1's buttons and sticks each time the game
// reads its pad. Positions are kept as fractions of the screen, so a layout fits any phone.
enum { TB_SELECT = 0x0001, TB_L3 = 0x0002, TB_R3 = 0x0004, TB_START = 0x0008, TB_UP = 0x0010, TB_RIGHT = 0x0020, TB_DOWN = 0x0040,
       TB_LEFT = 0x0080, TB_L2 = 0x0100, TB_R2 = 0x0200, TB_L1 = 0x0400, TB_R1 = 0x0800, TB_TRIANGLE = 0x1000, TB_CIRCLE = 0x2000,
       TB_CROSS = 0x4000, TB_SQUARE = 0x8000 }; // (include/sys/pad.h's bits, as gs_input.c has them)
enum { TE_DPAD, TE_FACE, TE_LSTICK, TE_RSTICK, TE_L, TE_R, TE_SELECT, TE_START, TE_L3, TE_R3, TE_COUNT };
static const struct { const char *key, *name; float x, y; } kTouchEl[TE_COUNT] = {
    {"dpad", "D-pad", 0.27f, 0.83f},       {"face", "Buttons", 0.87f, 0.65f},  {"lstick", "Left stick", 0.12f, 0.66f},
    {"rstick", "Right stick", 0.72f, 0.85f}, {"l", "L1 / L2", 0.065f, 0.30f}, {"r", "R1 / R2", 0.935f, 0.30f},
    {"select", "SELECT", 0.43f, 0.93f},     {"start", "START", 0.57f, 0.93f},  {"l3", "L3", 0.23f, 0.47f},
    {"r3", "R3", 0.77f, 0.47f},
};
static struct { float x, y, s; } sTe[TE_COUNT];
static int sTouchMode = -1, sTouchSize = 100, sTouchAlpha = 70; // mode: 0 off, 1 when no controller is connected, 2 always
static bool sTouchEdit;                                         // the layout editor is open
static int sTouchSel = -1;                                      // the part chosen in the editor
struct TouchFinger { SDL_FingerID id; float x, y, gx, gy; int el; };
static TouchFinger sFingers[10];
static int sFingerCount;
static float sPanel[4]; // the editor's panel (x0, y0, x1, y1) as last drawn: touches there are the panel's

static void touch_size(float *w, float *h) {
    ImGuiIO &io = ImGui::GetIO();
    *w = io.DisplaySize.x > 0.0f ? io.DisplaySize.x : 1280.0f;
    *h = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 720.0f;
}

static void touch_reset_layout(void) {
    for (int i = 0; i < TE_COUNT; i++) {
        sTe[i].x = kTouchEl[i].x;
        sTe[i].y = kTouchEl[i].y;
        sTe[i].s = 1.0f;
    }
}

static void touch_load(void) {
    char k[48];
    if (sTouchMode >= 0) {
        return;
    }
#ifdef __ANDROID__
    sTouchMode = Port_Setting("touch", 1);
#else
    sTouchMode = Port_Setting("touch", 0); // (a PC: only if asked for, on a touch screen)
#endif
    sTouchSize = Port_Setting("touch_size", 100);
    sTouchAlpha = Port_Setting("touch_alpha", 70);
    for (int i = 0; i < TE_COUNT; i++) {
        snprintf(k, sizeof(k), "touch_%s_x", kTouchEl[i].key);
        sTe[i].x = (float)Port_Setting(k, (int)(kTouchEl[i].x * 1000.0f + 0.5f)) / 1000.0f;
        snprintf(k, sizeof(k), "touch_%s_y", kTouchEl[i].key);
        sTe[i].y = (float)Port_Setting(k, (int)(kTouchEl[i].y * 1000.0f + 0.5f)) / 1000.0f;
        snprintf(k, sizeof(k), "touch_%s_s", kTouchEl[i].key);
        sTe[i].s = (float)Port_Setting(k, 100) / 100.0f;
    }
}

static void touch_save(void) {
    char k[48];
    Port_SettingSave("touch", sTouchMode);
    Port_SettingSave("touch_size", sTouchSize);
    Port_SettingSave("touch_alpha", sTouchAlpha);
    for (int i = 0; i < TE_COUNT; i++) {
        snprintf(k, sizeof(k), "touch_%s_x", kTouchEl[i].key);
        Port_SettingSave(k, (int)(sTe[i].x * 1000.0f + 0.5f));
        snprintf(k, sizeof(k), "touch_%s_y", kTouchEl[i].key);
        Port_SettingSave(k, (int)(sTe[i].y * 1000.0f + 0.5f));
        snprintf(k, sizeof(k), "touch_%s_s", kTouchEl[i].key);
        Port_SettingSave(k, (int)(sTe[i].s * 100.0f + 0.5f));
    }
    Port_SettingsWrite();
}

static bool touch_shown(void) {
    touch_load();
    if (sTouchEdit) {
        return true;
    }
    if (sOpen || sTouchMode == 0) {
        return false;
    }
    return sTouchMode == 2 || !SDL_HasGamepad();
}

// A part's centre and size unit in screen points.
// How far a part reaches from its centre, in its units (x, y).
static void touch_extent(int e, float *hx, float *hy) {
    switch (e) {
    case TE_DPAD: *hx = *hy = 1.45f; break;
    case TE_FACE: *hx = *hy = 1.6f; break;
    case TE_LSTICK: case TE_RSTICK: *hx = *hy = 1.25f; break;
    case TE_L: case TE_R: *hx = 0.95f; *hy = 0.95f; break;
    case TE_SELECT: case TE_START: *hx = 0.6f; *hy = 0.6f; break;
    default: *hx = *hy = 0.55f; break;
    }
}

static void touch_geo(int e, float *cx, float *cy, float *u) {
    float w, h, hx, hy;
    touch_size(&w, &h);
    *u = (w < h ? w : h) * 0.085f * (float)sTouchSize / 100.0f * sTe[e].s;
    *cx = sTe[e].x * w;
    *cy = sTe[e].y * h;
    touch_extent(e, &hx, &hy); /* kept whole on the screen, whatever its size and the screen's shape, and clear of
                                  the camera cut-out and rounded corners (the window's safe area) */
    hx *= *u;
    hy *= *u;
    float x0 = 0.0f, y0 = 0.0f, x1 = w, y1 = h;
    {
        SDL_Rect sa;
        SDL_Window *win = GsDraw_Window();
        if (win != NULL && SDL_GetWindowSafeArea(win, &sa) && sa.w > 0 && sa.h > 0) {
            x0 = (float)sa.x; y0 = (float)sa.y; x1 = (float)(sa.x + sa.w); y1 = (float)(sa.y + sa.h);
        }
    }
    if (2.0f * hx < x1 - x0) { *cx = fminf(fmaxf(*cx, x0 + hx), x1 - hx); }
    if (2.0f * hy < y1 - y0) { *cy = fminf(fmaxf(*cy, y0 + hy), y1 - hy); }
}

// Whether (x, y) is on part e; *d is how far from its centre, in its units (to pick the nearest of two).
static bool touch_hit(int e, float x, float y, float *d) {
    float cx, cy, u;
    touch_geo(e, &cx, &cy, &u);
    float dx = (x - cx) / u, dy = (y - cy) / u;
    *d = sqrtf(dx * dx + dy * dy);
    switch (e) {
    case TE_DPAD: return *d < 1.6f;
    case TE_FACE: return *d < 1.85f;
    case TE_LSTICK: case TE_RSTICK: return *d < 1.6f;
    case TE_L: case TE_R: return fabsf(dx) < 0.95f && fabsf(dy) < 1.0f;
    case TE_SELECT: case TE_START: return fabsf(dx) < 0.8f && fabsf(dy) < 0.5f;
    default: return *d < 0.65f; // L3, R3
    }
}

static const float kFaceD = 0.95f, kFaceR = 0.5f, kStickR = 1.15f;
static const float kFacePos[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}}; // triangle, circle, cross, square
static const unsigned kFaceBit[4] = {TB_TRIANGLE, TB_CIRCLE, TB_CROSS, TB_SQUARE};

// What a finger on part e at (x, y) presses; a stick's deflection (-1..1) in *ax, *ay.
static unsigned touch_press(int e, float x, float y, float *ax, float *ay) {
    float cx, cy, u;
    touch_geo(e, &cx, &cy, &u);
    float dx = (x - cx) / u, dy = (y - cy) / u, len = sqrtf(dx * dx + dy * dy);
    unsigned b = 0;
    switch (e) {
    case TE_DPAD: // eight ways: a direction counts beyond 22.5 degrees from the axis across it
        if (len > 0.25f) {
            float nx = dx / len, ny = dy / len;
            b |= ny < -0.38f ? TB_UP : 0;
            b |= ny > 0.38f ? TB_DOWN : 0;
            b |= nx < -0.38f ? TB_LEFT : 0;
            b |= nx > 0.38f ? TB_RIGHT : 0;
        }
        return b;
    case TE_FACE: { // every button the finger is on (between two: both), else the nearest
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < 4; i++) {
            float ex = dx - kFacePos[i][0] * kFaceD, ey = dy - kFacePos[i][1] * kFaceD, d = sqrtf(ex * ex + ey * ey);
            if (d < 0.66f) {
                b |= kFaceBit[i];
            }
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return b ? b : kFaceBit[best];
    }
    case TE_LSTICK: case TE_RSTICK:
        *ax = dx / kStickR;
        *ay = dy / kStickR;
        if (len > kStickR) {
            *ax = dx / len;
            *ay = dy / len;
        }
        return 0;
    case TE_L: return dy < 0.0f ? TB_L2 : TB_L1;
    case TE_R: return dy < 0.0f ? TB_R2 : TB_R1;
    case TE_SELECT: return TB_SELECT;
    case TE_START: return TB_START;
    case TE_L3: return TB_L3;
    default: return TB_R3;
    }
}

static int touch_part_at(float x, float y) {
    int best = -1;
    float bd = 1e9f, d;
    for (int e = 0; e < TE_COUNT; e++) {
        if (touch_hit(e, x, y, &d) && d < bd) {
            bd = d;
            best = e;
        }
    }
    return best;
}

static void touch_fingers_clear(void) { sFingerCount = 0; }

// SDL's finger events. 1 = the touch controls used it.
static int touch_event(const SDL_Event *ev) {
    float w, h;
    if (ev->type != SDL_EVENT_FINGER_DOWN && ev->type != SDL_EVENT_FINGER_MOTION && ev->type != SDL_EVENT_FINGER_UP &&
        ev->type != SDL_EVENT_FINGER_CANCELED) {
        return 0;
    }
    if (!touch_shown()) {
        sFingerCount = 0;
        return 0;
    }
    touch_size(&w, &h);
    float x = ev->tfinger.x * w, y = ev->tfinger.y * h;
    int i;
    for (i = 0; i < sFingerCount && sFingers[i].id != ev->tfinger.fingerID; i++) {
    }
    if (ev->type == SDL_EVENT_FINGER_DOWN) {
        if (sTouchEdit && x >= sPanel[0] && x <= sPanel[2] && y >= sPanel[1] && y <= sPanel[3]) {
            return 0; // the editor's buttons (ImGui, through the mouse SDL makes of the touch)
        }
        if (i == sFingerCount) {
            if (sFingerCount == 10) {
                return 1;
            }
            sFingerCount++;
        }
        TouchFinger &f = sFingers[i];
        f.id = ev->tfinger.fingerID;
        f.x = x;
        f.y = y;
        f.el = touch_part_at(x, y);
        if (sTouchEdit) {
            sTouchSel = f.el;
            if (f.el >= 0) {
                f.gx = sTe[f.el].x * w - x;
                f.gy = sTe[f.el].y * h - y;
            }
        }
        return 1;
    }
    if (i == sFingerCount) {
        return 0;
    }
    if (ev->type == SDL_EVENT_FINGER_MOTION) {
        sFingers[i].x = x;
        sFingers[i].y = y;
        if (sTouchEdit && sFingers[i].el >= 0) { // dragging a part to its new place
            int e = sFingers[i].el;
            float nx = (x + sFingers[i].gx) / w, ny = (y + sFingers[i].gy) / h;
            sTe[e].x = nx < 0.02f ? 0.02f : nx > 0.98f ? 0.98f : nx;
            sTe[e].y = ny < 0.03f ? 0.03f : ny > 0.97f ? 0.97f : ny;
        }
        return 1;
    }
    // up or cancelled
    sFingers[i] = sFingers[--sFingerCount];
    if (sTouchEdit) {
        touch_save();
    }
    return 1;
}

// Player 1's buttons and sticks from the fingers on the screen. 0 = the touch controls are not in use.
int Ui_TouchPad(unsigned *btn, unsigned char *lx, unsigned char *ly, unsigned char *rx, unsigned char *ry) {
    if (!sReady || sTouchEdit || !touch_shown()) {
        return 0;
    }
    unsigned b = 0;
    float sx[2] = {0.0f, 0.0f}, sy[2] = {0.0f, 0.0f};
    for (int i = 0; i < sFingerCount; i++) {
        int e = sFingers[i].el;
        float ax = 0.0f, ay = 0.0f;
        if (e < 0) {
            continue;
        }
        b |= touch_press(e, sFingers[i].x, sFingers[i].y, &ax, &ay);
        if (e == TE_LSTICK || e == TE_RSTICK) {
            sx[e - TE_LSTICK] = ax;
            sy[e - TE_LSTICK] = ay;
        }
    }
#define AX(v) ((unsigned char)(int)fminf(255.0f, fmaxf(0.0f, 128.0f + (v) * 127.5f)))
    *btn = b;
    *lx = AX(sx[0]);
    *ly = AX(sy[0]);
    *rx = AX(sx[1]);
    *ry = AX(sy[1]);
#undef AX
    return 1;
}

// ---- drawing: a DualShock 2 in glass (dark glossy buttons, the four coloured symbols, PS2 shapes) ----
static float sTa; // this frame's opacity, 0..1
static ImU32 tcol(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)fminf(255.0f, fmaxf(0.0f, a * sTa * 255.0f))); }
static ImU32 tmix(const int *c, int r, int g, int b, float t, float a) { // c mixed toward (r, g, b) by t
    return tcol((int)((float)c[0] + ((float)r - (float)c[0]) * t), (int)((float)c[1] + ((float)g - (float)c[1]) * t),
                (int)((float)c[2] + ((float)b - (float)c[2]) * t), a);
}

static void ttext(ImDrawList *dl, float cx, float cy, float size, ImU32 col, const char *s) {
    ImFont *font = ImGui::GetFont();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
    dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), col, s);
}

static const int kBody[3] = {38, 38, 46};     // the pad's black plastic
static const int kAccent[3] = {245, 140, 33}; // (the app's orange: a stick being held)

// A round glossy button: shadow, body with a soft top light, gloss, rim. `tint` (or NULL) colours it.
static void tglossy(ImDrawList *dl, float x, float y, float r, const int *tint, float tintAmount, bool on) {
    const int *c = kBody;
    dl->AddCircleFilled(ImVec2(x, y + r * 0.10f), r * 1.02f, tcol(0, 0, 0, 0.35f), 40); // shadow
    for (int k = 0; k < 4; k++) { // body: darker at the rim, lighter towards the top-left
        float t = (float)k / 3.0f, rr = r * (1.0f - 0.16f * (float)k);
        float lift = on ? 0.05f : 0.10f + 0.08f * t;
        ImU32 col = tint != NULL ? tmix(c, tint[0], tint[1], tint[2], tintAmount * (0.6f + 0.4f * t), 0.88f)
                                 : tmix(c, 255, 255, 255, lift, 0.85f);
        dl->AddCircleFilled(ImVec2(x - r * 0.04f * t, y - r * 0.05f * t), rr, col, 40);
    }
    if (!on) { // gloss
        dl->AddEllipseFilled(ImVec2(x - r * 0.18f, y - r * 0.42f), ImVec2(r * 0.55f, r * 0.26f), tcol(255, 255, 255, 0.16f), -0.35f, 28);
    }
    dl->AddCircle(ImVec2(x, y), r, tcol(0, 0, 0, 0.7f), 40, 0.05f * r);
}

static void tdraw_face(ImDrawList *dl, float cx, float cy, float u, unsigned b) {
    static const int kCol[4][3] = {{64, 205, 140}, {236, 72, 84}, {96, 146, 236}, {232, 124, 196}}; // the PS2's symbol colours
    dl->AddCircleFilled(ImVec2(cx, cy), (kFaceD + kFaceR) * u * 1.08f, tcol(0, 0, 0, 0.16f), 48); // the well they sit in
    for (int i = 0; i < 4; i++) {
        float x = cx + kFacePos[i][0] * kFaceD * u, y = cy + kFacePos[i][1] * kFaceD * u, r = kFaceR * u, t = 0.11f * u;
        bool on = (b & kFaceBit[i]) != 0;
        const int *c = kCol[i];
        if (on) { // glow
            dl->AddCircleFilled(ImVec2(x, y), r * 1.32f, tcol(c[0], c[1], c[2], 0.28f), 40);
        }
        tglossy(dl, x, y, r, on ? c : NULL, 0.85f, on);
        dl->AddCircle(ImVec2(x, y), r * 0.92f, tcol(c[0], c[1], c[2], on ? 0.0f : 0.55f), 40, 0.05f * u); // a coloured ring
        ImU32 sym = on ? tcol(255, 255, 255, 1.0f) : tcol(c[0], c[1], c[2], 1.0f);
        float s = 0.23f * u;
        switch (i) {
        case 0: {
            ImVec2 p[3] = {{x, y - s * 1.1f}, {x + s * 1.05f, y + s * 0.72f}, {x - s * 1.05f, y + s * 0.72f}};
            dl->AddPolyline(p, 3, sym, ImDrawFlags_Closed, t);
            break;
        }
        case 1: dl->AddCircle(ImVec2(x, y), s, sym, 28, t); break;
        case 2:
            dl->AddLine(ImVec2(x - s, y - s), ImVec2(x + s, y + s), sym, t);
            dl->AddLine(ImVec2(x - s, y + s), ImVec2(x + s, y - s), sym, t);
            break;
        default: dl->AddRect(ImVec2(x - s * 0.85f, y - s * 0.85f), ImVec2(x + s * 0.85f, y + s * 0.85f), sym, 0.0f, 0, t); break;
        }
    }
}

// The PS2's d-pad: four separate arrow-shaped keys around a hollow centre.
static void tdraw_dpad(ImDrawList *dl, float cx, float cy, float u, unsigned b) {
    static const unsigned kBit[4] = {TB_UP, TB_RIGHT, TB_DOWN, TB_LEFT};
    dl->AddCircleFilled(ImVec2(cx, cy), 1.5f * u, tcol(0, 0, 0, 0.16f), 48);
    for (int d = 0; d < 4; d++) {
        float a = (float)d * 1.5707963f, ca = cosf(a), sa = sinf(a);
        // the key pointing up, then turned: inner point, shoulders, outer corners (rounded off)
        static const float kShape[7][2] = {{0.0f, -0.20f}, {0.42f, -0.58f}, {0.42f, -1.20f}, {0.32f, -1.32f},
                                           {-0.32f, -1.32f}, {-0.42f, -1.20f}, {-0.42f, -0.58f}};
        ImVec2 p[7], q[7], sh[7];
        bool on = (b & kBit[d]) != 0;
        for (int k = 0; k < 7; k++) {
            float lx = kShape[k][0] * u, ly = kShape[k][1] * u;
            p[k] = ImVec2(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca);
            sh[k] = ImVec2(p[k].x, p[k].y + 0.08f * u);
            float ix = kShape[k][0] * 0.78f * u, iy = (kShape[k][1] * 0.86f - 0.10f) * u; // (the lit face, inset)
            q[k] = ImVec2(cx + ix * ca - iy * sa, cy + ix * sa + iy * ca);
        }
        dl->AddConvexPolyFilled(sh, 7, tcol(0, 0, 0, 0.35f));
        dl->AddConvexPolyFilled(p, 7, on ? tcol(225, 225, 235, 0.9f) : tmix(kBody, 255, 255, 255, 0.04f, 0.88f));
        dl->AddConvexPolyFilled(q, 7, on ? tcol(250, 250, 255, 0.9f) : tmix(kBody, 255, 255, 255, 0.14f, 0.85f));
        dl->AddPolyline(p, 7, tcol(0, 0, 0, 0.7f), ImDrawFlags_Closed, 0.05f * u);
        // the arrow printed on the key
        float m = 0.18f * u, oy = -0.92f * u;
        ImVec2 t0(0, oy - m), t1(m, oy + m * 0.6f), t2(-m, oy + m * 0.6f), tri[3] = {t0, t1, t2};
        for (int k = 0; k < 3; k++) {
            float lx = tri[k].x, ly = tri[k].y;
            tri[k] = ImVec2(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca);
        }
        dl->AddTriangleFilled(tri[0], tri[1], tri[2], on ? tcol(40, 40, 50, 1.0f) : tcol(150, 150, 168, 0.95f));
    }
}

// An analog stick: a ring with a grip, the cap with a hollow top and a ring of dots.
static void tdraw_stick(ImDrawList *dl, float cx, float cy, float u, float ax, float ay, bool held) {
    float R = kStickR * u;
    dl->AddCircleFilled(ImVec2(cx, cy), R, tcol(0, 0, 0, 0.30f), 56);
    dl->AddCircle(ImVec2(cx, cy), R, tmix(kBody, 255, 255, 255, 0.15f, 0.85f), 56, 0.10f * u);
    dl->AddCircle(ImVec2(cx, cy), R * 0.80f, tcol(255, 255, 255, 0.08f), 56, 0.03f * u);
    float kx = cx + ax * R * 0.62f, ky = cy + ay * R * 0.62f, kr = 0.62f * u;
    tglossy(dl, kx, ky, kr, held ? kAccent : NULL, 0.35f, false);
    dl->AddCircleFilled(ImVec2(kx, ky + kr * 0.04f), kr * 0.58f, tmix(kBody, 0, 0, 0, 0.35f, 0.9f), 36); // the hollow top
    dl->AddCircle(ImVec2(kx, ky), kr * 0.58f, tcol(255, 255, 255, 0.14f), 36, 0.03f * u);
    for (int k = 0; k < 14; k++) { // the grip's dots
        float a = (float)k * 0.4487989f;
        dl->AddCircleFilled(ImVec2(kx + cosf(a) * kr * 0.79f, ky + sinf(a) * kr * 0.79f), kr * 0.05f, tcol(130, 130, 145, 0.8f), 8);
    }
    if (held) {
        dl->AddCircle(ImVec2(kx, ky), kr * 1.06f, tcol(kAccent[0], kAccent[1], kAccent[2], 0.95f), 40, 0.06f * u);
    }
}

// A shoulder button: a glossy rounded bar with its name.
static void tdraw_bar(ImDrawList *dl, float x0, float y0, float x1, float y1, bool on, const char *label, float u) {
    float rr = (y1 - y0) * 0.45f;
    dl->AddRectFilled(ImVec2(x0, y0 + 0.07f * u), ImVec2(x1, y1 + 0.07f * u), tcol(0, 0, 0, 0.35f), rr);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), on ? tcol(225, 225, 235, 0.9f) : tmix(kBody, 255, 255, 255, 0.06f, 0.88f), rr);
    if (!on) {
        dl->AddRectFilled(ImVec2(x0 + 0.08f * u, y0 + 0.05f * u), ImVec2(x1 - 0.08f * u, (y0 + y1) * 0.5f), tcol(255, 255, 255, 0.10f), rr * 0.8f);
    }
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), tcol(0, 0, 0, 0.7f), rr, 0, 0.05f * u);
    ttext(dl, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.36f * u, on ? tcol(30, 30, 38, 1.0f) : tcol(215, 215, 225, 0.95f), label);
}

static void touch_draw(void) {
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    unsigned b = 0;
    float sx[2] = {0, 0}, sy[2] = {0, 0};
    bool held[2] = {false, false};
    for (int i = 0; i < sFingerCount && !sTouchEdit; i++) {
        int e = sFingers[i].el;
        float ax = 0, ay = 0;
        if (e < 0) continue;
        b |= touch_press(e, sFingers[i].x, sFingers[i].y, &ax, &ay);
        if (e == TE_LSTICK || e == TE_RSTICK) {
            sx[e - TE_LSTICK] = ax;
            sy[e - TE_LSTICK] = ay;
            held[e - TE_LSTICK] = true;
        }
    }
    sTa = (float)sTouchAlpha / 100.0f;
    if (sTouchEdit && sTa < 0.8f) {
        sTa = 0.8f;
    }
    for (int e = 0; e < TE_COUNT; e++) {
        float cx, cy, u;
        touch_geo(e, &cx, &cy, &u);
        switch (e) {
        case TE_DPAD: tdraw_dpad(dl, cx, cy, u, b); break;
        case TE_FACE: tdraw_face(dl, cx, cy, u, b); break;
        case TE_LSTICK: case TE_RSTICK: tdraw_stick(dl, cx, cy, u, sx[e - TE_LSTICK], sy[e - TE_LSTICK], held[e - TE_LSTICK]); break;
        case TE_L: case TE_R: {
            bool l = e == TE_L;
            float w = 0.85f * u, hh = 0.36f * u, g = 0.08f * u;
            tdraw_bar(dl, cx - w, cy - 2 * hh - g, cx + w, cy - g, (b & (l ? TB_L2 : TB_R2)) != 0, l ? "L2" : "R2", u);
            tdraw_bar(dl, cx - w, cy + g, cx + w, cy + 2 * hh + g, (b & (l ? TB_L1 : TB_R1)) != 0, l ? "L1" : "R1", u);
            break;
        }
        case TE_SELECT: { // the PS2's: a small flat bar, its name under it
            bool on = (b & TB_SELECT) != 0;
            tdraw_bar(dl, cx - 0.42f * u, cy - 0.14f * u, cx + 0.42f * u, cy + 0.14f * u, on, "", u);
            ttext(dl, cx, cy + 0.38f * u, 0.24f * u, tcol(200, 200, 212, 0.9f), "SELECT");
            break;
        }
        case TE_START: { // the PS2's: a key pointing right, its name under it
            bool on = (b & TB_START) != 0;
            ImVec2 p[3] = {{cx - 0.34f * u, cy - 0.22f * u}, {cx + 0.40f * u, cy}, {cx - 0.34f * u, cy + 0.22f * u}};
            ImVec2 s[3] = {{p[0].x, p[0].y + 0.07f * u}, {p[1].x, p[1].y + 0.07f * u}, {p[2].x, p[2].y + 0.07f * u}};
            dl->AddConvexPolyFilled(s, 3, tcol(0, 0, 0, 0.35f));
            dl->AddConvexPolyFilled(p, 3, on ? tcol(225, 225, 235, 0.9f) : tmix(kBody, 255, 255, 255, 0.10f, 0.9f));
            dl->AddPolyline(p, 3, tcol(0, 0, 0, 0.7f), ImDrawFlags_Closed, 0.05f * u);
            ttext(dl, cx, cy + 0.42f * u, 0.24f * u, tcol(200, 200, 212, 0.9f), "START");
            break;
        }
        default: {
            bool on = (b & (e == TE_L3 ? TB_L3 : TB_R3)) != 0;
            tglossy(dl, cx, cy, 0.45f * u, on ? kAccent : NULL, 0.8f, on);
            ttext(dl, cx, cy, 0.30f * u, on ? tcol(255, 255, 255, 1.0f) : tcol(215, 215, 225, 0.95f), kTouchEl[e].name);
            break;
        }
        }
        if (sTouchEdit) { // the editor: each part's area, the chosen one in orange
            float hx, hy;
            touch_extent(e, &hx, &hy);
            dl->AddRect(ImVec2(cx - hx * u, cy - hy * u), ImVec2(cx + hx * u, cy + hy * u),
                        e == sTouchSel ? IM_COL32(245, 140, 33, 255) : IM_COL32(255, 255, 255, 110), 0.15f * u, 0,
                        e == sTouchSel ? 0.08f * u : 0.04f * u);
        }
    }
}

// The editor's panel: what is chosen, larger / smaller, and done.
static void touch_edit_panel(void) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, S(8.0f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    if (ImGui::Begin("##touchedit", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        if (sTouchSel >= 0) {
            ImGui::Text("%s: %d%%", kTouchEl[sTouchSel].name, (int)(sTe[sTouchSel].s * 100.0f + 0.5f));
        } else {
            ImGui::TextUnformatted("Drag a part to move it; tap one to resize it");
        }
        ImGui::BeginDisabled(sTouchSel < 0);
        if (ImGui::Button(" - ")) {
            sTe[sTouchSel].s = fmaxf(0.5f, sTe[sTouchSel].s - 0.1f);
            touch_save();
        }
        ImGui::SameLine();
        if (ImGui::Button(" + ")) {
            sTe[sTouchSel].s = fminf(2.5f, sTe[sTouchSel].s + 0.1f);
            touch_save();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reset layout")) {
            touch_reset_layout();
            touch_save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Done")) {
            sTouchEdit = false;
            sTouchSel = -1;
            touch_save();
            set_open(true);
        }
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        sPanel[0] = p.x; sPanel[1] = p.y; sPanel[2] = p.x + s.x; sPanel[3] = p.y + s.y;
    }
    ImGui::End();
}

// The Controls tab's part for the touch controls.
static void touch_tab(void) {
    static const char *const kModes[] = {"Off", "When no controller is connected", "Always"};
    touch_load();
    ImGui::SeparatorText("Touch controls");
    ImGui::SetNextItemWidth(S(300.0f));
    if (ImGui::Combo("On screen", &sTouchMode, kModes, 3)) {
        touch_save();
    }
    ImGui::SetNextItemWidth(S(300.0f));
    ImGui::SliderInt("Size", &sTouchSize, 50, 200, "%d%%");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        touch_save();
    }
    ImGui::SetNextItemWidth(S(300.0f));
    ImGui::SliderInt("Opacity", &sTouchAlpha, 15, 100, "%d%%");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        touch_save();
    }
    if (ImGui::Button("Edit layout (move and resize)")) {
        set_open(false);
        sTouchEdit = true;
        sTouchSel = -1;
        sFingerCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset layout##t")) {
        touch_reset_layout();
        touch_save();
    }
    ImGui::Separator();
}

// Dragon Net Battle: the screen behind the main menu's entry. For now only its shape: what a player fills in to
// host or join. Nothing is sent anywhere yet.
extern "C" {
extern volatile int gPortNetMenuRequest;
extern volatile int gPortNetWindowClose; // gs/state.c
}

static void net_open(void) {
    set_open(true);
    sNet = true;
    Port_LobbyHelloAsk(); // (once per opening: is there a newer release, and is this one still let in)
    gPortInputCapture = 1; // the game takes no input at all while this is open (it sits on the main menu behind it)
}

// Quick match (the third tab of the online screen): what stands under the queues.
static bool sNetBusy;   // the lobby is waiting or connecting
static int sQueue = -1; // the queue chosen: 0 single, 1 team, 2..4 DP battle of 10 / 15 / 20 DP

static void quick_status(int state, int &lastState, const char *name) {
    // A search ends by itself after three minutes: a window left open does not go on asking the service for hours,
    // and the player is told that nobody was there instead of being left to wonder. One press searches again.
    static const Uint64 kSearchMs = 180000;
    static Uint64 since;
    if (state == 3 && since != 0 && SDL_GetTicks() - since > kSearchMs) {
        Port_LobbyCancel();
        lastState = -5;
        state = 0;
    }
    if (state == 3 || state == 1) {
        ImGui::TextUnformatted(state == 1 ? "Found a player. Connecting..." : "Searching for a player in this queue...");
        if (state == 3 && since != 0) {
            Uint64 left = kSearchMs - (SDL_GetTicks() - since);
            ImGui::TextDisabled("Searching for %d:%02d more. Leave the window open.", (int)(left / 60000), (int)(left / 1000 % 60));
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            Port_LobbyCancel();
            lastState = 0;
        }
        return;
    }
    if (lastState == -3) {
        ImGui::TextWrapped("%s", Port_LobbyError());
    } else if (lastState == -4) {
        ImGui::TextWrapped("The two games could not reach each other, directly or through the relay. Search again for another player.");
    } else if (lastState == -2) {
        ImGui::TextWrapped("The other player's game is a different version. Search again.");
    } else if (lastState == -5) {
        ImGui::TextWrapped("Nobody was searching this queue in the last three minutes. Search again, or try another queue.");
    }
    if (ImGui::Button(lastState == -5 ? "Search again" : "Search for a match", ImVec2(200.0f, 0.0f))) {
        since = SDL_GetTicks() + 1;
        // The same for everyone in a quick match, whoever turns out to be the host: rollback of up to 8 frames and
        // the input delay measured from the connection. (The Host tab's own choices are for matches with someone
        // you can agree them with; a stranger's saved ones were not to be inherited unseen.)
        Port_NetOptions(Port_RollCan() ? 8 : 0, -2);
        Port_NetSpeedSet(Port_NetSpeedStandard()); // (every quick match: the competitive standard)
        Port_NetRulesSet(sQueue == 0 ? 0 : sQueue == 1 ? 1 : 2, sQueue >= 2 ? sQueue - 2 : 0, 4);
        Port_NetNameSet(name);
        Port_SettingTextSave("net_name", name);
        Port_SettingSave("net_queue", sQueue);
        Port_SettingsWrite();
        lastState = 0;
        Port_LobbyStartQuick(sQueue);
    }
}

static void build_net(void) {
    static char name[17] = "Player", address[64] = "", port[8] = "7000";
    static int tab, roll = -1, delay = 1, battle = -1, timeLimit = 3, speed = -1;
    static bool useCode = true;       // host with a room code (no port forwarding) or on a port
    static char roomCode[8] = "";     // the code to join with
    static int lastState = 0;         // what the lobby said last (for its messages after a failure)
    ImGuiIO &io = ImGui::GetIO();
    bool open = true;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(S(560.0f), io.DisplaySize.x * 0.95f), 0.0f), ImGuiCond_Appearing);
    if (pad_pressed(ImGuiKey_GamepadFaceRight)) { // Circle / B: out of the window, as its close button
        open = false;
    }
    if (ImGui::Begin("Dragon Net Battle", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        {
            int min = 0, latest = 0, mine = Port_LobbyRelease(&min, &latest);
            if (min > mine) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.35f, 1.0f));
                ImGui::TextWrapped("Update available. Online servers are unreachable until you update.");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("You have 0.%d.%d; 0.%d.%d or newer is needed. github.com/z3xox/Tenkaichi3Decomp/releases", mine / 100, mine % 100, min / 100, min % 100);
                ImGui::Separator();
            } else if (latest > mine) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Update available: release 0.%d.%d.", latest / 100, latest % 100);
                ImGui::TextDisabled("github.com/z3xox/Tenkaichi3Decomp/releases");
                ImGui::Separator();
            }
        }
        ImGui::TextWrapped("Fight a player on another computer. One of you hosts, the other joins with the host's address.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(S(260.0f));
        {
            static bool read;
            if (!read) { // the name used last time
                read = true;
                Port_SettingText("net_name", "Player", name, sizeof(name));
            }
        }
        ImGui::InputText("Player name", name, sizeof(name));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Port_SettingTextSave("net_name", name);
            Port_SettingsWrite();
        }
        ImGui::Spacing();
        if (ImGui::BeginTabBar("net")) {
            if (ImGui::BeginTabItem("Host")) {
                static const char *const kRoll[] = {"Off (wait for each other)", "Up to 2 frames", "Up to 4 frames", "Up to 6 frames", "Up to 8 frames"};
                tab = 0;
                ImGui::Checkbox("Use a room code (no port forwarding needed)", &useCode);
                ImGui::SetItemTooltip("You get a short code to tell the other player, and the two games find each other through a\n"
                                      "small matchmaking service. Works with most home routers. Without it you host on a port,\n"
                                      "which must be reachable from the other player (port forwarding or a virtual network).");
                ImGui::BeginDisabled(useCode);
                ImGui::SetNextItemWidth(S(120.0f));
                ImGui::InputText("Port", port, sizeof(port), ImGuiInputTextFlags_CharsDecimal);
                ImGui::EndDisabled();
                if (roll < 0) { // the choices of last time (new names: the first ones' defaults were 4 frames and delay 1)
                    roll = Port_RollCan() ? Port_Setting("net_rollback2", 8) / 2 : 0;
                    delay = Port_Setting("net_delay2", -1) + 1; // 0 = automatic, else the delay + 1
                    if (roll < 0 || roll > 4) { roll = 4; }
                    if (delay < 0 || delay > 7) { delay = 0; }
                }
                ImGui::BeginDisabled(!Port_RollCan());
                ImGui::SetNextItemWidth(S(260.0f));
                ImGui::Combo("Rollback", &roll, kRoll, 5);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("With rollback your own moves come out at once and the game corrects itself when the\n"
                                      "other player's buttons arrive. More frames cope with a slower connection and cost\n"
                                      "more processor time. Off: both games wait for each other every frame.");
                }
                ImGui::SetNextItemWidth(S(260.0f));
                {
                    static const char *const kDelay[] = {"Automatic (from the connection)", "0 frames", "1 frame", "2 frames", "3 frames",
                                                         "4 frames", "5 frames", "6 frames"};
                    ImGui::Combo("Input delay", &delay, kDelay, 8);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long after a press your own move comes out. A slow connection needs more, or the game\n"
                                      "has to guess too far ahead and stutters. Automatic measures the connection when the match\n"
                                      "starts: 1 frame on a good one, more on a slow one.");
                }
                {
                    // The match's rules: the game's own versus menu is not shown online, so they are chosen here.
                    static const char *const kBattle[] = {"Single battle", "Team battle", "DP battle (10 DP)", "DP battle (15 DP)", "DP battle (20 DP)"};
                    static const char *const kTime[] = {"60 seconds", "90 seconds", "180 seconds", "240 seconds", "No limit"};
                    if (battle < 0) {
                        battle = Port_Setting("net_battle", 0);
                        timeLimit = Port_Setting("net_time", 3);
                        if (battle < 0 || battle > 4) { battle = 0; }
                        if (timeLimit < 0 || timeLimit > 4) { timeLimit = 3; }
                    }
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(S(260.0f));
                    ImGui::Combo("Battle", &battle, kBattle, 5);
                    ImGui::SetNextItemWidth(S(260.0f));
                    ImGui::Combo("Time limit", &timeLimit, kTime, 5);
                    {
                        if (speed < 0) {
                            speed = Port_Setting("net_speed_host", Port_NetSpeedStandard()) /* (a new name with 0.1.31: the standard went from 120 to 114) */;
                        }
                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderInt("Game speed", &speed, 100, 150, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::SetItemTooltip("How fast the match runs, for both players; the sound keeps its pitch.\n"
                                              "100%% is the game's own speed. %d%% is the competitive standard, and what\n"
                                              "quick matches run at.\n"
                                              "With a player on 0.1.29 only 100%% and 120%% are possible, with an older\n"
                                              "release only 100%%.", Port_NetSpeedStandard());
                    }
                }
                ImGui::TextDisabled("The host's choices apply to both players.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Quick match")) {
                static const char *const kQueue[] = {"Single battle", "Team battle", "DP battle (10 DP)", "DP battle (15 DP)", "DP battle (20 DP)"};
                static Uint64 asked;
                bool busy = sNetBusy; // (a search is under way: the queue is not changed under it)
                tab = 2;
                if (sQueue < 0) {
                    sQueue = Port_Setting("net_queue", 0);
                    if (sQueue < 0 || sQueue > 4) { sQueue = 0; }
                }
                {
                    // How many wait: asked when the tab is opened and then every thirty seconds, but only while the
                    // player is doing something here (a window left open all day asks nothing after two minutes),
                    // and not during a search (the search hears from the service anyway).
                    static Uint64 active;
                    bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsAnyItemActive();
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !moved; k++) {
                        moved = ImGui::IsKeyDown((ImGuiKey)k);
                    }
                    if (moved || active == 0) {
                        active = SDL_GetTicks() + 1;
                    }
                    if (!sNetBusy && (asked == 0 || (SDL_GetTicks() - asked > 30000 && SDL_GetTicks() - active < 120000))) {
                        asked = SDL_GetTicks() + 1;
                        Port_LobbyQueuesAsk();
                    }
                }
                ImGui::TextWrapped("Be matched with whoever is searching the same queue. All quick matches have no time limit and run at %d%% speed.", Port_NetSpeedStandard());
                ImGui::BeginDisabled(busy);
                for (int i = 0; i < 5; i++) {
                    char label[80];
                    int n = Port_LobbyQueueWaiting(i);
                    if (n < 0) {
                        snprintf(label, sizeof(label), "%s", kQueue[i]);
                    } else {
                        snprintf(label, sizeof(label), "%s   (%d searching)###q%d", kQueue[i], n, i);
                    }
                    ImGui::RadioButton(label, &sQueue, i);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("The two games connect to each other directly when they can, as in most online fighting");
                ImGui::TextDisabled("games: a player who inspects their own network traffic can see the other's address.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Join")) {
                tab = 1;
                ImGui::SetNextItemWidth(S(120.0f));
                ImGui::InputText("Room code", roomCode, sizeof(roomCode), ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
                ImGui::SameLine();
                if (ImGui::Button("Paste")) { // the letters and digits of what was copied, as capitals
                    char *clip = SDL_GetClipboardText();
                    size_t n = 0;
                    for (const char *c = clip; c != NULL && *c != '\0' && n + 1 < sizeof(roomCode); c++) {
                        if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) {
                            roomCode[n++] = (char)(*c >= 'a' ? *c - 32 : *c);
                        }
                    }
                    roomCode[n] = '\0';
                    SDL_free(clip);
                }
                ImGui::TextDisabled("or, without a code, the host's address and port:");
                ImGui::BeginDisabled(roomCode[0] != '\0');
                ImGui::SetNextItemWidth(S(260.0f));
                ImGui::InputText("Host's address", address, sizeof(address));
                ImGui::SetNextItemWidth(S(120.0f));
                ImGui::InputText("Port", port, sizeof(port), ImGuiInputTextFlags_CharsDecimal);
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing();
        {
            int state = Port_LobbyPoll();
            sNetBusy = state == 1 || state == 3;
            if (state == 2) {
                Port_LobbyLaunch(); // the other player is there: the game turns into the session at its next blank
                set_open(false);
                sNet = false;
            }
            bool byCode = tab == 0 ? useCode : roomCode[0] != '\0';
            if (state != 0) {
                lastState = state;
            }
            if (tab == 2) {
                quick_status(state, lastState, name);
            } else if (state == 3 || (state == 1 && byCode)) { // a room code: asking the service, then reaching the other player
                if (tab == 0 && Port_LobbyCode()[0] != '\0') {
                    ImGui::TextUnformatted("Room code:");
                    ImGui::SameLine();
                    ImGui::SetWindowFontScale(1.8f);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", Port_LobbyCode());
                    ImGui::SetWindowFontScale(1.0f);
                    {
                        static Uint64 copiedAt;
                        ImGui::SameLine();
                        if (ImGui::Button("Copy")) {
                            copiedAt = SDL_SetClipboardText(Port_LobbyCode()) ? SDL_GetTicks() : 0;
                        }
                        ImGui::SetItemTooltip("Copy the code, to paste it to the other player.");
                        if (copiedAt != 0 && SDL_GetTicks() - copiedAt < 2000) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("Copied");
                        }
                    }
                    ImGui::TextUnformatted(state == 3 ? "Tell the other player this code. Waiting for them to join..." : "The other player has joined. Connecting...");
                } else {
                    ImGui::TextUnformatted(state == 1 ? "Found the room. Connecting to the host..." : tab == 0 ? "Getting a room code..." : "Looking for the room...");
                }
                if (ImGui::Button("Cancel", ImVec2(S(120.0f), 0.0f))) {
                    Port_LobbyCancel();
                    lastState = 0;
                }
            } else if (state == 1) {
                if (tab == 0) {
                    ImGui::Text("Waiting for the other player on port %s...", port);
                    if (Port_LobbyOther()) {
                        ImGui::TextWrapped("A player with a different version of the game is trying to join. Both need the same release.");
                    }
                } else {
                    ImGui::Text("Looking for %s...", address);
                }
                if (ImGui::Button("Cancel", ImVec2(S(120.0f), 0.0f))) {
                    Port_LobbyCancel();
                }
            } else {
                if (lastState == -3) {
                    ImGui::TextWrapped("%s", Port_LobbyError());
                } else if (lastState == -4) {
                    ImGui::TextWrapped("The two games could not reach each other, directly or through the relay. "
                                       "Hosting on a port (with port forwarding or a virtual network such as Tailscale) still works.");
                }
                bool can = byCode ? (tab == 0 || strlen(roomCode) == 6) : atoi(port) > 0 && (tab == 0 || address[0] != '\0');
                ImGui::BeginDisabled(!can);
                if (ImGui::Button(tab == 0 ? "Host a match" : "Join the match", ImVec2(S(200.0f), 0.0f))) {
                    if (tab == 0 && roll >= 0) {
                        Port_NetOptions(roll * 2, delay == 0 ? -2 : delay - 1);
                        if (battle >= 0) {
                            Port_NetSpeedSet(speed);
                            Port_SettingSave("net_speed_host", speed);
                            Port_NetRulesSet(battle == 0 ? 0 : battle == 1 ? 1 : 2, battle >= 2 ? battle - 2 : 0, timeLimit);
                            Port_SettingSave("net_battle", battle);
                            Port_SettingSave("net_time", timeLimit);
                        }
                        Port_SettingSave("net_rollback2", roll * 2);
                        Port_SettingSave("net_delay2", delay - 1);
                        Port_SettingsWrite();
                    } else {
                        Port_NetOptions(-1, -1); // joining: the host's choices arrive with its answer
                    }
                    Port_NetNameSet(name);
                    Port_SettingTextSave("net_name", name);
                    Port_SettingsWrite();
                    lastState = 0;
                    if (byCode) {
                        Port_LobbyStartCode(tab == 0, roomCode);
                    } else {
                        Port_LobbyStart(tab == 0, address, atoi(port));
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Back", ImVec2(S(120.0f), 0.0f))) {
                    open = false;
                }
                if (state == -2) {
                    ImGui::TextWrapped("The host's game is a different version. Both players need the same release.");
                } else if (state < 0) {
                    ImGui::TextWrapped(tab == 0 ? "That port cannot be used (another program has it?)." : "That address is not known.");
                }
            }
        }
        ImGui::Spacing();
        ImGui::TextDisabled("When you are connected the game switches to the match: both players get the same roster,");
        ImGui::TextDisabled("with everything unlocked, and your own save is left alone. Leaving the match brings you");
        ImGui::TextDisabled("back to where you were. The host's controls also work the menus.");
    }
    ImGui::End();
    if (!open) {
        Port_LobbyCancel();
        set_open(false);
    }
}

void Ui_Toggle(void) {
    if (sReady) {
        set_open(!sOpen);
    }
}

// Android has no F1: the menu opens and closes with the back gesture or button (SDL hands it over as AC_BACK, the
// loader asks SDL to keep it), with the controller's PS / Home button, or with L3 and R3 together.
int Ui_AndroidToggleEvent(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat && ev->key.scancode == SDL_SCANCODE_AC_BACK && sTouchEdit) {
        sTouchEdit = false; // back in the layout editor: done, to the menu it was opened from
        sTouchSel = -1;
        touch_save();
        set_open(true);
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat && ev->key.scancode == SDL_SCANCODE_AC_BACK) {
        if (sCapKind == 0) {
            Ui_Toggle();
            return 1;
        }
        return 0;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && sCapKind == 0) {
        SDL_Gamepad *g = SDL_GetGamepadFromID(ev->gbutton.which);
        bool combo = g != NULL && (ev->gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK || ev->gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) &&
                     SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        if (ev->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE || combo) {
            Ui_Toggle();
            return 1;
        }
    }
    return 0;
}

static void bind(int value) {
    if (sCapAction == PORT_ACTIONS) { // the row "Open this window"
        if (sCapKind == 1) {
            *PortInput_OverlayKey() = value > 0 ? value : SDL_SCANCODE_F1; // (cleared = F1 again: a keyboard always has a way in)
        } else if (value < PORT_PAD_LT) { // (a button, not a trigger)
            *PortInput_OverlayPad() = value;
        }
    } else if (sCapKind == 1) {
        PortInput_Keys(sCapPlayer)[sCapAction] = value;
    } else {
        PortInput_PadButtons(sCapPlayer)[sCapAction] = value;
    }
    PortInput_Save();
}

int Ui_Event(const SDL_Event *ev) {
    if (!sReady) {
        return 0;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED || ev->type == SDL_EVENT_GAMEPAD_REMOVED) { // (also while it is closed: its list of controllers)
        ImGui_ImplSDL3_ProcessEvent(ev);
        return 0;
    }
    if (sQuit) { // the exit question: Esc again takes it away; the rest is the question's
        if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_ESCAPE) {
            sQuitNo = sQuitNo || !ev->key.repeat; // (taken away where it is drawn)
            return 1;
        }
        ImGui_ImplSDL3_ProcessEvent(ev);
        return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
               ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
    }
    if (!sOpen) {
        int touched = touch_event(ev); // (the on-screen pad)
        if (sTouchEdit) { // the layout editor: its panel gets the mouse SDL makes of a touch
            ImGui_ImplSDL3_ProcessEvent(ev);
            return 1;
        }
        return touched;
    }
    touch_event(ev); // (while the menu is open the pad only lets go of its fingers)
    if (sCapKind != 0) { // the next key or button is the binding; Esc leaves it as it was, Delete clears it
        bool done = false;
        if (ev->type == SDL_EVENT_KEY_DOWN) {
            if (ev->key.scancode == SDL_SCANCODE_DELETE || ev->key.scancode == SDL_SCANCODE_BACKSPACE) {
                bind(sCapKind == 1 ? 0 : -1);
            } else if (ev->key.scancode != SDL_SCANCODE_ESCAPE && sCapKind == 1) {
                bind((int)ev->key.scancode);
            }
            done = ev->key.scancode == SDL_SCANCODE_ESCAPE || ev->key.scancode == SDL_SCANCODE_DELETE ||
                   ev->key.scancode == SDL_SCANCODE_BACKSPACE || sCapKind == 1;
        } else if (sCapKind == 2 && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            bind((int)ev->gbutton.button);
            done = true;
        } else if (sCapKind == 2 && ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.value > 20000 &&
                   (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            bind(ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PORT_PAD_LT : PORT_PAD_RT);
            done = true;
        }
        if (done) {
            sCapKind = 0;
            gPortInputCapture = 0;
        }
        if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
            return 1;
        }
    } else if (ev->type == SDL_EVENT_KEY_DOWN && (ev->key.scancode == SDL_SCANCODE_ESCAPE || ev->key.scancode == SDL_SCANCODE_AC_BACK)) {
        set_open(false);
        return 1;
    }
    ImGui_ImplSDL3_ProcessEvent(ev);
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
}

static const char *pad_source_name(int src) {
    static const char *names[] = {"A / Cross", "B / Circle", "X / Square", "Y / Triangle", "Back / Select", "Guide", "Start",
                                  "Left stick click", "Right stick click", "Left shoulder", "Right shoulder", "D-pad up",
                                  "D-pad down", "D-pad left", "D-pad right", "Misc 1", "Right paddle 1", "Left paddle 1",
                                  "Right paddle 2", "Left paddle 2", "Touchpad"};
    static char other[24];
    if (src == PORT_PAD_LT) { return "Left trigger"; }
    if (src == PORT_PAD_RT) { return "Right trigger"; }
    if (src < 0) { return "-"; }
    if (src < (int)(sizeof(names) / sizeof(names[0]))) { return names[src]; }
    snprintf(other, sizeof(other), "Button %d", src);
    return other;
}

static bool tab(const char *label, int index) {
    ImGuiTabItemFlags flags = sForceTab == index ? ImGuiTabItemFlags_SetSelected : 0;
    bool on = ImGui::BeginTabItem(label, NULL, flags);
    if (on && sForceTab < 0) {
        sTab = index;
    }
    return on;
}

static void video_tab(PortVideo &v) {
    static const struct { const char *name; int milli; } aspects[] = {{"4:3 (original)", 1333}, {"16:10", 1600}, {"16:9", 1778}, {"21:9", 2389}, {"32:9", 3556}};
    char label[64];
    int best = 0, count = 0;

    snprintf(label, sizeof(label), "%dx  (%d x %d)", v.scale, 512 * v.scale, 448 * v.scale);
    if (ImGui::BeginCombo("Internal resolution", label)) {
#ifdef __ANDROID__
        const int maxScale = 8; // (as on a PC: what the phone cannot hold goes down by itself, gs_gpu.c)
#else
        const int maxScale = 8;
#endif
        for (int n = 1; n <= maxScale; n++) {
            snprintf(label, sizeof(label), "%dx  (%d x %d)", n, 512 * n, 448 * n);
            if (ImGui::Selectable(label, n == v.scale)) { v.scale = n; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("How finely the game is drawn. 1x is the PlayStation 2's own resolution.\nHigher is sharper and uses more video memory.");
    for (int i = 1; i < (int)(sizeof(aspects) / sizeof(aspects[0])); i++) {
        if (abs(aspects[i].milli - v.aspectMilli) < abs(aspects[best].milli - v.aspectMilli)) { best = i; }
    }
#ifdef __ANDROID__
    { // a phone: "Whole screen", the screen's own shape (plat_stub.c), first
        extern int Port_Setting(const char *name, int def);
        bool whole = Port_Setting("aspect_screen", 1) != 0;
        if (ImGui::BeginCombo("Aspect ratio", whole ? "Whole screen" : aspects[best].name)) {
            if (ImGui::Selectable("Whole screen", whole)) { v.aspectMilli = 0; }
            for (int i = 0; i < (int)(sizeof(aspects) / sizeof(aspects[0])); i++) {
                if (ImGui::Selectable(aspects[i].name, !whole && i == best)) { v.aspectMilli = aspects[i].milli + (aspects[i].milli == v.aspectMilli); }
            }
            ImGui::EndCombo();
        }
    }
#else
    if (ImGui::BeginCombo("Aspect ratio", aspects[best].name)) {
        for (int i = 0; i < (int)(sizeof(aspects) / sizeof(aspects[0])); i++) {
            if (ImGui::Selectable(aspects[i].name, i == best)) { v.aspectMilli = aspects[i].milli; }
        }
        ImGui::EndCombo();
    }
#endif
    ImGui::SetItemTooltip("Wider than 4:3 shows more of the fight to the sides. Menus are stretched.");
    if (ImGui::BeginCombo("2D filtering", v.smooth2d ? "Smooth" : "Sharp (console)")) {
        if (ImGui::Selectable("Sharp (console)", !v.smooth2d)) { v.smooth2d = 0; }
        if (ImGui::Selectable("Smooth", v.smooth2d != 0)) { v.smooth2d = 1; }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("How the display of a fight, the menus and text are drawn above 1x internal resolution.\n"
                          "Sharp: each of the PlayStation 2's pixels is a block. Smooth: as an emulator shows them.\n"
                          "No difference at 1x.");
#ifndef __ANDROID__ // (a phone: always the whole screen, gs_draw.c)
    bool full = v.fullscreen != 0;
    if (ImGui::Checkbox("Full screen", &full)) { v.fullscreen = full; }
    ImGui::SameLine();
    ImGui::TextDisabled("(F11)");
#endif
    {
        bool vs = v.vsync != 0, fs = v.frameSkip != 0;
        if (ImGui::Checkbox("V-sync", &vs)) { v.vsync = vs; }
        ImGui::SetItemTooltip("Each picture waits for the display's refresh. Turn it on if the picture tears.\n"
                              "Off is the default: it adds a little delay.");
        if (ImGui::Checkbox("Skip pictures instead of slowing down", &fs)) { v.frameSkip = fs; }
        ImGui::SetItemTooltip("When this machine cannot show a picture in time, the game normally runs slower.\n"
                              "With this on it stays at full speed and leaves pictures out instead (choppier,\n"
                              "not slower). It helps where the graphics chip is the slow part. On by default.");
    }
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (v.display > count) { v.display = 0; }
    snprintf(label, sizeof(label), "%s", v.display == 0 ? "Chosen by the desktop" : SDL_GetDisplayName(displays[v.display - 1]));
    if (ImGui::BeginCombo("Display", label)) {
        if (ImGui::Selectable("Chosen by the desktop", v.display == 0)) { v.display = 0; }
        for (int i = 0; i < count; i++) {
            snprintf(label, sizeof(label), "%d: %s", i + 1, SDL_GetDisplayName(displays[i]));
            if (ImGui::Selectable(label, v.display == i + 1)) { v.display = i + 1; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Takes effect the next time the game starts.");
    SDL_free(displays);
#ifndef __ANDROID__ // (a phone: Vulkan only, gs_draw.c)
    {   // which renderer draws the game: Vulkan (the default) or OpenGL 3.3, for machines without a working Vulkan
        static const char *const kApi[] = {"Vulkan", "OpenGL"};
        static int api = -1;
        if (api < 0) {
            api = Port_Setting("gpu_api", 0) == 1 ? 1 : 0;
        }
        if (ImGui::Combo("Renderer", &api, kApi, 2)) {
            Port_SettingSave("gpu_api", api);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Takes effect the next time the game starts. Both draw the same picture; Vulkan is faster.\n"
                              "If the chosen one cannot start, the game tries the other.");
        if (api != (sGL ? 1 : 0)) {
            ImGui::TextDisabled("Running now: %s. %s from the next start.", kApi[sGL ? 1 : 0], kApi[api]);
        }
    }
#endif
    {
        // In-between pictures (gs_draw.c): the fight is computed 30 times a second, as on the console; with this
        // on one more picture is drawn between each two. The game itself, its replays and online play are the same.
        static const char *kSmooth[] = { "Off (30, as the console)", "60 pictures a second", "120 pictures a second", "240 pictures a second" };
        int smooth = gsInterp < 0 ? 0 : gsInterp > 3 ? 3 : gsInterp;
        ImGui::SetNextItemWidth(S(416.0f));
        if (ImGui::Combo("Smooth motion", &smooth, kSmooth, 4)) {
            gsInterp = smooth;
            Port_SettingSave("interp", gsInterp);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Experimental. The game computes a fight 30 times a second. With this, more pictures are\n"
                              "drawn between each two. The fight itself does not change, and neither does online play.\n"
                              "The newest picture reaches the screen later: 17 ms at 60, 25 ms at 120, 29 ms at 240.\n"
                              "Choose what your screen can show. F9 switches it off and on.");
    }
    {
        bool on = meter_on();
        if (ImGui::Checkbox("Show frame rate, and the connection in an online match", &on)) {
            sMeter = on;
            Port_SettingSave("meter", sMeter);
            Port_SettingsWrite();
        }
    }
    ImGui::Spacing();
    if (v.texPackCount > 0) {
        bool on = v.texPack != 0;
        snprintf(label, sizeof(label), "Texture pack (%d textures)", v.texPackCount);
        if (ImGui::Checkbox(label, &on)) { v.texPack = on; }
        ImGui::SetItemTooltip("Replacement textures from the folder \"textures\" next to the game.");
    } else {
        ImGui::BeginDisabled();
        bool off = false;
        ImGui::Checkbox("Texture pack (none found)", &off);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Put a pack's files (PCSX2 naming, .dds or .png) into a folder \"textures\" next to the game.");
    }
}

static void effects_tab(PortVideo &v) {
    static const struct { const char *name, *tip; } fx[6] = {
        {"Outline", "The black line around the fighters."},
        {"See-through tint", "The tint that shows a fighter behind scenery."},
        {"Depth tint", "The haze that colours distant things."},
        {"Glare and glow", "The bloom around bright things and the sky's glare."},
        {"Distance blur", "The soft focus on distant scenery."},
        {"Movement blur", "The blur of the scenery while the view turns or moves fast."},
    };
    for (int i = 0; i < 6; i++) {
        bool on = !((v.fxOff >> i) & 1);
        if (ImGui::Checkbox(fx[i].name, &on)) { v.fxOff = (v.fxOff & ~(1 << i)) | (on ? 0 : 1 << i); }
        ImGui::SetItemTooltip("%s", fx[i].tip);
    }
    ImGui::Spacing();
    ImGui::SliderInt("Glow strength", &v.glow, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("100%% is what the PlayStation 2 draws; the default here is 60%%.");
}

// ---- "an update is available" ----
// Once per run the newest published release's number is asked of GitHub (net_match.c), unless "Check for updates" is
// off. If it is newer than this program: a line over the picture for ten seconds, and a line with a button to the
// release page at the top of this window for the rest of the run. Nothing is downloaded by the game.
// Not when nobody is looking (the offscreen and dummy video drivers); BT3_UPDATE_CHECK=1 / 0 says otherwise, and
// BT3_UPDATE_LATEST=<number> is taken as the answer without asking (testing).
static int sUpdCheck = -1; // the setting; -1 not read yet
static bool sUpdAsked, sUpdTold;

static int update_latest(void) {
    const char *fake = SDL_getenv("BT3_UPDATE_LATEST");
    if (sUpdCheck < 0) {
        sUpdCheck = Port_Setting("update_check", 1) != 0;
    }
    if (fake != NULL) {
        return SDL_atoi(fake);
    }
    if (!sUpdAsked) {
        const char *drv = SDL_GetCurrentVideoDriver(), *env = SDL_getenv("BT3_UPDATE_CHECK");
        bool seen = drv == NULL || (SDL_strcmp(drv, "offscreen") != 0 && SDL_strcmp(drv, "dummy") != 0);
        sUpdAsked = true;
        if (env != NULL ? SDL_atoi(env) != 0 : (sUpdCheck != 0 && seen)) {
            Match_UpdateAsk();
        }
    }
    return sUpdCheck != 0 || SDL_getenv("BT3_UPDATE_CHECK") != NULL ? Match_UpdateLatest() : 0;
}

static void misc_tab(void) {
    {
        bool chk = sUpdCheck != 0;
        if (ImGui::Checkbox("Check for updates", &chk)) {
            sUpdCheck = chk;
            Port_SettingSave("update_check", sUpdCheck);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Once per start, ask GitHub for the number of the newest release and say so if it is newer\n"
                              "than this one. Nothing is downloaded: the settings window then has a button to the page.");
    }
    // the start of the game (PR #50): read by FirstRun_Main, so they count from the next start
    static int sSkipLogos = -1, sSkipIntro = -1, sSkipCopy = -1;
    if (sSkipLogos < 0) {
        sSkipLogos = Port_Setting("skip_logos", 0) != 0;
        sSkipIntro = Port_Setting("skip_intro", 0) != 0;
        sSkipCopy = Port_Setting("skip_copyright", 0) != 0;
    }
    bool logos = sSkipLogos != 0, intro = sSkipIntro != 0, copy = sSkipCopy != 0;
    ImGui::Spacing();
    if (ImGui::Checkbox("Skip logos", &logos)) {
        sSkipLogos = logos;
        Port_SettingSave("skip_logos", sSkipLogos);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip("Start without the publisher and developer logos. From the next start of the game.");
    if (ImGui::Checkbox("Skip intro video", &intro)) {
        sSkipIntro = intro;
        Port_SettingSave("skip_intro", sSkipIntro);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip("Start without the opening movie. From the next start of the game.");
    if (ImGui::Checkbox("Skip copyright page", &copy)) {
        sSkipCopy = copy;
        Port_SettingSave("skip_copyright", sSkipCopy);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip("The page of legal text shown for five seconds before the title screen, every time.");
}

static void ui_tab(PortVideo &v) {
    if (ImGui::BeginCombo("Menu font", MenuText_FontName(v.menuFont))) {
        for (int i = 0; i < MenuText_FontCount(); i++) {
            if (!MenuText_FontAvailable(i, 0)) { continue; }
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), v.menuFont == i)) { v.menuFont = i; }
            if (v.menuFont == i) { ImGui::SetItemDefaultFocus(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Original uses the game's sprites. Other fonts replace matching menu labels immediately.\nDecorative lettering keeps its original artwork.");
    if (ImGui::BeginCombo("Dialogue / UI font", MenuText_FontName(v.textFont))) {
        for (int i = 0; i < MenuText_FontCount(); i++) {
            if (!MenuText_FontAvailable(i, 1)) { continue; }
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), v.textFont == i)) { v.textFont = i; }
            if (v.textFont == i) { ImGui::SetItemDefaultFocus(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Replace the shared font used by dialogue, subtitles and ordinary UI text.\n"
                          "Original keeps the game's glyphs. Text placement, colors, shadows and icons are preserved.");
    static const char *const kHud[] = {"At the screen's edges", "At the edges, long health bars", "In the middle, as in 4:3"};
    ImGui::Combo("HUD Style", &v.hudLayout, kHud, 3);
    ImGui::SetItemTooltip("Where a fight's health bars and gauges sit in a picture wider than 4:3.\n"
                          "Long health bars: the bars reach from the edges to the timer.\n"
                          "In the middle: the original 4:3 arrangement, next to the timer.");
    {
        bool m43 = Port_Menus43() != 0;
        if (ImGui::Checkbox("Menus in 4:3", &m43)) {
            Port_Menus43Set(m43);
            Port_SettingSave("menus_43", m43);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("In a picture wider than 4:3, show the menus at their own shape with black at the sides\n"
                              "instead of stretched to the width. Fights keep the wide picture.");
    }
    {
        static const char *const kBars[] = {"Black", "Border"};
        int b = sBars > 0;
        if (ImGui::Combo("Side bars", &b, kBars, 2)) {
            sBars = b;
            Port_SettingSave("side_bars", sBars);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("What fills the sides where the picture is narrower than the window:\n"
                              "a 4:3 picture, or the menus with \"Menus in 4:3\", in a wide window.\n"
                              "Your own: a folder \"bars\" next to the game with trim.png (the strip along the\n"
                              "picture, kept in proportion) and fill.png (stretched over the rest). Read at start.");
    }
}

extern "C" int gPortMusicFollows; // snd_adx.c: 1 = looping music is sped up or slowed down with the game

static void audio_tab(PortVideo &v) {
    ImGui::SliderInt("Music", &v.music, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Sound effects", &v.effects, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Hits, blasts, menu sounds. Takes effect with the next sound that starts.");
    ImGui::SliderInt("Voices", &v.voices, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("The fighters' voices in a fight and the spoken lines outside one.");
    ImGui::SliderInt("Stage sounds", &v.stage, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("A stage's background sound (wind, water) and its own effects.");
    {
        bool follows = gPortMusicFollows > 0;
        ImGui::Spacing();
        if (ImGui::Checkbox("Music follows game speed", &follows)) {
            gPortMusicFollows = follows;
            Port_SettingSave("music_follows_speed", follows);
        }
        ImGui::SetItemTooltip("When the game runs at another speed than 100%% (an online match, the Cheats tab):\n"
                              "off, the music plays as it is; on, it is sped up or slowed down with the game,\n"
                              "at its own pitch, as an emulator does. Spoken lines always follow the game.");
    }
}

// Cheats. "Unlock everything" is the game's own leftover debug function (Save_UnlockAll, which nothing in the game
// calls): the request is set here and carried out on the game's side at the next vertical blank (headless.c).
extern "C" {
extern volatile int gPortUnlockAll, gPortUnlockDone;
}

extern "C" int Port_GameSpeed(void);          // plat_stub.c: the player's own game speed, percent; never used online
extern "C" void Port_GameSpeedSet(int percent);
extern "C" void Port_AudioRefresh(void);

static void cheats_tab(void) {
    {
        int speed = Port_GameSpeed();
        if (ImGui::SliderInt("Game speed", &speed, 50, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            Port_GameSpeedSet(speed);
            Port_AudioRefresh();
        }
        ImGui::SetItemTooltip("How fast the game runs outside online play (the sound keeps its pitch). 100%% is the game's own;\n"
                              "114%% is what competitive play uses. An online match never uses this value:\n"
                              "its speed is the host's or the queue's.");
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            Port_GameSpeedSet(100);
            Port_AudioRefresh();
        }
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::TextWrapped("Unlock everything: all characters, stages, music and items, and the largest amount of Zenni. "
                       "This is a debug function the game's developers left in.");
    ImGui::Spacing();
    ImGui::TextWrapped("It also empties your records list, and it becomes permanent the next time the game saves. "
                       "Use it after your save has been loaded (from the main menu on).");
    ImGui::Spacing();
    if (ImGui::Button("Unlock everything...")) {
        ImGui::OpenPopup("Unlock everything?");
    }
    if (gPortUnlockDone) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Done.");
    }
    if (ImGui::BeginPopupModal("Unlock everything?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Your records list will be emptied. This cannot be undone once the game saves.");
        ImGui::Spacing();
        if (ImGui::Button("Unlock", ImVec2(S(140.0f), 0.0f))) {
            gPortUnlockAll = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(140.0f), 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The Controls tab's part for the sticks: each stick of the player's controller as it is now (the dot), its deadzone
// (the shaded middle: a circle, or the game's own square), what the game is given for it (the ring, and in numbers),
// the triggers, and the settings for these and for vibration. For seeing a stick that drifts and choosing a deadzone
// that covers it.
static void sticks_panel(int player) {
    static const char *const kStick[2] = {"Left stick", "Right stick"}, *const kTrigger[2] = {"Left trigger", "Right trigger"};
    static const char *const kDir[5] = {"", "up", "down", "left", "right"};
    const float R = 42.0f, box = 2.0f * R + 8.0f;
    const ImU32 colZone = ImGui::GetColorU32(ImGuiCol_Text, 0.22f);
    const ImU32 colRaw = ImGui::GetColorU32(ImGuiCol_Text), colGame = ImGui::GetColorU32(ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    int *dead = PortInput_Deadzone(player), *vib = PortInput_Vibration(player);
    PortStickView view;
    char label[64];

    PortInput_StickView(player, &view);
    if (!ImGui::BeginTable("sticks", 2)) {
        return;
    }
    float top = 0.0f;
    for (int s = 0; s < 2; s++) {
        ImGui::TableNextColumn();
        if (s == 0) {
            top = ImGui::GetCursorScreenPos().y;
        } else { // (the second cell would start lower by the text offset of the first one's last line)
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos(), c(at.x + box * 0.5f, at.y + box * 0.5f);
        ImGui::Dummy(ImVec2(box, box));
        dl->AddCircleFilled(c, R, ImGui::GetColorU32(ImGuiCol_FrameBg), 48);
        if (dead[s] < 0) { // the game's own: each axis by itself, 50 of 127.5
            float h = R * 50.0f / 127.5f;
            dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), colZone);
        } else if (dead[s] > 0) {
            dl->AddCircleFilled(c, R * (float)dead[s] / 100.0f, colZone, 48);
        }
        dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), ImGui::GetColorU32(ImGuiCol_Border));
        dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), ImGui::GetColorU32(ImGuiCol_Border));
        dl->AddCircle(c, R, ImGui::GetColorU32(ImGuiCol_Border), 48);
        if (view.connected) {
            dl->AddCircle(ImVec2(c.x + view.game[s][0] * R, c.y + view.game[s][1] * R), 5.5f, colGame, 16, 1.5f);
            dl->AddCircleFilled(ImVec2(c.x + view.raw[s][0] * R, c.y + view.raw[s][1] * R), 3.0f, colRaw, 12);
        }
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding(); // (in both cells: the second one's lines start that much lower anyway)
        if (view.connected) {
            ImGui::Text(view.dir[s] != 0 ? "%s: %s" : "%s", kStick[s], kDir[view.dir[s]]);
            ImGui::Text("stick  %+.2f %+.2f", view.raw[s][0], view.raw[s][1]);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colGame), "game   %+.2f %+.2f", view.game[s][0], view.game[s][1]);
        } else {
            ImGui::TextUnformatted(kStick[s]);
            ImGui::TextDisabled("no controller");
            ImGui::TextDisabled(" ");
        }
        ImGui::SetNextItemWidth(100.0f);
        snprintf(label, sizeof(label), "Deadzone##%d", s);
        ImGui::SliderInt(label, &dead[s], -1, PORT_DEADZONE_MAX, dead[s] < 0 ? "Original" : "%d%%", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            PortInput_Save();
        }
        ImGui::SetItemTooltip("How far the stick has to be moved before the game is told anything (the shaded middle).\n"
                              "Original: the game's own, 39%% for left / right and for up / down separately, a square.\n"
                              "0%% to 90%%: a round one of that size instead; from its edge the value grows evenly\n"
                              "to full at full tilt, and the stick counts as a direction half way between the two.\n"
                              "The white dot is the stick, the ring what the game is given; after the stick's name,\n"
                              "the direction the game takes it for. Choose a size that covers where the dot rests\n"
                              "when the stick is let go.");
        ImGui::EndGroup();
    }
    for (int s = 0; s < 2; s++) {
        ImGui::TableNextColumn();
        snprintf(label, sizeof(label), "%d%%%s", (int)(view.trigger[s] * 100.0f + 0.5f), view.triggerHeld[s] ? " pressed" : "");
        ImGui::ProgressBar(view.trigger[s], ImVec2(144.0f, 0.0f), label);
        ImGui::SetItemTooltip("A trigger counts as pressed from %d%% on. The game is told pressed or not, nothing between.",
                              (PORT_TRIGGER_AT * 100 + 16383) / 32767);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(kTrigger[s]);
    }
    ImGui::EndTable();
    bool on = vib[0] != 0;
    if (ImGui::Checkbox("Vibration", &on)) {
        vib[0] = on;
        PortInput_Save();
    }
    ImGui::SetItemTooltip("The game's own vibration (hits, clashes, charging), on this player's controller.\n"
                          "The game's Options > Controller > Vibration has to be on as well: it is, unless you turned it off.\n"
                          "The game gives none in a replay or for a side the computer plays.");
    ImGui::BeginDisabled(!on);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Strength", &vib[1], 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        PortInput_Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Test")) {
        PortInput_VibrationTest(player);
    }
    ImGui::SetItemTooltip("Half a second of both motors at this strength.");
    ImGui::EndDisabled();
    if (view.connected && !view.canVibrate) {
        ImGui::SameLine();
        ImGui::TextDisabled("(this controller has none)");
    }
}

static void controls_tab(void) {
    static int player;
    char label[96];
    int count = 0;

    ImGui::RadioButton("Player 1", &player, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Player 2", &player, 1);
    int *slot = PortInput_PadSlot(player), *keys = PortInput_Keys(player), *pads = PortInput_PadButtons(player);
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (*slot < 0) {
        snprintf(label, sizeof(label), "None");
    } else if (*slot < count) {
        snprintf(label, sizeof(label), "%d: %s", *slot + 1, SDL_GetGamepadNameForID(ids[*slot]));
    } else {
        snprintf(label, sizeof(label), "Controller %d (not connected)", *slot + 1);
    }
    ImGui::SetNextItemWidth(S(300.0f));
    if (ImGui::BeginCombo("Controller", label)) {
        if (ImGui::Selectable("None", *slot < 0)) { *slot = -1; PortInput_Save(); }
        for (int i = 0; i < (count > 4 ? count : 4); i++) {
            if (i < count) {
                snprintf(label, sizeof(label), "%d: %s", i + 1, SDL_GetGamepadNameForID(ids[i]));
            } else {
                snprintf(label, sizeof(label), "Controller %d (not connected)", i + 1);
            }
            if (ImGui::Selectable(label, *slot == i)) { *slot = i; PortInput_Save(); }
        }
        ImGui::EndCombo();
    }
    SDL_free(ids);
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        PortInput_ResetDefaults(player);
        PortInput_Save();
    }
    if (ImGui::CollapsingHeader("Sticks, triggers and vibration", ImGuiTreeNodeFlags_DefaultOpen)) {
        sticks_panel(player);
    }
    ImGui::TextDisabled("Choose a binding, then press the new key or button. Esc keeps it, Delete clears it;");
    ImGui::TextDisabled("with a controller, press nothing for five seconds to keep it.");
    // (never lower than a few rows: in a small window the tab scrolls as a whole instead)
    if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y > 140.0f ? ImGui::GetContentRegionAvail().y : 140.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Keyboard");
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        {   // what opens and closes this window (not a player's: one for the program)
            const int a = PORT_ACTIONS;
            bool waitKey = sCapKind == 1 && sCapAction == a, waitPad = sCapKind == 2 && sCapAction == a;
            int key = *PortInput_OverlayKey(), pad = *PortInput_OverlayPad();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Open this window");
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##k%d", waitKey ? "press a key..." : key > 0 ? SDL_GetScancodeName((SDL_Scancode)key) : "-", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(1, player, a);
            }
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##p%d", waitPad ? "press a button..." : pad >= 0 ? pad_source_name(pad) : "START + SELECT", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(2, player, a);
            }
        }
        for (int a = 0; a < PORT_ACTIONS; a++) {
            bool waitKey = sCapKind == 1 && sCapPlayer == player && sCapAction == a;
            bool waitPad = sCapKind == 2 && sCapPlayer == player && sCapAction == a;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(PortInput_ActionLabel(a));
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##k%d", waitKey ? "press a key..." : keys[a] > 0 ? SDL_GetScancodeName((SDL_Scancode)keys[a]) : "-", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(1, player, a);
            }
            ImGui::TableNextColumn();
            if (a < PORT_PAD_BUTTONS) {
                snprintf(label, sizeof(label), "%s##p%d", waitPad ? "press a button..." : pad_source_name(pads[a]), a);
                if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                    capture(2, player, a);
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(a < 20 ? "Left stick" : "Right stick");
            }
        }
        ImGui::EndTable();
    }
}

static void build(void) {
    PortVideo v, was;
    ImGuiIO &io = ImGui::GetIO();
    bool open = true;

    GsGpu_GetSettings(&v);
    was = v;
    if (sCapKind == 2 && SDL_GetTicks() > sCapUntil) { // nothing was pressed: the binding stays
        sCapKind = 0;
        gPortInputCapture = 0;
    }
    if (pad_pressed(ImGuiKey_GamepadFaceRight)) { // Circle / B: out of the window
        open = false;
    }
    if (sForceTab < 0 && (pad_pressed(ImGuiKey_GamepadL1) || pad_pressed(ImGuiKey_GamepadR1))) {
        sForceTab = (sTab + (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? 6 : 1)) % 7;
        sPadTab = true;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // (760 high where the picture has the room: the Controls tab's sticks and its list of bindings together)
    ImGui::SetNextWindowSize(ImVec2(fminf(S(640.0f), io.DisplaySize.x * 0.95f), fminf(S(io.DisplaySize.y >= 800.0f * sUiScale ? 760.0f : 600.0f), io.DisplaySize.y * 0.95f)), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &open, ImGuiWindowFlags_NoCollapse)) {
        {
            int latest = update_latest();
            if (latest > PORT_RELEASE) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Update available: 0.%d.%d (this is 0.%d.%d).", latest / 100, latest % 100,
                                   PORT_RELEASE / 100, PORT_RELEASE % 100);
                ImGui::SameLine();
                if (ImGui::Button("Download page")) {
                    SDL_OpenURL("https://github.com/" PORT_UPDATE_REPO "/releases/latest");
                }
                ImGui::SetItemTooltip("Opens the release page in your browser. To update, copy the new files over the old ones.");
                ImGui::Separator();
            }
        }
        // the tabs in a region of their own, above a last line that is always there
        ImGui::BeginChild("body", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_NavFlattened);
        if (ImGui::BeginTabBar("tabs")) {
            if (tab("Video", 0)) { video_tab(v); ImGui::EndTabItem(); }
            if (tab("Effects", 1)) { effects_tab(v); ImGui::EndTabItem(); }
            if (tab("Audio", 2)) { audio_tab(v); ImGui::EndTabItem(); }
            if (tab("UI", 3)) { ui_tab(v); ImGui::EndTabItem(); }
            if (tab("Controls", 4)) {
#ifdef __ANDROID__
                touch_tab();
#endif
                bool cs = Port_ClashSquare() != 0;
                if (ImGui::Checkbox("Clashes: Square counts as a direction", &cs)) {
                    Port_ClashSquareSet(cs);
                }
                ImGui::SetItemTooltip("In a clash of two beams or two rushes the game counts newly pressed directions:\n"
                                      "turning the stick, or tapping the d-pad. With this on, each press of Square\n"
                                      "counts as one of them while the clash lasts, as a tap of the d-pad does.\n"
                                      "It only changes your own pad, so it can be used in online play.");
                if (cs) {
                    static const int kQuarters[] = {4, 6, 7, 8};
                    static const char *const kWorth[] = {"1", "1.5", "1.75", "2"};
                    int at = 2;
                    for (int i = 0; i < 4; i++) {
                        if (kQuarters[i] == Port_ClashSquareX()) {
                            at = i;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(S(80.0f));
                    if (ImGui::Combo("per press", &at, kWorth, 4)) {
                        Port_ClashSquareXSet(kQuarters[at]);
                    }
                    ImGui::SetItemTooltip("What one press of Square is worth. The d-pad has four buttons to tap in turn and the\n"
                                          "stick four directions a turn; Square is one button. 1 is a single d-pad tap.\n"
                                          "Never more than one a frame, which is the most the game counts from any pad.");
                }
                ImGui::Separator();
                controls_tab();
                ImGui::EndTabItem();
            }
            if (tab("Cheats", 5)) { cheats_tab(); ImGui::EndTabItem(); }
            if (tab("Misc", 6)) { misc_tab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        if (sPadTab) { // (a tab chosen with the shoulder buttons)
            sPadTab = false;
            sTab = sForceTab;
            sForceTab = -1;
        } else if (ImGui::GetFrameCount() > 3) {
            sForceTab = -1;
        }
        ImGui::Separator();
        if (ImGui::Button("Exit game")) {
            sQuit = true;
        }
        if ((io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("D-pad: move   Cross / A: choose   Circle / B: back   L1 / R1: tab");
        }
    }
    ImGui::End();
    if (memcmp(&v, &was, sizeof(v)) != 0) {
        GsGpu_SetSettings(&v);
    }
    if (!open) {
        set_open(false);
    }
}

static bool sBuilt; // this frame has something to draw: the window, or the line over the picture

void Ui_DrawAgain(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    ImDrawData *dd = sReady && sBuilt ? ImGui::GetDrawData() : NULL; // (sBuilt: Ui_Draw made a picture this frame)
    SDL_GPUColorTargetInfo ti;
    SDL_GPURenderPass *pass;

    if (dd == NULL || dd->CmdListsCount == 0 || dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) {
        return;
    }
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_zero(ti);
    ti.texture = target;
    ti.load_op = SDL_GPU_LOADOP_LOAD;
    ti.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ti, 1, NULL);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
}

static char sNotice[160];
static Uint64 sNoticeUntil;
static volatile int sNoticeNew;

extern "C" void Port_UiNotice(const char *text) {
    SDL_strlcpy(sNotice, text != NULL ? text : "", sizeof(sNotice));
    sNoticeNew = 1;
}

// The meter: frames shown and the game's speed, and in an online match the connection. A setting (Video tab).
static bool meter_on(void) {
    if (sMeter < 0) {
#ifdef __ANDROID__
        const int def = 1; // phones: shown unless switched off (meter=0 in bt3_settings.txt, or BT3_METER=0)
#else
        const int def = 0;
#endif
        sMeter = getenv("BT3_METER") != NULL ? atoi(getenv("BT3_METER")) != 0 : Port_Setting("meter", def) != 0;
    }
    return sMeter != 0;
}
static float sFps, sSpeed; // frames shown per second; the game's vertical blanks per second as a share of 60

static void meter_count(void) { // once per frame shown
    static Uint64 t0;
    static unsigned frames, blanks0;
    Uint64 now = SDL_GetTicks();
    frames++;
    if (t0 == 0) {
        t0 = now;
        blanks0 = gPortLiveBlanks;
    } else if (now - t0 >= 500) {
        sFps = (float)frames * 1000.0f / (float)(now - t0);
        sSpeed = (float)(gPortLiveBlanks - blanks0) * 1000.0f / (float)(now - t0) / 60.0f * 100.0f;
        frames = 0;
        blanks0 = gPortLiveBlanks;
        t0 = now;
    }
}

static void meter_draw(void) {
    int n[8];
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##meter", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings)) {
        // a fight shows 30 frames a second and the menus 60, as on the console; the speed is what should stay at 100%
        ImGui::Text("%.0f fps   speed %.0f%%", sFps, sSpeed);
        if (Port_NetStats(n)) {
            if (n[0] >= 0) {
                ImGui::Text("ping %d ms%s", n[0], Port_LobbyRelayed() ? " (relay)" : "");
            } else {
                ImGui::TextUnformatted("ping ...");
            }
            if (n[5] > 0) {
                ImGui::Text("rollbacks %d/s, %.1f frames", n[1], (float)n[2] / 10.0f);
            } else {
                ImGui::TextUnformatted("rollback off");
            }
            ImGui::Text("waited %d ms/s (%d times)", n[4], n[3]);
            ImGui::Text("delay %d%s, rollback up to %d", n[6], n[7] ? " (auto)" : "", n[5]);
        }
    }
    ImGui::End();
}

// The name of an added stage or song as text, in the rectangle the menu gives (the strip images' place): cream
// letters with a dark outline, as tall as the rectangle allows, centred for a stage and from the left for a song.
static void name_text(ImDrawList *dl, const char *text, float x, float y, float w, float h, bool centre) {
    ImFont *font = ImGui::GetFont();
    float size = h * (centre ? 0.42f : 0.72f);
    ImVec2 ext = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    if (ext.x > w * 0.96f && ext.x > 0.0f) { // a long name: smaller, to fit
        size *= w * 0.96f / ext.x;
        ext = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    }
    float tx = centre ? x + (w - ext.x) * 0.5f : x + h * 0.15f, ty = y + (h - ext.y) * 0.5f, o = size * 0.07f + 1.0f;
    for (int k = 0; k < 8; k++) {
        static const float dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        dl->AddText(font, size, ImVec2(tx + dx[k] * o, ty + dy[k] * o), IM_COL32(40, 20, 0, 255), text);
    }
    dl->AddText(font, size, ImVec2(tx, ty), IM_COL32(255, 236, 170, 255), text);
}

/* Names in the game's own lettering (namefont.c): a name with no picture in a strip is put together from the letters
   of the disc's name pictures, once, and kept as a texture while it is the one shown (a few are kept, for going up
   and down a list). */
struct FontName {
    SDL_GPUTexture *tex; // NULL: this name cannot be drawn that way (a character the disc's names do not have)
    int style, w, h, choice, resolution;
    char text[64];
    bool used;
};
static FontName sFontNames[12];
static unsigned sFontNext;

static const FontName *font_name(int style, const char *text) {
    int choice = MenuText_GetFont();
    int resolution = choice ? 4 : 1; // (a font's strip: 4 times the disc's, for a window larger than the game's picture)
    if (text == NULL || text[0] == '\0' || (choice == 0 && !NameFont_Ready())) { // (not ready: asked again next frame)
        return NULL;
    }
    for (FontName &n : sFontNames) {
        if (n.used && n.style == style && n.choice == choice && n.resolution == resolution && strncmp(n.text, text, sizeof(n.text) - 1) == 0) {
            return n.tex != NULL ? &n : NULL;
        }
    }
    FontName &n = sFontNames[sFontNext++ % (sizeof(sFontNames) / sizeof(sFontNames[0]))];
    if (n.used && n.tex != NULL) {
        if (sGL) {
            GsGl_StripTextureFree((unsigned)(intptr_t)n.tex);
        } else {
            SDL_ReleaseGPUTexture(sDevice, n.tex);
        }
    }
    n.tex = NULL;
    n.w = n.h = 0;
    n.used = true;
    n.style = style;
    n.choice = choice; n.resolution = resolution;
    SDL_strlcpy(n.text, text, sizeof(n.text));
    unsigned char *pix;
    if (choice) {
        pix = style == NF_STAGE ? MenuText_RenderStage(n.text, resolution, &n.w, &n.h) :
                                  MenuText_RenderSong(n.text, style == NF_SONG_LIT, resolution, &n.w, &n.h);
    } else {
        pix = NameFont_Compose(style, n.text, &n.w, &n.h);
    }
    if (pix != NULL) {
        n.tex = make_strip(pix, (size_t)n.w * n.h * 4, (uint32_t)n.w, (uint32_t)n.h, 1); // (frees pix)
    }
    if (choice) { n.w /= resolution; n.h /= resolution; }
    return n.tex != NULL ? &n : NULL;
}

/* Draws such a name over the place of the disc's name picture: (x, y) is the corner of that picture's row in the
   window, kx and ky are window pixels per pixel of the row (512 wide). A name longer than the row is made smaller
   to fit, about the row's middle line; a stage name stays in the middle, a song name keeps its left edge. */
static void font_draw(ImDrawList *dl, const FontName *n, float x, float y, float kx, float ky, bool centre) {
    float k = n->w > 512 ? 512.0f / (float)n->w : 1.0f;
    float w = (float)n->w * kx * k, h = (float)n->h * ky * k;
    float x0 = centre ? x + (512.0f * kx - w) * 0.5f : x;
    float y0 = y + ((float)n->h * ky - h) * 0.5f;
    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)n->tex), ImVec2(x0, y0), ImVec2(x0 + w, y0 + h));
}

// Builds this frame's picture of the overlay (the settings or the online window, the line over the picture).
// false: there is nothing to draw. The same for both back ends; each then draws ImGui's data its own way.

// ---- The Discord status: where the player is, in two lines (port/android/native/loader.c sends them) -----------
// Read from the game's own state (docs/systems/menu_overlay.md, include/menu/*.h, include/battle/battle.h): the
// progress block's mode says which screen, or which game mode a fight belongs to; the battle block says whether a
// fight runs and whether it is paused. Only reads; published with a sequence number (odd while being written).
extern "C" {
extern uint32_t gProgress;          // the game's ViewProgress * (a 4-byte pointer)
int Port_BattleRunning(void);       // src/battle/battle_work.c: 1 while a fight (or its loading) runs
int Port_BattlePaused(void);        // the pause menu is open
int Port_SurvivalOpponent(void);    // which opponent of the survival run
int Battle_GetMode(void);
int Battle_GetStage(void);
int BattleReplay_IsActive(void);
volatile unsigned gPortDiscordSeq;
char gPortDiscordDetails[128], gPortDiscordState[128];
}

static int prog(int off) { return gProgress ? *(volatile int32_t *)(uintptr_t)(gProgress + (uint32_t)off) : 0; }

// The sagas of Dragon History, by gProgress->subMenu (episodes 3, 4, 7, 5, 16, 5, 4, 4). (Names by the counts and
// the game's order; check against the saga select.)
static const char *const kSagas[8] = {"Saiyan Saga", "Frieza Saga", "Android Saga", "Majin Buu Saga", "Movies",
                                      "Dragon Ball", "Dragon Ball GT", "What-If Saga"};
static const char *const kTours[5] = {"World Martial Arts Tournament", "World Martial Arts Big Tournament", "Cell Games",
                                      "Otherworld Tournament", "Yamcha Game"};

static const char *saga_name(void) {
    int s = prog(0x34);
    return s >= 0 && s < 8 ? kSagas[s] : "Dragon History";
}

static void discord_status(void) {
    char d[128], st[128], where[96];
    int mode = prog(0x18);
    int running = Port_BattleRunning();
    d[0] = st[0] = where[0] = '\0';
    if (gProgress == 0) {
        snprintf(d, sizeof(d), "Starting up");
    } else if (running) {
        int bm = Battle_GetMode(), stage = Battle_GetStage();
        const char *sn = stage >= 0x24 ? PortStages_Name(stage - 0x24) : NameFont_StageName(stage);
        if (sn != NULL) {
            snprintf(where, sizeof(where), "%s", sn);
        }
        if (Port_NetActive()) {
            snprintf(d, sizeof(d), "Dragon Net Battle (online)");
            snprintf(st, sizeof(st), "Fighting online");
        } else if (bm == 7 || mode <= 2) {
            snprintf(d, sizeof(d), "Title screen");
            snprintf(st, sizeof(st), "Watching a demo battle");
        } else if (bm == 1) {
            snprintf(d, sizeof(d), "Dragon History: %s", saga_name());
            snprintf(st, sizeof(st), "Episode %d", prog(0x38) + 1);
        } else if (mode >= 14 && mode <= 16) {
            snprintf(d, sizeof(d), "Mission 100");
            snprintf(st, sizeof(st), "Mission %d", prog(0x63C) * 5 + prog(0x640) + 1);
        } else if (mode >= 17 && mode <= 19) {
            snprintf(d, sizeof(d), "Survival: course %d", prog(0x640) + 1);
            snprintf(st, sizeof(st), "Opponent %d", Port_SurvivalOpponent() + 1);
        } else if (mode >= 20 && mode <= 23) {
            int turn = prog(0x674);
            snprintf(d, sizeof(d), "Sim Dragon");
            snprintf(st, sizeof(st), "Round %d, battle", turn / 10 + 1);
        } else if (mode >= 25 && mode <= 27) {
            snprintf(d, sizeof(d), "Ultimate Battle: ranking");
            snprintf(st, sizeof(st), "Challenging rank %d", prog(0x688) + 1);
        } else if (mode >= 28 && mode <= 30) {
            snprintf(d, sizeof(d), "Ultimate Battle Z");
            snprintf(st, sizeof(st), "Course %d", prog(0x688) + 1);
        } else if (bm == 4 || (mode >= 33 && mode <= 35)) {
            int t = prog(0x84);
            snprintf(d, sizeof(d), "%s", t >= 0 && t < 5 ? kTours[t] : "World Tournament");
            snprintf(st, sizeof(st), prog(0x80) ? "Free play" : "Round %d", prog(0x90) + 1);
        } else if (bm == 5 || mode == 44) {
            snprintf(d, sizeof(d), "Training");
            snprintf(st, sizeof(st), "Lesson");
        } else if (bm == 6 || mode == 45) {
            snprintf(d, sizeof(d), "Training");
            snprintf(st, sizeof(st), "Free training");
        } else if (mode == 56 || BattleReplay_IsActive()) {
            snprintf(d, sizeof(d), "Data Center");
            snprintf(st, sizeof(st), "Watching a replay");
        } else {
            static const char *const kWho[3] = {"1P vs COM", "1P vs 2P", "COM vs COM"};
            static const char *const kKind[3] = {"", " (team)", " (DP battle)"};
            int who = prog(0x620), kind = prog(0x624);
            snprintf(d, sizeof(d), "Duel%s", kind >= 0 && kind < 3 ? kKind[kind] : "");
            snprintf(st, sizeof(st), "%s", who >= 0 && who < 3 ? kWho[who] : "Battle");
        }
        if (Port_BattlePaused()) { // the pause menu
            char t[128];
            snprintf(t, sizeof(t), "Paused - %s", st);
            snprintf(st, sizeof(st), "%s", t);
        } else if (where[0] != '\0') {
            char t[128];
            snprintf(t, sizeof(t), "%s - %s", st, where);
            snprintf(st, sizeof(st), "%s", t);
        }
    } else {
        const char *a = NULL, *b = NULL;
        switch (mode) {
        case 1: a = "Title screen"; break;
        case 2: a = "Title screen"; b = "Opening movie"; break;
        case 3: a = "Dragon History"; b = "Watching the ending"; break;
        case 4: a = "Main menu"; break;
        case 6: a = "Dragon History"; b = "Choosing a saga"; break;
        case 7: snprintf(d, sizeof(d), "Dragon History: %s", saga_name()); b = "Choosing an episode"; break;
        case 8: snprintf(d, sizeof(d), "Dragon History: %s", saga_name()); b = "Episode results"; break;
        case 9: snprintf(d, sizeof(d), "Dragon History: %s", saga_name()); b = "End of the saga"; break;
        case 10: a = "Dragon History"; b = "Saving"; break;
        case 13: a = "Ultimate Battle"; b = "Choosing a mode"; break;
        case 14: a = "Mission 100"; b = "Choosing a mission"; break;
        case 15: a = "Mission 100"; b = "Choosing fighters"; break;
        case 16: a = "Mission 100"; b = "Results"; break;
        case 17: a = "Survival"; b = "Choosing a course"; break;
        case 18: a = "Survival"; b = "Choosing a fighter"; break;
        case 19: a = "Survival"; b = "Results"; break;
        case 20: a = "Sim Dragon"; b = "Main screen"; break;
        case 21: a = "Sim Dragon"; b = "Choosing a fighter"; break;
        case 22: a = "Sim Dragon"; break;
        case 23: a = "Sim Dragon"; b = "Results"; break;
        case 24: a = "Disc Fusion"; break;
        case 25: a = "Ultimate Battle: ranking"; b = "Choosing a fighter"; break;
        case 26: a = "Ultimate Battle: ranking"; b = "Ranking ladder"; break;
        case 27: a = "Ultimate Battle: ranking"; b = "Results"; break;
        case 28: a = "Ultimate Battle Z"; b = "Choosing a course"; break;
        case 29: a = "Ultimate Battle Z"; b = "Choosing a fighter"; break;
        case 30: a = "Ultimate Battle Z"; b = "Results"; break;
        case 33: a = "World Tournament"; b = "Choosing a tournament"; break;
        case 34: a = "World Tournament"; b = "Choosing a fighter"; break;
        case 35: a = "World Tournament"; b = "Tournament bracket"; break;
        case 38: a = "Duel"; b = "Choosing a battle"; break;
        case 39: a = "Duel"; b = "Choosing fighters"; break;
        case 40: a = "Duel"; b = "Choosing teams"; break;
        case 44: a = "Training"; b = "Choosing a lesson"; break;
        case 45: a = "Training"; b = "Choosing a fighter"; break;
        case 48: a = "Evolution Z"; break;
        case 49: a = "Evolution Z"; b = "Customizing a fighter"; break;
        case 50: a = "Evolution Z"; b = "In the shop"; break;
        case 53: a = "Data Center"; break;
        case 54: a = "Data Center"; b = "Passwords"; break;
        case 55: a = "Data Center"; b = "Custom fighters"; break;
        case 56: a = "Data Center"; b = "Replays"; break;
        case 60: a = "Character Reference"; break;
        case 62: a = "Options"; break;
        case 70: a = "Shenron"; b = "Making a wish"; break;
        default: a = "In the menus"; break;
        }
        if (a != NULL) {
            snprintf(d, sizeof(d), "%s", a);
        }
        if (b != NULL) {
            snprintf(st, sizeof(st), "%s", b);
        }
    }
    if (strcmp(d, gPortDiscordDetails) != 0 || strcmp(st, gPortDiscordState) != 0) {
        gPortDiscordSeq++; // odd: being written
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        memcpy(gPortDiscordDetails, d, sizeof(d));
        memcpy(gPortDiscordState, st, sizeof(st));
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        gPortDiscordSeq++;
    }
}

static bool frame_build(void) {
    discord_status();
    if (sReady && gPortNetWindowClose) { // back from an online session: the window it was started from closes
        gPortNetWindowClose = 0;
        gPortNetMenuRequest = 0;
        set_open(false);
    } else if (sReady && gPortNetMenuRequest) { // Dragon Net Battle was chosen in the game's main menu
        gPortNetMenuRequest = 0;
        net_open();
    }
    if (sReady && !sUpdTold && update_latest() > PORT_RELEASE && SDL_GetTicks() >= sNoticeUntil) {
        int latest = update_latest(); // (told once, and not over another notice)
        sUpdTold = true;
        SDL_snprintf(sNotice, sizeof(sNotice), "Update available: 0.%d.%d. F1 for the download page.", latest / 100, latest % 100);
        sNoticeUntil = SDL_GetTicks() + 10000;
    }
    bool notice = sNotice[0] != '\0' && SDL_GetTicks() < sNoticeUntil;
    if (sNoticeNew) { // (set from the game's side: the time starts when it is first drawn)
        sNoticeNew = 0;
        sNoticeUntil = SDL_GetTicks() + 6000;
        notice = sNotice[0] != '\0';
    }
    // the name of a stage added from outside the disc, over the stage select (a strip of pre-rendered names)
    bool nameOn = gUiNameReady != 0 && gUiNameIdx >= 0 && gUiNameIdx < PortStages_Count();
    bool songOn = gUiSongReady != 0 && gUiSongIdx >= 0 && gUiSongIdx < PortSongs_Count(); // (the same for an added song)
    bool overlay = nameOn || songOn;
    bool meter = sReady && meter_on();
    // the players' names under the gauges of an online fight
    bool names = sReady && Port_NetActive() && BtlChar_FindByObjId(0) != NULL && (Port_NetName(0)[0] != '\0' || Port_NetName(1)[0] != '\0');
    bool bars = sReady && bars_on();
    meter_count();
    sBuilt = false;
    bool touch = sReady && touch_shown();
    if (!sReady || (!sOpen && !sQuit && !notice && !overlay && !meter && !names && !bars && !touch)) {
        return false;
    }
    if (sPadHold && sCapKind != 2) { // a binding was taken: the controller works the window again once nothing is held
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        bool held = false;
        for (int i = 0; i < count; i++) {
            SDL_Gamepad *g = SDL_GetGamepadFromID(ids[i]);
            for (int b = 0; g != NULL && b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
                held = held || SDL_GetGamepadButton(g, (SDL_GamepadButton)b);
            }
        }
        SDL_free(ids);
        sPadHold = held;
    }
    if (sPadHold) { // (the button that becomes the binding would also press the row it is bound in)
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    } else {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    sBuilt = true;
    if (sGL) {
        ImGui_ImplOpenGL3_NewFrame();
    } else {
        ImGui_ImplSDLGPU3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (bars) {
        bars_draw();
    }
    if (sOpen) {
        if (sNet) {
            build_net();
        } else {
            build();
        }
    }
    if (sQuit) {
        build_quit();
    }
    if (meter) {
        meter_draw();
    }
    if (touch) {
        touch_draw();
        if (sTouchEdit) {
            touch_edit_panel();
        }
    }
    if (names && GsDraw_HudShown() && gUiPresentW > 0 && gUiPresentH > 0) { // (with the HUD: not over an intro)
        // Under each side's health bar (the game's 512 x 448 page: the bars end at about y 50). The left place is
        // side 0's, or this player's own side when the HUD's sides are swapped.
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        float sx = (float)gUiPresentW / 512.0f, sy = (float)gUiPresentH / 448.0f;
        float size = 15.0f * sy;
        ImFont *font = ImGui::GetFont();
        for (int place = 0; place < 2; place++) {
            const char *text = Port_NetName(place ^ Port_HudSwap());
            if (text[0] == '\0') {
                continue;
            }
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            // 106 of the HUD's own pixels in from its outer edge: clear of the panel of a team's waiting fighters,
            // which reaches to about 100 (the name sat on it at 62: reported). The HUD is narrowed in a wide
            // picture and sits at the edges or about the middle (gs_draw.c), and the name goes with it.
            float narrow = Port_IsWide() ? 1333.333f / (float)Port_AspectMilli() : 1.0f;
            float in = Port_HudLayout() == 2 ? 256.0f + (106.0f - 256.0f) * narrow : 106.0f * narrow;
            float x = place == 0 ? (float)gUiPresentX + in * sx : (float)gUiPresentX + (512.0f - in) * sx - ts.x;
            float y = (float)gUiPresentY + 56.0f * sy;
            for (int ox = -1; ox <= 1; ox++) {
                for (int oy = -1; oy <= 1; oy++) {
                    if (ox != 0 || oy != 0) {
                        dl->AddText(font, size, ImVec2(x + (float)ox * 1.5f, y + (float)oy * 1.5f), IM_COL32(0, 0, 0, 220), text);
                    }
                }
            }
            dl->AddText(font, size, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        }
    }
    if (notice) { // a line over the picture for a few seconds (the end of an online match)
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("##notice", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted(sNotice);
        }
        ImGui::End();
    }
    if (overlay && gUiPresentW > 0 && gUiPresentH > 0) {
        /* Each name's rectangle is in the game's 512x448 pixels: map it through the picture's rectangle in the
           window (the letterbox gs_gpu.c just blitted into). */
        float sx = (float)gUiPresentW / 512.0f;
        float sy = (float)gUiPresentH / 448.0f;
        // With a menu font every name is drawn here, so the names belong to the game picture, behind the settings
        // and other windows. With "Original" only an added stage or song is, in front of everything as before.
        ImDrawList *dl = MenuText_GetFont() != 0 ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();

        if (nameOn && MenuText_GetFont() != 0) {
            const FontName *fn = font_name(NF_STAGE, PortStages_Name(gUiNameIdx));
            float x = (float)gUiPresentX + (float)gUiNameX * sx, y = (float)gUiPresentY + (float)gUiNameY * sy;
            if (fn != NULL) {
                float kx = (float)gUiNameW * sx / 384.0f, ky = (float)gUiNameH * sy / 48.0f;
                font_draw(dl, fn, x - 64.0f * kx, y + (1.5f - 14.0f) * ky, kx, ky, true);
            } else {
                name_text(dl, PortStages_Name(gUiNameIdx), x, y, (float)gUiNameW * sx, (float)gUiNameH * sy, true);
            }
        } else if (nameOn && gUiNameIdx >= (int)sNameCount) { // no pre-rendered name for this one
            /* In the game's lettering if the disc's names have all its characters, else as plain text. The menu's
               rectangle is the strip image's (384x48, set 64 right and 14 down in the 512x64 row of the disc's own
               name picture); the lettering goes where that row is. The menu's movie draws the row a little below
               the clip's position: kNameDy, measured by drawing a disc name both ways (the same for the songs). */
            static const float kNameDy = 1.5f;
            const FontName *fn = font_name(NF_STAGE, PortStages_Name(gUiNameIdx));
            if (fn != NULL) {
                float kx = (float)gUiNameW * sx / 384.0f, ky = (float)gUiNameH * sy / 48.0f;
                font_draw(dl, fn, (float)gUiPresentX + (float)gUiNameX * sx - 64.0f * kx, (float)gUiPresentY + (float)gUiNameY * sy + (kNameDy - 14.0f) * ky, kx, ky, true);
            } else {
                name_text(dl, PortStages_Name(gUiNameIdx), (float)gUiPresentX + (float)gUiNameX * sx, (float)gUiPresentY + (float)gUiNameY * sy,
                          (float)gUiNameW * sx, (float)gUiNameH * sy, true);
            }
        } else if (nameOn) {
            float x = (float)gUiPresentX + (float)gUiNameX * sx;
            float y = (float)gUiPresentY + (float)gUiNameY * sy;
            float w = (float)gUiNameW * sx;
            float h = (float)gUiNameH * sy;
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sNameTex), ImVec2(x, y), ImVec2(x + w, y + h),
                         ImVec2(0.0f, (float)gUiNameIdx / (float)sNameCount),
                         ImVec2(1.0f, (float)(gUiNameIdx + 1) / (float)sNameCount));
        }
        if (songOn && MenuText_GetFont() != 0) {
            const FontName *fn = font_name(gUiSongLit ? NF_SONG_LIT : NF_SONG, PortSongs_Name(gUiSongIdx));
            float x = (float)gUiPresentX + (float)gUiSongX * sx, y = (float)gUiPresentY + (float)gUiSongY * sy;
            if (fn != NULL) {
                font_draw(dl, fn, x, y, (float)gUiSongW * sx / 512.0f, (float)gUiSongH * sy / 32.0f, false);
            } else {
                name_text(dl, PortSongs_Name(gUiSongIdx), x, y, (float)gUiSongW * sx, (float)gUiSongH * sy, false);
            }
        } else if (songOn && gUiSongIdx >= (int)sSongCount) {
            // (the rectangle is the 512x32 row of the disc's song-name picture itself)
            const FontName *fn = font_name(gUiSongLit ? NF_SONG_LIT : NF_SONG, PortSongs_Name(gUiSongIdx));
            if (fn != NULL) {
                static const float kSongDx = -0.2f, kSongDy = 2.35f;
                float kx = (float)gUiSongW * sx / 512.0f, ky = (float)gUiSongH * sy / 32.0f;
                font_draw(dl, fn, (float)gUiPresentX + (float)gUiSongX * sx + kSongDx * kx, (float)gUiPresentY + (float)gUiSongY * sy + kSongDy * ky, kx, ky, false);
            } else {
                name_text(dl, PortSongs_Name(gUiSongIdx), (float)gUiPresentX + (float)gUiSongX * sx, (float)gUiPresentY + (float)gUiSongY * sy,
                          (float)gUiSongW * sx, (float)gUiSongH * sy, false);
            }
        } else if (songOn) {
            float x = (float)gUiPresentX + (float)gUiSongX * sx;
            float y = (float)gUiPresentY + (float)gUiSongY * sy;
            float w = (float)gUiSongW * sx;
            float h = (float)gUiSongH * sy;
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sSongTex), ImVec2(x, y), ImVec2(x + w, y + h),
                         ImVec2(0.0f, (float)gUiSongIdx / (float)sSongCount),
                         ImVec2(1.0f, (float)(gUiSongIdx + 1) / (float)sSongCount));
        }
    }
    ImGui::Render();
    return true;
}

void Ui_Draw(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    if (!sGL && frame_build()) {
        Ui_DrawAgain(cmd, target);
    }
}

// The OpenGL back end's turn: over the presented picture, just before the buffers are swapped.
void Ui_DrawGL(void) {
    if (sGL && frame_build()) {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}
