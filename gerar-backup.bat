@echo off
gbak -v -b "%1" "%~n1.fbk"
echo Compactando banco de dados...
7za a "%~n1.ebk" "%~n1.fbk" >NUL
DEL /Q "%~n1.fbk"
pause

