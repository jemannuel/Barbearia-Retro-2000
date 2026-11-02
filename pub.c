/* pub.c - telas públicas (início, login, cadastro) e a área do cliente */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- validações simples ---------- */

static int email_valido(const char *e) {
    const char *arroba = strchr(e, '@');
    size_t n = strlen(e);
    if (n < 5 || n > 100 || !arroba || arroba == e) return 0;
    if (!strchr(arroba, '.') || strchr(e, ' ')) return 0;
    return arroba[1] != '.' && e[n - 1] != '.';
}

/* Deixa só os dígitos e confere se tem 10 ou 11 (DDD + número). Devolve 1 se válido. */
static int telefone_limpo(const char *origem, char *saida, size_t max) {
    size_t n = 0;
    for (; *origem && n + 1 < max; origem++)
        if (isdigit((unsigned char)*origem)) saida[n++] = *origem;
    saida[n] = '\0';
    return n == 10 || n == 11;
}

/* ============================================================
   INÍCIO
   ============================================================ */

void h_home(const Req *rq, Resp *rs) {
    const Sessao *s = sessao_atual(rq);
    Buf *b = &rs->corpo;
    char end[300], tel[40], ab[16], fe[16], dias[80], preco[24];
    sqlite3_stmt *st;

    config_get("endereco", end, sizeof end);
    config_get("telefone", tel, sizeof tel);
    config_get("abertura", ab, sizeof ab);
    config_get("fechamento", fe, sizeof fe);
    dias_texto(dias, sizeof dias);

    pagina_inicio(rs, rq, "Início", s);
    buf_add(b, "<section class='hero'><h1>Barbearia Retrô 2000</h1>"
               "<p>Tradição e estilo no Sacomã desde 2000. Agende seu horário pelo site, sem fila e sem ligação.</p>"
               "<p><a class='botao' href='/agendar'>Agendar horário</a> "
               "<a class='botao sec' href='/horarios'>Ver horários livres</a></p></section>");

    buf_add(b, "<h2>Como chegar e quando abrimos</h2><div class='card'><p><b>Endereço:</b> ");
    buf_esc(b, end);
    buf_add(b, "<br><b>Telefone:</b> ");
    buf_esc(b, tel);
    buf_add(b, "<br><b>Funcionamento:</b> ");
    buf_esc(b, dias);
    buf_add(b, ", das ");
    buf_esc(b, ab);
    buf_add(b, " às ");
    buf_esc(b, fe);
    buf_add(b, ".</p></div>");

    buf_add(b, "<h2>Serviços e preços</h2><div class='rolagem'><table><caption class='peq'>Tabela de serviços</caption>"
               "<tr><th>Serviço</th><th>Preço</th><th>Duração</th></tr>");
    st = db_prep("SELECT nome, preco_centavos, duracao_min FROM servicos WHERE ativo = 1 ORDER BY id");
    while (sqlite3_step(st) == SQLITE_ROW) {
        preco_fmt(sqlite3_column_int(st, 1), preco, sizeof preco);
        buf_add(b, "<tr><td>");
        buf_esc(b, col_txt(st, 0));
        buf_add(b, "</td><td>%s</td><td>%d min</td></tr>", preco, sqlite3_column_int(st, 2));
    }
    sqlite3_finalize(st);
    buf_add(b, "</table></div>");
    pagina_fim(rs);
}

/* ============================================================
   LOGIN E SAÍDA
   ============================================================ */

static void pagina_login(const Req *rq, Resp *rs, const char *erro, const char *login, const char *proximo) {
    Buf *b = &rs->corpo;
    pagina_inicio(rs, rq, "Entrar", NULL);
    buf_add(b, "<h1>Entrar</h1>");
    if (erro && erro[0]) {
        buf_add(b, "<p class='aviso erro' role='alert'>");
        buf_esc(b, erro);
        buf_add(b, "</p>");
    }
    buf_add(b, "<p>Clientes entram com o e-mail. Funcionários entram com o usuário.</p>"
               "<form method='post' action='/login'>"
               "<label for='login'>E-mail ou usuário</label>"
               "<input id='login' name='login' type='text' autocomplete='username' required maxlength='100' value='");
    buf_esc(b, login);
    buf_add(b, "'><label for='senha'>Senha</label>"
               "<input id='senha' name='senha' type='password' autocomplete='current-password' required maxlength='100'>");
    if (proximo && proximo[0]) {
        buf_add(b, "<input type='hidden' name='proximo' value='");
        buf_esc(b, proximo);
        buf_add(b, "'>");
    }
    buf_add(b, "<p><button type='submit'>Entrar</button></p></form>"
               "<p>Ainda não tem conta? <a href='/cadastro");
    if (proximo && proximo[0]) {
        char enc[1300];
        url_encode(proximo, enc, sizeof enc);
        buf_add(b, "?proximo=%s", enc);
    }
    buf_add(b, "'>Criar conta de cliente</a></p>");
    pagina_fim(rs);
}

void h_login_get(const Req *rq, Resp *rs) {
    char proximo[600] = "";
    param(rq->query, "proximo", proximo, sizeof proximo);
    if (!destino_seguro(proximo)) proximo[0] = '\0';
    pagina_login(rq, rs, NULL, "", proximo);
}

void h_login_post(const Req *rq, Resp *rs) {
    char login[110], senha[110], proximo[600] = "";
    sqlite3_stmt *st;
    int achou = 0, uid = 0;
    char salt[40] = "", hash[70] = "", perfil[20] = "", nome[110] = "";

    param(rq->corpo, "login", login, sizeof login);
    param(rq->corpo, "senha", senha, sizeof senha);
    param(rq->corpo, "proximo", proximo, sizeof proximo);
    aparar(login);
    minusculas(login);
    if (!destino_seguro(proximo)) proximo[0] = '\0';

    st = db_prep("SELECT id, nome, salt, senha_hash, perfil FROM usuarios WHERE login = ? AND ativo = 1");
    BT(st, 1, login);
    if (sqlite3_step(st) == SQLITE_ROW) {
        achou = 1;
        uid = sqlite3_column_int(st, 0);
        snprintf(nome, sizeof nome, "%s", col_txt(st, 1));
        snprintf(salt, sizeof salt, "%s", col_txt(st, 2));
        snprintf(hash, sizeof hash, "%s", col_txt(st, 3));
        snprintf(perfil, sizeof perfil, "%s", col_txt(st, 4));
    }
    sqlite3_finalize(st);

    /* A mensagem é a mesma para usuário inexistente e senha errada (não revela quais usuários existem) */
    if (!achou || !senha_confere(salt, hash, senha)) {
        pagina_login(rq, rs, "Usuário ou senha incorretos.", login, proximo);
        return;
    }
    sessao_criar(rs, uid, perfil, nome);
    if (!strcmp(perfil, "cliente") && proximo[0]) redirecionar(rs, proximo);
    else redirecionar(rs, area_do_perfil(perfil));
}

void h_sair(const Req *rq, Resp *rs) {
    sessao_encerrar(rq, rs);
    redirecionar_msg(rs, "/", 1, "Você saiu da sua conta.");
}

/* ============================================================
   CADASTRO DE CLIENTE
   ============================================================ */

static void pagina_cadastro(const Req *rq, Resp *rs, const char *erro,
                            const char *nome, const char *email, const char *tel, const char *proximo) {
    Buf *b = &rs->corpo;
    pagina_inicio(rs, rq, "Criar conta", NULL);
    buf_add(b, "<h1>Criar conta de cliente</h1>");
    if (erro && erro[0]) {
        buf_add(b, "<p class='aviso erro' role='alert'>");
        buf_esc(b, erro);
        buf_add(b, "</p>");
    }
    buf_add(b, "<p>Com a conta você agenda, cancela e remarca horários pelo site.</p>"
               "<form method='post' action='/cadastro'>"
               "<label for='nome'>Nome</label><input id='nome' name='nome' type='text' required maxlength='100' autocomplete='name' value='");
    buf_esc(b, nome);
    buf_add(b, "'><label for='email'>E-mail</label><input id='email' name='email' type='email' required maxlength='100' autocomplete='email' value='");
    buf_esc(b, email);
    buf_add(b, "'><label for='tel'>Telefone com DDD</label><input id='tel' name='telefone' type='tel' required maxlength='20' autocomplete='tel' placeholder='11 91234-5678' value='");
    buf_esc(b, tel);
    buf_add(b, "'><label for='senha'>Senha (mínimo 6 caracteres)</label>"
               "<input id='senha' name='senha' type='password' required minlength='6' maxlength='100' autocomplete='new-password'>"
               "<p><label style='font-weight:normal'><input type='checkbox' name='aceito' value='1' required>"
               "Autorizo o uso do meu nome, e-mail e telefone apenas para gerenciar meus agendamentos nesta barbearia "
               "(LGPD, Lei 13.709/2018). Posso excluir minha conta a qualquer momento em &quot;Meus dados&quot;.</label></p>");
    if (proximo && proximo[0]) {
        buf_add(b, "<input type='hidden' name='proximo' value='");
        buf_esc(b, proximo);
        buf_add(b, "'>");
    }
    buf_add(b, "<p><button type='submit'>Criar conta</button></p></form><p>Já tem conta? <a href='/login");
    if (proximo && proximo[0]) {
        char enc[1300];
        url_encode(proximo, enc, sizeof enc);
        buf_add(b, "?proximo=%s", enc);
    }
    buf_add(b, "'>Entrar</a></p>");
    pagina_fim(rs);
}

void h_cadastro_get(const Req *rq, Resp *rs) {
    char proximo[600] = "";
    param(rq->query, "proximo", proximo, sizeof proximo);
    if (!destino_seguro(proximo)) proximo[0] = '\0';
    pagina_cadastro(rq, rs, NULL, "", "", "", proximo);
}

void h_cadastro_post(const Req *rq, Resp *rs) {
    char nome[110], email[110], tel[40], telbruto[40], senha[110], aceito[8], proximo[600] = "";
    int uid;

    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "email", email, sizeof email);
    param(rq->corpo, "telefone", telbruto, sizeof telbruto);
    param(rq->corpo, "senha", senha, sizeof senha);
    param(rq->corpo, "aceito", aceito, sizeof aceito);
    param(rq->corpo, "proximo", proximo, sizeof proximo);
    aparar(nome);
    aparar(email);
    minusculas(email);
    if (!destino_seguro(proximo)) proximo[0] = '\0';

    if (strlen(nome) < 2 || strlen(nome) > 100) {
        pagina_cadastro(rq, rs, "Informe seu nome.", nome, email, telbruto, proximo); return;
    }
    if (!email_valido(email)) {
        pagina_cadastro(rq, rs, "Informe um e-mail válido.", nome, email, telbruto, proximo); return;
    }
    if (!telefone_limpo(telbruto, tel, sizeof tel)) {
        pagina_cadastro(rq, rs, "Informe o telefone com DDD (10 ou 11 números).", nome, email, telbruto, proximo); return;
    }
    if (strlen(senha) < 6) {
        pagina_cadastro(rq, rs, "A senha precisa ter pelo menos 6 caracteres.", nome, email, telbruto, proximo); return;
    }
    if (strcmp(aceito, "1") != 0) {
        pagina_cadastro(rq, rs, "É preciso aceitar o uso dos dados para criar a conta.", nome, email, telbruto, proximo); return;
    }

    uid = usuario_criar(nome, email, tel, senha, "cliente");      /* o login é único: repetir e-mail falha */
    if (!uid) {
        pagina_cadastro(rq, rs, "Já existe uma conta com esse e-mail. Tente entrar.", nome, email, telbruto, proximo);
        return;
    }
    sessao_criar(rs, uid, "cliente", nome);
    if (proximo[0]) redirecionar(rs, proximo);
    else redirecionar_msg(rs, "/cliente", 1, "Conta criada! Agora você já pode agendar.");
}

/* ============================================================
   ÁREA DO CLIENTE
   ============================================================ */

void h_cliente(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    char d[16], hi[8], hf[8], preco[24];
    int tem = 0;
    if (!s) return;

    pagina_inicio(rs, rq, "Meus agendamentos", s);
    buf_add(b, "<h1>Meus agendamentos</h1><p><a class='botao' href='/agendar'>Novo agendamento</a> "
               "<a class='botao sec' href='/cliente/dados'>Meus dados</a></p>");

    st = db_prep("SELECT a.id, a.data, a.slot_inicio, a.slots, a.status, sv.nome, ub.nome, sv.preco_centavos "
                 "FROM agendamentos a JOIN servicos sv ON sv.id = a.servico_id JOIN usuarios ub ON ub.id = a.barbeiro_id "
                 "WHERE a.cliente_id = ? ORDER BY a.data DESC, a.slot_inicio DESC LIMIT 50");
    sqlite3_bind_int(st, 1, s->uid);
    buf_add(b, "<div class='rolagem'><table><caption class='peq'>Seus agendamentos, do mais novo para o mais antigo</caption>"
               "<tr><th>Data</th><th>Horário</th><th>Serviço</th><th>Barbeiro</th><th>Valor</th><th>Situação</th><th>Ações</th></tr>");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), ini = sqlite3_column_int(st, 2), n = sqlite3_column_int(st, 3);
        const char *status = col_txt(st, 4);
        tem = 1;
        data_br(col_txt(st, 1), d);
        slot_texto(ini, hi);
        slot_texto(ini + n, hf);
        preco_fmt(sqlite3_column_int(st, 7), preco, sizeof preco);
        buf_add(b, "<tr><td>%s</td><td>%s às %s</td><td>", d, hi, hf);
        buf_esc(b, col_txt(st, 5));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 6));
        buf_add(b, "</td><td>%s</td><td><span class='status %s'>%s</span></td><td>", preco, status, rotulo_status(status));
        if (!strcmp(status, "agendado") && minutos_ate(col_txt(st, 1), ini) > 0) {
            buf_add(b, "<a class='botao sec mini' style='min-height:34px;padding:4px 10px' href='/agendar?remarcar=%d'>Remarcar</a> "
                       "<form method='post' action='/cliente/cancelar'><input type='hidden' name='id' value='%d'>"
                       "<button class='perigo mini' type='submit'>Cancelar</button></form>", id, id);
        } else {
            buf_add(b, "-");
        }
        buf_add(b, "</td></tr>");
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='7'>Você ainda não tem agendamentos.</td></tr>");
    buf_add(b, "</table></div>");
    {
        char h[16];
        config_get("antecedencia_horas", h, sizeof h);
        buf_add(b, "<p class='peq'>Cancelamentos e remarcações podem ser feitos com pelo menos %d hora(s) de antecedência. "
                   "Depois disso, ligue para a barbearia.</p>", atoi(h));
    }
    pagina_fim(rs);
}

void h_cliente_cancelar(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    sqlite3_stmt *st;
    int id, ini = 0, achou = 0;
    char data[16] = "", status[20] = "";
    if (!s) return;

    id = param_int(rq->corpo, "id", 0);
    st = db_prep("SELECT data, slot_inicio, status FROM agendamentos WHERE id = ? AND cliente_id = ?");
    sqlite3_bind_int(st, 1, id);
    sqlite3_bind_int(st, 2, s->uid);               /* só enxerga agendamentos que são dele */
    if (sqlite3_step(st) == SQLITE_ROW) {
        achou = 1;
        snprintf(data, sizeof data, "%s", col_txt(st, 0));
        ini = sqlite3_column_int(st, 1);
        snprintf(status, sizeof status, "%s", col_txt(st, 2));
    }
    sqlite3_finalize(st);

    if (!achou || strcmp(status, "agendado") != 0) {
        redirecionar_msg(rs, "/cliente", 0, "Agendamento não encontrado.");
        return;
    }
    if (!antecedencia_ok(data, ini)) {
        redirecionar_msg(rs, "/cliente", 0, "Já passou do prazo para cancelar pelo site. Ligue para a barbearia.");
        return;
    }
    st = db_prep("UPDATE agendamentos SET status = 'cancelado' WHERE id = ?");
    sqlite3_bind_int(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    redirecionar_msg(rs, "/cliente", 1, "Agendamento cancelado. O horário foi liberado.");
}

/* ---------- meus dados ---------- */

void h_dados_get(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    Buf *b = &rs->corpo;
    sqlite3_stmt *st;
    if (!s) return;

    pagina_inicio(rs, rq, "Meus dados", s);
    buf_add(b, "<h1>Meus dados</h1>");
    st = db_prep("SELECT nome, login, telefone FROM usuarios WHERE id = ?");
    sqlite3_bind_int(st, 1, s->uid);
    if (sqlite3_step(st) == SQLITE_ROW) {
        buf_add(b, "<form method='post' action='/cliente/dados'><label for='nome'>Nome</label>"
                   "<input id='nome' name='nome' type='text' required maxlength='100' value='");
        buf_esc(b, col_txt(st, 0));
        buf_add(b, "'><p class='peq'>E-mail (não pode ser alterado): ");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, "</p><label for='tel'>Telefone com DDD</label><input id='tel' name='telefone' type='tel' required maxlength='20' value='");
        buf_esc(b, col_txt(st, 2));
        buf_add(b, "'><p><button type='submit'>Salvar dados</button></p></form>");
    }
    sqlite3_finalize(st);

    buf_add(b, "<h2>Senha</h2><p><a class='botao sec' href='/senha'>Trocar minha senha</a></p>"
               "<h2>Excluir minha conta</h2>"
               "<div class='card'><p>Seus dados pessoais (nome, e-mail e telefone) serão apagados e os agendamentos futuros serão cancelados. "
               "Esta ação não pode ser desfeita.</p>"
               "<form method='post' action='/cliente/excluir'><label for='sx'>Digite sua senha para confirmar</label>"
               "<input id='sx' name='senha' type='password' required maxlength='100' autocomplete='current-password'>"
               "<p><button class='perigo' type='submit'>Excluir minha conta</button></p></form></div>");
    pagina_fim(rs);
}

void h_dados_post(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    char nome[110], telbruto[40], tel[40];
    sqlite3_stmt *st;
    if (!s) return;

    param(rq->corpo, "nome", nome, sizeof nome);
    param(rq->corpo, "telefone", telbruto, sizeof telbruto);
    aparar(nome);
    if (strlen(nome) < 2 || strlen(nome) > 100) {
        redirecionar_msg(rs, "/cliente/dados", 0, "Informe seu nome."); return;
    }
    if (!telefone_limpo(telbruto, tel, sizeof tel)) {
        redirecionar_msg(rs, "/cliente/dados", 0, "Informe o telefone com DDD (10 ou 11 números)."); return;
    }
    st = db_prep("UPDATE usuarios SET nome = ?, telefone = ? WHERE id = ?");
    BT(st, 1, nome);
    BT(st, 2, tel);
    sqlite3_bind_int(st, 3, s->uid);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sessao_trocar_nome(s->uid, nome);              /* atualiza o nome mostrado no topo */
    redirecionar_msg(rs, "/cliente/dados", 1, "Dados atualizados.");
}

static int senha_do_usuario_confere(int uid, const char *senha) {
    sqlite3_stmt *st = db_prep("SELECT salt, senha_hash FROM usuarios WHERE id = ?");
    int ok = 0;
    sqlite3_bind_int(st, 1, uid);
    if (sqlite3_step(st) == SQLITE_ROW) ok = senha_confere(col_txt(st, 0), col_txt(st, 1), senha);
    sqlite3_finalize(st);
    return ok;
}

/* LGPD: apaga os dados pessoais, mas mantém os atendimentos antigos (sem identificar a pessoa) para os relatórios */
void h_cliente_excluir(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    char senha[110], novo_login[40], aleatoria[33];
    sqlite3_stmt *st;
    int uid;
    if (!s) return;
    uid = s->uid;

    param(rq->corpo, "senha", senha, sizeof senha);
    if (!senha_do_usuario_confere(uid, senha)) {
        redirecionar_msg(rs, "/cliente/dados", 0, "Senha incorreta. A conta não foi excluída.");
        return;
    }
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    st = db_prep("UPDATE agendamentos SET status = 'cancelado' WHERE cliente_id = ? AND status = 'agendado' "
                 "AND (data > date('now','localtime') OR data = date('now','localtime'))");
    sqlite3_bind_int(st, 1, uid);
    sqlite3_step(st);
    sqlite3_finalize(st);

    snprintf(novo_login, sizeof novo_login, "removido-%d", uid);
    gerar_aleatorio(aleatoria, sizeof aleatoria);
    st = db_prep("UPDATE usuarios SET nome = 'Cliente removido', login = ?, telefone = NULL, ativo = 0 WHERE id = ?");
    BT(st, 1, novo_login);
    sqlite3_bind_int(st, 2, uid);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    usuario_trocar_senha(uid, aleatoria);          /* ninguém mais consegue entrar nessa conta */

    sessao_encerrar(rq, rs);
    redirecionar_msg(rs, "/", 1, "Sua conta foi excluída e seus dados pessoais foram apagados.");
}

/* ---------- trocar senha (todos os perfis) ---------- */

void h_senha_get(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente,barbeiro,recepcionista,dono");
    Buf *b = &rs->corpo;
    if (!s) return;
    pagina_inicio(rs, rq, "Trocar senha", s);
    buf_add(b, "<h1>Trocar minha senha</h1><form method='post' action='/senha'>"
               "<label for='a'>Senha atual</label><input id='a' name='atual' type='password' required maxlength='100' autocomplete='current-password'>"
               "<label for='n'>Nova senha (mínimo 6 caracteres)</label><input id='n' name='nova' type='password' required minlength='6' maxlength='100' autocomplete='new-password'>"
               "<p><button type='submit'>Salvar nova senha</button></p></form>");
    pagina_fim(rs);
}

void h_senha_post(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente,barbeiro,recepcionista,dono");
    char atual[110], nova[110];
    if (!s) return;
    param(rq->corpo, "atual", atual, sizeof atual);
    param(rq->corpo, "nova", nova, sizeof nova);
    if (!senha_do_usuario_confere(s->uid, atual)) {
        redirecionar_msg(rs, "/senha", 0, "A senha atual está incorreta."); return;
    }
    if (strlen(nova) < 6) {
        redirecionar_msg(rs, "/senha", 0, "A nova senha precisa ter pelo menos 6 caracteres."); return;
    }
    usuario_trocar_senha(s->uid, nova);
    redirecionar_msg(rs, area_do_perfil(s->perfil), 1, "Senha alterada com sucesso.");
}
