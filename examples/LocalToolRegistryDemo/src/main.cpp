/**
 * @file main.cpp
 * @brief Exemple LocalToolRegistryDemo — Registre Local d'Outils pour VoiceKit v0.6.4
 * 
 * Fonctionne a 100% hors ligne, sans Wi-Fi ni fournisseur IA.
 * Enregistre, decouvre, valide et execute trois commandes locales reelles :
 * 1. `system.get_status`      : Retourne l'etat materiel du CoreS3 (batterie, heap, ecran, audio)
 * 2. `audio.set_volume`       : Modifie reellement le volume materiel du haut-parleur M5CoreS3 (0-255)
 * 3. `display.set_brightness` : Modifie reellement la luminosite materielle de l'ecran LCD (0-255)
 * 4. `system.factory_reset`   : Commande sensible (SENSITIVE) illustrant le cycle de confirmation explicite
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <ToolRegistry.h>

static ToolRegistry s_registry;

// Variables d'état matériel suivies
static int s_currentVolume = 128;
static int s_currentBrightness = 160;
static String s_lastToken = "";
static String s_lastEventMsg = "Initialise - Pret";

// =============================================================================
// Prototypes et Fonctions Matérielles des Outils
// =============================================================================

/**
 * @brief Commande 1 : system.get_status (READ_ONLY)
 */
static ToolResult handleGetStatus(const ToolArgs&) {
    int batt = M5.Power.getBatteryLevel();
    bool isCharging = M5.Power.isCharging();
    uint32_t heap = esp_get_free_heap_size();
    uint32_t uptime = millis() / 1000;

    String json = "{";
    json += "\"battery\":" + String(batt) + ",";
    json += "\"charging\":" + String(isCharging ? "true" : "false") + ",";
    json += "\"free_heap\":" + String(heap) + ",";
    json += "\"uptime_s\":" + String(uptime) + ",";
    json += "\"volume\":" + String(s_currentVolume) + ",";
    json += "\"brightness\":" + String(s_currentBrightness);
    json += "}";

    return ToolResult::success(json);
}

/**
 * @brief Commande 2 : audio.set_volume (WRITE)
 */
static ToolResult handleSetVolume(const ToolArgs& args) {
    int vol = args.getInt("volume");
    s_currentVolume = vol;

    M5.Speaker.setVolume(vol);
    M5.Speaker.tone(1500, 60); // Bip sonore de confirmation

    return ToolResult::success("Volume haut-parleur regle a " + String(vol) + "/255");
}

/**
 * @brief Commande 3 : display.set_brightness (WRITE)
 */
static ToolResult handleSetBrightness(const ToolArgs& args) {
    int br = args.getInt("brightness");
    s_currentBrightness = br;

    M5.Display.setBrightness(br);

    return ToolResult::success("Luminosite ecran reglee a " + String(br) + "/255");
}

/**
 * @brief Commande 4 (illustration sécurité) : system.factory_reset (SENSITIVE)
 */
static ToolResult handleFactoryReset(const ToolArgs&) {
    M5.Speaker.tone(800, 200);
    return ToolResult::success("Reinitialisation d'usine simulee avec succes apres confirmation");
}

// =============================================================================
// Interface Graphique Locale (Écran CoreS3)
// =============================================================================

static void refreshScreen() {
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);

    // En-tête
    M5.Display.fillRect(0, 0, 320, 30, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("VoiceKit v0.6.4 : Local Tool Registry", 160, 15);

    // Sous-titre
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("100% Hors-Ligne (Sans Wi-Fi / Sans IA)", 10, 36);

    // Panneau État Matériel
    M5.Display.drawRoundRect(10, 56, 300, 80, 4, TFT_LIGHTGREY);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("ETAT DU MATERIEL (CoreS3) :", 20, 64);

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("Volume : " + String(s_currentVolume) + " / 255", 25, 84);
    M5.Display.drawString("Luminosite : " + String(s_currentBrightness) + " / 255", 25, 100);
    M5.Display.drawString("Batterie : " + String(M5.Power.getBatteryLevel()) + "%", 175, 84);
    M5.Display.drawString("Heap libre : " + String(esp_get_free_heap_size()) + " B", 175, 100);

    // Panneau Journal des Événements
    M5.Display.drawRoundRect(10, 144, 300, 86, 4, TFT_LIGHTGREY);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("DERNIER EVENEMENT REGISTRE :", 20, 152);

    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    String line = s_lastEventMsg;
    if (line.length() > 38) line = line.substring(0, 35) + "...";
    M5.Display.drawString(line, 20, 172);

    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString("Menu serie actif : tapez 1 a 8", 20, 202);

    M5.Display.endWrite();
}

// =============================================================================
// Menu Série Interactif
// =============================================================================

static void printMenu() {
    Serial.println("\n=======================================================");
    Serial.println("  VoiceKit v0.6.4 — Local Tool Registry Demo");
    Serial.println("  M5Stack CoreS3 (100% Hors-Ligne / Sans Wi-Fi / Sans IA)");
    Serial.println("=======================================================");
    Serial.println("Menu des actions interactives :");
    Serial.println("  [1] Decouvrir les outils enregistres (Discovery)");
    Serial.println("  [2] Executer 'system.get_status'");
    Serial.println("  [3] Executer 'audio.set_volume' (volume=180)");
    Serial.println("  [4] Executer 'display.set_brightness' (brightness=200)");
    Serial.println("  [5] Test validation : Parametre manquant (audio.set_volume)");
    Serial.println("  [6] Test validation : Valeur hors limites (brightness=350)");
    Serial.println("  [7] Test securite   : Commande sensible (system.factory_reset)");
    Serial.println("  [8] Test securite   : Confirmer la commande sensible via jeton");
    Serial.println("  [9] Test securite   : Rejeu de confirmation (doit echouer)");
    Serial.println("  [h] Re-afficher ce menu d'aide");
    Serial.println("=======================================================");
    Serial.print("Entrez votre choix [1-9, h] : ");
}

// =============================================================================
// Initialisation & Configuration
// =============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    // Initialisation matérielle
    M5.Display.setBrightness(s_currentBrightness);
    M5.Speaker.setVolume(s_currentVolume);

    // Écoute des événements du registre pour traçabilité et UI
    s_registry.onEvent([](const ToolEvent& ev) {
        String typeStr = toolEventTypeToString(ev.type);
        Serial.printf("[REGISTRE EVT] %-26s | outil=%-22s | req=%-8s | %s\n",
                      typeStr.c_str(), ev.toolId.c_str(), ev.requestId.c_str(), ev.message.c_str());
        s_lastEventMsg = typeStr + " : " + ev.toolId;
        refreshScreen();
    });

    // Enregistrement 1 : system.get_status
    s_registry.registerTool(
        "system.get_status",
        "Retourne l'etat materiel du CoreS3 (batterie, heap, volume, ecran)",
        handleGetStatus,
        {},
        ToolSecurityPolicy::READ_ONLY
    );

    // Enregistrement 2 : audio.set_volume
    std::vector<ToolParam> volParams = {
        ToolParam("volume", ToolParamType::INT, 0, 255, true, "Niveau sonore materiel (0-255)")
    };
    s_registry.registerTool(
        "audio.set_volume",
        "Modifie le volume materiel du haut-parleur",
        handleSetVolume,
        volParams,
        ToolSecurityPolicy::WRITE
    );

    // Enregistrement 3 : display.set_brightness
    std::vector<ToolParam> brParams = {
        ToolParam("brightness", ToolParamType::INT, 0, 255, true, "Luminosite de l'ecran LCD (0-255)")
    };
    s_registry.registerTool(
        "display.set_brightness",
        "Modifie la luminosite de l'ecran",
        handleSetBrightness,
        brParams,
        ToolSecurityPolicy::WRITE
    );

    // Enregistrement 4 : system.factory_reset (SENSITIVE)
    s_registry.registerTool(
        "system.factory_reset",
        "Remise a zero materielle (requiert confirmation explicite)",
        handleFactoryReset,
        {},
        ToolSecurityPolicy::SENSITIVE
    );

    refreshScreen();
    printMenu();
}

// =============================================================================
// Boucle Principale
// =============================================================================

void loop() {
    M5.update();
    s_registry.update();

    if (Serial.available()) {
        char c = (char)Serial.read();
        while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n' || Serial.peek() == ' ')) {
            Serial.read(); // Vider les retours à la ligne
        }

        Serial.println(c);

        switch (c) {
            case '1': {
                Serial.println("\n--- DECOUVERTE DES OUTILS ---");
                Serial.printf("Nombre total d'outils : %u / %u\n",
                              (unsigned)s_registry.getToolCount(),
                              (unsigned)s_registry.getMaxCapacity());
                auto ids = s_registry.getToolIds();
                for (size_t i = 0; i < ids.size(); ++i) {
                    const ToolDefinition* def = s_registry.findTool(ids[i]);
                    if (!def) continue;
                    Serial.printf("  [%u] %-24s | Securite: %-9s | %s\n",
                                  (unsigned)(i + 1),
                                  def->id.c_str(),
                                  toolSecurityPolicyToString(def->securityPolicy),
                                  def->description.c_str());
                    for (const auto& p : def->parameters) {
                        Serial.printf("        - Param: %s (%s, %s",
                                      p.name.c_str(),
                                      toolParamTypeToString(p.type),
                                      p.required ? "requis" : "optionnel");
                        if (p.hasMin || p.hasMax) {
                            Serial.printf(", [%.0f - %.0f]", p.minValue, p.maxValue);
                        }
                        Serial.printf(") : %s\n", p.description.c_str());
                    }
                }
                break;
            }

            case '2': {
                Serial.println("\n--- EXECUTION : system.get_status ---");
                ToolResult res = s_registry.execute("system.get_status");
                Serial.printf("Resultat statut : %s\n", res.statusString());
                if (res.isSuccess()) {
                    Serial.printf("Sortie : %s\n", res.output.c_str());
                } else {
                    Serial.printf("Erreur : %s\n", res.error.c_str());
                }
                break;
            }

            case '3': {
                Serial.println("\n--- EXECUTION : audio.set_volume (volume=180) ---");
                ToolArgs args;
                args.set("volume", 180);
                ToolResult res = s_registry.execute("audio.set_volume", args);
                Serial.printf("Resultat : %s | %s\n", res.statusString(), res.isSuccess() ? res.output.c_str() : res.error.c_str());
                break;
            }

            case '4': {
                Serial.println("\n--- EXECUTION : display.set_brightness (brightness=200) ---");
                ToolArgs args;
                args.set("brightness", 200);
                ToolResult res = s_registry.execute("display.set_brightness", args);
                Serial.printf("Resultat : %s | %s\n", res.statusString(), res.isSuccess() ? res.output.c_str() : res.error.c_str());
                break;
            }

            case '5': {
                Serial.println("\n--- TEST VALIDATION : Parametre manquant sur audio.set_volume ---");
                ToolArgs emptyArgs;
                ToolResult res = s_registry.execute("audio.set_volume", emptyArgs);
                Serial.printf("Resultat attendu (INVALID_ARGUMENT) : %s\n", res.statusString());
                Serial.printf("Message d'erreur de validation : %s\n", res.error.c_str());
                break;
            }

            case '6': {
                Serial.println("\n--- TEST VALIDATION : Valeur hors limites (brightness=350 > max 255) ---");
                ToolArgs badArgs;
                badArgs.set("brightness", 350);
                ToolResult res = s_registry.execute("display.set_brightness", badArgs);
                Serial.printf("Resultat attendu (INVALID_ARGUMENT) : %s\n", res.statusString());
                Serial.printf("Message d'erreur de validation : %s\n", res.error.c_str());
                break;
            }

            case '7': {
                Serial.println("\n--- TEST SECURITE : Commande sensible system.factory_reset ---");
                ToolResult res = s_registry.execute("system.factory_reset");
                Serial.printf("Resultat : %s\n", res.statusString());
                if (res.isConfirmationRequired()) {
                    s_lastToken = res.confirmationToken;
                    Serial.printf("Jeton de confirmation emis : %s\n", s_lastToken.c_str());
                    Serial.println("=> La commande NE s'est PAS executee automatiquement.");
                    Serial.println("=> Tapez '8' pour confirmer l'execution.");
                }
                break;
            }

            case '8': {
                Serial.println("\n--- TEST SECURITE : Confirmation via jeton ---");
                if (s_lastToken.isEmpty()) {
                    Serial.println("Aucun jeton en attente. Executez d'abord le choix [7].");
                } else {
                    Serial.printf("Envoi de confirmation avec le jeton : %s\n", s_lastToken.c_str());
                    ToolResult res = s_registry.confirm(s_lastToken);
                    Serial.printf("Resultat de la confirmation : %s | %s\n",
                                  res.statusString(),
                                  res.isSuccess() ? res.output.c_str() : res.error.c_str());
                }
                break;
            }

            case '9': {
                Serial.println("\n--- TEST SECURITE : Rejeu du meme jeton (doit etre refuse) ---");
                if (s_lastToken.isEmpty()) {
                    Serial.println("Aucun jeton teste. Executez d'abord les choix [7] puis [8].");
                } else {
                    Serial.printf("Tentative de re-confirmation du jeton deja utilise : %s\n", s_lastToken.c_str());
                    ToolResult res = s_registry.confirm(s_lastToken);
                    Serial.printf("Resultat attendu (PERMISSION_DENIED) : %s\n", res.statusString());
                    Serial.printf("Message d'erreur anti-rejeu : %s\n", res.error.c_str());
                }
                break;
            }

            case 'h':
            case 'H':
            case '?':
            default:
                printMenu();
                break;
        }

        Serial.print("\nEntrez votre choix [1-9, h] : ");
    }

    // Toucher de l'écran pour rafraîchir l'affichage
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail();
        if (detail.wasPressed()) {
            M5.Speaker.tone(2000, 30);
            refreshScreen();
        }
    }

    delay(20);
}

