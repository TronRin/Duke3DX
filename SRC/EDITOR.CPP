/*
=================================================
BuildX Editor for Duke Nukem 3D
Copyright (C) 2026 Justin Marshall

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
=================================================
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include "BUILD.H"

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

// Build.cpp currently declares these but does not provide game/editor-specific bodies.
extern "C" { long lockclock = 0; }
void _faketimerhandler(void) {}

// -----------------------------------------------------------------------------
// IDs and constants.
// -----------------------------------------------------------------------------
namespace ids
{
    enum : int
    {
        TIMER_RENDER = 1,
        TIMER_2D_REFRESH = 2,
        ID_TAB = 100,
        ID_VIEW2D = 101,
        ID_HOST3D = 102,
        ID_TEXTUREBAR = 103,
        ID_INSPECTOR = 104,

        ID_INSP_TILE_EDIT = 1100,
        ID_INSP_SHADE_EDIT,
        ID_INSP_PAL_EDIT,
        ID_INSP_VIS_EDIT,
        ID_INSP_LOTAG_EDIT,
        ID_INSP_HITAG_EDIT,
        ID_INSP_USE_TILE,
        ID_INSP_APPLY,
        ID_INSP_REFRESH,

        M_FILE_NEW = 40001,
        M_FILE_OPEN,
        M_FILE_OPEN_GRP,
        M_FILE_SAVE,
        M_FILE_SAVE_AS,
        M_FILE_EXIT,

        M_VIEW_2D,
        M_VIEW_3D,
        M_VIEW_ZOOM_IN,
        M_VIEW_ZOOM_OUT,
        M_VIEW_GRID,
        M_VIEW_GRID_LOCK,
        M_VIEW_SHOW_TAGS,

        M_EDIT_BEGIN_DRAW,
        M_EDIT_CLOSE_DRAW,
        M_EDIT_CANCEL_DRAW,
        M_EDIT_INSERT_POINT,
        M_EDIT_INSERT_SPRITE,
        M_EDIT_DELETE,
        M_EDIT_COPY_PROPS,
        M_EDIT_PASTE_PROPS,
        M_EDIT_APPLY_TEXTURE,
        M_EDIT_PROPERTIES,

        M_TOOLS_RELINK,
        M_TOOLS_SET_START,
        M_TOOLS_TOGGLE_BLOCKING,
        M_TOOLS_TOGGLE_ONEWAY,
        M_TOOLS_TOGGLE_MASKED,
        M_TOOLS_TOGGLE_TRANSLUCENT,
        M_TOOLS_TOGGLE_PARALLAX,
        M_TOOLS_SHADE_UP,
        M_TOOLS_SHADE_DOWN,
        M_TOOLS_Z_UP,
        M_TOOLS_Z_DOWN,
        M_TEXTURE_GOTO,
        M_HELP_KEYS,
    };
}

static constexpr int TEXTURE_BAR_HEIGHT = 138;
static constexpr int TAB_MARGIN = 4;
static constexpr int TILE_CELL = 72;
static constexpr int TILE_PREVIEW = 56;
static constexpr int BUILD_ANG_MAX = 2048;

// Original searchstat meanings retained where useful.
enum SearchStat : int
{
    SS_NONE = -1,
    SS_WALL = 0,
    SS_CEILING = 1,
    SS_FLOOR = 2,
    SS_SPRITE = 3,
    SS_MASKED_WALL = 4,
};

struct Selection
{
    int stat = SS_NONE;
    short sector = -1;
    short wall = -1;       // Also stores sprite index for SS_SPRITE, matching old editor searchwall.
    bool valid() const { return stat >= 0; }
    void clear() { stat = SS_NONE; sector = -1; wall = -1; }
};

struct CopiedProps
{
    bool valid = false;
    int stat = SS_NONE;
    short picnum = 0;
    short cstat = 0;
    short lotag = 0, hitag = 0, extra = -1;
    signed char shade = 0;
    char pal = 0;
    char visibility = 0;
    unsigned char xrepeat = 8, yrepeat = 8;
    char xpanning = 0, ypanning = 0;
};

struct CameraState
{
    long x = 32768;
    long y = 32768;
    long z = 0;
    long horiz = 100;
    short ang = 1536;
    short sect = -1;
};

struct AppState
{
    HINSTANCE inst = nullptr;
    HWND main = nullptr;
    HWND tab = nullptr;
    HWND view2d = nullptr;
    HWND host3d = nullptr;
    HWND textureBar = nullptr;
    HWND inspector = nullptr;
    HWND inspTitle = nullptr;
    HWND inspSurfaceTitle = nullptr;
    HWND inspTileEdit = nullptr;
    HWND inspShadeEdit = nullptr;
    HWND inspPalEdit = nullptr;
    HWND inspSectorTitle = nullptr;
    HWND inspVisEdit = nullptr;
    HWND inspLotagEdit = nullptr;
    HWND inspHitagEdit = nullptr;
    HWND inspUseTileButton = nullptr;
    HWND inspApplyButton = nullptr;
    HWND inspRefreshButton = nullptr;
    HWND engine = nullptr;
    WNDPROC oldEngineProc = nullptr;

    bool engineCreated = false;
    bool running = true;
    bool dirty = false;
    bool showTags = true;
    bool gridLock = true;
    int grid = 3;
    int activeTab = 0; // 0 = 2D, 1 = 3D.

    std::string boardPath = "newboard.map";
    bool boardFromGrp = false;
    std::string boardGrpPath;
    std::string boardGrpEntry;
    std::string status = "Ready.";

    CameraState cam;
    long startx = 32768, starty = 32768, startz = 0;
    short startang = 1536, startsect = -1;

    long viewX = 32768;
    long viewY = 32768;
    long zoom = 768;

    Selection sel;
    CopiedProps clip;

    short selectedTile = 0;
    int tileScroll = 0;
    std::vector<short> tileList;

    bool keys[256] = {};
    bool panning2D = false;
    bool draggingPoint = false;
    bool draggingSprite = false;
    bool orbit3D = false;
    bool dragPointMoved = false;
    bool dragSpriteMoved = false;
    bool dragDirtySet = false;
    bool pending2DInvalidate = false;
    bool inspectorUpdating = false;
    DWORD last2DInvalidateTick = 0;
    POINT lastMouse = {};
    short dragPointWall = -1;
    short dragSprite = -1;
    long dragOldX = 0, dragOldY = 0;

    bool drawingSector = false;
    std::vector<POINT> drawPts; // map coordinates in x/y.
    POINT lastMapMouse = { 0, 0 };

    LARGE_INTEGER freq = {};
    LARGE_INTEGER lastTick = {};
};

static AppState g;

// -----------------------------------------------------------------------------
// Small math helpers.
// -----------------------------------------------------------------------------
static long i64mulscale(long a, long b, int shift)
{
    return static_cast<long>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> shift);
}

static long mulscale14_local(long a, long b) { return i64mulscale(a, b, 14); }

static long divscale14_local(long a, long b)
{
    if (b == 0) return 0;
    return static_cast<long>((static_cast<std::int64_t>(a) << 14) / b);
}

static long scale_local(long a, long b, long c)
{
    if (c == 0) return 0;
    return static_cast<long>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) / c);
}

static long clamp_long(long v, long lo, long hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static double dist2(double ax, double ay, double bx, double by)
{
    const double dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

static double pointSegmentDistance2(double px, double py, double ax, double ay, double bx, double by, double* outT = nullptr)
{
    const double vx = bx - ax;
    const double vy = by - ay;
    const double wx = px - ax;
    const double wy = py - ay;
    const double len2 = vx * vx + vy * vy;
    double t = 0.0;
    if (len2 > 0.0) t = (wx * vx + wy * vy) / len2;
    t = max(0.0, min(1.0, t));
    if (outT) *outT = t;
    const double cx = ax + vx * t;
    const double cy = ay + vy * t;
    return dist2(px, py, cx, cy);
}

static int gridSize()
{
    if (g.grid <= 0) return 1;
    const int shift = max(1, 11 - g.grid);
    return 1 << shift;
}

static void snapPoint(long& x, long& y)
{
    if (!g.gridLock || g.grid <= 0) return;
    const int gs = gridSize();
    x = ((x + (gs >> 1)) / gs) * gs;
    y = ((y + (gs >> 1)) / gs) * gs;
}

static void mapToScreen(HWND hwnd, long mx, long my, int& sx, int& sy)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const double z = static_cast<double>(g.zoom) / 16384.0;
    sx = static_cast<int>((rc.right - rc.left) * 0.5 + (static_cast<double>(mx - g.viewX) * z));
    sy = static_cast<int>((rc.bottom - rc.top) * 0.5 + (static_cast<double>(my - g.viewY) * z));
}

static POINT screenToMap(HWND hwnd, int sx, int sy, bool snap)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const double z = static_cast<double>(g.zoom) / 16384.0;
    POINT p{};
    if (z <= 0.00001)
    {
        p.x = static_cast<LONG>(g.viewX);
        p.y = static_cast<LONG>(g.viewY);
    }
    else
    {
        p.x = static_cast<LONG>(std::llround(g.viewX + (sx - (rc.right - rc.left) * 0.5) / z));
        p.y = static_cast<LONG>(std::llround(g.viewY + (sy - (rc.bottom - rc.top) * 0.5) / z));
    }
    if (snap)
    {
        long x = p.x, y = p.y;
        snapPoint(x, y);
        p.x = x; p.y = y;
    }
    return p;
}

static long screenPixelsToMap(HWND hwnd, int pixels)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const double z = static_cast<double>(g.zoom) / 16384.0;
    if (z <= 0.00001) return 1024;
    return static_cast<long>(std::ceil(static_cast<double>(pixels) / z));
}

// -----------------------------------------------------------------------------
// Forward declarations.
// -----------------------------------------------------------------------------
static void updateInspector();
static void applyInspectorEdits(bool useCurrentTile);
static void syncEngineSearchGlobals(const Selection& s);

// -----------------------------------------------------------------------------
// UI utilities.
// -----------------------------------------------------------------------------
static void setStatus(const char* fmt, ...)
{
    char buf[512];
    va_list va;
    va_start(va, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, va);
    va_end(va);
    g.status = buf;
    if (g.textureBar) InvalidateRect(g.textureBar, nullptr, FALSE);
}

static void updateTitle()
{
    std::string title = "BuildX - ";
    title += g.boardPath.empty() ? "newboard.map" : g.boardPath;
    if (g.dirty) title += " *";
    if (g.main) SetWindowTextA(g.main, title.c_str());
}

static void setDirty(bool dirty = true)
{
    g.dirty = dirty;
    updateTitle();
}

static void markDirtyDuringDrag()
{
    if (g.dragDirtySet) return;
    if (!g.dirty) setDirty(true);
    else g.dirty = true;
    g.dragDirtySet = true;
}

static void request2DRefresh(HWND hwnd, bool immediate = false)
{
    if (!hwnd) return;

    const DWORD now = GetTickCount();
    const DWORD minMs = 15; // Cap high-frequency mouse redraws to about one screen refresh.
    const DWORD elapsed = now - g.last2DInvalidateTick;

    if (immediate || g.last2DInvalidateTick == 0 || elapsed >= minMs)
    {
        KillTimer(hwnd, ids::TIMER_2D_REFRESH);
        g.pending2DInvalidate = false;
        g.last2DInvalidateTick = now;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (!g.pending2DInvalidate)
    {
        g.pending2DInvalidate = true;
        const UINT delay = static_cast<UINT>(minMs - elapsed);
        SetTimer(hwnd, ids::TIMER_2D_REFRESH, delay ? delay : 1, nullptr);
    }
}

static bool confirmDiscardChanges()
{
    if (!g.dirty) return true;

    const int r = MessageBoxA(g.main, "Save changes to the current map?", "Unsaved map", MB_ICONQUESTION | MB_YESNOCANCEL);
    if (r == IDCANCEL) return false;
    if (r == IDNO) return true; // Discard changes and continue with New/Open.

    if (r == IDYES)
    {
        SendMessage(g.main, WM_COMMAND, ids::M_FILE_SAVE, 0);
        return !g.dirty; // Save succeeded only if the dirty flag was cleared.
    }

    return false;
}

static void refreshAllViews()
{
    if (g.view2d) InvalidateRect(g.view2d, nullptr, FALSE);
    if (g.textureBar) InvalidateRect(g.textureBar, nullptr, FALSE);
    if (g.inspector) InvalidateRect(g.inspector, nullptr, FALSE);
}

static HMENU makeMainMenu()
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ids::M_FILE_NEW, "&New\tCtrl+N");
    AppendMenuA(file, MF_STRING, ids::M_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenuA(file, MF_STRING, ids::M_FILE_OPEN_GRP, "Open From &GRP...");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, ids::M_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenuA(file, MF_STRING, ids::M_FILE_SAVE_AS, "Save &As...");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, ids::M_FILE_EXIT, "E&xit");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), "&File");

    HMENU view = CreatePopupMenu();
    AppendMenuA(view, MF_STRING, ids::M_VIEW_2D, "Show &2D Editor\tF2");
    AppendMenuA(view, MF_STRING, ids::M_VIEW_3D, "Show &3D Editor\tF3");
    AppendMenuA(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(view, MF_STRING, ids::M_VIEW_ZOOM_IN, "Zoom &In");
    AppendMenuA(view, MF_STRING, ids::M_VIEW_ZOOM_OUT, "Zoom &Out");
    AppendMenuA(view, MF_STRING, ids::M_VIEW_GRID, "Cycle &Grid\tG");
    AppendMenuA(view, MF_STRING, ids::M_VIEW_GRID_LOCK, "&Grid Lock\tL");
    AppendMenuA(view, MF_STRING, ids::M_VIEW_SHOW_TAGS, "Show &Tags\tCtrl+T");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), "&View");

    HMENU edit = CreatePopupMenu();
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_BEGIN_DRAW, "Begin/Add Sector Point\tSpace");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_CLOSE_DRAW, "Close Drawn Sector\tEnter");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_CANCEL_DRAW, "Cancel Sector Drawing\tEsc");
    AppendMenuA(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_INSERT_POINT, "Insert Point\tIns");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_INSERT_SPRITE, "Insert Sprite\tS");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_DELETE, "Delete Selection\tDel");
    AppendMenuA(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_COPY_PROPS, "Copy Properties\tTab");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_PASTE_PROPS, "Paste Properties\tEnter");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_APPLY_TEXTURE, "Apply Selected Texture\tV");
    AppendMenuA(edit, MF_STRING, ids::M_EDIT_PROPERTIES, "Properties...\tAlt+Enter");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), "&Edit");

    HMENU tools = CreatePopupMenu();
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_RELINK, "Repair Red-Wall Links");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_SET_START, "Set Player Start Here\tScroll Lock");
    AppendMenuA(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_TOGGLE_BLOCKING, "Toggle Blocking\tB");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_TOGGLE_ONEWAY, "Toggle One-Way\t1");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_TOGGLE_MASKED, "Toggle Masked Wall\tM");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_TOGGLE_TRANSLUCENT, "Toggle Translucency\tT");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_TOGGLE_PARALLAX, "Toggle Parallax\tP");
    AppendMenuA(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_SHADE_UP, "Shade Darker\t-");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_SHADE_DOWN, "Shade Brighter\t+");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_Z_UP, "Raise Z\tPgUp");
    AppendMenuA(tools, MF_STRING, ids::M_TOOLS_Z_DOWN, "Lower Z\tPgDn");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), "&Tools");

    HMENU texture = CreatePopupMenu();
    AppendMenuA(texture, MF_STRING, ids::M_TEXTURE_GOTO, "Goto Tile...");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(texture), "&Texture");

    HMENU help = CreatePopupMenu();
    AppendMenuA(help, MF_STRING, ids::M_HELP_KEYS, "Keyboard Help");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), "&Help");
    return menu;
}

// -----------------------------------------------------------------------------
// Tiny modal input box, no resource file required.
// -----------------------------------------------------------------------------
struct InputBoxState
{
    std::string title;
    std::string label;
    std::string value;
    bool done = false;
    bool ok = false;
    HWND edit = nullptr;
};

static LRESULT CALLBACK InputBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    InputBoxState* st = reinterpret_cast<InputBoxState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_CREATE:
    {
        st = reinterpret_cast<InputBoxState*>(reinterpret_cast<CREATESTRUCTA*>(lp)->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        CreateWindowA("STATIC", st->label.c_str(), WS_CHILD | WS_VISIBLE, 12, 12, 340, 18, hwnd, nullptr, g.inst, nullptr);
        st->edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", st->value.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            12, 38, 340, 24, hwnd, reinterpret_cast<HMENU>(1), g.inst, nullptr);
        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 196, 74, 72, 26, hwnd, reinterpret_cast<HMENU>(IDOK), g.inst, nullptr);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 280, 74, 72, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), g.inst, nullptr);
        SendMessageA(st->edit, EM_SETSEL, 0, -1);
        SetFocus(st->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK)
        {
            char buf[512] = {};
            GetWindowTextA(st->edit, buf, sizeof(buf));
            st->value = buf;
            st->ok = true;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL)
        {
            st->ok = false;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (st) { st->ok = false; st->done = true; }
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool promptString(const char* title, const char* label, std::string& value)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSA wc{};
        wc.lpfnWndProc = InputBoxProc;
        wc.hInstance = g.inst;
        wc.lpszClassName = "BuildEditorInputBox";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        registered = true;
    }

    InputBoxState st;
    st.title = title ? title : "Input";
    st.label = label ? label : "Value:";
    st.value = value;

    RECT pr{};
    GetWindowRect(g.main, &pr);
    const int w = 374, h = 138;
    const int x = pr.left + ((pr.right - pr.left) - w) / 2;
    const int y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "BuildEditorInputBox", st.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, w, h, g.main, nullptr, g.inst, &st);
    if (!hwnd) return false;

    EnableWindow(g.main, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    MSG msg{};
    while (!st.done && GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageA(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(g.main, TRUE);
    SetActiveWindow(g.main);

    if (st.ok) value = st.value;
    return st.ok;
}

static bool promptLong(const char* title, const char* label, long& value, long minv, long maxv)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%ld", value);
    std::string s = buf;
    if (!promptString(title, label, s)) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 0);
    if (!end || *end != 0)
    {
        MessageBoxA(g.main, "That was not a valid number.", "Input", MB_ICONERROR);
        return false;
    }
    value = clamp_long(v, minv, maxv);
    return true;
}

struct ChoiceBoxState
{
    std::string title;
    std::string label;
    std::vector<std::string> items;
    int selected = 0;
    bool done = false;
    bool ok = false;
    HWND list = nullptr;
};

static void finishChoiceBox(HWND hwnd, ChoiceBoxState* st, bool ok)
{
    if (!st) return;
    st->ok = ok;
    if (ok && st->list)
    {
        const int sel = static_cast<int>(SendMessageA(st->list, LB_GETCURSEL, 0, 0));
        if (sel != LB_ERR) st->selected = sel;
    }
    st->done = true;
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK ChoiceBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ChoiceBoxState* st = reinterpret_cast<ChoiceBoxState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_CREATE:
    {
        st = reinterpret_cast<ChoiceBoxState*>(reinterpret_cast<CREATESTRUCTA*>(lp)->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        CreateWindowA("STATIC", st->label.c_str(), WS_CHILD | WS_VISIBLE, 12, 12, 396, 18, hwnd, nullptr, g.inst, nullptr);
        st->list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            12, 36, 396, 232, hwnd, reinterpret_cast<HMENU>(1001), g.inst, nullptr);
        for (const std::string& item : st->items)
            SendMessageA(st->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SendMessageA(st->list, LB_SETCURSEL, clamp_int(st->selected, 0, static_cast<int>(st->items.size()) - 1), 0);
        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 252, 280, 72, 26, hwnd, reinterpret_cast<HMENU>(IDOK), g.inst, nullptr);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 336, 280, 72, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), g.inst, nullptr);
        SetFocus(st->list);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || (LOWORD(wp) == 1001 && HIWORD(wp) == LBN_DBLCLK))
        {
            finishChoiceBox(hwnd, st, true);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL)
        {
            finishChoiceBox(hwnd, st, false);
            return 0;
        }
        break;
    case WM_CLOSE:
        finishChoiceBox(hwnd, st, false);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool chooseStringIndex(const char* title, const char* label, const std::vector<std::string>& items, int& selected)
{
    if (items.empty()) return false;

    static bool registered = false;
    if (!registered)
    {
        WNDCLASSA wc{};
        wc.lpfnWndProc = ChoiceBoxProc;
        wc.hInstance = g.inst;
        wc.lpszClassName = "BuildEditorChoiceBox";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        registered = true;
    }

    ChoiceBoxState st;
    st.title = title ? title : "Choose";
    st.label = label ? label : "Choose an item:";
    st.items = items;
    st.selected = selected;

    RECT pr{};
    GetWindowRect(g.main, &pr);
    const int w = 432, h = 350;
    const int x = pr.left + ((pr.right - pr.left) - w) / 2;
    const int y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "BuildEditorChoiceBox", st.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, w, h, g.main, nullptr, g.inst, &st);
    if (!hwnd) return false;

    EnableWindow(g.main, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    MSG msg{};
    while (!st.done && GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageA(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(g.main, TRUE);
    SetActiveWindow(g.main);

    if (st.ok) selected = st.selected;
    return st.ok;
}

// -----------------------------------------------------------------------------
// BUILD map editing helpers.
// -----------------------------------------------------------------------------
static short ownerSectorOfWall(short wallnum)
{
    if (wallnum < 0 || wallnum >= numwalls) return -1;
    for (short s = 0; s < numsectors; ++s)
    {
        const int start = sector[s].wallptr;
        const int end = start + sector[s].wallnum;
        if (wallnum >= start && wallnum < end) return s;
    }
    return -1;
}

static long wallLength(short wi)
{
    if (wi < 0 || wi >= numwalls) return 0;
    const walltype& w = wall[wi];
    if (w.point2 < 0 || w.point2 >= numwalls) return 0;
    const long dx = wall[w.point2].x - w.x;
    const long dy = wall[w.point2].y - w.y;
    return ksqrt(dx * dx + dy * dy);
}

static void fixWallRepeat(short wi)
{
    if (wi < 0 || wi >= numwalls) return;
    long len = wallLength(wi);
    wall[wi].xrepeat = static_cast<char>(clamp_long((len + 4) >> 3, 1, 255));
    if (wall[wi].yrepeat == 0) wall[wi].yrepeat = 8;
}

struct EdgeKey
{
    long x1, y1, x2, y2;

    EdgeKey() : x1(0), y1(0), x2(0), y2(0) {}
    EdgeKey(long ax, long ay, long bx, long by) : x1(ax), y1(ay), x2(bx), y2(by) {}

    bool operator==(const EdgeKey& other) const
    {
        return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
    }
};

struct EdgeKeyHash
{
    std::size_t operator()(const EdgeKey& k) const
    {
        std::uint64_t h = 1469598103934665603ull;
        hashMix(h, k.x1);
        hashMix(h, k.y1);
        hashMix(h, k.x2);
        hashMix(h, k.y2);
        return static_cast<std::size_t>(h);
    }

    static void hashMix(std::uint64_t& h, long v)
    {
        const std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
        h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h *= 1099511628211ull;
    }
};

static void buildWallOwnerList(std::vector<short>& owners)
{
    owners.assign(numwalls > 0 ? numwalls : 0, static_cast<short>(-1));
    for (short s = 0; s < numsectors; ++s)
    {
        const int start = sector[s].wallptr;
        const int end = start + sector[s].wallnum;
        if (start < 0) continue;
        for (int i = start; i < end && i < numwalls; ++i)
        {
            if (i >= 0) owners[static_cast<size_t>(i)] = s;
        }
    }
}

static void rebuildSectorLinks()
{
    for (short i = 0; i < numwalls; ++i)
    {
        wall[i].nextwall = -1;
        wall[i].nextsector = -1;
    }

    std::vector<short> owner;
    buildWallOwnerList(owner);

    std::unordered_multimap<EdgeKey, short, EdgeKeyHash> openEdges;
    openEdges.reserve(static_cast<size_t>(max(1, numwalls * 2)));

    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].point2 < 0 || wall[i].point2 >= numwalls) continue;
        if (i >= static_cast<short>(owner.size()) || owner[i] < 0) continue;

        const walltype& a0 = wall[i];
        const walltype& a1 = wall[a0.point2];
        const EdgeKey key(a0.x, a0.y, a1.x, a1.y);
        const EdgeKey reverse(a1.x, a1.y, a0.x, a0.y);

        auto range = openEdges.equal_range(reverse);
        auto match = range.second;
        for (auto it = range.first; it != range.second; ++it)
        {
            const short j = it->second;
            if (j >= 0 && j < numwalls && j < static_cast<short>(owner.size()) && owner[j] >= 0 && owner[j] != owner[i])
            {
                match = it;
                break;
            }
        }

        if (match != range.second)
        {
            const short j = match->second;
            wall[i].nextwall = j;
            wall[i].nextsector = owner[j];
            wall[j].nextwall = i;
            wall[j].nextsector = owner[i];
            openEdges.erase(match);
        }
        else
        {
            openEdges.emplace(key, i);
        }
    }
    setStatus("Red-wall links rebuilt.");
}

static void refreshTileList()
{
    g.tileList.clear();
    for (short i = 0; i < MAXTILES; ++i)
        if (tilesizx[i] > 0 && tilesizy[i] > 0)
            g.tileList.push_back(i);
    if (g.tileList.empty()) g.tileList.push_back(0);
    g.tileScroll = clamp_int(g.tileScroll, 0, max(0, static_cast<int>(g.tileList.size()) - 1));
}

static int tileListIndex(short tile)
{
    for (int i = 0; i < static_cast<int>(g.tileList.size()); ++i)
        if (g.tileList[i] == tile) return i;
    return 0;
}

static void centerTextureBarOnSelected()
{
    if (g.tileList.empty()) return;
    g.tileScroll = max(0, tileListIndex(g.selectedTile) - 2);
    if (g.textureBar) InvalidateRect(g.textureBar, nullptr, FALSE);
}

static void initNewMap()
{
    std::memset(sector, 0, sizeof(sector));
    std::memset(wall, 0, sizeof(wall));
    std::memset(sprite, 0, sizeof(sprite));
    initspritelists();

    numsectors = 1;
    numwalls = 4;

    sectortype& s = sector[0];
    s.wallptr = 0;
    s.wallnum = 4;
    s.ceilingz = -32768;
    s.floorz = 32768;
    s.ceilingpicnum = 0;
    s.floorpicnum = 0;
    s.ceilingheinum = 0;
    s.floorheinum = 0;
    s.ceilingshade = 0;
    s.floorshade = 0;
    s.visibility = 0;
    s.extra = -1;

    const long x0 = 24576, y0 = 24576, x1 = 40960, y1 = 40960;
    const long xs[4] = { x0, x1, x1, x0 };
    const long ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; ++i)
    {
        wall[i].x = xs[i];
        wall[i].y = ys[i];
        wall[i].point2 = static_cast<short>((i + 1) & 3);
        wall[i].nextwall = -1;
        wall[i].nextsector = -1;
        wall[i].picnum = 0;
        wall[i].overpicnum = 0;
        wall[i].shade = 0;
        wall[i].pal = 0;
        wall[i].xrepeat = 8;
        wall[i].yrepeat = 8;
        wall[i].extra = -1;
        fixWallRepeat(static_cast<short>(i));
    }

    g.cam.x = 32768;
    g.cam.y = 32768;
    g.cam.z = 0;
    g.cam.ang = 1536;
    g.cam.horiz = 100;
    g.cam.sect = 0;
    g.viewX = g.cam.x;
    g.viewY = g.cam.y;
    g.startx = g.cam.x;
    g.starty = g.cam.y;
    g.startz = g.cam.z;
    g.startang = g.cam.ang;
    g.startsect = g.cam.sect;
    g.sel.clear();
    syncEngineSearchGlobals(g.sel);
    g.drawingSector = false;
    g.drawPts.clear();
    rebuildSectorLinks();
}

static bool loadMapFile(const char* path)
{
    if (!path || !path[0]) return false;
    char filename[MAX_PATH];
    std::strncpy(filename, path, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = 0;

    long px = 32768, py = 32768, pz = 0;
    short a = 1536, s = -1;
    if (loadboard(filename, &px, &py, &pz, &a, &s) < 0)
    {
        MessageBoxA(g.main, "Could not load that map. A new square map was created instead.", "Open map", MB_ICONWARNING);
        initNewMap();
        return false;
    }

    g.boardPath = path;
    g.boardFromGrp = false;
    g.boardGrpPath.clear();
    g.boardGrpEntry.clear();
    g.cam.x = px; g.cam.y = py; g.cam.z = pz; g.cam.ang = a; g.cam.sect = s; g.cam.horiz = 100;
    updatesector(g.cam.x, g.cam.y, &g.cam.sect);
    g.viewX = g.cam.x;
    g.viewY = g.cam.y;
    g.startx = px; g.starty = py; g.startz = pz; g.startang = a; g.startsect = g.cam.sect;
    g.sel.clear();
    syncEngineSearchGlobals(g.sel);
    rebuildSectorLinks();
    setDirty(false);
    setStatus("Loaded %s", path);
    refreshAllViews();
    return true;
}

static bool saveMapFile(const char* path)
{
    if (!path || !path[0]) return false;
    updatesector(g.startx, g.starty, &g.startsect);
    char filename[MAX_PATH];
    std::strncpy(filename, path, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = 0;
    if (saveboard(filename, &g.startx, &g.starty, &g.startz, &g.startang, &g.startsect) < 0)
    {
        MessageBoxA(g.main, "Could not save the map.", "Save map", MB_ICONERROR);
        return false;
    }
    g.boardPath = path;
    g.boardFromGrp = false;
    g.boardGrpPath.clear();
    g.boardGrpEntry.clear();
    setDirty(false);
    setStatus("Saved %s", path);
    return true;
}

struct GrpMapEntry
{
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

static std::uint32_t readLe32(const unsigned char b[4])
{
    return static_cast<std::uint32_t>(b[0]) |
        (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) |
        (static_cast<std::uint32_t>(b[3]) << 24);
}

static bool readExact(FILE* f, void* dst, size_t bytes)
{
    return std::fread(dst, 1, bytes, f) == bytes;
}

static bool hasMapExtension(const std::string& name)
{
    const size_t n = name.size();
    if (n < 4 || name[n - 4] != '.') return false;
    const char a = name[n - 3], b = name[n - 2], c = name[n - 1];
    return (a == 'm' || a == 'M') && (b == 'a' || b == 'A') && (c == 'p' || c == 'P');
}

static std::string upperAscii(std::string s)
{
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    return s;
}

static bool readGrpMapEntries(const char* grpPath, std::vector<GrpMapEntry>& maps)
{
    maps.clear();
    if (!grpPath || !grpPath[0]) return false;

    FILE* f = nullptr;
    if (fopen_s(&f, grpPath, "rb") != 0 || !f) return false;

    char signature[12] = {};
    unsigned char countBytes[4] = {};
    if (!readExact(f, signature, sizeof(signature)) || !readExact(f, countBytes, sizeof(countBytes)) ||
        std::memcmp(signature, "KenSilverman", 12) != 0)
    {
        std::fclose(f);
        return false;
    }

    const std::uint32_t count = readLe32(countBytes);
    std::uint64_t dataOffset = 16ull + static_cast<std::uint64_t>(count) * 16ull;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        char rawName[12] = {};
        unsigned char sizeBytes[4] = {};
        if (!readExact(f, rawName, sizeof(rawName)) || !readExact(f, sizeBytes, sizeof(sizeBytes)))
        {
            std::fclose(f);
            return false;
        }

        size_t nameLen = 0;
        while (nameLen < sizeof(rawName) && rawName[nameLen] != 0) ++nameLen;
        std::string name(rawName, rawName + nameLen);
        const std::uint32_t size = readLe32(sizeBytes);

        if (hasMapExtension(name) && dataOffset <= 0xffffffffull)
        {
            GrpMapEntry e;
            e.name = name;
            e.offset = static_cast<std::uint32_t>(dataOffset);
            e.size = size;
            maps.push_back(e);
        }
        dataOffset += size;
    }

    std::fclose(f);
    std::sort(maps.begin(), maps.end(), [](const GrpMapEntry& a, const GrpMapEntry& b)
        {
            return upperAscii(a.name) < upperAscii(b.name);
        });
    return true;
}

static bool extractGrpEntryToTemp(const char* grpPath, const GrpMapEntry& entry, std::string& tempPath)
{
    tempPath.clear();

    char tempDir[MAX_PATH] = {};
    char tempFile[MAX_PATH] = {};
    if (!GetTempPathA(sizeof(tempDir), tempDir) || !GetTempFileNameA(tempDir, "BXM", 0, tempFile))
        return false;

    HANDLE in = CreateFileA(grpPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE)
    {
        DeleteFileA(tempFile);
        return false;
    }

    HANDLE out = CreateFileA(tempFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (out == INVALID_HANDLE_VALUE)
    {
        CloseHandle(in);
        DeleteFileA(tempFile);
        return false;
    }

    LARGE_INTEGER pos{};
    pos.QuadPart = entry.offset;
    bool ok = SetFilePointerEx(in, pos, nullptr, FILE_BEGIN) != 0;
    std::vector<unsigned char> buffer(64 * 1024);
    std::uint32_t remaining = entry.size;

    while (ok && remaining > 0)
    {
        const DWORD want = static_cast<DWORD>(remaining > buffer.size() ? buffer.size() : remaining);
        DWORD got = 0;
        if (!ReadFile(in, buffer.data(), want, &got, nullptr) || got == 0)
        {
            ok = false;
            break;
        }

        DWORD wrote = 0;
        if (!WriteFile(out, buffer.data(), got, &wrote, nullptr) || wrote != got)
        {
            ok = false;
            break;
        }

        remaining -= got;
    }

    CloseHandle(out);
    CloseHandle(in);

    if (!ok)
    {
        DeleteFileA(tempFile);
        return false;
    }

    tempPath = tempFile;
    return true;
}

static const char* pathBaseName(const char* path)
{
    if (!path) return "";
    const char* slash1 = std::strrchr(path, '\\');
    const char* slash2 = std::strrchr(path, '/');
    const char* slash = nullptr;
    if (slash1 && slash2) slash = (slash1 > slash2) ? slash1 : slash2;
    else slash = slash1 ? slash1 : slash2;
    return slash ? slash + 1 : path;
}

static bool promptOpenMapFromGrp()
{
    char grpFile[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = "BUILD group files (*.grp)\0*.grp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = grpFile;
    ofn.nMaxFile = sizeof(grpFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "grp";
    if (!GetOpenFileNameA(&ofn)) return false;

    std::vector<GrpMapEntry> maps;
    if (!readGrpMapEntries(grpFile, maps))
    {
        MessageBoxA(g.main, "Could not read that GRP file. Make sure it is a Ken Silverman BUILD .grp file.", "Open from GRP", MB_ICONERROR);
        return false;
    }
    if (maps.empty())
    {
        MessageBoxA(g.main, "That GRP file does not contain any .map entries.", "Open from GRP", MB_ICONINFORMATION);
        return false;
    }

    int choice = 0;
    if (maps.size() > 1)
    {
        std::vector<std::string> names;
        names.reserve(maps.size());
        for (const GrpMapEntry& e : maps) names.push_back(e.name);
        if (!chooseStringIndex("Open map from GRP", "Select a map inside the GRP:", names, choice))
            return false;
    }

    std::string tempMap;
    if (!extractGrpEntryToTemp(grpFile, maps[choice], tempMap))
    {
        MessageBoxA(g.main, "Could not extract that map from the GRP file.", "Open from GRP", MB_ICONERROR);
        return false;
    }

    const bool ok = loadMapFile(tempMap.c_str());
    DeleteFileA(tempMap.c_str());
    if (!ok) return false;

    g.boardFromGrp = true;
    g.boardGrpPath = grpFile;
    g.boardGrpEntry = maps[choice].name;
    g.boardPath = std::string(maps[choice].name) + " (" + pathBaseName(grpFile) + ")";
    setDirty(false);
    updateTitle();
    setStatus("Loaded %s from %s", maps[choice].name.c_str(), pathBaseName(grpFile));
    refreshAllViews();
    return true;
}

static bool promptOpenMap()
{
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = "BUILD maps (*.map)\0*.map\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "map";
    if (!GetOpenFileNameA(&ofn)) return false;
    return loadMapFile(file);
}

static bool promptSaveMap()
{
    char file[MAX_PATH] = "";
    if (!g.boardFromGrp)
        std::strncpy(file, g.boardPath.c_str(), sizeof(file) - 1);
    else if (!g.boardGrpEntry.empty())
        std::strncpy(file, g.boardGrpEntry.c_str(), sizeof(file) - 1);
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = "BUILD maps (*.map)\0*.map\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "map";
    if (!GetSaveFileNameA(&ofn)) return false;
    return saveMapFile(file);
}

static bool insertPointOnWall(short wi, long x, long y)
{
    if (wi < 0 || wi >= numwalls || numwalls >= MAXWALLS - 1) return false;
    const short sect = ownerSectorOfWall(wi);
    if (sect < 0) return false;

    const short insertAt = static_cast<short>(wi + 1);
    const short oldPoint2 = wall[wi].point2;

    for (int i = numwalls; i > insertAt; --i)
        wall[i] = wall[i - 1];

    for (short s = 0; s < numsectors; ++s)
    {
        if (sector[s].wallptr >= insertAt) sector[s].wallptr++;
    }
    sector[sect].wallnum++;

    for (short i = 0; i <= numwalls; ++i)
    {
        if (i == insertAt) continue;
        if (wall[i].point2 >= insertAt) wall[i].point2++;
        if (wall[i].nextwall >= insertAt) wall[i].nextwall++;
    }

    wall[insertAt] = wall[wi];
    wall[insertAt].x = x;
    wall[insertAt].y = y;
    wall[insertAt].point2 = static_cast<short>(oldPoint2 >= insertAt ? oldPoint2 + 1 : oldPoint2);
    wall[insertAt].nextwall = -1;
    wall[insertAt].nextsector = -1;
    wall[wi].point2 = insertAt;
    ++numwalls;

    fixWallRepeat(wi);
    fixWallRepeat(insertAt);
    rebuildSectorLinks();
    setDirty();
    refreshAllViews();
    return true;
}

static bool deletePoint(short wi)
{
    if (wi < 0 || wi >= numwalls) return false;
    const short sect = ownerSectorOfWall(wi);
    if (sect < 0 || sector[sect].wallnum <= 3) return false;

    short prev = -1;
    const int start = sector[sect].wallptr;
    const int end = start + sector[sect].wallnum;
    for (int i = start; i < end; ++i)
    {
        if (wall[i].point2 == wi) { prev = static_cast<short>(i); break; }
    }
    if (prev < 0) return false;

    const short next = wall[wi].point2;
    wall[prev].point2 = next;

    for (int i = wi; i < numwalls - 1; ++i)
        wall[i] = wall[i + 1];

    --numwalls;
    sector[sect].wallnum--;

    for (short s = 0; s < numsectors; ++s)
        if (sector[s].wallptr > wi) sector[s].wallptr--;

    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].point2 > wi) wall[i].point2--;
        else if (wall[i].point2 == wi) wall[i].point2 = static_cast<short>(next > wi ? next - 1 : next);
        if (wall[i].nextwall >= wi) wall[i].nextwall = -1;
        wall[i].nextsector = -1;
    }

    fixWallRepeat(prev > wi ? prev - 1 : prev);
    rebuildSectorLinks();
    g.sel.clear();
    syncEngineSearchGlobals(g.sel);
    setDirty();
    refreshAllViews();
    return true;
}


static void rebuildSpriteListsKeepingSprites()
{
    std::array<spritetype, MAXSPRITES> oldSprites;
    for (int i = 0; i < MAXSPRITES; ++i) oldSprites[i] = sprite[i];

    for (int i = 0; i <= MAXSECTORS; ++i) headspritesect[i] = -1;
    for (int i = 0; i <= MAXSTATUS; ++i) headspritestat[i] = -1;
    for (int i = 0; i < MAXSPRITES; ++i)
    {
        prevspritesect[i] = nextspritesect[i] = -1;
        prevspritestat[i] = nextspritestat[i] = -1;
        sprite[i] = oldSprites[i];
    }

    auto linkSect = [](short i, short sect)
        {
            prevspritesect[i] = -1;
            nextspritesect[i] = headspritesect[sect];
            if (headspritesect[sect] >= 0) prevspritesect[headspritesect[sect]] = i;
            headspritesect[sect] = i;
        };
    auto linkStat = [](short i, short stat)
        {
            prevspritestat[i] = -1;
            nextspritestat[i] = headspritestat[stat];
            if (headspritestat[stat] >= 0) prevspritestat[headspritestat[stat]] = i;
            headspritestat[stat] = i;
        };

    for (short i = 0; i < MAXSPRITES; ++i)
    {
        bool active = sprite[i].sectnum >= 0 && sprite[i].sectnum < numsectors &&
            sprite[i].statnum >= 0 && sprite[i].statnum < MAXSTATUS;
        if (active)
        {
            linkSect(i, sprite[i].sectnum);
            linkStat(i, sprite[i].statnum);
        }
        else
        {
            sprite[i].sectnum = MAXSECTORS;
            sprite[i].statnum = MAXSTATUS;
            linkSect(i, MAXSECTORS);
            linkStat(i, MAXSTATUS);
        }
    }
}

static bool deleteSector(short sect)
{
    if (sect < 0 || sect >= numsectors) return false;
    const int start = sector[sect].wallptr;
    const int count = sector[sect].wallnum;
    if (start < 0 || count <= 0 || start + count > numwalls) return false;

    for (short spr = headspritesect[sect]; spr >= 0; )
    {
        short next = nextspritesect[spr];
        deletesprite(spr);
        spr = next;
    }

    for (int i = start; i + count < numwalls; ++i)
        wall[i] = wall[i + count];
    numwalls = static_cast<short>(numwalls - count);

    for (short s = sect; s < numsectors - 1; ++s)
        sector[s] = sector[s + 1];
    --numsectors;

    for (short s = 0; s < numsectors; ++s)
        if (sector[s].wallptr > start) sector[s].wallptr = static_cast<short>(sector[s].wallptr - count);

    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].point2 >= start + count) wall[i].point2 = static_cast<short>(wall[i].point2 - count);
        else if (wall[i].point2 >= start) wall[i].point2 = i;
        wall[i].nextwall = -1;
        wall[i].nextsector = -1;
    }

    for (short s = 0; s < MAXSPRITES; ++s)
    {
        if (sprite[s].sectnum == sect) deletesprite(s);
        else if (sprite[s].sectnum > sect && sprite[s].sectnum < MAXSECTORS) sprite[s].sectnum--;
    }

    rebuildSpriteListsKeepingSprites();
    rebuildSectorLinks();
    g.sel.clear();
    syncEngineSearchGlobals(g.sel);
    setDirty();
    refreshAllViews();
    return true;
}

static bool moveJoinedPoint(short wi, long nx, long ny)
{
    if (wi < 0 || wi >= numwalls) return false;
    const long ox = wall[wi].x;
    const long oy = wall[wi].y;
    snapPoint(nx, ny);
    if (nx == ox && ny == oy) return false;

    bool changed = false;
    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].x == ox && wall[i].y == oy)
        {
            wall[i].x = nx;
            wall[i].y = ny;
            changed = true;
        }
    }

    if (changed)
    {
        g.dragPointMoved = true;
        markDirtyDuringDrag();
    }
    return changed;
}

static void finalizePointDrag()
{
    if (!g.dragPointMoved) return;

    // Wall lengths and red-wall joins only need to be recalculated once after
    // the user releases the mouse. Doing this during every WM_MOUSEMOVE made
    // large maps feel extremely slow.
    for (short i = 0; i < numwalls; ++i) fixWallRepeat(i);
    rebuildSectorLinks();
    setStatus("Point moved.");
    refreshAllViews();
}

static bool commitDrawnSector()
{
    if (!g.drawingSector || g.drawPts.size() < 3) return false;
    if (numsectors >= MAXSECTORS || numwalls + static_cast<int>(g.drawPts.size()) >= MAXWALLS)
    {
        MessageBoxA(g.main, "The map has reached the BUILD sector/wall limit.", "Sector drawing", MB_ICONERROR);
        return false;
    }

    // Remove duplicate closing point if present.
    if (g.drawPts.size() >= 2)
    {
        POINT a = g.drawPts.front();
        POINT b = g.drawPts.back();
        if (a.x == b.x && a.y == b.y) g.drawPts.pop_back();
    }
    if (g.drawPts.size() < 3) return false;

    // BUILD outer loops are expected in a consistent order. Use positive signed
    // area in the editor's y-down view, matching the default square above.
    long long area = 0;
    for (size_t i = 0, j = g.drawPts.size() - 1; i < g.drawPts.size(); j = i++)
        area += static_cast<long long>(g.drawPts[j].x) * g.drawPts[i].y - static_cast<long long>(g.drawPts[i].x) * g.drawPts[j].y;
    if (area < 0) std::reverse(g.drawPts.begin(), g.drawPts.end());

    const short newSect = numsectors;
    const short firstWall = numwalls;
    const short n = static_cast<short>(g.drawPts.size());

    sectortype templ{};
    if (g.sel.sector >= 0 && g.sel.sector < numsectors)
        templ = sector[g.sel.sector];
    else if (numsectors > 0)
        templ = sector[0];
    else
    {
        templ.ceilingz = -32768;
        templ.floorz = 32768;
        templ.ceilingpicnum = 0;
        templ.floorpicnum = 0;
        templ.extra = -1;
    }
    templ.wallptr = firstWall;
    templ.wallnum = n;
    templ.extra = -1;
    sector[newSect] = templ;

    for (short i = 0; i < n; ++i)
    {
        walltype w{};
        w.x = g.drawPts[i].x;
        w.y = g.drawPts[i].y;
        w.point2 = static_cast<short>(firstWall + ((i + 1) % n));
        w.nextwall = -1;
        w.nextsector = -1;
        w.picnum = g.selectedTile;
        w.overpicnum = g.selectedTile;
        w.shade = 0;
        w.pal = 0;
        w.xrepeat = 8;
        w.yrepeat = 8;
        w.extra = -1;
        wall[firstWall + i] = w;
    }

    ++numsectors;
    numwalls = static_cast<short>(numwalls + n);
    for (short i = firstWall; i < numwalls; ++i) fixWallRepeat(i);
    rebuildSectorLinks();
    g.drawingSector = false;
    g.drawPts.clear();
    g.sel.stat = SS_FLOOR;
    g.sel.sector = newSect;
    g.sel.wall = firstWall;
    syncEngineSearchGlobals(g.sel);
    setDirty();
    setStatus("Sector %d created.", newSect);
    refreshAllViews();
    return true;
}

static short nearestPointAtScreen(HWND hwnd, int sx, int sy)
{
    // Hit-test vertices in screen space, not snapped map space.  On large maps
    // and off-grid geometry, snapping the mouse before testing can move the
    // test point far enough away that dragging never starts.
    const double th2 = 12.0 * 12.0;
    double best = th2;
    short bestWall = -1;

    for (short i = 0; i < numwalls; ++i)
    {
        int vx = 0, vy = 0;
        mapToScreen(hwnd, wall[i].x, wall[i].y, vx, vy);
        const double d = dist2(sx, sy, vx, vy);
        if (d < best)
        {
            best = d;
            bestWall = i;
        }
    }
    return bestWall;
}

static short nearestSpriteAtScreen(HWND hwnd, int sx, int sy)
{
    const double th2 = 10.0 * 10.0;
    double best = th2;
    short bestSprite = -1;

    for (short i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum >= MAXSTATUS || sprite[i].sectnum >= MAXSECTORS) continue;

        int vx = 0, vy = 0;
        mapToScreen(hwnd, sprite[i].x, sprite[i].y, vx, vy);
        const double d = dist2(sx, sy, vx, vy);
        if (d < best)
        {
            best = d;
            bestSprite = i;
        }
    }
    return bestSprite;
}

static short nearestLineAtScreen(HWND hwnd, int sx, int sy, double* bestT = nullptr)
{
    const double th2 = 7.0 * 7.0;
    double best = th2;
    double t = 0.0;
    short bestWall = -1;

    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].point2 < 0 || wall[i].point2 >= numwalls) continue;

        int ax = 0, ay = 0, bx = 0, by = 0;
        mapToScreen(hwnd, wall[i].x, wall[i].y, ax, ay);
        mapToScreen(hwnd, wall[wall[i].point2].x, wall[wall[i].point2].y, bx, by);

        double localT = 0.0;
        const double d = pointSegmentDistance2(sx, sy, ax, ay, bx, by, &localT);
        if (d < best)
        {
            best = d;
            t = localT;
            bestWall = i;
        }
    }

    if (bestT) *bestT = t;
    return bestWall;
}

static Selection pick2DAtScreen(HWND hwnd, int sx, int sy, long mx, long my)
{
    Selection s;

    short spr = nearestSpriteAtScreen(hwnd, sx, sy);
    if (spr >= 0)
    {
        s.stat = SS_SPRITE;
        s.wall = spr;
        s.sector = sprite[spr].sectnum;
        return s;
    }

    short line = nearestLineAtScreen(hwnd, sx, sy);
    if (line >= 0)
    {
        s.stat = (wall[line].cstat & 16) ? SS_MASKED_WALL : SS_WALL;
        s.wall = line;
        s.sector = ownerSectorOfWall(line);
        return s;
    }

    for (short i = 0; i < numsectors; ++i)
    {
        if (inside(mx, my, i) == 1)
        {
            s.stat = SS_FLOOR;
            s.sector = i;
            s.wall = sector[i].wallptr;
            return s;
        }
    }
    return s;
}

static void syncEngineSearchGlobals(const Selection& s)
{
    searchstat = static_cast<short>(s.stat);
    searchsector = s.sector;
    searchwall = s.wall;
    updateInspector();
}

// -----------------------------------------------------------------------------
// Selection property helpers.
// -----------------------------------------------------------------------------
static short getSelectedPic()
{
    if (!g.sel.valid()) return g.selectedTile;
    switch (g.sel.stat)
    {
    case SS_WALL: return wall[g.sel.wall].picnum;
    case SS_MASKED_WALL: return wall[g.sel.wall].overpicnum;
    case SS_CEILING: return sector[g.sel.sector].ceilingpicnum;
    case SS_FLOOR: return sector[g.sel.sector].floorpicnum;
    case SS_SPRITE: return sprite[g.sel.wall].picnum;
    default: return g.selectedTile;
    }
}

static void setSelectedPic(short pic)
{
    if (!g.sel.valid() || pic < 0 || pic >= MAXTILES) return;
    switch (g.sel.stat)
    {
    case SS_WALL:
        wall[g.sel.wall].picnum = pic;
        fixWallRepeat(g.sel.wall);
        break;
    case SS_MASKED_WALL:
        wall[g.sel.wall].overpicnum = pic;
        if (wall[g.sel.wall].nextwall >= 0) wall[wall[g.sel.wall].nextwall].overpicnum = pic;
        break;
    case SS_CEILING:
        sector[g.sel.sector].ceilingpicnum = pic;
        break;
    case SS_FLOOR:
        sector[g.sel.sector].floorpicnum = pic;
        break;
    case SS_SPRITE:
        sprite[g.sel.wall].picnum = pic;
        break;
    }
    setDirty();
    updateInspector();
    refreshAllViews();
}

static bool selectedSectorValid()
{
    return g.sel.sector >= 0 && g.sel.sector < numsectors;
}

static bool selectedSurfaceValid()
{
    if (!g.sel.valid()) return false;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
        return g.sel.wall >= 0 && g.sel.wall < numwalls;
    case SS_CEILING:
    case SS_FLOOR:
        return selectedSectorValid();
    case SS_SPRITE:
        return g.sel.wall >= 0 && g.sel.wall < MAXSPRITES &&
            sprite[g.sel.wall].statnum < MAXSTATUS && sprite[g.sel.wall].sectnum < MAXSECTORS;
    default:
        return false;
    }
}

static const char* selectedSurfaceName()
{
    switch (g.sel.stat)
    {
    case SS_WALL: return "Wall";
    case SS_MASKED_WALL: return "Masked wall";
    case SS_CEILING: return "Ceiling";
    case SS_FLOOR: return "Floor/Sector";
    case SS_SPRITE: return "Sprite";
    default: return "No surface";
    }
}

static int getSelectedShadeValue()
{
    if (!selectedSurfaceValid()) return 0;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL: return wall[g.sel.wall].shade;
    case SS_CEILING: return sector[g.sel.sector].ceilingshade;
    case SS_FLOOR: return sector[g.sel.sector].floorshade;
    case SS_SPRITE: return sprite[g.sel.wall].shade;
    default: return 0;
    }
}

static int getSelectedPalValue()
{
    if (!selectedSurfaceValid()) return 0;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL: return static_cast<unsigned char>(wall[g.sel.wall].pal);
    case SS_CEILING: return static_cast<unsigned char>(sector[g.sel.sector].ceilingpal);
    case SS_FLOOR: return static_cast<unsigned char>(sector[g.sel.sector].floorpal);
    case SS_SPRITE: return static_cast<unsigned char>(sprite[g.sel.wall].pal);
    default: return 0;
    }
}

static bool setSelectedPicDirect(short pic)
{
    if (!selectedSurfaceValid() || pic < 0 || pic >= MAXTILES) return false;
    bool changed = false;
    switch (g.sel.stat)
    {
    case SS_WALL:
        changed = wall[g.sel.wall].picnum != pic;
        wall[g.sel.wall].picnum = pic;
        if (changed) fixWallRepeat(g.sel.wall);
        break;
    case SS_MASKED_WALL:
        changed = wall[g.sel.wall].overpicnum != pic;
        wall[g.sel.wall].overpicnum = pic;
        if (wall[g.sel.wall].nextwall >= 0)
        {
            changed = changed || wall[wall[g.sel.wall].nextwall].overpicnum != pic;
            wall[wall[g.sel.wall].nextwall].overpicnum = pic;
        }
        break;
    case SS_CEILING:
        changed = sector[g.sel.sector].ceilingpicnum != pic;
        sector[g.sel.sector].ceilingpicnum = pic;
        break;
    case SS_FLOOR:
        changed = sector[g.sel.sector].floorpicnum != pic;
        sector[g.sel.sector].floorpicnum = pic;
        break;
    case SS_SPRITE:
        changed = sprite[g.sel.wall].picnum != pic;
        sprite[g.sel.wall].picnum = pic;
        break;
    }
    return changed;
}

static bool setSelectedShadeDirect(int shade)
{
    if (!selectedSurfaceValid()) return false;
    const signed char v = static_cast<signed char>(clamp_int(shade, -128, 127));
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
        if (wall[g.sel.wall].shade == v) return false;
        wall[g.sel.wall].shade = v;
        return true;
    case SS_CEILING:
        if (sector[g.sel.sector].ceilingshade == v) return false;
        sector[g.sel.sector].ceilingshade = v;
        return true;
    case SS_FLOOR:
        if (sector[g.sel.sector].floorshade == v) return false;
        sector[g.sel.sector].floorshade = v;
        return true;
    case SS_SPRITE:
        if (sprite[g.sel.wall].shade == v) return false;
        sprite[g.sel.wall].shade = v;
        return true;
    default:
        return false;
    }
}

static bool setSelectedPalDirect(int palnum)
{
    if (!selectedSurfaceValid()) return false;
    const char v = static_cast<char>(clamp_int(palnum, 0, 255));
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
        if (wall[g.sel.wall].pal == v) return false;
        wall[g.sel.wall].pal = v;
        return true;
    case SS_CEILING:
        if (sector[g.sel.sector].ceilingpal == v) return false;
        sector[g.sel.sector].ceilingpal = v;
        return true;
    case SS_FLOOR:
        if (sector[g.sel.sector].floorpal == v) return false;
        sector[g.sel.sector].floorpal = v;
        return true;
    case SS_SPRITE:
        if (sprite[g.sel.wall].pal == v) return false;
        sprite[g.sel.wall].pal = v;
        return true;
    default:
        return false;
    }
}

static void copyProps()
{
    if (!g.sel.valid()) return;
    CopiedProps c;
    c.valid = true;
    c.stat = g.sel.stat;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
    {
        walltype& w = wall[g.sel.wall];
        c.picnum = (g.sel.stat == SS_MASKED_WALL) ? w.overpicnum : w.picnum;
        c.cstat = w.cstat;
        c.lotag = w.lotag;
        c.hitag = w.hitag;
        c.extra = w.extra;
        c.shade = w.shade;
        c.pal = w.pal;
        c.xrepeat = static_cast<unsigned char>(w.xrepeat);
        c.yrepeat = static_cast<unsigned char>(w.yrepeat);
        c.xpanning = w.xpanning;
        c.ypanning = w.ypanning;
        break;
    }
    case SS_CEILING:
    case SS_FLOOR:
    {
        sectortype& s = sector[g.sel.sector];
        const bool ceil = (g.sel.stat == SS_CEILING);
        c.picnum = ceil ? s.ceilingpicnum : s.floorpicnum;
        c.cstat = ceil ? s.ceilingstat : s.floorstat;
        c.lotag = s.lotag;
        c.hitag = s.hitag;
        c.extra = s.extra;
        c.shade = ceil ? s.ceilingshade : s.floorshade;
        c.pal = ceil ? s.ceilingpal : s.floorpal;
        c.visibility = s.visibility;
        c.xpanning = ceil ? s.ceilingxpanning : s.floorxpanning;
        c.ypanning = ceil ? s.ceilingypanning : s.floorypanning;
        break;
    }
    case SS_SPRITE:
    {
        spritetype& sp = sprite[g.sel.wall];
        c.picnum = sp.picnum;
        c.cstat = sp.cstat;
        c.lotag = sp.lotag;
        c.hitag = sp.hitag;
        c.extra = sp.extra;
        c.shade = sp.shade;
        c.pal = sp.pal;
        c.xrepeat = sp.xrepeat;
        c.yrepeat = sp.yrepeat;
        break;
    }
    }
    g.clip = c;
    g.selectedTile = c.picnum;
    centerTextureBarOnSelected();
    setStatus("Copied properties from selection.");
}

static void pasteProps()
{
    if (!g.clip.valid || !g.sel.valid()) return;
    const CopiedProps& c = g.clip;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
    {
        walltype& w = wall[g.sel.wall];
        if (g.sel.stat == SS_MASKED_WALL) w.overpicnum = c.picnum; else w.picnum = c.picnum;
        w.cstat = c.cstat;
        w.lotag = c.lotag;
        w.hitag = c.hitag;
        w.extra = c.extra;
        w.shade = c.shade;
        w.pal = c.pal;
        w.xrepeat = static_cast<char>(c.xrepeat);
        w.yrepeat = static_cast<char>(c.yrepeat);
        w.xpanning = c.xpanning;
        w.ypanning = c.ypanning;
        break;
    }
    case SS_CEILING:
    case SS_FLOOR:
    {
        sectortype& s = sector[g.sel.sector];
        if (g.sel.stat == SS_CEILING)
        {
            s.ceilingpicnum = c.picnum;
            s.ceilingstat = c.cstat;
            s.ceilingshade = c.shade;
            s.ceilingpal = c.pal;
            s.ceilingxpanning = c.xpanning;
            s.ceilingypanning = c.ypanning;
        }
        else
        {
            s.floorpicnum = c.picnum;
            s.floorstat = c.cstat;
            s.floorshade = c.shade;
            s.floorpal = c.pal;
            s.floorxpanning = c.xpanning;
            s.floorypanning = c.ypanning;
        }
        s.visibility = c.visibility;
        s.lotag = c.lotag;
        s.hitag = c.hitag;
        s.extra = c.extra;
        break;
    }
    case SS_SPRITE:
    {
        spritetype& sp = sprite[g.sel.wall];
        sp.picnum = c.picnum;
        sp.cstat = c.cstat;
        sp.lotag = c.lotag;
        sp.hitag = c.hitag;
        sp.extra = c.extra;
        sp.shade = c.shade;
        sp.pal = c.pal;
        sp.xrepeat = c.xrepeat;
        sp.yrepeat = c.yrepeat;
        break;
    }
    }
    setDirty();
    updateInspector();
    refreshAllViews();
    setStatus("Pasted properties to selection.");
}

static void applySelectedTexture()
{
    setSelectedPic(g.selectedTile);
    setStatus("Applied tile %d.", g.selectedTile);
}

static void shadeSelection(int delta)
{
    if (!g.sel.valid()) return;
    switch (g.sel.stat)
    {
    case SS_WALL:
    case SS_MASKED_WALL:
        wall[g.sel.wall].shade = static_cast<signed char>(clamp_int(wall[g.sel.wall].shade + delta, -128, 127));
        break;
    case SS_CEILING:
        sector[g.sel.sector].ceilingshade = static_cast<signed char>(clamp_int(sector[g.sel.sector].ceilingshade + delta, -128, 127));
        break;
    case SS_FLOOR:
        sector[g.sel.sector].floorshade = static_cast<signed char>(clamp_int(sector[g.sel.sector].floorshade + delta, -128, 127));
        break;
    case SS_SPRITE:
        sprite[g.sel.wall].shade = static_cast<signed char>(clamp_int(sprite[g.sel.wall].shade + delta, -128, 127));
        break;
    }
    setDirty(); updateInspector(); refreshAllViews();
}

static void zAdjustSelection(long dz)
{
    if (!g.sel.valid()) return;
    switch (g.sel.stat)
    {
    case SS_CEILING:
        sector[g.sel.sector].ceilingz += dz;
        break;
    case SS_FLOOR:
        sector[g.sel.sector].floorz += dz;
        break;
    case SS_SPRITE:
        sprite[g.sel.wall].z += dz;
        break;
    case SS_WALL:
    case SS_MASKED_WALL:
        sector[g.sel.sector].ceilingz += dz;
        sector[g.sel.sector].floorz += dz;
        break;
    }
    setDirty(); refreshAllViews();
}

static void toggleBlocking()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_SPRITE)
    {
        spritetype& sp = sprite[g.sel.wall];
        sp.cstat ^= 1;
        sp.cstat &= ~256;
        sp.cstat |= ((sp.cstat & 1) << 8);
    }
    else if (g.sel.wall >= 0)
    {
        walltype& w = wall[g.sel.wall];
        w.cstat ^= 1;
        w.cstat &= ~64;
        if (w.nextwall >= 0)
        {
            wall[w.nextwall].cstat &= ~(1 | 64);
            wall[w.nextwall].cstat |= (w.cstat & 1);
        }
    }
    setDirty(); refreshAllViews();
}

static void toggleOneWay()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_SPRITE) sprite[g.sel.wall].cstat ^= 64;
    else if (g.sel.wall >= 0) wall[g.sel.wall].cstat ^= 32;
    setDirty(); refreshAllViews();
}

static void toggleMasked()
{
    if (!g.sel.valid() || g.sel.stat == SS_SPRITE || g.sel.wall < 0) return;
    walltype& w = wall[g.sel.wall];
    if (w.nextwall < 0) return;
    w.cstat ^= 16;
    if (w.cstat & 16)
    {
        w.cstat &= ~8;
        wall[w.nextwall].cstat |= 8 | 16;
        wall[w.nextwall].overpicnum = w.overpicnum;
    }
    else
    {
        w.cstat &= ~8;
        wall[w.nextwall].cstat &= ~(8 | 16);
    }
    w.cstat &= ~32;
    wall[w.nextwall].cstat &= ~32;
    setDirty(); refreshAllViews();
}

static void toggleTranslucent()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_SPRITE)
    {
        spritetype& sp = sprite[g.sel.wall];
        if ((sp.cstat & 2) == 0) sp.cstat |= 2;
        else if ((sp.cstat & 512) == 0) sp.cstat |= 512;
        else sp.cstat &= ~(2 | 512);
    }
    else if (g.sel.stat == SS_MASKED_WALL || g.sel.stat == SS_WALL)
    {
        walltype& w = wall[g.sel.wall];
        if ((w.cstat & 128) == 0) w.cstat |= 128;
        else if ((w.cstat & 512) == 0) w.cstat |= 512;
        else w.cstat &= ~(128 | 512);
        if (w.nextwall >= 0)
        {
            wall[w.nextwall].cstat &= ~(128 | 512);
            wall[w.nextwall].cstat |= (w.cstat & (128 | 512));
        }
    }
    setDirty(); refreshAllViews();
}

static void toggleParallax()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_CEILING || g.sel.stat == SS_WALL || g.sel.stat == SS_MASKED_WALL)
        sector[g.sel.sector].ceilingstat ^= 1;
    else if (g.sel.stat == SS_FLOOR)
        sector[g.sel.sector].floorstat ^= 1;
    setDirty(); refreshAllViews();
}

static void editProperties()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_WALL || g.sel.stat == SS_MASKED_WALL)
    {
        walltype& w = wall[g.sel.wall];
        long v = (g.sel.stat == SS_MASKED_WALL) ? w.overpicnum : w.picnum;
        if (promptLong("Wall properties", "Picture tile:", v, 0, MAXTILES - 1)) { if (g.sel.stat == SS_MASKED_WALL) w.overpicnum = static_cast<short>(v); else w.picnum = static_cast<short>(v); }
        v = w.lotag; if (promptLong("Wall properties", "Lo-tag:", v, -32768, 65535)) w.lotag = static_cast<short>(v);
        v = w.hitag; if (promptLong("Wall properties", "Hi-tag:", v, -32768, 65535)) w.hitag = static_cast<short>(v);
        v = w.cstat; if (promptLong("Wall properties", "Cstat:", v, 0, 65535)) w.cstat = static_cast<short>(v);
        v = w.pal; if (promptLong("Wall properties", "Palette:", v, 0, 255)) w.pal = static_cast<char>(v);
    }
    else if (g.sel.stat == SS_CEILING || g.sel.stat == SS_FLOOR)
    {
        sectortype& s = sector[g.sel.sector];
        const bool ceil = g.sel.stat == SS_CEILING;
        long v = ceil ? s.ceilingpicnum : s.floorpicnum;
        if (promptLong("Sector properties", ceil ? "Ceiling tile:" : "Floor tile:", v, 0, MAXTILES - 1)) { if (ceil) s.ceilingpicnum = static_cast<short>(v); else s.floorpicnum = static_cast<short>(v); }
        v = s.lotag; if (promptLong("Sector properties", "Lo-tag:", v, -32768, 65535)) s.lotag = static_cast<short>(v);
        v = s.hitag; if (promptLong("Sector properties", "Hi-tag:", v, -32768, 65535)) s.hitag = static_cast<short>(v);
        v = ceil ? s.ceilingstat : s.floorstat; if (promptLong("Sector properties", ceil ? "Ceiling stat:" : "Floor stat:", v, 0, 65535)) { if (ceil) s.ceilingstat = static_cast<short>(v); else s.floorstat = static_cast<short>(v); }
        v = ceil ? s.ceilingpal : s.floorpal; if (promptLong("Sector properties", ceil ? "Ceiling palette:" : "Floor palette:", v, 0, 255)) { if (ceil) s.ceilingpal = static_cast<char>(v); else s.floorpal = static_cast<char>(v); }
    }
    else if (g.sel.stat == SS_SPRITE)
    {
        spritetype& sp = sprite[g.sel.wall];
        long v = sp.picnum; if (promptLong("Sprite properties", "Picture tile:", v, 0, MAXTILES - 1)) sp.picnum = static_cast<short>(v);
        v = sp.lotag; if (promptLong("Sprite properties", "Lo-tag:", v, -32768, 65535)) sp.lotag = static_cast<short>(v);
        v = sp.hitag; if (promptLong("Sprite properties", "Hi-tag:", v, -32768, 65535)) sp.hitag = static_cast<short>(v);
        v = sp.cstat; if (promptLong("Sprite properties", "Cstat:", v, 0, 65535)) sp.cstat = static_cast<short>(v);
        v = sp.pal; if (promptLong("Sprite properties", "Palette:", v, 0, 255)) sp.pal = static_cast<char>(v);
        v = sp.statnum; if (promptLong("Sprite properties", "Status list:", v, 0, MAXSTATUS - 1)) changespritestat(g.sel.wall, static_cast<short>(v));
    }
    setDirty(); updateInspector(); refreshAllViews();
}

static void insertSpriteAt(long mx, long my)
{
    short sect = -1;
    for (short i = 0; i < numsectors; ++i)
    {
        if (inside(mx, my, i) == 1) { sect = i; break; }
    }
    if (sect < 0) return;
    short idx = insertsprite(sect, 0);
    if (idx < 0) return;
    spritetype& sp = sprite[idx];
    sp.x = mx;
    sp.y = my;
    sp.z = getflorzofslope(sect, mx, my);
    sp.cstat = 0;
    sp.picnum = g.selectedTile;
    sp.shade = 0;
    sp.pal = 0;
    sp.clipdist = 32;
    sp.xrepeat = 64;
    sp.yrepeat = 64;
    sp.xoffset = 0;
    sp.yoffset = 0;
    sp.ang = 1536;
    sp.owner = -1;
    sp.xvel = sp.yvel = sp.zvel = 0;
    sp.lotag = sp.hitag = 0;
    sp.extra = -1;
    setsprite(idx, mx, my, sp.z);
    g.sel.stat = SS_SPRITE;
    g.sel.wall = idx;
    g.sel.sector = sect;
    setDirty();
    syncEngineSearchGlobals(g.sel);
    refreshAllViews();
    setStatus("Sprite %d inserted.", idx);
}

static void deleteSelection()
{
    if (!g.sel.valid()) return;
    if (g.sel.stat == SS_SPRITE)
    {
        deletesprite(g.sel.wall);
        g.sel.clear();
        syncEngineSearchGlobals(g.sel);
        setDirty(); refreshAllViews();
        return;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && g.sel.sector >= 0)
    {
        deleteSector(g.sel.sector);
        return;
    }
    if (g.sel.wall >= 0)
    {
        deletePoint(g.sel.wall);
    }
}

// -----------------------------------------------------------------------------
// Engine window hosting and 3D rendering.
// -----------------------------------------------------------------------------
static HWND findEngineWindow()
{
    HWND hwnd = FindWindowA("BuildGL1DropInWindow", nullptr);
    if (hwnd) return hwnd;
    // Class-name lookup may fail if the title changed; enumerate as a fallback.
    struct FindData { HWND found = nullptr; } data;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL
        {
            char cls[128] = {};
            GetClassNameA(h, cls, sizeof(cls));
            if (std::strcmp(cls, "BuildGL1DropInWindow") == 0)
            {
                reinterpret_cast<FindData*>(lp)->found = h;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&data));
    return data.found;
}

static Selection pick3D(int sx, int sy)
{
    Selection s;
    RECT rc{};
    GetClientRect(g.engine ? g.engine : g.host3d, &rc);
    const int w = max(1, rc.right - rc.left);
    const int h = max(1, rc.bottom - rc.top);

    long dax = 16384;
    long day = divscale14_local(sx - (w >> 1), (w >> 1));
    rotatepoint(0, 0, dax, day, g.cam.ang, &dax, &day);
    const long dz = (scale_local(sy, 200, h) - g.cam.horiz) * 2000;

    short hs = -1, hw = -1, hsp = -1;
    long hx = 0, hy = 0, hz = 0;
    hitscan(g.cam.x, g.cam.y, g.cam.z, g.cam.sect, dax, day, dz, &hs, &hw, &hsp, &hx, &hy, &hz, CLIPMASK1);

    if (hsp >= 0)
    {
        s.stat = SS_SPRITE;
        s.wall = hsp;
        s.sector = sprite[hsp].sectnum;
    }
    else if (hw >= 0)
    {
        s.stat = (wall[hw].cstat & 16) ? SS_MASKED_WALL : SS_WALL;
        s.wall = hw;
        s.sector = ownerSectorOfWall(hw);
    }
    else if (hs >= 0)
    {
        long cz = getceilzofslope(hs, hx, hy);
        long fz = getflorzofslope(hs, hx, hy);
        s.stat = (std::labs(hz - cz) < std::labs(hz - fz)) ? SS_CEILING : SS_FLOOR;
        s.sector = hs;
        s.wall = sector[hs].wallptr;
    }

    return s;
}

static LRESULT CALLBACK EngineChildSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g.sel = pick3D(p.x, p.y);
        syncEngineSearchGlobals(g.sel);
        if ((GetKeyState(VK_CONTROL) & 0x8000) && g.sel.valid()) applySelectedTexture();
        else if (g.sel.valid())
        {
            g.selectedTile = getSelectedPic();
            centerTextureBarOnSelected();
            setStatus("3D selection: stat %d sector %d wall/sprite %d", g.sel.stat, g.sel.sector, g.sel.wall);
        }
        refreshAllViews();
        return 0;
    }
    case WM_RBUTTONDOWN:
        g.orbit3D = true;
        g.lastMouse = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        g.orbit3D = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (g.orbit3D)
        {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            const int dx = p.x - g.lastMouse.x;
            const int dy = p.y - g.lastMouse.y;
            g.cam.ang = static_cast<short>((g.cam.ang + dx * 4) & 2047);
            g.cam.horiz = clamp_long(g.cam.horiz + dy, -100, 300);
            g.lastMouse = p;
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        g.cam.z -= (GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA) * (4 << 8);
        return 0;
    }
    return CallWindowProcA(g.oldEngineProc, hwnd, msg, wp, lp);
}

static void attachEngineWindow()
{
    if (!g.host3d) return;

    if (!g.engineCreated)
    {
        RECT rc{};
        GetClientRect(g.host3d, &rc);
        const int w = max(320, rc.right - rc.left);
        const int h = max(200, rc.bottom - rc.top);
        setgamemode(1, w, h);
        g.engine = findEngineWindow();
        g.engineCreated = true;
    }

    if (!g.engine) g.engine = findEngineWindow();
    if (!g.engine) return;

    SetParent(g.engine, g.host3d);
    LONG_PTR style = GetWindowLongPtrA(g.engine, GWL_STYLE);
    style &= ~(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetWindowLongPtrA(g.engine, GWL_STYLE, style);

    if (!g.oldEngineProc)
        g.oldEngineProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(g.engine, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EngineChildSubclassProc)));

    RECT rc{};
    GetClientRect(g.host3d, &rc);
    SetWindowPos(g.engine, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

static void update3DCamera(double dt)
{
    if (g.cam.sect < 0) updatesector(g.cam.x, g.cam.y, &g.cam.sect);

    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const long move = static_cast<long>((shift ? 12000.0 : 6000.0) * dt);
    const long turn = static_cast<long>(700.0 * dt);
    long dx = 0, dy = 0;

    if (GetAsyncKeyState(VK_LEFT) & 0x8000) g.cam.ang = static_cast<short>((g.cam.ang - turn) & 2047);
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) g.cam.ang = static_cast<short>((g.cam.ang + turn) & 2047);
    if (GetAsyncKeyState(VK_UP) & 0x8000) g.cam.horiz = clamp_long(g.cam.horiz - static_cast<long>(200.0 * dt), -100, 300);
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) g.cam.horiz = clamp_long(g.cam.horiz + static_cast<long>(200.0 * dt), -100, 300);

    auto addMove = [&](int angOffset, long amount)
        {
            dx += i64mulscale(sintable[(g.cam.ang + angOffset + 512) & 2047], amount, 14);
            dy += i64mulscale(sintable[(g.cam.ang + angOffset) & 2047], amount, 14);
        };

    if (GetAsyncKeyState('W') & 0x8000) addMove(0, move);
    if (GetAsyncKeyState('S') & 0x8000) addMove(1024, move);
    if (GetAsyncKeyState('A') & 0x8000) addMove(1536, move);
    if (GetAsyncKeyState('D') & 0x8000) addMove(512, move);

    if (dx || dy)
    {
        g.cam.x += dx;
        g.cam.y += dy;
        updatesector(g.cam.x, g.cam.y, &g.cam.sect);
    }
    if (GetAsyncKeyState(VK_PRIOR) & 0x8000) g.cam.z -= static_cast<long>(4096.0 * dt);
    if (GetAsyncKeyState(VK_NEXT) & 0x8000) g.cam.z += static_cast<long>(4096.0 * dt);
}

static void render3D()
{
    if (g.activeTab != 1) return;
    attachEngineWindow();
    if (!g.engine) return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    double dt = 1.0 / 60.0;
    if (g.lastTick.QuadPart != 0 && g.freq.QuadPart != 0)
        dt = static_cast<double>(now.QuadPart - g.lastTick.QuadPart) / static_cast<double>(g.freq.QuadPart);
    g.lastTick = now;
    dt = max(0.0, min(dt, 0.1));

    update3DCamera(dt);
    totalclock += static_cast<long>(dt * 120.0);
    drawrooms(g.cam.x, g.cam.y, g.cam.z, g.cam.ang, g.cam.horiz, g.cam.sect);
    drawmasks();
    nextpage();
}

// Called by Build.cpp's private GL window proc.
LRESULT CALLBACK Engine_KeyboardWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hWnd; (void)lParam;
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < 256) g.keys[wParam] = true;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < 256) g.keys[wParam] = false;
        return 0;
    case WM_KILLFOCUS:
    case WM_ACTIVATEAPP:
        if (msg == WM_KILLFOCUS || wParam == FALSE) std::memset(g.keys, 0, sizeof(g.keys));
        return 0;
    default:
        return 0;
    }
}

// -----------------------------------------------------------------------------
// 2D view painting and messages.
//
// The 2D editor no longer renders its map primitives through GDI.  Everything
// in this view is rasterized into a persistent 32-bit BGRA software framebuffer,
// then WM_PAINT copies that buffer to the window with SetDIBitsToDevice.  This
// avoids per-line HPEN/HBRUSH churn, avoids erase/redraw flicker, and keeps GDI
// away from the OpenGL child window used by the 3D backend.
// -----------------------------------------------------------------------------
struct Soft2DBackBuffer
{
    int w = 0;
    int h = 0;
    std::vector<std::uint32_t> pixels;
    BITMAPINFO bmi{};

    void resize(int nw, int nh)
    {
        nw = max(1, nw);
        nh = max(1, nh);
        if (w == nw && h == nh && !pixels.empty()) return;

        w = nw;
        h = nh;
        pixels.assign(static_cast<size_t>(w) * h, 0xff000000u);

        std::memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }
};

static Soft2DBackBuffer g_2dBuffer;

static std::uint32_t makeColor(std::uint8_t r, std::uint8_t g_, std::uint8_t b)
{
    return 0xff000000u | (static_cast<std::uint32_t>(r) << 16) |
        (static_cast<std::uint32_t>(g_) << 8) | static_cast<std::uint32_t>(b);
}

static std::uint32_t palColor32(int idx)
{
    idx &= 255;
    return makeColor(static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 0]) << 2),
        static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 1]) << 2),
        static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 2]) << 2));
}

static void clear2D(std::uint32_t color)
{
    std::fill(g_2dBuffer.pixels.begin(), g_2dBuffer.pixels.end(), color);
}

static void putPixel2D(int x, int y, std::uint32_t color)
{
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(g_2dBuffer.w) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(g_2dBuffer.h))
        return;
    g_2dBuffer.pixels[static_cast<size_t>(y) * g_2dBuffer.w + x] = color;
}

static void fillRect2D(int x0, int y0, int x1, int y1, std::uint32_t color)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);

    x0 = clamp_int(x0, 0, g_2dBuffer.w);
    x1 = clamp_int(x1, 0, g_2dBuffer.w);
    y0 = clamp_int(y0, 0, g_2dBuffer.h);
    y1 = clamp_int(y1, 0, g_2dBuffer.h);
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y)
    {
        std::uint32_t* dst = &g_2dBuffer.pixels[static_cast<size_t>(y) * g_2dBuffer.w + x0];
        std::fill(dst, dst + (x1 - x0), color);
    }
}

static void drawLineThin2D(int x0, int y0, int x1, int y1, std::uint32_t color)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        putPixel2D(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err << 1;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void drawLine2D(int x0, int y0, int x1, int y1, std::uint32_t color, int width = 1)
{
    width = max(1, width);
    if (width <= 1)
    {
        drawLineThin2D(x0, y0, x1, y1, color);
        return;
    }

    const int r = width / 2;
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);

    if (dx >= dy)
    {
        for (int o = -r; o <= r; ++o)
            drawLineThin2D(x0, y0 + o, x1, y1 + o, color);
    }
    else
    {
        for (int o = -r; o <= r; ++o)
            drawLineThin2D(x0 + o, y0, x1 + o, y1, color);
    }
}

static void drawRectOutline2D(int x0, int y0, int x1, int y1, std::uint32_t color)
{
    drawLine2D(x0, y0, x1, y0, color);
    drawLine2D(x1, y0, x1, y1, color);
    drawLine2D(x1, y1, x0, y1, color);
    drawLine2D(x0, y1, x0, y0, color);
}

// Minimal editor HUD font. Lowercase input is promoted to uppercase.
static const std::uint8_t* font5x7Rows(char ch)
{
    static const std::uint8_t blank[7] = { 0,0,0,0,0,0,0 };
    static const std::uint8_t qmark[7] = { 0x0e,0x11,0x01,0x02,0x04,0x00,0x04 };

    switch (ch)
    {
    case '0': { static const std::uint8_t r[7] = { 0x0e,0x11,0x13,0x15,0x19,0x11,0x0e }; return r; }
    case '1': { static const std::uint8_t r[7] = { 0x04,0x0c,0x04,0x04,0x04,0x04,0x0e }; return r; }
    case '2': { static const std::uint8_t r[7] = { 0x0e,0x11,0x01,0x02,0x04,0x08,0x1f }; return r; }
    case '3': { static const std::uint8_t r[7] = { 0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e }; return r; }
    case '4': { static const std::uint8_t r[7] = { 0x02,0x06,0x0a,0x12,0x1f,0x02,0x02 }; return r; }
    case '5': { static const std::uint8_t r[7] = { 0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e }; return r; }
    case '6': { static const std::uint8_t r[7] = { 0x06,0x08,0x10,0x1e,0x11,0x11,0x0e }; return r; }
    case '7': { static const std::uint8_t r[7] = { 0x1f,0x01,0x02,0x04,0x08,0x08,0x08 }; return r; }
    case '8': { static const std::uint8_t r[7] = { 0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e }; return r; }
    case '9': { static const std::uint8_t r[7] = { 0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c }; return r; }

    case 'A': { static const std::uint8_t r[7] = { 0x0e,0x11,0x11,0x1f,0x11,0x11,0x11 }; return r; }
    case 'B': { static const std::uint8_t r[7] = { 0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e }; return r; }
    case 'C': { static const std::uint8_t r[7] = { 0x0e,0x11,0x10,0x10,0x10,0x11,0x0e }; return r; }
    case 'D': { static const std::uint8_t r[7] = { 0x1e,0x11,0x11,0x11,0x11,0x11,0x1e }; return r; }
    case 'E': { static const std::uint8_t r[7] = { 0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f }; return r; }
    case 'F': { static const std::uint8_t r[7] = { 0x1f,0x10,0x10,0x1e,0x10,0x10,0x10 }; return r; }
    case 'G': { static const std::uint8_t r[7] = { 0x0e,0x11,0x10,0x17,0x11,0x11,0x0e }; return r; }
    case 'H': { static const std::uint8_t r[7] = { 0x11,0x11,0x11,0x1f,0x11,0x11,0x11 }; return r; }
    case 'I': { static const std::uint8_t r[7] = { 0x0e,0x04,0x04,0x04,0x04,0x04,0x0e }; return r; }
    case 'J': { static const std::uint8_t r[7] = { 0x07,0x02,0x02,0x02,0x12,0x12,0x0c }; return r; }
    case 'K': { static const std::uint8_t r[7] = { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 }; return r; }
    case 'L': { static const std::uint8_t r[7] = { 0x10,0x10,0x10,0x10,0x10,0x10,0x1f }; return r; }
    case 'M': { static const std::uint8_t r[7] = { 0x11,0x1b,0x15,0x15,0x11,0x11,0x11 }; return r; }
    case 'N': { static const std::uint8_t r[7] = { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 }; return r; }
    case 'O': { static const std::uint8_t r[7] = { 0x0e,0x11,0x11,0x11,0x11,0x11,0x0e }; return r; }
    case 'P': { static const std::uint8_t r[7] = { 0x1e,0x11,0x11,0x1e,0x10,0x10,0x10 }; return r; }
    case 'Q': { static const std::uint8_t r[7] = { 0x0e,0x11,0x11,0x11,0x15,0x12,0x0d }; return r; }
    case 'R': { static const std::uint8_t r[7] = { 0x1e,0x11,0x11,0x1e,0x14,0x12,0x11 }; return r; }
    case 'S': { static const std::uint8_t r[7] = { 0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e }; return r; }
    case 'T': { static const std::uint8_t r[7] = { 0x1f,0x04,0x04,0x04,0x04,0x04,0x04 }; return r; }
    case 'U': { static const std::uint8_t r[7] = { 0x11,0x11,0x11,0x11,0x11,0x11,0x0e }; return r; }
    case 'V': { static const std::uint8_t r[7] = { 0x11,0x11,0x11,0x11,0x11,0x0a,0x04 }; return r; }
    case 'W': { static const std::uint8_t r[7] = { 0x11,0x11,0x11,0x15,0x15,0x15,0x0a }; return r; }
    case 'X': { static const std::uint8_t r[7] = { 0x11,0x11,0x0a,0x04,0x0a,0x11,0x11 }; return r; }
    case 'Y': { static const std::uint8_t r[7] = { 0x11,0x11,0x0a,0x04,0x04,0x04,0x04 }; return r; }
    case 'Z': { static const std::uint8_t r[7] = { 0x1f,0x01,0x02,0x04,0x08,0x10,0x1f }; return r; }

    case ' ': return blank;
    case '-': { static const std::uint8_t r[7] = { 0x00,0x00,0x00,0x1f,0x00,0x00,0x00 }; return r; }
    case '+': { static const std::uint8_t r[7] = { 0x00,0x04,0x04,0x1f,0x04,0x04,0x00 }; return r; }
    case '.': { static const std::uint8_t r[7] = { 0x00,0x00,0x00,0x00,0x00,0x0c,0x0c }; return r; }
    case ',': { static const std::uint8_t r[7] = { 0x00,0x00,0x00,0x00,0x0c,0x04,0x08 }; return r; }
    case ':': { static const std::uint8_t r[7] = { 0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00 }; return r; }
    case '/': { static const std::uint8_t r[7] = { 0x01,0x01,0x02,0x04,0x08,0x10,0x10 }; return r; }
    case '\\': { static const std::uint8_t r[7] = { 0x10,0x10,0x08,0x04,0x02,0x01,0x01 }; return r; }
    case '*': { static const std::uint8_t r[7] = { 0x00,0x15,0x0e,0x1f,0x0e,0x15,0x00 }; return r; }
    case '_': { static const std::uint8_t r[7] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x1f }; return r; }
    case '%': { static const std::uint8_t r[7] = { 0x19,0x1a,0x02,0x04,0x08,0x0b,0x13 }; return r; }
    case '(': { static const std::uint8_t r[7] = { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 }; return r; }
    case ')': { static const std::uint8_t r[7] = { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 }; return r; }
    case '[': { static const std::uint8_t r[7] = { 0x0e,0x08,0x08,0x08,0x08,0x08,0x0e }; return r; }
    case ']': { static const std::uint8_t r[7] = { 0x0e,0x02,0x02,0x02,0x02,0x02,0x0e }; return r; }
    default: return qmark;
    }
}

static void drawChar2D(int x, int y, char ch, std::uint32_t color, int scale = 1)
{
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    const std::uint8_t* rows = font5x7Rows(ch);
    scale = max(1, scale);

    for (int ry = 0; ry < 7; ++ry)
    {
        const std::uint8_t bits = rows[ry];
        for (int rx = 0; rx < 5; ++rx)
        {
            if (bits & (1 << (4 - rx)))
                fillRect2D(x + rx * scale, y + ry * scale, x + (rx + 1) * scale, y + (ry + 1) * scale, color);
        }
    }
}

static void drawText2D(int x, int y, const char* text, std::uint32_t color, int scale = 1)
{
    if (!text) return;
    const int step = 6 * max(1, scale);
    int cx = x;
    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            cx = x;
            y += 9 * max(1, scale);
            continue;
        }
        drawChar2D(cx, y, *p, color, scale);
        cx += step;
        if (cx >= g_2dBuffer.w - step) break;
    }
}

static void draw2DGrid(HWND hwnd)
{
    if (g.grid <= 0) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int gs = gridSize();
    const double z = static_cast<double>(g.zoom) / 16384.0;
    if (z <= 0.00001) return;
    const long left = static_cast<long>(g.viewX - (rc.right * 0.5) / z) - gs * 2;
    const long top = static_cast<long>(g.viewY - (rc.bottom * 0.5) / z) - gs * 2;
    const long right = static_cast<long>(g.viewX + (rc.right * 0.5) / z) + gs * 2;
    const long bottom = static_cast<long>(g.viewY + (rc.bottom * 0.5) / z) + gs * 2;

    const std::uint32_t color = g.gridLock ? makeColor(32, 48, 32) : makeColor(32, 32, 48);
    long firstX = (left / gs) * gs;
    long firstY = (top / gs) * gs;
    for (long x = firstX; x <= right; x += gs)
    {
        int sx1, sy1, sx2, sy2;
        mapToScreen(hwnd, x, top, sx1, sy1);
        mapToScreen(hwnd, x, bottom, sx2, sy2);
        drawLine2D(sx1, sy1, sx2, sy2, color);
    }
    for (long y = firstY; y <= bottom; y += gs)
    {
        int sx1, sy1, sx2, sy2;
        mapToScreen(hwnd, left, y, sx1, sy1);
        mapToScreen(hwnd, right, y, sx2, sy2);
        drawLine2D(sx1, sy1, sx2, sy2, color);
    }
}

static void rasterize2DView(HWND hwnd)
{
    RECT rc{}; GetClientRect(hwnd, &rc);
    g_2dBuffer.resize(rc.right - rc.left, rc.bottom - rc.top);
    clear2D(makeColor(0, 0, 0));

    draw2DGrid(hwnd);

    // Draw sectors/walls.
    for (short i = 0; i < numwalls; ++i)
    {
        if (wall[i].point2 < 0 || wall[i].point2 >= numwalls) continue;
        if (wall[i].nextwall >= 0 && i > wall[i].nextwall) continue; // avoid duplicate red walls

        int x1, y1, x2, y2;
        mapToScreen(hwnd, wall[i].x, wall[i].y, x1, y1);
        mapToScreen(hwnd, wall[wall[i].point2].x, wall[wall[i].point2].y, x2, y2);

        std::uint32_t c = (wall[i].nextsector >= 0) ? makeColor(220, 40, 40) : makeColor(210, 210, 210);
        if (wall[i].cstat & 1) c = makeColor(255, 180, 40);
        const int width = (g.sel.valid() && g.sel.wall == i && (g.sel.stat == SS_WALL || g.sel.stat == SS_MASKED_WALL)) ? 3 : 1;
        drawLine2D(x1, y1, x2, y2, c, width);
    }

    // Highlight a selected 2D sector by outlining all of its walls.
    if (selectedSectorValid() && (g.sel.stat == SS_FLOOR || g.sel.stat == SS_CEILING))
    {
        const int start = sector[g.sel.sector].wallptr;
        const int end = start + sector[g.sel.sector].wallnum;
        for (int wi = start; wi < end && wi >= 0 && wi < numwalls; ++wi)
        {
            if (wall[wi].point2 < 0 || wall[wi].point2 >= numwalls) continue;
            int x1, y1, x2, y2;
            mapToScreen(hwnd, wall[wi].x, wall[wi].y, x1, y1);
            mapToScreen(hwnd, wall[wall[wi].point2].x, wall[wall[wi].point2].y, x2, y2);
            drawLine2D(x1, y1, x2, y2, makeColor(80, 255, 80), 3);
        }
    }

    // Draw vertices.
    for (short i = 0; i < numwalls; ++i)
    {
        int sx, sy;
        mapToScreen(hwnd, wall[i].x, wall[i].y, sx, sy);
        fillRect2D(sx - 2, sy - 2, sx + 3, sy + 3, makeColor(180, 180, 180));
    }

    // Draw sprites.
    for (short i = 0; i < MAXSPRITES; ++i)
    {
        if (sprite[i].statnum >= MAXSTATUS || sprite[i].sectnum >= MAXSECTORS) continue;
        int sx, sy;
        mapToScreen(hwnd, sprite[i].x, sprite[i].y, sx, sy);
        const std::uint32_t c = (g.sel.stat == SS_SPRITE && g.sel.wall == i) ? makeColor(80, 255, 80) : makeColor(80, 180, 255);
        drawLine2D(sx - 5, sy, sx + 5, sy, c, 2);
        drawLine2D(sx, sy - 5, sx, sy + 5, c, 2);
        const int ax = sx + ((sintable[(sprite[i].ang + 512) & 2047]) >> 12);
        const int ay = sy + ((sintable[sprite[i].ang & 2047]) >> 12);
        drawLine2D(sx, sy, ax, ay, c, 1);
    }

    auto drawArrow = [&](long x, long y, short a, std::uint32_t c)
        {
            int sx, sy;
            mapToScreen(hwnd, x, y, sx, sy);
            int dx = sintable[(a + 512) & 2047] >> 10;
            int dy = sintable[a & 2047] >> 10;
            drawLine2D(sx, sy, sx + dx, sy + dy, c, 2);
            drawLine2D(sx + dx, sy + dy, sx + dx - dy / 2, sy + dy + dx / 2, c, 1);
            drawLine2D(sx + dx, sy + dy, sx + dx + dy / 2, sy + dy - dx / 2, c, 1);
        };
    drawArrow(g.startx, g.starty, g.startang, makeColor(160, 120, 60));
    drawArrow(g.cam.x, g.cam.y, g.cam.ang, makeColor(255, 255, 255));

    if (g.showTags)
    {
        for (short s = 0; s < numsectors; ++s)
        {
            long ax = 0, ay = 0;
            const int start = sector[s].wallptr;
            const int end = start + sector[s].wallnum;
            if (start < 0 || end > numwalls || sector[s].wallnum <= 0) continue;
            for (int w = start; w < end; ++w) { ax += wall[w].x; ay += wall[w].y; }
            ax /= sector[s].wallnum;
            ay /= sector[s].wallnum;

            int sx, sy;
            mapToScreen(hwnd, ax, ay, sx, sy);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "S%d L%d H%d", s, sector[s].lotag, sector[s].hitag);
            drawText2D(sx - 20, sy - 8, buf, makeColor(180, 180, 80));
        }
    }

    if (g.drawingSector && !g.drawPts.empty())
    {
        for (size_t i = 0; i < g.drawPts.size(); ++i)
        {
            POINT a = g.drawPts[i];
            POINT b = (i + 1 < g.drawPts.size()) ? g.drawPts[i + 1] : g.lastMapMouse;
            int x1, y1, x2, y2;
            mapToScreen(hwnd, a.x, a.y, x1, y1);
            mapToScreen(hwnd, b.x, b.y, x2, y2);
            drawLine2D(x1, y1, x2, y2, makeColor(80, 255, 80), 2);
        }
    }

    char status[512];
    std::snprintf(status, sizeof(status), "2D POS %ld,%ld  ZOOM %ld  GRID %d%s  SECTORS %d WALLS %d  %s",
        g.lastMapMouse.x, g.lastMapMouse.y, g.zoom, g.grid,
        g.gridLock ? " LOCKED" : " FREE", numsectors, numwalls, g.status.c_str());

    // Small dark backing strip so the software-font HUD remains readable.
    fillRect2D(4, 4, min(g_2dBuffer.w - 4, 4 + static_cast<int>(std::strlen(status)) * 6 + 8), 18, makeColor(8, 8, 8));
    drawRectOutline2D(4, 4, min(g_2dBuffer.w - 4, 4 + static_cast<int>(std::strlen(status)) * 6 + 8), 18, makeColor(32, 32, 32));
    drawText2D(8, 8, status, makeColor(230, 230, 230));
}

static void draw2DView(HWND hwnd, HDC paintDC)
{
    rasterize2DView(hwnd);
    SetDIBitsToDevice(paintDC,
        0, 0,
        static_cast<DWORD>(g_2dBuffer.w),
        static_cast<DWORD>(g_2dBuffer.h),
        0, 0,
        0,
        static_cast<UINT>(g_2dBuffer.h),
        g_2dBuffer.pixels.data(),
        &g_2dBuffer.bmi,
        DIB_RGB_COLORS);
}

static void beginOrAddDrawPoint(HWND hwnd)
{
    POINT p = screenToMap(hwnd, g.lastMouse.x, g.lastMouse.y, true);
    if (!g.drawingSector)
    {
        g.drawingSector = true;
        g.drawPts.clear();
        g.drawPts.push_back(p);
        setStatus("Sector drawing started. Add points with Space/click; close with Enter.");
    }
    else
    {
        if (g.drawPts.size() >= 3)
        {
            POINT first = g.drawPts.front();
            const long closeMap = screenPixelsToMap(hwnd, 10);
            if (dist2(first.x, first.y, p.x, p.y) <= static_cast<double>(closeMap) * closeMap)
            {
                commitDrawnSector();
                return;
            }
        }
        if (g.drawPts.empty() || g.drawPts.back().x != p.x || g.drawPts.back().y != p.y)
            g.drawPts.push_back(p);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void insertPointAtMouse(HWND hwnd)
{
    double t = 0.0;
    short line = nearestLineAtScreen(hwnd, g.lastMouse.x, g.lastMouse.y, &t);
    if (line >= 0)
    {
        const walltype& a = wall[line];
        const walltype& b = wall[a.point2];
        long x = static_cast<long>(std::llround(a.x + (b.x - a.x) * t));
        long y = static_cast<long>(std::llround(a.y + (b.y - a.y) * t));
        snapPoint(x, y);
        insertPointOnWall(line, x, y);
        setStatus("Point inserted on wall %d.", line);
    }
}

static LRESULT CALLBACK View2DProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        return 0;
    case WM_TIMER:
        if (wp == ids::TIMER_2D_REFRESH)
        {
            KillTimer(hwnd, ids::TIMER_2D_REFRESH);
            g.pending2DInvalidate = false;
            g.last2DInvalidateTick = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        draw2DView(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        POINT mp = screenToMap(hwnd, p.x, p.y, false);
        g.lastMapMouse = mp;
        if (g.panning2D)
        {
            const double z = static_cast<double>(g.zoom) / 16384.0;
            if (z > 0.00001)
            {
                g.viewX -= static_cast<long>((p.x - g.lastMouse.x) / z);
                g.viewY -= static_cast<long>((p.y - g.lastMouse.y) / z);
            }
            g.lastMouse = p;
            request2DRefresh(hwnd);
        }
        else if (g.draggingPoint)
        {
            POINT sp = screenToMap(hwnd, p.x, p.y, true);
            if (moveJoinedPoint(g.dragPointWall, sp.x, sp.y))
                request2DRefresh(hwnd);
        }
        else if (g.draggingSprite)
        {
            POINT sp = screenToMap(hwnd, p.x, p.y, true);
            spritetype& spr = sprite[g.dragSprite];
            if (spr.x != sp.x || spr.y != sp.y)
            {
                spr.x = sp.x;
                spr.y = sp.y;
                setsprite(g.dragSprite, spr.x, spr.y, spr.z);
                g.sel.sector = spr.sectnum;
                g.dragSpriteMoved = true;
                markDirtyDuringDrag();
                request2DRefresh(hwnd);
            }
        }
        else if (g.drawingSector)
        {
            POINT sp = screenToMap(hwnd, p.x, p.y, true);
            if (g.lastMapMouse.x != sp.x || g.lastMapMouse.y != sp.y)
            {
                g.lastMapMouse = sp;
                request2DRefresh(hwnd);
            }
        }
        else
        {
            request2DRefresh(hwnd);
        }

        if (!g.panning2D)
            g.lastMouse = p;
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        SetCapture(hwnd);
        g.lastMouse = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        POINT mp = screenToMap(hwnd, g.lastMouse.x, g.lastMouse.y, false);
        g.lastMapMouse = mp;
        g.dragPointMoved = false;
        g.dragSpriteMoved = false;
        g.dragDirtySet = false;

        if (g.drawingSector)
        {
            beginOrAddDrawPoint(hwnd);
            return 0;
        }

        short pt = nearestPointAtScreen(hwnd, g.lastMouse.x, g.lastMouse.y);
        short spr = nearestSpriteAtScreen(hwnd, g.lastMouse.x, g.lastMouse.y);
        if (pt >= 0)
        {
            // Vertices get priority over sprites. Dense maps often have sprites
            // near wall points; letting sprites win makes vertex dragging feel
            // broken even though the point is visibly under the cursor.
            g.draggingPoint = true;
            g.dragPointWall = pt;
            g.dragOldX = wall[pt].x;
            g.dragOldY = wall[pt].y;
            g.sel.stat = SS_WALL;
            g.sel.wall = pt;
            g.sel.sector = ownerSectorOfWall(pt);
        }
        else if (spr >= 0)
        {
            g.draggingSprite = true;
            g.dragSprite = spr;
            g.sel.stat = SS_SPRITE;
            g.sel.wall = spr;
            g.sel.sector = sprite[spr].sectnum;
        }
        else
        {
            g.sel = pick2DAtScreen(hwnd, g.lastMouse.x, g.lastMouse.y, mp.x, mp.y);
            if ((GetKeyState(VK_CONTROL) & 0x8000) && g.sel.valid()) applySelectedTexture();
        }
        syncEngineSearchGlobals(g.sel);
        if (g.sel.valid())
        {
            g.selectedTile = getSelectedPic();
            centerTextureBarOnSelected();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        const bool wasDraggingPoint = g.draggingPoint;
        g.draggingPoint = false;
        g.draggingSprite = false;
        ReleaseCapture();
        if (wasDraggingPoint) finalizePointDrag();
        g.dragPointMoved = false;
        g.dragSpriteMoved = false;
        g.dragDirtySet = false;
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g.draggingPoint) finalizePointDrag();
        g.draggingPoint = false;
        g.draggingSprite = false;
        g.dragPointMoved = false;
        g.dragSpriteMoved = false;
        g.dragDirtySet = false;
        return 0;
    case WM_RBUTTONDOWN:
        g.panning2D = true;
        g.lastMouse = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        g.panning2D = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta > 0) g.zoom = min(g.zoom + (g.zoom >> 3) + 1, 65536);
        else g.zoom = max(g.zoom - (g.zoom >> 3), 24);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp)
        {
        case VK_SPACE: beginOrAddDrawPoint(hwnd); return 0;
        case VK_RETURN: if (g.drawingSector) commitDrawnSector(); else pasteProps(); return 0;
        case VK_ESCAPE: g.drawingSector = false; g.drawPts.clear(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case VK_INSERT: insertPointAtMouse(hwnd); return 0;
        case VK_DELETE: deleteSelection(); return 0;
        case 'S': insertSpriteAt(g.lastMapMouse.x, g.lastMapMouse.y); return 0;
        case 'G': g.grid = (g.grid + 1) % 7; setStatus("Grid %d", g.grid); InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case 'L': g.gridLock = !g.gridLock; setStatus("Grid lock %s", g.gridLock ? "ON" : "OFF"); InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case 'V': applySelectedTexture(); return 0;
        case VK_TAB: copyProps(); return 0;
        case 'B': toggleBlocking(); return 0;
        case 'M': toggleMasked(); return 0;
        case 'T': if (GetKeyState(VK_CONTROL) & 0x8000) { g.showTags = !g.showTags; InvalidateRect(hwnd, nullptr, FALSE); }
                else toggleTranslucent(); return 0;
        case 'P': toggleParallax(); return 0;
        case '1': toggleOneWay(); return 0;
        case VK_OEM_MINUS: shadeSelection(+1); return 0;
        case VK_OEM_PLUS: shadeSelection(-1); return 0;
        case VK_PRIOR: zAdjustSelection(-1024); return 0;
        case VK_NEXT: zAdjustSelection(+1024); return 0;
        }
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// Texture bar.
// -----------------------------------------------------------------------------
static void drawTilePreview(HDC hdc, short tile, int x, int y, int w, int h)
{
    if (tile < 0 || tile >= MAXTILES || tilesizx[tile] <= 0 || tilesizy[tile] <= 0)
    {
        Rectangle(hdc, x, y, x + w, y + h);
        return;
    }
    if (!waloff[tile]) loadtile(tile);
    const unsigned char* src = reinterpret_cast<const unsigned char*>(static_cast<std::intptr_t>(waloff[tile]));
    if (!src) return;

    const int tw = tilesizx[tile];
    const int th = tilesizy[tile];
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint32_t> pixels(static_cast<size_t>(w) * h, 0xff000000u);

    for (int py = 0; py < h; ++py)
    {
        int sy = py * th / max(1, h);
        for (int px = 0; px < w; ++px)
        {
            int sx = px * tw / max(1, w);
            unsigned char idx = src[sx * th + sy]; // ART is column-major.
            std::uint8_t r, gr, b;
            if (idx == 255)
            {
                const bool chk = (((px >> 3) ^ (py >> 3)) & 1) != 0;
                r = gr = b = chk ? 48 : 24;
            }
            else
            {
                r = static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 0]) << 2);
                gr = static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 1]) << 2);
                b = static_cast<std::uint8_t>(static_cast<unsigned char>(palette[idx * 3 + 2]) << 2);
            }
            pixels[static_cast<size_t>(py) * w + px] = 0xff000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(gr) << 8) | b;
        }
    }

    StretchDIBits(hdc, x, y, w, h, 0, 0, w, h, pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static void drawTextureBar(HWND hwnd, HDC hdc)
{
    RECT rc{}; GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(26, 26, 30));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(230, 230, 230));
    char top[512];
    std::snprintf(top, sizeof(top), "Tile %d   %dx%d   %s", g.selectedTile,
        (g.selectedTile >= 0 && g.selectedTile < MAXTILES) ? tilesizx[g.selectedTile] : 0,
        (g.selectedTile >= 0 && g.selectedTile < MAXTILES) ? tilesizy[g.selectedTile] : 0,
        g.status.c_str());
    TextOutA(hdc, 8, 6, top, static_cast<int>(std::strlen(top)));

    const int y0 = 28;
    const int rows = max(1, (rc.bottom - y0) / TILE_CELL);
    const int cols = max(1, rc.right / TILE_CELL);
    const int visible = rows * cols;
    g.tileScroll = clamp_int(g.tileScroll, 0, max(0, static_cast<int>(g.tileList.size()) - visible));

    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)));
    for (int i = 0; i < visible; ++i)
    {
        const int idx = g.tileScroll + i;
        if (idx < 0 || idx >= static_cast<int>(g.tileList.size())) break;
        const short tile = g.tileList[idx];
        const int col = i % cols;
        const int row = i / cols;
        const int x = col * TILE_CELL + 4;
        const int y = y0 + row * TILE_CELL + 2;
        RECT cell{ x - 2, y - 2, x + TILE_CELL - 6, y + TILE_CELL - 4 };
        HBRUSH cellBg = CreateSolidBrush(tile == g.selectedTile ? RGB(80, 80, 40) : RGB(42, 42, 48));
        FillRect(hdc, &cell, cellBg);
        DeleteObject(cellBg);
        drawTilePreview(hdc, tile, x + 4, y + 4, TILE_PREVIEW, TILE_PREVIEW);
        char label[32]; std::snprintf(label, sizeof(label), "%d", tile);
        SetTextColor(hdc, tile == g.selectedTile ? RGB(255, 255, 120) : RGB(210, 210, 210));
        TextOutA(hdc, x + 4, y + TILE_PREVIEW + 6, label, static_cast<int>(std::strlen(label)));
    }
    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK TextureBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        drawTextureBar(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int y0 = 28;
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        if (y < y0) return 0;
        const int cols = max(1, rc.right / TILE_CELL);
        const int col = x / TILE_CELL;
        const int row = (y - y0) / TILE_CELL;
        const int idx = g.tileScroll + row * cols + col;
        if (idx >= 0 && idx < static_cast<int>(g.tileList.size()))
        {
            g.selectedTile = g.tileList[idx];
            setStatus("Selected tile %d. Press V or Ctrl-click an object to apply.", g.selectedTile);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        SendMessage(g.main, WM_COMMAND, ids::M_TEXTURE_GOTO, 0);
        return 0;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g.tileScroll += (delta < 0) ? 4 : -4;
        g.tileScroll = clamp_int(g.tileScroll, 0, max(0, static_cast<int>(g.tileList.size()) - 1));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// Right-side selection inspector.
// -----------------------------------------------------------------------------
static void setControlFont(HWND h)
{
    if (h) SendMessageA(h, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

static HWND makeInspectorControl(HWND parent, const char* cls, const char* text, DWORD style, DWORD exStyle, int id)
{
    HWND h = CreateWindowExA(exStyle, cls, text ? text : "", WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(id), g.inst, nullptr);
    setControlFont(h);
    return h;
}

static void moveCtl(HWND h, int x, int y, int w, int ht)
{
    if (h) MoveWindow(h, x, y, max(1, w), max(1, ht), TRUE);
}

static void setEditLong(HWND h, long value)
{
    if (!h) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%ld", value);
    SetWindowTextA(h, buf);
}

static void setEditBlank(HWND h)
{
    if (h) SetWindowTextA(h, "");
}

static bool readEditLong(HWND h, const char* label, long& out)
{
    char buf[128] = {};
    GetWindowTextA(h, buf, sizeof(buf));
    char* start = buf;
    while (*start == ' ' || *start == '\t') ++start;
    char* end = nullptr;
    long v = std::strtol(start, &end, 0);
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (!start[0] || !end || *end != 0)
    {
        char msg[192];
        std::snprintf(msg, sizeof(msg), "%s must be a valid number.", label ? label : "Value");
        MessageBoxA(g.main, msg, "Selection properties", MB_ICONERROR);
        if (h) SetFocus(h);
        return false;
    }
    out = v;
    return true;
}

static void enableCtl(HWND h, bool enabled)
{
    if (h) EnableWindow(h, enabled ? TRUE : FALSE);
}

static void updateInspector()
{
    if (!g.inspector || !g.inspTitle) return;

    g.inspectorUpdating = true;
    const bool surfaceOk = selectedSurfaceValid();
    const bool sectorOk = selectedSectorValid();

    char title[160];
    if (surfaceOk)
    {
        if (g.sel.stat == SS_SPRITE)
            std::snprintf(title, sizeof(title), "Selection: %s %d", selectedSurfaceName(), g.sel.wall);
        else if (g.sel.stat == SS_WALL || g.sel.stat == SS_MASKED_WALL)
            std::snprintf(title, sizeof(title), "Selection: %s %d", selectedSurfaceName(), g.sel.wall);
        else
            std::snprintf(title, sizeof(title), "Selection: %s in sector %d", selectedSurfaceName(), g.sel.sector);
    }
    else if (sectorOk)
    {
        std::snprintf(title, sizeof(title), "Selection: sector %d", g.sel.sector);
    }
    else
    {
        std::snprintf(title, sizeof(title), "Selection: none");
    }
    SetWindowTextA(g.inspTitle, title);

    SetWindowTextA(g.inspSurfaceTitle, surfaceOk ? "Surface" : "Surface (nothing selected)");
    enableCtl(g.inspTileEdit, surfaceOk);
    enableCtl(g.inspShadeEdit, surfaceOk);
    enableCtl(g.inspPalEdit, surfaceOk);
    enableCtl(g.inspUseTileButton, surfaceOk);

    if (surfaceOk)
    {
        setEditLong(g.inspTileEdit, getSelectedPic());
        setEditLong(g.inspShadeEdit, getSelectedShadeValue());
        setEditLong(g.inspPalEdit, getSelectedPalValue());
    }
    else
    {
        setEditBlank(g.inspTileEdit);
        setEditBlank(g.inspShadeEdit);
        setEditBlank(g.inspPalEdit);
    }

    char sectorTitle[128];
    if (sectorOk) std::snprintf(sectorTitle, sizeof(sectorTitle), "Sector %d", g.sel.sector);
    else std::snprintf(sectorTitle, sizeof(sectorTitle), "Sector (nothing selected)");
    SetWindowTextA(g.inspSectorTitle, sectorTitle);

    enableCtl(g.inspVisEdit, sectorOk);
    enableCtl(g.inspLotagEdit, sectorOk);
    enableCtl(g.inspHitagEdit, sectorOk);
    enableCtl(g.inspApplyButton, surfaceOk || sectorOk);
    enableCtl(g.inspRefreshButton, true);

    if (sectorOk)
    {
        setEditLong(g.inspVisEdit, static_cast<unsigned char>(sector[g.sel.sector].visibility));
        setEditLong(g.inspLotagEdit, sector[g.sel.sector].lotag);
        setEditLong(g.inspHitagEdit, sector[g.sel.sector].hitag);
    }
    else
    {
        setEditBlank(g.inspVisEdit);
        setEditBlank(g.inspLotagEdit);
        setEditBlank(g.inspHitagEdit);
    }

    g.inspectorUpdating = false;
}

static void applyInspectorEdits(bool useCurrentTile)
{
    if (g.inspectorUpdating) return;

    const bool surfaceOk = selectedSurfaceValid();
    const bool sectorOk = selectedSectorValid();
    if (!surfaceOk && !sectorOk)
    {
        setStatus("No selected wall/floor/ceiling/sector to edit.");
        return;
    }

    bool changed = false;
    if (surfaceOk)
    {
        long tileValue = g.selectedTile;
        if (!useCurrentTile && !readEditLong(g.inspTileEdit, "Texture", tileValue)) return;
        tileValue = clamp_long(tileValue, 0, MAXTILES - 1);
        changed = setSelectedPicDirect(static_cast<short>(tileValue)) || changed;
        if (useCurrentTile) setEditLong(g.inspTileEdit, tileValue);

        long shadeValue = 0;
        if (!readEditLong(g.inspShadeEdit, "Shade", shadeValue)) return;
        changed = setSelectedShadeDirect(static_cast<int>(shadeValue)) || changed;

        long palValue = 0;
        if (!readEditLong(g.inspPalEdit, "Palette", palValue)) return;
        changed = setSelectedPalDirect(static_cast<int>(palValue)) || changed;
    }

    if (sectorOk)
    {
        sectortype& s = sector[g.sel.sector];
        long visValue = 0, loValue = 0, hiValue = 0;
        if (!readEditLong(g.inspVisEdit, "Sector visibility", visValue)) return;
        if (!readEditLong(g.inspLotagEdit, "Sector lotag", loValue)) return;
        if (!readEditLong(g.inspHitagEdit, "Sector hitag", hiValue)) return;

        const char vis = static_cast<char>(clamp_int(static_cast<int>(visValue), 0, 255));
        const short lotag = static_cast<short>(clamp_long(loValue, -32768, 65535));
        const short hitag = static_cast<short>(clamp_long(hiValue, -32768, 65535));

        if (s.visibility != vis) { s.visibility = vis; changed = true; }
        if (s.lotag != lotag) { s.lotag = lotag; changed = true; }
        if (s.hitag != hitag) { s.hitag = hitag; changed = true; }
    }

    if (changed)
    {
        setDirty();
        setStatus("Selection properties updated.");
        updateInspector();
        refreshAllViews();
    }
    else
    {
        setStatus("Selection properties unchanged.");
    }
}

static void layoutInspectorControls(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int W = max(1, rc.right - rc.left);
    const int m = 10;
    const int labelW = 72;
    const int editX = m + labelW;
    const int editW = max(52, W - editX - m);
    const int rowH = 22;
    int y = 10;

    moveCtl(g.inspTitle, m, y, W - 2 * m, 22); y += 30;
    moveCtl(g.inspSurfaceTitle, m, y, W - 2 * m, 20); y += 26;

    HWND labelTexture = GetDlgItem(hwnd, 1201);
    HWND labelShade = GetDlgItem(hwnd, 1202);
    HWND labelPal = GetDlgItem(hwnd, 1203);
    HWND labelVis = GetDlgItem(hwnd, 1204);
    HWND labelLotag = GetDlgItem(hwnd, 1205);
    HWND labelHitag = GetDlgItem(hwnd, 1206);

    moveCtl(labelTexture, m, y + 4, labelW, 18);
    moveCtl(g.inspTileEdit, editX, y, editW, rowH); y += 28;
    moveCtl(g.inspUseTileButton, m, y, W - 2 * m, 24); y += 34;
    moveCtl(labelShade, m, y + 4, labelW, 18);
    moveCtl(g.inspShadeEdit, editX, y, editW, rowH); y += 28;
    moveCtl(labelPal, m, y + 4, labelW, 18);
    moveCtl(g.inspPalEdit, editX, y, editW, rowH); y += 36;

    moveCtl(g.inspSectorTitle, m, y, W - 2 * m, 20); y += 26;
    moveCtl(labelVis, m, y + 4, labelW, 18);
    moveCtl(g.inspVisEdit, editX, y, editW, rowH); y += 28;
    moveCtl(labelLotag, m, y + 4, labelW, 18);
    moveCtl(g.inspLotagEdit, editX, y, editW, rowH); y += 28;
    moveCtl(labelHitag, m, y + 4, labelW, 18);
    moveCtl(g.inspHitagEdit, editX, y, editW, rowH); y += 36;

    const int buttonW = max(70, (W - 3 * m) / 2);
    moveCtl(g.inspApplyButton, m, y, buttonW, 26);
    moveCtl(g.inspRefreshButton, m + buttonW + m, y, W - (m + buttonW + 2 * m), 26);
}

static LRESULT CALLBACK InspectorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g.inspTitle = makeInspectorControl(hwnd, "STATIC", "Selection: none", SS_LEFT, 0, 1200);
        g.inspSurfaceTitle = makeInspectorControl(hwnd, "STATIC", "Surface", SS_LEFT, 0, 1207);
        makeInspectorControl(hwnd, "STATIC", "Texture", SS_LEFT, 0, 1201);
        g.inspTileEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_TILE_EDIT);
        g.inspUseTileButton = makeInspectorControl(hwnd, "BUTTON", "Use selected texture tile", BS_PUSHBUTTON, 0, ids::ID_INSP_USE_TILE);
        makeInspectorControl(hwnd, "STATIC", "Shade", SS_LEFT, 0, 1202);
        g.inspShadeEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_SHADE_EDIT);
        makeInspectorControl(hwnd, "STATIC", "Palette", SS_LEFT, 0, 1203);
        g.inspPalEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_PAL_EDIT);
        g.inspSectorTitle = makeInspectorControl(hwnd, "STATIC", "Sector", SS_LEFT, 0, 1208);
        makeInspectorControl(hwnd, "STATIC", "Visibility", SS_LEFT, 0, 1204);
        g.inspVisEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_VIS_EDIT);
        makeInspectorControl(hwnd, "STATIC", "Lotag", SS_LEFT, 0, 1205);
        g.inspLotagEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_LOTAG_EDIT);
        makeInspectorControl(hwnd, "STATIC", "Hitag", SS_LEFT, 0, 1206);
        g.inspHitagEdit = makeInspectorControl(hwnd, "EDIT", "", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ids::ID_INSP_HITAG_EDIT);
        g.inspApplyButton = makeInspectorControl(hwnd, "BUTTON", "Apply", BS_DEFPUSHBUTTON, 0, ids::ID_INSP_APPLY);
        g.inspRefreshButton = makeInspectorControl(hwnd, "BUTTON", "Refresh", BS_PUSHBUTTON, 0, ids::ID_INSP_REFRESH);
        layoutInspectorControls(hwnd);
        updateInspector();
        return 0;
    }
    case WM_SIZE:
        layoutInspectorControls(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ids::ID_INSP_USE_TILE:
            applyInspectorEdits(true);
            return 0;
        case ids::ID_INSP_APPLY:
            applyInspectorEdits(false);
            return 0;
        case ids::ID_INSP_REFRESH:
            updateInspector();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        return 1;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// -----------------------------------------------------------------------------
// Main window layout and commands.
// -----------------------------------------------------------------------------
static void switchTab(int tab)
{
    g.activeTab = tab ? 1 : 0;
    TabCtrl_SetCurSel(g.tab, g.activeTab);
    ShowWindow(g.view2d, g.activeTab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.host3d, g.activeTab == 1 ? SW_SHOW : SW_HIDE);
    if (g.engine) ShowWindow(g.engine, g.activeTab == 1 ? SW_SHOW : SW_HIDE);
    if (g.activeTab == 1) attachEngineWindow();
    refreshAllViews();
}

static void layoutChildren(HWND hwnd)
{
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int clientW = max(1, rc.right - rc.left);
    const int clientH = max(1, rc.bottom - rc.top);
    const int inspectorW = max(180, clientW / 5); // Right inspector: roughly 20% of the main window.
    const int editorW = max(1, clientW - inspectorW);

    const int texH = TEXTURE_BAR_HEIGHT;
    const int tabH = max(0, clientH - texH);
    MoveWindow(g.tab, 0, 0, editorW, tabH, TRUE);
    MoveWindow(g.textureBar, 0, tabH, editorW, texH, TRUE);
    if (g.inspector) MoveWindow(g.inspector, editorW, 0, inspectorW, clientH, TRUE);

    RECT tr{}; GetClientRect(g.tab, &tr);
    TabCtrl_AdjustRect(g.tab, FALSE, &tr);
    tr.left += TAB_MARGIN;
    tr.top += TAB_MARGIN;
    tr.right -= TAB_MARGIN;
    tr.bottom -= TAB_MARGIN;
    const int w = max(1, tr.right - tr.left);
    const int h = max(1, tr.bottom - tr.top);
    MoveWindow(g.view2d, tr.left, tr.top, w, h, TRUE);
    MoveWindow(g.host3d, tr.left, tr.top, w, h, TRUE);
    if (g.engine)
        SetWindowPos(g.engine, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_SHOWWINDOW);
}

static void gotoTileDialog()
{
    long v = g.selectedTile;
    if (promptLong("Goto tile", "Tile number:", v, 0, MAXTILES - 1))
    {
        g.selectedTile = static_cast<short>(v);
        if (tilesizx[g.selectedTile] <= 0 || tilesizy[g.selectedTile] <= 0)
            setStatus("Tile %d is empty, but it is selected.", g.selectedTile);
        centerTextureBarOnSelected();
    }
}

static void showKeyboardHelp()
{
    const char* msg =
        "2D view:\n"
        "  Right drag: pan, mouse wheel: zoom\n"
        "  Left drag vertex/sprite: move it\n"
        "  Space/click: begin/add sector points, Enter: close sector, Esc: cancel\n"
        "  Insert: insert point on nearest wall, S: insert sprite, Delete: delete\n"
        "  G: grid size, L: grid lock, Ctrl+T: show tags\n"
        "\n3D view:\n"
        "  W/A/S/D: move, arrows: look/turn, PgUp/PgDn: fly\n"
        "  Right drag: turn/look, left click: select wall/floor/sprite\n"
        "\nEditing:\n"
        "  Texture bar click: select tile, V/Ctrl-click: apply tile\n"
        "  Tab: copy properties, Enter: paste properties\n"
        "  B/1/M/T/P: blocking, one-way, masked, translucent, parallax\n"
        "  +/-: shade, PgUp/PgDn: adjust selected Z where applicable";
    MessageBoxA(g.main, msg, "Keyboard help", MB_ICONINFORMATION);
}

static bool mountGroupIfExists(const char* name)
{
    DWORD attr = GetFileAttributesA(name);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
    return initgroupfile(const_cast<char*>(name)) >= 0;
}

static void loadBuildAssets()
{
    mountGroupIfExists("duke3d.grp");
    mountGroupIfExists("DUKE3D.GRP");
    initengine();
    inittimerhandler();
    loadpics(const_cast<char*>("tiles000.art"));
    refreshTileList();
}

static void handleCommand(HWND hwnd, int id)
{
    switch (id)
    {
    case ids::M_FILE_NEW:
        if (!confirmDiscardChanges()) break;
        initNewMap();
        g.boardPath = "newboard.map";
        g.boardFromGrp = false;
        g.boardGrpPath.clear();
        g.boardGrpEntry.clear();
        setDirty(false);
        updateTitle();
        refreshAllViews();
        break;
    case ids::M_FILE_OPEN:
        if (confirmDiscardChanges()) promptOpenMap();
        break;
    case ids::M_FILE_OPEN_GRP:
        if (confirmDiscardChanges()) promptOpenMapFromGrp();
        break;
    case ids::M_FILE_SAVE:
        if (g.boardFromGrp || g.boardPath.empty() || g.boardPath == "newboard.map") promptSaveMap();
        else saveMapFile(g.boardPath.c_str());
        break;
    case ids::M_FILE_SAVE_AS:
        promptSaveMap();
        break;
    case ids::M_FILE_EXIT:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case ids::M_VIEW_2D:
        switchTab(0); break;
    case ids::M_VIEW_3D:
        switchTab(1); break;
    case ids::M_VIEW_ZOOM_IN:
        g.zoom = min(g.zoom + (g.zoom >> 3) + 1, 65536); refreshAllViews(); break;
    case ids::M_VIEW_ZOOM_OUT:
        g.zoom = max(g.zoom - (g.zoom >> 3), 24); refreshAllViews(); break;
    case ids::M_VIEW_GRID:
        g.grid = (g.grid + 1) % 7; refreshAllViews(); break;
    case ids::M_VIEW_GRID_LOCK:
        g.gridLock = !g.gridLock; refreshAllViews(); break;
    case ids::M_VIEW_SHOW_TAGS:
        g.showTags = !g.showTags; refreshAllViews(); break;
    case ids::M_EDIT_BEGIN_DRAW:
        beginOrAddDrawPoint(g.view2d); break;
    case ids::M_EDIT_CLOSE_DRAW:
        commitDrawnSector(); break;
    case ids::M_EDIT_CANCEL_DRAW:
        g.drawingSector = false; g.drawPts.clear(); refreshAllViews(); break;
    case ids::M_EDIT_INSERT_POINT:
        insertPointAtMouse(g.view2d); break;
    case ids::M_EDIT_INSERT_SPRITE:
        insertSpriteAt(g.lastMapMouse.x, g.lastMapMouse.y); break;
    case ids::M_EDIT_DELETE:
        deleteSelection(); break;
    case ids::M_EDIT_COPY_PROPS:
        copyProps(); break;
    case ids::M_EDIT_PASTE_PROPS:
        pasteProps(); break;
    case ids::M_EDIT_APPLY_TEXTURE:
        applySelectedTexture(); break;
    case ids::M_EDIT_PROPERTIES:
        editProperties(); break;
    case ids::M_TOOLS_RELINK:
        rebuildSectorLinks(); setDirty(); refreshAllViews(); break;
    case ids::M_TOOLS_SET_START:
        g.startx = g.cam.x; g.starty = g.cam.y; g.startz = g.cam.z; g.startang = g.cam.ang; g.startsect = g.cam.sect; setDirty(); refreshAllViews(); break;
    case ids::M_TOOLS_TOGGLE_BLOCKING:
        toggleBlocking(); break;
    case ids::M_TOOLS_TOGGLE_ONEWAY:
        toggleOneWay(); break;
    case ids::M_TOOLS_TOGGLE_MASKED:
        toggleMasked(); break;
    case ids::M_TOOLS_TOGGLE_TRANSLUCENT:
        toggleTranslucent(); break;
    case ids::M_TOOLS_TOGGLE_PARALLAX:
        toggleParallax(); break;
    case ids::M_TOOLS_SHADE_UP:
        shadeSelection(+1); break;
    case ids::M_TOOLS_SHADE_DOWN:
        shadeSelection(-1); break;
    case ids::M_TOOLS_Z_UP:
        zAdjustSelection(-1024); break;
    case ids::M_TOOLS_Z_DOWN:
        zAdjustSelection(+1024); break;
    case ids::M_TEXTURE_GOTO:
        gotoTileDialog(); break;
    case ids::M_HELP_KEYS:
        showKeyboardHelp(); break;
    }
}

static LRESULT CALLBACK Host3DProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (g.engine) SetWindowPos(g.engine, nullptr, 0, 0, LOWORD(lp), HIWORD(lp), SWP_NOZORDER | SWP_SHOWWINDOW);
        return 0;
    case WM_SETFOCUS:
        if (g.engine) SetFocus(g.engine);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_CREATE:
    {
        g.main = hwnd;
        SetMenu(hwnd, makeMainMenu());

        g.tab = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(ids::ID_TAB), g.inst, nullptr);
        TCITEMA item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<char*>("2D Editor");
        TabCtrl_InsertItem(g.tab, 0, &item);
        item.pszText = const_cast<char*>("3D Editor");
        TabCtrl_InsertItem(g.tab, 1, &item);

        g.view2d = CreateWindowExA(WS_EX_CLIENTEDGE, "BuildEditor2DView", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
            0, 0, 100, 100, g.tab, reinterpret_cast<HMENU>(ids::ID_VIEW2D), g.inst, nullptr);
        g.host3d = CreateWindowExA(WS_EX_CLIENTEDGE, "BuildEditor3DHost", "", WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP,
            0, 0, 100, 100, g.tab, reinterpret_cast<HMENU>(ids::ID_HOST3D), g.inst, nullptr);
        g.textureBar = CreateWindowExA(WS_EX_CLIENTEDGE, "BuildEditorTextureBar", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, TEXTURE_BAR_HEIGHT, hwnd, reinterpret_cast<HMENU>(ids::ID_TEXTUREBAR), g.inst, nullptr);
        g.inspector = CreateWindowExA(WS_EX_CLIENTEDGE, "BuildEditorInspector", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(ids::ID_INSPECTOR), g.inst, nullptr);
        updateInspector();

        QueryPerformanceFrequency(&g.freq);
        QueryPerformanceCounter(&g.lastTick);
        SetTimer(hwnd, ids::TIMER_RENDER, 16, nullptr);
        updateTitle();
        layoutChildren(hwnd);
        return 0;
    }
    case WM_SIZE:
        layoutChildren(hwnd);
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lp)->hwndFrom == g.tab && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE)
            switchTab(TabCtrl_GetCurSel(g.tab));
        return 0;
    case WM_TIMER:
        if (wp == ids::TIMER_RENDER) render3D();
        return 0;
    case WM_COMMAND:
        handleCommand(hwnd, LOWORD(wp));
        return 0;
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            if (wp == 'N') { handleCommand(hwnd, ids::M_FILE_NEW); return 0; }
            if (wp == 'O') { handleCommand(hwnd, ids::M_FILE_OPEN); return 0; }
            if (wp == 'S') { handleCommand(hwnd, ids::M_FILE_SAVE); return 0; }
            if (wp == 'T') { handleCommand(hwnd, ids::M_VIEW_SHOW_TAGS); return 0; }
        }
        if (wp == VK_F2) { switchTab(0); return 0; }
        if (wp == VK_F3) { switchTab(1); return 0; }
        break;
    case WM_CLOSE:
        if (!confirmDiscardChanges()) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ids::TIMER_RENDER);
        uninitengine();
        uninitgroupfile();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void registerWindowClasses(HINSTANCE inst)
{
    WNDCLASSA wc{};
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_DBLCLKS;

    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = "BuildWin32EditorMain";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassA(&wc);

    wc.lpfnWndProc = View2DProc;
    wc.lpszClassName = "BuildEditor2DView";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&wc);

    wc.style = CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = Host3DProc;
    wc.lpszClassName = "BuildEditor3DHost";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&wc);

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = TextureBarProc;
    wc.lpszClassName = "BuildEditorTextureBar";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = InspectorProc;
    wc.lpszClassName = "BuildEditorInspector";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = nullptr;
    RegisterClassA(&wc);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int nCmdShow)
{
    g.inst = hInst;
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    registerWindowClasses(hInst);
    loadBuildAssets();

    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string initialMap;
    if (argc >= 2 && argvW && argvW[1])
    {
        char mb[MAX_PATH] = {};
        WideCharToMultiByte(CP_ACP, 0, argvW[1], -1, mb, sizeof(mb), nullptr, nullptr);
        initialMap = mb;
    }
    if (argvW) LocalFree(argvW);

    bool initialLoaded = false;
    if (!initialMap.empty()) initialLoaded = loadMapFile(initialMap.c_str());
    else initNewMap();

    HWND hwnd = CreateWindowExA(0, "BuildWin32EditorMain", "BuildX",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    g.main = hwnd;
    updateTitle();
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (!initialMap.empty() && initialLoaded)
        updateTitle();

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return static_cast<int>(msg.wParam);
}
