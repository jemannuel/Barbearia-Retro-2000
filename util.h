#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* ---------- Texto que cresce sozinho (usado para montar as páginas HTML) ---------- */
typedef struct {
    char  *d;      /* conteúdo */
    size_t len;    /* tamanho usado */
    size_t cap;    /* tamanho reservado */
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_add(Buf *b, const char *formato, ...);   /* igual ao printf */
void buf_esc(Buf *b, const char *texto);          /* escreve texto protegendo caracteres do HTML */

/* ---------- Formulários e endereços ---------- */
void url_decode(const char *origem, char *destino, size_t max);
void url_encode(const char *origem, char *destino, size_t max);
int  param(const char *dados, const char *chave, char *saida, size_t max);  /* 1 se achou */
int  param_int(const char *dados, const char *chave, int padrao);

/* ---------- Textos ---------- */
void aparar(char *s);        /* tira espaços do começo e do fim */
void minusculas(char *s);

/* ---------- Senhas ---------- */
void util_iniciar(void);
void gerar_aleatorio(char *saida, size_t n);          /* n inclui o \0; devolve texto hexadecimal */
void gerar_senha_temporaria(char *saida, size_t n);
void sha256_hex(const char *texto, char *saida65);
void hash_senha(const char *salt, const char *senha, char *saida65);
int  senha_confere(const char *salt, const char *hash_guardado, const char *senha);

/* ---------- Datas (formato AAAA-MM-DD) e horários em blocos de 30 minutos ---------- */
void hoje(char *saida);                               /* saida com pelo menos 11 bytes */
int  data_valida(const char *s);
int  dia_semana(const char *data);                    /* 0 = domingo ... 6 = sábado */
void data_mais_dias(const char *data, int dias, char *saida);
void data_br(const char *data, char *saida);          /* AAAA-MM-DD -> DD/MM/AAAA */
long minutos_ate(const char *data, int slot);         /* minutos de agora até o início do bloco */
void slot_texto(int slot, char *saida);               /* 18 -> "09:00" */
int  hora_para_slot(const char *hhmm);                /* "09:30" -> 19; -1 se inválido */

#endif
