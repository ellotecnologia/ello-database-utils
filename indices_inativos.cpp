// Analisa um banco de dados Ello (Firebird) e lista os indices inativos (RDB$INDEX_INACTIVE = 1).
// Permite reativar os indices selecionados (ALTER INDEX ... ACTIVE). Veja o Makefile para compilar (MSVC x86).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <ibase.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static std::wstring widen(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// Firebird
// ---------------------------------------------------------------------------------------------------------------------

struct FbError {
    std::string msg;
};

[[noreturn]] static void fbThrow(const ISC_STATUS* status)
{
    std::string out;
    const ISC_STATUS* p = status;
    char buf[512];
    while (fb_interpret(buf, sizeof(buf), &p)) {
        if (!out.empty()) out += "\n";
        out += buf;
    }
    throw FbError{out};
}

class FbConnection {
public:
    isc_db_handle db = 0;
    isc_tr_handle trans = 0;

    explicit FbConnection(const std::string& database)
    {
        ISC_STATUS_ARRAY status;
        const char* envUser = getenv("ISC_USER");
        const char* envPass = getenv("ISC_PASSWORD");
        std::string user = envUser && *envUser ? envUser : "sysdba";
        std::string pass = envPass && *envPass ? envPass : "masterkey";
        std::string dpb;
        dpb += (char)isc_dpb_version1;
        dpb += (char)isc_dpb_user_name;
        dpb += (char)user.size();
        dpb += user;
        dpb += (char)isc_dpb_password;
        dpb += (char)pass.size();
        dpb += pass;

        if (isc_attach_database(status, 0, database.c_str(), &db, (short)dpb.size(), dpb.data())) fbThrow(status);
    }

    ~FbConnection()
    {
        ISC_STATUS_ARRAY status;
        if (trans) isc_rollback_transaction(status, &trans);
        if (db) isc_detach_database(status, &db);
    }

    void begin()
    {
        ISC_STATUS_ARRAY status;
        if (isc_start_transaction(status, &trans, 1, &db, 0, nullptr)) fbThrow(status);
    }

    void commit()
    {
        ISC_STATUS_ARRAY status;
        if (isc_commit_transaction(status, &trans)) fbThrow(status);
        trans = 0;
    }

    void rollback()
    {
        ISC_STATUS_ARRAY status;
        isc_rollback_transaction(status, &trans);
        trans = 0;
    }

    void execute(const std::string& sql)
    {
        ISC_STATUS_ARRAY status;
        if (isc_dsql_execute_immediate(status, &db, &trans, 0, sql.c_str(), SQL_DIALECT_V6, nullptr)) fbThrow(status);
    }

    FbConnection(const FbConnection&) = delete;
    FbConnection& operator=(const FbConnection&) = delete;
};

struct IndexInfo {
    std::wstring name, table, kind;
};

struct VarChar {
    short len;
    char str[65];
};

static std::vector<IndexInfo> listInactiveIndexes(FbConnection& conn)
{
    static const char sql[] =
        "SELECT CAST(TRIM(i.RDB$INDEX_NAME) AS VARCHAR(64)), CAST(TRIM(i.RDB$RELATION_NAME) AS VARCHAR(64)), "
        "CAST(COALESCE(TRIM(rc.RDB$CONSTRAINT_TYPE), '') AS VARCHAR(64)) "
        "FROM RDB$INDICES i "
        "LEFT JOIN RDB$RELATION_CONSTRAINTS rc ON rc.RDB$INDEX_NAME = i.RDB$INDEX_NAME "
        "WHERE i.RDB$INDEX_INACTIVE = 1 AND COALESCE(i.RDB$SYSTEM_FLAG, 0) = 0 "
        "ORDER BY i.RDB$RELATION_NAME, i.RDB$INDEX_NAME";

    ISC_STATUS_ARRAY status;
    isc_stmt_handle st = 0;
    XSQLDA* da = (XSQLDA*)calloc(1, XSQLDA_LENGTH(3));
    da->version = SQLDA_VERSION1;
    da->sqln = 3;

    std::vector<IndexInfo> out;
    VarChar v[3];
    short ind[3] = {0, 0, 0};
    try {
        if (isc_dsql_allocate_statement(status, &conn.db, &st)) fbThrow(status);
        if (isc_dsql_prepare(status, &conn.trans, &st, 0, sql, SQL_DIALECT_V6, da)) fbThrow(status);
        if (da->sqld != 3) throw FbError{"Consulta retornou um numero inesperado de colunas."};
        for (int i = 0; i < 3; i++) {
            da->sqlvar[i].sqltype = SQL_VARYING + 1;
            da->sqlvar[i].sqllen = 64;
            da->sqlvar[i].sqldata = (char*)&v[i];
            da->sqlvar[i].sqlind = &ind[i];
        }
        if (isc_dsql_execute(status, &conn.trans, &st, SQL_DIALECT_V6, nullptr)) fbThrow(status);
        for (;;) {
            ISC_STATUS rc = isc_dsql_fetch(status, &st, SQL_DIALECT_V6, da);
            if (rc == 100) break;
            if (rc) fbThrow(status);
            IndexInfo info;
            info.name = widen(std::string(v[0].str, v[0].len));
            info.table = widen(std::string(v[1].str, v[1].len));
            std::string t(v[2].str, v[2].len);
            if (t == "PRIMARY KEY") info.kind = L"Chave primária";
            else if (t == "FOREIGN KEY") info.kind = L"Chave estrangeira";
            else if (t == "UNIQUE") info.kind = L"Único";
            else info.kind = L"Comum";
            out.push_back(info);
        }
    } catch (...) {
        ISC_STATUS_ARRAY s2;
        if (st) isc_dsql_free_statement(s2, &st, DSQL_drop);
        free(da);
        throw;
    }
    ISC_STATUS_ARRAY s2;
    isc_dsql_free_statement(s2, &st, DSQL_drop);
    free(da);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------------------------------------------------

enum { ID_DB_BROWSE = 101, ID_ANALYZE, ID_ACTIVATE_SEL, ID_ACTIVATE_ALL, ID_DB_EDIT, ID_LIST, ID_STATUS };

static const wchar_t kTitle[] = L"Índices inativos - Ello";
static HWND g_hwnd, g_dbEdit, g_list, g_status, g_btnAnalyze, g_btnSel, g_btnAll;
static HFONT g_font;
static std::vector<IndexInfo> g_indexes;
static std::string g_database;

static void setStatus(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }

static void fillList()
{
    ListView_DeleteAllItems(g_list);
    for (size_t i = 0; i < g_indexes.size(); i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)g_indexes[i].table.c_str();
        int idx = (int)SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        LVITEMW sub = {};
        sub.iSubItem = 1;
        sub.pszText = (LPWSTR)g_indexes[i].name.c_str();
        SendMessageW(g_list, LVM_SETITEMTEXTW, idx, (LPARAM)&sub);
        sub.iSubItem = 2;
        sub.pszText = (LPWSTR)g_indexes[i].kind.c_str();
        SendMessageW(g_list, LVM_SETITEMTEXTW, idx, (LPARAM)&sub);
    }
    BOOL any = !g_indexes.empty();
    EnableWindow(g_btnSel, any);
    EnableWindow(g_btnAll, any);
}

static std::string currentDatabase()
{
    wchar_t buf[MAX_PATH * 2];
    GetWindowTextW(g_dbEdit, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    std::wstring w = buf;
    size_t a = w.find_first_not_of(L" \t\"");
    if (a == std::wstring::npos) return "";
    size_t b = w.find_last_not_of(L" \t\"");
    return narrow(w.substr(a, b - a + 1));
}

static void analyze(HWND hwnd)
{
    g_database = currentDatabase();
    if (g_database.empty()) {
        MessageBoxW(hwnd, L"Informe o banco de dados.", kTitle, MB_ICONWARNING);
        return;
    }
    setStatus(L"Analisando...");
    UpdateWindow(hwnd);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    try {
        FbConnection conn(g_database);
        conn.begin();
        g_indexes = listInactiveIndexes(conn);
        conn.rollback();
    } catch (const FbError& e) {
        g_indexes.clear();
        fillList();
        setStatus(L"Erro ao analisar o banco.");
        MessageBoxW(hwnd, widen(e.msg).c_str(), kTitle, MB_ICONERROR);
        return;
    }
    fillList();
    wchar_t buf[128];
    if (g_indexes.empty()) {
        setStatus(L"Nenhum índice inativo encontrado.");
    } else {
        swprintf_s(buf, L"%d índice(s) inativo(s) encontrado(s).", (int)g_indexes.size());
        setStatus(buf);
    }
}

static void activate(HWND hwnd, bool all)
{
    std::vector<size_t> sel;
    for (size_t i = 0; i < g_indexes.size(); i++)
        if (all || ListView_GetItemState(g_list, (int)i, LVIS_SELECTED) & LVIS_SELECTED) sel.push_back(i);
    if (sel.empty()) {
        MessageBoxW(hwnd, L"Selecione ao menos um índice.", kTitle, MB_ICONINFORMATION);
        return;
    }
    wchar_t q[256];
    swprintf_s(q, L"Reativar %d índice(s)?\n\nA reativação reconstrói o índice e pode demorar em tabelas grandes. "
                   L"Faça isso sem usuários conectados ao banco.",
               (int)sel.size());
    if (MessageBoxW(hwnd, q, kTitle, MB_ICONQUESTION | MB_YESNO) != IDYES) return;

    int ok = 0;
    std::wstring errors;
    try {
        FbConnection conn(g_database);
        for (size_t n = 0; n < sel.size(); n++) {
            const IndexInfo& info = g_indexes[sel[n]];
            wchar_t buf[256];
            swprintf_s(buf, L"Reativando %d de %d: %s", (int)n + 1, (int)sel.size(), info.name.c_str());
            setStatus(buf);
            UpdateWindow(hwnd);
            try {
                conn.begin();
                conn.execute("ALTER INDEX \"" + narrow(info.name) + "\" ACTIVE");
                conn.commit();
                ok++;
            } catch (const FbError& e) {
                if (conn.trans) conn.rollback();
                errors += info.name + L": " + widen(e.msg) + L"\n";
            }
        }
    } catch (const FbError& e) {
        errors += widen(e.msg) + L"\n";
    }

    analyze(hwnd);
    wchar_t msg[128];
    swprintf_s(msg, L"%d índice(s) reativado(s).", ok);
    if (errors.empty()) {
        MessageBoxW(hwnd, msg, kTitle, MB_ICONINFORMATION);
    } else {
        std::wstring full = std::wstring(msg) + L"\n\nFalhas:\n" + errors;
        MessageBoxW(hwnd, full.c_str(), kTitle, MB_ICONWARNING);
    }
}

static void browseDatabase(HWND hwnd)
{
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Banco Ello (*.ello)\0*.ello\0Firebird (*.fdb;*.gdb)\0*.fdb;*.gdb\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_dbEdit, file);
        analyze(hwnd);
    }
}

static BOOL CALLBACK setFontProc(HWND child, LPARAM font)
{
    SendMessageW(child, WM_SETFONT, font, TRUE);
    return TRUE;
}

static void layout(int w, int h)
{
    const int m = 12, btnW = 110, btnH = 26;
    MoveWindow(g_dbEdit, m, 32, w - 2 * m - 90 - 8, 24, TRUE);
    MoveWindow(GetDlgItem(g_hwnd, ID_DB_BROWSE), w - m - 90, 31, 90, 26, TRUE);
    MoveWindow(g_list, m, 68, w - 2 * m, h - 68 - 78, TRUE);
    MoveWindow(g_status, m, h - 70, w - 2 * m, 18, TRUE);
    MoveWindow(g_btnAnalyze, m, h - 40, btnW, btnH, TRUE);
    MoveWindow(g_btnAll, w - m - btnW, h - 40, btnW, btnH, TRUE);
    MoveWindow(g_btnSel, w - m - btnW - 8 - 160, h - 40, 160, btnH, TRUE);
}

static HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

static void createControls(HWND hwnd)
{
    mk(hwnd, L"STATIC", L"Banco de dados:", 0, -1);
    MoveWindow(GetWindow(hwnd, GW_CHILD), 12, 12, 300, 16, TRUE);
    g_dbEdit = mk(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_DB_EDIT, WS_EX_CLIENTEDGE);
    mk(hwnd, L"BUTTON", L"Procurar...", WS_TABSTOP | BS_PUSHBUTTON, ID_DB_BROWSE);
    g_list = mk(hwnd, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, ID_LIST, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cols[] = {L"Tabela", L"Índice", L"Tipo"};
    int widths[] = {200, 230, 100};
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        SendMessageW(g_list, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
    }
    g_status = mk(hwnd, L"STATIC", L"", 0, ID_STATUS);
    g_btnAnalyze = mk(hwnd, L"BUTTON", L"Analisar", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_ANALYZE);
    g_btnSel = mk(hwnd, L"BUTTON", L"Reativar selecionados", WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, ID_ACTIVATE_SEL);
    g_btnAll = mk(hwnd, L"BUTTON", L"Reativar todos", WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, ID_ACTIVATE_ALL);

    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    EnumChildWindows(hwnd, setFontProc, (LPARAM)g_font);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        createControls(hwnd);
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) layout(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize = {620, 360};
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_DB_BROWSE: browseDatabase(hwnd); break;
        case ID_ANALYZE: analyze(hwnd); break;
        case ID_ACTIVATE_SEL: activate(hwnd, false); break;
        case ID_ACTIVATE_ALL: activate(hwnd, true); break;
        }
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
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EloIndicesInativos";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 680, 460,
                                nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            SetWindowTextW(g_dbEdit, argv[1]);
            analyze(hwnd);
        }
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

// O Makefile usa /ENTRY:mainCRTStartup (veja fbclient_x86.rsp)
int main() { return run(); }
