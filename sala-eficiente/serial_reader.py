import json
import random
import re
import threading
import time
from collections import deque

try:
    import serial
except ImportError:
    serial = None


_estadoPadrao = {
    "presenca": False,
    "luzBruta": 0,
    "luzLigada": False,
    "movimentos30s": 0,
    "estado": "yellow",
    "buzzer": False,
}

# Traducao das cores enviadas pelo Arduino (pt) para o padrao usado no
# dashboard (en). Apenas troca o idioma da cor; o significado e:
#   verde/green   -> Sala Disponivel
#   amarelo/yellow-> Sala Ocupada
#   vermelho/red  -> Desperdicio detectado
_estadoPtParaEn = {"vermelho": "red", "verde": "green", "amarelo": "yellow"}


class LeitorSerial(threading.Thread):

    def __init__(self, aoReceberDados, porta="/dev/ttyACM0", baud=115200, mock=False,
                 intervaloEnvio=1.0):
        super().__init__(daemon=True)
        self.aoReceberDados = aoReceberDados
        self.porta = porta
        self.baud = baud
        self.mock = mock or serial is None
        self.intervaloEnvio = intervaloEnvio
        self._executando = True
        self._serial = None

        self._qtdOk = 0
        self._qtdInvalidas = 0
        self._recebeuPrimeira = False

        # Limiar de luz (0-1023) usado apenas pelo gerador de dados do mock;
        # com hardware real quem decide "luz acesa" e o proprio firmware.
        # Mantido igual ao limiar do EcoClass.ino (200).
        self.limiarLuz = 200

    def parar(self):
        self._executando = False

    def run(self):
        if self.mock:
            self._executarMock()
        else:
            self._executarSerial()

    def _executarSerial(self):
        while self._executando:
            try:
                self._serial = serial.Serial(self.porta, self.baud, timeout=2)
                print(f"[serial] conectado em {self.porta} @ {self.baud} baud")
                self._serial.reset_input_buffer()

                while self._executando:
                    bruto = self._serial.readline()
                    linha = bruto.decode("utf-8", errors="ignore").strip()
                    if not linha:
                        continue
                    dados = self._analisarLinha(linha)
                    if dados is not None:
                        self.aoReceberDados(dados)

            except serial.SerialException as e:
                print(f"[serial] erro de conexao: {e}. Reconectando em 3s...")
                time.sleep(3)
            finally:
                if self._serial and self._serial.is_open:
                    self._serial.close()

    def _analisarLinha(self, linha):
        try:
            dados = json.loads(linha)
            normalizado = self._normalizar(dados)
        except json.JSONDecodeError:
            normalizado = self._analisarTexto(linha)
            if normalizado is None:
                self._registrarLinhaInvalida(linha, "formato nao reconhecido")
                return None
        except (ValueError, TypeError, AttributeError) as e:
            self._registrarLinhaInvalida(linha, f"campo invalido: {e}")
            return None

        self._qtdOk += 1
        if not self._recebeuPrimeira:
            self._recebeuPrimeira = True
            print(f"[serial] OK -> primeira leitura valida recebida: {normalizado}")
        return normalizado

    def _analisarTexto(self, linha):
        if "=" not in linha:
            return None
        pares = dict(re.findall(r"(\w+)=(\S+)", linha))
        if "estado" not in pares and "presenca" not in pares:
            return None

        luzBruta, luzLigada = 0, False
        correspondencia = re.match(r"(\d+)(?:\((on|off)\))?", pares.get("luz", ""))
        if correspondencia:
            luzBruta = int(correspondencia.group(1))
            luzLigada = correspondencia.group(2) == "on"

        # .lower() torna a comparacao insensivel a maiusculas/minusculas:
        # o firmware imprimia "SIM" e "ON", entao comparar com "sim"/"on"
        # cru daria sempre False.
        return {
            "presenca": pares.get("presenca", "nao").lower() == "sim",
            "luzBruta": luzBruta,
            "luzLigada": luzLigada,
            "movimentos30s": self._paraInteiro(pares.get("mov", 0)),
            "estado": _estadoPtParaEn.get(pares.get("estado", ""),
                                          pares.get("estado", "yellow")),
            "buzzer": pares.get("buzzer", "off").lower() == "on",
        }

    def _registrarLinhaInvalida(self, linha, motivo):
        self._qtdInvalidas += 1
        if self._qtdInvalidas <= 3 or self._qtdInvalidas % 50 == 0:
            print(f"[serial] linha ignorada ({motivo}) "
                  f"[descartadas: {self._qtdInvalidas}]: {linha!r}")
        if not self._recebeuPrimeira and self._qtdOk == 0 and self._qtdInvalidas == 3:
            print("[serial] ATENCAO: chegam dados na serial, mas nenhuma linha é "
                  "válida. Confira o formato impresso pelo firmware (uma linha "
                  "por mensagem)")

    @staticmethod
    def _paraInteiro(valor, padrao=0):
        try:
            return int(valor)
        except (ValueError, TypeError):
            return padrao

    @classmethod
    def _normalizar(cls, dados):
        # Converte o JSON cru do Arduino (chaves em ingles) para o formato
        # interno usado pelo dashboard. Esta e a funcao usada quando o
        # firmware envia JSON valido.
        if not isinstance(dados, dict):
            raise TypeError("mensagem não é válida")
        # A cor pode vir em pt (verde/amarelo/vermelho) ou em en
        # (green/yellow/red); o dashboard so entende en, entao traduzimos.
        # O get com fallback para o proprio valor mantem compativel com ambos.
        estadoBruto = str(dados.get("state", "yellow"))
        return {
            "presenca": bool(dados.get("presence", False)),
            "luzBruta": cls._paraInteiro(dados.get("light_raw", 0)),
            "luzLigada": bool(dados.get("light_on", False)),
            "movimentos30s": cls._paraInteiro(dados.get("movements_30s", 0)),
            "estado": _estadoPtParaEn.get(estadoBruto, estadoBruto),
            "buzzer": bool(dados.get("buzzer", False)),
        }

    def _executarMock(self):
        print("[mock] gerando dados simulados (sem hardware). MOCK ativo.")

        demoBuzzerAposS = 12
        temposMovimento = deque()

        fase = "ocupada"
        faseAte = time.time() + random.uniform(10, 14)
        luzAcesa = True
        vermelhoDesde = None

        while self._executando:
            agora = time.time()

            if agora >= faseAte:
                if fase == "ocupada":
                    fase, luzAcesa = "vaziaLuzAcesa", True
                    faseAte = agora + random.uniform(45, 60)
                elif fase == "vaziaLuzAcesa":
                    fase, luzAcesa = "vaziaLuzApagada", False
                    faseAte = agora + random.uniform(10, 14)
                else:
                    fase, luzAcesa = "ocupada", True
                    faseAte = agora + random.uniform(10, 14)

            if fase == "ocupada" and random.random() < 0.6:
                temposMovimento.append(agora)
            while temposMovimento and agora - temposMovimento[0] > 30:
                temposMovimento.popleft()
            movimentos30s = len(temposMovimento)

            presenca = movimentos30s > 0

            if luzAcesa:
                baixo = min(self.limiarLuz + 120, 950)
                luzBruta = random.randint(baixo, 950)
            else:
                alto = max(80, min(self.limiarLuz - 150, 1023))
                luzBruta = random.randint(40, alto)
            luzLigada = luzBruta > self.limiarLuz

            # Define a cor/estado da sala (mesma regra do firmware EcoClass.ino):
            #   green  -> Sala Disponivel       (vazia e com a luz apagada)
            #   yellow -> Sala Ocupada          (ha presenca)
            #   red    -> Desperdicio detectado (vazia, porem com a luz acesa)
            if presenca:
                estado = "yellow"   # Sala Ocupada
            elif not luzLigada:
                estado = "green"    # Sala Disponivel
            else:
                estado = "red"      # Desperdicio detectado

            # O buzzer so dispara no desperdicio (vermelho) prolongado.
            if estado == "red":
                if vermelhoDesde is None:
                    vermelhoDesde = agora
                buzzer = (agora - vermelhoDesde) >= demoBuzzerAposS
            else:
                vermelhoDesde = None
                buzzer = False

            self.aoReceberDados({
                "presenca": presenca,
                "luzBruta": luzBruta,
                "luzLigada": luzLigada,
                "movimentos30s": movimentos30s,
                "estado": estado,
                "buzzer": buzzer,
            })

            time.sleep(self.intervaloEnvio)


if __name__ == "__main__":
    def _imprimir(d):
        print(json.dumps(d, ensure_ascii=False))

    leitor = LeitorSerial(aoReceberDados=_imprimir, mock=True, intervaloEnvio=1.0)
    leitor.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        leitor.parar()
        print("\nEncerrado.")
