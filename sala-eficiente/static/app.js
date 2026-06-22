(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const raiz = document.documentElement;

  const elementos = {
    conexao:         $("conn"),
    rotuloConexao:   $("conn-label"),
    reconectar:      $("reconnect"),
    statusHero:      $("hero-status"),
    subHero:         $("hero-sub"),
    aoVivo:          $("live"),
    cardPresenca:    $("card-presence"),
    valorPresenca:   $("presence-value"),
    cardLuz:         $("card-light"),
    valorLuz:        $("light-value"),
    barraLuz:        $("light-bar"),
    marcaLimiar:     $("threshold-mark"),
    luzBruta:        $("light-raw"),
    movimentos:      $("movements"),
    buzzer:          $("buzzer"),
  };

  // Texto exibido no painel para cada cor/estado recebido do servidor.
  // O significado das cores (igual ao firmware EcoClass.ino):
  //   green  -> Sala Disponivel       (vazia e com a luz apagada)
  //   yellow -> Sala Ocupada          (ha presenca)
  //   red    -> Desperdicio detectado (vazia, porem com a luz acesa)
  const textosEstado = {
    green:  { palavra: "Disponivel",  sub: "Sala livre e luz apagada." },
    yellow: { palavra: "Ocupada",     sub: "Tem gente na sala." },
    red:    { palavra: "Desperdicio", sub: "Sala vazia com a luz acesa." },
  };

  const luzMax = 1023;          // fundo de escala do LDR (analogRead vai de 0 a 1023)
  const intervaloPollMs = 1000;
  const timeoutFetchMs = 4000;
  const offlineAposMs = 5000;
  let limiarAtual = 200;        // igual ao limiar do firmware EcoClass.ino
  let timerAoVivo = null;
  let ultimoOkEm = Date.now();
  let consultando = false;

  async function consultar() {
    if (consultando) return;
    consultando = true;

    const controlador = new AbortController();
    const temporizador = setTimeout(() => controlador.abort(), timeoutFetchMs);

    try {
      const resposta = await fetch("/api/state", { cache: "no-store", signal: controlador.signal });
      if (!resposta.ok) throw new Error("HTTP " + resposta.status);
      const dados = await resposta.json();
      ultimoOkEm = Date.now();
      definirConexao("connected", "conectado");
      renderizar(dados);
      pulsarAoVivo();
    } catch (e) {
      if (Date.now() - ultimoOkEm >= offlineAposMs) {
        definirConexao("offline", "reconectando...");
      }
    } finally {
      clearTimeout(temporizador);
      consultando = false;
    }
  }

  function renderizar(d) {
    const estado = (d.estado in textosEstado) ? d.estado : "yellow";
    raiz.setAttribute("data-state", estado);
    elementos.statusHero.textContent = textosEstado[estado].palavra;
    elementos.subHero.textContent = textosEstado[estado].sub;

    elementos.cardPresenca.setAttribute("data-on", String(!!d.presenca));
    elementos.valorPresenca.textContent = d.presenca ? "Sala ocupada" : "Sala vazia";

    elementos.cardLuz.setAttribute("data-on", String(!!d.luzLigada));
    elementos.valorLuz.textContent = d.luzLigada ? "Luz ligada" : "Luz apagada";

    const bruto = limitar(Number(d.luzBruta) || 0, 0, luzMax);
    elementos.luzBruta.textContent = bruto + " / " + luzMax;
    elementos.barraLuz.style.width = ((bruto / luzMax) * 100).toFixed(1) + "%";

    const mov = Number(d.movimentos30s) || 0;
    elementos.movimentos.textContent =
      mov === 1 ? "1 movimento nos ultimos 30s"
                : mov + " movimentos nos ultimos 30s";

    elementos.buzzer.hidden = !d.buzzer;
  }

  function definirConexao(status, rotulo) {
    elementos.conexao.setAttribute("data-status", status);
    elementos.rotuloConexao.textContent = rotulo;
    elementos.reconectar.hidden = (status === "connected");
  }

  function pulsarAoVivo() {
    elementos.aoVivo.classList.add("is-on");
    clearTimeout(timerAoVivo);
    timerAoVivo = setTimeout(() => elementos.aoVivo.classList.remove("is-on"), 1300);
  }

  function posicionarMarcaLimiar() {
    const pct = limitar((limiarAtual / luzMax) * 100, 0, 100);
    elementos.marcaLimiar.style.left = pct + "%";
  }

  function limitar(valor, minimo, maximo) { return Math.max(minimo, Math.min(maximo, valor)); }

  definirConexao("connecting", "conectando...");
  posicionarMarcaLimiar();
  consultar();
  setInterval(consultar, intervaloPollMs);
})();
