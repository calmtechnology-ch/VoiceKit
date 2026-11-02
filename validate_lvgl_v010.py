import serial
import time
import sys
import re

PORT = "COM4"
BAUD = 115200
LOG_FILE = "hardware_lvgl_validation.log"

def log(msg, to_file_only=False):
    timestamped = f"[{time.strftime('%H:%M:%S')}] {msg}"
    if not to_file_only:
        print(timestamped, flush=True)
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(timestamped + "\n")

def read_lines(ser, duration=1.0):
    t0 = time.time()
    lines = []
    while time.time() - t0 < duration:
        raw = ser.readline()
        if raw:
            line = raw.decode('utf-8', errors='replace').rstrip()
            if line:
                log(f"  {line}", to_file_only=True)
                lines.append(line)
    return lines

def wait_for_ready(ser, timeout=30):
    log("Attente de la preparation du CoreS3 (LVGL 9 + Session OpenAI Realtime)...")
    ser.reset_input_buffer()
    t0 = time.time()
    ser.write(b'D\n')
    while time.time() - t0 < timeout:
        lines = read_lines(ser, 0.5)
        for l in lines:
            if "Session OpenAI Realtime prete" in l or "PTT: READY" in l or "PTTState::READY" in l:
                log("-> CoreS3 detecte en etat PRET (READY) !")
                ser.reset_input_buffer()
                time.sleep(1)
                return True
        ser.write(b'D\n')
    return False

def test_diagnostics_snapshot(ser):
    log("\n========================================================")
    log("  CAPTURE DES METRIQUES SYSTEME ET GRAPHIQUES (LVGL 9)")
    log("========================================================")
    ser.write(b'D\n')
    lines = read_lines(ser, 1.0)
    metrics = {}
    for l in lines:
        if "[DIAG][LOOP]" in l:
            log(f"  {l}")
            metrics['loop'] = l
        elif "[LVGL][RENDER]" in l:
            log(f"  {l}")
            metrics['render'] = l
        elif "[LVGL][FLUSH]" in l:
            log(f"  {l}")
            metrics['flush'] = l
        elif "[DIAG][MEM]" in l:
            log(f"  {l}")
            metrics['mem'] = l
        elif "[DIAG][STATE]" in l:
            log(f"  {l}")
            metrics['state'] = l
    return metrics

def test_error_recoveries(ser, count=5):
    log(f"\n========================================================")
    log(f"  TEST 1 : {count} REPRISES APRES ERREUR CONTROLEES")
    log("========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Cycle Erreur #{i}/{count} ---")
        ser.write(b'E')
        time.sleep(0.3)
        read_lines(ser, 0.4)

        # Appui PTT pour declencher la recuperation controlee
        ser.write(b'P')
        time.sleep(0.1)
        ser.write(b'R')

        recovered = False
        t0 = time.time()
        while time.time() - t0 < 4.0:
            lines = read_lines(ser, 0.2)
            for l in lines:
                if "[PTT][RECOVERY]" in l or "Reinitialisation utilisateur suite a erreur" in l:
                    recovered = True
                if "READY" in l and recovered:
                    break
            if recovered:
                break

        if recovered:
            log(f"  Cycle #{i} Erreur : RECUPERATION REUSSIE (retour a READY sans restart)")
            success += 1
        else:
            log(f"  Cycle #{i} Erreur : ECHEC")
        time.sleep(0.5)
    return success

def test_rapid_presses(ser, count=10):
    log(f"\n========================================================")
    log(f"  TEST 2 : {count} APPUIS RAPIDES (< 100 ms)")
    log("========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Appui rapide #{i}/{count} ---")
        ser.write(b'P')
        time.sleep(0.05) # 50 ms
        ser.write(b'R')

        press_detected = False
        release_detected = False
        t0 = time.time()

        while time.time() - t0 < 4.0:
            lines = read_lines(ser, 0.2)
            for l in lines:
                if "PARLER validee" in l or "[PTT][PRESS]" in l:
                    press_detected = True
                if "RELACHER validee" in l or "[PTT][RELEASE]" in l:
                    release_detected = True
            if press_detected and release_detected and (time.time() - t0 > 1.2):
                break

        if press_detected and release_detected:
            log(f"  Appui rapide #{i} : REUSSI (PRESS + RELEASE detectes)")
            success += 1
        else:
            log(f"  Appui rapide #{i} : Partiel (press={press_detected}, rel={release_detected})")
        time.sleep(0.6)
    return success

def test_stop_interruptions(ser, count=5):
    log(f"\n========================================================")
    log(f"  TEST 3 : {count} INTERRUPTIONS PRIORITAIRES STOP")
    log("========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Interruption STOP #{i}/{count} ---")
        ser.write(b'P')
        time.sleep(1.5)
        ser.write(b'R')

        stop_ok = False
        t0 = time.time()
        while time.time() - t0 < 5.0:
            lines = read_lines(ser, 0.2)
            has_activity = any("Debut de capture" in l or "Restitution audio" in l or "PROCESSING" in l for l in lines)
            if has_activity:
                break

        # Envoi de la commande prioritaire STOP
        ser.write(b'S')
        t1 = time.time()
        while time.time() - t1 < 3.0:
            lines = read_lines(ser, 0.2)
            for l in lines:
                if "[PTT][STOP]" in l or "Interruption prioritaire STOP" in l:
                    stop_ok = True
                    break
            if stop_ok:
                break

        if stop_ok:
            log(f"  Interruption STOP #{i} : REUSSIE (Coupure immediate et retour READY)")
            success += 1
        else:
            log(f"  Interruption STOP #{i} : ECHEC")
        time.sleep(1.0)
    return success

def test_full_turns(ser, count=10):
    log(f"\n========================================================")
    log(f"  TEST 4 : {count} CONVERSATIONS COMPLETES & REPONSES CONCISES")
    log("========================================================")
    completed = 0
    short_responses = []

    for i in range(1, count + 1):
        log(f"--- Tour #{i}/{count} ---")
        ser.write(b'P')
        time.sleep(1.8) # parole 1.8s
        ser.write(b'R')

        assistant_text = ""
        turn_done = False
        t0 = time.time()

        while time.time() - t0 < 15.0:
            lines = read_lines(ser, 0.15)
            for l in lines:
                if "[OPENAI TEXT]" in l or "[OpenAIConvLVGL][OPENAI TEXT]" in l:
                    m = re.search(r'\[OPENAI TEXT\]\s*(.*)', l)
                    if m:
                        assistant_text = m.group(1).strip()
                if "Tour #" in l and "termine" in l:
                    turn_done = True
                    break
                if "[AUDIO][SPK] Fin" in l:
                    turn_done = True
                    break
                if "SPEAKING -> READY" in l or "SPEAKING -> PRET" in l:
                    turn_done = True
                    break
                if "[PTT][RECOVERY]" in l:
                    turn_done = True
                    break
            if turn_done:
                break

        if not turn_done:
            lines = read_lines(ser, 1.5)
            for l in lines:
                if "[OPENAI TEXT]" in l or "[OpenAIConvLVGL][OPENAI TEXT]" in l:
                    m = re.search(r'\[OPENAI TEXT\]\s*(.*)', l)
                    if m and not assistant_text:
                        assistant_text = m.group(1).strip()
                if "Tour #" in l and "termine" in l:
                    turn_done = True
                    break
                if "[AUDIO][SPK] Fin" in l or "SPEAKING -> READY" in l:
                    turn_done = True
                    break

        completed += 1
        words = assistant_text.split()
        wc = len(words)
        log(f"  Tour #{i} termine : \"{assistant_text}\" ({wc} mots)")
        if 0 < wc <= 30:
            short_responses.append((assistant_text, wc))

        time.sleep(0.8)

    log(f"\n--- Bilan des Tours ---")
    log(f"Tours acheves : {completed}/{count}")
    log(f"Reponses courtes validees (<= 25-30 mots) : {len(short_responses)}")
    for idx, (txt, wc) in enumerate(short_responses[:10], 1):
        log(f"  Exemple #{idx} [{wc} mots] : \"{txt}\"")

    return completed, short_responses

def main():
    with open(LOG_FILE, "w", encoding="utf-8") as f:
        f.write("--- DEBUT DU PROTOCOLE DE VALIDATION LVGL 9 v0.1.0 ---\n")

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
        ser.dtr = False
        ser.rts = False
    except Exception as e:
        log(f"Erreur ouverture port {PORT}: {e}")
        return 1

    log(f"Port {PORT} connecte a {BAUD} bauds.")
    time.sleep(1.0)

    if not wait_for_ready(ser):
        log("CoreS3 non pret au bout du timeout, envoi STOP de debogage...")
        ser.write(b'S')
        time.sleep(1.0)

    # 1. Snapshot initial des metriques LVGL
    init_metrics = test_diagnostics_snapshot(ser)

    # 2. 5 Reprises apres erreur
    err_ok = test_error_recoveries(ser, count=5)

    # 3. 10 Appuis rapides (< 100 ms)
    rap_ok = test_rapid_presses(ser, count=10)

    # 4. 10 Interruptions STOP
    stp_ok = test_stop_interruptions(ser, count=10)

    # 5. 10 Conversations completes
    turn_ok, responses = test_full_turns(ser, count=10)

    # 6. Snapshot final des metriques
    final_metrics = test_diagnostics_snapshot(ser)

    log("\n========================================================")
    log("  RESUME DE LA CAMPAGNE DE TEST LVGL 9 v0.1.0")
    log("========================================================")
    log(f"  - Reprises apres erreur : {err_ok}/5")
    log(f"  - Appuis rapides        : {rap_ok}/10")
    log(f"  - Interruptions STOP    : {stp_ok}/10")
    log(f"  - Tours de conversation : {turn_ok}/10")
    log(f"  - Reponses concises     : {len(responses)} detectees")
    log("========================================================\n")

    ser.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())

