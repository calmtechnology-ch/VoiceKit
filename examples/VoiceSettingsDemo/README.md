# VoiceSettingsDemo — Configuration vocale et Français UTF-8

Exemple autonome de **VoiceKit v0.4.1** pour **M5Stack CoreS3**.

Cet exemple démontre l'utilisation de la configuration vocale étendue et la préservation intégrale des caractères UTF-8 français, allemands et anglais avec Google Gemini Live.

## Fonctionnalités

- **Configuration vocale multi-critères** :
  - **Voix Gemini** : Aoede, Charon, Fenrir, Kore, Puck.
  - **Langues BCP-47** : français (`fr-FR`), allemand (`de-DE`), anglais (`en-US`).
  - **Styles d'élocution** : naturel (`natural`), professionnel (`professional`), expressif (`expressive`), narratif (`narrative`).
  - **Tonalités vocales** : neutre (`neutral`), calme (`calm`), chaleureux (`warm`), enjoué (`cheerful`), sérieux (`serious`).
  - **Accents optionnels** : indications de prononciation (ex: parisien, méridional).
- **Chaîne UTF-8 de bout en bout** :
  - Restitution de phrases accentuées avec ligatures (`œ`, `æ`), apostrophes typographiques (`’`) et guillemets (`«`, `»`).
  - Validation stricte RFC 3629 et échappement JSON RFC 8259 centralisé.
- **Reconnexion sécurisée** :
  - Négociation du nouveau setup Gemini Live uniquement au repos (moteur non occupé).
  - Aucune interruption pendant une élocution ou une écoute active.
- **Interface tactile M5Stack CoreS3** :
  - Affichage en temps réel de la configuration active, de l'état de connexion et du texte prononcé.
  - Bouton tactile « PROFIL SUIVANT » pour enchaîner les tests.
  - Commandes console série (`N` = suivant, `R` = répéter, `S` = stopper).

## Scénarios de démonstration inclus

1. **Français — Phrase obligatoire UTF-8** :
   - Voix : `Aoede` | Langue : `fr-FR` | Style : `natural` | Ton : `warm` | Accent : `parisien`
   - Phrase : *« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »*
2. **Français — Style Professionnel & Sérieux** :
   - Voix : `Charon` | Langue : `fr-FR` | Style : `professional` | Ton : `serious`
   - Phrase : *« Le compte rendu de la séance plénière confirme une hausse des investissements de vingt-quatre pour cent. »*
3. **Français — Style Expressif & Enjoué** :
   - Voix : `Fenrir` | Langue : `fr-FR` | Style : `expressive` | Ton : `cheerful` | Accent : `meridional`
   - Phrase : *« Formidable ! Quelle magnifique journée ensoleillée pour célébrer cette splendide réussite ! »*
4. **Allemand — Style Calme & Naturel** :
   - Voix : `Kore` | Langue : `de-DE` | Style : `natural` | Ton : `calm`
   - Phrase : *« Guten Tag! Das Wetter in Berlin ist heute wunderbar und die Vögel singen fröhlich im Garten. »*
5. **Anglais — Style Narratif & Chaleureux** :
   - Voix : `Puck` | Langue : `en-US` | Style : `narrative` | Ton : `warm`
   - Phrase : *« Once upon a time, high in the misty mountains, an ancient clock began to strike twelve. »*

## Compilation et téléversement

```bash
# Compilation avec PlatformIO
pio run -d examples/VoiceSettingsDemo

# Téléversement vers M5Stack CoreS3
pio run -d examples/VoiceSettingsDemo -t upload
```
