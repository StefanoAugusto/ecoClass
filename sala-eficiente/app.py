import os
import time

from flask import Flask, render_template, jsonify

from serial_reader import LeitorSerial

app = Flask(__name__)

ultimoEstado = {
    "presenca": False, "luzBruta": 0, "luzLigada": False,
    "movimentos30s": 0, "estado": "yellow", "buzzer": False,
    "sequencia": 0, "atualizadoEm": None,
}


def tratarNovosDados(dados):
    global ultimoEstado
    ultimoEstado = {
        **dados,
        "sequencia": ultimoEstado["sequencia"] + 1,
        "atualizadoEm": time.time(),
    }


portaSerial = os.environ.get("SERIAL_PORT", "/dev/ttyACM0")
baudSerial = int(os.environ.get("SERIAL_BAUD", "115200"))
mock = os.environ.get("MOCK", "0") == "1"

leitor = LeitorSerial(
    aoReceberDados=tratarNovosDados,
    porta=portaSerial,
    baud=baudSerial,
    mock=mock,
)


@app.route("/")
def paginaInicial():
    return render_template("index.html")


@app.route("/api/state")
def apiEstado():
    return jsonify(ultimoEstado)


if __name__ == "__main__":
    leitor.start()

    host = os.environ.get("HOST", "0.0.0.0")
    porta = int(os.environ.get("PORT", "5000"))
    origem = "MOCK (sem hardware)" if mock else f"SERIAL {portaSerial}@{baudSerial}"

    print("=" * 52)
    print("  Sala Eficiente - Smart Campus")
    print(f"  Dashboard: http://{host}:{porta}")
    print(f"  Fonte de dados: {origem}")
    print("=" * 52)

    app.run(host=host, port=porta, threaded=True)
