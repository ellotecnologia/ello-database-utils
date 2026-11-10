// Nucleo do ebak: backup e restauracao de bancos Ello via Services API do fbclient.
#pragma once
#include <string>

// Recebe os eventos de uma operacao (chamado na thread que executa a operacao)
struct Reporter {
    virtual ~Reporter() {}
    virtual void log(const std::wstring& line) = 0;                 // linha de texto (saida do gbak e mensagens)
    virtual void stage(const std::wstring& /*title*/) {}            // etapa atual (ex.: "Compactando...")
    virtual void watchFile(const std::wstring& /*path*/) {}         // arquivo cujo tamanho acompanha o progresso
};

// Devolvem 0 em caso de sucesso, 1 para uso invalido e 2 para erro
int ebakBackup(const std::wstring& db, const std::wstring& outEbk, Reporter& r);
int ebakRestore(const std::wstring& backup, const std::wstring& outDb, Reporter& r);
