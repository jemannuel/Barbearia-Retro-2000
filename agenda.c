/* agenda.c - regras de agendamento (blocos de 30 min) e telas de horários, barbeiro e recepção */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
   REGRAS DE NEGÓCIO
   O dia é dividido em blocos de 30 minutos ("slots"):
   slot 18 = 09:00, slot 19 = 09:30, ... slot 38 = 19:00.
   Um serviço de 60 minutos ocupa 2 blocos seguidos.
   ============================================================ */

/* Primeiro e último bloco do expediente, conforme a configuração do dono */
static void faixa(int *ini, int *fim) {
    char a[16], f[16];
    config_get("abertura", a, sizeof a);
    config_get("fechamento", f, sizeof f);
    *ini = hora_para_slot(a);
    *fim = hora_para_slot(f);
    if (*ini < 0 || *fim < 0 || *ini >= *fim) { *ini = 18; *fim = 38; }
}

static int dia_aberto(const char *data) {
    char dias[16], d[2];
    config_get("dias_abertos", dias, sizeof dias);
    d[0] = (char)('0' + dia_semana(data));
    d[1] = '\0';
    return strstr(dias, d) != NULL;
}

/* Regra RN05: cancelar/remarcar só com antecedência mínima definida pelo dono */
int antecedencia_ok(const char *data, int slot) {
    char v[16];
    config_get("antecedencia_horas", v, sizeof v);
    return minutos_ate(data, slot) >= (long)atoi(v) * 60;
}

/* Regra RN01: um barbeiro não pode ter dois atendimentos no mesmo horário.
   Dois intervalos se sobrepõem quando: início_A < fim_B  E  início_B < fim_A */
static int ocupado(int barbeiro, const char *data, int slot, int n) {
    sqlite3_stmt *st = db_prep(
        "SELECT COUNT(*) FROM agendamentos WHERE barbeiro_id = ? AND data = ? AND status <> 'cancelado' "
        "AND slot_inicio < ? AND slot_inicio + slots > ?");
    int c = 0;
    sqlite3_bind_int(st, 1, barbeiro);
    BT(st, 2, data);
    sqlite3_bind_int(st, 3, slot + n);
    sqlite3_bind_int(st, 4, slot);
    if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return c > 0;
}

/* Horário livre = dentro do expediente + no futuro (RN02) + sem conflito (RN01) */
static int horario_livre(int barbeiro, const char *data, int slot, int n) {
    int ini, fim;
    faixa(&ini, &fim);
    if (slot < ini || slot + n > fim) return 0;
    if (minutos_ate(data, slot) <= 0) return 0;
    return !ocupado(barbeiro, data, slot, n);
}

/* Só dá para agendar de hoje até 30 dias à frente, em dia de funcionamento */
static int validar_data_agenda(const char *data, char *erro, size_t emax) {
    char h[11], lim[11];
    if (!data_valida(data)) { snprintf(erro, emax, "Escolha uma data válida."); return 0; }
    hoje(h);
    data_mais_dias(h, 30, lim);
    if (strcmp(data, h) < 0)   { snprintf(erro, emax, "Não é possível agendar em datas que já passaram."); return 0; }
    if (strcmp(data, lim) > 0) { snprintf(erro, emax, "Só é possível agendar até 30 dias à frente."); return 0; }
    if (!dia_aberto(data))     { snprintf(erro, emax, "A barbearia não abre nesse dia."); return 0; }
    return 1;
}

static int obter_servico(int id, char *nome, size_t nmax, int *slots, int *preco) {
    sqlite3_stmt *st = db_prep("SELECT nome, preco_centavos, duracao_min FROM servicos WHERE id = ? AND ativo = 1");
    int ok = 0;
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        ok = 1;
        if (nome) snprintf(nome, nmax, "%s", col_txt(st, 0));
        if (preco) *preco = sqlite3_column_int(st, 1);
        if (slots) *slots = (sqlite3_column_int(st, 2) + 29) / 30;     /* arredonda para cima */
    }
    sqlite3_finalize(st);
    return ok;
}

static int barbeiro_ativo(int id) {
    sqlite3_stmt *st = db_prep("SELECT COUNT(*) FROM usuarios WHERE id = ? AND perfil = 'barbeiro' AND ativo = 1");
    int c = 0;
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return c > 0;
}

static void nome_usuario(int id, char *saida, size_t max) {
    sqlite3_stmt *st = db_prep("SELECT nome FROM usuarios WHERE id = ?");
    snprintf(saida, max, "%s", "?");
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) snprintf(saida, max, "%s", col_txt(st, 0));
    sqlite3_finalize(st);
}

/* Grava o agendamento depois de conferir todas as regras. Devolve 1 se deu certo. */
static int criar_agendamento(int cliente_id, const char *nome_av, const char *tel_av, int barbeiro, int servico,
                             const char *data, int slot, int *novo_id, char *erro, size_t emax) {
    int slots = 0, ok;
    sqlite3_stmt *st;

    if (!validar_data_agenda(data, erro, emax)) return 0;
    if (!obter_servico(servico, NULL, 0, &slots, NULL)) { snprintf(erro, emax, "Escolha um serviço válido."); return 0; }
    if (!barbeiro_ativo(barbeiro)) { snprintf(erro, emax, "Escolha um barbeiro válido."); return 0; }
    if (!horario_livre(barbeiro, data, slot, slots)) {
        snprintf(erro, emax, "Esse horário não está mais disponível. Escolha outro.");
        return 0;
    }
    st = db_prep("INSERT INTO agendamentos (cliente_id, nome_avulso, tel_avulso, barbeiro_id, servico_id, data, slot_inicio, slots) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    if (cliente_id > 0) sqlite3_bind_int(st, 1, cliente_id); else sqlite3_bind_null(st, 1);
    if (nome_av && nome_av[0]) BT(st, 2, nome_av); else sqlite3_bind_null(st, 2);
    if (tel_av && tel_av[0]) BT(st, 3, tel_av); else sqlite3_bind_null(st, 3);
    sqlite3_bind_int(st, 4, barbeiro);
    sqlite3_bind_int(st, 5, servico);
    BT(st, 6, data);
    sqlite3_bind_int(st, 7, slot);
    sqlite3_bind_int(st, 8, slots);
    ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    if (!ok) { snprintf(erro, emax, "Não foi possível gravar o agendamento."); return 0; }
    *novo_id = (int)sqlite3_last_insert_rowid(db);
    return 1;
}

/* ============================================================
   FORMULÁRIO DE ESCOLHA (serviço, barbeiro e data)
   ============================================================ */

static void form_escolha(Buf *b, const char *action, int servico, int barbeiro, const char *data,
                         int remarcar, int servico_obrigatorio) {
    sqlite3_stmt *st;
    char h[11], lim[11], preco[24];
    hoje(h);
    data_mais_dias(h, 30, lim);

    buf_add(b, "<form method='get' action='%s' class='linha'>", action);
    buf_add(b, "<label for='servico'>Serviço<select id='servico' name='servico'%s>",
            servico_obrigatorio ? " required" : "");
    buf_add(b, "<option value='%s'>%s</option>", servico_obrigatorio ? "" : "0",
            servico_obrigatorio ? "Escolha..." : "Qualquer (meia hora)");
    st = db_prep("SELECT id, nome, preco_centavos, duracao_min FROM servicos WHERE ativo = 1 ORDER BY id");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        preco_fmt(sqlite3_column_int(st, 2), preco, sizeof preco);
        buf_add(b, "<option value='%d'%s>", id, id == servico ? " selected" : "");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, " - %s - %d min</option>", preco, sqlite3_column_int(st, 3));
    }
    sqlite3_finalize(st);
    buf_add(b, "</select></label><label for='barbeiro'>Barbeiro<select id='barbeiro' name='barbeiro' required>"
               "<option value=''>Escolha...</option>");
    st = db_prep("SELECT id, nome FROM usuarios WHERE perfil = 'barbeiro' AND ativo = 1 ORDER BY nome");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        buf_add(b, "<option value='%d'%s>", id, id == barbeiro ? " selected" : "");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, "</option>");
    }
    sqlite3_finalize(st);
    buf_add(b, "</select></label><label for='data'>Data<input id='data' name='data' type='date' required min='%s' max='%s' value='", h, lim);
    buf_esc(b, data);
    buf_add(b, "'></label>");
    if (remarcar > 0) buf_add(b, "<input type='hidden' name='remarcar' value='%d'>", remarcar);
    buf_add(b, "<button type='submit'>Ver horários</button></form>");
}

/* Lista os horários de início possíveis para um serviço de "n" blocos */
static int listar_livres(int barbeiro, const char *data, int n, int out[48]) {
    int ini, fim, c = 0;
    faixa(&ini, &fim);
    for (int s = ini; s + n <= fim && c < 48; s++)
        if (horario_livre(barbeiro, data, s, n)) out[c++] = s;
    return c;
}

/* ============================================================
   HORÁRIOS LIVRES (público, sem login)
   ============================================================ */

void h_horarios(const Req *rq, Resp *rs) {
    const Sessao *s = sessao_atual(rq);
    Buf *b = &rs->corpo;
    int servico = param_int(rq->query, "servico", 0), barbeiro = param_int(rq->query, "barbeiro", 0);
    char data[16] = "", erro[200], hi[8];
    param(rq->query, "data", data, sizeof data);

    pagina_inicio(rs, rq, "Horários livres", s);
    buf_add(b, "<h1>Horários livres</h1><p>Escolha o barbeiro e o dia para ver os horários disponíveis. "
               "Para reservar, é preciso ter uma conta.</p>");
    form_escolha(b, "/horarios", servico, barbeiro, data, 0, 0);

    if (barbeiro > 0 && data[0]) {
        int slots = 1, livres[48], c;
        if (servico > 0 && !obter_servico(servico, NULL, 0, &slots, NULL)) slots = 1;
        if (!validar_data_agenda(data, erro, sizeof erro)) {
            buf_add(b, "<p class='aviso erro' role='alert'>");
            buf_esc(b, erro);
            buf_add(b, "</p>");
        } else if (!barbeiro_ativo(barbeiro)) {
            buf_add(b, "<p class='aviso erro' role='alert'>Barbeiro não encontrado.</p>");
        } else {
            char dbr[16], nome[110];
            data_br(data, dbr);
            nome_usuario(barbeiro, nome, sizeof nome);
            c = listar_livres(barbeiro, data, slots, livres);
            buf_add(b, "<h2>");
            buf_esc(b, nome);
            buf_add(b, " em %s</h2>", dbr);
            if (c == 0) {
                buf_add(b, "<p>Não há horários livres nesse dia. Tente outra data ou outro barbeiro.</p>");
            } else {
                buf_add(b, "<div class='chips' role='list'>");
                for (int i = 0; i < c; i++) {
                    slot_texto(livres[i], hi);
                    buf_add(b, "<span class='chip' role='listitem'>%s</span>", hi);
                }
                buf_add(b, "</div><p><a class='botao' href='/agendar?servico=%d&amp;barbeiro=%d&amp;data=%s'>Quero agendar</a></p>",
                        servico, barbeiro, data);
            }
        }
    }
    pagina_fim(rs);
}

/* ============================================================
   AGENDAR (cliente) e NOVO AGENDAMENTO (recepção / dono)
   As duas telas usam o mesmo código. A diferença é "staff":
   o cliente agenda para ele mesmo; a recepção digita nome e telefone.
   ============================================================ */

static void pagina_agendar(const Req *rq, Resp *rs, const Sessao *s, int staff) {
    Buf *b = &rs->corpo;
    int servico = param_int(rq->query, "servico", 0), barbeiro = param_int(rq->query, "barbeiro", 0);
    int remarcar = param_int(rq->query, "remarcar", 0);
    char data[16] = "", erro[200], hi[8], hf[8], dbr[16];
    const char *self = staff ? "/recepcao/novo" : "/agendar";
    const char *acao = staff ? "/recepcao/confirmar" : "/agendar";
    param(rq->query, "data", data, sizeof data);

    if (remarcar > 0) {
        sqlite3_stmt *st = db_prep("SELECT servico_id, barbeiro_id, data, slot_inicio, cliente_id, status FROM agendamentos WHERE id = ?");
        int achou = 0, ini = 0;
        char dold[16] = "";
        sqlite3_bind_int(st, 1, remarcar);
        if (sqlite3_step(st) == SQLITE_ROW) {
            if (!strcmp(col_txt(st, 5), "agendado") && (staff || sqlite3_column_int(st, 4) == s->uid)) {
                achou = 1;
                if (servico == 0) servico = sqlite3_column_int(st, 0);
                if (barbeiro == 0) barbeiro = sqlite3_column_int(st, 1);
                snprintf(dold, sizeof dold, "%s", col_txt(st, 2));
                ini = sqlite3_column_int(st, 3);
            }
        }
        sqlite3_finalize(st);
        if (!achou) {
            pagina_msg(rs, rq, s, 404, "Agendamento não encontrado", "Não achamos esse agendamento para remarcar.",
                       staff ? "/recepcao" : "/cliente", "Voltar");
            return;
        }
        if (!staff && !antecedencia_ok(dold, ini)) {
            pagina_msg(rs, rq, s, 403, "Prazo encerrado", "Já passou do prazo para remarcar pelo site. Ligue para a barbearia.",
                       "/cliente", "Voltar");
            return;
        }
    }

    pagina_inicio(rs, rq, staff ? "Novo agendamento" : "Agendar horário", s);
    buf_add(b, "<h1>%s</h1>", remarcar > 0 ? "Remarcar horário" : (staff ? "Novo agendamento" : "Agendar horário"));
    if (remarcar > 0) buf_add(b, "<p>Escolha o novo horário. O horário antigo só é cancelado quando você confirmar o novo.</p>");
    form_escolha(b, self, servico, barbeiro, data, remarcar, 1);

    if (servico > 0 && barbeiro > 0 && data[0]) {
        int slots = 0, preco = 0, livres[48], c;
        char nome_serv[100], nome_barb[110], pf[24];
        if (!obter_servico(servico, nome_serv, sizeof nome_serv, &slots, &preco)) {
            buf_add(b, "<p class='aviso erro' role='alert'>Serviço não encontrado.</p>");
        } else if (!validar_data_agenda(data, erro, sizeof erro)) {
            buf_add(b, "<p class='aviso erro' role='alert'>");
            buf_esc(b, erro);
            buf_add(b, "</p>");
        } else if (!barbeiro_ativo(barbeiro)) {
            buf_add(b, "<p class='aviso erro' role='alert'>Barbeiro não encontrado.</p>");
        } else {
            nome_usuario(barbeiro, nome_barb, sizeof nome_barb);
            data_br(data, dbr);
            preco_fmt(preco, pf, sizeof pf);
            c = listar_livres(barbeiro, data, slots, livres);
            buf_add(b, "<h2>Horários livres em %s</h2><p>", dbr);
            buf_esc(b, nome_serv);
            buf_add(b, " (%s, %d min) com ", pf, slots * 30);
            buf_esc(b, nome_barb);
            buf_add(b, ".</p>");
            if (c == 0) {
                buf_add(b, "<p class='aviso erro'>Não há horários livres nesse dia para esse serviço. Tente outra data ou outro barbeiro.</p>");
            } else {
                buf_add(b, "<form method='post' action='%s'>", acao);
                buf_add(b, "<input type='hidden' name='servico' value='%d'><input type='hidden' name='barbeiro' value='%d'>"
                           "<input type='hidden' name='data' value='%s'>", servico, barbeiro, data);
                if (remarcar > 0) buf_add(b, "<input type='hidden' name='remarcar' value='%d'>", remarcar);
                if (staff && remarcar == 0) {
                    buf_add(b, "<label for='nav'>Nome do cliente</label><input id='nav' name='nome_av' type='text' required maxlength='100'>"
                               "<label for='tav'>Telefone do cliente</label><input id='tav' name='tel_av' type='tel' maxlength='20'>");
                }
                buf_add(b, "<fieldset style='border:0;padding:0;margin:12px 0'><legend><b>Escolha o horário</b></legend><div class='chips'>");
                for (int i = 0; i < c; i++) {
                    slot_texto(livres[i], hi);
                    slot_texto(livres[i] + slots, hf);
                    buf_add(b, "<label class='chip'><input type='radio' name='slot' value='%d' required>%s às %s</label>", livres[i], hi, hf);
                }
                buf_add(b, "</div></fieldset><p><button type='submit'>Confirmar agendamento</button></p></form>");
            }
        }
    }
    pagina_fim(rs);
}

/* Confirma: grava o agendamento (e, se for remarcação, cancela o antigo na mesma operação) */
static void confirmar_agendamento(const Req *rq, Resp *rs, const Sessao *s, int staff) {
    int servico = param_int(rq->corpo, "servico", 0), barbeiro = param_int(rq->corpo, "barbeiro", 0);
    int slot = param_int(rq->corpo, "slot", -1), remarcar = param_int(rq->corpo, "remarcar", 0);
    char data[16] = "", erro[200] = "", nome_av[110] = "", tel_av[40] = "";
    int cliente_id = staff ? 0 : s->uid, novo = 0, ok;
    char voltar[200];
    Buf *b = &rs->corpo;
    param(rq->corpo, "data", data, sizeof data);
    param(rq->corpo, "nome_av", nome_av, sizeof nome_av);
    param(rq->corpo, "tel_av", tel_av, sizeof tel_av);
    aparar(nome_av);
    aparar(tel_av);

    snprintf(voltar, sizeof voltar, "%s?servico=%d&barbeiro=%d&data=%s%s%d", staff ? "/recepcao/novo" : "/agendar",
             servico, barbeiro, data, remarcar > 0 ? "&remarcar=" : "&x=", remarcar > 0 ? remarcar : 0);

    if (staff && remarcar == 0 && strlen(nome_av) < 2) {
        redirecionar_msg(rs, voltar, 0, "Informe o nome do cliente."); return;
    }
    if (slot < 0) { redirecionar_msg(rs, voltar, 0, "Escolha um horário."); return; }

    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    if (remarcar > 0) {
        sqlite3_stmt *st = db_prep("SELECT cliente_id, nome_avulso, tel_avulso, status, data, slot_inicio FROM agendamentos WHERE id = ?");
        int achou = 0, ini = 0;
        char dold[16] = "";
        sqlite3_bind_int(st, 1, remarcar);
        if (sqlite3_step(st) == SQLITE_ROW && !strcmp(col_txt(st, 3), "agendado")
            && (staff || sqlite3_column_int(st, 0) == s->uid)) {
            achou = 1;
            if (staff) {     /* mantém os dados de quem tinha marcado */
                cliente_id = sqlite3_column_int(st, 0);
                snprintf(nome_av, sizeof nome_av, "%s", col_txt(st, 1));
                snprintf(tel_av, sizeof tel_av, "%s", col_txt(st, 2));
            }
            snprintf(dold, sizeof dold, "%s", col_txt(st, 4));
            ini = sqlite3_column_int(st, 5);
        }
        sqlite3_finalize(st);
        if (!achou) {
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            redirecionar_msg(rs, staff ? "/recepcao" : "/cliente", 0, "Agendamento original não encontrado.");
            return;
        }
        if (!staff && !antecedencia_ok(dold, ini)) {
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            redirecionar_msg(rs, "/cliente", 0, "Já passou do prazo para remarcar pelo site. Ligue para a barbearia.");
            return;
        }
        st = db_prep("UPDATE agendamentos SET status = 'cancelado' WHERE id = ?");
        sqlite3_bind_int(st, 1, remarcar);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    ok = criar_agendamento(cliente_id, nome_av, tel_av, barbeiro, servico, data, slot, &novo, erro, sizeof erro);
    if (!ok) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);     /* desfaz também o cancelamento do antigo */
        redirecionar_msg(rs, voltar, 0, erro);
        return;
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

    /* Tela de confirmação (RF27) com todos os dados */
    {
        char nome_serv[100], nome_barb[110], pf[24], hi[8], hf[8], dbr[16];
        int slots = 0, preco = 0;
        obter_servico(servico, nome_serv, sizeof nome_serv, &slots, &preco);
        nome_usuario(barbeiro, nome_barb, sizeof nome_barb);
        preco_fmt(preco, pf, sizeof pf);
        slot_texto(slot, hi);
        slot_texto(slot + slots, hf);
        data_br(data, dbr);
        pagina_inicio(rs, rq, "Agendamento confirmado", s);
        buf_add(b, "<h1>Agendamento confirmado!</h1><div class='card'><p><b>Serviço:</b> ");
        buf_esc(b, nome_serv);
        buf_add(b, " (%s)<br><b>Barbeiro:</b> ", pf);
        buf_esc(b, nome_barb);
        buf_add(b, "<br><b>Data:</b> %s<br><b>Horário:</b> %s às %s<br><b>Cliente:</b> ", dbr, hi, hf);
        if (staff) buf_esc(b, nome_av); else buf_esc(b, s->nome);
        buf_add(b, "<br><b>Número do agendamento:</b> %d</p></div>", novo);
        if (staff) buf_add(b, "<p><a class='botao' href='/recepcao?data=%s'>Ver agenda do dia</a> "
                              "<a class='botao sec' href='/recepcao/novo'>Novo agendamento</a></p>", data);
        else buf_add(b, "<p><a class='botao' href='/cliente'>Meus agendamentos</a></p>");
        pagina_fim(rs);
    }
}

void h_agendar_get(const Req *rq, Resp *rs) {
    const Sessao *s = sessao_atual(rq);
    if (!s) {       /* quem ainda não tem conta é levado ao cadastro e volta para cá depois */
        char alvo[700], enc[1500], base[1600];
        snprintf(alvo, sizeof alvo, "/agendar%s%.600s", rq->query[0] ? "?" : "", rq->query);
        url_encode(alvo, enc, sizeof enc);
        snprintf(base, sizeof base, "/cadastro?proximo=%s", enc);
        redirecionar_msg(rs, base, 0, "Para agendar, crie sua conta (ou entre, se já tiver).");
        return;
    }
    if (strcmp(s->perfil, "cliente") != 0) {
        pagina_msg(rs, rq, s, 403, "Agendamento de clientes",
                   "Esta tela é para clientes. Funcionários fazem agendamentos pela Recepção.",
                   area_do_perfil(s->perfil), "Ir para minha área");
        return;
    }
    pagina_agendar(rq, rs, s, 0);
}

void h_agendar_post(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "cliente");
    if (s) confirmar_agendamento(rq, rs, s, 0);
}

void h_recepcao_novo(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    if (s) pagina_agendar(rq, rs, s, 1);
}

void h_recepcao_confirmar(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    if (s) confirmar_agendamento(rq, rs, s, 1);
}

/* ============================================================
   AGENDA DO DIA (barbeiro vê a dele; recepção e dono veem a de todos)
   ============================================================ */

static void tabela_agenda(Buf *b, const char *data, int barbeiro_filtro, int staff, const char *acao_url) {
    sqlite3_stmt *st = db_prep(
        "SELECT a.id, a.slot_inicio, a.slots, a.status, ub.nome, COALESCE(c.nome, a.nome_avulso, ''), "
        "COALESCE(c.telefone, a.tel_avulso, ''), COALESCE(sv.nome, '-') "
        "FROM agendamentos a JOIN usuarios ub ON ub.id = a.barbeiro_id "
        "LEFT JOIN usuarios c ON c.id = a.cliente_id LEFT JOIN servicos sv ON sv.id = a.servico_id "
        "WHERE a.data = ? AND a.status <> 'cancelado' AND (? = 0 OR a.barbeiro_id = ?) "
        "ORDER BY a.slot_inicio, ub.nome");
    char hi[8], hf[8];
    int tem = 0;
    BT(st, 1, data);
    sqlite3_bind_int(st, 2, barbeiro_filtro);
    sqlite3_bind_int(st, 3, barbeiro_filtro);

    buf_add(b, "<div class='rolagem'><table><caption class='peq'>Agenda do dia</caption><tr><th>Horário</th>");
    if (staff) buf_add(b, "<th>Barbeiro</th>");
    buf_add(b, "<th>Cliente</th><th>Telefone</th><th>Serviço</th><th>Situação</th><th>Ações</th></tr>");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0), ini = sqlite3_column_int(st, 1), n = sqlite3_column_int(st, 2);
        const char *status = col_txt(st, 3);
        tem = 1;
        slot_texto(ini, hi);
        slot_texto(ini + n, hf);
        buf_add(b, "<tr><td>%s às %s</td>", hi, hf);
        if (staff) { buf_add(b, "<td>"); buf_esc(b, col_txt(st, 4)); buf_add(b, "</td>"); }
        buf_add(b, "<td>");
        buf_esc(b, col_txt(st, 5));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 6));
        buf_add(b, "</td><td>");
        buf_esc(b, col_txt(st, 7));
        buf_add(b, "</td><td><span class='status %s'>%s</span></td><td>", status, rotulo_status(status));

        if (!strcmp(status, "agendado")) {
            static const char *acoes[3][2] = { { "concluir", "Concluído" }, { "faltou", "Faltou" }, { "cancelar", "Cancelar" } };
            for (int k = 0; k < 3; k++)
                buf_add(b, "<form method='post' action='%s'><input type='hidden' name='id' value='%d'>"
                           "<input type='hidden' name='acao' value='%s'><input type='hidden' name='data' value='%s'>"
                           "<input type='hidden' name='barbeiro' value='%d'>"
                           "<button class='mini%s' type='submit'>%s</button></form>",
                        acao_url, id, acoes[k][0], data, barbeiro_filtro, k == 2 ? " perigo" : "", acoes[k][1]);
            if (staff)
                buf_add(b, "<a class='botao sec mini' style='min-height:34px;padding:4px 10px' href='/recepcao/novo?remarcar=%d'>Remarcar</a>", id);
        } else if (!strcmp(status, "bloqueado")) {
            buf_add(b, "<form method='post' action='%s'><input type='hidden' name='id' value='%d'>"
                       "<input type='hidden' name='acao' value='liberar'><input type='hidden' name='data' value='%s'>"
                       "<input type='hidden' name='barbeiro' value='%d'><button class='mini' type='submit'>Liberar</button></form>",
                    acao_url, id, data, barbeiro_filtro);
        } else {
            buf_add(b, "-");
        }
        buf_add(b, "</td></tr>");
    }
    sqlite3_finalize(st);
    if (!tem) buf_add(b, "<tr><td colspan='%d'>Nenhum atendimento ou bloqueio nesse dia.</td></tr>", staff ? 7 : 6);
    buf_add(b, "</table></div>");
}

/* Muda a situação de um agendamento. O barbeiro só mexe nos dele; recepção e dono mexem em todos. */
static void acao_status(const Req *rq, Resp *rs, const Sessao *s, int staff) {
    int id = param_int(rq->corpo, "id", 0), barbeiro_f = param_int(rq->corpo, "barbeiro", 0);
    char acao[20], data[16] = "", base[200], status_atual[20] = "";
    int dono_ag = 0, achou = 0;
    sqlite3_stmt *st;
    const char *novo = NULL;

    param(rq->corpo, "acao", acao, sizeof acao);
    param(rq->corpo, "data", data, sizeof data);
    if (!data_valida(data)) hoje(data);
    if (staff) snprintf(base, sizeof base, "/recepcao?data=%s&barbeiro=%d", data, barbeiro_f);
    else snprintf(base, sizeof base, "/barbeiro?data=%s", data);

    st = db_prep("SELECT barbeiro_id, status FROM agendamentos WHERE id = ?");
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        achou = 1;
        dono_ag = sqlite3_column_int(st, 0);
        snprintf(status_atual, sizeof status_atual, "%s", col_txt(st, 1));
    }
    sqlite3_finalize(st);

    if (!achou || (!staff && dono_ag != s->uid)) {
        redirecionar_msg(rs, base, 0, "Agendamento não encontrado.");
        return;
    }
    if (!strcmp(acao, "liberar")) {
        if (strcmp(status_atual, "bloqueado") != 0) { redirecionar_msg(rs, base, 0, "Esse horário não está bloqueado."); return; }
        st = db_prep("DELETE FROM agendamentos WHERE id = ? AND status = 'bloqueado'");
        sqlite3_bind_int(st, 1, id);
        sqlite3_step(st);
        sqlite3_finalize(st);
        redirecionar_msg(rs, base, 1, "Horário liberado.");
        return;
    }
    if (!strcmp(acao, "concluir")) novo = "concluido";
    else if (!strcmp(acao, "faltou")) novo = "faltou";
    else if (!strcmp(acao, "cancelar")) novo = "cancelado";
    if (!novo || strcmp(status_atual, "agendado") != 0) {
        redirecionar_msg(rs, base, 0, "Essa ação não pode ser feita agora.");
        return;
    }
    st = db_prep("UPDATE agendamentos SET status = ? WHERE id = ?");
    BT(st, 1, novo);
    sqlite3_bind_int(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    redirecionar_msg(rs, base, 1, "Situação atualizada.");
}

void h_barbeiro_status(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "barbeiro");
    if (s) acao_status(rq, rs, s, 0);
}

void h_recepcao_status(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    if (s) acao_status(rq, rs, s, 1);
}

void h_barbeiro(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "barbeiro");
    Buf *b = &rs->corpo;
    char data[16] = "", h[11], dia[11], dbr[16], hi[8];
    int ini, fim;
    if (!s) return;

    param(rq->query, "data", data, sizeof data);
    hoje(h);
    if (!data_valida(data)) snprintf(data, sizeof data, "%s", h);
    faixa(&ini, &fim);

    pagina_inicio(rs, rq, "Minha agenda", s);
    data_br(data, dbr);
    buf_add(b, "<h1>Minha agenda</h1><h2>Próximos 7 dias</h2><div class='chips'>");
    for (int i = 0; i < 7; i++) {
        sqlite3_stmt *st = db_prep("SELECT COUNT(*) FROM agendamentos WHERE barbeiro_id = ? AND data = ? "
                                   "AND status IN ('agendado','concluido','faltou')");
        char dd[16];
        int n = 0;
        data_mais_dias(h, i, dia);
        sqlite3_bind_int(st, 1, s->uid);
        BT(st, 2, dia);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        data_br(dia, dd);
        dd[5] = '\0';        /* só DD/MM */
        buf_add(b, "<a class='chip%s' href='/barbeiro?data=%s'>%s (%d)</a>", strcmp(dia, data) == 0 ? " ativo" : "", dia, dd, n);
    }
    buf_add(b, "</div><p class='peq'>O número entre parênteses é a quantidade de atendimentos no dia.</p>"
               "<form method='get' action='/barbeiro' class='linha'><label for='d'>Ir para outra data"
               "<input id='d' type='date' name='data' value='%s'></label><button class='sec' type='submit'>Ver</button></form>", data);

    buf_add(b, "<h2>Agenda de %s</h2>", dbr);
    tabela_agenda(b, data, s->uid, 0, "/barbeiro/status");

    buf_add(b, "<h2>Bloquear horários (folga, almoço)</h2>"
               "<form method='post' action='/barbeiro/bloquear' class='linha'>"
               "<label for='bd'>Data<input id='bd' type='date' name='data' required min='%s' value='%s'></label>"
               "<label for='de'>De<select id='de' name='de'>", h, data);
    for (int sl = ini; sl < fim; sl++) { slot_texto(sl, hi); buf_add(b, "<option value='%d'>%s</option>", sl, hi); }
    buf_add(b, "</select></label><label for='ate'>Até<select id='ate' name='ate'>");
    for (int sl = ini + 1; sl <= fim; sl++) { slot_texto(sl, hi); buf_add(b, "<option value='%d'%s>%s</option>", sl, sl == ini + 2 ? " selected" : "", hi); }
    buf_add(b, "</select></label><button type='submit'>Bloquear</button></form>");
    pagina_fim(rs);
}

void h_barbeiro_bloquear(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "barbeiro");
    char data[16] = "", base[100], h[11];
    int de, ate, ini, fim;
    sqlite3_stmt *st;
    if (!s) return;

    param(rq->corpo, "data", data, sizeof data);
    de = param_int(rq->corpo, "de", -1);
    ate = param_int(rq->corpo, "ate", -1);
    faixa(&ini, &fim);
    hoje(h);
    if (!data_valida(data)) hoje(data);
    snprintf(base, sizeof base, "/barbeiro?data=%s", data);

    if (strcmp(data, h) < 0) { redirecionar_msg(rs, base, 0, "Escolha uma data de hoje em diante."); return; }
    if (de < ini || ate > fim || de >= ate) { redirecionar_msg(rs, base, 0, "Horário inválido: o fim precisa ser depois do início."); return; }
    if (ocupado(s->uid, data, de, ate - de)) {
        redirecionar_msg(rs, base, 0, "Já existe um atendimento ou bloqueio nesse intervalo. Cancele ou libere antes.");
        return;
    }
    st = db_prep("INSERT INTO agendamentos (barbeiro_id, data, slot_inicio, slots, status) VALUES (?, ?, ?, ?, 'bloqueado')");
    sqlite3_bind_int(st, 1, s->uid);
    BT(st, 2, data);
    sqlite3_bind_int(st, 3, de);
    sqlite3_bind_int(st, 4, ate - de);
    sqlite3_step(st);
    sqlite3_finalize(st);
    redirecionar_msg(rs, base, 1, "Horário bloqueado. Ele não aparece mais para os clientes.");
}

void h_recepcao(const Req *rq, Resp *rs) {
    const Sessao *s = exigir(rq, rs, "recepcionista,dono");
    Buf *b = &rs->corpo;
    char data[16] = "", h[11], dbr[16];
    int barbeiro = param_int(rq->query, "barbeiro", 0);
    sqlite3_stmt *st;
    if (!s) return;

    param(rq->query, "data", data, sizeof data);
    hoje(h);
    if (!data_valida(data)) snprintf(data, sizeof data, "%s", h);
    data_br(data, dbr);

    pagina_inicio(rs, rq, "Agenda geral", s);
    buf_add(b, "<h1>Agenda geral</h1><p><a class='botao' href='/recepcao/novo'>Novo agendamento</a> "
               "<a class='botao sec' href='/recepcao/clientes'>Clientes</a>");
    if (!strcmp(s->perfil, "dono")) buf_add(b, " <a class='botao sec' href='/dono'>Painel do dono</a>");
    buf_add(b, "</p><form method='get' action='/recepcao' class='linha'>"
               "<label for='d'>Data<input id='d' type='date' name='data' value='%s'></label>"
               "<label for='b'>Barbeiro<select id='b' name='barbeiro'><option value='0'>Todos</option>", data);
    st = db_prep("SELECT id, nome FROM usuarios WHERE perfil = 'barbeiro' AND ativo = 1 ORDER BY nome");
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        buf_add(b, "<option value='%d'%s>", id, id == barbeiro ? " selected" : "");
        buf_esc(b, col_txt(st, 1));
        buf_add(b, "</option>");
    }
    sqlite3_finalize(st);
    buf_add(b, "</select></label><button type='submit'>Ver agenda</button></form><h2>Dia %s</h2>", dbr);
    tabela_agenda(b, data, barbeiro, 1, "/recepcao/status");
    pagina_fim(rs);
}
