# encoding: utf-8
import os.path
import fdb

# Conexão com o Banco de dados Firebird
caminho_bd = input("Informe o caminho do Banco de Dados: ")
conn = fdb.connect(host='localhost', user='sysdba', password='masterkey', database=caminho_bd)

data_inicio = input("Informe a data de inicio (ex: 01.01.2019): ")
data_fim    = input("Informe a data de termino (ex: 31.01.2019): ")
modelo_dfe  = input("Informe o modelo de DF-e a exportar (ex: 55): ")
diretorio   = input("Informe a pasta de destino dos XMLs: ")

# Cria o diretório caso não exista
if not os.path.isdir(diretorio):
  os.mkdir(diretorio)

# Obtem os XMLs do banco de dados
q1 = conn.cursor()
q2 = conn.cursor()
q1.execute("""
  SELECT idnota, chave, xml_nfe
  FROM tnfenota
  WHERE dataemissao BETWEEN '{0}' AND '{1}'
    AND modelo='{2}'""".format(data_inicio, data_fim, modelo_dfe)
)

# Salva XMLs
for idnota, chave, xml in q1:
    print(chave)

    if chave is None:
        continue

    if xml is None:
        continue

    arquivos = open("{0}/{1}.xml".format(diretorio, chave), 'w')
    arquivos.write(xml.decode('utf8'))
    arquivos.close()
    q2.execute("UPDATE TNFeNota SET XML_NFE=NULL, XML_LOTE=NULL WHERE IdNota = {0}".format(idnota))

conn.commit()

print("\nXMLs exportados com sucesso para a pasta {}".format(diretorio))
