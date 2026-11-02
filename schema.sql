-- ============================================================
-- Barbearia Retrô 2000 - Modelo físico (SQLite)
-- Este arquivo é executado toda vez que o servidor inicia.
-- Como usa "IF NOT EXISTS", ele não apaga dados já cadastrados.
-- ============================================================

-- Tabela de usuários: clientes, barbeiros, recepcionista e dono.
-- "login" guarda o e-mail do cliente ou o nome de usuário do funcionário.
CREATE TABLE IF NOT EXISTS usuarios (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    nome        TEXT    NOT NULL,
    login       TEXT    NOT NULL UNIQUE,
    telefone    TEXT,
    salt        TEXT    NOT NULL,
    senha_hash  TEXT    NOT NULL,
    perfil      TEXT    NOT NULL
                CHECK (perfil IN ('cliente', 'barbeiro', 'recepcionista', 'dono')),
    ativo       INTEGER NOT NULL DEFAULT 1,
    criado_em   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Serviços oferecidos. O preço fica em centavos para evitar erro de arredondamento.
CREATE TABLE IF NOT EXISTS servicos (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    nome            TEXT    NOT NULL,
    preco_centavos  INTEGER NOT NULL CHECK (preco_centavos >= 0),
    duracao_min     INTEGER NOT NULL CHECK (duracao_min > 0),
    ativo           INTEGER NOT NULL DEFAULT 1
);

-- Agendamentos. O dia é dividido em blocos de 30 minutos (slot 0 = 00:00, slot 18 = 09:00).
-- Um agendamento ocupa "slots" blocos seguidos a partir de "slot_inicio".
-- Bloqueios de horário (folga, almoço) usam status 'bloqueado' e não têm cliente nem serviço.
-- Quando a recepcionista agenda por telefone, o cliente pode não ter conta:
-- nesse caso usa nome_avulso e tel_avulso.
CREATE TABLE IF NOT EXISTS agendamentos (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    cliente_id   INTEGER REFERENCES usuarios(id),
    nome_avulso  TEXT,
    tel_avulso   TEXT,
    barbeiro_id  INTEGER NOT NULL REFERENCES usuarios(id),
    servico_id   INTEGER REFERENCES servicos(id),
    data         TEXT    NOT NULL,                      -- AAAA-MM-DD
    slot_inicio  INTEGER NOT NULL CHECK (slot_inicio BETWEEN 0 AND 47),
    slots        INTEGER NOT NULL CHECK (slots >= 1),
    status       TEXT    NOT NULL DEFAULT 'agendado'
                 CHECK (status IN ('agendado', 'concluido', 'cancelado', 'faltou', 'bloqueado')),
    criado_em    TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE INDEX IF NOT EXISTS idx_agend_barbeiro_data ON agendamentos (barbeiro_id, data);
CREATE INDEX IF NOT EXISTS idx_agend_cliente       ON agendamentos (cliente_id);

-- Configurações da barbearia (endereço, horários etc.), editáveis pelo dono.
CREATE TABLE IF NOT EXISTS config (
    chave  TEXT PRIMARY KEY,
    valor  TEXT NOT NULL
);

INSERT OR IGNORE INTO config (chave, valor) VALUES ('endereco', 'Rua Exemplo, 123 - Sacomã, São Paulo - SP (endereço fictício)');
INSERT OR IGNORE INTO config (chave, valor) VALUES ('telefone', '(11) 90000-0000');
INSERT OR IGNORE INTO config (chave, valor) VALUES ('abertura', '09:00');
INSERT OR IGNORE INTO config (chave, valor) VALUES ('fechamento', '19:00');
-- Dias abertos: 0=domingo, 1=segunda, ... 6=sábado (aqui: terça a sábado)
INSERT OR IGNORE INTO config (chave, valor) VALUES ('dias_abertos', '23456');
-- Antecedência mínima (em horas) para o cliente cancelar ou remarcar
INSERT OR IGNORE INTO config (chave, valor) VALUES ('antecedencia_horas', '2');
