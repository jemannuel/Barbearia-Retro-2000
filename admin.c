/* admin.c - painel do dono: funcionários, serviços, configurações, relatórios e clientes */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* Mostra uma senha temporária na tela (só nesta vez; no banco fica apenas o hash) */
static void pagina_senha_temporaria(const Req *rq, Resp *rs, const Sessao *s, const char *nome,
                                    const char *senha, const char *voltar_url) {
    Buf *b = &rs->corpo;
    pagina_inicio(rs, rq, "Senha temporária", s);
    buf_add(b, "<h1>Senha redefinida</h1><p>Nova senha temporária de <b>");
    buf_esc(b, nome);
    buf_add(b, "</b>:</p><p><span class='senha'>");
    buf_esc(b, senha);
    buf_add(b, "</span></p><p>Anote e entregue pessoalmente. Ela não será mostrada de novo. "
               "A pessoa deve trocar a senha no primeiro acesso (menu &quot;Minha área&quot;).</p>"
               "<p><a class='botao' href='");
    buf_esc(b, voltar_url);
    buf_add(b, "'>Voltar</a></p>");
    pagina_fim(rs);
}

/* ============================================================
   PAINEL DO DONO
   ============================================================ */

void h_dono(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    Buf *b = &rs->corpo;
    if (!s) return;
    pagina_inicio(rs, rq, "Painel do dono", s);
    buf_add(b, "<h1>Painel do dono</h1><p>Aqui você controla tudo na barbearia.</p>"
               "<div class='card'><h3>Agenda geral</h3><p>Veja e altere os agendamentos de todos os barbeiros.</p>"
               "<a class='botao' href='/recepcao'>Abrir agenda</a></div>"
               "<div class='card'><h3>Funcionários</h3><p>Cadastre, desative e redefina senhas de barbeiros e da recepção.</p>"
               "<a class='botao' href='/dono/funcionarios'>Gerenciar</a></div>"
               "<div class='card'><h3>Serviços e preços</h3><p>Cadastre serviços, preços e durações.</p>"
               "<a class='botao' href='/dono/servicos'>Gerenciar</a></div>"
               "<div class='card'><h3>Dados da barbearia</h3><p>Endereço, telefone, horário de funcionamento e prazo de cancelamento.</p>"
               "<a class='botao' href='/dono/config'>Editar</a></div>"
               "<div class='card'><h3>Relatórios</h3><p>Atendimentos por dia, serviços mais pedidos, faltas e valores por barbeiro.</p>"
               "<a class='botao' href='/dono/relatorios'>Ver relatórios</a></div>"
               "<div class='card'><h3>Clientes</h3><p>Lista de clientes cadastrados e redefinição de senha.</p>"
               "<a class='botao' href='/recepcao/clientes'>Ver clientes</a></div>");
    pagina_fim(rs);
}

/* ============================================================
   FUNCIONÁRIOS
   ============================================================ */

void h_func_get(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!s) return;

    pagina_inicio(rs, rq, "Funcionários", s);
    buf_add(b, "<h1>Funcionários</h1><div class='rolagem'><table><caption class='peq'>Equipe da barbearia</caption>"
               "<tr><th>Nome</th><th>Usuário</th><th>Telefone</th><th>Função</th><th>Situação</th><th>Ações</th></tr>");
    st = db_prep("SELECT id, nome, login, COALESCE(telefone,''), perfil, ativo FROM usuarios "
                 "WHERE perfil <> 'cliente' ORDER BY CASE perfil WHEN 'dono' THEN 0 WHEN 'barbeiro' THEN 1 ELSE 2 END, nome");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), ativo = sqlite3_column_int(st, 5);
        const char *perfil = col_txt(st, 4);
        buf_add(b, "<tr><td>");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 2));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 3));
        buf_add(b, "</td><td>%s</td><td>%s</td><td>", rotulo_perfil(perfil), ativo ? "Ativo" : "Desativado");
        buf_add(b, "<form method='post' action='/dono/func/senha'><input type='hidden' name='id' value='%d'>"
                   "<button class='mini' type='submit'>Redefinir senha</button></form>", id);
        if (strcmp(perfil, "dono") != 0)
            buf_add(b, "<form method='post' action='/dono/func/ativo'><input type='hidden' name='id' value='%d'>"
                       "<input type='hidden' name='ativo' value='%d'><button class='mini %s' type='submit'>%s</button></form>",
                    id, ativo ? 0 : 1, ativo ? "perigo" : "", ativo ? "Desativar" : "Reativar");
        buf_add(b, "</td></tr>");
    }
    sqlite3_finalize(st);
    buf_add(b, "</table></div><h2>Novo funcionário</h2><form method='post' action='/dono/func/novo'>"
               "<label for='n'>Nome</label><input id='n' name='nome' type='text' required maxlength='100'>"
               "<label for='u'>Usuário (letras minúsculas, números, ponto ou traço)</label>"
               "<input id='u' name='login' type='text' required minlength='3' maxlength='30' pattern='[a-z0-9._-]+'>"
               "<label for='t'>Telefone</label><input id='t' name='telefone' type='tel' maxlength='20'>"
               "<label for='f'>Função</label><select id='f' name='perfil'><option value='barbeiro'>Barbeiro</option>"
               "<option value='recepcionista'>Recepcionista</option></select>"
               "<label for='p'>Senha inicial (mínimo 6 caracteres)</label>"
               "<input id='p' name='senha' type='password' required minlength='6' maxlength='100' autocomplete='new-password'>"
               "<p><button type='submit'>Cadastrar funcionário</button></p></form>");
    pagina_fim(rs);
}

void h_func_novo(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    char nome[110], login[40], tel[40], perfil[20], senha[110];
    if (!s) return;

    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "login", login, sizeof login);
    param(rq->corpo, "telefone", tel, sizeof tel);
    param(rq->corpo, "perfil", perfil, sizeof perfil);
    param(rq->corpo, "senha", senha, sizeof senha);
    aparar(nome);
    aparar(login);
    minusculas(login);

    if (strlen(nome) < 2) { redirecionar_msg(rs, "/dono/funcionarios", 0, "Informe o nome."); return; }
    if (strlen(login) < 3) { redirecionar_msg(rs, "/dono/funcionarios", 0, "O usuário precisa ter pelo menos 3 caracteres."); return; }
    for (const char *c = login; *c; c++)
        if (!(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '.' || *c == '_' || *c == '-')) {
            redirecionar_msg(rs, "/dono/funcionarios", 0, "Usuário só pode ter letras minúsculas sem acento, números, ponto, _ ou traço.");
            return;
        }
    if (strcmp(perfil, "barbeiro") != 0 && strcmp(perfil, "recepcionista") != 0) {
        redirecionar_msg(rs, "/dono/funcionarios", 0, "Função inválida."); return;
    }
    if (strlen(senha) < 6) { redirecionar_msg(rs, "/dono/funcionarios", 0, "A senha precisa ter pelo menos 6 caracteres."); return; }
    if (!usuario_criar(nome, login, tel, senha, perfil)) {
        redirecionar_msg(rs, "/dono/funcionarios", 0, "Já existe alguém com esse usuário.");
        return;
    }
    redirecionar_msg(rs, "/dono/funcionarios", 1, "Funcionário cadastrado.");
}

/* Funcionário que sai: desativa em vez de apagar, para não perder o histórico de atendimentos */
void h_func_ativo(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    int id, ativo;
    sqlite3_stmt *st;
    if (!s) return;
    id = param_int(rq->corpo, "id", 0);
    ativo = param_int(rq->corpo, "ativo", 1) ? 1 : 0;

    st = db_prep("UPDATE usuarios SET ativo = ? WHERE id = ? AND perfil IN ('barbeiro','recepcionista')");
    sqlite3_bind_int(st, 1, ativo);
    sqlite3_bind_int(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    if (!ativo) sessoes_encerrar_usuario(id);       /* derruba o login que estiver aberto */
    redirecionar_msg(rs, "/dono/funcionarios", 1, ativo ? "Funcionário reativado." : "Funcionário desativado.");
}

/* Redefine a senha de funcionário (dono) */
void h_func_senha(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    char nova[12], nome[110] = "";
    sqlite3_stmt *st;
    int id, achou = 0;
    if (!s) return;
    id = param_int(rq->corpo, "id", 0);

    st = db_prep("SELECT nome FROM usuarios WHERE id = ? AND perfil <> 'cliente'");
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) { achou = 1; snprintf(nome, sizeof nome, "%s", col_txt(st, 0)); }
    sqlite3_finalize(st);
    if (!achou) { redirecionar_msg(rs, "/dono/funcionarios", 0, "Funcionário não encontrado."); return; }

    gerar_senha_temporaria(nova, sizeof nova);
    usuario_trocar_senha(id, nova);
    sessoes_encerrar_usuario(id);
    pagina_senha_temporaria(rq, rs, s, nome, nova, "/dono/funcionarios");
}

/* ============================================================
   SERVIÇOS
   ============================================================ */

/* "40,00" ou "40.5" -> centavos. Devolve -1 se inválido. */
static int preco_para_centavos(const char *txt) {
    char tmp[32], *fim;
    double v;
    snprintf(tmp, sizeof tmp, "%s", txt);
    for (char *c = tmp; *c; c++) if (*c == ',') *c = '.';
    v = strtod(tmp, &fim);
    if (fim == tmp || *fim != '\0' || v < 0 || v > 5000) return -1;
    return (int)(v * 100 + 0.5);
}

static void linha_servico(Buf *b, int id, const char *nome, int centavos, int duracao, int ativo) {
    buf_add(b, "<tr><form method='post' action='/dono/servico/salvar'><td>"
               "<input type='hidden' name='id' value='%d'><input name='nome' type='text' required maxlength='60' aria-label='Nome do serviço' value='", id);
    buf_esc(b, nome);
    buf_add(b, "'></td><td><input name='preco' type='text' required inputmode='decimal' size='8' aria-label='Preço em reais' value='%d,%02d'></td>"
               "<td><select name='duracao' aria-label='Duração'>", centavos / 100, centavos % 100);
    for (int d = 30; d <= 180; d += 30) buf_add(b, "<option value='%d'%s>%d min</option>", d, d == duracao ? " selected" : "", d);
    buf_add(b, "</select></td><td><label style='margin:0'><input type='checkbox' name='ativo' value='1'%s>Ativo</label></td>"
               "<td><button class='mini' type='submit'>%s</button></td></form></tr>", ativo ? " checked" : "", id ? "Salvar" : "Adicionar");
}

void h_serv_get(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!s) return;

    pagina_inicio(rs, rq, "Serviços", s);
    buf_add(b, "<h1>Serviços e preços</h1><p class='peq'>A duração define quantos blocos de 30 minutos o serviço ocupa na agenda. "
               "Serviços desativados somem do site, mas o histórico é mantido.</p>"
               "<div class='rolagem'><table><caption class='peq'>Serviços</caption>"
               "<tr><th>Nome</th><th>Preço (R$)</th><th>Duração</th><th>Situação</th><th>Ação</th></tr>");
    st = db_prep("SELECT id, nome, preco_centavos, duracao_min, ativo FROM servicos ORDER BY id");
    while (sqlite3_step(st) == SQLITE_ROW)
        linha_servico(b, sqlite3_column_int(st, 0), col_txt(st, 1), sqlite3_column_int(st, 2),
                      sqlite3_column_int(st, 3), sqlite3_column_int(st, 4));
    sqlite3_finalize(st);
    linha_servico(b, 0, "", 0, 30, 1);
    buf_add(b, "</table></div><p class='peq'>A última linha, em branco, serve para cadastrar um serviço novo.</p>");
    pagina_fim(rs);
}

void h_serv_salvar(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    char nome[80], preco_txt[32], ativo_txt[8];
    int id, duracao, centavos, ativo;
    sqlite3_stmt *st;
    if (!s) return;

    id = param_int(rq->corpo, "id", 0);
    duracao = param_int(rq->corpo, "duracao", 30);
    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "preco", preco_txt, sizeof preco_txt);
    ativo = param(rq->corpo, "ativo", ativo_txt, sizeof ativo_txt) ? 1 : 0;
    aparar(nome);

    if (strlen(nome) < 2) { redirecionar_msg(rs, "/dono/servicos", 0, "Informe o nome do serviço."); return; }
    centavos = preco_para_centavos(preco_txt);
    if (centavos < 0) { redirecionar_msg(rs, "/dono/servicos", 0, "Preço inválido. Use um valor como 40,00."); return; }
    if (duracao < 30 || duracao > 180 || duracao % 30 != 0) {
        redirecionar_msg(rs, "/dono/servicos", 0, "A duração precisa ser múltipla de 30 minutos."); return;
    }
    if (id > 0) {
        st = db_prep("UPDATE servicos SET nome = ?, preco_centavos = ?, duracao_min = ?, ativo = ? WHERE id = ?");
        BT(st, 1, nome);
        sqlite3_bind_int(st, 2, centavos);
        sqlite3_bind_int(st, 3, duracao);
        sqlite3_bind_int(st, 4, ativo);
        sqlite3_bind_int(st, 5, id);
    } else {
        st = db_prep("INSERT INTO servicos (nome, preco_centavos, duracao_min, ativo) VALUES (?, ?, ?, ?)");
        BT(st, 1, nome);
        sqlite3_bind_int(st, 2, centavos);
        sqlite3_bind_int(st, 3, duracao);
        sqlite3_bind_int(st, 4, ativo);
    }
    sqlite3_step(st);
    sqlite3_finalize(st);
    redirecionar_msg(rs, "/dono/servicos", 1, id > 0 ? "Serviço salvo." : "Serviço cadastrado.");
}

/* ============================================================
   DADOS DA BARBEARIA
   ============================================================ */

static void select_hora(Buf *b, const char *nome, const char *rotulo, const char *atual) {
    char hh[8];
    buf_add(b, "<label for='%s'>%s<select id='%s' name='%s'>", nome, rotulo, nome, nome);
    for (int sl = 0; sl < 48; sl++) {
        slot_texto(sl, hh);
        buf_add(b, "<option value='%s'%s>%s</option>", hh, strcmp(hh, atual) == 0 ? " selected" : "", hh);
    }
    buf_add(b, "</select></label>");
}

void h_config_get(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    Buf *b = &rs->corpo;
    char end[300], tel[40], ab[16], fe[16], dias[16], ant[16];
    static const char *nomes[7] = { "Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado" };
    if (!s) return;
    config_get("endereco", end, sizeof end);
    config_get("telefone", tel, sizeof tel);
    config_get("abertura", ab, sizeof ab);
    config_get("fechamento", fe, sizeof fe);
    config_get("dias_abertos", dias, sizeof dias);
    config_get("antecedencia_horas", ant, sizeof ant);

    pagina_inicio(rs, rq, "Dados da barbearia", s);
    buf_add(b, "<h1>Dados da barbearia</h1><form method='post' action='/dono/config'>"
               "<label for='e'>Endereço</label><input id='e' name='endereco' type='text' required maxlength='250' value='");
    buf_esc(b, end);
    buf_add(b, "'><label for='t'>Telefone</label><input id='t' name='telefone' type='text' required maxlength='30' value='");
    buf_esc(b, tel);
    buf_add(b, "'><div class='linha'>");
    select_hora(b, "abertura", "Abre às", ab);
    select_hora(b, "fechamento", "Fecha às", fe);
    buf_add(b, "</div><fieldset style='margin:14px 0;max-width:420px'><legend><b>Dias de funcionamento</b></legend>");
    for (int d = 0; d < 7; d++)
        buf_add(b, "<label style='font-weight:normal;margin:4px 0'><input type='checkbox' name='dia%d' value='1'%s>%s</label>",
                d, strchr(dias, '0' + d) ? " checked" : "", nomes[d]);
    buf_add(b, "</fieldset><label for='a'>Antecedência mínima para o cliente cancelar ou remarcar (horas)</label>"
               "<input id='a' name='antecedencia' type='text' inputmode='numeric' required maxlength='2' size='4' value='%d'>"
               "<p><button type='submit'>Salvar</button></p></form>", atoi(ant));
    pagina_fim(rs);
}

void h_config_post(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    char end[300], tel[40], ab[16], fe[16], dias[16] = "", tmp[8], ant[16];
    int n = 0, horas;
    if (!s) return;

    param(rq->corpo, "endereco", end, sizeof end);
    param(rq->corpo, "telefone", tel, sizeof tel);
    param(rq->corpo, "abertura", ab, sizeof ab);
    param(rq->corpo, "fechamento", fe, sizeof fe);
    horas = param_int(rq->corpo, "antecedencia", 2);
    aparar(end);
    aparar(tel);
    for (int d = 0; d < 7; d++) {
        char chave[16];
        snprintf(chave, sizeof chave, "dia%d", d);
        if (param(rq->corpo, chave, tmp, sizeof tmp)) dias[n++] = (char)('0' + d);
    }
    dias[n] = '\0';

    if (end[0] == '\0' || tel[0] == '\0') { redirecionar_msg(rs, "/dono/config", 0, "Preencha endereço e telefone."); return; }
    if (hora_para_slot(ab) < 0 || hora_para_slot(fe) < 0 || hora_para_slot(ab) >= hora_para_slot(fe)) {
        redirecionar_msg(rs, "/dono/config", 0, "O horário de fechamento precisa ser depois do de abertura."); return;
    }
    if (n == 0) { redirecionar_msg(rs, "/dono/config", 0, "Marque pelo menos um dia de funcionamento."); return; }
    if (horas < 0 || horas > 72) { redirecionar_msg(rs, "/dono/config", 0, "A antecedência deve ficar entre 0 e 72 horas."); return; }

    snprintf(ant, sizeof ant, "%d", horas);
    config_set("endereco", end);
    config_set("telefone", tel);
    config_set("abertura", ab);
    config_set("fechamento", fe);
    config_set("dias_abertos", dias);
    config_set("antecedencia_horas", ant);
    redirecionar_msg(rs, "/dono/config", 1, "Dados salvos.");
}

/* ============================================================
   RELATÓRIOS (cada um é um SELECT com JOIN e GROUP BY)
   ============================================================ */

static const char *SQL_POR_DIA =
    "SELECT data, COUNT(*) AS total\n"
    "FROM agendamentos\n"
    "WHERE status IN ('agendado','concluido','faltou')\n"
    "  AND data BETWEEN :de AND :ate\n"
    "GROUP BY data\n"
    "ORDER BY data;";

static const char *SQL_SERVICOS =
    "SELECT s.nome, COUNT(*) AS vezes\n"
    "FROM agendamentos a\n"
    "JOIN servicos s ON s.id = a.servico_id\n"
    "WHERE a.status IN ('agendado','concluido','faltou')\n"
    "  AND a.data BETWEEN :de AND :ate\n"
    "GROUP BY s.id\n"
    "ORDER BY vezes DESC;";

static const char *SQL_BARBEIROS =
    "SELECT u.nome,\n"
    "       SUM(a.status = 'concluido') AS concluidos,\n"
    "       SUM(a.status = 'faltou')    AS faltas,\n"
    "       SUM(a.status = 'cancelado') AS cancelados,\n"
    "       SUM(CASE WHEN a.status = 'concluido' THEN s.preco_centavos ELSE 0 END) AS valor\n"
    "FROM agendamentos a\n"
    "JOIN usuarios u ON u.id = a.barbeiro_id\n"
    "JOIN servicos s ON s.id = a.servico_id\n"
    "WHERE a.data BETWEEN :de AND :ate\n"
    "GROUP BY u.id\n"
    "ORDER BY concluidos DESC;";

static void bind_periodo(sqlite3_stmt *st, const char *de, const char *ate) {
    sqlite3_bind_text(st, sqlite3_bind_parameter_index(st, ":de"), de, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, sqlite3_bind_parameter_index(st, ":ate"), ate, -1, SQLITE_TRANSIENT);
}

static void mostra_sql(Buf *b, const char *sql) {
    buf_add(b, "<details><summary>Ver o SQL deste relatório</summary><pre>");
    buf_esc(b, sql);
    buf_add(b, "</pre></details>");
}

void h_relatorios(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "dono");
    Buf *b = &rs->corpo;
    char de[16] = "", ate[16] = "", h[11], dbr[16], pf[24];
    sqlite3_stmt *st;
    int tem;
    if (!s) return;

    hoje(h);
    param(rq->query, "de", de, sizeof de);
    param(rq->query, "ate", ate, sizeof ate);
    if (!data_valida(de)) data_mais_dias(h, -30, de);
    if (!data_valida(ate)) data_mais_dias(h, 30, ate);
    if (strcmp(de, ate) > 0) { char t[16]; snprintf(t, sizeof t, "%s", de); snprintf(de, sizeof de, "%s", ate); snprintf(ate, sizeof ate, "%s", t); }

    pagina_inicio(rs, rq, "Relatórios", s);
    buf_add(b, "<h1>Relatórios</h1><form method='get' action='/dono/relatorios' class='linha'>"
               "<label for='de'>De<input id='de' type='date' name='de' value='%s'></label>"
               "<label for='ate'>Até<input id='ate' type='date' name='ate' value='%s'></label>"
               "<button type='submit'>Atualizar</button></form>", de, ate);

    /* 1) Atendimentos por dia */
    buf_add(b, "<h2>Agendamentos por dia</h2><div class='rolagem'><table><tr><th>Data</th><th>Agendamentos</th></tr>");
    st = db_prep(SQL_POR_DIA);
    bind_periodo(st, de, ate);
    tem = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        tem = 1;
        data_br(col_txt(st, 0), dbr);
        buf_add(b, "<tr><td>%s</td><td>%d</td></tr>", dbr, sqlite3_column_int(st, 1));
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='2'>Sem agendamentos no período.</td></tr>");
    buf_add(b, "</table></div>");
    mostra_sql(b, SQL_POR_DIA);

    /* 2) Serviços mais pedidos */
    buf_add(b, "<h2>Serviços mais pedidos</h2><div class='rolagem'><table><tr><th>Serviço</th><th>Vezes</th></tr>");
    st = db_prep(SQL_SERVICOS);
    bind_periodo(st, de, ate);
    tem = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        tem = 1;
        buf_add(b, "<tr><td>");
        buf_esc(b, col_txt(st, 0));
        buf_add(b, "</td><td>%d</td></tr>", sqlite3_column_int(st, 1));
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='2'>Sem dados no período.</td></tr>");
    buf_add(b, "</table></div>");
    mostra_sql(b, SQL_SERVICOS);

    /* 3) Desempenho por barbeiro */
    buf_add(b, "<h2>Atendimentos, faltas e valores por barbeiro</h2><div class='rolagem'><table>"
               "<tr><th>Barbeiro</th><th>Concluídos</th><th>Faltas</th><th>Cancelados</th><th>Valor dos concluídos</th></tr>");
    st = db_prep(SQL_BARBEIROS);
    bind_periodo(st, de, ate);
    tem = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        tem = 1;
        preco_fmt(sqlite3_column_int(st, 4), pf, sizeof pf);
        buf_add(b, "<tr><td>");
        buf_esc(b, col_txt(st, 0));
        buf_add(b, "</td><td>%d</td><td>%d</td><td>%d</td><td>%s</td></tr>",
                sqlite3_column_int(st, 1), sqlite3_column_int(st, 2), sqlite3_column_int(st, 3), pf);
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='5'>Sem dados no período.</td></tr>");
    buf_add(b, "</table></div>");
    mostra_sql(b, SQL_BARBEIROS);
    pagina_fim(rs);
}

/* ============================================================
   CLIENTES (recepção e dono)
   ============================================================ */

void h_clientes(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    int tem = 0;
    if (!s) return;

    pagina_inicio(rs, rq, "Clientes", s);
    buf_add(b, "<h1>Clientes</h1><p class='peq'>Se um cliente esquecer a senha, redefina aqui e entregue a senha temporária a ele.</p>"
               "<div class='rolagem'><table><caption class='peq'>Clientes cadastrados</caption>"
               "<tr><th>Nome</th><th>E-mail</th><th>Telefone</th><th>Ação</th></tr>");
    st = db_prep("SELECT id, nome, login, COALESCE(telefone,'') FROM usuarios WHERE perfil = 'cliente' AND ativo = 1 ORDER BY nome");
    while (sqlite3_step(st) == SQLITE_ROW) {
        tem = 1;
        buf_add(b, "<tr><td>");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 2));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 3));
        buf_add(b, "</td><td><form method='post' action='/recepcao/clientes/senha'><input type='hidden' name='id' value='%d'>"
                   "<button class='mini' type='submit'>Redefinir senha</button></form></td></tr>", sqlite3_column_int(st, 0));
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='4'>Nenhum cliente cadastrado.</td></tr>");
    buf_add(b, "</table></div>");
    pagina_fim(rs);
}

void h_cliente_senha(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    char nova[12], nome[110] = "";
    sqlite3_stmt *st;
    int id, achou = 0;
    if (!s) return;
    id = param_int(rq->corpo, "id", 0);

    st = db_prep("SELECT nome FROM usuarios WHERE id = ? AND perfil = 'cliente' AND ativo = 1");
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) { achou = 1; snprintf(nome, sizeof nome, "%s", col_txt(st, 0)); }
    sqlite3_finalize(st);
    if (!achou) { redirecionar_msg(rs, "/recepcao/clientes", 0, "Cliente não encontrado."); return; }

    gerar_senha_temporaria(nova, sizeof nova);
    usuario_trocar_senha(id, nova);
    sessoes_encerrar_usuario(id);
    pagina_senha_temporaria(rq, rs, s, nome, nova, "/recepcao/clientes");
}
