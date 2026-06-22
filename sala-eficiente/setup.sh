#!/usr/bin/env bash
# =============================================================================
# setup.sh - prepara o servidor Sala Eficiente no Raspberry Pi.
# Cria o ambiente virtual, instala as dependencias e (opcional) configura o
# servico de inicializacao automatica.
#
# Uso:
#   chmod +x setup.sh
#   ./setup.sh                # cria o venv e instala as dependencias
#   ./setup.sh --autostart    # tambem instala o servico systemd (inicia no boot)
# =============================================================================

set -e

# pasta onde este script esta (a pasta server/)
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "==> Verificando Python 3..."
if ! command -v python3 >/dev/null 2>&1; then
  echo "ERRO: python3 nao encontrado."
  echo "Instale com: sudo apt install -y python3 python3-venv python3-pip"
  exit 1
fi
python3 --version

echo "==> Criando o ambiente virtual (venv)..."
if [ ! -d venv ]; then
  python3 -m venv venv
fi

echo "==> Instalando as dependencias (pode demorar alguns minutos)..."
./venv/bin/pip install --upgrade pip >/dev/null
./venv/bin/pip install -r requirements.txt

echo "==> Verificando acesso a porta serial (grupo dialout)..."
if groups | grep -qw dialout; then
  echo "    OK: usuario ja esta no grupo dialout."
else
  echo "    AVISO: seu usuario NAO esta no grupo 'dialout'."
  echo "    Para ler o Arduino, rode:  sudo usermod -aG dialout $USER"
  echo "    e depois reinicie o Pi:     sudo reboot"
fi

# ---- Opcional: instala o servico systemd (--autostart) ----------------------
if [ "${1:-}" = "--autostart" ]; then
  echo "==> Instalando o servico systemd (inicio automatico no boot)..."
  USER_NAME="$(whoami)"
  SERVICE=/etc/systemd/system/sala-eficiente.service
  sudo tee "$SERVICE" >/dev/null <<EOF
[Unit]
Description=Servidor Sala Eficiente (Smart Campus)
After=network.target

[Service]
User=$USER_NAME
WorkingDirectory=$DIR
ExecStart=$DIR/venv/bin/python3 app.py
# Para iniciar em modo simulacao (sem hardware), descomente a linha abaixo:
# Environment=MOCK=1
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF
  sudo systemctl daemon-reload
  sudo systemctl enable sala-eficiente.service
  sudo systemctl restart sala-eficiente.service
  echo "    Servico instalado e iniciado."
  echo "    Status:  sudo systemctl status sala-eficiente"
  echo "    Logs:    journalctl -u sala-eficiente -f"
fi

echo
echo "======================================================"
echo " Pronto! Para rodar manualmente:"
echo "   source venv/bin/activate"
echo "   MOCK=1 python3 app.py     # sem hardware (teste do dashboard)"
echo "   python3 app.py            # com o Arduino conectado por USB"
echo
echo " Descobrir a porta do Arduino:"
echo "   ls /dev/ttyACM* /dev/ttyUSB*"
echo
echo " Abra no navegador:  http://localhost:5000"
echo " (ou de outro aparelho: http://IP_DO_PI:5000)"
echo "======================================================"
