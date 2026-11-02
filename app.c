/* app.c - banco de dados, sessões de login e o "esqueleto" das páginas */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

sqlite3 *db = NULL;

/* ============================================================
   BANCO DE DADOS
   ============================================================ */

sqlite3_stmt *db_prep(const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "Erro de SQL: %s\n  %s\n", sqlite3_errmsg(db), sql);
        return NULL;     /* as funções do SQLite aceitam NULL sem travar */
    }
    return st;
}

const char *col_txt(sqlite3_stmt *st, int i) {
    const unsigned char *t = sqlite3_column_text(st, i);
    return t ? (const char *)t : "";
}

void config_get(const char *chave, char *saida, size_t max) {
    sqlite3_stmt *st = db_prep("SELECT valor FROM config WHERE chave = ?");
    if (max == 0) { sqlite3_finalize(st); return; }
    saida[0] = '\0';
    BT(st, 1, chave);
    if (sqlite3_step(st) == SQLITE_ROW) snprintf(saida, max, "%s", col_txt(st, 0));
    sqlite3_finalize(st);
}

void config_set(const char *chave, const char *valor) {
    sqlite3_stmt *st = db_prep("INSERT OR REPLACE INTO config (chave, valor) VALUES (?, ?)");
    BT(st, 1, chave);
    BT(st, 2, valor);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* Cria um usuário guardando só o hash da senha (nunca a senha em si). Devolve o id, ou 0 se falhar. */
int usuario_criar(const char *nome, const char *login, const char *tel, const char *senha, const char *perfil) {
    char salt[33], hash[65];
    int ok;
    sqlite3_stmt *st = db_prep(
        "INSERT INTO usuarios (nome, login, telefone, salt, senha_hash, perfil) VALUES (?, ?, ?, ?, ?, ?)");
    gerar_aleatorio(salt, sizeof salt);
    hash_senha(salt, senha, hash);
    BT(st, 1, nome);
    BT(st, 2, login);
    BT(st, 3, tel);
    BT(st, 4, salt);
    BT(st, 5, hash);
    BT(st, 6, perfil);
    ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok ? (int)sqlite3_last_insert_rowid(db) : 0;
}

int usuario_trocar_senha(int uid, const char *nova) {
    char salt[33], hash[65];
    int ok;
    sqlite3_stmt *st = db_prep("UPDATE usuarios SET salt = ?, senha_hash = ? WHERE id = ?");
    gerar_aleatorio(salt, sizeof salt);
    hash_senha(salt, nova, hash);
    BT(st, 1, salt);
    BT(st, 2, hash);
    sqlite3_bind_int(st, 3, uid);
    ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

static void dados_exemplo(void) {
    /* Só roda na primeira vez, quando o banco está vazio. Os ids saem na ordem: 1 a 6. */
    usuario_criar("Dono da Barbearia", "dono",     "11900000001", "dono123",    "dono");
    usuario_criar("Carlos",            "carlos",   "11900000002", "barber123",  "barbeiro");
    usuario_criar("Rafael",            "rafael",   "11900000003", "barber123",  "barbeiro");
    usuario_criar("Marcelo",           "marcelo",  "11900000004", "barber123",  "barbeiro");
    usuario_criar("Recepção",          "recepcao", "11900000005", "recep123",   "recepcionista");
    usuario_criar("Cliente Exemplo",   "cliente@exemplo.com", "11988887777", "cliente123", "cliente");

    sqlite3_exec(db,
        "INSERT INTO servicos (nome, preco_centavos, duracao_min) VALUES "
        "('Corte', 4000, 30), ('Barba', 3000, 30), ('Sobrancelha', 1500, 30), ('Corte + Barba', 6500, 60);",
        NULL, NULL, NULL);

    /* Alguns atendimentos de exemplo para os relatórios do dono já mostrarem números */
    sqlite3_exec(db,
        "INSERT INTO agendamentos (cliente_id, barbeiro_id, servico_id, data, slot_inicio, slots, status) VALUES "
        "(6, 2, 1, date('now','localtime','-5 day'), 20, 1, 'concluido'),"
        "(6, 3, 4, date('now','localtime','-4 day'), 24, 2, 'concluido'),"
        "(6, 4, 3, date('now','localtime','-3 day'), 30, 1, 'concluido'),"
        "(6, 2, 2, date('now','localtime','-2 day'), 22, 1, 'faltou'),"
        "(6, 3, 1, date('now','localtime','-2 day'), 26, 1, 'concluido'),"
        "(6, 4, 1, date('now','localtime','-1 day'), 28, 1, 'cancelado'),"
        "(6, 2, 4, date('now','localtime','-1 day'), 32, 2, 'concluido');",
        NULL, NULL, NULL);
}

int db_abrir(const char *arquivo, const char *schema) {
    FILE *f;
    long n;
    char *sql, *erro = NULL;
    sqlite3_stmt *st;
    int vazio = 0;

    if (sqlite3_open(arquivo, &db) != SQLITE_OK) {
        fprintf(stderr, "Não foi possível abrir o banco: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    f = fopen(schema, "rb");
    if (!f) {
        fprintf(stderr, "Não achei o arquivo %s. Rode o programa de dentro da pasta do projeto.\n", schema);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    sql = malloc((size_t)n + 1);
    if (!sql) { fclose(f); return 0; }
    n = (long)fread(sql, 1, (size_t)n, f);
    sql[n] = '\0';
    fclose(f);

    if (sqlite3_exec(db, sql, NULL, NULL, &erro) != SQLITE_OK) {
        fprintf(stderr, "Erro no schema.sql: %s\n", erro ? erro : "?");
        sqlite3_free(erro);
        free(sql);
        return 0;
    }
    free(sql);

    st = db_prep("SELECT COUNT(*) FROM usuarios");
    if (sqlite3_step(st) == SQLITE_ROW) vazio = (sqlite3_column_int(st, 0) == 0);
    sqlite3_finalize(st);
    if (vazio) dados_exemplo();
    return 1;
}

/* "23456" -> "Ter, Qua, Qui, Sex, Sáb" */
void dias_texto(char *saida, size_t max) {
    static const char *nomes[7] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb" };
    char dias[16];
    size_t n = 0;
    config_get("dias_abertos", dias, sizeof dias);
    saida[0] = '\0';
    for (int d = 0; d < 7; d++) {
        char c = (char)('0' + d);
        if (strchr(dias, c)) {
            int w = snprintf(saida + n, max - n, "%s%s", n ? ", " : "", nomes[d]);
            if (w < 0 || (size_t)w >= max - n) break;
            n += (size_t)w;
        }
    }
    if (n == 0) snprintf(saida, max, "(nenhum dia)");
}

/* ============================================================
   SESSÕES (quem está logado). Ficam só na memória do programa.
   ============================================================ */

#define MAX_SESSOES 64
#define DURACAO_SESSAO (2 * 60 * 60)     /* 2 horas sem usar o site */

typedef struct {
    char   sid[33];
    Sessao s;
    time_t expira;
} Entrada;

static Entrada tabela[MAX_SESSOES];

const Sessao *sessao_atual(const Req *rq) {
    time_t agora = time(NULL);
    if (!rq->cookie_sid[0]) return NULL;
    for (int i = 0; i < MAX_SESSOES; i++) {
        if (tabela[i].sid[0] && strcmp(tabela[i].sid, rq->cookie_sid) == 0) {
            if (tabela[i].expira < agora) { tabela[i].sid[0] = '\0'; return NULL; }
            tabela[i].expira = agora + DURACAO_SESSAO;
            return &tabela[i].s;
        }
    }
    return NULL;
}

void sessao_criar(Resp *rs, int uid, const char *perfil, const char *nome) {
    time_t agora = time(NULL);
    int escolhida = 0;
    for (int i = 0; i < MAX_SESSOES; i++) {          /* acha uma vaga livre ou vencida */
        if (!tabela[i].sid[0] || tabela[i].expira < agora) { escolhida = i; break; }
        if (tabela[i].expira < tabela[escolhida].expira) escolhida = i;   /* senão troca a mais antiga */
    }
    memset(&tabela[escolhida], 0, sizeof tabela[escolhida]);
    gerar_aleatorio(tabela[escolhida].sid, sizeof tabela[escolhida].sid);
    tabela[escolhida].s.uid = uid;
    snprintf(tabela[escolhida].s.perfil, sizeof tabela[escolhida].s.perfil, "%s", perfil);
    snprintf(tabela[escolhida].s.nome, sizeof tabela[escolhida].s.nome, "%s", nome);
    tabela[escolhida].expira = agora + DURACAO_SESSAO;
    /* HttpOnly: o JavaScript da página não lê o cookie. SameSite=Strict: outros sites não conseguem usá-lo. */
    snprintf(rs->cookie, sizeof rs->cookie, "sid=%s; Path=/; HttpOnly; SameSite=Strict", tabela[escolhida].sid);
}

void sessao_encerrar(const Req *rq, Resp *rs) {
    for (int i = 0; i < MAX_SESSOES; i++)
        if (tabela[i].sid[0] && strcmp(tabela[i].sid, rq->cookie_sid) == 0) tabela[i].sid[0] = '\0';
    snprintf(rs->cookie, sizeof rs->cookie, "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
}

void sessoes_encerrar_usuario(int uid) {
    for (int i = 0; i < MAX_SESSOES; i++)
        if (tabela[i].sid[0] && tabela[i].s.uid == uid) tabela[i].sid[0] = '\0';
}

void sessao_trocar_nome(int uid, const char *nome) {
    for (int i = 0; i < MAX_SESSOES; i++)
        if (tabela[i].sid[0] && tabela[i].s.uid == uid)
            snprintf(tabela[i].s.nome, sizeof tabela[i].s.nome, "%.100s", nome);
}

/* Controle de acesso: devolve a sessão se o perfil é permitido; senão já monta a resposta e devolve NULL. */
const Sessao *exigir(const Req *rq, Resp *rs, const char *perfis) {
    const Sessao *s = sessao_atual(rq);
    char lista[100], procurado[40];
    if (!s) {
        redirecionar_msg(rs, "/login", 0, "Entre com seu usuário e senha para continuar.");
        return NULL;
    }
    snprintf(lista, sizeof lista, ",%s,", perfis);
    snprintf(procurado, sizeof procurado, ",%s,", s->perfil);
    if (!strstr(lista, procurado)) {
        pagina_msg(rs, rq, s, 403, "Acesso negado", "Seu perfil não tem permissão para abrir esta página.",
                   area_do_perfil(s->perfil), "Voltar para a minha área");
        return NULL;
    }
    return s;
}

/* ============================================================
   PÁGINAS
   ============================================================ */

void redirecionar(Resp *rs, const char *destino) {
    rs->status = 303;
    snprintf(rs->local, sizeof rs->local, "%s", destino);
}

void redirecionar_msg(Resp *rs, const char *base, int ok, const char *texto) {
    char enc[600], url[700];
    url_encode(texto, enc, sizeof enc);
    snprintf(url, sizeof url, "%s%cmsg=%s&ok=%d", base, strchr(base, '?') ? '&' : '?', enc, ok);
    redirecionar(rs, url);
}

/* Só aceita voltar para páginas de agendamento (evita redirecionar para sites de fora) */
int destino_seguro(const char *d) {
    if (strncmp(d, "/agendar", 8) != 0 || strlen(d) > 500) return 0;
    for (; *d; d++) if ((unsigned char)*d < 0x20) return 0;
    return 1;
}

const char *rotulo_status(const char *status) {
    if (!strcmp(status, "agendado"))  return "Agendado";
    if (!strcmp(status, "concluido")) return "Concluído";
    if (!strcmp(status, "cancelado")) return "Cancelado";
    if (!strcmp(status, "faltou"))    return "Faltou";
    if (!strcmp(status, "bloqueado")) return "Bloqueado";
    return status;
}

const char *rotulo_perfil(const char *perfil) {
    if (!strcmp(perfil, "dono"))          return "Dono";
    if (!strcmp(perfil, "barbeiro"))      return "Barbeiro";
    if (!strcmp(perfil, "recepcionista")) return "Recepcionista";
    return "Cliente";
}

const char *area_do_perfil(const char *perfil) {
    if (!strcmp(perfil, "dono"))          return "/dono";
    if (!strcmp(perfil, "barbeiro"))      return "/barbeiro";
    if (!strcmp(perfil, "recepcionista")) return "/recepcao";
    return "/cliente";
}

void preco_fmt(int centavos, char *saida, size_t max) {
    snprintf(saida, max, "R$ %d,%02d", centavos / 100, centavos % 100);
}

static const char *CSS =
    ":root{--fundo:#f4ecd8;--texto:#2b1d14;--marrom:#3b2314;--mostarda:#d9a21b;--vermelho:#8f1d22;--verde:#1f5c2e;--linha:#cdbf9f;--card:#fffaf0}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--fundo);color:var(--texto);font:1.05rem/1.55 Georgia,'Times New Roman',serif}"
    "a{color:var(--vermelho)}"
    ".pular{position:absolute;left:-999px}.pular:focus{left:8px;top:8px;background:#fff;padding:8px;z-index:9}"
    "header{background:var(--marrom);color:#fff;border-bottom:5px solid var(--mostarda)}"
    ".topo{max-width:1000px;margin:0 auto;padding:12px 16px;display:flex;flex-wrap:wrap;gap:8px 20px;align-items:center;justify-content:space-between}"
    ".marca{font-size:1.5rem;font-weight:bold;color:var(--mostarda);text-decoration:none;letter-spacing:1px}"
    "nav{display:flex;flex-wrap:wrap;gap:6px 14px;align-items:center}"
    "nav a{color:#fff;text-decoration:none;padding:6px 4px;border-bottom:2px solid transparent}"
    "nav a:hover,nav a:focus{border-bottom-color:var(--mostarda)}"
    "nav .quem{color:var(--mostarda);font-size:.95rem}"
    "nav form{display:inline;margin:0}"
    "main{max-width:1000px;margin:0 auto;padding:20px 16px 40px}"
    "h1,h2,h3{font-family:Georgia,serif;color:var(--marrom);line-height:1.2}"
    "h1{font-size:1.9rem;margin:.3em 0 .5em}h2{font-size:1.35rem;margin-top:1.6em}"
    ".hero{background:var(--card);border:2px solid var(--marrom);border-radius:6px;padding:20px;margin-bottom:20px}"
    ".card{background:var(--card);border:1px solid var(--linha);border-radius:6px;padding:14px 16px;margin:12px 0}"
    ".botao,button{display:inline-block;background:var(--mostarda);color:var(--marrom);border:2px solid var(--marrom);border-radius:5px;"
    "padding:9px 16px;font:bold 1rem Georgia,serif;text-decoration:none;cursor:pointer;min-height:44px}"
    ".botao:hover,button:hover{background:#efbd45}.botao.sec,button.sec{background:#fff}"
    "button.perigo{background:#fff;color:var(--vermelho);border-color:var(--vermelho)}"
    "button.mini{padding:4px 10px;min-height:34px;font-size:.9rem}"
    "a:focus,button:focus,input:focus,select:focus{outline:3px solid #1b5fd1;outline-offset:2px}"
    "label{display:block;font-weight:bold;margin:12px 0 4px}"
    "input,select{font:1rem Georgia,serif;padding:9px;border:2px solid var(--marrom);border-radius:5px;background:#fff;color:var(--texto);max-width:100%}"
    "input[type=text],input[type=email],input[type=password],input[type=tel],input[type=date],select{width:100%;max-width:420px}"
    "input[type=checkbox],input[type=radio]{width:auto;margin-right:6px;transform:scale(1.2)}"
    ".linha{display:flex;flex-wrap:wrap;gap:4px 16px;align-items:flex-end}.linha>label{margin-top:0}"
    ".linha select,.linha input{width:auto;min-width:160px}"
    "table{width:100%;border-collapse:collapse;background:var(--card);margin:10px 0}"
    "th,td{border:1px solid var(--linha);padding:8px 10px;text-align:left;vertical-align:top}"
    "th{background:var(--marrom);color:#fff}"
    ".rolagem{overflow-x:auto}"
    "td form{display:inline;margin:2px 2px 2px 0}"
    ".aviso{padding:10px 14px;border-radius:5px;margin:12px 0;font-weight:bold;border:2px solid}"
    ".aviso.ok{background:#e4f3e6;border-color:var(--verde);color:var(--verde)}"
    ".aviso.erro{background:#fbe6e6;border-color:var(--vermelho);color:var(--vermelho)}"
    ".chips{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0}"
    ".chip{display:inline-block;background:#fff;border:2px solid var(--marrom);border-radius:20px;padding:6px 14px;text-decoration:none;color:var(--marrom);font-weight:bold}"
    ".chip.ativo{background:var(--marrom);color:#fff}"
    "label.chip{margin:0;cursor:pointer}"
    ".status{font-weight:bold}.status.agendado{color:#8a5a00}.status.concluido{color:var(--verde)}"
    ".status.cancelado,.status.faltou{color:var(--vermelho)}.status.bloqueado{color:#555}"
    ".senha{font:bold 1.6rem monospace;background:#fff;border:2px dashed var(--marrom);padding:10px 16px;display:inline-block}"
    "details{margin:8px 0}pre{background:#fff;border:1px solid var(--linha);padding:10px;overflow-x:auto;font-size:.85rem}"
    "footer{border-top:3px double var(--marrom);text-align:center;padding:16px;font-size:.95rem;color:#4a3a2c}"
    ".peq{font-size:.9rem;color:#4a3a2c}";

void pagina_inicio(Resp *rs, const Req *rq, const char *titulo, const Sessao *s) {
    Buf *b = &rs->corpo;
    char m[400] = "";
    buf_add(b, "<!DOCTYPE html>\n<html lang='pt-BR'><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width, initial-scale=1'>"
               "<title>");
    buf_esc(b, titulo);
    buf_add(b, " - Barbearia Retrô 2000</title><style>%s</style></head><body>", CSS);
    buf_add(b, "<a class='pular' href='#conteudo'>Pular para o conteúdo</a>");
    buf_add(b, "<header><div class='topo'><a class='marca' href='/'>BARBEARIA RETRÔ 2000</a><nav aria-label='Menu principal'>");
    buf_add(b, "<a href='/'>Início</a><a href='/horarios'>Horários</a>");
    if (!s || !strcmp(s->perfil, "cliente")) buf_add(b, "<a href='/agendar'>Agendar</a>");
    if (s) {
        buf_add(b, "<a href='%s'>Minha área</a><span class='quem'>", area_do_perfil(s->perfil));
        buf_esc(b, s->nome);
        buf_add(b, " (%s)</span><form method='post' action='/sair'><button class='sec mini' type='submit'>Sair</button></form>",
                rotulo_perfil(s->perfil));
    } else {
        buf_add(b, "<a href='/login'>Entrar</a><a href='/cadastro'>Criar conta</a>");
    }
    buf_add(b, "</nav></div></header><main id='conteudo'>");
    if (rq && param(rq->query, "msg", m, sizeof m) && m[0]) {
        buf_add(b, "<p class='aviso %s' role='alert'>", param_int(rq->query, "ok", 0) ? "ok" : "erro");
        buf_esc(b, m);
        buf_add(b, "</p>");
    }
}

void pagina_fim(Resp *rs) {
    Buf *b = &rs->corpo;
    char end[300], tel[40];
    config_get("endereco", end, sizeof end);
    config_get("telefone", tel, sizeof tel);
    buf_add(b, "</main><footer>Barbearia Retrô 2000 - ");
    buf_esc(b, end);
    buf_add(b, " - Tel. ");
    buf_esc(b, tel);
    buf_add(b, "<br><span class='peq'>Projeto acadêmico (PIM II - UNIP). Barbearia e dados fictícios.</span></footer></body></html>");
}

void pagina_msg(Resp *rs, const Req *rq, const Sessao *s, int status,
                const char *titulo, const char *msg, const char *url, const char *txt_link) {
    Buf *b = &rs->corpo;
    rs->status = status;
    b->len = 0;
    b->d[0] = '\0';
    pagina_inicio(rs, rq, titulo, s);
    buf_add(b, "<h1>");
    buf_esc(b, titulo);
    buf_add(b, "</h1><p>");
    buf_esc(b, msg);
    buf_add(b, "</p>");
    if (url) {
        buf_add(b, "<p><a class='botao' href='");
        buf_esc(b, url);
        buf_add(b, "'>");
        buf_esc(b, txt_link);
        buf_add(b, "</a></p>");
    }
    pagina_fim(rs);
}
