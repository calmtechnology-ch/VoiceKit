/**
 * @file main.cpp
 * @brief VoiceKit v0.6.6 — Exemple VoiceSettingsDemo (Universal Voice Profiles)
 *        Interface tactile LVGL 9 (320x240) sur M5Stack CoreS3 :
 *        - Écran 1 : Fournisseur (Gemini Live / OpenAI Realtime), Langue BCP-47, État connexion
 *        - Écran 2 : Filtre de genre perçu, Liste des voix compatibles, Bouton « Tester » (vrai essai audio)
 *        - Écran 3 : Style, Tonalité, Accent, Bouton « Enregistrer » (NVS sans clé API)
 *        - Écran 4 : Résumé du profil actuel, « Écouter », « Appliquer » (confirmé serveur), « Réinitialiser », Erreurs
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>
#include <WiFi.h>

#include <VoxCoreVoice.h>
#include <VoiceProfileManager.h>
#include "FrenchFonts.h"

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
    #ifdef WIFI_PASS
        #define WIFI_PASSWORD WIFI_PASS
    #else
        #define WIFI_PASSWORD ""
    #endif
#endif

#ifndef GEMINI_API_KEY
    #ifdef GEMINI_KEY
        #define GEMINI_API_KEY GEMINI_KEY
    #else
        #define GEMINI_API_KEY ""
    #endif
#endif

#ifndef OPENAI_API_KEY
    #ifdef OPENAI_KEY
        #define OPENAI_API_KEY OPENAI_KEY
    #else
        #define OPENAI_API_KEY ""
    #endif
#endif

// =============================================================================
// Constantes d'affichage LVGL 9 (M5Stack CoreS3 : 320 x 240)
// =============================================================================

static constexpr uint32_t SCREEN_WIDTH   = 320;
static constexpr uint32_t SCREEN_HEIGHT  = 240;
static constexpr uint32_t DRAW_BUF_LINES = 20;

alignas(4) static uint8_t s_drawBuf[SCREEN_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t)];
static lv_display_t* s_disp = nullptr;

// =============================================================================
// Moteur vocal unique & états d'édition du profil
// =============================================================================

static VoxCoreVoice s_voice;
static VoiceProfile s_draftProfile;
static uint8_t      s_activeTab = 0;

// Drapeaux de coordination non bloquante pour l'essai audio et l'application serveur
static bool   s_speakAfterConfirm = false;
static bool   s_awaitingServerConfirm = false;
static String s_statusBanner = "Initialisation...";
static String s_errorBanner  = "";
static bool   s_uiDirty      = true;

// Options statiques de langues, styles, tonalités et accents (zéro réallocation)
static LanguageDescriptor s_langs[16];
static size_t             s_langCount = 0;
static size_t             s_selectedLangIdx = 0;

static VoiceDescriptor    s_filteredVoices[20];
static size_t             s_filteredVoiceCount = 0;

static constexpr VoiceStyle kStyles[] = {
    VoiceStyle::NATURAL, VoiceStyle::PROFESSIONAL, VoiceStyle::EXPRESSIVE, VoiceStyle::NARRATIVE
};
static constexpr size_t kStyleCount = sizeof(kStyles) / sizeof(kStyles[0]);

static constexpr VoiceTone kTones[] = {
    VoiceTone::NEUTRAL, VoiceTone::CALM, VoiceTone::WARM, VoiceTone::CHEERFUL, VoiceTone::SERIOUS
};
static constexpr size_t kToneCount = sizeof(kTones) / sizeof(kTones[0]);

struct AccentPreset {
    const char* labelUi;
    const char* directiveValue;
};
static constexpr AccentPreset kAccents[] = {
    {"Aucun",           ""},
    {"Parisien",        "parisien"},
    {"Suisse romand",   "suisse romand"},
    {"Méridional",      "meridional"},
    {"Québécois",       "quebecois"},
    {"Britannique",     "britannique"}
};
static constexpr size_t kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);
static size_t s_selectedAccentIdx = 0;

// =============================================================================
// Références des widgets LVGL 9
// =============================================================================

static lv_obj_t* s_tabBtns[4]        = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* s_panels[4]         = {nullptr, nullptr, nullptr, nullptr};

// Écran 1 : Fournisseur et langue
static lv_obj_t* s_btnGemini         = nullptr;
static lv_obj_t* s_btnOpenAi         = nullptr;
static lv_obj_t* s_lblLangValue      = nullptr;
static lv_obj_t* s_lblConnState      = nullptr;

// Écran 2 : Choix de la voix
static lv_obj_t* s_genderBtns[4]     = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* s_voiceListCont     = nullptr;
static lv_obj_t* s_btnTestVoice      = nullptr;
static lv_obj_t* s_lblTestVoice      = nullptr;

// Écran 3 : Personnalisation
static lv_obj_t* s_lblStyleVal       = nullptr;
static lv_obj_t* s_lblToneVal        = nullptr;
static lv_obj_t* s_lblAccentVal      = nullptr;
static lv_obj_t* s_lblSaveFeedback   = nullptr;

// Écran 4 : Validation
static lv_obj_t* s_lblActiveSummary  = nullptr;
static lv_obj_t* s_lblStatusBanner   = nullptr;
static lv_obj_t* s_lblErrorBanner    = nullptr;
static lv_obj_t* s_btnApply          = nullptr;
static lv_obj_t* s_lblApply          = nullptr;

// =============================================================================
// Utilitaires de validation des clés (sans divulgation)
// =============================================================================

static String sanitizeKey(const char* raw) {
    if (!raw || raw[0] == '\0') return "";
    static const char* const kPlaceholders[] = {
        "VOTRE_CLE_API_GEMINI", "VOTRE_CLE_API_OPENAI", "VOTRE_CLE_ICI", "YOUR_API_KEY"
    };
    for (const char* ph : kPlaceholders) {
        if (strcmp(raw, ph) == 0) return "";
    }
    return String(raw);
}

static String sanitizeWifiField(const char* raw) {
    if (!raw || raw[0] == '\0') return "";
    static const char* const kPlaceholders[] = {
        "VOTRE_SSID_WIFI", "VOTRE_MOT_DE_PASSE_WIFI", "VOTRE_MOT_DE_PASSE", "YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"
    };
    for (const char* ph : kPlaceholders) {
        if (strcmp(raw, ph) == 0) return "";
    }
    return String(raw);
}

static void syncDraftIndexesFromProfile() {
    s_selectedLangIdx = 0;
    for (size_t i = 0; i < s_langCount; ++i) {
        if (s_draftProfile.language.equalsIgnoreCase(s_langs[i].bcp47)) {
            s_selectedLangIdx = i;
            break;
        }
    }
    s_selectedAccentIdx = 0;
    for (size_t i = 0; i < kAccentCount; ++i) {
        if (s_draftProfile.accent.equalsIgnoreCase(kAccents[i].directiveValue)) {
            s_selectedAccentIdx = i;
            break;
        }
    }
}

// =============================================================================
// Callbacks matériels d'affichage et tactile LVGL 9
// =============================================================================

static uint32_t lvglTickCallback() {
    return (uint32_t)millis();
}

static inline void setObjHidden(lv_obj_t* obj, bool hidden) {
    if (!obj) return;
#if LV_VERSION_CHECK(9, 5, 0)
    lv_obj_set_hidden(obj, hidden);
#else
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
#endif
}

static inline void disableObjScroll(lv_obj_t* obj) {
    if (!obj) return;
#if LV_VERSION_CHECK(9, 5, 0)
    lv_obj_set_scrollable(obj, false);
#else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
#endif
}

static void dispFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.endWrite();
    lv_display_flush_ready(disp);
}

static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.isPressed()) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = detail.x;
            data->point.y = detail.y;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}

// =============================================================================
// Rafraîchissement de la liste des voix et de l'interface LVGL 9
// =============================================================================

static void rebuildVoiceList();
static void refreshUi();

static void onVoiceItemClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < s_filteredVoiceCount) {
        s_draftProfile.voiceId = s_filteredVoices[idx].id;
        s_errorBanner = "";
        rebuildVoiceList();
        s_uiDirty = true;
    }
}

static void rebuildVoiceList() {
    if (!s_voiceListCont) return;
    lv_obj_clean(s_voiceListCont);

    s_filteredVoiceCount = s_voice.getProfileManager().getVoicesForProvider(
        s_draftProfile.provider, s_draftProfile.preferredGender,
        s_filteredVoices, sizeof(s_filteredVoices) / sizeof(s_filteredVoices[0]));

    // Si la voix courante n'appartient pas au fournisseur/filtre courant, sélectionner la 1re disponible
    bool currentInList = false;
    for (size_t i = 0; i < s_filteredVoiceCount; ++i) {
        if (s_draftProfile.voiceId.equalsIgnoreCase(s_filteredVoices[i].id)) {
            s_draftProfile.voiceId = s_filteredVoices[i].id;
            currentInList = true;
            break;
        }
    }
    if (!currentInList && s_filteredVoiceCount > 0) {
        s_draftProfile.voiceId = s_filteredVoices[0].id;
    }

    for (size_t i = 0; i < s_filteredVoiceCount; ++i) {
        const VoiceDescriptor& vd = s_filteredVoices[i];
        bool selected = s_draftProfile.voiceId.equalsIgnoreCase(vd.id);

        lv_obj_t* btn = lv_button_create(s_voiceListCont);
        lv_obj_set_size(btn, 286, 30);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0x1565C0) : lv_color_hex(0x232A38), 0);
        lv_obj_add_event_cb(btn, onVoiceItemClicked, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        lv_obj_t* lbl = lv_label_create(btn);
        char line[96];
        snprintf(line, sizeof(line), "%s%s — %s (%s)",
                 selected ? "● " : "  ",
                 vd.displayName,
                 voiceGenderToFrenchLabel(vd.gender),
                 vd.traitsFr);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_font(lbl, &font_french_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void triggerDraftOrSpeak(bool speakSampleAfterConfirm) {
    s_errorBanner = "";

    if (s_voice.isBusy() || s_voice.isConnecting()) {
        s_errorBanner = "Moteur occupé ou connexion en cours : patientez.";
        s_uiDirty = true;
        return;
    }

    const VoiceProfile activeProf = s_voice.getVoiceProfile();
    const bool alreadyConfirmedOnline =
        s_voice.isReady() && s_draftProfile.hasSameAcousticConfig(activeProf);

    if (alreadyConfirmedOnline) {
        s_voice.setPreferredGender(s_draftProfile.preferredGender);
        s_awaitingServerConfirm = false;
        s_statusBanner = "Profil appliqué et actif";
        if (speakSampleAfterConfirm) {
            String phrase = s_voice.getProfileManager().getSamplePhraseForLanguage(s_draftProfile.language);
            if (!s_voice.say(phrase)) {
                s_errorBanner = s_voice.getLastError();
            } else {
                s_statusBanner = "Essai audio en cours...";
            }
        }
        s_uiDirty = true;
        return;
    }

    // Demande d'application avec connexion/confirmation serveur obligatoire :
    // RÈGLE STRICTE : Ne JAMAIS afficher « Profil appliqué » avant onProfileApplied() !
    s_awaitingServerConfirm = true;
    s_speakAfterConfirm = speakSampleAfterConfirm;
    s_statusBanner = "Attente confirmation fournisseur...";
    s_uiDirty = true;

    if (!s_voice.applyVoiceProfile(s_draftProfile, true)) {
        s_awaitingServerConfirm = false;
        s_speakAfterConfirm = false;
        s_errorBanner = s_voice.getLastError();
        s_statusBanner = "Échec d'application du profil";
        s_uiDirty = true;
    }
}

// =============================================================================
// Gestionnaires d'événements tactiles LVGL 9
// =============================================================================

static void switchTab(uint8_t tabIdx) {
    if (tabIdx >= 4) return;
    s_activeTab = tabIdx;
    for (uint8_t i = 0; i < 4; ++i) {
        if (s_panels[i]) {
            setObjHidden(s_panels[i], i != tabIdx);
        }
        if (s_tabBtns[i]) {
            lv_obj_set_style_bg_color(s_tabBtns[i],
                (i == tabIdx) ? lv_color_hex(0x1976D2) : lv_color_hex(0x1E2430), 0);
        }
    }
    s_uiDirty = true;
}

static void onTabClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    switchTab(idx);
}

static void onProviderSelected(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    VoiceProviderType prov = (VoiceProviderType)(uintptr_t)lv_event_get_user_data(e);
    if (s_draftProfile.provider != prov) {
        s_draftProfile.provider = prov;
        if (!s_voice.getProfileManager().isValidVoiceForProvider(prov, s_draftProfile.voiceId)) {
            s_draftProfile.voiceId = s_voice.getProfileManager().getDefaultVoiceForProvider(
                prov, s_draftProfile.preferredGender);
        }
        rebuildVoiceList();
        s_errorBanner = "";
        s_statusBanner = "Profil modifié (non encore appliqué)";
        s_uiDirty = true;
    }
}

static void onLangCycle(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_langCount == 0) return;
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    if (delta < 0) {
        s_selectedLangIdx = (s_selectedLangIdx == 0) ? (s_langCount - 1) : (s_selectedLangIdx - 1);
    } else {
        s_selectedLangIdx = (s_selectedLangIdx + 1) % s_langCount;
    }
    s_draftProfile.language = s_langs[s_selectedLangIdx].bcp47;
    s_statusBanner = "Langue sélectionnée (à appliquer)";
    s_uiDirty = true;
}

static void onGenderFilterClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    VoiceGender g = (VoiceGender)(uintptr_t)lv_event_get_user_data(e);
    s_draftProfile.preferredGender = g;
    rebuildVoiceList();
    s_uiDirty = true;
}

static void onTestVoiceClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    triggerDraftOrSpeak(true);
}

static void onStyleCycle(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    size_t cur = 0;
    for (size_t i = 0; i < kStyleCount; ++i) {
        if (kStyles[i] == s_draftProfile.style) { cur = i; break; }
    }
    s_draftProfile.style = kStyles[(cur + 1) % kStyleCount];
    s_statusBanner = "Style modifié (à appliquer)";
    s_uiDirty = true;
}

static void onToneCycle(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    size_t cur = 0;
    for (size_t i = 0; i < kToneCount; ++i) {
        if (kTones[i] == s_draftProfile.tone) { cur = i; break; }
    }
    s_draftProfile.tone = kTones[(cur + 1) % kToneCount];
    s_statusBanner = "Tonalité modifiée (à appliquer)";
    s_uiDirty = true;
}

static void onAccentCycle(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_selectedAccentIdx = (s_selectedAccentIdx + 1) % kAccentCount;
    s_draftProfile.accent = kAccents[s_selectedAccentIdx].directiveValue;
    s_statusBanner = "Accent modifié (à appliquer)";
    s_uiDirty = true;
}

static void onSaveNvsClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    bool ok = s_voice.getProfileManager().saveActiveProfile(s_draftProfile);
    if (s_lblSaveFeedback) {
        lv_label_set_text(s_lblSaveFeedback,
            ok ? "Profil enregistré en NVS (sans clé API)." : "Erreur de sauvegarde NVS.");
        lv_obj_set_style_text_color(s_lblSaveFeedback,
            ok ? lv_color_hex(0x66BB6A) : lv_color_hex(0xEF5350), 0);
    }
    s_statusBanner = ok ? "Profil sauvegardé en NVS" : "Échec sauvegarde NVS";
    s_uiDirty = true;
}

static void onListenActiveClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_errorBanner = "";
    VoiceProfile active = s_voice.getVoiceProfile();
    String phrase = s_voice.getProfileManager().getSamplePhraseForLanguage(active.language);
    if (!s_voice.say(phrase)) {
        s_errorBanner = s_voice.getLastError();
    } else {
        s_statusBanner = "Lecture du profil actif...";
    }
    s_uiDirty = true;
}

static void onApplyClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    triggerDraftOrSpeak(false);
}

static void onResetClicked(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_errorBanner = "";
    if (s_voice.isBusy() || s_voice.isConnecting()) {
        s_errorBanner = "Impossible de réinitialiser pendant une activité.";
        s_uiDirty = true;
        return;
    }
    s_voice.getProfileManager().resetActiveProfile();
    s_draftProfile = VoiceProfileManager::getDefaultProfile(s_draftProfile.provider);
    syncDraftIndexesFromProfile();
    rebuildVoiceList();
    if (s_voice.isReady() || s_voice.hasProviderApiKey(s_draftProfile.provider)) {
        triggerDraftOrSpeak(false);
    } else {
        s_voice.applyVoiceProfile(s_draftProfile, false);
        s_statusBanner = "Profil par défaut restauré (hors ligne)";
    }
    s_uiDirty = true;
}

// =============================================================================
// Construction des 4 écrans tactiles LVGL 9 (320 x 240)
// =============================================================================

static lv_obj_t* createStyledButton(lv_obj_t* parent, const char* text, int32_t x, int32_t y,
                                    int32_t w, int32_t h, uint32_t bgHex,
                                    lv_event_cb_t cb, void* userData = nullptr,
                                    lv_obj_t** outLabel = nullptr) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex), 0);
    lv_obj_set_style_pad_all(btn, 2, 0);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    }
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_french_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
    if (outLabel) *outLabel = lbl;
    return btn;
}

static void buildUi() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E131B), 0);
    disableObjScroll(scr);

    // Barre d'onglets supérieure (4 écrans : 320 x 30 px)
    const char* tabTitles[4] = {"1. Fourn.", "2. Voix", "3. Style", "4. Valid."};
    for (uint8_t i = 0; i < 4; ++i) {
        s_tabBtns[i] = createStyledButton(scr, tabTitles[i], 4 + i * 79, 3, 76, 28,
                                          0x1E2430, onTabClicked, (void*)(uintptr_t)i);
    }

    // Création des 4 conteneurs d'écrans (312 x 202 px)
    for (uint8_t i = 0; i < 4; ++i) {
        s_panels[i] = lv_obj_create(scr);
        lv_obj_set_pos(s_panels[i], 4, 35);
        lv_obj_set_size(s_panels[i], 312, 201);
        lv_obj_set_style_bg_color(s_panels[i], lv_color_hex(0x151C28), 0);
        lv_obj_set_style_border_color(s_panels[i], lv_color_hex(0x2A3548), 0);
        lv_obj_set_style_border_width(s_panels[i], 1, 0);
        lv_obj_set_style_radius(s_panels[i], 8, 0);
        lv_obj_set_style_pad_all(s_panels[i], 6, 0);
        disableObjScroll(s_panels[i]);
    }

    // -------------------------------------------------------------------------
    // ÉCRAN 1 — Fournisseur et langue
    // -------------------------------------------------------------------------
    {
        lv_obj_t* p = s_panels[0];
        lv_obj_t* t1 = lv_label_create(p);
        lv_label_set_text(t1, "Fournisseur vocal :");
        lv_obj_set_style_text_font(t1, &font_french_14, 0);
        lv_obj_set_style_text_color(t1, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_pos(t1, 4, 2);

        s_btnGemini = createStyledButton(p, "Gemini Live", 4, 24, 142, 36,
            0x1565C0, onProviderSelected, (void*)(uintptr_t)VoiceProviderType::GEMINI);
        s_btnOpenAi = createStyledButton(p, "OpenAI Realtime", 154, 24, 142, 36,
            0x263238, onProviderSelected, (void*)(uintptr_t)VoiceProviderType::OPENAI);

        lv_obj_t* t2 = lv_label_create(p);
        lv_label_set_text(t2, "Langue parlée (BCP-47) :");
        lv_obj_set_style_text_font(t2, &font_french_14, 0);
        lv_obj_set_style_text_color(t2, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_pos(t2, 4, 68);

        createStyledButton(p, "◀", 4, 90, 40, 36, 0x2C384A, onLangCycle, (void*)(intptr_t)-1);
        s_lblLangValue = lv_label_create(p);
        lv_obj_set_size(s_lblLangValue, 204, 36);
        lv_obj_set_pos(s_lblLangValue, 48, 98);
        lv_obj_set_style_text_align(s_lblLangValue, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_lblLangValue, &font_french_14, 0);
        lv_obj_set_style_text_color(s_lblLangValue, lv_color_hex(0xFFFFFF), 0);
        createStyledButton(p, "▶", 256, 90, 40, 36, 0x2C384A, onLangCycle, (void*)(intptr_t)1);

        s_lblConnState = lv_label_create(p);
        lv_obj_set_pos(s_lblConnState, 4, 138);
        lv_obj_set_width(s_lblConnState, 292);
        lv_label_set_long_mode(s_lblConnState, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_lblConnState, &font_french_12, 0);
        lv_obj_set_style_text_color(s_lblConnState, lv_color_hex(0xCFD8DC), 0);
    }

    // -------------------------------------------------------------------------
    // ÉCRAN 2 — Choix de la voix (filtre de genre + liste + bouton « Tester »)
    // -------------------------------------------------------------------------
    {
        lv_obj_t* p = s_panels[1];
        const char* gLabels[4] = {"Toutes", "Féminin", "Masculin", "Neutre"};
        const VoiceGender gVals[4] = {
            VoiceGender::ANY, VoiceGender::FEMININE, VoiceGender::MASCULINE, VoiceGender::NEUTRAL
        };
        for (uint8_t i = 0; i < 4; ++i) {
            s_genderBtns[i] = createStyledButton(p, gLabels[i], 2 + i * 74, 0, 71, 26,
                0x263238, onGenderFilterClicked, (void*)(uintptr_t)gVals[i]);
        }

        s_voiceListCont = lv_obj_create(p);
        lv_obj_set_pos(s_voiceListCont, 0, 30);
        lv_obj_set_size(s_voiceListCont, 298, 120);
        lv_obj_set_style_bg_color(s_voiceListCont, lv_color_hex(0x0E141E), 0);
        lv_obj_set_style_border_color(s_voiceListCont, lv_color_hex(0x263238), 0);
        lv_obj_set_style_pad_all(s_voiceListCont, 4, 0);
        lv_obj_set_style_pad_row(s_voiceListCont, 4, 0);
        lv_obj_set_flex_flow(s_voiceListCont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(s_voiceListCont, LV_DIR_VER);

        s_btnTestVoice = createStyledButton(p, "Tester la voix sélectionnée", 2, 154, 294, 34,
            0x2E7D32, onTestVoiceClicked, nullptr, &s_lblTestVoice);
    }

    // -------------------------------------------------------------------------
    // ÉCRAN 3 — Personnalisation (Style, Tonalité, Accent, « Enregistrer »)
    // -------------------------------------------------------------------------
    {
        lv_obj_t* p = s_panels[2];

        lv_obj_t* ls = lv_label_create(p);
        lv_label_set_text(ls, "Style :");
        lv_obj_set_style_text_font(ls, &font_french_14, 0);
        lv_obj_set_style_text_color(ls, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_pos(ls, 4, 8);
        createStyledButton(p, "", 96, 2, 200, 34, 0x263238, onStyleCycle, nullptr, &s_lblStyleVal);

        lv_obj_t* lt = lv_label_create(p);
        lv_label_set_text(lt, "Tonalité :");
        lv_obj_set_style_text_font(lt, &font_french_14, 0);
        lv_obj_set_style_text_color(lt, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_pos(lt, 4, 48);
        createStyledButton(p, "", 96, 42, 200, 34, 0x263238, onToneCycle, nullptr, &s_lblToneVal);

        lv_obj_t* la = lv_label_create(p);
        lv_label_set_text(la, "Accent :");
        lv_obj_set_style_text_font(la, &font_french_14, 0);
        lv_obj_set_style_text_color(la, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_pos(la, 4, 88);
        createStyledButton(p, "", 96, 82, 200, 34, 0x263238, onAccentCycle, nullptr, &s_lblAccentVal);

        createStyledButton(p, "Enregistrer dans la mémoire NVS", 4, 124, 292, 34,
                           0x00695C, onSaveNvsClicked);

        s_lblSaveFeedback = lv_label_create(p);
        lv_label_set_text(s_lblSaveFeedback, "Clés API jamais stockées dans le profil.");
        lv_obj_set_style_text_font(s_lblSaveFeedback, &font_french_12, 0);
        lv_obj_set_style_text_color(s_lblSaveFeedback, lv_color_hex(0x90A4AE), 0);
        lv_obj_set_pos(s_lblSaveFeedback, 4, 164);
    }

    // -------------------------------------------------------------------------
    // ÉCRAN 4 — Validation (Profil actuel, Écouter, Appliquer, Réinitialiser, Erreurs)
    // -------------------------------------------------------------------------
    {
        lv_obj_t* p = s_panels[3];

        s_lblActiveSummary = lv_label_create(p);
        lv_obj_set_pos(s_lblActiveSummary, 2, 0);
        lv_obj_set_width(s_lblActiveSummary, 294);
        lv_label_set_long_mode(s_lblActiveSummary, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_lblActiveSummary, &font_french_12, 0);
        lv_obj_set_style_text_color(s_lblActiveSummary, lv_color_hex(0xE3F2FD), 0);

        s_lblStatusBanner = lv_label_create(p);
        lv_obj_set_pos(s_lblStatusBanner, 2, 74);
        lv_obj_set_width(s_lblStatusBanner, 294);
        lv_obj_set_style_text_font(s_lblStatusBanner, &font_french_12, 0);
        lv_obj_set_style_text_color(s_lblStatusBanner, lv_color_hex(0x81C784), 0);

        s_lblErrorBanner = lv_label_create(p);
        lv_obj_set_pos(s_lblErrorBanner, 2, 94);
        lv_obj_set_width(s_lblErrorBanner, 294);
        lv_label_set_long_mode(s_lblErrorBanner, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_lblErrorBanner, &font_french_12, 0);
        lv_obj_set_style_text_color(s_lblErrorBanner, lv_color_hex(0xFF8A80), 0);

        createStyledButton(p, "Écouter", 2, 148, 94, 38, 0x2E7D32, onListenActiveClicked);
        s_btnApply = createStyledButton(p, "Appliquer", 101, 148, 96, 38,
                                        0x1565C0, onApplyClicked, nullptr, &s_lblApply);
        createStyledButton(p, "Réinitialiser", 202, 148, 94, 38, 0x6A1B9A, onResetClicked);
    }

    rebuildVoiceList();
    switchTab(0);
}

static void refreshUi() {
    // Écran 1 : Fournisseur, langue, état connexion
    if (s_btnGemini && s_btnOpenAi) {
        bool isGem = (s_draftProfile.provider == VoiceProviderType::GEMINI);
        lv_obj_set_style_bg_color(s_btnGemini, isGem ? lv_color_hex(0x1565C0) : lv_color_hex(0x263238), 0);
        lv_obj_set_style_bg_color(s_btnOpenAi, !isGem ? lv_color_hex(0x00897B) : lv_color_hex(0x263238), 0);
    }
    if (s_lblLangValue && s_selectedLangIdx < s_langCount) {
        char lbuf[64];
        snprintf(lbuf, sizeof(lbuf), "%s (%s)",
                 s_langs[s_selectedLangIdx].displayNameFr,
                 s_langs[s_selectedLangIdx].bcp47);
        lv_label_set_text(s_lblLangValue, lbuf);
    }
    if (s_lblConnState) {
        bool wifiOk = s_voice.getWiFiManager().isConnected() || (WiFi.status() == WL_CONNECTED);
        bool hasKey = s_voice.hasProviderApiKey(s_draftProfile.provider);
        char cbuf[160];
        snprintf(cbuf, sizeof(cbuf),
                 "Wi-Fi : %s | État moteur : %s\nClé %s : %s | Actif : %s (%s)",
                 wifiOk ? "Connecté" : "Connexion...",
                 voiceStateToString(s_voice.getState()),
                 voiceProviderTypeToString(s_draftProfile.provider),
                 hasKey ? "Configurée" : "Absente",
                 voiceProviderTypeToString(s_voice.getProviderType()),
                 s_voice.getVoice().c_str());
        lv_label_set_text(s_lblConnState, cbuf);
    }

    // Écran 2 : Filtres de genre et bouton Tester
    const VoiceGender gVals[4] = {
        VoiceGender::ANY, VoiceGender::FEMININE, VoiceGender::MASCULINE, VoiceGender::NEUTRAL
    };
    for (uint8_t i = 0; i < 4; ++i) {
        if (s_genderBtns[i]) {
            lv_obj_set_style_bg_color(s_genderBtns[i],
                (s_draftProfile.preferredGender == gVals[i]) ? lv_color_hex(0x1565C0) : lv_color_hex(0x263238), 0);
        }
    }
    if (s_lblTestVoice) {
        if (s_voice.isSpeaking()) {
            lv_label_set_text(s_lblTestVoice, "Restitution audio en cours...");
        } else if (s_awaitingServerConfirm && s_speakAfterConfirm) {
            lv_label_set_text(s_lblTestVoice, "Confirmation serveur avant essai...");
        } else {
            lv_label_set_text(s_lblTestVoice, "Tester la voix sélectionnée");
        }
    }

    // Écran 3 : Style, tonalité, accent
    if (s_lblStyleVal)  lv_label_set_text(s_lblStyleVal, voiceStyleToFrenchLabel(s_draftProfile.style));
    if (s_lblToneVal)   lv_label_set_text(s_lblToneVal, voiceToneToFrenchLabel(s_draftProfile.tone));
    if (s_lblAccentVal) lv_label_set_text(s_lblAccentVal, kAccents[s_selectedAccentIdx].labelUi);

    // Écran 4 : Résumé du profil confirmé vs brouillon, statut et erreurs
    if (s_lblActiveSummary) {
        VoiceProfile act = s_voice.getVoiceProfile();
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf),
                 "Confirmé : %s | %s | %s\nStyle : %s | Ton : %s | Accent : %s\nSélection : %s | %s | %s",
                 voiceProviderTypeToString(act.provider), act.voiceId.c_str(), act.language.c_str(),
                 voiceStyleToFrenchLabel(act.style), voiceToneToFrenchLabel(act.tone),
                 act.accent.isEmpty() ? "aucun" : act.accent.c_str(),
                 voiceProviderTypeToString(s_draftProfile.provider),
                 s_draftProfile.voiceId.c_str(), s_draftProfile.language.c_str());
        lv_label_set_text(s_lblActiveSummary, sbuf);
    }
    if (s_lblStatusBanner) {
        lv_label_set_text(s_lblStatusBanner, s_statusBanner.c_str());
    }
    if (s_lblErrorBanner) {
        lv_label_set_text(s_lblErrorBanner, s_errorBanner.c_str());
    }
    if (s_lblApply) {
        lv_label_set_text(s_lblApply, s_awaitingServerConfirm ? "En cours..." : "Appliquer");
    }
}

// =============================================================================
// Cycle de vie Arduino (setup / loop non bloquante)
// =============================================================================

void setup() {
    auto cfgM5 = M5.config();
    cfgM5.serial_baudrate = 115200;
    M5.begin(cfgM5);
    M5.Display.waitDisplay();
    M5.Display.setRotation(1);
    M5.Display.setBrightness(200);

    // 1. Initialisation LVGL 9
    lv_init();
    lv_tick_set_cb(lvglTickCallback);

    s_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(s_disp, dispFlushCb);
    lv_display_set_buffers(s_disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);

    // 2. Chargement du catalogue de langues
    s_langCount = s_voice.getProfileManager().getSupportedLanguages(
        s_langs, sizeof(s_langs) / sizeof(s_langs[0]));

    // 3. Préparation de la configuration VoiceKit v0.6.6
    VoiceKitConfig vcfg;
    vcfg.wifiSSID     = sanitizeWifiField(WIFI_SSID);
    vcfg.wifiPassword = sanitizeWifiField(WIFI_PASSWORD);
    vcfg.wifiMode     = vcfg.wifiSSID.isEmpty() ? WiFiMode::EXTERNAL_WIFI : WiFiMode::MANAGED_WIFI;
    vcfg.geminiApiKey = sanitizeKey(GEMINI_API_KEY);
    vcfg.openAiApiKey = sanitizeKey(OPENAI_API_KEY);
    vcfg.provider     = (!vcfg.geminiApiKey.isEmpty() || vcfg.openAiApiKey.isEmpty())
                        ? VoiceProviderType::GEMINI
                        : VoiceProviderType::OPENAI;
    vcfg.restoreSavedProfileOnBoot = true;
    vcfg.volume = 80;

    // 4. Enregistrement des callbacks (préservés pendant tous les changements de profil)
    s_voice.onReady([]() {
        if (!s_awaitingServerConfirm) {
            s_statusBanner = "Connecté et prêt";
        }
        s_uiDirty = true;
    });

    s_voice.onProfileApplied([](const VoiceProfile& confirmed) {
        // RÈGLE OBLIGATOIRE : « Profil appliqué » n'est affiché qu'après confirmation effective du fournisseur !
        s_draftProfile = confirmed;
        syncDraftIndexesFromProfile();
        s_awaitingServerConfirm = false;
        s_errorBanner = "";
        s_statusBanner = "Profil appliqué (confirmé par le fournisseur)";
        rebuildVoiceList();

        if (s_speakAfterConfirm) {
            s_speakAfterConfirm = false;
            String phrase = s_voice.getProfileManager().getSamplePhraseForLanguage(confirmed.language);
            if (!s_voice.say(phrase)) {
                s_errorBanner = s_voice.getLastError();
            } else {
                s_statusBanner = "Profil appliqué — essai audio en cours...";
            }
        }
        s_uiDirty = true;
    });

    s_voice.onProfileError([](const String& err) {
        s_awaitingServerConfirm = false;
        s_speakAfterConfirm = false;
        s_errorBanner = err;
        s_statusBanner = "Ancien profil restauré suite à l'erreur";
        s_draftProfile = s_voice.getVoiceProfile();
        syncDraftIndexesFromProfile();
        rebuildVoiceList();
        s_uiDirty = true;
    });

    s_voice.onSpeechFinished([]() {
        if (!s_awaitingServerConfirm) {
            s_statusBanner = "Essai audio terminé — Prêt";
        }
        s_uiDirty = true;
    });

    s_voice.onError([](const String& err) {
        s_errorBanner = err;
        s_uiDirty = true;
    });

    // 5. Démarrage du moteur VoiceKit
    s_voice.begin(vcfg);
    s_draftProfile = s_voice.getVoiceProfile();
    syncDraftIndexesFromProfile();

    if (vcfg.wifiSSID.isEmpty()) {
        s_errorBanner = "Configurez include/secrets.h (WIFI_SSID + clé API).";
        s_statusBanner = "Mode hors ligne (configuration requise)";
    } else {
        s_statusBanner = "Connexion Wi-Fi en cours...";
    }

    buildUi();
    refreshUi();
}

void loop() {
    static uint32_t s_lastUiSyncMs = 0;
    M5.update();

    // Avancement non bloquant du moteur vocal et du réseau
    s_voice.update();

    uint32_t now = millis();
    if (s_uiDirty || (now - s_lastUiSyncMs >= 250)) {
        s_uiDirty = false;
        s_lastUiSyncMs = now;
        refreshUi();
    }

    // Rafraîchissement fluide LVGL 9 sans blocage
    lv_timer_handler();
    delay(5);
}
