import sys
import fdb
import ctypes

db_file_name = sys.argv[1]

conn = fdb.connect(db_file_name, 'sysdba', 'masterkey')
cursor = conn.cursor()

cursor.execute("""\
SELECT FIRST 1 a.VERSAO
FROM TSCRIPTS a
order by ultimoscript desc
""")

versao = cursor.fetchone()[0]

conn.close()

MessageBox = ctypes.windll.user32.MessageBoxA
MessageBox(None, versao, db_file_name, 0)
