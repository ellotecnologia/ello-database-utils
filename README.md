# Ello Database Utils

Utilitários para trabalhar com bancos de dados Firebird do Ello direto pelo Windows Explorer.
O instalador (`Ello-Database-Utils.exe`) registra os tipos de arquivo abaixo e adiciona
ações ao menu de contexto (botão direito).

## Tipos de arquivo

| Extensão | Descrição            | Ações no menu de contexto                                      |
|----------|----------------------|----------------------------------------------------------------|
| `.ello`  | Banco de dados Ello  | Abrir (fSQL), Usar este banco, Versão do banco, Manutenção     |
| `.ebk`   | Backup Ello          | Restaurar (`ebak.exe restore`: descompacta e restaura)         |
| `.fbk`   | Backup Firebird      | Restaurar (`ebak.exe restore`)                                 |

Submenu **Manutenção** (em arquivos `.ello`):

- **Gerar Backup**: `ebak.exe backup`, gera o backup pelo `fbclient` (Services API), compacta para `.ebk` com o 7-Zip e remove o `.fbk`.
- **Abrir fSQL**: abre o banco no fsql, um shell interativo para Firebird.
- **Criar Banco com CHARSET**: converte o charset do banco com o `fixcharset.exe`, gerando `<banco>_novo.ello` ao lado do original.
- **Estatísticas do banco**: abre o `fbstats.exe` com o banco selecionado (requer o WebView2 Runtime da Microsoft).
- **Exportar XMLs**: abre o `exportador_xml.exe` com o banco selecionado.

Ações diretas:

- **Usar este banco** (`usar_banco.exe`): grava o banco selecionado em `C:\ello\windows\ello.ini`
  (seção `[Dados]`, chave `Database`).
- **Versão do banco** (`versao_banco.exe`): mostra a versão do release e o número do patch
  (`GERVERSAODORELEASE` e `GERIDSCRIPTRELEASE` da tabela `TGerParametros`).

## O que o instalador faz

- Instala em `C:\Program Files\Ello Database Utils`.
- Copia `gbak`, `fbclient.dll`, `7za`, `fsql`, `fixcharset`, `fbstats` e `WebView2Loader.dll` para `{app}\bin` e adiciona essa pasta ao `PATH`.
- Define as variáveis de ambiente de sistema `ISC_USER=sysdba` e `ISC_PASSWORD=masterkey`.
- Registra os tipos de arquivo e o menu de contexto (precisa de administrador).
  A desinstalação remove as chaves de registro.

## Compilando

Requisitos:

- **MSVC 2019 Build Tools** (para `usar_banco.exe` e `versao_banco.exe`).
- **Inno Setup 6** (para o instalador).
- Os cabeçalhos do Firebird 2.5 (`ibase.h`), por padrão em `C:\bin\Firebird25\include`
  (ajuste `FB` no `Makefile`).
- Um `fbclient.dll` e um `gbak.exe` de **32 bits**, em `bin\`. O `fsql.exe` é um programa
  Delphi de 32 bits e precisa de um `fbclient.dll` de 32 bits.

### 1. Programas em C

Em um prompt de comando:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
nmake
```

Gera `bin\usar_banco.exe` e `bin\versao_banco.exe`. Como o MSVC não tem uma biblioteca de
importação de 32 bits do Firebird, o `Makefile` gera uma a partir de `fbclient_x86.def`;
o `fbclient_x86.rsp` mapeia os nomes `stdcall` decorados para os exports do `fbclient.dll`.

`nmake clean` remove os arquivos gerados.

### 2. Pasta `bin\`

O `setup.iss` espera encontrar em `bin\`:

`gbak.exe`, `fbclient.dll`, `7za.exe`, `fixcharset.exe`, `fbstats.exe`, `WebView2Loader.dll`, `usar_banco.exe` e `versao_banco.exe`.

O `fsql.exe` é copiado direto de `C:\dev\fsql\fsql.exe` (ver `[Files]` no `setup.iss`).
Os binários (`*.exe`) não são versionados.

### 3. Instalador

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" setup.iss
```

O resultado fica em `Output\Ello-Database-Utils.exe`.

O `setup.iss` deve ser mantido em **UTF-8 com BOM** (Inno Setup 6) por causa dos acentos.

### ebak / ebakw (backup e restauração)

```bat
ebak backup  <banco.ello> [saida.ebk]
ebak restore <backup.ebk | backup.fbk> [saida.ello]
```

- `backup` gera `<banco>.ebk` ao lado do banco (um `.fbk` compactado com o `7za.exe`).
- `restore` gera `<backup>.ello` ao lado do backup e **não sobrescreve** um banco existente.
- O Firebird precisa estar no mesmo computador: os arquivos são lidos e gravados pelo servidor (Services API), e usa `sysdba`/`masterkey` (ou `ISC_USER`/`ISC_PASSWORD`).
- Ao ser aberto pelo Explorer, espera uma tecla ao terminar para dar tempo de ler o resultado.
- `ebakw.exe` aceita os mesmos argumentos, mas mostra uma janela com a etapa atual, o tempo decorrido, o tamanho do arquivo sendo gerado e o log do `gbak`. É a versão usada pelos menus do Explorer. (A barra de progresso é indeterminada: o `gbak` não informa percentual. Não há botão cancelar, pois a operação roda no servidor Firebird.)

### Exportador de XMLs

`bin\exportador_xml.exe` (fonte: `exportador_xml.cpp`) é uma janela para exportar os XMLs de DF-e de um banco Ello: informe o banco, o período de emissão, o modelo e a pasta de destino. É compilado junto com os demais por `nmake` e instalado em `{app}in`.

## Arquivos

| Arquivo                                          | Função                                              |
|--------------------------------------------------|-----------------------------------------------------|
| `setup.iss`                                      | Script do instalador (Inno Setup)                   |
| `Makefile`                                       | Compila os programas em C com MSVC (`nmake`)        |
| `usar_banco.c`, `versao_banco.c`                 | Fontes dos programas em C                           |
| `exportador_xml.cpp`                             | Exportador de XMLs de DF-e (C++, interface gráfica) |
| `fbclient_x86.def`, `fbclient_x86.rsp`           | Biblioteca de importação do `fbclient.dll` (x86)    |
| `ebak.cpp`, `ebak_gui.cpp`, `ebak_core.cpp/.h`   | `ebak` (CLI) e `ebakw` (janela): backup/restauração |
| `Icons\`                                         | Ícones dos tipos de arquivo                         |
