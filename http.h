#ifndef HTTP_H
#define HTTP_H

#include "util.h"

/* Dados de um pedido que chegou do navegador */
typedef struct {
    char  metodo[8];        /* GET ou POST */
    char  caminho[256];     /* ex: /horarios */
    char  query[1024];      /* parte depois do ? (ex: data=2026-10-20&barbeiro=2) */
    char  cookie_sid[40];   /* identificador da sessão (cookie "sid") */
    char *corpo;            /* dados enviados por um formulário POST (nunca é NULL) */
} Req;

/* Resposta que o programa monta */
typedef struct {
    int  status;            /* 200 = OK, 303 = redirecionar, 403, 404... */
    char local[700];        /* destino do redirecionamento (quando status = 303) */
    char cookie[200];       /* cookie a enviar ao navegador (opcional) */
    Buf  corpo;             /* HTML da página */
} Resp;

typedef void (*Tratador)(const Req *, Resp *);

/* Abre o servidor em 127.0.0.1:porta e atende um pedido por vez. Não retorna. */
int servidor_rodar(int porta, Tratador tratar);

#endif
