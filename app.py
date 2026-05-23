import threading
import json
import os
import serial
import pandas as pd
from flask import Flask, render_template, jsonify, send_file, Response
from datetime import datetime
from queue import Queue

app = Flask(__name__)


PORTA_SERIAL = "COM3" #Porta arduino    
BAUD_RATE    = 9600
ARQUIVO_XLS  = "chamada.xlsx"

# Dicionário RA → Nome (igual ao array do Arduino)
ALUNOS = {
    "26000288": "Yasmin Mamud",
    "26000287": "Nicolas Lima",
    "26000193": "Thais Sales",
    "26000306": "Gustavo Staut",
    "26000289": "Ewelyn Cristina",
}

# FILA SSE avisa o cliente quando chegar dado novo
clientes_sse: list = []

def notificar_clientes():
    for q in clientes_sse:
        q.put("atualizar")


# PLANILHA
def garantir_planilha():
    if not os.path.exists(ARQUIVO_XLS):
        df = pd.DataFrame(columns=["Nome", "RA", "Data", "Hora", "Origem"])
        df.to_excel(ARQUIVO_XLS, index=False)
        print("[XLSX] Planilha criada.")

def salvar_na_planilha(registros: list, origem: str):
    garantir_planilha()
    df_existente = pd.read_excel(ARQUIVO_XLS)
    novas_linhas = []

    for r in registros:
        ra   = r.get("ra", "")
        nome = ALUNOS.get(ra, "Desconhecido")

        data_raw = r.get("data", "")
        if len(data_raw) == 6:
            data_fmt = f"{data_raw[0:2]}/{data_raw[2:4]}/20{data_raw[4:6]}"
        else:
            data_fmt = data_raw

        novas_linhas.append({
            "Nome":   nome,
            "RA":     ra,
            "Data":   data_fmt,
            "Hora":   r.get("hora", ""),
            "Origem": origem
        })

    df_final = pd.concat([df_existente, pd.DataFrame(novas_linhas)], ignore_index=True)
    df_final.to_excel(ARQUIVO_XLS, index=False)
    print(f"[XLSX] {len(novas_linhas)} registro(s) salvos. Total na planilha: {len(df_final)}")


# THREAD — LEITURA DA SERIAL
def escutar_serial():
    print(f"[SERIAL] Conectando em {PORTA_SERIAL} a {BAUD_RATE} baud...")

    while True:
        try:
            with serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=1) as ser:
                print("[SERIAL] Conectado! Aguardando dados do Arduino...")

                capturando   = False
                buffer_json  = ""
                origem_envio = "automatico"

                while True:
                    linha = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not linha:
                        continue

                    print(f"[SERIAL] {linha}")

                    if "##OVERFLOW##" in linha:
                        origem_envio = "overflow"

                    elif "##JSON_START##" in linha:
                        capturando  = True
                        buffer_json = ""

                    elif "##JSON_END##" in linha and capturando:
                        capturando = False
                        try:
                            dados     = json.loads(buffer_json)
                            registros = dados.get("registros", [])
                            salvar_na_planilha(registros, origem_envio)
                            notificar_clientes()
                            origem_envio = "automatico"
                            print(f"[OK] {len(registros)} registro(s) processados.")
                        except json.JSONDecodeError as e:
                            print(f"[ERRO] JSON inválido: {e}")

                    elif capturando:
                        buffer_json += linha

        except serial.SerialException as e:
            print(f"[SERIAL] Erro: {e}. Reconectando em 5s...")
            import time; time.sleep(5)


# ROTAS
@app.route("/")
def index():    
    return render_template("login.html")

@app.route("/chamada")  
def chamada():
    return render_template("chamada.html")

@app.route("/historico")
def historico():
    return render_template("historico.html")

@app.route("/api/chamada")
def api_chamada():
    garantir_planilha()
    df   = pd.read_excel(ARQUIVO_XLS)
    hoje = datetime.now().strftime("%d/%m/%Y")
    df_hoje = df[df["Data"] == hoje]

    return jsonify([
        {"nome": row["Nome"], "ra": str(row["RA"]),
         "data": row["Data"], "horario": row["Hora"], "status": "Presente"}
        for _, row in df_hoje.iterrows()
    ])

@app.route("/api/historico")
def api_historico():
    garantir_planilha()
    df = pd.read_excel(ARQUIVO_XLS)

    return jsonify([
        {"nome": row["Nome"], "ra": str(row["RA"]),
         "data": row["Data"], "horario": row["Hora"], "status": "Presente"}
        for _, row in df.iterrows()
    ])

@app.route("/api/forcar-envio", methods=["POST"])
def forcar_envio():
    """Escreve 'E' na Serial — faz o Arduino disparar enviarChamada()."""
    try:
        with serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=1) as ser:
            ser.write(b'E')
        return jsonify({"ok": True})
    except serial.SerialException as e:
        print(f"[ERRO] forcar_envio: {e}")
        return jsonify({"ok": False, "erro": str(e)}), 500

@app.route("/download")
def download():
    garantir_planilha()
    return send_file(
        ARQUIVO_XLS,
        as_attachment=True,
        download_name=f"chamada_{datetime.now().strftime('%d%m%Y')}.xlsx"
    )

@app.route("/eventos")
def eventos():
    # SSE (Server-Sent Events) para atualizar a página automaticamente quando chegar dado novo
    def stream():
        q = Queue()
        clientes_sse.append(q)
        try:
            while True:
                try:
                    msg = q.get(timeout=15)
                    yield f"data: {msg}\n\n"
                except:
                    yield ": heartbeat\n\n"
        finally:
            clientes_sse.remove(q)

    return Response(stream(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


if __name__ == "__main__":
    garantir_planilha()
    threading.Thread(target=escutar_serial, daemon=True).start()
    print("[FLASK] Acesse: http://localhost:5000")
    app.run(debug=False, threaded=True)
