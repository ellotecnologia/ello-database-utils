@echo off
echo Extraindo banco de dados...
7za x "%1" >NUL
gbak -v -c "%~n1.ebk" "%~n1.ello"
DEL /Q "%~n1.ebk"
pause

