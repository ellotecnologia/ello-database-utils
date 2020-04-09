#include <stdio.h>
#include <windows.h>

int main(int argc, char* argv[])
{
    if(argc < 2) {
        puts("Configura o arquivo .ini do Ello para utilizar o banco de dados informado.");
        puts("Uso: usar_banco <nome do banco de dados>");
        return 1;
    }
    WritePrivateProfileString("Dados", "Database", argv[1], "C:\\ello\\windows\\ello.ini");
    return 0;
}