import os.path
import argparse
import fdb

parser = argparse.ArgumentParser(description='Exporta XMLs de um banco de dados Ello')
parser.add_argument('--db', help='Caminho do banco de dados')
parser.add_argument('-i', '--inicio', help='Data de emissão inicial (formato: dd.mm.aaaa)')
parser.add_argument('-f', '--fim', help='Data de emissão final (formato: dd.mm.aaaa)')
parser.add_argument('-m', '--modelo', help='Modelo de documentos a serem exportados')
parser.add_argument('-p', '--pasta-destino', help='Pasta onde salvar os XMLs')
parser.add_argument('-n', '--nao-remover-xml', action='store_true', default=False, help='Não remover o XML do banco de dados')

args = parser.parse_args()

caminho_bd  = args.db or input("Informe o caminho do Banco de Dados: ")
data_inicio = args.inicio or input("Informe a data de inicio (ex: 01.01.2019): ")
data_fim    = args.fim or input("Informe a data de termino (ex: 31.01.2019): ")
modelo_dfe  = args.modelo or input("Informe o modelo de DF-e a exportar (ex: 55): ")
diretorio   = args.pasta_destino or input("Informe a pasta de destino dos XMLs: ")

conn = fdb.connect(host='localhost', user='sysdba', password='masterkey', database=caminho_bd)

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
        if not args.nao_remover_xml:
            q2.execute("UPDATE TNFeNota SET XML_NFE=NULL, XML_LOTE=NULL WHERE Empresa={} AND IdNota = {}".format(id_empresa, id_nota))
    except Exception as e:
        print("Erro ao tentar gravar xml da nota {} ({})".format(chave, e))

conn.commit()

print("\nXMLs exportados com sucesso para a pasta {}".format(diretorio))
