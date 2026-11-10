# Build with MSVC 2019 Build Tools from an x86 (32-bit) environment, to match fbclient.dll and fsql.exe:
#   call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
#   nmake

# Only the Firebird headers are needed. The x86 import lib comes from fbclient_x86.def; fbclient_x86.rsp (linker options) maps the
# decorated stdcall names (_isc_xxx@N) that the compiler emits to the undecorated exports of fbclient.dll.
FB=C:\bin\Firebird25
CFLAGS=/nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /I"$(FB)\include"
LDFLAGS=/link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

all: bin\usar_banco.exe bin\versao_banco.exe bin\exportador_xml.exe bin\ebak.exe bin\ebakw.exe

bin:
	@if not exist bin mkdir bin

bin\usar_banco.exe: usar_banco.c bin
	cl $(CFLAGS) /Fo"bin\\" /Fe"$@" usar_banco.c user32.lib $(LDFLAGS)

bin\fbclient_x86.lib: fbclient_x86.def bin
	lib /nologo /machine:x86 /def:fbclient_x86.def /out:$@

bin\versao_banco.exe: versao_banco.c bin\fbclient_x86.lib
	cl $(CFLAGS) /Fo"bin\\" /Fe"$@" versao_banco.c user32.lib bin\fbclient_x86.lib @fbclient_x86.rsp

bin\ebak.exe: ebak.cpp ebak_core.cpp ebak_core.h bin\fbclient_x86.lib
	cl $(CFLAGS) /EHsc /Fo"bin\\" /Fe"$@" ebak.cpp ebak_core.cpp bin\fbclient_x86.lib @fbclient_x86.rsp /link /SUBSYSTEM:CONSOLE

bin\ebakw.exe: ebak_gui.cpp ebak_core.cpp ebak_core.h bin\fbclient_x86.lib
	cl $(CFLAGS) /EHsc /Fo"bin\\" /Fe"$@" ebak_gui.cpp ebak_core.cpp bin\fbclient_x86.lib @fbclient_x86.rsp

bin\exportador_xml.exe: exportador_xml.cpp bin\fbclient_x86.lib
	cl $(CFLAGS) /EHsc /Fo"bin\\" /Fe"$@" exportador_xml.cpp bin\fbclient_x86.lib @fbclient_x86.rsp

clean:
	-del bin\usar_banco.exe bin\versao_banco.exe bin\exportador_xml.exe bin\ebak.exe bin\ebakw.exe bin\*.obj bin\fbclient_x86.lib bin\fbclient_x86.exp
