CC_FLAGS=-I"C:\Program Files\Firebird\Firebird_2_5\include"
LD_FLAGS=-luser32 -Wl,-subsystem=windows

all:
	@tcc usar_banco.c $(LD_FLAGS)
	@tcc $(CC_FLAGS) versao_banco.c $(LD_FLAGS) "C:\Program Files\Firebird\Firebird_2_5\bin\fbclient.dll"

exportador:
	pyinstaller -c --onefile exportador_xml.py
	