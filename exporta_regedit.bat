@echo off

regedit /e 1.reg "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\CommandStore\shell\Ello.AbrirISQL"
regedit /e 2.reg "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\CommandStore\shell\Ello.Backup"
regedit /e 3.reg "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\CommandStore\shell\Ello.BackupEnviar"

regedit /e 4.reg "HKEY_CLASSES_ROOT\.fbk"
regedit /e 5.reg "HKEY_CLASSES_ROOT\.ebk"
regedit /e 6.reg "HKEY_CLASSES_ROOT\.ello"

regedit /e 7.reg "HKEY_CLASSES_ROOT\Ello.Database"
regedit /e 8.reg "HKEY_CLASSES_ROOT\Ello.Database.Backup"
regedit /e 9.reg "HKEY_CLASSES_ROOT\Ello.Firebird.Backup"

echo Windows Registry Editor Version 5.00 >final.reg
echo.>>final.reg
type 1.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 2.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 3.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 4.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 5.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 6.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 7.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 8.reg | find /v "Windows Registry Editor Version 5.00">>final.reg
type 9.reg | find /v "Windows Registry Editor Version 5.00">>final.reg

del 1.reg
del 2.reg
del 3.reg
del 4.reg
del 5.reg
del 6.reg
del 7.reg
del 8.reg
del 9.reg

