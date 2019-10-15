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

print("Iniciando exportacao, aguarde...")

# Obtem os XMLs do banco de dados
q1 = conn.cursor()
q2 = conn.cursor()
q1.execute("""
  SELECT empresa, idnota, chave, xml_nfe
  FROM tnfenota
  WHERE dataemissao BETWEEN '{0}' AND '{1}'
    AND modelo='{2}'""".format(data_inicio, data_fim, modelo_dfe)
)

# Salva XMLs
for id_empresa, id_nota, chave, xml in q1:
    if chave is None:
        continue

    if xml is None:
        continue

    print("Salvando XML: {}".format(chave))

    try:
        with open("{0}/{1}-nfe.xml".format(diretorio, chave), 'w') as arquivo:
            arquivo.write(xml.decode('utf8'))
        q2.execute("UPDATE TNFeNota SET XML_NFE=NULL, XML_LOTE=NULL WHERE Empresa={} AND IdNota = {}".format(id_empresa, id_nota))
    except Exception as e:
        print("Erro ao tentar gravar xml da nota {} ({})".format(chave, e))

conn.commit()

print("\nXMLs exportados com sucesso para a pasta {}".format(diretorio))
