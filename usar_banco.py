# coding: utf8
import os
import sys
import ConfigParser

ini_file = '/Ello/Windows/Ello.ini'
db_file = sys.argv[1].replace('\\', '/')

config = ConfigParser.RawConfigParser()
config.read(ini_file)
config.set('Dados', 'DataBase', db_file)

with open(ini_file, 'w') as config_file:
    config.write(config_file)

print 'Arquivo {0} configurado para usar o banco {1}'.format(ini_file, db_file)

