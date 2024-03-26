@echo off
echo Extraindo banco de dados...
7za x "%1" >NUL
gbak -v -c "%~n1.fbk" "%~n1.ello"
DEL /Q "%~n1.fbk"
pause

