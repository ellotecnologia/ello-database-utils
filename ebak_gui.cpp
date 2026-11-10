// ebakw - versao grafica do ebak: backup e restauracao de bancos Ello acompanhando o progresso em uma janela.
//
//   ebakw backup  <banco.ello> [saida.ebk]
//   ebakw restore <backup.ebk|backup.fbk> [saida.ello]
//
// Mesmos argumentos do ebak.exe; a operacao comeca assim que a janela abre. A logica fica em ebak_core.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <thread>

#include "ebak_core.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
    WM_APP_LOG = WM_APP + 1,  // ha linhas novas na fila g_logQueue
    WM_APP_STAGE,             // lParam: std::wstring*
    WM_APP_WATCH,             // lParam: std::wstring*
    WM_APP_DONE               // wParam: codigo de retorno
};

enum { ID_STAGE = 100, ID_PROGRESS, ID_INFO, ID_LOG, ID_CLOSE, ID_TIMER = 1 };

static HWND g_hwnd, g_title, g_stage, g_progress, g_info, g_log, g_close;
static HFONT g_font = nullptr;
static bool g_running = true;
static int g_exitCode = 1;
static DWORD g_startTick = 0;
static std::wstring g_watched;  // somente acessada pela thread de interface

static std::wstring g_action, g_input, g_output;

static std::mutex g_logMutex;
static std::vector<std::wstring> g_logQueue;
static std::atomic<bool> g_logPosted(false);

// Reporter executado na thread de trabalho: so envia mensagens para a janela
struct WindowReporter : Reporter {
    static void post(UINT msg, const std::wstring& s) { PostMessageW(g_hwnd, msg, 0, (LPARAM) new std::wstring(s)); }
    void log(const std::wstring& line) override
    {
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            g_logQueue.push_back(line);
        }
        // Uma unica mensagem pendente por vez: o gbak gera milhares de linhas e inundar a fila de mensagens
        // da janela impediria o WM_TIMER (tempo decorrido) de disparar.
        if (!g_logPosted.exchange(true)) PostMessageW(g_hwnd, WM_APP_LOG, 0, 0);
    }
    void stage(const std::wstring& title) override { post(WM_APP_STAGE, title); }
    void watchFile(const std::wstring& path) override { post(WM_APP_WATCH, path); }
};

static void worker()
{
    WindowReporter r;
    int rc = g_action == L"backup" ? ebakBackup(g_input, g_output, r) : ebakRestore(g_input, g_output, r);
    PostMessageW(g_hwnd, WM_APP_DONE, rc, 0);
}

static const int MAX_LOG_LINES = 5000;  // limite do listbox e de memoria: mantem apenas as ultimas linhas
static int g_logExtent = 0;
static size_t g_logLongest = 0;

static void drainLog()
{
    std::vector<std::wstring> lines;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        lines.swap(g_logQueue);
    }
    if (lines.empty()) return;

    SendMessageW(g_log, WM_SETREDRAW, FALSE, 0);
    HDC dc = GetDC(g_log);
    HGDIOBJ old = SelectObject(dc, g_font);
    for (auto& line : lines) {
        for (auto& c : line)
            if (c == 10 || c == 13) c = L' ';
        SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        if (line.size() > g_logLongest) {  // so mede linhas mais longas que as ja medidas
            g_logLongest = line.size();
            SIZE sz = {0, 0};
            GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
            if (sz.cx + 8 > g_logExtent) g_logExtent = sz.cx + 8;
        }
    }
    SelectObject(dc, old);
    ReleaseDC(g_log, dc);
    SendMessageW(g_log, LB_SETHORIZONTALEXTENT, g_logExtent, 0);

    int count = (int)SendMessageW(g_log, LB_GETCOUNT, 0, 0);
    while (count > MAX_LOG_LINES) {
        SendMessageW(g_log, LB_DELETESTRING, 0, 0);
        --count;
    }
    SendMessageW(g_log, LB_SETTOPINDEX, count > 0 ? count - 1 : 0, 0);
    SendMessageW(g_log, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_log, nullptr, TRUE);
}

static std::wstring formatSize(unsigned __int64 bytes)
{
    wchar_t buf[64];
    if (bytes >= (1ull << 30)) swprintf_s(buf, L"%.2f GB", bytes / double(1ull << 30));
    else swprintf_s(buf, L"%.1f MB", bytes / double(1ull << 20));
    return buf;
}

static void updateInfo()
{
    DWORD secs = (GetTickCount() - g_startTick) / 1000;
    wchar_t buf[128];
    swprintf_s(buf, L"Tempo decorrido: %02u:%02u:%02u", secs / 3600, (secs / 60) % 60, secs % 60);
    std::wstring text = buf;

    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!g_watched.empty() && GetFileAttributesExW(g_watched.c_str(), GetFileExInfoStandard, &fa)) {
        unsigned __int64 size = ((unsigned __int64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        text += L"      Arquivo gerado: " + formatSize(size);
    }
    SetWindowTextW(g_info, text.c_str());
}

static void finish(int rc)
{
    drainLog();
    g_running = false;
    g_exitCode = rc;
    KillTimer(g_hwnd, ID_TIMER);
    updateInfo();

    SendMessageW(g_progress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongPtrW(g_progress, GWL_STYLE, GetWindowLongPtrW(g_progress, GWL_STYLE) & ~PBS_MARQUEE);
    SendMessageW(g_progress, PBM_SETRANGE32, 0, 100);
    SendMessageW(g_progress, PBM_SETPOS, 100, 0);
    if (rc == 0) {
        SetWindowTextW(g_stage, L"Conclu\u00eddo com sucesso.");
    } else {
        SendMessageW(g_progress, PBM_SETSTATE, PBST_ERROR, 0);
        SetWindowTextW(g_stage, rc == 1 ? L"Argumentos inv\u00e1lidos." : L"A opera\u00e7\u00e3o terminou com erros. Veja o log abaixo.");
    }
    EnableWindow(g_close, TRUE);
    SetFocus(g_close);
}

static HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                        DWORD exStyle = 0)
{
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        std::wstring title = (g_action == L"backup" ? L"Backup de: " : L"Restaura\u00e7\u00e3o de: ") + g_input;
        g_title = makeControl(hwnd, L"STATIC", title.c_str(), SS_PATHELLIPSIS, 12, 12, 536, 18, -1);
        g_stage = makeControl(hwnd, L"STATIC", L"Iniciando...", 0, 12, 36, 536, 18, ID_STAGE);
        g_progress = makeControl(hwnd, PROGRESS_CLASSW, L"", PBS_MARQUEE, 12, 60, 536, 20, ID_PROGRESS);
        g_info = makeControl(hwnd, L"STATIC", L"", 0, 12, 88, 536, 18, ID_INFO);
        g_log = makeControl(hwnd, L"LISTBOX", L"", WS_VSCROLL | WS_HSCROLL | LBS_NOSEL | LBS_NOINTEGRALHEIGHT, 12, 114, 536, 242,
                            ID_LOG, WS_EX_CLIENTEDGE);
        g_close = makeControl(hwnd, L"BUTTON", L"Fechar", WS_TABSTOP | BS_DEFPUSHBUTTON, 448, 366, 100, 28, ID_CLOSE);
        EnableWindow(g_close, FALSE);

        NONCLIENTMETRICSW ncm = {sizeof(ncm)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        EnumChildWindows(
            hwnd, [](HWND h, LPARAM f) -> BOOL { SendMessageW(h, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)g_font);

        SendMessageW(g_progress, PBM_SETMARQUEE, TRUE, 30);
        g_startTick = GetTickCount();
        SetTimer(hwnd, ID_TIMER, 500, nullptr);
        updateInfo();
        std::thread(worker).detach();
        return 0;
    }

    case WM_TIMER:
        updateInfo();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_CLOSE && !g_running) DestroyWindow(hwnd);
        return 0;

    case WM_APP_LOG:
        g_logPosted = false;
        drainLog();
        updateInfo();  // atualiza o tempo a cada lote, mesmo que o WM_TIMER esteja sendo adiado
        return 0;
    case WM_APP_STAGE: {
        std::unique_ptr<std::wstring> s((std::wstring*)lp);
        SetWindowTextW(g_stage, s->c_str());
        return 0;
    }
    case WM_APP_WATCH: {
        std::unique_ptr<std::wstring> s((std::wstring*)lp);
        g_watched = *s;
        updateInfo();
        return 0;
    }
    case WM_APP_DONE:
        finish((int)wp);
        return 0;

    case WM_CLOSE:
        if (g_running) {
            // O backup/restore roda no servidor Firebird e nao pode ser interrompido com seguranca por aqui
            MessageBoxW(hwnd, L"A opera\u00e7\u00e3o est\u00e1 em andamento no servidor e n\u00e3o pode ser interrompida com seguran\u00e7a. Aguarde a conclus\u00e3o.",
                        L"ebak", MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int run()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool ok = argv && argc >= 3 && argc <= 4 && (std::wstring(argv[1]) == L"backup" || std::wstring(argv[1]) == L"restore");
    if (!ok) {
        MessageBoxW(nullptr,
                    L"Uso:\n  ebakw backup  <banco.ello> [saida.ebk]\n  ebakw restore <backup.ebk | backup.fbk> [saida.ello]",
                    L"ebak", MB_ICONINFORMATION);
        if (argv) LocalFree(argv);
        return 1;
    }
    g_action = argv[1];
    g_input = argv[2];
    if (argc == 4) g_output = argv[3];
    LocalFree(argv);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EloEbakWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, 560, 406};
    AdjustWindowRect(&rc, style, FALSE);
    std::wstring caption = g_action == L"backup" ? L"Backup - Ello" : L"Restaura\u00e7\u00e3o - Ello";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, caption.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                                rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return g_exitCode;
}

// O Makefile usa /ENTRY:mainCRTStartup (veja fbclient_x86.rsp)
int main() { return run(); }
