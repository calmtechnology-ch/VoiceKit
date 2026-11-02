import serial
import time
import re

PORT = "COM4"
BAUD = 115200

def test_turn():
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    ser.dtr = False
    ser.rts = False
    print(f"Ouvert {PORT}")
    time.sleep(1)

    # Verifier si READY
    ser.write(b'D\n')
    ready = False
    t0 = time.time()
    while time.time() - t0 < 10:
        raw = ser.readline()
        if raw:
            line = raw.decode('utf-8', errors='replace').strip()
            print(f"  {line}")
            if "PTT: READY" in line or "PRET" in line:
                ready = True
                break

    if not ready:
        print("Moteur non pret, STOP...")
        ser.write(b'S')
        time.sleep(1)

    print("\n--- Demarrage Prise de Parole (PARLER / 'P') ---")
    ser.write(b'P')
    t_start = time.time()
    
    # Maintenir 2 secondes
    time.sleep(2.0)

    print("--- Fin de Parole (RELACHER / 'R') ---")
    ser.write(b'R')

    assistant_text = ""
    t_wait = time.time()
    while time.time() - t_wait < 15:
        raw = ser.readline()
        if raw:
            line = raw.decode('utf-8', errors='replace').strip()
            print(f"  {line}")
            if "[OPENAI TEXT]" in line:
                m = re.search(r'\[OPENAI TEXT\]\s*(.*)', line)
                if m:
                    assistant_text = m.group(1).strip()
            if "Tour #" in line and "termine" in line:
                print(f"\n=> SUCCES TOUR ! Reponse : \"{assistant_text}\"")
                break

    ser.close()

if __name__ == "__main__":
    test_turn()

