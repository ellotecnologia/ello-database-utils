// Exportador de XMLs de DF-e de um banco de dados Ello (Firebird) - interface grafica Win32.
// Equivalente ao antigo exportador_xml.py. Veja o Makefile para as instrucoes de compilacao (MSVC x86).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <ibase.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------------------------------------------------
// Utilitarios
// ---------------------------------------------------------------------------------------------------------------------

static std::wstring widen(const std::string& s, UINT cp = CP_ACP)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w, UINT cp = CP_ACP)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Firebird
// ---------------------------------------------------------------------------------------------------------------------

static std::string fbError(const ISC_STATUS* status)
{
    std::string out;
    const ISC_STATUS* p = status;
    char buf[512];
    while (fb_interpret(buf, sizeof(buf), &p)) {
        if (!out.empty()) out += "\n";
        out += buf;
    }
    return out;
}

struct FbError {
    std::string msg;
};

[[noreturn]] static void fbThrow(const ISC_STATUS* status) { throw FbError{fbError(status)}; }

class FbConnection {
public:
    isc_db_handle db = 0;
    isc_tr_handle trans = 0;

    explicit FbConnection(const std::string& database)
    {
        ISC_STATUS_ARRAY status;
        const char user[] = "sysdba";
        const char pass[] = "masterkey";
        std::string dpb;
        dpb += (char)isc_dpb_version1;
        dpb += (char)isc_dpb_user_name;
        dpb += (char)(sizeof(user) - 1);
        dpb += user;
        dpb += (char)isc_dpb_password;
        dpb += (char)(sizeof(pass) - 1);
        dpb += pass;

        if (isc_attach_database(status, 0, database.c_str(), &db, (short)dpb.size(), dpb.data())) fbThrow(status);
        if (isc_start_transaction(status, &trans, 1, &db, 0, nullptr)) {
            ISC_STATUS_ARRAY s2;
            isc_detach_database(s2, &db);
            fbThrow(status);
        }
    }

    ~FbConnection()
    {
        ISC_STATUS_ARRAY status;
        if (trans) isc_rollback_transaction(status, &trans);
        if (db) isc_detach_database(status, &db);
    }

    void commit()
    {
        ISC_STATUS_ARRAY status;
        if (isc_commit_transaction(status, &trans)) fbThrow(status);
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

struct SqldaDeleter {
    void operator()(XSQLDA* p) const { free(p); }
};
typedef std::unique_ptr<XSQLDA, SqldaDeleter> SqldaPtr;

static SqldaPtr newSqlda(int vars)
{
    XSQLDA* da = (XSQLDA*)calloc(1, XSQLDA_LENGTH(vars));
    da->version = SQLDA_VERSION1;
    da->sqln = (short)vars;
    return SqldaPtr(da);
}

class FbStatement {
public:
    isc_stmt_handle h = 0;
    FbConnection& conn;

    FbStatement(FbConnection& c, const std::string& sql, XSQLDA* out) : conn(c)
    {
        ISC_STATUS_ARRAY status;
        if (isc_dsql_allocate_statement(status, &conn.db, &h)) fbThrow(status);
        if (isc_dsql_prepare(status, &conn.trans, &h, 0, sql.c_str(), SQL_DIALECT_V6, out)) fbThrow(status);
    }

    ~FbStatement()
    {
        ISC_STATUS_ARRAY status;
        if (h) isc_dsql_free_statement(status, &h, DSQL_drop);
    }

    void execute()
    {
        ISC_STATUS_ARRAY status;
        if (isc_dsql_execute(status, &conn.trans, &h, SQL_DIALECT_V6, nullptr)) fbThrow(status);
    }

    // true se obteve uma linha, false ao fim do cursor
    bool fetch(XSQLDA* da)
    {
        ISC_STATUS_ARRAY status;
        ISC_STATUS rc = isc_dsql_fetch(status, &h, SQL_DIALECT_V6, da);
        if (rc == 100) return false;
        if (rc) fbThrow(status);
        return true;
    }

    FbStatement(const FbStatement&) = delete;
    FbStatement& operator=(const FbStatement&) = delete;
};

static std::string readBlob(FbConnection& conn, ISC_QUAD* id)
{
    ISC_STATUS_ARRAY status;
    isc_blob_handle blob = 0;
    if (isc_open_blob2(status, &conn.db, &conn.trans, &blob, id, 0, nullptr)) fbThrow(status);

    std::string data;
    char buf[32000];
    for (;;) {
        unsigned short actual = 0;
        ISC_STATUS rc = isc_get_segment(status, &blob, &actual, sizeof(buf), buf);
        if (rc == 0 || status[1] == isc_segment) {
            data.append(buf, actual);
            continue;
        }
        if (status[1] == isc_segstr_eof) break;

        std::string err = fbError(status);
        ISC_STATUS_ARRAY s2;
        isc_close_blob(s2, &blob);
        throw FbError{err};
    }
    ISC_STATUS_ARRAY s2;
    isc_close_blob(s2, &blob);
    return data;
}

static __int64 countRows(FbConnection& conn, const std::string& sql)
{
    SqldaPtr da = newSqlda(1);
    FbStatement st(conn, sql, da.get());

    __int64 value = 0;
    short ind = 0;
    da->sqlvar[0].sqltype = SQL_INT64 + 1;
    da->sqlvar[0].sqlscale = 0;
    da->sqlvar[0].sqllen = sizeof(value);
    da->sqlvar[0].sqldata = (char*)&value;
    da->sqlvar[0].sqlind = &ind;

    st.execute();
    st.fetch(da.get());
    return value;
}

// ---------------------------------------------------------------------------------------------------------------------
// Exportacao (executada em thread separada)
// ---------------------------------------------------------------------------------------------------------------------

enum {
    WM_APP_LOG = WM_APP + 1,   // lParam: std::wstring* (dono: receptor)
    WM_APP_PROGRESS,           // wParam: atual, lParam: total
    WM_APP_DONE                // wParam: resultado (DoneResult), lParam: quantidade exportada
};

enum DoneResult { DONE_OK = 0, DONE_ERROR = 1, DONE_CANCELLED = 2 };

struct ExportParams {
    std::string database;
    std::wstring dir;
    std::string dateFrom, dateTo, model;
    bool removeXml = true;
};

static HWND g_hwnd = nullptr;
static std::atomic<bool> g_cancel(false);

static void postLog(const std::wstring& text)
{
    PostMessageW(g_hwnd, WM_APP_LOG, 0, (LPARAM) new std::wstring(text));
}

static std::wstring sanitizeFileName(const std::string& s)
{
    std::wstring w = widen(s);
    for (auto& c : w)
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
    return w;
}

static bool writeFile(const std::wstring& path, const std::string& data)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t off = 0;
    while (off < data.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(data.size() - off, 1 << 20), written = 0;
        if (!WriteFile(f, data.data() + off, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        off += written;
    }
    if (!CloseHandle(f)) ok = false;
    return ok;
}

static void exportXmls(const ExportParams& p)
{
    int exported = 0;
    DoneResult result = DONE_OK;

    try {
        int err = SHCreateDirectoryExW(nullptr, p.dir.c_str(), nullptr);
        if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS)
            throw FbError{"Nao foi possivel criar a pasta de destino."};

        postLog(L"Conectando ao banco de dados...");
        FbConnection conn(p.database);

        std::string from = "FROM tnfenota WHERE dataemissao BETWEEN '" + p.dateFrom + "' AND '" + p.dateTo +
                           "' AND modelo='" + p.model + "'";

        __int64 total = countRows(conn, "SELECT COUNT(*) " + from + " AND chave IS NOT NULL AND xml_nfe IS NOT NULL");
        PostMessageW(g_hwnd, WM_APP_PROGRESS, 0, (LPARAM)total);
        postLog(L"Iniciando exportacao, aguarde...");

        {  // escopo do cursor: precisa ser liberado antes do commit
        struct {
            short len;
            char str[65];
        } chave;
        __int64 empresa = 0, idNota = 0;
        ISC_QUAD xmlId = {0, 0};
        short ind[4] = {0, 0, 0, 0};

        SqldaPtr da = newSqlda(4);
        FbStatement st(conn, "SELECT CAST(empresa AS BIGINT), CAST(idnota AS BIGINT), CAST(chave AS VARCHAR(64)), xml_nfe " + from,
                       da.get());
        if (da->sqld != 4) throw FbError{"Consulta retornou um numero inesperado de colunas."};

        da->sqlvar[0].sqltype = SQL_INT64 + 1;
        da->sqlvar[0].sqlscale = 0;
        da->sqlvar[0].sqllen = sizeof(__int64);
        da->sqlvar[0].sqldata = (char*)&empresa;
        da->sqlvar[0].sqlind = &ind[0];
        da->sqlvar[1].sqltype = SQL_INT64 + 1;
        da->sqlvar[1].sqlscale = 0;
        da->sqlvar[1].sqllen = sizeof(__int64);
        da->sqlvar[1].sqldata = (char*)&idNota;
        da->sqlvar[1].sqlind = &ind[1];
        da->sqlvar[2].sqltype = SQL_VARYING + 1;
        da->sqlvar[2].sqllen = 64;
        da->sqlvar[2].sqldata = (char*)&chave;
        da->sqlvar[2].sqlind = &ind[2];
        da->sqlvar[3].sqltype = SQL_BLOB + 1;
        da->sqlvar[3].sqllen = sizeof(ISC_QUAD);
        da->sqlvar[3].sqldata = (char*)&xmlId;
        da->sqlvar[3].sqlind = &ind[3];

        st.execute();

        int processed = 0;
        while (st.fetch(da.get())) {
            if (g_cancel) {
                result = DONE_CANCELLED;
                break;
            }
            if (ind[2] < 0 || ind[3] < 0) continue;  // sem chave ou sem XML

            std::string key(chave.str, chave.len);
            postLog(L"Salvando XML: " + widen(key));
            ++processed;
            PostMessageW(g_hwnd, WM_APP_PROGRESS, processed, (LPARAM)total);

            try {
                std::string xml = readBlob(conn, &xmlId);
                std::wstring path = p.dir + L"\\" + sanitizeFileName(key) + L"-nfe.xml";
                if (!writeFile(path, xml)) throw FbError{"falha ao gravar o arquivo"};
                if (p.removeXml) {
                    conn.execute("UPDATE TNFeNota SET XML_NFE=NULL WHERE Empresa=" + std::to_string(empresa) +
                                 " AND IdNota=" + std::to_string(idNota));
                }
                ++exported;
            } catch (const FbError& e) {
                postLog(L"Erro ao tentar gravar xml da nota " + widen(key) + L" (" + widen(e.msg) + L")");
            }
        }

        }
        conn.commit();
    } catch (const FbError& e) {
        postLog(L"ERRO: " + widen(e.msg));
        result = DONE_ERROR;
    }

    PostMessageW(g_hwnd, WM_APP_DONE, result, exported);
}

// ---------------------------------------------------------------------------------------------------------------------
// Interface grafica
// ---------------------------------------------------------------------------------------------------------------------

enum {
    ID_DB_EDIT = 100, ID_DB_BROWSE, ID_FROM, ID_TO, ID_MODEL, ID_DIR_EDIT, ID_DIR_BROWSE, ID_REMOVE, ID_START, ID_CANCEL,
    ID_PROGRESS, ID_STATUS, ID_LOG
};

static HWND g_db, g_from, g_to, g_model, g_dir, g_remove, g_start, g_cancelBtn, g_progress, g_status, g_log;
static HFONT g_font = nullptr;
static bool g_running = false;
static bool g_closePending = false;

static std::wstring getText(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

static HWND makeControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                        DWORD exStyle = 0)
{
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

static std::string formatDate(HWND dtp)
{
    SYSTEMTIME st;
    SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
    char buf[16];
    sprintf_s(buf, "%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
    return buf;
}

static void browseDatabase(HWND hwnd)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Bancos de dados Firebird (*.fdb;*.gdb)\0*.fdb;*.gdb\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Selecione o banco de dados";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_db, file);
}

static void browseFolder(HWND hwnd)
{
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Pasta de destino dos XMLs");
    if (SUCCEEDED(dlg->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(g_dir, path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

static void setRunning(bool running)
{
    g_running = running;
    EnableWindow(g_start, !running);
    EnableWindow(g_cancelBtn, running);
    for (HWND h : {g_db, g_from, g_to, g_model, g_dir, g_remove, GetDlgItem(GetParent(g_db), ID_DB_BROWSE),
                   GetDlgItem(GetParent(g_db), ID_DIR_BROWSE)})
        EnableWindow(h, !running);
}

static void appendLog(const std::wstring& text)
{
    std::wstring line = text;
    for (auto& c : line)
        if (c == 10 || c == 13) c = L' ';
    int idx = (int)SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    HDC dc = GetDC(g_log);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(g_log, dc);
    if (sz.cx + 8 > (int)SendMessageW(g_log, LB_GETHORIZONTALEXTENT, 0, 0))
        SendMessageW(g_log, LB_SETHORIZONTALEXTENT, sz.cx + 8, 0);
    SendMessageW(g_log, LB_SETTOPINDEX, idx, 0);
}

static void startExport(HWND hwnd)
{
    std::wstring db = trim(getText(g_db));
    std::wstring dir = trim(getText(g_dir));
    std::wstring model = trim(getText(g_model));

    if (db.empty()) {
        MessageBoxW(hwnd, L"Informe o caminho do banco de dados.", L"Exportador de XML", MB_ICONWARNING);
        SetFocus(g_db);
        return;
    }
    if (model.empty() || model.find_first_not_of(L"0123456789") != std::wstring::npos) {
        MessageBoxW(hwnd, L"Informe o modelo do DF-e (somente numeros, ex: 55).", L"Exportador de XML", MB_ICONWARNING);
        SetFocus(g_model);
        return;
    }
    if (dir.empty()) {
        MessageBoxW(hwnd, L"Informe a pasta de destino dos XMLs.", L"Exportador de XML", MB_ICONWARNING);
        SetFocus(g_dir);
        return;
    }
    SYSTEMTIME a, b;
    SendMessageW(g_from, DTM_GETSYSTEMTIME, 0, (LPARAM)&a);
    SendMessageW(g_to, DTM_GETSYSTEMTIME, 0, (LPARAM)&b);
    FILETIME fa, fb;
    SystemTimeToFileTime(&a, &fa);
    SystemTimeToFileTime(&b, &fb);
    if (CompareFileTime(&fa, &fb) > 0) {
        MessageBoxW(hwnd, L"A data inicial deve ser anterior ou igual a data final.", L"Exportador de XML", MB_ICONWARNING);
        return;
    }
    bool remove = SendMessageW(g_remove, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (remove &&
        MessageBoxW(hwnd,
                    L"Os XMLs exportados serao REMOVIDOS do banco de dados.\n\nDeseja continuar?",
                    L"Exportador de XML", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    ExportParams p;
    std::string dbPath = narrow(db);
    // Equivalente ao host='localhost' do script original, a menos que o usuario ja tenha informado "servidor:caminho"
    if (dbPath.compare(0, 2, "\\\\") != 0 && dbPath.find(':', 2) == std::string::npos) dbPath = "localhost:" + dbPath;
    p.database = dbPath;
    p.dir = dir;
    p.dateFrom = formatDate(g_from);
    p.dateTo = formatDate(g_to);
    p.model = narrow(model);
    p.removeXml = remove;

    g_cancel = false;
    SendMessageW(g_log, LB_RESETCONTENT, 0, 0);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SetWindowTextW(g_status, L"Exportando...");
    setRunning(true);
    std::thread(exportXmls, p).detach();
}

static void createControls(HWND hwnd)
{
    makeControl(hwnd, L"STATIC", L"Banco de dados:", 0, 12, 12, 300, 16, -1);
    g_db = makeControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 12, 30, 440, 23, ID_DB_EDIT, WS_EX_CLIENTEDGE);
    makeControl(hwnd, L"BUTTON", L"Procurar...", WS_TABSTOP | BS_PUSHBUTTON, 460, 29, 88, 25, ID_DB_BROWSE);

    makeControl(hwnd, L"STATIC", L"Data de emiss\u00e3o inicial:", 0, 12, 66, 160, 16, -1);
    g_from = makeControl(hwnd, L"SysDateTimePick32", L"", WS_TABSTOP | DTS_SHORTDATEFORMAT, 12, 84, 130, 23, ID_FROM);
    makeControl(hwnd, L"STATIC", L"Data de emiss\u00e3o final:", 0, 160, 66, 160, 16, -1);
    g_to = makeControl(hwnd, L"SysDateTimePick32", L"", WS_TABSTOP | DTS_SHORTDATEFORMAT, 160, 84, 130, 23, ID_TO);
    makeControl(hwnd, L"STATIC", L"Modelo (ex: 55):", 0, 308, 66, 120, 16, -1);
    g_model = makeControl(hwnd, L"EDIT", L"55", WS_TABSTOP | ES_NUMBER, 308, 84, 70, 23, ID_MODEL, WS_EX_CLIENTEDGE);

    makeControl(hwnd, L"STATIC", L"Pasta de destino dos XMLs:", 0, 12, 120, 300, 16, -1);
    g_dir = makeControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 12, 138, 440, 23, ID_DIR_EDIT, WS_EX_CLIENTEDGE);
    makeControl(hwnd, L"BUTTON", L"Procurar...", WS_TABSTOP | BS_PUSHBUTTON, 460, 137, 88, 25, ID_DIR_BROWSE);

    g_remove = makeControl(hwnd, L"BUTTON", L"Remover o XML do banco de dados ap\u00f3s exportar", WS_TABSTOP | BS_AUTOCHECKBOX, 12,
                           174, 400, 20, ID_REMOVE);
    SendMessageW(g_remove, BM_SETCHECK, BST_CHECKED, 0);

    g_start = makeControl(hwnd, L"BUTTON", L"Exportar", WS_TABSTOP | BS_DEFPUSHBUTTON, 12, 204, 110, 28, ID_START);
    g_cancelBtn = makeControl(hwnd, L"BUTTON", L"Cancelar", WS_TABSTOP | BS_PUSHBUTTON, 130, 204, 110, 28, ID_CANCEL);
    EnableWindow(g_cancelBtn, FALSE);

    g_progress = makeControl(hwnd, PROGRESS_CLASSW, L"", 0, 12, 244, 536, 18, ID_PROGRESS);
    g_status = makeControl(hwnd, L"STATIC", L"", 0, 12, 268, 536, 16, ID_STATUS);
    g_log = makeControl(hwnd, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOSEL | LBS_NOINTEGRALHEIGHT, 12, 290,
                        536, 150, ID_LOG, WS_EX_CLIENTEDGE);

    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    EnumChildWindows(
        hwnd, [](HWND h, LPARAM f) -> BOOL { SendMessageW(h, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)g_font);

    SendMessageW(g_from, DTM_SETFORMATW, 0, (LPARAM)L"dd'.'MM'.'yyyy");
    SendMessageW(g_to, DTM_SETFORMATW, 0, (LPARAM)L"dd'.'MM'.'yyyy");
    SYSTEMTIME today, first;
    GetLocalTime(&today);
    first = today;
    first.wDay = 1;
    SendMessageW(g_from, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&first);
    SendMessageW(g_to, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&today);

    // Sugere o banco configurado no ello.ini (veja usar_banco.c)
    wchar_t ini[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Dados", L"Database", L"", ini, MAX_PATH, L"C:\\ello\\windows\\ello.ini");
    SetWindowTextW(g_db, ini);

    // Banco passado como argumento (menu de contexto do arquivo .ello)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) SetWindowTextW(g_db, argv[1]);
        LocalFree(argv);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        createControls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_DB_BROWSE: browseDatabase(hwnd); break;
        case ID_DIR_BROWSE: browseFolder(hwnd); break;
        case ID_START: startExport(hwnd); break;
        case ID_CANCEL:
            g_cancel = true;
            EnableWindow(g_cancelBtn, FALSE);
            SetWindowTextW(g_status, L"Cancelando...");
            break;
        }
        return 0;

    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> text((std::wstring*)lp);
        appendLog(*text);
        return 0;
    }

    case WM_APP_PROGRESS: {
        int total = (int)lp, cur = (int)wp;
        SendMessageW(g_progress, PBM_SETRANGE32, 0, total > 0 ? total : 1);
        SendMessageW(g_progress, PBM_SETPOS, cur, 0);
        wchar_t buf[64];
        swprintf_s(buf, L"Exportando... %d de %d", cur, total);
        SetWindowTextW(g_status, buf);
        return 0;
    }

    case WM_APP_DONE: {
        setRunning(false);
        wchar_t buf[128];
        if (wp == DONE_OK) {
            swprintf_s(buf, L"Conclu\u00eddo: %d XML(s) exportado(s).", (int)lp);
            SendMessageW(g_progress, PBM_SETPOS, (WPARAM)SendMessageW(g_progress, PBM_GETRANGE, FALSE, 0), 0);
        } else if (wp == DONE_CANCELLED) {
            swprintf_s(buf, L"Cancelado: %d XML(s) exportado(s).", (int)lp);
        } else {
            swprintf_s(buf, L"Erro durante a exporta\u00e7\u00e3o (%d XML(s) exportado(s)).", (int)lp);
        }
        SetWindowTextW(g_status, buf);
        appendLog(buf);
        if (g_closePending) DestroyWindow(hwnd);
        else if (wp == DONE_OK) MessageBoxW(hwnd, buf, L"Exportador de XML", MB_ICONINFORMATION);
        else if (wp == DONE_ERROR) MessageBoxW(hwnd, buf, L"Exportador de XML", MB_ICONERROR);
        return 0;
    }

    case WM_CLOSE:
        if (g_running) {
            if (MessageBoxW(hwnd, L"Uma exporta\u00e7\u00e3o est\u00e1 em andamento. Deseja cancel\u00e1-la e sair?",
                            L"Exportador de XML", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                g_cancel = true;
                g_closePending = true;  // fecha quando a thread terminar
            }
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_DATE_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EloExportadorXml";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, 560, 452};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Exportador de XML - Ello", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return (int)msg.wParam;
}

// O Makefile usa /ENTRY:mainCRTStartup (veja fbclient_x86.rsp)
int main() { return run(); }
