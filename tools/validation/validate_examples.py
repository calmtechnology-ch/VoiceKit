#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VoiceKit v0.6.7.1 — Script de Validation Automatisée des Exemples
Vérifie la conformité, la structure de fichiers, la configuration PlatformIO
et effectue la compilation de tous les exemples officiels.
"""

import os
import sys
import time
import shutil
import argparse
import subprocess
from pathlib import Path

# 12 Exemples officiels conservés pour VoiceKit v0.6.7.1
OFFICIAL_EXAMPLES = [
    {
        "name": "MinimalTTS",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Synthèse vocale unitaire simple (TTS 24 kHz)",
    },
    {
        "name": "MinimalSTT",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Reconnaissance vocale Push-to-Talk fiabilisée",
    },
    {
        "name": "MinimalConversation",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Dialogue bidirectionnel natif avec Google Gemini Live",
    },
    {
        "name": "MinimalTTS_Arduino",
        "type": "arduino",
        "requires_secrets": False,
        "entry_point": "MinimalTTS_Arduino.ino",
        "description": "Compatibilité IDE Arduino officiel autonome",
    },
    {
        "name": "WakeWordFoundation",
        "type": "platformio",
        "requires_secrets": False,
        "entry_point": "src/main.cpp",
        "description": "Détection locale hors ligne « Bonjour ESP » (WakeNet)",
    },
    {
        "name": "WakeWordConversation",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Cycle complet mains libres Wake Word -> Conversation",
    },
    {
        "name": "LocalToolRegistryDemo",
        "type": "platformio",
        "requires_secrets": False,
        "entry_point": "src/main.cpp",
        "description": "Registre local d'outils matériels sécurisés hors ligne",
    },
    {
        "name": "VoiceDiagnostics",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Tableau de bord de télémétrie et métriques système",
    },
    {
        "name": "VoiceSettingsDemo",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Démonstrateur multilingue, profils vocaux et chaîne UTF-8",
    },
    {
        "name": "OpenAIConversationLVGL",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Référence OpenAI Realtime avec interface graphique LVGL 9",
    },
    {
        "name": "IntegratedVoiceDemo",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Exemple principal : orchestrateur IntegratedVoicePipeline",
    },
    {
        "name": "TwoClickConversationLVGL",
        "type": "platformio",
        "requires_secrets": True,
        "entry_point": "src/main.cpp",
        "description": "Exemple de référence interaction vocale 2 clics (PTT & VAD holistique) avec LVGL 9",
    },
]

MANDATORY_README_SECTIONS = [
    "Objectif",
    "Fonctionnalit",
    "Matériel requis",
    "Configuration",
    "Compilation",
    "Téléversement",
    "Mode d'utilisation",
    "Tests à effectuer",
    "Résultats attendus",
    "Limitations connues",
]


def check_readme_sections(readme_path: Path):
    if not readme_path.exists():
        return False, "README.md manquant"
    try:
        content = readme_path.read_text(encoding="utf-8")
    except Exception as e:
        return False, f"Erreur lecture README: {e}"

    missing = []
    for sec in MANDATORY_README_SECTIONS:
        if sec.lower() not in content.lower():
            missing.append(sec)

    if missing:
        return False, f"Sections manquantes: {', '.join(missing)}"
    return True, "10/10 sections conformes"


def check_platformio_ini(ini_path: Path):
    if not ini_path.exists():
        return False, "platformio.ini manquant"
    try:
        content = ini_path.read_text(encoding="utf-8")
    except Exception as e:
        return False, f"Erreur lecture platformio.ini: {e}"

    issues = []
    # Vérification pas de upload_port forcé non commenté
    for line in content.splitlines():
        trimmed = line.strip()
        if trimmed.startswith("upload_port") and not trimmed.startswith(";"):
            issues.append(f"upload_port imposé: '{trimmed}'")

    if "board = m5stack-cores3" not in content:
        issues.append("board != m5stack-cores3")
    if "-std=gnu++17" not in content:
        issues.append("C++17 flag (-std=gnu++17) manquant")

    if issues:
        return False, "; ".join(issues)
    return True, "platformio.ini conforme (CoreS3, C++17, sans port imposé)"


def validate_example_files(root_dir: Path, ex: dict):
    ex_dir = root_dir / "examples" / ex["name"]
    res = {
        "name": ex["name"],
        "type": ex["type"],
        "dir_exists": ex_dir.is_dir(),
        "entry_point": (ex_dir / ex["entry_point"]).exists(),
        "readme_ok": False,
        "readme_msg": "",
        "config_ok": False,
        "config_msg": "",
        "secrets_ok": True,
        "secrets_msg": "",
    }

    if not res["dir_exists"]:
        return res

    readme_ok, readme_msg = check_readme_sections(ex_dir / "README.md")
    res["readme_ok"] = readme_ok
    res["readme_msg"] = readme_msg

    if ex["type"] == "platformio":
        cfg_ok, cfg_msg = check_platformio_ini(ex_dir / "platformio.ini")
        res["config_ok"] = cfg_ok
        res["config_msg"] = cfg_msg

        if ex["requires_secrets"]:
            has_example = (ex_dir / "include" / "secrets.example.h").exists()
            if not has_example:
                res["secrets_ok"] = False
                res["secrets_msg"] = "secrets.example.h manquant"
            else:
                res["secrets_msg"] = "secrets.example.h présent"
        else:
            res["secrets_msg"] = "N/A (hors ligne)"
    else:
        res["config_ok"] = True
        res["config_msg"] = "Arduino IDE (.ino autonome)"
        res["secrets_msg"] = "Constantes dans le .ino"

    return res


def compile_platformio_example(root_dir: Path, ex_name: str, pio_cmd: str):
    ex_dir = root_dir / "examples" / ex_name
    cmd = [pio_cmd, "run", "-d", str(ex_dir)]

    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
        )
        duration = time.time() - t0
        output = proc.stdout

        # Analyse succincte
        success = (proc.returncode == 0)
        flash_info = ""
        ram_info = ""
        for line in output.splitlines():
            if "Flash:" in line:
                flash_info = line.strip()
            elif "RAM:" in line:
                ram_info = line.strip()

        return {
            "success": success,
            "duration_s": round(duration, 2),
            "flash": flash_info,
            "ram": ram_info,
            "returncode": proc.returncode,
            "error_tail": "\n".join(output.splitlines()[-15:]) if not success else "",
        }
    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "duration_s": 300,
            "flash": "",
            "ram": "",
            "returncode": -1,
            "error_tail": "Timeout de compilation (300s dépassé)",
        }
    except Exception as e:
        return {
            "success": False,
            "duration_s": round(time.time() - t0, 2),
            "flash": "",
            "ram": "",
            "returncode": -1,
            "error_tail": str(e),
        }


def main():
    parser = argparse.ArgumentParser(description="VoiceKit v0.6.7.1 Examples Validator")
    parser.add_argument("--check-only", action="store_true", help="Vérifie uniquement les fichiers sans compiler")
    parser.add_argument("--example", type=str, default="", help="Nom d'un exemple spécifique à valider")
    parser.add_argument("--output", type=str, default="", help="Fichier de sortie du rapport Markdown")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent.parent
    os.chdir(root_dir)

    print("================================================================================")
    print("  VOICEKIT v0.6.7.1 — VALIDATION OFFICIELLE DES EXEMPLES")
    print(f"  Répertoire racine : {root_dir}")
    print("================================================================================\n")

    # Détection des outils
    pio_path = shutil.which("pio") or shutil.which("platformio")
    arduino_cli_path = shutil.which("arduino-cli")

    print(f"[*] PlatformIO CLI : {pio_path or 'NON TROUVÉ'}")
    print(f"[*] Arduino CLI    : {arduino_cli_path or 'NON DISPONIBLE (marqué NOT TESTED pour .ino)'}\n")

    examples_to_run = OFFICIAL_EXAMPLES
    if args.example:
        examples_to_run = [e for e in OFFICIAL_EXAMPLES if e["name"].lower() == args.example.lower()]
        if not examples_to_run:
            print(f"[ERREUR] Aucun exemple trouvé avec le nom '{args.example}'.")
            sys.exit(1)

    # 1. Vérification des fichiers et configurations
    print(f"--- 1. AUDIT DE STRUCTURE ET CONFORMITÉ DES {len(examples_to_run)} EXEMPLES ---")
    file_results = []
    all_files_ok = True
    for ex in examples_to_run:
        res = validate_example_files(root_dir, ex)
        file_results.append(res)
        status_char = "OK" if (res["dir_exists"] and res["entry_point"] and res["readme_ok"] and res["config_ok"] and res["secrets_ok"]) else "FAIL"
        if status_char == "FAIL":
            all_files_ok = False
        print(f"[{status_char:4}] {ex['name']:<24} | README: {res['readme_msg']} | Conf: {res['config_msg']} | Secr: {res['secrets_msg']}")

    print()

    # 2. Compilation PlatformIO
    build_results = {}
    if not args.check_only:
        print("--- 2. COMPILATION DES EXEMPLES PLATFORMIO ---")
        if not pio_path:
            print("[AVERTISSEMENT] PlatformIO CLI indisponible. Compilation ignorée.")
        else:
            for ex in examples_to_run:
                if ex["type"] == "platformio":
                    print(f"[*] Compilation de {ex['name']}...", end="", flush=True)
                    bres = compile_platformio_example(root_dir, ex["name"], pio_path)
                    build_results[ex["name"]] = bres
                    if bres["success"]:
                        print(f" SUCCÈS ({bres['duration_s']}s) | {bres['flash']} | {bres['ram']}")
                    else:
                        print(f" ÉCHEC ({bres['duration_s']}s)")
                        print("    Dernières lignes d'erreur :")
                        for errline in bres["error_tail"].splitlines():
                            print(f"    | {errline}")
                elif ex["type"] == "arduino":
                    if arduino_cli_path:
                        print(f"[*] Validation de {ex['name']} via Arduino CLI... (NON TESTÉ - config cible requise)")
                    else:
                        print(f"[-] {ex['name']} : Traité séparément (Arduino CLI absent -> NOT TESTED)")
                        build_results[ex["name"]] = {
                            "success": None,
                            "status": "NOT TESTED",
                            "note": "Arduino CLI non disponible dans l'environnement courant",
                        }

    # 3. Bilan Récapitulatif
    print("\n================================================================================")
    print("  RAPPORT RÉCAPITULATIF DE VALIDATION DES EXEMPLES v0.6.7.1")
    print("================================================================================")
    print(f"{'Exemple':<26} | {'Type':<11} | {'Fichiers':<9} | {'README':<8} | {'Compilation':<15}")
    print("-" * 80)

    total_pio = 0
    success_pio = 0

    for ex in examples_to_run:
        fres = next(f for f in file_results if f["name"] == ex["name"])
        f_ok = fres["dir_exists"] and fres["entry_point"] and fres["config_ok"] and fres["secrets_ok"]
        r_ok = fres["readme_ok"]

        bres = build_results.get(ex["name"], {})
        if ex["type"] == "platformio":
            total_pio += 1
            if bres.get("success") is True:
                success_pio += 1
                b_str = f"PASS ({bres['duration_s']}s)"
            elif bres.get("success") is False:
                b_str = "FAIL"
            else:
                b_str = "NON EXÉCUTÉ"
        else:
            b_str = "NOT TESTED (CLI)"

        print(f"{ex['name']:<26} | {ex['type']:<11} | {'OK' if f_ok else 'FAIL':<9} | {'10/10' if r_ok else 'FAIL':<8} | {b_str:<15}")

    print("-" * 80)
    print(f"Exemples PlatformIO compilés avec succès : {success_pio}/{total_pio}")

    # Export markdown optionnel
    if args.output:
        out_p = Path(args.output)
        md_lines = [
            "# Rapport de Validation des Exemples — VoiceKit v0.6.6\n",
            f"Date d'exécution : {time.strftime('%Y-%m-%d %H:%M:%S')}\n",
            "| Exemple | Type | Fichiers | Documentation (10/10) | Compilation PlatformIO |",
            "|---|---|---|---|---|",
        ]
        for ex in examples_to_run:
            fres = next(f for f in file_results if f["name"] == ex["name"])
            f_ok = fres["dir_exists"] and fres["entry_point"] and fres["config_ok"] and fres["secrets_ok"]
            r_ok = fres["readme_ok"]
            bres = build_results.get(ex["name"], {})
            if ex["type"] == "platformio":
                b_str = f"PASS ({bres.get('duration_s', 0)}s)" if bres.get("success") else "FAIL"
            else:
                b_str = "NOT TESTED (Arduino CLI absent)"
            md_lines.append(f"| `{ex['name']}` | {ex['type']} | {'✅ OK' if f_ok else '❌ FAIL'} | {'✅ 10/10' if r_ok else '❌ Incomplet'} | {b_str} |")

        out_p.write_text("\n".join(md_lines), encoding="utf-8")
        print(f"\n[+] Rapport exporté dans : {out_p.resolve()}")

    if total_pio > 0 and success_pio == total_pio and all_files_ok:
        print("\n>>> VALIDATION LOGICIELLE ET COMPILATION : 100% SUCCÈS <<<")
        return 0
    elif args.check_only and all_files_ok:
        print("\n>>> AUDIT DE STRUCTURE ET DOCUMENTATION : 100% SUCCÈS <<<")
        return 0
    else:
        print("\n>>> AVERTISSEMENT : CERTAINES VALIDATIONS SONT EN ÉCHEC OU NON TESTÉES <<<")
        return 1


if __name__ == "__main__":
    sys.exit(main())

