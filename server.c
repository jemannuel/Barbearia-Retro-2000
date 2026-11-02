/* ============================================================
   server.c - Sistema de agendamento da Barbearia Retrô 2000
   PIM II - ADS - UNIP

   Como funciona:
   1. O navegador pede uma página (ex: /horarios).
   2. http.c recebe o pedido e chama a função tratar(), aqui embaixo.
   3. tratar() descobre quem é o usuário (sessão) e chama a função da página.
   4. A função da página consulta o banco SQLite, monta o HTML e devolve.
   ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "sqlite3.h"
#include "http.h"
#include "util.h"

static sqlite3 *db;

static const char *DIAS[] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };

/* ---------------------------------------------------------------
   Estilo das páginas (cores retrô, contraste alto, botões grandes)
   --------------------------------------------------------------- */
#define CSS \
 "*{box-sizing:border-box}" \
 "body{margin:0;font-family:Arial,Helvetica,sans-serif;font-size:17px;line-height:1.5;background:#f4ead5;color:#2b1d14}" \
 "header{background:#3b2418;color:#f4ead5;padding:12px 16px;border-bottom:6px solid #a4161a}" \
 "header h1{margin:0 0 6px;font-size:1.5em;letter-spacing:1px}" \
 "nav a{color:#f4ead5;display:inline-block;margin:2px 14px 2px 0;padding:4px 0}" \
 "main{max-width:980px;margin:0 auto;padding:16px}" \
 "footer{text-align:center;font-size:.85em;padding:20px;color:#5a4636}" \
 "h2{border-bottom:3px double #a4161a;padding-bottom:4px}" \
 "a{color:#7a1013}" \
 "a:focus,button:focus,input:focus,select:focus,summary:focus{outline:3px solid #1b4fd8;outline-offset:2px}" \
 ".pular{position:absolute;left:-999px}.pular:focus{left:8px;top:8px;background:#fff;padding:6px;z-index:9}" \
 "table{border-collapse:collapse;width:100%;background:#fffaf0;margin:10px 0}" \
 "caption{text-align:left;font-weight:bold;padding:6px 0}" \
 "th,td{border:1px solid #b9a98f;padding:6px 8px;text-align:left;vertical-align:middle}" \
 "th{background:#3b2418;color:#f4ead5}" \
 "label{display:block;margin:10px 0 2px;font-weight:bold}" \
 "input,select{font-size:1em;padding:8px;border:2px solid #5a4636;border-radius:4px;background:#fff;color:#2b1d14;max-width:100%}" \
 "input[type=checkbox]{width:20px;height:20px;vertical-align:middle}" \
 "button,.botao{font-size:1em;min-height:44px;padding:8px 16px;border:0;border-radius:4px;background:#a4161a;color:#fff;cursor:pointer;text-decoration:none;display:inline-block}" \
 ".leve{background:#5a4636}.perigo{background:#5c0a0c}" \
 "td form{display:inline}td button{min-height:36px;padding:4px 10px;font-size:.9em;margin:2px}" \
 ".aviso{padding:10px 14px;margin:12px 0;border-left:6px solid #1c6b2a;background:#e3f3e5}" \
 ".erro{border-left-color:#a4161a;background:#fbe3e3}" \
 ".cartao{background:#fffaf0;border:1px solid #b9a98f;padding:6px 16px 14px;margin:14px 0}" \
 ".slots form{display:inline}.slots button{min-width:78px;margin:3px}" \
 ".livre{display:inline-block;border:2px solid #5a4636;border-radius:4px;padding:4px 10px;margin:3px;background:#fff}" \
 ".st-concluido{color:#1c6b2a;font-weight:bold}.st-faltou,.st-cancelado{color:#a4161a;font-weight:bold}.st-bloqueado{color:#5a4636;font-style:italic}" \
 "pre{background:#2b1d14;color:#f4ead5;padding:10px;overflow:auto;white-space:pre-wrap}" \
 "@media(max-width:600px){body{font-size:16px}th,td{padding:4px}}"

/* ===============================================================
   Parte 1 - Banco de dados (funções de apoio)
   =============================================================== */

static sqlite3_stmt *prep(const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Erro SQL: %s\n  em: %s\n", sqlite3_errmsg(db), sql);
        return NULL;
    }
    return st;
}

static void exec_sql(const char *sql) {
    char *e = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
        fprintf(stderr, "Erro SQL: %s\n", e ? e : "?");
        sqlite3_free(e);
    }
}

static const char *col(sqlite3_stmt *st, int i) {
    const unsigned char *t = sqlite3_column_text(st, i);
    return t ? (const char *)t : "";
}

static void cfg_get(const char *chave, char *saida, size_t n) {
    sqlite3_stmt *st = prep("SELECT valor FROM config WHERE chave=?");
    saida[0] = '\0';
    if (!st) return;
    sqlite3_bind_text(st, 1, chave, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) snprintf(saida, n, "%s", col(st, 0));
    sqlite3_finalize(st);
}

static void cfg_set(const char *chave, const char *valor) {
    sqlite3_stmt *st = prep("INSERT OR REPLACE INTO config (chave, valor) VALUES (?, ?)");
    if (!st) return;
    sqlite3_bind_text(st, 1, chave, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, valor, -1, SQLITE_STATIC);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

static int cfg_slot(const char *chave, int padrao) {
    char v[16];
    int s;
    cfg_get(chave, v, sizeof v);
    s = hora_para_slot(v);
    return s < 0 ? padrao : s;
}

static void dinheiro(int centavos, char *saida) {
    snprintf(saida, 24, "R$ %d,%02d", centavos / 100, centavos % 100);
}

/* Converte "35", "35,50" ou "35.50" em centavos. Devolve -1 se o texto não for um preço válido. */
static int parse_dinheiro(const char *txt) {
    char t[48];
    char *p, *fim;
    double v;
    snprintf(t, sizeof t, "%s", txt);
    for (p = t; *p; p++) if (*p == ',') *p = '.';
    v = strtod(t, &fim);
    if (fim == t) return -1;                       /* não leu nenhum número (ex: "abc") */
    while (*fim == ' ') fim++;
    if (*fim != '\0') return -1;                   /* sobrou texto depois do número (ex: "12x") */
    if (!isfinite(v) || v < 0 || v > 10000) return -1;
    return (int)floor(v * 100.0 + 0.5);
}

/* Cria um usuário. Devolve o id, ou 0 se o login já existe. */
static int inserir_usuario(const char *nome, const char *login, const char *tel,
                           const char *senha, const char *perfil) {
    char salt[33], h[65];
    sqlite3_stmt *st;
    int rc, id = 0;
    gerar_aleatorio(salt, sizeof salt);
    hash_senha(salt, senha, h);
    st = prep("INSERT INTO usuarios (nome, login, telefone, salt, senha_hash, perfil) VALUES (?,?,?,?,?,?)");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, nome, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, login, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, tel, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, h, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, perfil, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) id = (int)sqlite3_last_insert_rowid(db);
    return id;
}

static void trocar_senha(int uid, const char *nova) {
    char salt[33], h[65];
    sqlite3_stmt *st = prep("UPDATE usuarios SET salt=?, senha_hash=? WHERE id=?");
    gerar_aleatorio(salt, sizeof salt);
    hash_senha(salt, nova, h);
    if (!st) return;
    sqlite3_bind_text(st, 1, salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, h, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, uid);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* ===============================================================
   Parte 2 - Sessões (quem está logado)
   O navegador guarda um cookie "sid"; aqui na memória ligamos
   esse código ao usuário. Ao fechar o programa, todos saem.
   =============================================================== */

#define MAX_SESS 64
#define TEMPO_SESSAO 7200   /* 2 horas sem usar o site */

typedef struct { char sid[33]; int uid; char perfil[16]; char nome[100]; time_t ultimo; } SessaoSrv;
static SessaoSrv sess[MAX_SESS];

typedef struct { int logado; int uid; char perfil[16]; char nome[100]; } Sessao;

static void sessao_ler(const Req *rq, Sessao *s) {
    int i;
    memset(s, 0, sizeof *s);
    if (!rq->cookie_sid[0]) return;
    for (i = 0; i < MAX_SESS; i++) {
        if (sess[i].sid[0] && strcmp(sess[i].sid, rq->cookie_sid) == 0) {
            if (time(NULL) - sess[i].ultimo > TEMPO_SESSAO) { sess[i].sid[0] = '\0'; return; }
            sess[i].ultimo = time(NULL);
            s->logado = 1;
            s->uid = sess[i].uid;
            snprintf(s->perfil, sizeof s->perfil, "%s", sess[i].perfil);
            snprintf(s->nome, sizeof s->nome, "%s", sess[i].nome);
            return;
        }
    }
}

static void sessao_criar(Resp *rs, int uid, const char *perfil, const char *nome) {
    int i, escolhido = 0;
    time_t mais_velho = time(NULL) + 1;
    for (i = 0; i < MAX_SESS; i++) {
        if (!sess[i].sid[0]) { escolhido = i; break; }
        if (sess[i].ultimo < mais_velho) { mais_velho = sess[i].ultimo; escolhido = i; }
    }
    gerar_aleatorio(sess[escolhido].sid, sizeof sess[escolhido].sid);
    sess[escolhido].uid = uid;
    snprintf(sess[escolhido].perfil, sizeof sess[escolhido].perfil, "%s", perfil);
    snprintf(sess[escolhido].nome, sizeof sess[escolhido].nome, "%s", nome);
    sess[escolhido].ultimo = time(NULL);
    snprintf(rs->cookie, sizeof rs->cookie, "sid=%s; Path=/; HttpOnly; SameSite=Strict", sess[escolhido].sid);
}

static void sessao_encerrar_usuario(int uid) {
    int i;
    for (i = 0; i < MAX_SESS; i++)
        if (sess[i].sid[0] && sess[i].uid == uid) sess[i].sid[0] = '\0';
}

static void sessao_sair(const Req *rq, Resp *rs) {
    int i;
    for (i = 0; i < MAX_SESS; i++)
        if (sess[i].sid[0] && strcmp(sess[i].sid, rq->cookie_sid) == 0) sess[i].sid[0] = '\0';
    snprintf(rs->cookie, sizeof rs->cookie, "sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

/* ===============================================================
   Parte 3 - Apoio às páginas
   =============================================================== */

static void redir(Resp *rs, const char *destino, const char *msg, int erro) {
    char enc[600];
    rs->status = 303;
    if (msg && msg[0]) {
        url_encode(msg, enc, sizeof enc);
        snprintf(rs->local, sizeof rs->local, "%s%c%s=%s", destino,
                 strchr(destino, '?') ? '&' : '?', erro ? "erro" : "msg", enc);
    } else {
        snprintf(rs->local, sizeof rs->local, "%s", destino);
    }
}

/* Só aceita voltar para endereços internos do próprio site */
static const char *destino_seguro(const char *d, const char *padrao) {
    if (d[0] == '/' && d[1] != '/' && d[1] != '\\' && strlen(d) < 200) return d;
    return padrao;
}

static const char *home_do_perfil(const char *perfil) {
    if (strcmp(perfil, "cliente") == 0) return "/cliente";
    if (strcmp(perfil, "barbeiro") == 0) return "/barbeiro";
    if (strcmp(perfil, "recepcionista") == 0) return "/agenda";
    if (strcmp(perfil, "dono") == 0) return "/dono";
    return "/";
}

/* Controle de acesso: só deixa passar quem tem um dos perfis da lista */
static int exige(const Sessao *s, Resp *rs, const char *perfis) {
    if (!s->logado) { redir(rs, "/login", "Entre na sua conta para continuar.", 1); return 0; }
    if (!strstr(perfis, s->perfil)) { redir(rs, home_do_perfil(s->perfil), "Você não tem acesso a essa área.", 1); return 0; }
    return 1;
}

static int dia_aberto(const char *data) {
    char dias[16];
    cfg_get("dias_abertos", dias, sizeof dias);
    return strchr(dias, '0' + dia_semana(data)) != NULL;
}

static void pagina_inicio(Buf *b, const Req *rq, const Sessao *s, const char *titulo) {
    char m[300], e[300];
    buf_add(b, "<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n<meta charset=\"utf-8\">\n"
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>");
    buf_esc(b, titulo);
    buf_add(b, " - Barbearia Retrô 2000</title>\n<style>%s</style>\n</head>\n<body>\n", CSS);
    buf_add(b, "<a class=\"pular\" href=\"#conteudo\">Ir para o conteúdo</a>\n");
    buf_add(b, "<header><h1>Barbearia Retrô 2000</h1>\n<nav aria-label=\"Menu principal\">");
    if (!s->logado) {
        buf_add(b, "<a href=\"/\">Início</a><a href=\"/horarios\">Horários</a>"
                   "<a href=\"/login\">Entrar</a><a href=\"/cadastro\">Criar conta</a>");
    } else if (strcmp(s->perfil, "cliente") == 0) {
        buf_add(b, "<a href=\"/\">Início</a><a href=\"/horarios\">Agendar</a>"
                   "<a href=\"/cliente\">Meus agendamentos</a><a href=\"/conta\">Minha conta</a><a href=\"/sair\">Sair</a>");
    } else if (strcmp(s->perfil, "barbeiro") == 0) {
        buf_add(b, "<a href=\"/barbeiro\">Minha agenda</a><a href=\"/conta\">Minha conta</a><a href=\"/sair\">Sair</a>");
    } else if (strcmp(s->perfil, "recepcionista") == 0) {
        buf_add(b, "<a href=\"/agenda\">Agenda geral</a><a href=\"/clientes\">Clientes</a>"
                   "<a href=\"/conta\">Minha conta</a><a href=\"/sair\">Sair</a>");
    } else {
        buf_add(b, "<a href=\"/dono\">Painel</a><a href=\"/agenda\">Agenda geral</a><a href=\"/funcionarios\">Funcionários</a>"
                   "<a href=\"/servicos\">Serviços</a><a href=\"/clientes\">Clientes</a><a href=\"/relatorios\">Relatórios</a>"
                   "<a href=\"/config\">Configurações</a><a href=\"/conta\">Minha conta</a><a href=\"/sair\">Sair</a>");
    }
    buf_add(b, "</nav>\n</header>\n<main id=\"conteudo\">\n");
    if (s->logado) {
        buf_add(b, "<p>Olá, <strong>");
        buf_esc(b, s->nome);
        buf_add(b, "</strong> (%s)</p>\n", s->perfil);
    }
    param(rq->query, "msg", m, sizeof m);
    param(rq->query, "erro", e, sizeof e);
    if (m[0]) { buf_add(b, "<div class=\"aviso\" role=\"status\">"); buf_esc(b, m); buf_add(b, "</div>\n"); }
    if (e[0]) { buf_add(b, "<div class=\"aviso erro\" role=\"alert\">"); buf_esc(b, e); buf_add(b, "</div>\n"); }
    buf_add(b, "<h2>"); buf_esc(b, titulo); buf_add(b, "</h2>\n");
}

static void pagina_fim(Buf *b) {
    buf_add(b, "</main>\n<footer>Projeto acadêmico PIM II - UNIP. Barbearia, endereço e dados fictícios.</footer>\n</body>\n</html>\n");
}

/* <option> com os horários de 30 em 30 minutos entre dois blocos */
static void opcoes_hora(Buf *b, int de, int ate, int selecionado) {
    int i;
    char t[8];
    for (i = de; i <= ate; i++) {
        slot_texto(i, t);
        buf_add(b, "<option value=\"%d\"%s>%s</option>", i, i == selecionado ? " selected" : "", t);
    }
}

/* Agendamentos que ocupam horários de um barbeiro em um dia */
static void ocupacao(int barbeiro_id, const char *data, int occ[48]) {
    sqlite3_stmt *st = prep("SELECT slot_inicio, slots FROM agendamentos "
                            "WHERE barbeiro_id=? AND data=? AND status!='cancelado'");
    int i;
    memset(occ, 0, 48 * sizeof(int));
    if (!st) return;
    sqlite3_bind_int(st, 1, barbeiro_id);
    sqlite3_bind_text(st, 2, data, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int ini = sqlite3_column_int(st, 0), n = sqlite3_column_int(st, 1);
        for (i = 0; i < n; i++) if (ini + i >= 0 && ini + i < 48) occ[ini + i] = 1;
    }
    sqlite3_finalize(st);
}

/* Regra RN01: um barbeiro não pode ter dois agendamentos no mesmo horário.
   Devolve NULL se deu certo, ou o texto do erro. */
static const char *criar_agendamento(int cliente_id, const char *nome_av, const char *tel_av,
                                     int barbeiro_id, int servico_id, const char *data,
                                     int slot, int cancelar_id) {
    char hj[11], lim[11];
    sqlite3_stmt *st;
    int dur = 0, n, ab, fe, i, ok, occ[48];

    if (!data_valida(data)) return "Data inválida.";
    hoje(hj);
    if (strcmp(data, hj) < 0) return "Não é possível agendar em uma data que já passou.";   /* RN02 */
    data_mais_dias(hj, 60, lim);
    if (strcmp(data, lim) > 0) return "Só é possível agendar até 60 dias à frente.";
    if (!dia_aberto(data)) return "A barbearia não abre nesse dia.";

    st = prep("SELECT duracao_min FROM servicos WHERE id=? AND ativo=1");
    if (!st) return "Erro interno.";
    sqlite3_bind_int(st, 1, servico_id);
    if (sqlite3_step(st) == SQLITE_ROW) dur = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (dur <= 0) return "Serviço inválido.";

    st = prep("SELECT 1 FROM usuarios WHERE id=? AND perfil='barbeiro' AND ativo=1");
    if (!st) return "Erro interno.";
    sqlite3_bind_int(st, 1, barbeiro_id);
    ok = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (!ok) return "Barbeiro inválido.";

    n = (dur + 29) / 30;                       /* quantos blocos de 30 min o serviço ocupa */
    ab = cfg_slot("abertura", 18);
    fe = cfg_slot("fechamento", 38);
    if (slot < ab || slot + n > fe) return "Esse horário está fora do funcionamento da barbearia.";
    if (minutos_ate(data, slot) <= 0) return "Esse horário já passou.";               /* RN02 */

    exec_sql("BEGIN IMMEDIATE");
    if (cancelar_id > 0) {                     /* remarcação: libera o horário antigo */
        st = prep("UPDATE agendamentos SET status='cancelado' WHERE id=?");
        if (st) { sqlite3_bind_int(st, 1, cancelar_id); sqlite3_step(st); sqlite3_finalize(st); }
    }
    ocupacao(barbeiro_id, data, occ);
    for (i = 0; i < n; i++) {
        if (occ[slot + i]) { exec_sql("ROLLBACK"); return "Esse horário não está mais disponível. Escolha outro."; }
    }
    st = prep("INSERT INTO agendamentos (cliente_id, nome_avulso, tel_avulso, barbeiro_id, servico_id, data, slot_inicio, slots) "
              "VALUES (?,?,?,?,?,?,?,?)");
    if (!st) { exec_sql("ROLLBACK"); return "Erro interno."; }
    if (cliente_id > 0) sqlite3_bind_int(st, 1, cliente_id); else sqlite3_bind_null(st, 1);
    if (nome_av && nome_av[0]) sqlite3_bind_text(st, 2, nome_av, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 2);
    if (tel_av && tel_av[0]) sqlite3_bind_text(st, 3, tel_av, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 3);
    sqlite3_bind_int(st, 4, barbeiro_id);
    sqlite3_bind_int(st, 5, servico_id);
    sqlite3_bind_text(st, 6, data, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 7, slot);
    sqlite3_bind_int(st, 8, n);
    ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) { exec_sql("ROLLBACK"); return "Não foi possível gravar o agendamento."; }
    exec_sql("COMMIT");
    return NULL;
}

static void data_extenso(const char *data, char *saida, size_t n) {
    char br[11];
    data_br(data, br);
    snprintf(saida, n, "%s (%s)", br, DIAS[dia_semana(data)]);
}

/* ===============================================================
   Parte 4 - Páginas públicas
   =============================================================== */

static void pg_inicio(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char v[200], ab[16], fe[16], dias[16], d[100];
    sqlite3_stmt *st;
    int i, primeiro = 1;

    pagina_inicio(b, rq, s, "Bem-vindo");
    buf_add(b, "<p>Barbearia tradicional do Sacomã, em São Paulo, atendendo desde 2000. "
               "Agende seu horário pelo site, sem fila e sem ligação.</p>\n");
    cfg_get("endereco", v, sizeof v);
    buf_add(b, "<div class=\"cartao\"><h3>Onde estamos</h3><p><strong>Endereço:</strong> ");
    buf_esc(b, v);
    cfg_get("telefone", v, sizeof v);
    buf_add(b, "<br><strong>Telefone:</strong> ");
    buf_esc(b, v);
    cfg_get("abertura", ab, sizeof ab);
    cfg_get("fechamento", fe, sizeof fe);
    cfg_get("dias_abertos", dias, sizeof dias);
    d[0] = '\0';
    for (i = 0; i < 7; i++) {
        if (strchr(dias, '0' + i)) {
            strncat(d, primeiro ? "" : ", ", sizeof d - strlen(d) - 1);
            strncat(d, DIAS[i], sizeof d - strlen(d) - 1);
            primeiro = 0;
        }
    }
    buf_add(b, "<br><strong>Funcionamento:</strong> %s, das %s às %s</p></div>\n", d, ab, fe);

    buf_add(b, "<table><caption>Serviços e preços</caption><tr><th scope=\"col\">Serviço</th>"
               "<th scope=\"col\">Duração</th><th scope=\"col\">Preço</th></tr>\n");
    st = prep("SELECT nome, duracao_min, preco_centavos FROM servicos WHERE ativo=1 ORDER BY preco_centavos");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        char p[24];
        dinheiro(sqlite3_column_int(st, 2), p);
        buf_add(b, "<tr><td>");
        buf_esc(b, col(st, 0));
        buf_add(b, "</td><td>%d min</td><td>%s</td></tr>\n", sqlite3_column_int(st, 1), p);
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</table>\n<p><a class=\"botao\" href=\"/horarios\">Ver horários e agendar</a></p>\n");
    pagina_fim(b);
}

/* Mostra os horários livres de cada barbeiro. Visitante só vê; cliente logado vê botões. */
static void pg_horarios(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char data[16], hj[11], rotulo[60], t[8];
    int servico_id = param_int(rq->query, "servico", 0);
    int remarcar = param_int(rq->query, "remarcar", 0);
    int dur = 30, ab = cfg_slot("abertura", 18), fe = cfg_slot("fechamento", 38);
    int i, achou = 0, n, e_cliente = s->logado && strcmp(s->perfil, "cliente") == 0;
    sqlite3_stmt *st;

    hoje(hj);
    pagina_inicio(b, rq, s, remarcar ? "Remarcar horário" : "Horários disponíveis");

    if (remarcar > 0) {   /* confere se o agendamento é mesmo deste cliente */
        int ok = 0;
        st = prep("SELECT servico_id FROM agendamentos WHERE id=? AND cliente_id=? AND status='agendado'");
        if (st) {
            sqlite3_bind_int(st, 1, remarcar);
            sqlite3_bind_int(st, 2, e_cliente ? s->uid : -1);
            if (sqlite3_step(st) == SQLITE_ROW) { ok = 1; if (servico_id == 0) servico_id = sqlite3_column_int(st, 0); }
            sqlite3_finalize(st);
        }
        if (!ok) remarcar = 0;
        else buf_add(b, "<div class=\"aviso\">Escolha o novo horário. O horário antigo só é liberado depois que você confirmar o novo.</div>\n");
    }

    /* serviço escolhido (padrão: o primeiro ativo) */
    st = prep("SELECT id, nome, duracao_min, preco_centavos FROM servicos WHERE ativo=1 ORDER BY id");
    {
        int primeiro_id = 0, existe = 0;
        while (st && sqlite3_step(st) == SQLITE_ROW) {
            if (!primeiro_id) primeiro_id = sqlite3_column_int(st, 0);
            if (sqlite3_column_int(st, 0) == servico_id) existe = 1;
        }
        if (st) sqlite3_finalize(st);
        if (!existe) servico_id = primeiro_id;
    }

    /* data escolhida (padrão: primeiro dia aberto) */
    param(rq->query, "data", data, sizeof data);
    {
        int valida = 0;
        for (i = 0; i < 21; i++) {
            char d[11];
            data_mais_dias(hj, i, d);
            if (strcmp(d, data) == 0 && dia_aberto(d)) valida = 1;
        }
        if (!valida) {
            data[0] = '\0';
            for (i = 0; i < 21; i++) {
                char d[11];
                data_mais_dias(hj, i, d);
                if (dia_aberto(d)) { snprintf(data, sizeof data, "%s", d); break; }
            }
        }
    }

    buf_add(b, "<form method=\"get\" action=\"/horarios\" class=\"cartao\">\n");
    if (remarcar > 0) buf_add(b, "<input type=\"hidden\" name=\"remarcar\" value=\"%d\">", remarcar);
    buf_add(b, "<label for=\"servico\">Serviço</label><select id=\"servico\" name=\"servico\">");
    st = prep("SELECT id, nome, duracao_min, preco_centavos FROM servicos WHERE ativo=1 ORDER BY id");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        char p[24];
        int id = sqlite3_column_int(st, 0);
        dinheiro(sqlite3_column_int(st, 3), p);
        if (id == servico_id) dur = sqlite3_column_int(st, 2);
        buf_add(b, "<option value=\"%d\"%s>", id, id == servico_id ? " selected" : "");
        buf_esc(b, col(st, 1));
        buf_add(b, " - %s - %d min</option>", p, sqlite3_column_int(st, 2));
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</select>\n<label for=\"data\">Dia</label><select id=\"data\" name=\"data\">");
    for (i = 0; i < 21; i++) {
        char d[11];
        data_mais_dias(hj, i, d);
        if (!dia_aberto(d)) continue;
        data_extenso(d, rotulo, sizeof rotulo);
        buf_add(b, "<option value=\"%s\"%s>%s</option>", d, strcmp(d, data) == 0 ? " selected" : "", rotulo);
    }
    buf_add(b, "</select>\n<p><button type=\"submit\">Ver horários</button></p>\n</form>\n");

    n = (dur + 29) / 30;
    if (!data[0]) {
        buf_add(b, "<p>A barbearia não tem dias de funcionamento cadastrados.</p>\n");
        pagina_fim(b);
        return;
    }
    if (!s->logado) {
        buf_add(b, "<div class=\"aviso\">Para agendar é preciso ter uma conta. <a href=\"/cadastro\">Criar conta</a> ou <a href=\"/login\">entrar</a>.</div>\n");
    } else if (!e_cliente) {
        buf_add(b, "<div class=\"aviso\">Você está vendo os horários livres. Para criar agendamentos use a <a href=\"/agenda\">agenda geral</a>.</div>\n");
    }

    st = prep("SELECT id, nome FROM usuarios WHERE perfil='barbeiro' AND ativo=1 ORDER BY nome");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        int bid = sqlite3_column_int(st, 0), occ[48], sl, livres = 0;
        ocupacao(bid, data, occ);
        buf_add(b, "<div class=\"cartao slots\"><h3>Barbeiro ");
        buf_esc(b, col(st, 1));
        buf_add(b, "</h3>\n");
        for (sl = ab; sl + n <= fe; sl++) {
            int j, livre = 1;
            for (j = 0; j < n; j++) if (occ[sl + j]) livre = 0;
            if (minutos_ate(data, sl) <= 0) livre = 0;
            if (!livre) continue;
            livres++;
            achou = 1;
            slot_texto(sl, t);
            if (e_cliente) {
                buf_add(b, "<form method=\"post\" action=\"/agendar\">"
                           "<input type=\"hidden\" name=\"barbeiro\" value=\"%d\">"
                           "<input type=\"hidden\" name=\"servico\" value=\"%d\">"
                           "<input type=\"hidden\" name=\"data\" value=\"%s\">"
                           "<input type=\"hidden\" name=\"slot\" value=\"%d\">"
                           "<input type=\"hidden\" name=\"remarcar\" value=\"%d\">"
                           "<button type=\"submit\" aria-label=\"Agendar às %s\">%s</button></form>\n",
                        bid, servico_id, data, sl, remarcar, t, t);
            } else {
                buf_add(b, "<span class=\"livre\">%s</span>", t);
            }
        }
        if (!livres) buf_add(b, "<p>Sem horários livres nesse dia.</p>");
        buf_add(b, "</div>\n");
    }
    if (st) sqlite3_finalize(st);
    if (!achou) buf_add(b, "<p>Nenhum horário livre nesse dia. Tente outro dia.</p>\n");
    pagina_fim(b);
}

static void pg_cadastro(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    if (s->logado) { redir(rs, home_do_perfil(s->perfil), NULL, 0); return; }
    pagina_inicio(b, rq, s, "Criar conta");
    buf_add(b, "<form method=\"post\" action=\"/cadastro\" class=\"cartao\">\n"
        "<label for=\"nome\">Nome</label><input id=\"nome\" name=\"nome\" required maxlength=\"80\" autocomplete=\"name\">\n"
        "<label for=\"email\">E-mail (será o seu login)</label><input id=\"email\" name=\"email\" type=\"email\" required maxlength=\"100\" autocomplete=\"email\">\n"
        "<label for=\"telefone\">Telefone com DDD</label><input id=\"telefone\" name=\"telefone\" type=\"tel\" required maxlength=\"20\" autocomplete=\"tel\">\n"
        "<label for=\"senha\">Senha (mínimo 6 caracteres)</label><input id=\"senha\" name=\"senha\" type=\"password\" required minlength=\"6\" maxlength=\"60\" autocomplete=\"new-password\">\n"
        "<p><label><input type=\"checkbox\" name=\"aceito\" value=\"1\" required> "
        "Concordo que a barbearia guarde meu nome, e-mail e telefone apenas para gerenciar meus agendamentos (LGPD). "
        "Posso excluir meus dados quando quiser, em \"Minha conta\".</label></p>\n"
        "<button type=\"submit\">Criar conta</button>\n</form>\n"
        "<p>Já tem conta? <a href=\"/login\">Entrar</a></p>\n");
    pagina_fim(b);
}

static void post_cadastro(const Req *rq, Resp *rs, const Sessao *s) {
    char nome[100], email[120], tel[40], senha[80], aceito[8];
    int id, i, digitos = 0;
    (void)s;
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "email", email, sizeof email);
    param(rq->corpo, "telefone", tel, sizeof tel);
    param(rq->corpo, "senha", senha, sizeof senha);
    param(rq->corpo, "aceito", aceito, sizeof aceito);
    aparar(nome); aparar(email); aparar(tel); minusculas(email);
    for (i = 0; tel[i]; i++) if (tel[i] >= '0' && tel[i] <= '9') digitos++;

    if (!nome[0]) { redir(rs, "/cadastro", "Informe seu nome.", 1); return; }
    if (!strchr(email, '@') || !strchr(strchr(email, '@'), '.') || strlen(email) < 6) { redir(rs, "/cadastro", "Informe um e-mail válido.", 1); return; }
    if (digitos < 10) { redir(rs, "/cadastro", "Informe o telefone com DDD.", 1); return; }
    if (strlen(senha) < 6) { redir(rs, "/cadastro", "A senha precisa ter pelo menos 6 caracteres.", 1); return; }
    if (strcmp(aceito, "1") != 0) { redir(rs, "/cadastro", "É preciso aceitar o uso dos dados para criar a conta.", 1); return; }

    id = inserir_usuario(nome, email, tel, senha, "cliente");
    if (!id) { redir(rs, "/cadastro", "Já existe uma conta com esse e-mail.", 1); return; }     /* RN03 */
    sessao_criar(rs, id, "cliente", nome);
    redir(rs, "/horarios", "Conta criada! Agora escolha o serviço, o dia e o horário.", 0);
}

static void pg_login(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    if (s->logado) { redir(rs, home_do_perfil(s->perfil), NULL, 0); return; }
    pagina_inicio(b, rq, s, "Entrar");
    buf_add(b, "<form method=\"post\" action=\"/login\" class=\"cartao\">\n"
        "<label for=\"login\">E-mail (cliente) ou usuário (funcionário)</label><input id=\"login\" name=\"login\" required maxlength=\"100\" autocomplete=\"username\">\n"
        "<label for=\"senha\">Senha</label><input id=\"senha\" name=\"senha\" type=\"password\" required maxlength=\"60\" autocomplete=\"current-password\">\n"
        "<p><button type=\"submit\">Entrar</button></p>\n</form>\n"
        "<p>Ainda não tem conta? <a href=\"/cadastro\">Criar conta</a></p>\n");
    pagina_fim(b);
}

static void post_login(const Req *rq, Resp *rs, const Sessao *s) {
    char login[120], senha[80];
    sqlite3_stmt *st;
    int ok = 0, id = 0;
    char nome[100] = "", perfil[16] = "";
    (void)s;
    param(rq->corpo, "login", login, sizeof login);
    param(rq->corpo, "senha", senha, sizeof senha);
    aparar(login); minusculas(login);

    st = prep("SELECT id, nome, perfil, salt, senha_hash FROM usuarios WHERE login=? AND ativo=1");
    if (st) {
        sqlite3_bind_text(st, 1, login, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW && senha_confere(col(st, 3), col(st, 4), senha)) {
            ok = 1;
            id = sqlite3_column_int(st, 0);
            snprintf(nome, sizeof nome, "%s", col(st, 1));
            snprintf(perfil, sizeof perfil, "%s", col(st, 2));
        }
        sqlite3_finalize(st);
    }
    if (!ok) { redir(rs, "/login", "Usuário ou senha incorretos.", 1); return; }
    sessao_criar(rs, id, perfil, nome);
    redir(rs, home_do_perfil(perfil), NULL, 0);
}

static void pg_sair(const Req *rq, Resp *rs, const Sessao *s) {
    (void)s;
    sessao_sair(rq, rs);
    redir(rs, "/", "Você saiu da conta.", 0);
}

/* ===============================================================
   Parte 5 - Área do cliente
   =============================================================== */

static void pg_cliente(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char hj[11];
    sqlite3_stmt *st;
    int vazio = 1;
    if (!exige(s, rs, "cliente")) return;
    hoje(hj);
    pagina_inicio(b, rq, s, "Meus agendamentos");
    buf_add(b, "<p><a class=\"botao\" href=\"/horarios\">Novo agendamento</a></p>\n");

    buf_add(b, "<table><caption>Próximos agendamentos</caption><tr><th scope=\"col\">Dia</th><th scope=\"col\">Hora</th>"
               "<th scope=\"col\">Barbeiro</th><th scope=\"col\">Serviço</th><th scope=\"col\">Ações</th></tr>\n");
    st = prep("SELECT a.id, a.data, a.slot_inicio, bb.nome, sv.nome, sv.id FROM agendamentos a "
              "JOIN usuarios bb ON bb.id=a.barbeiro_id JOIN servicos sv ON sv.id=a.servico_id "
              "WHERE a.cliente_id=? AND a.status='agendado' AND a.data>=? ORDER BY a.data, a.slot_inicio");
    if (st) {
        sqlite3_bind_int(st, 1, s->uid);
        sqlite3_bind_text(st, 2, hj, -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            char rot[60], t[8];
            int id = sqlite3_column_int(st, 0);
            vazio = 0;
            data_extenso(col(st, 1), rot, sizeof rot);
            slot_texto(sqlite3_column_int(st, 2), t);
            buf_add(b, "<tr><td>%s</td><td>%s</td><td>", rot, t);
            buf_esc(b, col(st, 3));
            buf_add(b, "</td><td>");
            buf_esc(b, col(st, 4));
            buf_add(b, "</td><td><a class=\"botao\" href=\"/horarios?remarcar=%d&amp;servico=%d\">Remarcar</a> "
                       "<form method=\"post\" action=\"/cancelar\"><input type=\"hidden\" name=\"id\" value=\"%d\">"
                       "<button class=\"perigo\" type=\"submit\">Cancelar</button></form></td></tr>\n",
                    id, sqlite3_column_int(st, 5), id);
        }
        sqlite3_finalize(st);
    }
    if (vazio) buf_add(b, "<tr><td colspan=\"5\">Você não tem agendamentos futuros.</td></tr>\n");
    buf_add(b, "</table>\n");

    buf_add(b, "<table><caption>Histórico (últimos 15)</caption><tr><th scope=\"col\">Dia</th><th scope=\"col\">Hora</th>"
               "<th scope=\"col\">Barbeiro</th><th scope=\"col\">Serviço</th><th scope=\"col\">Situação</th></tr>\n");
    st = prep("SELECT a.data, a.slot_inicio, bb.nome, sv.nome, a.status FROM agendamentos a "
              "JOIN usuarios bb ON bb.id=a.barbeiro_id JOIN servicos sv ON sv.id=a.servico_id "
              "WHERE a.cliente_id=? AND NOT (a.status='agendado' AND a.data>=?) "
              "ORDER BY a.data DESC, a.slot_inicio DESC LIMIT 15");
    vazio = 1;
    if (st) {
        sqlite3_bind_int(st, 1, s->uid);
        sqlite3_bind_text(st, 2, hj, -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            char rot[60], t[8];
            vazio = 0;
            data_extenso(col(st, 0), rot, sizeof rot);
            slot_texto(sqlite3_column_int(st, 1), t);
            buf_add(b, "<tr><td>%s</td><td>%s</td><td>", rot, t);
            buf_esc(b, col(st, 2));
            buf_add(b, "</td><td>");
            buf_esc(b, col(st, 3));
            buf_add(b, "</td><td class=\"st-%s\">%s</td></tr>\n", col(st, 4), col(st, 4));
        }
        sqlite3_finalize(st);
    }
    if (vazio) buf_add(b, "<tr><td colspan=\"5\">Nada por aqui ainda.</td></tr>\n");
    buf_add(b, "</table>\n");
    pagina_fim(b);
}

static void post_agendar(const Req *rq, Resp *rs, const Sessao *s) {
    char data[16];
    int remarcar, cancelar_id = 0;
    const char *erro;
    if (!exige(s, rs, "cliente")) return;
    param(rq->corpo, "data", data, sizeof data);
    remarcar = param_int(rq->corpo, "remarcar", 0);

    if (remarcar > 0) {     /* o agendamento antigo precisa ser deste cliente e respeitar a antecedência */
        sqlite3_stmt *st = prep("SELECT data, slot_inicio FROM agendamentos WHERE id=? AND cliente_id=? AND status='agendado'");
        char h[16];
        int ok = 0, horas;
        cfg_get("antecedencia_horas", h, sizeof h);
        horas = atoi(h);
        if (st) {
            sqlite3_bind_int(st, 1, remarcar);
            sqlite3_bind_int(st, 2, s->uid);
            if (sqlite3_step(st) == SQLITE_ROW) {
                ok = 1;
                if (minutos_ate(col(st, 0), sqlite3_column_int(st, 1)) < horas * 60L) ok = -1;
            }
            sqlite3_finalize(st);
        }
        if (ok == 0) { redir(rs, "/cliente", "Agendamento não encontrado.", 1); return; }
        if (ok < 0) { redir(rs, "/cliente", "Só é possível remarcar com antecedência mínima definida pela barbearia. Ligue para a barbearia.", 1); return; }
        cancelar_id = remarcar;
    }
    erro = criar_agendamento(s->uid, NULL, NULL, param_int(rq->corpo, "barbeiro", 0),
                             param_int(rq->corpo, "servico", 0), data, param_int(rq->corpo, "slot", -1), cancelar_id);
    if (erro) { redir(rs, "/horarios", erro, 1); return; }
    redir(rs, "/cliente", remarcar ? "Agendamento remarcado com sucesso!" : "Agendamento confirmado! Confira os dados abaixo.", 0);
}

static void post_cancelar(const Req *rq, Resp *rs, const Sessao *s) {
    sqlite3_stmt *st;
    char h[16];
    int id = param_int(rq->corpo, "id", 0), horas, achou = 0, tarde = 0;
    if (!exige(s, rs, "cliente")) return;
    cfg_get("antecedencia_horas", h, sizeof h);
    horas = atoi(h);
    st = prep("SELECT data, slot_inicio FROM agendamentos WHERE id=? AND cliente_id=? AND status='agendado'");
    if (st) {
        sqlite3_bind_int(st, 1, id);
        sqlite3_bind_int(st, 2, s->uid);
        if (sqlite3_step(st) == SQLITE_ROW) {
            achou = 1;
            if (minutos_ate(col(st, 0), sqlite3_column_int(st, 1)) < horas * 60L) tarde = 1;     /* RN05 */
        }
        sqlite3_finalize(st);
    }
    if (!achou) { redir(rs, "/cliente", "Agendamento não encontrado.", 1); return; }
    if (tarde) {
        char m[200];
        snprintf(m, sizeof m, "Cancelamento só com pelo menos %d hora(s) de antecedência. Ligue para a barbearia.", horas);
        redir(rs, "/cliente", m, 1);
        return;
    }
    st = prep("UPDATE agendamentos SET status='cancelado' WHERE id=? AND cliente_id=?");
    if (st) { sqlite3_bind_int(st, 1, id); sqlite3_bind_int(st, 2, s->uid); sqlite3_step(st); sqlite3_finalize(st); }
    redir(rs, "/cliente", "Agendamento cancelado.", 0);
}

/* ===============================================================
   Parte 6 - Minha conta (todos os perfis)
   =============================================================== */

static void pg_conta(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!exige(s, rs, "cliente,barbeiro,recepcionista,dono")) return;
    pagina_inicio(b, rq, s, "Minha conta");
    st = prep("SELECT nome, login, telefone FROM usuarios WHERE id=?");
    if (st) {
        sqlite3_bind_int(st, 1, s->uid);
        if (sqlite3_step(st) == SQLITE_ROW) {
            buf_add(b, "<form method=\"post\" action=\"/conta/dados\" class=\"cartao\"><h3>Meus dados</h3>\n"
                       "<p>Login: <strong>");
            buf_esc(b, col(st, 1));
            buf_add(b, "</strong></p>\n<label for=\"nome\">Nome</label><input id=\"nome\" name=\"nome\" required maxlength=\"80\" value=\"");
            buf_esc(b, col(st, 0));
            buf_add(b, "\">\n<label for=\"telefone\">Telefone</label><input id=\"telefone\" name=\"telefone\" type=\"tel\" maxlength=\"20\" value=\"");
            buf_esc(b, col(st, 2));
            buf_add(b, "\">\n<p><button type=\"submit\">Salvar dados</button></p></form>\n");
        }
        sqlite3_finalize(st);
    }
    buf_add(b, "<form method=\"post\" action=\"/conta/senha\" class=\"cartao\"><h3>Trocar senha</h3>\n"
        "<label for=\"atual\">Senha atual</label><input id=\"atual\" name=\"atual\" type=\"password\" required maxlength=\"60\" autocomplete=\"current-password\">\n"
        "<label for=\"nova\">Nova senha (mínimo 6 caracteres)</label><input id=\"nova\" name=\"nova\" type=\"password\" required minlength=\"6\" maxlength=\"60\" autocomplete=\"new-password\">\n"
        "<p><button type=\"submit\">Trocar senha</button></p></form>\n");
    if (strcmp(s->perfil, "cliente") == 0) {
        buf_add(b, "<form method=\"post\" action=\"/conta/excluir\" class=\"cartao\"><h3>Excluir minha conta (LGPD)</h3>\n"
            "<p>Seus dados pessoais serão apagados e seus agendamentos futuros serão cancelados. Isso não pode ser desfeito.</p>\n"
            "<label for=\"senha_ex\">Digite sua senha para confirmar</label><input id=\"senha_ex\" name=\"senha\" type=\"password\" required maxlength=\"60\">\n"
            "<p><button class=\"perigo\" type=\"submit\">Excluir minha conta</button></p></form>\n");
    }
    pagina_fim(b);
}

static void post_conta_dados(const Req *rq, Resp *rs, const Sessao *s) {
    char nome[100], tel[40];
    sqlite3_stmt *st;
    if (!exige(s, rs, "cliente,barbeiro,recepcionista,dono")) return;
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "telefone", tel, sizeof tel);
    aparar(nome); aparar(tel);
    if (!nome[0]) { redir(rs, "/conta", "O nome não pode ficar vazio.", 1); return; }
    st = prep("UPDATE usuarios SET nome=?, telefone=? WHERE id=?");
    if (st) {
        sqlite3_bind_text(st, 1, nome, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, tel, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 3, s->uid);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    {   /* atualiza o nome guardado na sessão */
        int i;
        for (i = 0; i < MAX_SESS; i++)
            if (sess[i].sid[0] && sess[i].uid == s->uid) snprintf(sess[i].nome, sizeof sess[i].nome, "%s", nome);
    }
    redir(rs, "/conta", "Dados atualizados.", 0);
}

/* Confere a senha atual de quem está logado */
static int senha_do_usuario_confere(int uid, const char *senha) {
    sqlite3_stmt *st = prep("SELECT salt, senha_hash FROM usuarios WHERE id=?");
    int ok = 0;
    if (!st) return 0;
    sqlite3_bind_int(st, 1, uid);
    if (sqlite3_step(st) == SQLITE_ROW) ok = senha_confere(col(st, 0), col(st, 1), senha);
    sqlite3_finalize(st);
    return ok;
}

static void post_conta_senha(const Req *rq, Resp *rs, const Sessao *s) {
    char atual[80], nova[80];
    if (!exige(s, rs, "cliente,barbeiro,recepcionista,dono")) return;
    param(rq->corpo, "atual", atual, sizeof atual);
    param(rq->corpo, "nova", nova, sizeof nova);
    if (!senha_do_usuario_confere(s->uid, atual)) { redir(rs, "/conta", "A senha atual está incorreta.", 1); return; }
    if (strlen(nova) < 6) { redir(rs, "/conta", "A nova senha precisa ter pelo menos 6 caracteres.", 1); return; }
    trocar_senha(s->uid, nova);
    redir(rs, "/conta", "Senha alterada com sucesso.", 0);
}

/* Direito de exclusão (LGPD): apaga os dados pessoais, mas mantém o histórico sem identificar a pessoa */
static void post_conta_excluir(const Req *rq, Resp *rs, const Sessao *s) {
    char senha[80], hj[11];
    sqlite3_stmt *st;
    if (!exige(s, rs, "cliente")) return;
    param(rq->corpo, "senha", senha, sizeof senha);
    if (!senha_do_usuario_confere(s->uid, senha)) { redir(rs, "/conta", "Senha incorreta. A conta não foi excluída.", 1); return; }
    hoje(hj);
    exec_sql("BEGIN");
    st = prep("UPDATE agendamentos SET status='cancelado' WHERE cliente_id=? AND status='agendado' AND data>=?");
    if (st) { sqlite3_bind_int(st, 1, s->uid); sqlite3_bind_text(st, 2, hj, -1, SQLITE_STATIC); sqlite3_step(st); sqlite3_finalize(st); }
    st = prep("UPDATE agendamentos SET cliente_id=NULL, nome_avulso='Cliente removido', tel_avulso=NULL WHERE cliente_id=?");
    if (st) { sqlite3_bind_int(st, 1, s->uid); sqlite3_step(st); sqlite3_finalize(st); }
    st = prep("DELETE FROM usuarios WHERE id=? AND perfil='cliente'");
    if (st) { sqlite3_bind_int(st, 1, s->uid); sqlite3_step(st); sqlite3_finalize(st); }
    exec_sql("COMMIT");
    sessao_encerrar_usuario(s->uid);
    sessao_sair(rq, rs);
    redir(rs, "/", "Sua conta e seus dados foram excluídos.", 0);
}

/* ===============================================================
   Parte 7 - Agenda (barbeiro, recepcionista e dono)
   =============================================================== */

/* Tabela com os atendimentos de um barbeiro em um dia */
static void tabela_agenda(Buf *b, int barbeiro_id, const char *nome_barbeiro, const char *data, const char *voltar) {
    sqlite3_stmt *st;
    int vazio = 1;
    buf_add(b, "<table><caption>");
    if (nome_barbeiro) { buf_add(b, "Barbeiro "); buf_esc(b, nome_barbeiro); } else buf_add(b, "Atendimentos do dia");
    buf_add(b, "</caption><tr><th scope=\"col\">Horário</th><th scope=\"col\">Cliente</th><th scope=\"col\">Telefone</th>"
               "<th scope=\"col\">Serviço</th><th scope=\"col\">Situação</th><th scope=\"col\">Ações</th></tr>\n");
    st = prep("SELECT a.id, a.slot_inicio, a.slots, a.status, COALESCE(u.nome, a.nome_avulso, ''), "
              "COALESCE(u.telefone, a.tel_avulso, ''), COALESCE(s.nome, '') "
              "FROM agendamentos a LEFT JOIN usuarios u ON u.id=a.cliente_id LEFT JOIN servicos s ON s.id=a.servico_id "
              "WHERE a.barbeiro_id=? AND a.data=? AND a.status!='cancelado' ORDER BY a.slot_inicio");
    if (st) {
        sqlite3_bind_int(st, 1, barbeiro_id);
        sqlite3_bind_text(st, 2, data, -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            char t1[8], t2[8], v[300];
            int id = sqlite3_column_int(st, 0), ini = sqlite3_column_int(st, 1), n = sqlite3_column_int(st, 2);
            const char *status = col(st, 3);
            vazio = 0;
            slot_texto(ini, t1);
            slot_texto(ini + n > 47 ? 47 : ini + n, t2);
            url_encode(voltar, v, sizeof v);
            buf_add(b, "<tr><td>%s às %s</td><td>", t1, t2);
            buf_esc(b, col(st, 4));
            buf_add(b, "</td><td>");
            buf_esc(b, col(st, 5));
            buf_add(b, "</td><td>");
            buf_esc(b, col(st, 6));
            buf_add(b, "</td><td class=\"st-%s\">%s</td><td>", status, status);
#define BOTAO_STATUS(valor, texto, classe) \
            buf_add(b, "<form method=\"post\" action=\"/status\"><input type=\"hidden\" name=\"id\" value=\"%d\">" \
                       "<input type=\"hidden\" name=\"status\" value=\"" valor "\"><input type=\"hidden\" name=\"voltar\" value=\"%s\">" \
                       "<button class=\"" classe "\" type=\"submit\">" texto "</button></form>", id, voltar)
            if (strcmp(status, "agendado") == 0) {
                BOTAO_STATUS("concluido", "Concluído", "");
                BOTAO_STATUS("faltou", "Faltou", "leve");
                BOTAO_STATUS("cancelado", "Cancelar", "perigo");
            } else if (strcmp(status, "bloqueado") == 0) {
                BOTAO_STATUS("cancelado", "Liberar horário", "leve");
            } else {
                BOTAO_STATUS("agendado", "Corrigir para agendado", "leve");
            }
            buf_add(b, "</td></tr>\n");
        }
        sqlite3_finalize(st);
    }
    if (vazio) buf_add(b, "<tr><td colspan=\"6\">Nenhum atendimento neste dia.</td></tr>\n");
    buf_add(b, "</table>\n");
}

static void navegacao_dia(Buf *b, const char *base, const char *data) {
    char ant[11], prox[11], rot[60];
    data_mais_dias(data, -1, ant);
    data_mais_dias(data, 1, prox);
    data_extenso(data, rot, sizeof rot);
    buf_add(b, "<div class=\"cartao\"><p><strong>Dia: %s</strong></p>"
               "<a class=\"botao leve\" href=\"%s?data=%s\">&laquo; Dia anterior</a> "
               "<a class=\"botao leve\" href=\"%s?data=%s\">Próximo dia &raquo;</a>"
               "<form method=\"get\" action=\"%s\"><label for=\"data\">Ir para a data</label>"
               "<input id=\"data\" name=\"data\" type=\"date\" value=\"%s\" required> <button type=\"submit\">Ver</button></form></div>\n",
            rot, base, ant, base, prox, base, data);
}

static void data_da_query(const Req *rq, char *data) {
    param(rq->query, "data", data, 16);
    if (!data_valida(data)) hoje(data);
}

static void pg_barbeiro(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char data[16], voltar[60], hj[11];
    int ab = cfg_slot("abertura", 18), fe = cfg_slot("fechamento", 38), i;
    sqlite3_stmt *st;
    if (!exige(s, rs, "barbeiro")) return;
    data_da_query(rq, data);
    snprintf(voltar, sizeof voltar, "/barbeiro?data=%s", data);
    pagina_inicio(b, rq, s, "Minha agenda");
    navegacao_dia(b, "/barbeiro", data);
    tabela_agenda(b, s->uid, NULL, data, voltar);

    buf_add(b, "<form method=\"post\" action=\"/bloquear\" class=\"cartao\"><h3>Bloquear horário (folga, almoço)</h3>\n"
               "<input type=\"hidden\" name=\"data\" value=\"%s\">"
               "<label for=\"de\">De</label><select id=\"de\" name=\"de\">", data);
    opcoes_hora(b, ab, fe - 1, ab);
    buf_add(b, "</select><label for=\"ate\">Até</label><select id=\"ate\" name=\"ate\">");
    opcoes_hora(b, ab + 1, fe, ab + 2 > fe ? fe : ab + 2);
    buf_add(b, "</select><p><button type=\"submit\">Bloquear</button></p></form>\n");

    hoje(hj);
    buf_add(b, "<table><caption>Resumo dos próximos 7 dias</caption><tr><th scope=\"col\">Dia</th><th scope=\"col\">Atendimentos agendados</th></tr>\n");
    for (i = 0; i < 7; i++) {
        char d[11], rot[60];
        int total = 0;
        data_mais_dias(hj, i, d);
        data_extenso(d, rot, sizeof rot);
        st = prep("SELECT COUNT(*) FROM agendamentos WHERE barbeiro_id=? AND data=? AND status='agendado'");
        if (st) {
            sqlite3_bind_int(st, 1, s->uid);
            sqlite3_bind_text(st, 2, d, -1, SQLITE_STATIC);
            if (sqlite3_step(st) == SQLITE_ROW) total = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        buf_add(b, "<tr><td><a href=\"/barbeiro?data=%s\">%s</a></td><td>%d</td></tr>\n", d, rot, total);
    }
    buf_add(b, "</table>\n");
    pagina_fim(b);
}

static void post_status(const Req *rq, Resp *rs, const Sessao *s) {
    char novo[20], voltar[260], atual[20] = "";
    const char *dest;
    int id = param_int(rq->corpo, "id", 0), barbeiro_id = 0, achou = 0, permitido = 0;
    sqlite3_stmt *st;
    if (!exige(s, rs, "barbeiro,recepcionista,dono")) return;
    param(rq->corpo, "status", novo, sizeof novo);
    param(rq->corpo, "voltar", voltar, sizeof voltar);
    dest = destino_seguro(voltar, home_do_perfil(s->perfil));

    st = prep("SELECT status, barbeiro_id FROM agendamentos WHERE id=?");
    if (st) {
        sqlite3_bind_int(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) { achou = 1; snprintf(atual, sizeof atual, "%s", col(st, 0)); barbeiro_id = sqlite3_column_int(st, 1); }
        sqlite3_finalize(st);
    }
    if (!achou) { redir(rs, dest, "Agendamento não encontrado.", 1); return; }
    if (strcmp(s->perfil, "barbeiro") == 0 && barbeiro_id != s->uid) { redir(rs, dest, "Você só pode alterar a sua própria agenda.", 1); return; }

    /* Mudanças permitidas: agendado -> concluido/faltou/cancelado; bloqueado -> cancelado; concluido/faltou -> agendado */
    if (strcmp(atual, "agendado") == 0 && (!strcmp(novo, "concluido") || !strcmp(novo, "faltou") || !strcmp(novo, "cancelado"))) permitido = 1;
    if (strcmp(atual, "bloqueado") == 0 && !strcmp(novo, "cancelado")) permitido = 1;
    if ((!strcmp(atual, "concluido") || !strcmp(atual, "faltou")) && !strcmp(novo, "agendado")) permitido = 1;
    if (!permitido) { redir(rs, dest, "Essa mudança de situação não é permitida.", 1); return; }

    st = prep("UPDATE agendamentos SET status=? WHERE id=?");
    if (st) { sqlite3_bind_text(st, 1, novo, -1, SQLITE_STATIC); sqlite3_bind_int(st, 2, id); sqlite3_step(st); sqlite3_finalize(st); }
    redir(rs, dest, "Situação atualizada.", 0);
}

static void post_bloquear(const Req *rq, Resp *rs, const Sessao *s) {
    char data[16], hj[11], voltar[60];
    int de, ate, occ[48], i, ab = cfg_slot("abertura", 18), fe = cfg_slot("fechamento", 38);
    sqlite3_stmt *st;
    if (!exige(s, rs, "barbeiro")) return;
    param(rq->corpo, "data", data, sizeof data);
    de = param_int(rq->corpo, "de", -1);
    ate = param_int(rq->corpo, "ate", -1);
    hoje(hj);
    if (!data_valida(data) || strcmp(data, hj) < 0) { redir(rs, "/barbeiro", "Data inválida.", 1); return; }
    snprintf(voltar, sizeof voltar, "/barbeiro?data=%s", data);
    if (de < ab || ate > fe || de >= ate) { redir(rs, voltar, "Intervalo de horário inválido.", 1); return; }
    ocupacao(s->uid, data, occ);
    for (i = de; i < ate; i++) if (occ[i]) { redir(rs, voltar, "Já existe agendamento ou bloqueio nesse intervalo.", 1); return; }
    st = prep("INSERT INTO agendamentos (barbeiro_id, nome_avulso, data, slot_inicio, slots, status) VALUES (?, 'Bloqueio', ?, ?, ?, 'bloqueado')");
    if (st) {
        sqlite3_bind_int(st, 1, s->uid);
        sqlite3_bind_text(st, 2, data, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 3, de);
        sqlite3_bind_int(st, 4, ate - de);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    redir(rs, voltar, "Horário bloqueado.", 0);
}

/* Agenda de todos os barbeiros (recepcionista e dono) */
static void pg_agenda(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char data[16], voltar[60];
    int ab = cfg_slot("abertura", 18), fe = cfg_slot("fechamento", 38);
    sqlite3_stmt *st;
    if (!exige(s, rs, "recepcionista,dono")) return;
    data_da_query(rq, data);
    snprintf(voltar, sizeof voltar, "/agenda?data=%s", data);
    pagina_inicio(b, rq, s, "Agenda geral");
    navegacao_dia(b, "/agenda", data);

    st = prep("SELECT id, nome FROM usuarios WHERE perfil='barbeiro' AND ativo=1 ORDER BY nome");
    while (st && sqlite3_step(st) == SQLITE_ROW) tabela_agenda(b, sqlite3_column_int(st, 0), col(st, 1), data, voltar);
    if (st) sqlite3_finalize(st);

    buf_add(b, "<form method=\"post\" action=\"/agendar-staff\" class=\"cartao\"><h3>Novo agendamento (por telefone ou balcão)</h3>\n"
               "<label for=\"barbeiro\">Barbeiro</label><select id=\"barbeiro\" name=\"barbeiro\">");
    st = prep("SELECT id, nome FROM usuarios WHERE perfil='barbeiro' AND ativo=1 ORDER BY nome");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        buf_add(b, "<option value=\"%d\">", sqlite3_column_int(st, 0));
        buf_esc(b, col(st, 1));
        buf_add(b, "</option>");
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</select><label for=\"servico\">Serviço</label><select id=\"servico\" name=\"servico\">");
    st = prep("SELECT id, nome, duracao_min FROM servicos WHERE ativo=1 ORDER BY id");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        buf_add(b, "<option value=\"%d\">", sqlite3_column_int(st, 0));
        buf_esc(b, col(st, 1));
        buf_add(b, " (%d min)</option>", sqlite3_column_int(st, 2));
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</select><label for=\"dia\">Dia</label><input id=\"dia\" name=\"data\" type=\"date\" value=\"%s\" required>"
               "<label for=\"slot\">Horário</label><select id=\"slot\" name=\"slot\">", data);
    opcoes_hora(b, ab, fe - 1, ab);
    buf_add(b, "</select>\n<p>Informe o e-mail de um cliente que já tem conta <strong>ou</strong> o nome e o telefone de quem não tem conta.</p>\n"
               "<label for=\"email\">E-mail do cliente (opcional)</label><input id=\"email\" name=\"email\" type=\"email\" maxlength=\"100\">\n"
               "<label for=\"nome\">Nome (sem conta)</label><input id=\"nome\" name=\"nome\" maxlength=\"80\">\n"
               "<label for=\"tel\">Telefone (sem conta)</label><input id=\"tel\" name=\"tel\" type=\"tel\" maxlength=\"20\">\n"
               "<p><button type=\"submit\">Agendar</button></p></form>\n");
    pagina_fim(b);
}

static void post_agendar_staff(const Req *rq, Resp *rs, const Sessao *s) {
    char data[16], email[120], nome[100], tel[40], voltar[60];
    int cliente_id = 0;
    const char *erro;
    if (!exige(s, rs, "recepcionista,dono")) return;
    param(rq->corpo, "data", data, sizeof data);
    param(rq->corpo, "email", email, sizeof email);
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "tel", tel, sizeof tel);
    aparar(email); aparar(nome); aparar(tel); minusculas(email);
    snprintf(voltar, sizeof voltar, "/agenda?data=%s", data_valida(data) ? data : "");

    if (email[0]) {
        sqlite3_stmt *st = prep("SELECT id FROM usuarios WHERE login=? AND perfil='cliente' AND ativo=1");
        if (st) {
            sqlite3_bind_text(st, 1, email, -1, SQLITE_STATIC);
            if (sqlite3_step(st) == SQLITE_ROW) cliente_id = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        if (!cliente_id) { redir(rs, voltar, "Não existe cliente com esse e-mail. Use nome e telefone.", 1); return; }
    } else if (!nome[0]) {
        redir(rs, voltar, "Informe o e-mail de um cliente ou o nome de quem não tem conta.", 1);
        return;
    }
    erro = criar_agendamento(cliente_id, nome, tel, param_int(rq->corpo, "barbeiro", 0),
                             param_int(rq->corpo, "servico", 0), data, param_int(rq->corpo, "slot", -1), 0);
    if (erro) { redir(rs, voltar, erro, 1); return; }
    redir(rs, voltar, "Agendamento criado.", 0);
}

/* ===============================================================
   Parte 8 - Clientes e senhas (recepcionista e dono)
   =============================================================== */

static void pg_clientes(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!exige(s, rs, "recepcionista,dono")) return;
    pagina_inicio(b, rq, s, "Clientes");
    buf_add(b, "<table><caption>Clientes cadastrados (até 200)</caption><tr><th scope=\"col\">Nome</th><th scope=\"col\">E-mail</th>"
               "<th scope=\"col\">Telefone</th><th scope=\"col\">Desde</th><th scope=\"col\">Ação</th></tr>\n");
    st = prep("SELECT id, nome, login, telefone, criado_em FROM usuarios WHERE perfil='cliente' ORDER BY nome LIMIT 200");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        buf_add(b, "<tr><td>");
        buf_esc(b, col(st, 1));
        buf_add(b, "</td><td>");
        buf_esc(b, col(st, 2));
        buf_add(b, "</td><td>");
        buf_esc(b, col(st, 3));
        buf_add(b, "</td><td>");
        buf_esc(b, col(st, 4));
        buf_add(b, "</td><td><form method=\"post\" action=\"/resetar\"><input type=\"hidden\" name=\"id\" value=\"%d\">"
                   "<input type=\"hidden\" name=\"voltar\" value=\"/clientes\">"
                   "<button class=\"leve\" type=\"submit\">Redefinir senha</button></form></td></tr>\n", sqlite3_column_int(st, 0));
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</table>\n");
    pagina_fim(b);
}

/* Gera uma senha temporária. Recepcionista só mexe em clientes; o dono, em todos. */
static void post_resetar(const Req *rq, Resp *rs, const Sessao *s) {
    char voltar[260], perfil[16] = "", nome[100] = "", temp[10], msg[300];
    const char *dest;
    sqlite3_stmt *st;
    int id = param_int(rq->corpo, "id", 0), achou = 0;
    if (!exige(s, rs, "recepcionista,dono")) return;
    param(rq->corpo, "voltar", voltar, sizeof voltar);
    dest = destino_seguro(voltar, "/clientes");
    st = prep("SELECT perfil, nome FROM usuarios WHERE id=?");
    if (st) {
        sqlite3_bind_int(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) { achou = 1; snprintf(perfil, sizeof perfil, "%s", col(st, 0)); snprintf(nome, sizeof nome, "%s", col(st, 1)); }
        sqlite3_finalize(st);
    }
    if (!achou) { redir(rs, dest, "Usuário não encontrado.", 1); return; }
    if (strcmp(s->perfil, "recepcionista") == 0 && strcmp(perfil, "cliente") != 0) {
        redir(rs, dest, "A recepcionista só pode redefinir a senha de clientes.", 1);
        return;
    }
    gerar_senha_temporaria(temp, 9);
    trocar_senha(id, temp);
    sessao_encerrar_usuario(id);
    snprintf(msg, sizeof msg, "Senha temporária de %s: %s (anote e entregue; a pessoa deve trocar em Minha conta).", nome, temp);
    redir(rs, dest, msg, 0);
}

/* ===============================================================
   Parte 9 - Área do dono
   =============================================================== */

static int contar(const char *sql, const char *a, const char *b2) {
    sqlite3_stmt *st = prep(sql);
    int n = 0;
    if (!st) return 0;
    if (a) sqlite3_bind_text(st, 1, a, -1, SQLITE_STATIC);
    if (b2) sqlite3_bind_text(st, 2, b2, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

static void pg_dono(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char hj[11], fim[11];
    if (!exige(s, rs, "dono")) return;
    hoje(hj);
    data_mais_dias(hj, 7, fim);
    pagina_inicio(b, rq, s, "Painel do dono");
    buf_add(b, "<div class=\"cartao\"><h3>Resumo</h3><ul>"
               "<li>Agendamentos de hoje: <strong>%d</strong></li>"
               "<li>Agendamentos nos próximos 7 dias: <strong>%d</strong></li>"
               "<li>Clientes cadastrados: <strong>%d</strong></li>"
               "<li>Funcionários ativos: <strong>%d</strong></li></ul></div>\n",
            contar("SELECT COUNT(*) FROM agendamentos WHERE data=? AND status='agendado'", hj, NULL),
            contar("SELECT COUNT(*) FROM agendamentos WHERE data>=? AND data<=? AND status='agendado'", hj, fim),
            contar("SELECT COUNT(*) FROM usuarios WHERE perfil='cliente'", NULL, NULL),
            contar("SELECT COUNT(*) FROM usuarios WHERE perfil!='cliente' AND ativo=1", NULL, NULL));
    buf_add(b, "<div class=\"cartao\"><h3>O que você pode fazer</h3><ul>"
               "<li><a href=\"/agenda\">Ver e alterar todas as agendas</a></li>"
               "<li><a href=\"/funcionarios\">Cadastrar barbeiros e recepcionista, desativar e redefinir senhas</a></li>"
               "<li><a href=\"/servicos\">Serviços e preços</a></li>"
               "<li><a href=\"/relatorios\">Relatórios</a></li>"
               "<li><a href=\"/config\">Endereço, telefone e horário de funcionamento</a></li></ul></div>\n");
    pagina_fim(b);
}

static void pg_funcionarios(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!exige(s, rs, "dono")) return;
    pagina_inicio(b, rq, s, "Funcionários");
    buf_add(b, "<table><caption>Equipe</caption><tr><th scope=\"col\">Nome</th><th scope=\"col\">Usuário</th><th scope=\"col\">Função</th>"
               "<th scope=\"col\">Situação</th><th scope=\"col\">Ações</th></tr>\n");
    st = prep("SELECT id, nome, login, perfil, ativo FROM usuarios WHERE perfil!='cliente' ORDER BY perfil, nome");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), ativo = sqlite3_column_int(st, 4);
        buf_add(b, "<tr><td>");
        buf_esc(b, col(st, 1));
        buf_add(b, "</td><td>");
        buf_esc(b, col(st, 2));
        buf_add(b, "</td><td>%s</td><td>%s</td><td>", col(st, 3), ativo ? "ativo" : "desativado");
        buf_add(b, "<form method=\"post\" action=\"/resetar\"><input type=\"hidden\" name=\"id\" value=\"%d\">"
                   "<input type=\"hidden\" name=\"voltar\" value=\"/funcionarios\">"
                   "<button class=\"leve\" type=\"submit\">Redefinir senha</button></form>", id);
        if (id != s->uid) {
            buf_add(b, "<form method=\"post\" action=\"/func-ativo\"><input type=\"hidden\" name=\"id\" value=\"%d\">"
                       "<input type=\"hidden\" name=\"ativo\" value=\"%d\"><button class=\"%s\" type=\"submit\">%s</button></form>",
                    id, ativo ? 0 : 1, ativo ? "perigo" : "", ativo ? "Desativar" : "Reativar");
        }
        buf_add(b, "</td></tr>\n");
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "</table>\n"
        "<form method=\"post\" action=\"/func-novo\" class=\"cartao\"><h3>Novo funcionário</h3>\n"
        "<label for=\"nome\">Nome</label><input id=\"nome\" name=\"nome\" required maxlength=\"80\">\n"
        "<label for=\"login\">Usuário (para entrar)</label><input id=\"login\" name=\"login\" required maxlength=\"40\">\n"
        "<label for=\"perfil\">Função</label><select id=\"perfil\" name=\"perfil\"><option value=\"barbeiro\">Barbeiro</option>"
        "<option value=\"recepcionista\">Recepcionista</option></select>\n"
        "<label for=\"senha\">Senha inicial (mínimo 6 caracteres)</label><input id=\"senha\" name=\"senha\" type=\"password\" required minlength=\"6\" maxlength=\"60\">\n"
        "<p><button type=\"submit\">Cadastrar</button></p></form>\n");
    pagina_fim(b);
}

static void post_func_novo(const Req *rq, Resp *rs, const Sessao *s) {
    char nome[100], login[60], perfil[20], senha[80];
    if (!exige(s, rs, "dono")) return;
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "login", login, sizeof login);
    param(rq->corpo, "perfil", perfil, sizeof perfil);
    param(rq->corpo, "senha", senha, sizeof senha);
    aparar(nome); aparar(login); minusculas(login);
    if (!nome[0] || !login[0]) { redir(rs, "/funcionarios", "Informe nome e usuário.", 1); return; }
    if (strcmp(perfil, "barbeiro") != 0 && strcmp(perfil, "recepcionista") != 0) { redir(rs, "/funcionarios", "Função inválida.", 1); return; }
    if (strlen(senha) < 6) { redir(rs, "/funcionarios", "A senha precisa ter pelo menos 6 caracteres.", 1); return; }
    if (!inserir_usuario(nome, login, "", senha, perfil)) { redir(rs, "/funcionarios", "Esse usuário já existe.", 1); return; }
    redir(rs, "/funcionarios", "Funcionário cadastrado.", 0);
}

static void post_func_ativo(const Req *rq, Resp *rs, const Sessao *s) {
    int id = param_int(rq->corpo, "id", 0), ativo = param_int(rq->corpo, "ativo", 1) ? 1 : 0;
    sqlite3_stmt *st;
    if (!exige(s, rs, "dono")) return;
    if (id == s->uid) { redir(rs, "/funcionarios", "Você não pode desativar a própria conta.", 1); return; }
    st = prep("UPDATE usuarios SET ativo=? WHERE id=? AND perfil!='cliente'");
    if (st) { sqlite3_bind_int(st, 1, ativo); sqlite3_bind_int(st, 2, id); sqlite3_step(st); sqlite3_finalize(st); }
    if (!ativo) sessao_encerrar_usuario(id);
    redir(rs, "/funcionarios", ativo ? "Funcionário reativado." : "Funcionário desativado. Os agendamentos antigos continuam no histórico.", 0);
}

static void pg_servicos(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    int d;
    if (!exige(s, rs, "dono")) return;
    pagina_inicio(b, rq, s, "Serviços e preços");
    buf_add(b, "<p>A duração é medida em blocos de 30 minutos. Um serviço de 60 min ocupa dois horários seguidos do barbeiro.</p>\n");
    st = prep("SELECT id, nome, preco_centavos, duracao_min, ativo FROM servicos ORDER BY id");
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), preco = sqlite3_column_int(st, 2), dur = sqlite3_column_int(st, 3);
        buf_add(b, "<form method=\"post\" action=\"/serv-salvar\" class=\"cartao\"><input type=\"hidden\" name=\"id\" value=\"%d\">"
                   "<label for=\"n%d\">Nome</label><input id=\"n%d\" name=\"nome\" required maxlength=\"60\" value=\"", id, id, id);
        buf_esc(b, col(st, 1));
        buf_add(b, "\"><label for=\"p%d\">Preço (R$)</label><input id=\"p%d\" name=\"preco\" required value=\"%d,%02d\" size=\"8\">"
                   "<label for=\"d%d\">Duração</label><select id=\"d%d\" name=\"duracao\">", id, id, preco / 100, preco % 100, id, id);
        for (d = 30; d <= 180; d += 30) buf_add(b, "<option value=\"%d\"%s>%d min</option>", d, d == dur ? " selected" : "", d);
        buf_add(b, "</select><label for=\"a%d\">Situação</label><select id=\"a%d\" name=\"ativo\">"
                   "<option value=\"1\"%s>Ativo</option><option value=\"0\"%s>Desativado</option></select>"
                   "<p><button type=\"submit\">Salvar</button></p></form>\n",
                id, id, sqlite3_column_int(st, 4) ? " selected" : "", sqlite3_column_int(st, 4) ? "" : " selected");
    }
    if (st) sqlite3_finalize(st);
    buf_add(b, "<form method=\"post\" action=\"/serv-salvar\" class=\"cartao\"><h3>Novo serviço</h3><input type=\"hidden\" name=\"id\" value=\"0\">"
               "<label for=\"nn\">Nome</label><input id=\"nn\" name=\"nome\" required maxlength=\"60\">"
               "<label for=\"np\">Preço (R$)</label><input id=\"np\" name=\"preco\" required size=\"8\" placeholder=\"35,00\">"
               "<label for=\"nd\">Duração</label><select id=\"nd\" name=\"duracao\">");
    for (d = 30; d <= 180; d += 30) buf_add(b, "<option value=\"%d\">%d min</option>", d, d);
    buf_add(b, "</select><input type=\"hidden\" name=\"ativo\" value=\"1\"><p><button type=\"submit\">Cadastrar</button></p></form>\n");
    pagina_fim(b);
}

static void post_serv_salvar(const Req *rq, Resp *rs, const Sessao *s) {
    char nome[100], preco_txt[40];
    int id = param_int(rq->corpo, "id", 0), dur = param_int(rq->corpo, "duracao", 30);
    int ativo = param_int(rq->corpo, "ativo", 1) ? 1 : 0, preco;
    sqlite3_stmt *st;
    if (!exige(s, rs, "dono")) return;
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "preco", preco_txt, sizeof preco_txt);
    aparar(nome);
    preco = parse_dinheiro(preco_txt);
    if (!nome[0]) { redir(rs, "/servicos", "Informe o nome do serviço.", 1); return; }
    if (preco < 0) { redir(rs, "/servicos", "Preço inválido.", 1); return; }
    if (dur < 30 || dur > 180 || dur % 30 != 0) { redir(rs, "/servicos", "Duração inválida.", 1); return; }
    if (id > 0) {
        st = prep("UPDATE servicos SET nome=?, preco_centavos=?, duracao_min=?, ativo=? WHERE id=?");
        if (!st) return;
        sqlite3_bind_text(st, 1, nome, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 2, preco);
        sqlite3_bind_int(st, 3, dur);
        sqlite3_bind_int(st, 4, ativo);
        sqlite3_bind_int(st, 5, id);
    } else {
        st = prep("INSERT INTO servicos (nome, preco_centavos, duracao_min, ativo) VALUES (?,?,?,?)");
        if (!st) return;
        sqlite3_bind_text(st, 1, nome, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 2, preco);
        sqlite3_bind_int(st, 3, dur);
        sqlite3_bind_int(st, 4, ativo);
    }
    sqlite3_step(st);
    sqlite3_finalize(st);
    redir(rs, "/servicos", "Serviço salvo.", 0);
}

static void pg_config(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char v[200], dias[16];
    int i;
    if (!exige(s, rs, "dono")) return;
    pagina_inicio(b, rq, s, "Configurações da barbearia");
    buf_add(b, "<form method=\"post\" action=\"/config\" class=\"cartao\">\n");
    cfg_get("endereco", v, sizeof v);
    buf_add(b, "<label for=\"endereco\">Endereço</label><input id=\"endereco\" name=\"endereco\" required maxlength=\"150\" size=\"60\" value=\"");
    buf_esc(b, v);
    cfg_get("telefone", v, sizeof v);
    buf_add(b, "\"><label for=\"telefone\">Telefone</label><input id=\"telefone\" name=\"telefone\" required maxlength=\"20\" value=\"");
    buf_esc(b, v);
    buf_add(b, "\"><label for=\"abertura\">Abre às</label><select id=\"abertura\" name=\"abertura\">");
    opcoes_hora(b, 12, 36, cfg_slot("abertura", 18));
    buf_add(b, "</select><label for=\"fechamento\">Fecha às</label><select id=\"fechamento\" name=\"fechamento\">");
    opcoes_hora(b, 14, 46, cfg_slot("fechamento", 38));
    buf_add(b, "</select>\n<fieldset><legend>Dias de funcionamento</legend>");
    cfg_get("dias_abertos", dias, sizeof dias);
    for (i = 0; i < 7; i++)
        buf_add(b, "<label><input type=\"checkbox\" name=\"d%d\" value=\"1\"%s> %s</label>", i, strchr(dias, '0' + i) ? " checked" : "", DIAS[i]);
    cfg_get("antecedencia_horas", v, sizeof v);
    buf_add(b, "</fieldset><label for=\"ant\">Antecedência mínima para o cliente cancelar ou remarcar (horas)</label>"
               "<input id=\"ant\" name=\"antecedencia\" type=\"number\" min=\"0\" max=\"72\" value=\"%d\">"
               "<p><button type=\"submit\">Salvar</button></p></form>\n", atoi(v));
    pagina_fim(b);
}

static void post_config(const Req *rq, Resp *rs, const Sessao *s) {
    char endereco[200], tel[40], dias[8] = "", t[8], tmp[16];
    int ab = param_int(rq->corpo, "abertura", -1), fe = param_int(rq->corpo, "fechamento", -1), i, ant;
    if (!exige(s, rs, "dono")) return;
    param(rq->corpo, "endereco", endereco, sizeof endereco);
    param(rq->corpo, "telefone", tel, sizeof tel);
    aparar(endereco); aparar(tel);
    ant = param_int(rq->corpo, "antecedencia", 2);
    if (!endereco[0] || !tel[0]) { redir(rs, "/config", "Preencha endereço e telefone.", 1); return; }
    if (ab < 0 || fe > 47 || ab >= fe) { redir(rs, "/config", "O horário de abertura precisa ser antes do fechamento.", 1); return; }
    for (i = 0; i < 7; i++) {
        char nome[4];
        snprintf(nome, sizeof nome, "d%d", i);
        if (param(rq->corpo, nome, tmp, sizeof tmp)) { size_t n = strlen(dias); dias[n] = (char)('0' + i); dias[n + 1] = '\0'; }
    }
    if (!dias[0]) { redir(rs, "/config", "Marque pelo menos um dia de funcionamento.", 1); return; }
    if (ant < 0 || ant > 72) ant = 2;
    cfg_set("endereco", endereco);
    cfg_set("telefone", tel);
    slot_texto(ab, t); cfg_set("abertura", t);
    slot_texto(fe, t); cfg_set("fechamento", t);
    cfg_set("dias_abertos", dias);
    snprintf(tmp, sizeof tmp, "%d", ant);
    cfg_set("antecedencia_horas", tmp);
    redir(rs, "/config", "Configurações salvas.", 0);
}

/* Cada relatório é uma consulta SQL; o texto da consulta aparece na tela para a explicação no trabalho */
static void relatorio(Buf *b, const char *titulo, const char *sql, const char *colunas[], int ncol, int moeda_col,
                      const char *de, const char *ate, int usa_periodo) {
    sqlite3_stmt *st = prep(sql);
    int i, vazio = 1;
    buf_add(b, "<table><caption>%s</caption><tr>", titulo);
    for (i = 0; i < ncol; i++) buf_add(b, "<th scope=\"col\">%s</th>", colunas[i]);
    buf_add(b, "</tr>\n");
    if (st) {
        if (usa_periodo) {
            sqlite3_bind_text(st, 1, de, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, ate, -1, SQLITE_STATIC);
        }
        while (sqlite3_step(st) == SQLITE_ROW) {
            vazio = 0;
            buf_add(b, "<tr>");
            for (i = 0; i < ncol; i++) {
                if (i == moeda_col) {
                    char p[24];
                    dinheiro(sqlite3_column_int(st, i), p);
                    buf_add(b, "<td>%s</td>", p);
                } else {
                    buf_add(b, "<td>");
                    buf_esc(b, col(st, i));
                    buf_add(b, "</td>");
                }
            }
            buf_add(b, "</tr>\n");
        }
        sqlite3_finalize(st);
    }
    if (vazio) buf_add(b, "<tr><td colspan=\"%d\">Sem dados no período.</td></tr>\n", ncol);
    buf_add(b, "</table>\n<details><summary>Ver a consulta SQL deste relatório</summary><pre>");
    buf_esc(b, sql);
    buf_add(b, "</pre></details>\n");
}

static void pg_relatorios(const Req *rq, Resp *rs, const Sessao *s) {
    Buf *b = &rs->corpo;
    char de[16], ate[16], hj[11], padrao_de[11];
    static const char *c1[] = { "Dia", "Agendamentos" };
    static const char *c2[] = { "Serviço", "Quantidade", "Faturamento (concluídos)" };
    static const char *c3[] = { "Barbeiro", "Concluídos", "Faltas", "Cancelados", "Ainda agendados" };
    static const char *c4[] = { "Cliente", "Faltas" };
    if (!exige(s, rs, "dono")) return;
    hoje(hj);
    data_mais_dias(hj, -30, padrao_de);
    param(rq->query, "de", de, sizeof de);
    param(rq->query, "ate", ate, sizeof ate);
    if (!data_valida(de)) snprintf(de, sizeof de, "%s", padrao_de);
    if (!data_valida(ate)) snprintf(ate, sizeof ate, "%s", hj);
    pagina_inicio(b, rq, s, "Relatórios");
    buf_add(b, "<form method=\"get\" action=\"/relatorios\" class=\"cartao\"><label for=\"de\">De</label><input id=\"de\" name=\"de\" type=\"date\" value=\"%s\">"
               "<label for=\"ate\">Até</label><input id=\"ate\" name=\"ate\" type=\"date\" value=\"%s\"><p><button type=\"submit\">Atualizar</button></p></form>\n", de, ate);

    relatorio(b, "Agendamentos por dia",
        "SELECT data, COUNT(*) FROM agendamentos\n"
        "WHERE status IN ('agendado','concluido','faltou') AND data BETWEEN ? AND ?\n"
        "GROUP BY data ORDER BY data",
        c1, 2, -1, de, ate, 1);
    relatorio(b, "Serviços mais pedidos",
        "SELECT sv.nome, COUNT(*) AS total,\n"
        "       SUM(CASE WHEN a.status='concluido' THEN sv.preco_centavos ELSE 0 END)\n"
        "FROM agendamentos a JOIN servicos sv ON sv.id = a.servico_id\n"
        "WHERE a.status IN ('agendado','concluido','faltou') AND a.data BETWEEN ? AND ?\n"
        "GROUP BY sv.id ORDER BY total DESC",
        c2, 3, 2, de, ate, 1);
    relatorio(b, "Atendimentos por barbeiro",
        "SELECT u.nome, SUM(a.status='concluido'), SUM(a.status='faltou'),\n"
        "       SUM(a.status='cancelado'), SUM(a.status='agendado')\n"
        "FROM agendamentos a JOIN usuarios u ON u.id = a.barbeiro_id\n"
        "WHERE a.status != 'bloqueado' AND a.data BETWEEN ? AND ?\n"
        "GROUP BY u.id ORDER BY u.nome",
        c3, 5, -1, de, ate, 1);
    relatorio(b, "Clientes que mais faltaram",
        "SELECT COALESCE(u.nome, a.nome_avulso), COUNT(*)\n"
        "FROM agendamentos a LEFT JOIN usuarios u ON u.id = a.cliente_id\n"
        "WHERE a.status = 'faltou' AND a.data BETWEEN ? AND ?\n"
        "GROUP BY COALESCE(a.cliente_id, a.nome_avulso) ORDER BY 2 DESC LIMIT 10",
        c4, 2, -1, de, ate, 1);
    pagina_fim(b);
}

/* ===============================================================
   Parte 10 - Roteamento e início do programa
   =============================================================== */

static void pg_404(const Req *rq, Resp *rs, const Sessao *s) {
    pagina_inicio(&rs->corpo, rq, s, "Página não encontrada");
    buf_add(&rs->corpo, "<p>Esse endereço não existe. <a href=\"/\">Voltar ao início</a>.</p>\n");
    pagina_fim(&rs->corpo);
    rs->status = 404;
}

typedef void (*Pagina)(const Req *, Resp *, const Sessao *);
typedef struct { const char *metodo; const char *caminho; Pagina f; } Rota;

static const Rota ROTAS[] = {
    { "GET",  "/",             pg_inicio },
    { "GET",  "/horarios",     pg_horarios },
    { "GET",  "/cadastro",     pg_cadastro },
    { "POST", "/cadastro",     post_cadastro },
    { "GET",  "/login",        pg_login },
    { "POST", "/login",        post_login },
    { "GET",  "/sair",         pg_sair },
    { "GET",  "/cliente",      pg_cliente },
    { "POST", "/agendar",      post_agendar },
    { "POST", "/cancelar",     post_cancelar },
    { "GET",  "/conta",        pg_conta },
    { "POST", "/conta/dados",  post_conta_dados },
    { "POST", "/conta/senha",  post_conta_senha },
    { "POST", "/conta/excluir", post_conta_excluir },
    { "GET",  "/barbeiro",     pg_barbeiro },
    { "POST", "/status",       post_status },
    { "POST", "/bloquear",     post_bloquear },
    { "GET",  "/agenda",       pg_agenda },
    { "POST", "/agendar-staff", post_agendar_staff },
    { "GET",  "/clientes",     pg_clientes },
    { "POST", "/resetar",      post_resetar },
    { "GET",  "/dono",         pg_dono },
    { "GET",  "/funcionarios", pg_funcionarios },
    { "POST", "/func-novo",    post_func_novo },
    { "POST", "/func-ativo",   post_func_ativo },
    { "GET",  "/servicos",     pg_servicos },
    { "POST", "/serv-salvar",  post_serv_salvar },
    { "GET",  "/config",       pg_config },
    { "POST", "/config",       post_config },
    { "GET",  "/relatorios",   pg_relatorios },
};

static void tratar(const Req *rq, Resp *rs) {
    Sessao s;
    size_t i;
    sessao_ler(rq, &s);
    for (i = 0; i < sizeof ROTAS / sizeof ROTAS[0]; i++) {
        if (strcmp(rq->metodo, ROTAS[i].metodo) == 0 && strcmp(rq->caminho, ROTAS[i].caminho) == 0) {
            ROTAS[i].f(rq, rs, &s);
            return;
        }
    }
    pg_404(rq, rs, &s);
}

static int executar_arquivo_sql(const char *nome) {
    FILE *f = fopen(nome, "rb");
    long n;
    char *txt, *erro = NULL;
    int rc;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    txt = malloc((size_t)n + 1);
    if (!txt) { fclose(f); return 0; }
    n = (long)fread(txt, 1, (size_t)n, f);
    txt[n] = '\0';
    fclose(f);
    rc = sqlite3_exec(db, txt, NULL, NULL, &erro);
    if (rc != SQLITE_OK) { fprintf(stderr, "Erro em %s: %s\n", nome, erro ? erro : "?"); sqlite3_free(erro); }
    free(txt);
    return rc == SQLITE_OK;
}

/* Na primeira vez que o programa roda, cria o dono, a equipe e os serviços */
static void dados_iniciais(void) {
    if (contar("SELECT COUNT(*) FROM usuarios", NULL, NULL) == 0) {
        inserir_usuario("Seu Antônio", "dono", "", "dono123", "dono");
        inserir_usuario("Carlos", "barbeiro1", "", "barbeiro123", "barbeiro");
        inserir_usuario("Rafael", "barbeiro2", "", "barbeiro123", "barbeiro");
        inserir_usuario("Bruno", "barbeiro3", "", "barbeiro123", "barbeiro");
        inserir_usuario("Marina", "recepcao", "", "recepcao123", "recepcionista");
        printf("Usuários iniciais criados (troque as senhas em 'Minha conta'):\n"
               "  dono       -> usuário: dono       senha: dono123\n"
               "  barbeiros  -> usuários: barbeiro1, barbeiro2, barbeiro3   senha: barbeiro123\n"
               "  recepção   -> usuário: recepcao   senha: recepcao123\n");
    }
    if (contar("SELECT COUNT(*) FROM servicos", NULL, NULL) == 0) {
        exec_sql("INSERT INTO servicos (nome, preco_centavos, duracao_min) VALUES "
                 "('Corte de cabelo', 3500, 30), ('Barba', 2500, 30), ('Sobrancelha', 1500, 30), ('Corte + Barba', 5500, 60)");
    }
}

int main(int argc, char **argv) {
    int porta = 8080, i, carregar_exemplo = 0;
    for (i = 1; i < argc; i++) {
        if (atoi(argv[i]) > 0) porta = atoi(argv[i]);                 /* ex: barbearia 8080 */
        if (strcmp(argv[i], "exemplo") == 0) carregar_exemplo = 1;    /* ex: barbearia 8080 exemplo */
    }

    util_iniciar();
    if (sqlite3_open("barbearia.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Não foi possível abrir o banco barbearia.db: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    exec_sql("PRAGMA foreign_keys = ON");
    if (!executar_arquivo_sql("schema.sql")) {
        fprintf(stderr, "Não foi possível ler o schema.sql. Rode o programa dentro da pasta do projeto.\n");
        return 1;
    }
    dados_iniciais();
    if (carregar_exemplo) {
        if (executar_arquivo_sql("dados_exemplo.sql")) printf("Dados de exemplo carregados (clientes ...@exemplo.com, senha cliente123).\n");
        else fprintf(stderr, "Não foi possível carregar o dados_exemplo.sql.\n");
    }

    printf("Barbearia Retrô 2000 no ar.\nAbra o navegador em: http://localhost:%d\nPara parar, feche esta janela ou aperte Ctrl+C.\n", porta);
    fflush(stdout);
    return servidor_rodar(porta, tratar);
}
