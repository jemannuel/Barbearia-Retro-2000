#ifndef APP_H
#define APP_H

#include <sqlite3.h>
#include "util.h"
#include "http.h"

/* Banco de dados aberto (um só para o programa inteiro) */
extern sqlite3 *db;

/* Dados de quem está logado */
typedef struct {
    int  uid;
    char perfil[16];     /* cliente, barbeiro, recepcionista ou dono */
    char nome[101];
} Sessao;

/* Atalho para ligar um texto a um "?" do comando SQL */
#define BT(st, i, s) sqlite3_bind_text((st), (i), (s), -1, SQLITE_TRANSIENT)

/* ---------- banco (app.c) ---------- */
int           db_abrir(const char *arquivo, const char *schema);
sqlite3_stmt *db_prep(const char *sql);
const char   *col_txt(sqlite3_stmt *st, int i);
void          config_get(const char *chave, char *saida, size_t max);
void          config_set(const char *chave, const char *valor);
int           usuario_criar(const char *nome, const char *login, const char *tel,
                            const char *senha, const char *perfil);   /* devolve o id ou 0 */
int           usuario_trocar_senha(int uid, const char *nova);
void          dias_texto(char *saida, size_t max);

/* ---------- sessões e permissões (app.c) ---------- */
const Sessao *sessao_atual(const Req *rq);
void          sessao_criar(Resp *rs, int uid, const char *perfil, const char *nome);
void          sessao_encerrar(const Req *rq, Resp *rs);
void          sessoes_encerrar_usuario(int uid);
void          sessao_trocar_nome(int uid, const char *nome);
const Sessao *exigir(const Req *rq, Resp *rs, const char *perfis);    /* "dono,recepcionista" */

/* ---------- páginas (app.c) ---------- */
void        redirecionar(Resp *rs, const char *destino);
void        redirecionar_msg(Resp *rs, const char *base, int ok, const char *texto);
void        pagina_inicio(Resp *rs, const Req *rq, const char *titulo, const Sessao *s);
void        pagina_fim(Resp *rs);
void        pagina_msg(Resp *rs, const Req *rq, const Sessao *s, int status,
                       const char *titulo, const char *msg, const char *url, const char *txt_link);
void        preco_fmt(int centavos, char *saida, size_t max);
int         destino_seguro(const char *d);
const char *rotulo_status(const char *status);
const char *rotulo_perfil(const char *perfil);
const char *area_do_perfil(const char *perfil);

/* ---------- regras da agenda (agenda.c) ---------- */
int  antecedencia_ok(const char *data, int slot);

/* ---------- telas: públicas e do cliente (pub.c) ---------- */
void h_home(const Req *, Resp *);
void h_login_get(const Req *, Resp *);
void h_login_post(const Req *, Resp *);
void h_sair(const Req *, Resp *);
void h_cadastro_get(const Req *, Resp *);
void h_cadastro_post(const Req *, Resp *);
void h_cliente(const Req *, Resp *);
void h_cliente_cancelar(const Req *, Resp *);
void h_dados_get(const Req *, Resp *);
void h_dados_post(const Req *, Resp *);
void h_cliente_excluir(const Req *, Resp *);
void h_senha_get(const Req *, Resp *);
void h_senha_post(const Req *, Resp *);

/* ---------- telas: agenda (agenda.c) ---------- */
void h_horarios(const Req *, Resp *);
void h_agendar_get(const Req *, Resp *);
void h_agendar_post(const Req *, Resp *);
void h_barbeiro(const Req *, Resp *);
void h_barbeiro_status(const Req *, Resp *);
void h_barbeiro_bloquear(const Req *, Resp *);
void h_recepcao(const Req *, Resp *);
void h_recepcao_status(const Req *, Resp *);
void h_recepcao_novo(const Req *, Resp *);
void h_recepcao_confirmar(const Req *, Resp *);

/* ---------- telas: dono e administração (admin.c) ---------- */
void h_dono(const Req *, Resp *);
void h_func_get(const Req *, Resp *);
void h_func_novo(const Req *, Resp *);
void h_func_ativo(const Req *, Resp *);
void h_func_senha(const Req *, Resp *);
void h_serv_get(const Req *, Resp *);
void h_serv_salvar(const Req *, Resp *);
void h_config_get(const Req *, Resp *);
void h_config_post(const Req *, Resp *);
void h_relatorios(const Req *, Resp *);
void h_clientes(const Req *, Resp *);
void h_cliente_senha(const Req *, Resp *);

#endif
