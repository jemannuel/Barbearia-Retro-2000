-- ============================================================
-- Barbearia Retrô 2000 - Dados de exemplo (só para demonstração)
-- Cria 4 clientes de teste e cerca de 3 semanas de agendamentos,
-- para os relatórios do dono não aparecerem vazios.
-- Todos os clientes de exemplo têm a senha: cliente123
-- Para carregar: barbearia.exe 8080 exemplo   (ou ./barbearia 8080 exemplo)
-- Só carrega uma vez: se já foi carregado, o programa ignora.
-- ============================================================

INSERT OR IGNORE INTO usuarios (nome, login, telefone, salt, senha_hash, perfil) VALUES
 ('Cliente Exemplo', 'cliente@exemplo.com', '(11) 91111-1111', 'd3m0d3m0d3m0d3m0d3m0d3m0d3m0d3m0', '9ad2034c718ee48f9f1b10d3ee8a1e886cbf3189f2646e13b63af41849f9a851', 'cliente'),
 ('Pedro Alves',     'pedro@exemplo.com',   '(11) 92222-2222', 'd3m0d3m0d3m0d3m0d3m0d3m0d3m0d3m0', '9ad2034c718ee48f9f1b10d3ee8a1e886cbf3189f2646e13b63af41849f9a851', 'cliente'),
 ('Lucas Mota',      'lucas@exemplo.com',   '(11) 93333-3333', 'd3m0d3m0d3m0d3m0d3m0d3m0d3m0d3m0', '9ad2034c718ee48f9f1b10d3ee8a1e886cbf3189f2646e13b63af41849f9a851', 'cliente'),
 ('Marcos Dias',     'marcos@exemplo.com',  '(11) 94444-4444', 'd3m0d3m0d3m0d3m0d3m0d3m0d3m0d3m0', '9ad2034c718ee48f9f1b10d3ee8a1e886cbf3189f2646e13b63af41849f9a851', 'cliente');

-- Atendimentos dos últimos 20 dias (só de terça a sábado, como a barbearia funciona).
-- A situação varia: a maioria concluída, alguns faltaram, alguns cancelaram.
WITH RECURSIVE dias(n) AS (SELECT -20 UNION ALL SELECT n + 1 FROM dias WHERE n < -1),
barb AS (SELECT id, ROW_NUMBER() OVER (ORDER BY login) AS k FROM usuarios WHERE perfil = 'barbeiro'),
cli  AS (SELECT id, ROW_NUMBER() OVER (ORDER BY login) AS k FROM usuarios WHERE login LIKE '%@exemplo.com'),
pad(p, slot, servico_nome, slots) AS (VALUES (0, 18, 'Corte de cabelo', 1), (1, 20, 'Corte + Barba', 2),
                                             (2, 24, 'Barba', 1), (3, 30, 'Sobrancelha', 1))
INSERT INTO agendamentos (cliente_id, barbeiro_id, servico_id, data, slot_inicio, slots, status)
SELECT (SELECT id FROM cli WHERE k = 1 + ((-d.n + b.k + pad.p) % 4)),
       b.id,
       (SELECT id FROM servicos WHERE nome = pad.servico_nome),
       date('now', 'localtime', d.n || ' day'),
       pad.slot, pad.slots,
       CASE (-d.n + b.k + pad.p) % 9 WHEN 0 THEN 'faltou' WHEN 4 THEN 'cancelado' ELSE 'concluido' END
FROM dias d, barb b, pad
WHERE strftime('%w', date('now', 'localtime', d.n || ' day')) IN ('2', '3', '4', '5', '6')
  AND (SELECT valor FROM config WHERE chave = 'exemplo_carregado') IS NULL;

-- Alguns agendamentos futuros (próximos 3 dias).
WITH RECURSIVE dias(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM dias WHERE n < 3),
barb AS (SELECT id, ROW_NUMBER() OVER (ORDER BY login) AS k FROM usuarios WHERE perfil = 'barbeiro'),
cli  AS (SELECT id, ROW_NUMBER() OVER (ORDER BY login) AS k FROM usuarios WHERE login LIKE '%@exemplo.com'),
pad(p, slot, servico_nome, slots) AS (VALUES (0, 22, 'Corte de cabelo', 1), (1, 26, 'Corte + Barba', 2))
INSERT INTO agendamentos (cliente_id, barbeiro_id, servico_id, data, slot_inicio, slots, status)
SELECT (SELECT id FROM cli WHERE k = 1 + ((d.n + b.k + pad.p) % 4)),
       b.id,
       (SELECT id FROM servicos WHERE nome = pad.servico_nome),
       date('now', 'localtime', d.n || ' day'),
       pad.slot, pad.slots, 'agendado'
FROM dias d, barb b, pad
WHERE strftime('%w', date('now', 'localtime', d.n || ' day')) IN ('2', '3', '4', '5', '6')
  AND (SELECT valor FROM config WHERE chave = 'exemplo_carregado') IS NULL;

INSERT OR IGNORE INTO config (chave, valor) VALUES ('exemplo_carregado', '1');
