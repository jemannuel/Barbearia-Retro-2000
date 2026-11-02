/* util.c - funções de apoio: texto, formulários, senhas (SHA-256) e datas */
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

/* ============================ Buf ============================ */

void buf_init(Buf *b) {
    b->cap = 1024;
    b->len = 0;
    b->d = malloc(b->cap);
    if (!b->d) { fprintf(stderr, "Sem memória\n"); exit(1); }
    b->d[0] = '\0';
}

void buf_free(Buf *b) {
    free(b->d);
    b->d = NULL;
    b->len = b->cap = 0;
}

static void buf_garantir(Buf *b, size_t preciso) {
    if (preciso <= b->cap) return;
    size_t nova = b->cap ? b->cap : 1024;
    while (nova < preciso) nova *= 2;
    char *p = realloc(b->d, nova);
    if (!p) { fprintf(stderr, "Sem memória\n"); exit(1); }
    b->d = p;
    b->cap = nova;
}

void buf_add(Buf *b, const char *formato, ...) {
    va_list ap, ap2;
    va_start(ap, formato);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, formato, ap2);   /* só mede o tamanho */
    va_end(ap2);
    if (n < 0) { va_end(ap); return; }
    buf_garantir(b, b->len + (size_t)n + 1);
    vsnprintf(b->d + b->len, (size_t)n + 1, formato, ap);
    b->len += (size_t)n;
    va_end(ap);
}

/* Troca & < > " ' por códigos HTML. Isso impede que um nome como
   <script>...</script> seja executado pelo navegador (ataque XSS). */
void buf_esc(Buf *b, const char *t) {
    for (; *t; t++) {
        switch (*t) {
            case '&':  buf_add(b, "&amp;");  break;
            case '<':  buf_add(b, "&lt;");   break;
            case '>':  buf_add(b, "&gt;");   break;
            case '"':  buf_add(b, "&quot;"); break;
            case '\'': buf_add(b, "&#39;");  break;
            default:   buf_add(b, "%c", *t); break;
        }
    }
}

/* ============================ Formulários ============================ */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode(const char *o, char *d, size_t max) {
    size_t n = 0;
    if (max == 0) return;
    while (*o && n + 1 < max) {
        if (*o == '+') {
            d[n++] = ' ';
            o++;
        } else if (*o == '%' && hexval(o[1]) >= 0 && hexval(o[2]) >= 0) {
            d[n++] = (char)(hexval(o[1]) * 16 + hexval(o[2]));
            o += 3;
        } else {
            d[n++] = *o++;
        }
    }
    d[n] = '\0';
}

void url_encode(const char *o, char *d, size_t max) {
    size_t n = 0;
    if (max == 0) return;
    for (; *o && n + 4 < max; o++) {
        unsigned char c = (unsigned char)*o;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            d[n++] = (char)c;
        } else {
            snprintf(d + n, 4, "%%%02X", c);
            n += 3;
        }
    }
    d[n] = '\0';
}

/* Procura "chave=valor" em dados no formato "a=1&b=2" */
int param(const char *dados, const char *chave, char *saida, size_t max) {
    size_t kl = strlen(chave);
    const char *p = dados;
    if (max > 0) saida[0] = '\0';
    while (p && *p) {
        if (strncmp(p, chave, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t n = strcspn(v, "&");
            char tmp[1100];
            if (n >= sizeof tmp) n = sizeof tmp - 1;
            memcpy(tmp, v, n);
            tmp[n] = '\0';
            url_decode(tmp, saida, max);
            return 1;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return 0;
}

int param_int(const char *dados, const char *chave, int padrao) {
    char tmp[32];
    if (!param(dados, chave, tmp, sizeof tmp) || tmp[0] == '\0') return padrao;
    return atoi(tmp);
}

void aparar(char *s) {
    size_t i = 0, n;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

void minusculas(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* ============================ SHA-256 ============================ */

static const unsigned int K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static unsigned int ror(unsigned int x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256(const unsigned char *msg, size_t len, unsigned char out[32]) {
    unsigned int h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    size_t total = ((len + 9 + 63) / 64) * 64;          /* mensagem + 0x80 + tamanho, em blocos de 64 */
    unsigned char *p = calloc(total, 1);
    unsigned long long bits = (unsigned long long)len * 8;
    if (!p) { fprintf(stderr, "Sem memória\n"); exit(1); }
    memcpy(p, msg, len);
    p[len] = 0x80;
    for (int i = 0; i < 8; i++) p[total - 1 - i] = (unsigned char)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        unsigned int w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned int)p[off + 4 * i] << 24) | ((unsigned int)p[off + 4 * i + 1] << 16) |
                   ((unsigned int)p[off + 4 * i + 2] << 8) | (unsigned int)p[off + 4 * i + 3];
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned int s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            unsigned int S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            unsigned int ch = (e & f) ^ (~e & g);
            unsigned int t1 = hh + S1 + ch + K[i] + w[i];
            unsigned int S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    free(p);
    for (int i = 0; i < 8; i++) {
        out[4 * i]     = (unsigned char)(h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h[i] >> 8);
        out[4 * i + 3] = (unsigned char)(h[i]);
    }
}

void sha256_hex(const char *texto, char *saida65) {
    unsigned char h[32];
    sha256((const unsigned char *)texto, strlen(texto), h);
    for (int i = 0; i < 32; i++) snprintf(saida65 + 2 * i, 3, "%02x", h[i]);
}

/* ============================ Senhas ============================ */

void util_iniciar(void) {
    srand((unsigned)(time(NULL) * 2654435761u + (unsigned)clock()));
}

/* Preenche o vetor com bytes aleatórios. No Linux/Mac usa /dev/urandom (seguro).
   No Windows cai no rand(), suficiente para esta demonstração local. */
static void bytes_aleatorios(unsigned char *v, size_t n) {
    size_t i = 0;
#ifndef _WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        i = fread(v, 1, n, f);
        fclose(f);
    }
#endif
    for (; i < n; i++) v[i] = (unsigned char)(rand() & 0xFF);
}

void gerar_aleatorio(char *saida, size_t n) {
    static const char hex[] = "0123456789abcdef";
    unsigned char v[128];
    size_t i;
    if (n == 0) return;
    if (n > sizeof v) n = sizeof v;
    bytes_aleatorios(v, n);
    for (i = 0; i + 1 < n; i++) saida[i] = hex[v[i] % 16];
    saida[i] = '\0';
}

void gerar_senha_temporaria(char *saida, size_t n) {
    static const char letras[] = "abcdefghjkmnpqrstuvwxyz23456789";   /* sem letras que se confundem */
    unsigned char v[64];
    size_t i;
    if (n == 0) return;
    if (n > sizeof v) n = sizeof v;
    bytes_aleatorios(v, n);
    for (i = 0; i + 1 < n; i++) saida[i] = letras[v[i] % (sizeof letras - 1)];
    saida[i] = '\0';
}

/* A senha nunca é guardada. Guardamos o SHA-256 de (salt + senha).
   O salt é um texto aleatório diferente para cada usuário. */
void hash_senha(const char *salt, const char *senha, char *saida65) {
    char junto[300];
    snprintf(junto, sizeof junto, "%s%s", salt, senha);
    sha256_hex(junto, saida65);
}

int senha_confere(const char *salt, const char *hash_guardado, const char *senha) {
    char h[65];
    hash_senha(salt, senha, h);
    return strcmp(h, hash_guardado) == 0;
}

/* ============================ Datas ============================ */

void hoje(char *saida) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    strftime(saida, 11, "%Y-%m-%d", lt);
}

static int ler_data(const char *s, int *y, int *m, int *d) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    return sscanf(s, "%4d-%2d-%2d", y, m, d) == 3;
}

int data_valida(const char *s) {
    int y, m, d;
    struct tm t;
    if (!ler_data(s, &y, &m, &d)) return 0;
    if (y < 2000 || y > 2100) return 0;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12; t.tm_isdst = -1;
    if (mktime(&t) == (time_t)-1) return 0;
    return t.tm_mday == d && t.tm_mon == m - 1;     /* se mudou, a data não existia (ex: 31/02) */
}

int dia_semana(const char *data) {
    int y, m, d;
    struct tm t;
    if (!ler_data(data, &y, &m, &d)) return 0;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12; t.tm_isdst = -1;
    mktime(&t);
    return t.tm_wday;
}

void data_mais_dias(const char *data, int dias, char *saida) {
    int y, m, d;
    struct tm t;
    if (!ler_data(data, &y, &m, &d)) { hoje(saida); return; }
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d + dias; t.tm_hour = 12; t.tm_isdst = -1;
    mktime(&t);
    strftime(saida, 11, "%Y-%m-%d", &t);
}

void data_br(const char *data, char *saida) {
    int y, m, d;
    if (!ler_data(data, &y, &m, &d)) { snprintf(saida, 11, "%s", "--/--/----"); return; }
    snprintf(saida, 11, "%02d/%02d/%04d", d, m, y);
}

long minutos_ate(const char *data, int slot) {
    int y, m, d;
    struct tm t;
    time_t alvo;
    if (!ler_data(data, &y, &m, &d)) return -1;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    t.tm_hour = slot / 2; t.tm_min = (slot % 2) * 30; t.tm_isdst = -1;
    alvo = mktime(&t);
    return (long)(difftime(alvo, time(NULL)) / 60.0);
}

void slot_texto(int slot, char *saida) {
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%02d:%02d", (slot / 2) % 100, ((slot % 2) * 30) % 100);
    memcpy(saida, tmp, 6);      /* "HH:MM" + terminador */
    saida[5] = '\0';
}

int hora_para_slot(const char *hhmm) {
    int h, m;
    if (strlen(hhmm) < 4 || sscanf(hhmm, "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || (m != 0 && m != 30)) return -1;
    return h * 2 + m / 30;
}
