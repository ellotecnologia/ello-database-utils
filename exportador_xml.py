#coding: utf-8
from __future__ import print_function
import os.path
import fdb
#Conexão com o Banco de dados Firebird
caminho_bd = input("Informe o caminho do Banco de Dados: ")
conn = fdb.connect(host='localhost', user='sysdba', password='masterkey', database=caminho_bd)

#Inicio da interação com o usuario
print("")
print("Digite a data completa separada por ponto.")

#Solicita dados para exportação
data_inicio  = input("Informe a data de inicio : ")
data_fim     = input("Informe a data de termino: ")
modelo_dfe   = input("Digite o modelo de DF-e que deseja exportar: ")
diretorio    = input('Insira o local de destino: ')
if os.path.isdir(diretorio): #vemos se a diretorio já existe
  print ('Ja existe um arquivo com esse nome!')
else:
  os.mkdir(diretorio) #criamos o diretorio caso nao exista
  print ("Criado com sucesso!")

#Executa select das datas digitadas no banco de dados
q  = conn.cursor()
q2 = conn.cursor()
q.execute("select idnota, chave, xml_nfe from tnfenota where dataemissao between '{0}' and '{1}' and modelo ='{2}'".format(data_inicio, data_fim, modelo_dfe))

#Salva XMLs
for registro in q:
    idnota = registro[0]
    chave  = registro[1]
    xml    = registro[2]
    print(chave)
    if chave is None:
        continue
    if xml is None:
        continue
    arquivos = open("{0}/{1}.xml".format(diretorio, chave), 'w')
    arquivos.write(xml.decode('utf8'))
    arquivos.close()
    q2.execute("UPDATE TNFENOTA SET XML_NFE = null where idnota = {0}".format(idnota))
conn.commit()

#Finaliza processo de exportação
print("")
print("Processo Finalizado!!!")