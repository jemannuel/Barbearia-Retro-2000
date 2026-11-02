@echo off
rem Compila o sistema no Windows (precisa do gcc instalado, veja o LEIAME.txt)
if not exist sqlite3.c (
  echo ERRO: falta o arquivo sqlite3.c nesta pasta. Veja o passo 2 do LEIAME.txt
  pause
  exit /b 1
)
if not exist sqlite3.o (
  echo Compilando o SQLite. Isso so acontece na primeira vez e pode levar cerca de 1 minuto...
  gcc -O2 -c sqlite3.c -o sqlite3.o
  if errorlevel 1 (
    echo ERRO ao compilar o SQLite.
    pause
    exit /b 1
  )
)
gcc -O2 -Wall -o barbearia.exe server.c util.c http.c sqlite3.o -lws2_32
if errorlevel 1 (
  echo ERRO ao compilar o sistema. Copie a mensagem acima e mande para o grupo.
  pause
  exit /b 1
)
echo.
echo Pronto! Agora de dois cliques em iniciar.bat
pause
