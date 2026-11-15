# OrientationClockVoice V2.1

Horloge vocale pour M5Stack CoreS3 combinant RTC, synchronisation NTP,
rotation automatique de l'ecran et annonces VoiceKit en francais naturel.

## Interactions

| Action | Effet |
|---|---|
| Toucher bref | Annonce l'heure en francais naturel |
| Appui long (800 ms) | Ouvre l'ecran complet de reglage du reveil |
| Glissement vers le haut/bas | Monte ou baisse le volume de 10 % |
| Bouton A / C | Recule ou avance le reveil de 15 minutes |
| Bouton B | Active ou desactive le reveil |
| Bouton PWR | Rejoue instantanement le dernier audio mis en cache |

Dans l'ecran de reglage, les six boutons `-H`, `+H`, `-M`, `+M`, `ON` et `OK`
permettent de regler et activer le reveil sans passer par la console.

Le reveil et son heure sont conserves en NVS apres un redemarrage. Sa sonnerie
se repete jusqu'a une action : toucher bref pour un snooze de cinq minutes,
appui long pour l'arreter. Elle fonctionne sans reseau.

## Installation

1. Copier `include/wifi_config.h.example` vers `include/wifi_config.h`.
2. Renseigner le Wi-Fi et la cle du fournisseur vocal dans le nouveau fichier.
3. Compiler et televerser avec PlatformIO :

```powershell
pio run -t upload
pio device monitor -b 115200
```

`include/wifi_config.h` est ignore par Git. Ne jamais placer de secrets dans
les deux fichiers d'exemple.

## Fonctionnement hors ligne

Le RTC conserve l'heure entre deux synchronisations. Quand le service vocal
n'est pas joignable, l'application garde le cadran et le reveil actifs, affiche
un diagnostic lisible et produit un signal sonore local. Le dernier flux PCM
TTS accepte par le haut-parleur est copie en PSRAM puis conserve dans LittleFS.
Une phrase identique peut ainsi etre rejouee sans Wi-Fi, meme apres redemarrage.
Le cache est limite a une phrase et a 1 Mio pour proteger la memoire et la flash.

## Tests natifs

Le formateur francais est testable sans carte :

```powershell
.\tests\run_tests.ps1
```

Les cas couverts incluent minuit, midi, quart, demi, moins le quart, le passage
a minuit, le style exact et les valeurs invalides.

