// Gera os PDFs da documentação a partir dos .md (a fonte, editável junto com o código).
//
//   node Documentação/ferramentas/gerar_pdf.mjs              # PDF completo + um PDF por documento
//   node Documentação/ferramentas/gerar_pdf.mjs --completo   # só o PDF completo
//   node Documentação/ferramentas/gerar_pdf.mjs 03-algoritmo-inclinacao.md referencia/mpu.md   # não refaz o completo
//
// Normalmente chamado por Documentação/gerar_pdf.py, que instala as dependências antes.
//
// Saída em Documentação/pdf/:
//   00-documentacao-completa.pdf   capa, sumário com páginas e todos os documentos na ordem de ORDEM;
//                                  os links entre documentos funcionam dentro do PDF
//   <documento>.pdf                um por .md, no mesmo caminho (referencia/mpu.md -> pdf/referencia/mpu.pdf);
//                                  link para outro documento aparece como texto
//
// Como funciona: markdown-it converte cada .md em HTML; as fórmulas ($...$ e $$...$$) viram HTML do KaTeX
// já aqui; os blocos ```mermaid são desenhados dentro do Chrome pelo mermaid.js; o Chrome (via puppeteer)
// imprime em A4. O PDF completo é impresso duas vezes: a primeira só para descobrir em que página cada
// item do sumário caiu (lendo os links do sumário com o pdf.js), a segunda com os números preenchidos.
//
// Usa o Chrome/Chromium instalado; para outro: CHROME=/caminho/do/navegador.

import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import katex from 'katex';
import MarkdownIt from 'markdown-it';
import puppeteer from 'puppeteer-core';
import { getDocument } from 'pdfjs-dist/legacy/build/pdf.mjs';

const FERRAMENTAS = path.dirname(fileURLToPath(import.meta.url));
const DOC = path.resolve(FERRAMENTAS, '..');
const SAIDA = path.join(DOC, 'pdf');
const MODULOS = path.join(FERRAMENTAS, 'node_modules');

// Ordem do PDF completo e do sumário. Documento novo: acrescentar aqui (o script avisa se faltar algum).
const ORDEM = [
  'README.md',
  '01-visao-geral.md',
  '02-arquitetura.md',
  '03-algoritmo-inclinacao.md',
  '04-deteccao-e-calibracao.md',
  '05-sensores-analogicos.md',
  '06-motor-e-nvs.md',
  '07-protocolo-e-interfaces.md',
  '08-app-web.md',
  '09-simulador-testes-ferramentas.md',
  '10-decisoes-de-projeto.md',
  '11-mapa-tcc.md',
  'referencia/config.md',
  'referencia/main.md',
  'referencia/tilt.md',
  'referencia/posture.md',
  'referencia/filters.md',
  'referencia/mpu.md',
  'referencia/sensors.md',
  'referencia/motor.md',
  'referencia/settings.md',
  'referencia/console.md',
  'referencia/protocolo.md',
  'referencia/web.md',
  'MANUTENCAO.md',
];

// Documentos cujos títulos de seção (##) entram no sumário; dos outros entra só o título.
const SUMARIO_COM_SECOES = (doc) => !doc.startsWith('referencia/') && doc !== 'MANUTENCAO.md';

const TITULO = 'Colete Postural — Documentação do software';

const NAVEGADORES = [
  process.env.CHROME,
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/Applications/Chromium.app/Contents/MacOS/Chromium',
  '/usr/bin/google-chrome',
  '/usr/bin/google-chrome-stable',
  '/usr/bin/chromium',
  '/usr/bin/chromium-browser',
];

// ---------------------------------------------------------------- Markdown -> HTML

// "referencia/mpu.md" -> "referencia-mpu": prefixo dos ids, único no PDF completo.
const idDoc = (doc) => doc.replace(/\.md$/, '').replace(/\//g, '-');

// Âncora no estilo do GitHub, para os links "doc.md#secao" escritos nos .md continuarem valendo.
function slug(texto) {
  return texto.trim().toLowerCase().replace(/[^\p{L}\p{N}\p{M}\s_-]/gu, '').replace(/\s/g, '-');
}

function escapar(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function formula(tex, bloco) {
  return katex.renderToString(tex, { displayMode: bloco, throwOnError: false, output: 'html' });
}

// Fórmulas no estilo do GitHub: $...$ dentro da linha e $$...$$ sozinho na linha.
// A regra roda antes da de escape, senão o "\," do LaTeX perderia a barra.
function pluginFormulas(md) {
  md.inline.ruler.before('escape', 'formula_linha', (state, silencioso) => {
    const src = state.src;
    const ini = state.pos;
    if (src[ini] !== '$' || src[ini + 1] === '$' || src[ini + 1] === ' ') return false;
    let fim = ini + 1;
    while (fim < state.posMax && !(src[fim] === '$' && src[fim - 1] !== '\\')) fim++;
    if (fim >= state.posMax || src[fim - 1] === ' ' || fim === ini + 1) return false;
    if (!silencioso) state.push('formula_linha', 'math', 0).content = src.slice(ini + 1, fim);
    state.pos = fim + 1;
    return true;
  });
  md.block.ruler.before('fence', 'formula_bloco', (state, inicio, _fim, silencioso) => {
    const linha = state.src.slice(state.bMarks[inicio] + state.tShift[inicio], state.eMarks[inicio]).trim();
    if (!linha.startsWith('$$') || linha.length < 5 || !linha.endsWith('$$')) return false;
    if (!silencioso) state.push('formula_bloco', 'math', 0).content = linha.slice(2, -2);
    state.line = inicio + 1;
    return true;
  });
  md.renderer.rules.formula_linha = (t, i) => formula(t[i].content, false);
  md.renderer.rules.formula_bloco = (t, i) => `<div class="formula">${formula(t[i].content, true)}</div>\n`;
}

// Converte um .md em HTML. `resolverLink(href)` devolve o href final ou null (vira texto sem link).
// Devolve também a lista de títulos (nível, texto, id) para o sumário.
function converter(doc, resolverLink) {
  const md = new MarkdownIt({ html: true, typographer: false });
  pluginFormulas(md);
  const prefixo = idDoc(doc);
  const titulos = [];
  const usados = new Map();

  md.core.ruler.push('ids_e_listas', (state) => {
    const t = state.tokens;
    for (let i = 0; i < t.length; i++) {
      // Títulos: id "<doc>--<slug>", com sufixo -1, -2... quando repete (como o GitHub).
      if (t[i].type === 'heading_open') {
        const filhos = t[i + 1].children || [];
        const texto = filhos.filter((f) => f.type === 'text' || f.type === 'code_inline').map((f) => f.content).join('');
        let s = slug(texto);
        const n = usados.get(s) || 0;
        usados.set(s, n + 1);
        if (n) s += `-${n}`;
        const id = `${prefixo}--${s}`;
        t[i].attrSet('id', id);
        titulos.push({ nivel: Number(t[i].tag.slice(1)), texto, id });
      }
      // Lista de tarefas "- [ ] item": markdown-it não tem; vira uma caixinha.
      if (t[i].type === 'inline' && t[i - 2]?.type === 'list_item_open' && t[i].children?.[0]?.type === 'text') {
        const c = t[i].children[0];
        c.content = c.content.replace(/^\[ \] /, '☐ ').replace(/^\[[xX]\] /, '☑ ');
      }
    }
  });

  // Links: reescritos para dentro do PDF, para o PDF vizinho, ou removidos (arquivos fora da documentação).
  const abrirPadrao = md.renderer.rules.link_open || ((t, i, o, e, s) => s.renderToken(t, i, o));
  const fecharPadrao = md.renderer.rules.link_close || ((t, i, o, e, s) => s.renderToken(t, i, o));
  const pilha = [];
  md.renderer.rules.link_open = (t, i, o, e, s) => {
    const novo = resolverLink(t[i].attrGet('href'));
    pilha.push(novo !== null);
    if (novo === null) return '';
    t[i].attrSet('href', novo);
    return abrirPadrao(t, i, o, e, s);
  };
  md.renderer.rules.link_close = (t, i, o, e, s) => (pilha.pop() ? fecharPadrao(t, i, o, e, s) : '');

  // Âncoras manuais (<a id="d-11">) recebem o mesmo prefixo dos títulos.
  const comPrefixo = (html) => html.replace(/\bid="([^"]+)"/g, (_, id) => `id="${prefixo}--${id}"`);
  md.renderer.rules.html_inline = (t, i) => comPrefixo(t[i].content);
  md.renderer.rules.html_block = (t, i) => comPrefixo(t[i].content);

  // Código na linha: pontos de quebra depois de _ . / , : e ::, para nomes longos quebrarem nas tabelas
  // em "test_angulo_entre_ / vetores" e não no meio de uma palavra.
  md.renderer.rules.code_inline = (t, i) => `<code>${escapar(t[i].content).replace(/(::|[_./,]|:(?!:))(?=\S)/g, '$1<wbr>')}</code>`;

  // Diagramas: o texto Mermaid vai para a página e o mermaid.js desenha lá dentro.
  const cercaPadrao = md.renderer.rules.fence;
  md.renderer.rules.fence = (t, i, o, e, s) => {
    if (t[i].info.trim() === 'mermaid') return `<div class="diagrama"><pre class="mermaid">${escapar(t[i].content)}</pre></div>\n`;
    return cercaPadrao(t, i, o, e, s);
  };

  const fonte = readFileSync(path.join(DOC, doc), 'utf8');
  const corpo = md.render(fonte);
  return { corpo, titulos };
}

// Resolve um href escrito no .md `doc`. `completo`: todos os documentos estão no mesmo PDF.
function criarResolvedor(doc, completo) {
  return (href) => {
    if (!href) return null;
    if (/^[a-z][a-z0-9+.-]*:/i.test(href)) return href; // http, https, mailto
    const [caminho, ancora] = href.split('#');
    const alvo = caminho ? path.posix.normalize(path.posix.join(path.posix.dirname(doc), decodeURI(caminho))) : doc;
    if (!ORDEM.includes(alvo)) return null; // ../README.md, ../VALIDACAO.md, código: não estão no PDF
    // PDF separado: link para outro documento vira texto. O Chrome grava todo link para arquivo como
    // caminho absoluto (file:///pasta-temporária/...), que não funcionaria em outro computador.
    if (!completo && alvo !== doc) return null;
    return ancora ? `#${idDoc(alvo)}--${decodeURI(ancora)}` : `#${idDoc(alvo)}`;
  };
}

// ---------------------------------------------------------------- Página

const CSS = `
@page { size: A4; margin: 19mm 16mm 17mm 16mm; }
html { font-family: "Helvetica Neue", Helvetica, Arial, "Liberation Sans", sans-serif; font-size: 10pt;
  line-height: 1.45; color: #1d2228; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; }
h1 { font-size: 19pt; line-height: 1.2; color: #12385f; margin: 0 0 8pt; padding-bottom: 5pt; border-bottom: 2px solid #12385f; }
h2 { font-size: 13.5pt; line-height: 1.25; color: #12385f; margin: 16pt 0 5pt; break-after: avoid; }
h3 { font-size: 11.3pt; color: #1d2228; margin: 12pt 0 4pt; break-after: avoid; }
h4 { font-size: 10.3pt; margin: 10pt 0 3pt; break-after: avoid; }
p { margin: 0 0 7pt; orphans: 3; widows: 3; }
ul, ol { margin: 0 0 7pt; padding-left: 18pt; }
li { margin: 1pt 0; }
li > p { margin: 0 0 3pt; }
a { color: #1a5fa8; text-decoration: none; }
strong { color: #10161c; }
code { font-family: Menlo, "DejaVu Sans Mono", Consolas, monospace; font-size: 0.86em; background: #eef1f4;
  padding: 0 2px; border-radius: 2px; overflow-wrap: anywhere; }
pre { background: #f6f8fa; border: 1px solid #dfe3e8; border-radius: 3px; padding: 6pt 8pt; margin: 0 0 9pt;
  white-space: pre-wrap; overflow-wrap: anywhere; font-size: 8.2pt; line-height: 1.38; break-inside: avoid; }
pre code { background: none; padding: 0; font-size: inherit; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 10pt; font-size: 8.5pt; line-height: 1.35; }
thead { display: table-header-group; }
tr { break-inside: avoid; }
th, td { border: 1px solid #d0d7de; padding: 3pt 5pt; vertical-align: top; text-align: left; }
td code, th code { overflow-wrap: break-word; }
th { background: #e9eef4; color: #12385f; }
tbody tr:nth-child(even) td { background: #f8fafc; }
blockquote { margin: 0 0 10pt; padding: 4pt 8pt; border-left: 3px solid #8fa9c4; background: #f3f6f9;
  color: #46525e; font-size: 8.6pt; }
blockquote p { margin: 0; }
hr { border: 0; border-top: 1px solid #d0d7de; margin: 12pt 0; }
.diagrama { break-inside: avoid; margin: 6pt 0 12pt; text-align: center; }
.diagrama.paisagem { page: paisagem; margin: 0; }
@page paisagem { size: A4 landscape; margin: 15mm 16mm 17mm 16mm; }
.diagrama svg { height: auto; }
.diagrama pre.mermaid { text-align: left; }
.formula { margin: 4pt 0 9pt; break-inside: avoid; }
.katex { font-size: 1.08em; }
.katex-display { margin: 0; }
section.doc { break-before: page; }
section.doc:first-of-type { break-before: auto; }
.capa + .sumario, .sumario + section.doc { break-before: page; }

.capa { height: 255mm; display: flex; flex-direction: column; }
.capa .topo { font-size: 10pt; letter-spacing: 0.12em; text-transform: uppercase; color: #5d6b78; margin-top: 30mm; }
.capa .titulo { font-size: 25pt; line-height: 1.2; font-weight: bold; color: #12385f; margin: 10mm 0 6mm; }
.capa .subtitulo { font-size: 16pt; color: #1d2228; border-top: 2px solid #12385f; padding-top: 5mm; }
.capa .descricao { font-size: 10.5pt; color: #46525e; margin-top: 4mm; }
.capa .rodape { margin-top: auto; font-size: 8.6pt; color: #5d6b78; line-height: 1.5; }

.sumario h1 { margin-bottom: 10pt; }
.sumario ol { list-style: none; padding: 0; margin: 0; }
.sumario li { margin: 0; break-inside: avoid; }
.sumario a { display: flex; align-items: baseline; color: #1d2228; }
.sumario .t { flex: 0 1 auto; }
.sumario .pontos { flex: 1 1 auto; border-bottom: 1px dotted #9aa5b1; margin: 0 4pt; min-width: 10pt; }
.sumario .p { flex: 0 0 auto; min-width: 2.4em; text-align: right; font-variant-numeric: tabular-nums; }
.sumario .n1 { font-weight: bold; font-size: 10.2pt; margin-top: 6pt; color: #12385f; }
.sumario .n1 a { color: #12385f; }
.sumario .n2 { font-size: 9pt; padding-left: 14pt; line-height: 1.5; }
`;

const RODAPE = `<div style="font-size:7.5pt;color:#7a8794;width:100%;padding:0 16mm;display:flex;justify-content:space-between;font-family:Helvetica,Arial,sans-serif">
<span>${escapar(TITULO)}</span><span><span class="pageNumber"></span> / <span class="totalPages"></span></span></div>`;

function pagina(titulo, conteudo) {
  const css = pathToFileURL(path.join(MODULOS, 'katex/dist/katex.min.css')).href;
  const mermaid = pathToFileURL(path.join(MODULOS, 'mermaid/dist/mermaid.min.js')).href;
  const diagramas = pathToFileURL(path.join(FERRAMENTAS, 'diagramas.js')).href;
  return `<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><title>${escapar(titulo)}</title>
<link rel="stylesheet" href="${css}">
<style>${CSS}</style>
<script src="${mermaid}"></script>
</head><body>
${conteudo}
<script src="${diagramas}"></script>
</body></html>`;
}

function capa() {
  const hoje = new Date().toLocaleDateString('pt-BR', { day: '2-digit', month: '2-digit', year: 'numeric' });
  return `<section class="capa">
<div class="topo">Trabalho de Conclusão de Curso</div>
<div class="titulo">Colete Ortopédico Inteligente para Monitoramento Postural em Tempo Real</div>
<div class="subtitulo">Documentação do software</div>
<div class="descricao">Firmware do ESP32, app web embarcado, simulador e testes do colete postural (BioSpine)</div>
<div class="rodape">Gerado em ${hoje}.<br>
Fonte: os arquivos <code>.md</code> da pasta <code>Documentação/</code> do repositório; este PDF é gerado a partir
deles por <code>python3 Documentação/gerar_pdf.py</code>. Para corrigir algo, edite o <code>.md</code> e gere de novo.</div>
</section>`;
}

function sumario(itens) {
  const linhas = itens.map((it, i) => `<li class="n${it.nivel}"><a href="#${it.id}"><span class="t">${escapar(it.texto)}</span>`
    + `<span class="pontos"></span><span class="p" data-i="${i}">000</span></a></li>`);
  return `<nav class="sumario"><h1>Sumário</h1><ol>\n${linhas.join('\n')}\n</ol></nav>`;
}

// ---------------------------------------------------------------- Impressão

function acharNavegador() {
  const nav = NAVEGADORES.find((n) => n && existsSync(n));
  if (!nav) {
    console.error('Chrome/Chromium não encontrado. Instale um ou defina CHROME=/caminho/do/navegador.');
    process.exit(1);
  }
  return nav;
}

const OPCOES_PDF = {
  format: 'A4',
  printBackground: true,
  preferCSSPageSize: true,
  displayHeaderFooter: true,
  headerTemplate: '<span></span>',
  footerTemplate: RODAPE,
  outline: true,
  tagged: true,
};

// Abre o HTML no Chrome, espera os diagramas e devolve a aba (com a lista de erros dos diagramas).
async function abrir(navegador, html, pastaTmp, nome) {
  const arquivo = path.join(pastaTmp, `${nome.replace(/\//g, '-')}.html`);
  writeFileSync(arquivo, html);
  const aba = await navegador.newPage();
  await aba.goto(pathToFileURL(arquivo).href, { waitUntil: 'load' });
  await aba.waitForFunction('window.__pronto === true', { timeout: 120000 });
  const erros = await aba.evaluate(() => window.__erros);
  // Diagrama que mesmo na página deitada ficou com letra pequena: só avisa (ver ESCALA_MIN em diagramas.js).
  const escalas = await aba.evaluate(() => [...document.querySelectorAll('.diagrama')].map((d) => Number(d.dataset.escala)));
  escalas.forEach((s, i) => {
    if (s < 0.45) console.warn(`aviso: ${nome}, diagrama ${i + 1}: letra pequena no papel (escala ${s}); simplifique o diagrama`);
  });
  return { aba, erros };
}

// Página (1, 2, ...) de destino de cada link interno, na ordem em que aparecem a partir de `inicio`.
async function paginasDosLinks(pdfBytes, quantos, inicio) {
  const pdf = await getDocument({ data: new Uint8Array(pdfBytes), verbosity: 0 }).promise;
  const paginas = [];
  for (let p = inicio; p <= pdf.numPages && paginas.length < quantos; p++) {
    const anotacoes = (await (await pdf.getPage(p)).getAnnotations())
      .filter((a) => a.subtype === 'Link' && a.dest)
      .sort((a, b) => b.rect[3] - a.rect[3]); // de cima para baixo
    for (const a of anotacoes) {
      const destino = typeof a.dest === 'string' ? await pdf.getDestination(a.dest) : a.dest;
      paginas.push(destino ? (await pdf.getPageIndex(destino[0])) + 1 : null);
    }
  }
  await pdf.destroy();
  return paginas.slice(0, quantos);
}

// Ids presentes e links internos quebrados de um HTML gerado.
function linksQuebrados(html) {
  const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
  return [...new Set([...html.matchAll(/href="#([^"]+)"/g)].map((m) => m[1]))].filter((id) => !ids.has(id));
}

async function gerarCompleto(navegador, pastaTmp, problemas) {
  const secoes = [];
  const itens = [];
  for (const doc of ORDEM) {
    const { corpo, titulos } = converter(doc, criarResolvedor(doc, true));
    secoes.push(`<section class="doc" id="${idDoc(doc)}">\n${corpo}</section>`);
    for (const t of titulos) {
      if (t.nivel === 1) itens.push({ nivel: 1, texto: t.texto, id: idDoc(doc) });
      else if (t.nivel === 2 && SUMARIO_COM_SECOES(doc)) itens.push(t);
    }
  }
  const html = pagina(TITULO, `${capa()}\n${sumario(itens)}\n${secoes.join('\n')}`);
  for (const id of linksQuebrados(html)) problemas.push(`completo: link para âncora inexistente #${id}`);

  const { aba, erros } = await abrir(navegador, html, pastaTmp, '00-documentacao-completa');
  for (const e of erros) problemas.push(`completo: ${e}`);
  // 1ª impressão: descobre as páginas; 2ª: com os números no sumário.
  const rascunho = await aba.pdf(OPCOES_PDF);
  const paginas = await paginasDosLinks(rascunho, itens.length, 2);
  if (paginas.length !== itens.length || paginas.includes(null)) {
    problemas.push('completo: não consegui ler as páginas do sumário; ficou sem números');
  }
  await aba.evaluate((ps) => {
    for (const el of document.querySelectorAll('.sumario .p')) el.textContent = ps[Number(el.dataset.i)] ?? '';
  }, paginas);
  const arquivo = path.join(SAIDA, '00-documentacao-completa.pdf');
  writeFileSync(arquivo, await aba.pdf(OPCOES_PDF));
  await aba.close();
  return arquivo;
}

async function gerarSeparado(navegador, doc, pastaTmp, problemas) {
  const { corpo, titulos } = converter(doc, criarResolvedor(doc, false));
  const titulo = titulos.find((t) => t.nivel === 1)?.texto || doc;
  const html = pagina(titulo, `<section class="doc" id="${idDoc(doc)}">\n${corpo}</section>`);
  for (const id of linksQuebrados(html)) problemas.push(`${doc}: link para âncora inexistente #${id}`);
  const { aba, erros } = await abrir(navegador, html, pastaTmp, doc);
  for (const e of erros) problemas.push(`${doc}: ${e}`);
  const arquivo = path.join(SAIDA, doc.replace(/\.md$/, '.pdf'));
  mkdirSync(path.dirname(arquivo), { recursive: true });
  writeFileSync(arquivo, await aba.pdf(OPCOES_PDF));
  await aba.close();
  return arquivo;
}

// ---------------------------------------------------------------- Principal

function mdsDaPasta() {
  const lista = readdirSync(DOC).filter((n) => n.endsWith('.md'));
  const ref = path.join(DOC, 'referencia');
  if (existsSync(ref)) lista.push(...readdirSync(ref).filter((n) => n.endsWith('.md')).map((n) => `referencia/${n}`));
  return lista;
}

async function main() {
  const args = process.argv.slice(2);
  const soCompleto = args.includes('--completo');
  const pedidos = args.filter((a) => !a.startsWith('--')).map((a) => a.replace(/^.*Documentação\//, ''));
  for (const p of pedidos) {
    if (!ORDEM.includes(p)) {
      console.error(`${p}: não está na lista ORDEM de ${path.relative(DOC, fileURLToPath(import.meta.url))}`);
      process.exit(1);
    }
  }
  const fora = mdsDaPasta().filter((d) => !ORDEM.includes(d));
  if (fora.length) console.warn(`aviso: fora da lista ORDEM (não entram nos PDFs): ${fora.join(', ')}`);

  mkdirSync(SAIDA, { recursive: true });
  const pastaTmp = mkdtempSync(path.join(tmpdir(), 'doc-pdf-'));
  const navegador = await puppeteer.launch({ executablePath: acharNavegador(), headless: true, args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const problemas = [];
  try {
    const separados = pedidos.length ? pedidos : soCompleto ? [] : ORDEM;
    if (!pedidos.length) console.log(path.relative(DOC, await gerarCompleto(navegador, pastaTmp, problemas)));
    for (const doc of separados) console.log(path.relative(DOC, await gerarSeparado(navegador, doc, pastaTmp, problemas)));
  } finally {
    await navegador.close();
    rmSync(pastaTmp, { recursive: true, force: true });
  }
  if (problemas.length) {
    console.error(`\n${problemas.length} problema(s):\n  ${problemas.join('\n  ')}`);
    process.exit(1);
  }
}

await main();
