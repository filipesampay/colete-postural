# Instruções para agentes de IA

Firmware ESP32 (PlatformIO + Arduino) e app web embarcado do colete postural (TCC). Visão geral e comandos:
`README.md`. Documentação completa: `Documentação/README.md`.

## Antes de mudar código

- Leia `Documentação/MANUTENCAO.md`: mapa arquivo de código → documentos, convenções de comentário e checklist.
- **Mudou o comportamento, mudou a documentação no mesmo commit**: comentários do arquivo, página de
  `Documentação/referencia/<modulo>.md` e os capítulos indicados no mapa (inclusive os diagramas Mermaid).
- A documentação se edita nos `.md`; os PDFs em `Documentação/pdf/` são gerados. Depois de mudar um `.md`, rode
  `python3 Documentação/gerar_pdf.py` e commite `Documentação/pdf/` junto. Nunca edite os PDFs.
- Decisões já tomadas e seus motivos: `Documentação/10-decisoes-de-projeto.md`. Não reverta sem motivo forte;
  ao mudar uma, registre a nova decisão lá.

## Regras do código que não podem quebrar

- A tarefa de controle (`tarefaControle` em `src/main.cpp`) **nunca** chama `Serial` nem `web_*`: use `emitir()`.
- Dados compartilhados entre as tarefas só com `TRAVAR()`/`DESTRAVAR()`, e dentro da trava só cópias curtas.
- `src/tilt.*`, `src/posture.*` e `src/filters.h` não podem depender do Arduino (rodam nos testes do PC).
- `src/config.h`: o `VALIDACAO.md` cita números de linha deste arquivo; não insira nem remova linhas antes da seção do Wi-Fi.
- `src/web/index.html` vai inteiro para a flash: comentários curtos, nunca `</script>` em comentário JS.
- Comentários em português, estilo `//`, explicando o porquê (ver `Documentação/MANUTENCAO.md`, seção 3).

## Verificar

```bash
pio test -e native     # 21 testes da lógica pura
tools/sim.sh           # simulador do firmware (conferir a saída dos roteiros)
pio run                # compila para o ESP32
python3 Documentação/gerar_pdf.py   # PDFs da documentação (depois de mudar algum .md)
```
