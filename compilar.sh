#!/bin/sh
# Compila o sistema no Linux ou no Mac.
# Se existir sqlite3.c na pasta, usa ele. Senão tenta a biblioteca instalada no sistema.
if [ -f sqlite3.c ]; then
    gcc -O2 -Wall -o barbearia server.c util.c http.c sqlite3.c -lpthread -ldl -lm || exit 1
else
    gcc -O2 -Wall -o barbearia server.c util.c http.c -lsqlite3 -lm || exit 1
fi
echo "Pronto! Para rodar:  ./barbearia   (ou ./barbearia 8080 exemplo para carregar dados de exemplo)"
