# FireBladeStandalone

Plugin SKSE ad **uso personale** (mashup / helper). Non è una mod a sé: serve solo insieme a un data pack privato che **non è incluso in questo repository**.

Personal-use mashup helper for SKSE (SE 1.5.97 + AE). It replaces, with native code and an in-game menu, a few dependencies that a weapon-combo mod normally gets from other mods (dual wielding, weapon arts, keyboard key tracing). It contains no third-party assets.

## Cosa fa

- legge l'equipaggiamento del giocatore (arma a destra + magia a sinistra) e accende/spegne dei "marker" (piccole abilità nascoste) che Open Animation Replacer legge con la condizione `HasMagicEffect`;
- traduce lo stick sinistro e i pulsanti del pad nei marker "avanti / indietro / ultimate" (niente tastiera);
- riproduce in C++ alcuni effetti che prima erano script Papyrus o plugin di terzi (rinascita a poca salute, scatto di inseguimento, incenerimento, esplosione sul bersaglio);
- offre un menu F1 (SKSE Menu Framework) con tutte le opzioni, salvate in `Data/SKSE/Plugins/FireBladeStandalone.ini`.

## Compilare

1. Crea un repository (anche privato) con il contenuto di questa cartella alla radice (compresa `.github/`).
2. A ogni push parte il workflow **Build** (Windows, MSVC, vcpkg, CommonLibSSE NG v13.1.0).
3. Scarica l'artefatto **FireBladeStandalone**: contiene `SKSE/Plugins/FireBladeStandalone.dll` già con i file alla radice, pronto per MO2.

Requisiti a runtime: SKSE64, Address Library, SKSE Menu Framework, Open Animation Replacer, Payload Interpreter.

## Nota

Il plugin non sa nulla del nome del file dati: lo legge da `[General] TargetPlugin=` nell'INI che accompagna il data pack. Senza quella riga resta inattivo e scrive un avviso nel log (`Documents/My Games/Skyrim Special Edition/SKSE/FireBladeStandalone.log`).

Uso personale, nessuna ridistribuzione.
