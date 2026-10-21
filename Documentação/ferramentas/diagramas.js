// Roda dentro do Chrome, carregado por gerar_pdf.mjs no fim de cada página.
// Desenha os diagramas Mermaid e escolhe o tamanho de cada um no papel. Ao terminar marca
// window.__pronto (o puppeteer espera por ele); erros ficam em window.__erros.
window.__erros = [];

// Área útil em px CSS (96/pol), descontadas as margens do @page: retrato (A4) e paisagem (@page paisagem).
const RETRATO = { larg: 672, alt: 930, altBoa: 620 };
const PAISAGEM = { larg: 1000, alt: 640, altBoa: 640 };
const ESCALA_MIN = 0.55; // abaixo disso a letra do diagrama fica menor que ~6,5 pt

// Fração do tamanho natural para caber na área: no máximo 80% (letra do diagrama ~ letra do texto).
// Um diagrama alto encolhe até altBoa (para caber embaixo de algum texto), mas não abaixo de 60%;
// nunca passa da altura da página.
function escala(vb, area) {
  let s = Math.min(0.8, area.larg / vb.width);
  if (vb.height * s > area.altBoa) s = Math.max(area.altBoa / vb.height, Math.min(s, 0.6));
  return Math.min(s, area.alt / vb.height);
}

async function desenhar(id, texto, caixa) {
  const { svg } = await mermaid.render(id, texto);
  caixa.innerHTML = svg;
  const el = caixa.querySelector('svg');
  return { el, vb: el.viewBox.baseVal };
}

(async () => {
  // Espaçamentos menores que o padrão: diagrama mais estreito, letra maior depois de caber no A4.
  mermaid.initialize({ startOnLoad: false, theme: 'default', securityLevel: 'strict',
    themeVariables: { fontFamily: 'Helvetica Neue, Helvetica, Arial, sans-serif' },
    flowchart: { nodeSpacing: 28, rankSpacing: 40, padding: 10, wrappingWidth: 170,
      subGraphTitleMargin: { top: 4, bottom: 10 } },
    state: { nodeSpacing: 30, rankSpacing: 36 },
    sequence: { wrap: true, width: 120, actorMargin: 24, messageMargin: 28, noteMargin: 8, boxMargin: 6 } });
  const caixas = [...document.querySelectorAll('.diagrama')];
  for (let i = 0; i < caixas.length; i++) {
    const caixa = caixas[i];
    const texto = caixa.querySelector('pre.mermaid').textContent;
    try {
      let { el, vb } = await desenhar('diagrama' + i, texto, caixa);
      let area = RETRATO;
      // Fluxo em linha, muito largo e baixo: no papel cabe melhor de cima para baixo.
      const deitado = /^\s*flowchart\s+(LR|RL)\b/.test(texto);
      if (deitado && escala(vb, RETRATO) < ESCALA_MIN && vb.width > 5 * vb.height) {
        ({ el, vb } = await desenhar('diagrama' + i + 'tb', texto.replace(/^(\s*flowchart\s+)(LR|RL)/, '$1TB'), caixa));
      }
      // Ainda largo demais para o retrato: vai sozinho numa página deitada.
      if (escala(vb, RETRATO) < ESCALA_MIN && escala(vb, PAISAGEM) > escala(vb, RETRATO)) {
        caixa.classList.add('paisagem');
        area = PAISAGEM;
      }
      const s = escala(vb, area);
      el.setAttribute('width', (vb.width * s).toFixed(1));
      el.setAttribute('height', (vb.height * s).toFixed(1));
      el.style.maxWidth = '100%';
      caixa.dataset.escala = s.toFixed(2);
    } catch (e) {
      window.__erros.push('diagrama ' + (i + 1) + ': ' + (e && e.message ? e.message : e));
    }
  }
  await document.fonts.ready;
  window.__pronto = true;
})();
