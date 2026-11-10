// Nucleo do ebak: backup e restauracao de bancos Ello via Services API do fbclient (usado por ebak.exe e ebakw.exe).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <ibase.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ebak_core.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

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

static std::wstring fullPath(const std::wstring& path)
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(path.c_str(), (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    return n ? std::wstring(buf, n) : path;
}

static std::wstring dirOf(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : path.substr(0, p);
}

static std::wstring baseNameNoExt(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    std::wstring name = p == std::wstring::npos ? path : path.substr(p + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

static std::wstring extOf(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    std::wstring name = p == std::wstring::npos ? path : path.substr(p + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = name.substr(dot);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

static bool fileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dirExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Executa um programa sem janela e sem saida; devolve o exit code (-1 se nao conseguiu executar)
static int runQuiet(const std::wstring& exe, const std::wstring& args)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = nul;
    si.hStdError = nul;
    PROCESS_INFORMATION pi = {};

    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

// Remove uma pasta e seu conteudo
static void removeTree(const std::wstring& dir)
{
    if (!dirExists(dir)) return;
    std::wstring from = dir + L'\0';  // a lista de nomes termina com duplo NUL
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

static std::wstring find7za()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring candidate = dirOf(self) + L"\\7za.exe";
    return fileExists(candidate) ? candidate : L"7za.exe";  // senao procura no PATH
}

// ---------------------------------------------------------------------------------------------------------------------
// Firebird (Services API)
// ---------------------------------------------------------------------------------------------------------------------

struct FbError {
    std::string msg;
};

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

[[noreturn]] static void fbThrow(const ISC_STATUS* status) { throw FbError{fbError(status)}; }

static std::string fbUser()
{
    const char* u = getenv("ISC_USER");
    return u && *u ? u : "sysdba";
}

static std::string fbPassword()
{
    const char* p = getenv("ISC_PASSWORD");
    return p && *p ? p : "masterkey";
}

static void appendStr(std::string& spb, char tag, const std::string& value)  // tag + len(1)
{
    spb += tag;
    spb += (char)value.size();
    spb += value;
}

static void appendStr2(std::string& spb, char tag, const std::string& value)  // tag + len(2, little endian)
{
    spb += tag;
    spb += (char)(value.size() & 0xFF);
    spb += (char)((value.size() >> 8) & 0xFF);
    spb += value;
}

static void appendInt(std::string& spb, char tag, ISC_LONG value)  // tag + 4 bytes little endian
{
    spb += tag;
    for (int i = 0; i < 4; ++i) spb += (char)((value >> (8 * i)) & 0xFF);
}

class FbService {
public:
    isc_svc_handle svc = 0;

    FbService()
    {
        ISC_STATUS_ARRAY status;
        std::string spb;
        spb += (char)isc_spb_version;
        spb += (char)isc_spb_current_version;
        appendStr(spb, (char)isc_spb_user_name, fbUser());
        appendStr(spb, (char)isc_spb_password, fbPassword());
        if (isc_service_attach(status, 0, "service_mgr", &svc, (unsigned short)spb.size(), spb.data())) fbThrow(status);
    }

    ~FbService()
    {
        ISC_STATUS_ARRAY status;
        if (svc) isc_service_detach(status, &svc);
    }

    void start(const std::string& spb)
    {
        ISC_STATUS_ARRAY status;
        if (isc_service_start(status, &svc, nullptr, (unsigned short)spb.size(), spb.data())) fbThrow(status);
    }

    // Exibe a saida verbosa do servico ate o fim. Devolve true se nenhuma linha indicou erro.
    bool pumpOutput(Reporter& r)
    {
        bool ok = true;
        const char request = isc_info_svc_line;
        char buf[1024];
        for (;;) {
            ISC_STATUS_ARRAY status;
            if (isc_service_query(status, &svc, nullptr, 0, nullptr, 1, &request, sizeof(buf), buf)) fbThrow(status);

            if (buf[0] == isc_info_svc_line) {
                int len = (int)isc_vax_integer(buf + 1, 2);
                if (len == 0) break;  // fim da saida do servico
                std::string line(buf + 3, len);
                r.log(widen(line));
                // So linhas de erro do proprio gbak ("gbak: ERROR: ...") ou o aviso de abortar contam como falha;
                // procurar "ERRO" em qualquer lugar da linha dava falso positivo (ex.: coluna ou tabela chamada ERRO)
                std::string t;
                for (char c : line)
                    if (!isspace((unsigned char)c)) t += (char)tolower((unsigned char)c);
                if (t.compare(0, 10, "gbak:error") == 0 || t.find("exitingbeforecompletion") != std::string::npos) ok = false;
            } else if (buf[0] == isc_info_data_not_ready || buf[0] == isc_info_truncated) {
                Sleep(50);
            } else {
                break;  // isc_info_end ou desconhecido
            }
        }
        return ok;
    }

    FbService(const FbService&) = delete;
    FbService& operator=(const FbService&) = delete;
};

// Roda o gbak no servidor (caminhos sao do ponto de vista do servidor Firebird, que deve ser local)
static bool serviceBackup(const std::wstring& db, const std::wstring& fbk, Reporter& r)
{
    FbService s;
    std::string spb;
    spb += (char)isc_action_svc_backup;
    appendStr2(spb, (char)isc_spb_dbname, narrow(db));
    appendStr2(spb, (char)isc_spb_bkp_file, narrow(fbk));
    spb += (char)isc_spb_verbose;
    appendInt(spb, (char)isc_spb_options, 0);
    s.start(spb);
    return s.pumpOutput(r);
}

static bool serviceRestore(const std::wstring& fbk, const std::wstring& db, Reporter& r)
{
    FbService s;
    std::string spb;
    spb += (char)isc_action_svc_restore;
    appendStr2(spb, (char)isc_spb_bkp_file, narrow(fbk));
    appendStr2(spb, (char)isc_spb_dbname, narrow(db));
    spb += (char)isc_spb_verbose;
    appendInt(spb, (char)isc_spb_options, isc_spb_res_create);  // como "gbak -c": nao sobrescreve banco existente
    s.start(spb);
    return s.pumpOutput(r);
}

// Confirma que o banco restaurado pode ser aberto
static bool canAttach(const std::wstring& db, Reporter& r)
{
    ISC_STATUS_ARRAY status;
    isc_db_handle h = 0;
    std::string user = fbUser(), pass = fbPassword(), dpb;
    dpb += (char)isc_dpb_version1;
    appendStr(dpb, (char)isc_dpb_user_name, user);
    appendStr(dpb, (char)isc_dpb_password, pass);
    if (isc_attach_database(status, 0, narrow(db).c_str(), &h, (short)dpb.size(), dpb.data())) {
        r.log(L"Erro ao abrir o banco restaurado: " + widen(fbError(status)));
        return false;
    }
    isc_detach_database(status, &h);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Acoes
// ---------------------------------------------------------------------------------------------------------------------

int ebakBackup(const std::wstring& dbArg, const std::wstring& outArg, Reporter& r)
{
    std::wstring db = fullPath(dbArg);
    if (!fileExists(db)) {
        r.log(L"Banco de dados nao encontrado: " + db);
        return 2;
    }
    std::wstring ebk = outArg.empty() ? dirOf(db) + L"\\" + baseNameNoExt(db) + L".ebk" : fullPath(outArg);
    std::wstring fbk = dirOf(ebk) + L"\\" + baseNameNoExt(ebk) + L".fbk";

    r.log(L"Gerando backup de " + db + L" ...");
    r.stage(L"Gerando backup...");
    r.watchFile(fbk);
    try {
        if (!serviceBackup(db, fbk, r)) {
            r.log(L"\r\nO backup terminou com erros.");
            return 2;
        }
    } catch (const FbError& e) {
        r.log(L"Erro: " + widen(e.msg));
        return 2;
    }
    if (!fileExists(fbk)) {
        r.log(L"\r\nO arquivo de backup nao foi gerado: " + fbk);
        return 2;
    }

    r.log(L"Compactando banco de dados...");
    r.stage(L"Compactando o backup...");
    // Compacta para um arquivo temporario para nao perder um .ebk anterior caso algo falhe
    std::wstring tmp = ebk + L".tmp";
    DeleteFileW(tmp.c_str());
    r.watchFile(tmp);
    int rc = runQuiet(find7za(), L"a -t7z \"" + tmp + L"\" \"" + fbk + L"\"");
    if (rc != 0) {
        r.log(L"Erro ao compactar o backup com o 7-Zip (codigo " + std::to_wstring(rc) + L"). O arquivo " + fbk +
                L" foi mantido.");
        DeleteFileW(tmp.c_str());
        return 2;
    }
    if (!MoveFileExW(tmp.c_str(), ebk.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        r.log(L"Erro ao gravar " + ebk);
        DeleteFileW(tmp.c_str());
        return 2;
    }
    DeleteFileW(fbk.c_str());
    r.log(L"\r\nBackup gerado com sucesso: " + ebk);
    return 0;
}

int ebakRestore(const std::wstring& srcArg, const std::wstring& outArg, Reporter& r)
{
    std::wstring src = fullPath(srcArg);
    if (!fileExists(src)) {
        r.log(L"Arquivo de backup nao encontrado: " + src);
        return 2;
    }
    std::wstring ext = extOf(src);
    if (ext != L".ebk" && ext != L".fbk") {
        r.log(L"Extensao nao suportada (use .ebk ou .fbk): " + src);
        return 1;
    }
    std::wstring db = outArg.empty() ? dirOf(src) + L"\\" + baseNameNoExt(src) + L".ello" : fullPath(outArg);
    if (fileExists(db)) {
        r.log(L"O banco de dados ja existe e nao sera sobrescrito: " + db);
        return 2;
    }

    std::wstring fbk = src;
    std::wstring tmpDir;
    if (ext == L".ebk") {
        r.log(L"Extraindo banco de dados...");
        r.stage(L"Extraindo o backup...");
        tmpDir = src + L".tmp";
        if (dirExists(tmpDir)) {
            r.log(L"A pasta temporaria ja existe (remova-a e tente novamente): " + tmpDir);
            return 2;
        }
        int rc = runQuiet(find7za(), L"x \"" + src + L"\" -o\"" + tmpDir + L"\" -y");
        fbk.clear();
        if (rc == 0) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((tmpDir + L"\\*.fbk").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                fbk = tmpDir + L"\\" + fd.cFileName;
                FindClose(h);
            }
        }
        if (fbk.empty()) {
            r.log(L"Erro ao extrair o backup com o 7-Zip (arquivo .fbk nao encontrado em " + src + L").");
            removeTree(tmpDir);
            return 2;
        }
    }

    r.log(L"Restaurando para " + db + L" ...");
    r.stage(L"Restaurando o banco de dados...");
    r.watchFile(db);
    bool ok = false;
    try {
        ok = serviceRestore(fbk, db, r);
    } catch (const FbError& e) {
        r.log(L"Erro: " + widen(e.msg));
    }

    if (!tmpDir.empty()) removeTree(tmpDir);

    if (!ok || !fileExists(db) || !canAttach(db, r)) {
        r.log(L"\r\nA restauracao terminou com erros.");
        return 2;
    }
    r.log(L"\r\nBanco de dados restaurado com sucesso: " + db);
    return 0;
}
