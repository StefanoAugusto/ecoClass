/* =====================================================================
   Sala Eficiente / EcoClass - Firmware do Arduino
   ---------------------------------------------------------------------
   Objetivo: monitorar uma sala e sinalizar, por meio de um semaforo de
   LEDs, se ela esta sendo usada de forma eficiente.

   SIGNIFICADO DAS CORES (semaforo):
     VERDE    -> Sala Disponivel       (vazia e com a luz apagada)
     AMARELO  -> Sala Ocupada          (ha presenca de pessoas)
     VERMELHO -> Desperdicio detectado (vazia, porem com a luz acesa)

   Quando o desperdicio (vermelho) persiste por muito tempo, um buzzer
   passa a apitar para chamar a atencao.

   Sensores/atuadores usados:
     - PIR (movimento)  -> detecta presenca
     - LDR (luz)        -> mede a luminosidade do ambiente
     - 3 LEDs           -> semaforo verde/amarelo/vermelho
     - Buzzer           -> alarme sonoro de desperdicio prolongado

   O Arduino tambem envia o status na Serial (115200 baud) uma vez por
   segundo, como um objeto JSON (uma linha por mensagem), que o servidor
   Python le e repassa ao dashboard. Exemplo de linha enviada:
     {"presence":true,"light_raw":512,"light_on":true,
      "movements_30s":3,"state":"amarelo","buzzer":false}
   ===================================================================== */

/* ---------- Mapeamento dos pinos fisicos do Arduino ---------- */
const uint8_t pinoPir     = 2;   // sensor de presenca (PIR) - entrada digital
const uint8_t pinoLdr     = A0;  // sensor de luz (LDR) - entrada analogica
const uint8_t ledVerde    = 5;   // LED verde   -> Sala Disponivel
const uint8_t ledAmarelo  = 6;   // LED amarelo -> Sala Ocupada
const uint8_t ledVermelho = 7;   // LED vermelho-> Desperdicio
const uint8_t pinoBuzzer  = 8;   // buzzer (alarme sonoro)

/* ---------- Parametros de calibracao / tempos ----------
   Ajuste estes valores conforme o ambiente e o hardware. */
const int           limiarLuz         = 200;     // acima disso consideramos "luz acesa"
const unsigned long janelaOcupacao    = 30000UL; // 30s: janela para contar movimentos recentes
const unsigned long tempoBuzzer       = 10000UL; // 10s de desperdicio continuo antes de apitar
const unsigned long intervaloEnvio    = 1000UL;  // 1s: periodo entre leituras/relatorios
const int           maxMovimentos     = 10;      // tamanho do historico circular de movimentos
const unsigned long debounceMovimento = 200UL;   // ignora pulsos do PIR muito proximos (ruido)
const unsigned long intervaloBip      = 400UL;   // cadencia do bip do buzzer (liga/desliga)
const int           amostrasLuz       = 5;       // numero de leituras do LDR para tirar a media
const long          baudSerial        = 115200;  // velocidade da comunicacao serial

/* ---------- Prototipos das funcoes ---------- */
void lerPir(unsigned long agora);
int  contarMovimentos(unsigned long agora);
int  lerLuz();
void atualizarEstado(unsigned long agora);
void atualizarBuzzer(unsigned long agora);
void imprimirStatus(unsigned long agora);

/* ---------- Variaveis de controle do historico de movimentos ----------
   Guardamos os instantes (millis) dos ultimos movimentos num buffer
   circular, para contar quantos ocorreram nos ultimos 30 segundos. */
unsigned long temposMovimento[maxMovimentos];
int indiceMovimento = 0, movimentosArmazenados = 0;
unsigned long ultimoMovimento = 0;
int estadoAnteriorPir = LOW;   // estado anterior do PIR (para detectar a borda de subida)

/* ---------- Variaveis de controle do desperdicio / buzzer ---------- */
bool emDesperdicio = false;            // estamos atualmente em situacao de desperdicio?
unsigned long inicioDesperdicio = 0;   // instante em que o desperdicio comecou
bool buzzerAtivo = false, estadoBip = false;
unsigned long ultimoBip = 0;
unsigned long ultimoEnvio = 0;         // controla o intervalo de 1s entre relatorios

/* ---------- Estado atual da sala (enviado para o dashboard) ---------- */
bool presenca = false, luzLigada = false;
int luzBruta = 0, movimentos = 0;
const char* estado = "verde";  // estado inicial: Sala Disponivel (ate a 1a leitura)

void setup() {
  // Configura os pinos: PIR como entrada; LEDs e buzzer como saida.
  pinMode(pinoPir, INPUT);
  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarelo, OUTPUT);
  pinMode(ledVermelho, OUTPUT);
  pinMode(pinoBuzzer, OUTPUT);
  Serial.begin(baudSerial);  // inicia a comunicacao serial com o servidor
  delay(300);
  Serial.println(F("Sala Eficiente iniciada"));
}

void loop() {
  unsigned long agora = millis();  // tempo atual em ms desde que o Arduino ligou
  lerPir(agora);                   // verifica o PIR a cada ciclo (alta frequencia)
  atualizarBuzzer(agora);          // mantem o bip do buzzer no ritmo certo

  // O bloco abaixo roda apenas 1x por segundo (intervaloEnvio):
  if (agora - ultimoEnvio >= intervaloEnvio) {
    ultimoEnvio = agora;
    atualizarEstado(agora);  // recalcula presenca/luz/estado e aciona os LEDs
    imprimirStatus(agora);   // envia o status pela serial para o dashboard
  }
}

/* Le o sensor PIR e registra um novo movimento quando ha borda de subida
   (passou de LOW para HIGH). O debounce evita contar ruido como movimento. */
void lerPir(unsigned long agora) {
  int pir = digitalRead(pinoPir);
  if (pir == HIGH && estadoAnteriorPir == LOW && agora - ultimoMovimento > debounceMovimento) {
    temposMovimento[indiceMovimento] = agora;                 // grava o instante do movimento
    indiceMovimento = (indiceMovimento + 1) % maxMovimentos;  // avanca no buffer circular
    if (movimentosArmazenados < maxMovimentos) movimentosArmazenados++;
    ultimoMovimento = agora;
  }
  estadoAnteriorPir = pir;  // memoriza o estado para a proxima comparacao
}

/* Conta quantos dos movimentos guardados aconteceram dentro da janela de
   ocupacao (ultimos 30s). Usado para decidir se ha presenca na sala. */
int contarMovimentos(unsigned long agora) {
  int conta = 0;
  for (int i = 0; i < movimentosArmazenados; i++)
    if (agora - temposMovimento[i] <= janelaOcupacao) conta++;
  return conta;
}

/* Le o LDR varias vezes e retorna a media, para suavizar o ruido da leitura. */
int lerLuz() {
  long soma = 0;
  for (int i = 0; i < amostrasLuz; i++) { soma += analogRead(pinoLdr); delay(2); }
  return (int)(soma / amostrasLuz);
}

/* Coracao da logica: a partir de presenca + luz, define o estado (cor) da
   sala, aciona os LEDs correspondentes e gerencia o alarme de desperdicio.

   Regras do semaforo:
     VERDE    = Sala Disponivel       -> sem presenca E luz apagada
     AMARELO  = Sala Ocupada          -> ha presenca
     VERMELHO = Desperdicio detectado -> sem presenca E luz acesa */
void atualizarEstado(unsigned long agora) {
  movimentos = contarMovimentos(agora);  // movimentos nos ultimos 30s
  presenca = (movimentos >= 1);          // havendo qualquer movimento, consideramos ocupada
  luzBruta = lerLuz();                    // leitura crua do LDR
  luzLigada = (luzBruta > limiarLuz);     // acima do limiar => luz acesa

  // Decide a cor/estado conforme as regras acima.
  if (presenca) estado = "amarelo";          // Sala Ocupada
  else if (!luzLigada) estado = "verde";     // Sala Disponivel (vazia e escura)
  else estado = "vermelho";                  // Desperdicio (vazia e iluminada)

  // Liga apenas o LED correspondente ao estado atual.
  digitalWrite(ledVerde,    (!presenca && !luzLigada) ? HIGH : LOW); // verde   = Disponivel
  digitalWrite(ledAmarelo,  presenca ? HIGH : LOW);                  // amarelo = Ocupada
  digitalWrite(ledVermelho, (!presenca && luzLigada) ? HIGH : LOW);  // vermelho= Desperdicio

  // Desperdicio = sala vazia com a luz acesa. Se isso persistir por
  // 'tempoBuzzer', o buzzer e acionado para alertar.
  bool desperdicio = (!presenca && luzLigada);
  if (desperdicio) {
    if (!emDesperdicio) { emDesperdicio = true; inicioDesperdicio = agora; } // marca o inicio
    if (agora - inicioDesperdicio >= tempoBuzzer) buzzerAtivo = true;        // tempo estourou
  } else {
    emDesperdicio = false;  // saiu do desperdicio: zera tudo
    buzzerAtivo = false;
  }
}

/* Faz o buzzer apitar de forma intermitente (bip) enquanto buzzerAtivo for
   verdadeiro. Usa millis() para nao travar o programa com delay(). */
void atualizarBuzzer(unsigned long agora) {
  if (buzzerAtivo) {
    if (agora - ultimoBip >= intervaloBip) {
      estadoBip = !estadoBip;                              // alterna liga/desliga
      digitalWrite(pinoBuzzer, estadoBip ? HIGH : LOW);
      ultimoBip = agora;
    }
  } else if (estadoBip) {
    estadoBip = false;                // garante que o buzzer fique desligado
    digitalWrite(pinoBuzzer, LOW);
  }
}

/* Envia o status atual pela Serial como um objeto JSON, uma linha por
   mensagem (terminada por \n). O servidor Python faz json.loads dessa linha.

   IMPORTANTE sobre o formato JSON:
     - As chaves seguem o padrao que o Python espera (em ingles):
       presence, light_raw, light_on, movements_30s, state, buzzer.
     - Booleanos precisam ser "true"/"false" em minusculo (o Serial.print
       de um bool imprimiria "1"/"0", que nao e JSON valido), por isso
       montamos os textos manualmente.
     - O valor de "state" vai entre aspas (e uma string).

   Exemplo de linha enviada:
     {"presence":true,"light_raw":512,"light_on":true,"movements_30s":3,"state":"amarelo","buzzer":false} */
void imprimirStatus(unsigned long agora) {
  Serial.print(F("{\"presence\":"));      Serial.print(presenca ? F("true") : F("false"));
  Serial.print(F(",\"light_raw\":"));     Serial.print(luzBruta);
  Serial.print(F(",\"light_on\":"));      Serial.print(luzLigada ? F("true") : F("false"));
  Serial.print(F(",\"movements_30s\":")); Serial.print(movimentos);
  Serial.print(F(",\"state\":\""));       Serial.print(estado);  Serial.print(F("\""));
  Serial.print(F(",\"buzzer\":"));        Serial.print(buzzerAtivo ? F("true") : F("false"));
  Serial.println(F("}"));
}
