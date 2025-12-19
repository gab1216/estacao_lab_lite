#!/usr/bin/env python3
import argparse
import csv
import os
import sys
from datetime import datetime

import serial  # pip install pyserial

ASTERISKS = "**********"


def parse_line(line: str):
    """
    Espera algo como (2 + 9 tokens = 11 no total):
    09/22/2025 14:51:02 -3.794832 -38.557644 24.12 45.00 101196.00 0 0.00 0.00 0.00

    Campos mapeados:
      lat, lon, temp_c, ur, press, pluv, rad, vento_int, vento_dir
    """
    line = line.strip()
    if not line:
        raise ValueError("linha vazia")

    parts = line.split()
    if len(parts) < 11:  # 2 (data/hora) + 8 valores
        raise ValueError(f"colunas insuficientes: {len(parts)} -> {line}")

    date_str = parts[0]      # MM/DD/YYYY
    time_str = parts[1]      # HH:MM:SS
    tail = parts[2:]         # os demais campos (>=8)

    if len(tail) < 9:
        raise ValueError(f"esperava ao menos 9 colunas após data/hora, recebi {len(tail)} -> {line}")

    try:
        dt = datetime.strptime(f"{date_str} {time_str}", "%m/%d/%Y %H:%M:%S")
    except Exception as e:
        raise ValueError(f"data/hora inválidas: {e} -> {date_str} {time_str}")

    ymd = dt.strftime("%Y%m%d")
    dt_iso = dt.strftime("%Y-%m-%dT%H:%M:%S")

    def parse_coord(tok):
        if tok == ASTERISKS:
            return ASTERISKS
        try:
            return float(tok)
        except:
            return ASTERISKS

    lat = parse_coord(tail[0])
    lon = parse_coord(tail[1])

    def to_float(tok, name):
        try:
            return float(tok)
        except Exception:
            raise ValueError(f"valor inválido em '{name}': {tok}")

    temp_c     = to_float(tail[2], "temperatura")
    ur         = to_float(tail[3], "umidade_relativa")
    press      = to_float(tail[4], "pressao")
    rad        = to_float(tail[5], "radiacao")
    vento_int  = to_float(tail[6], "vento_intensidade")
    vento_dir  = to_float(tail[7], "vento_direcao")

    # Se houver extras, ignoramos mas avisamos uma vez por linha
    if len(tail) > 8:
        extras = " ".join(tail[8:])
        sys.stderr.write(f"[INFO] ignorando tokens extras: {extras}\n")

    return {
        "dt_iso": dt_iso,
        "ymd": ymd,
        "lat": lat,
        "lon": lon,
        "temp_c": temp_c,
        "ur": ur,
        "press": press,
        "rad": rad,
        "vento_int": vento_int,
        "vento_dir": vento_dir,
    }

def ensure_writer(base_dir, ymd):
    os.makedirs(base_dir, exist_ok=True)
    filepath = os.path.join(base_dir, f"{ymd}.csv")
    file_exists = os.path.exists(filepath)

    f = open(filepath, "a", newline="", encoding="utf-8")
    w = csv.writer(f)
    if not file_exists or os.path.getsize(filepath) == 0:
        w.writerow([
            "datetime_iso", "lat", "lon",
            "temp_C", "umid_rel_%", "press", "radiacao",
            "vento_intensidade", "vento_direcao",
        ])
    return f, w, filepath

def stitch_and_parse(prev_tail, line):
    line = line.strip()
    if not line:
        return None, prev_tail

    # Caso 1: já é uma linha completa
    try:
        rec = parse_line(line)
        return rec, ""
    except ValueError as e:
        # Se for por "colunas insuficientes", tentamos concatenar com o tail anterior
        msg = str(e)
        if msg.startswith("colunas insuficientes") or "esperava 9 colunas" in msg:
            if prev_tail:
                combo = (prev_tail + " " + line).strip()
                try:
                    rec = parse_line(combo)
                    return rec, ""
                except Exception:
                    # Ainda ruim: guardamos o maior dos dois como prev_tail
                    # Heurística: se começar com data, é o começo; senão é o fim
                    if line[:2].isdigit() and "/" in line[:3]:
                        return None, line
                    else:
                        return None, prev_tail + " " + line
            else:
                # não havia tail anterior, armazenamos esta como tail
                return None, line
        else:
            # outro erro (ex.: data ruim) -> propaga
            raise

def main():



    ap = argparse.ArgumentParser(description="Logger de dados Arduino -> CSV diário (nomeado pela data do Arduino). (fix: leitura estável)")
    ap.add_argument("--port", "-p", default="/dev/ttyACM0", help="Porta serial (default: /dev/ttyACM0)")
    ap.add_argument("--baud", "-b", type=int, default=38400, help="Baud rate (default: 38400)")
    ap.add_argument("--outdir", "-o", default="logs", help="Diretório de saída (default: logs)")
    ap.add_argument("--timeout", type=float, default=None, help="Timeout em segundos (default: None=blocking)")
    args = ap.parse_args()

    # Abrimos em modo bloqueante para evitar linhas cortadas por timeout
    try:
        ser = serial.Serial(args.port, args.baud, timeout=args.timeout)
    except Exception as e:
        print(f"[ERRO] Não consegui abrir {args.port} @ {args.baud}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Lendo de {args.port} @ {args.baud} baud (Ctrl+C para sair) ...")

    current_ymd = None
    f = None
    w = None
    filepath = None
    tail_buf = ""  # guarda metade de linha se vier quebrada

    try:
        while True:
            raw = ser.readline()  # bloqueia até '\\n' quando timeout=None
            if not raw:
                continue

            text = raw.decode(errors="ignore").replace("\r", "").strip()
            if not text:
                continue
            print(f"[SERIAL] {text}")  # mostra no console a linha crua capturada

            try:
                rec, tail_buf = stitch_and_parse(tail_buf, text)
                if rec is None:
                    # ainda não temos uma linha completa; esperar a próxima iteração
                    continue
            except Exception as e:
                sys.stderr.write(f"[WARN] {e}\n")
                continue

            # gira arquivo se mudar o dia
            if rec["ymd"] != current_ymd or w is None:
                if f:
                    f.flush()
                    f.close()
                f, w, filepath = ensure_writer(args.outdir, rec["ymd"])
                current_ymd = rec["ymd"]
                print(f"[INFO] gravando em: {filepath}")

            lat = rec["lat"]
            lon = rec["lon"]
            row = [
                rec["dt_iso"],
                lat if isinstance(lat, str) else f"{lat:.6f}",
                lon if isinstance(lon, str) else f"{lon:.6f}",
                f"{rec['temp_c']:.2f}",
                f"{rec['ur']:.2f}",
                f"{rec['press']:.2f}",
                f"{rec['rad']:.2f}",
                f"{rec['vento_int']:.2f}",
                f"{rec['vento_dir']:.2f}",
            ]
            w.writerow(row)
            f.flush()
            print(f"[CSV] gravado: {row}")
    except KeyboardInterrupt:
        print("\n[OK] Encerrando pelo usuário.")

    finally:
        try:
            if f:
                f.flush()
                f.close()
        except:
            pass
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    main()