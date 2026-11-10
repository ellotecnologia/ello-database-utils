// ebak - backup e restauracao de bancos de dados Ello (Firebird) pela linha de comando.
//
//   ebak backup  <banco.ello> [saida.ebk]               gera o backup (.fbk) e o compacta com 7-Zip em um .ebk
//   ebak restore <backup.ebk|backup.fbk> [saida.ello]   restaura um backup .ebk ou .fbk
//
// A logica fica em ebak_core.cpp (compartilhada com a versao grafica, ebakw.exe). Veja o Makefile para compilar (MSVC x86).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <conio.h>
#include <shellapi.h>

#include <string>

#include "ebak_core.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// Escreve texto Unicode no console (ou em UTF-8 se a saida foi redirecionada)
static void print(const std::wstring& text)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode, written;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.data(), (DWORD)text.size(), &written, nullptr);
    } else {
        int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string u8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &u8[0], n, nullptr, nullptr);
        WriteFile(h, u8.data(), (DWORD)u8.size(), &written, nullptr);
    }
}

static void println(const std::wstring& text) { print(text + L"\r\n"); }

struct ConsoleReporter : Reporter {
    void log(const std::wstring& line) override { println(line); }
};

static void usage()
{
    println(L"Backup e restauracao de bancos de dados Ello.");
    println(L"");
    println(L"Uso:");
    println(L"  ebak backup  <banco.ello> [saida.ebk]");
    println(L"  ebak restore <backup.ebk | backup.fbk> [saida.ello]");
    println(L"");
    println(L"Para acompanhar o progresso em uma janela, use ebakw.exe com os mesmos argumentos.");
}

// Quando iniciado pelo Explorer o console some ao terminar: espera uma tecla para dar tempo de ler.
static void pauseIfOwnConsole()
{
    DWORD pid;
    if (GetConsoleProcessList(&pid, 1) == 1) {
        print(L"\r\nPressione qualquer tecla para sair...");
        _getch();
    }
}

int main()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = 1;

    if (argv && argc >= 3 && argc <= 4) {
        std::wstring action = argv[1];
        std::wstring out = argc == 4 ? argv[3] : L"";
        ConsoleReporter reporter;
        if (action == L"backup") {
            rc = ebakBackup(argv[2], out, reporter);
        } else if (action == L"restore") {
            rc = ebakRestore(argv[2], out, reporter);
        } else {
            usage();
        }
    } else {
        usage();
    }

    if (argv) LocalFree(argv);
    pauseIfOwnConsole();
    return rc;
}
