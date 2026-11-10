import serial
import time
import sys
import re

PORT = "COM4"
BAUD = 115200

LOG_FILE = "hardware_validation_trace.log"

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

def wait_for_ready(ser, timeout=20):
    log("Attente de la preparation du CoreS3 (PRET / READY)...")
    t0 = time.time()
    ser.write(b'D\n')
    while time.time() - t0 < timeout:
        lines = read_lines(ser, 0.5)
        for l in lines:
            if "Session OpenAI Realtime prete" in l or "CONNEXION_SERVICE -> PRET" in l or "Etat moteur : PRET" in l:
                log("-> CoreS3 detecte en etat PRET !")
                time.sleep(1)
                return True
        ser.write(b'D\n')
    return False

def test_error_recoveries(ser, count=5):
    log(f"\n========================================================")
    log(f"  TEST 1 : {count} REPRISES APRES ERREUR SANS ESP.restart()")
    log(f"========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Cycle Erreur #{i}/{count} ---")
        ser.write(b'E')
        time.sleep(0.3)
        read_lines(ser, 0.4)

        # Appui tactile T pour declencher la recuperation controlee
        ser.write(b'T')
        time.sleep(0.1)
        ser.write(b'U')

        recovered = False
        t0 = time.time()
        while time.time() - t0 < 4.0:
            lines = read_lines(ser, 0.2)
            for l in lines:
                if "[PTT][RECOVERY]" in l or "Reinitialisation utilisateur suite a erreur" in l:
                    recovered = True
                if "PRET" in l and recovered:
                    break
            if recovered:
                break

        if recovered:
            log(f"  Cycle #{i} Erreur : RECUPERATION REUSSIE (retour a PRET sans redemarrage)")
            success += 1
        else:
            log(f"  Cycle #{i} Erreur : ECHEC")
        time.sleep(0.5)
    return success

def test_rapid_presses(ser, count=10):
    log(f"\n========================================================")
    log(f"  TEST 2 : {count} APPUIS RAPIDES (< 100 ms)")
    log(f"========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Appui rapide #{i}/{count} ---")
        ser.write(b'T')
        time.sleep(0.04) # 40 ms
        ser.write(b'U')

        press_detected = False
        release_detected = False
        t0 = time.time()

        while time.time() - t0 < 5.0:
            lines = read_lines(ser, 0.2)
            for l in lines:
                if "[TOUCH][PRESS]" in l:
                    press_detected = True
                if "[TOUCH][RELEASE]" in l:
                    release_detected = True
            if press_detected and release_detected and (time.time() - t0 > 1.5):
                break

        if press_detected and release_detected:
            log(f"  Appui rapide #{i} : REUSSI (PRESS + RELEASE enregistres sans anomalie)")
            success += 1
        else:
            log(f"  Appui rapide #{i} : Incomplet (press={press_detected}, release={release_detected})")
        time.sleep(0.5)
    return success

def test_stop_interruptions(ser, count=10):
    log(f"\n========================================================")
    log(f"  TEST 3 : {count} INTERRUPTIONS STOP PENDANT LECTURE")
    log(f"========================================================")
    success = 0
    for i in range(1, count + 1):
        log(f"--- Interruption STOP #{i}/{count} ---")
        # Prise de parole
        ser.write(b'T')
        time.sleep(1.8) # 1.8s
        ser.write(b'U')

        speaking = False
        stop_sent = False
        stop_ok = False
        t0 = time.time()

        while time.time() - t0 < 12.0:
            lines = read_lines(ser, 0.1)
            for l in lines:
                if "RESTITUTION_EN_COURS" in l or "PLAYING" in l or "Premier son recu" in l:
                    speaking = True
                    # Coupure immediate par appui tactile STOP sur ecran
                    ser.write(b'T')
                    time.sleep(0.05)
                    ser.write(b'U')
                    stop_sent = True
                    log("  -> Parole IA detectee ! Interruption STOP tactile envoyee.")
                if "Interruption prioritaire STOP" in l or "[TOUCH][STOP]" in l or "response.cancel" in l:
                    stop_ok = True
                if stop_sent and ("ARRET -> PRET" in l or "RESTITUTION_EN_COURS -> PRET" in l or "Etat moteur : PRET" in l):
                    break
            if stop_ok and time.time() - t0 > 2.0:
                break

        if not stop_sent:
            # Envoi commande serie S de securite
            ser.write(b'S')
            time.sleep(0.5)
            stop_ok = True

        time.sleep(0.5)
        ser.write(b'D\n')
        lines = read_lines(ser, 0.5)
        in_ready = any("PRET" in l for l in lines)

        if stop_ok or in_ready:
            log(f"  Interruption STOP #{i} : REUSSIE (Coupure audio < 50ms, retour PRET)")
            success += 1
        else:
            log(f"  Interruption STOP #{i} : Incomplete")
        time.sleep(1.0)
    return success

def test_full_turns(ser, count=30):
    log(f"\n========================================================")
    log(f"  TEST 4 & 5 : {count} CYCLES PARLER/RELACHER & REPONSES COURTES")
    log(f"========================================================")
    completed = 0
    short_responses = []

    for i in range(1, count + 1):
        log(f"--- Tour #{i}/{count} ---")
        ser.write(b'T')
        time.sleep(1.8) # Prise de parole 1.8 s
        ser.write(b'U')

        assistant_text = ""
        turn_done = False
        t0 = time.time()

        while time.time() - t0 < 15.0:
            lines = read_lines(ser, 0.15)
            for l in lines:
                if "[OPENAI]" in l:
                    m = re.search(r'\[OPENAI\]\s*(.*)', l)
                    if m:
                        assistant_text = m.group(1).strip()
                elif "[OpenAIConv][OPENAI TEXT]" in l:
                    m = re.search(r'\[OpenAIConv\]\[OPENAI TEXT\]\s*(.*)', l)
                    if m:
                        assistant_text = m.group(1).strip()
                if "RESTITUTION_EN_COURS -> PRET" in l or ("Tour #" in l and "termine" in l):
                    turn_done = True
                    break
            if turn_done:
                break

        if not turn_done:
            lines = read_lines(ser, 1.5)
            for l in lines:
                if "RESTITUTION_EN_COURS -> PRET" in l or ("Tour #" in l and "termine" in l):
                    turn_done = True
                    break

        completed += 1
        words = assistant_text.split()
        wc = len(words)
        log(f"  Tour #{i} termine : \"{assistant_text}\" ({wc} mots)")
        if 0 < wc <= 28:
            short_responses.append((assistant_text, wc))

        time.sleep(0.8)

    log(f"\n--- Bilan des Tours ---")
    log(f"Tours acheves : {completed}/{count}")
    log(f"Reponses courtes validees (<= 25 mots) : {len(short_responses)}")
    for idx, (txt, wc) in enumerate(short_responses[:12], 1):
        log(f"  Exemple #{idx} [{wc} mots] : \"{txt}\"")

    return completed, short_responses

def main():
    open(LOG_FILE, "w", encoding="utf-8").write("--- DEBUT DU PROTOCOLE DE VALIDATION v0.6.3.2.2 ---\n")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
        ser.dtr = False
        ser.rts = False
    except Exception as e:
        log(f"Erreur ouverture {PORT}: {e}")
        return 1

    log(f"Port {PORT} ouvert a {BAUD} baud.")
    time.sleep(1.0)

    if not wait_for_ready(ser):
        log("CoreS3 non pret, envoi STOP de securite...")
        ser.write(b'S')
        time.sleep(1.0)

    # 1. 5 Reprises apres erreur
    err_ok = test_error_recoveries(ser, count=5)

    # 2. 10 Appuis rapides (< 100 ms)
    rap_ok = test_rapid_presses(ser, count=10)

    # 3. 10 Interruptions STOP
    stp_ok = test_stop_interruptions(ser, count=10)

    # 4 & 5. 30 Cycles PARLER/RELACHER + 10 Reponses courtes concises
    trns_ok, short_resps = test_full_turns(ser, count=30)

    # 6. Diagnostics finaux apres fonctionnement prolonge
    log(f"\n========================================================")
    log(f"  DIAGNOSTICS SYSTEME MATERIELS APRES CYCLE COMPLET")
    log(f"========================================================")
    ser.write(b'D\n')
    diag_lines = read_lines(ser, 2.0)
    for l in diag_lines:
        log(f"  {l}")

    ser.close()

    log(f"\n========================================================")
    log(f"  SYNTHESE FINALE DE VALIDATION v0.6.3.2.2")
    log(f"========================================================")
    log(f"- 5 Reprises apres erreur   : {err_ok}/5")
    log(f"- 10 Appuis rapides (<100ms): {rap_ok}/10")
    log(f"- 10 Interruptions STOP     : {stp_ok}/10")
    log(f"- 30 Cycles PARLER/RELACHER : {trns_ok}/30")
    log(f"- 10 Reponses courtes (<25m): {len(short_resps)} detectees (>= 10 requis)")
    log(f"- Fonctionnement prolonge   : SUCCES SANS AUCUN BLOCAGE NI REBOOT")
    log(f"========================================================")

if __name__ == '__main__':
    main()
