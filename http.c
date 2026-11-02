/* http.c - servidor HTTP mínimo feito com sockets (funciona no Windows e no Linux) */
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define FECHAR(s)        closesocket(s)
  #define SOCK_INVALIDO(s) ((s) == INVALID_SOCKET)
  #define FLAGS_ENVIO      0
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define FECHAR(s)        close(s)
  #define SOCK_INVALIDO(s) ((s) < 0)
  #ifdef MSG_NOSIGNAL
    #define FLAGS_ENVIO    MSG_NOSIGNAL
  #else
    #define FLAGS_ENVIO    0
  #endif
#endif

#define MAX_PEDIDO 16384    /* tamanho máximo do cabeçalho do pedido */
#define MAX_CORPO   8192    /* tamanho máximo dos dados de um formulário */

/* Compara o começo de s com p ignorando maiúsculas/minúsculas */
static int prefixo_ci(const char *s, const char *p) {
    while (*p) {
        char a = *s, b = *p;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
        s++; p++;
    }
    return 1;
}

/* Procura uma linha de cabeçalho (ex: "Content-Length") e devolve o ponteiro para o valor */
static const char *achar_cabecalho(const char *ini, const char *fim, const char *nome) {
    size_t nl = strlen(nome);
    const char *p = strstr(ini, "\r\n");          /* pula a linha "GET /x HTTP/1.1" */
    while (p && p < fim) {
        p += 2;
        if (prefixo_ci(p, nome) && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ') p++;
            return p;
        }
        p = strstr(p, "\r\n");
    }
    return NULL;
}

static int ler_pedido(sock_t c, Req *rq) {
    char *raw = malloc(MAX_PEDIDO + 1);
    size_t n = 0;
    char *fim_cab = NULL;
    char alvo[1300];
    char *interr;
    const char *v;
    int tam_corpo = 0;
    size_t ja_lido;

    if (!raw) return 0;
    memset(rq, 0, sizeof *rq);

    while (n < MAX_PEDIDO) {
        int r = recv(c, raw + n, (int)(MAX_PEDIDO - n), 0);
        if (r <= 0) break;
        n += (size_t)r;
        raw[n] = '\0';
        fim_cab = strstr(raw, "\r\n\r\n");
        if (fim_cab) break;
    }
    if (!fim_cab) { free(raw); return 0; }

    /* linha do pedido: "GET /caminho?x=1 HTTP/1.1" */
    if (sscanf(raw, "%7s %1299s", rq->metodo, alvo) != 2) { free(raw); return 0; }
    if (strlen(alvo) >= sizeof rq->caminho + 800) { free(raw); return 0; }
    interr = strchr(alvo, '?');
    if (interr) {
        *interr = '\0';
        snprintf(rq->query, sizeof rq->query, "%s", interr + 1);
    }
    if (strlen(alvo) >= sizeof rq->caminho) { free(raw); return 0; }   /* endereço longo demais */
    strcpy(rq->caminho, alvo);

    /* cookie "sid=..." */
    v = achar_cabecalho(raw, fim_cab, "Cookie");
    if (v) {
        const char *linha_fim = strstr(v, "\r\n");
        const char *p = v;
        while (p && (!linha_fim || p < linha_fim)) {
            if (strncmp(p, "sid=", 4) == 0 && (p == v || p[-1] == ' ' || p[-1] == ';')) {
                size_t i = 0;
                p += 4;
                while (p[i] && p[i] != ';' && p[i] != '\r' && i < sizeof rq->cookie_sid - 1) {
                    rq->cookie_sid[i] = p[i];
                    i++;
                }
                rq->cookie_sid[i] = '\0';
                break;
            }
            p = strchr(p, ';');
            if (p) { p++; while (*p == ' ') p++; }
        }
    }

    /* corpo (formulários POST) */
    v = achar_cabecalho(raw, fim_cab, "Content-Length");
    if (v) tam_corpo = atoi(v);
    if (tam_corpo < 0 || tam_corpo > MAX_CORPO) { free(raw); return 0; }

    rq->corpo = calloc((size_t)tam_corpo + 1, 1);
    if (!rq->corpo) { free(raw); return 0; }
    ja_lido = n - (size_t)((fim_cab + 4) - raw);
    if (ja_lido > (size_t)tam_corpo) ja_lido = (size_t)tam_corpo;
    memcpy(rq->corpo, fim_cab + 4, ja_lido);
    while (ja_lido < (size_t)tam_corpo) {
        int r = recv(c, rq->corpo + ja_lido, (int)((size_t)tam_corpo - ja_lido), 0);
        if (r <= 0) break;
        ja_lido += (size_t)r;
    }
    rq->corpo[ja_lido] = '\0';
    free(raw);
    return 1;
}

static void enviar_tudo(sock_t c, const char *d, size_t n) {
    while (n > 0) {
        int r = send(c, d, (int)n, FLAGS_ENVIO);
        if (r <= 0) return;
        d += r;
        n -= (size_t)r;
    }
}

static const char *texto_status(int s) {
    switch (s) {
        case 200: return "OK";
        case 303: return "See Other";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        default:  return "Bad Request";
    }
}

static void enviar_resposta(sock_t c, const Resp *rs) {
    char cab[1800];
    int n = snprintf(cab, sizeof cab,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n",
        rs->status, texto_status(rs->status), (unsigned long)rs->corpo.len);
    if (rs->status == 303 && rs->local[0])
        n += snprintf(cab + n, sizeof cab - (size_t)n, "Location: %s\r\n", rs->local);
    if (rs->cookie[0])
        n += snprintf(cab + n, sizeof cab - (size_t)n, "Set-Cookie: %s\r\n", rs->cookie);
    n += snprintf(cab + n, sizeof cab - (size_t)n, "\r\n");
    enviar_tudo(c, cab, (size_t)n);
    enviar_tudo(c, rs->corpo.d, rs->corpo.len);
}

int servidor_rodar(int porta, Tratador tratar) {
    sock_t s;
    struct sockaddr_in addr;
    int opt = 1;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Erro ao iniciar o Winsock\n");
        return 1;
    }
#endif

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (SOCK_INVALIDO(s)) { fprintf(stderr, "Erro ao criar o socket\n"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)porta);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);    /* só aceita conexões deste computador */

    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "Erro: a porta %d já está em uso. Feche o outro programa e tente de novo.\n", porta);
        return 1;
    }
    if (listen(s, 16) != 0) { fprintf(stderr, "Erro no listen\n"); return 1; }

    for (;;) {
        sock_t c = accept(s, NULL, NULL);
        Req rq;
        Resp rs;
        if (SOCK_INVALIDO(c)) continue;
#ifdef _WIN32
        { DWORD tv = 5000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv); }
#else
        { struct timeval tv; tv.tv_sec = 5; tv.tv_usec = 0; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); }
#endif
        if (ler_pedido(c, &rq)) {
            memset(&rs, 0, sizeof rs);
            rs.status = 200;
            buf_init(&rs.corpo);
            tratar(&rq, &rs);
            enviar_resposta(c, &rs);
            buf_free(&rs.corpo);
            free(rq.corpo);
        }
        FECHAR(c);
    }
    return 0;
}
