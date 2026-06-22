# Servidor — Sala Eficiente (Smart Campus)

Servidor web que lê o Arduino pela serial e mostra, em tempo real, a ocupação e a
iluminação da sala num dashboard. Inclui um **modo de simulação (mock)** para testar
sem o hardware montado.

## Arquivos

```
server/
├── app.py                 # servidor Flask (HTTP + JSON)
├── serial_reader.py       # leitura serial do Arduino + modo mock
├── requirements.txt       # dependencias Python
├── templates/
│   └── index.html         # dashboard (pagina unica)
└── static/
    ├── style.css          # tema verde/amarelo Smart Campus
    ├── app.js             # consulta /api/state a cada 1s + atualiza a UI
```

## Instalacao (no Raspberry Pi)

```bash
cd server
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## Rodar

**Modo de simulação (sem hardware) — bom para testar o dashboard agora:**
```bash
MOCK=1 python3 app.py
```

**Modo real (com o Arduino conectado por USB):**
```bash
python3 app.py
# se o Arduino aparecer noutra porta:
SERIAL_PORT=/dev/ttyUSB0 python3 app.py
```

Depois abra no navegador:
- no próprio Pi: <http://localhost:5000>
- de outro aparelho na mesma rede: `http://sala-eficiente.local:5000` (ou `http://IP_DO_PI:5000`)

## Variáveis de ambiente (opcionais)

| Variável | Padrão | Função |
|----------|--------|--------|
| `MOCK` | `0` | `1` roda com dados simulados, sem hardware |
| `SERIAL_PORT` | `/dev/ttyACM0` | porta serial do Arduino |
| `SERIAL_BAUD` | `115200` | velocidade da serial |
| `HOST` | `0.0.0.0` | interface do servidor (toda a rede) |
| `PORT` | `5000` | porta do servidor web |

## Testar só o leitor serial / mock

```bash
python3 serial_reader.py      # imprime as mensagens JSON do mock no terminal
```

## O protocolo (Arduino → Pi)

Uma linha JSON por mensagem, a 115200 baud:
```json
{"presence": true, "light_raw": 342, "light_on": true, "movements_30s": 7, "state": "red", "buzzer": false}
```