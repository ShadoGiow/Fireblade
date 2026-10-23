# Swap Moveset

Plugin SKSE per Skyrim che cambia il **moveset** del personaggio (le animazioni di camminata, corsa,
combo, schivata...) con un tasto o una combinazione di tasti, anche da controller.

> **Uso personale / mashup.** Questo progetto contiene solo il plugin e gli strumenti. I pack di animazioni
> sono lavoro di altri autori: non sono inclusi e restano soggetti ai permessi dei loro autori.

## Come funziona

Il plugin non contiene animazioni. Aggiunge a *Open Animation Replacer* (OAR) una condizione,
`SwapMoveset_PackActive`, che e' vera solo mentre la modalita' e' attiva con un certo pack.
Un pack e' un normale mod OAR in cui ogni sotto-mod ha quella condizione come prima condizione
(lo fa da solo `tools/swapmoveset_convert.py`). Il convertitore toglie anche le condizioni sull'arma
(per esempio "spada a una mano in mano destra"): il moveset deve valere con qualsiasi arma, o senza.

```
tasto/combinazione  ->  Swap Moveset  ->  "pack attivo = demo"  ->  OAR sceglie le animazioni del pack
```

## Requisiti

| | |
|---|---|
| Skyrim | SE 1.5.97 o AE, con **Address Library for SKSE Plugins** |
| **SKSE64** | corrispondente alla versione del gioco |
| **Open Animation Replacer** | 2.x (con le API per condizioni personalizzate) |
| **SKSE Menu Framework** | per il menu F1 (serve la versione 2 o piu' recente). Senza, il plugin funziona lo stesso e si configura da file |
| Dipendenze dei pack | quelle che servono al moveset (per esempio MCO / TK Dodge / Directional Movement). Le legge dal campo "Note" del pack nel menu |

## Compilare (GitHub Actions)

Non serve installare nulla sul PC.

1. Crea un repository su GitHub e caricaci **tutto il contenuto di questa cartella**, compresa `.github`:
   ```
   git init && git add -A && git commit -m "Swap Moveset" && git branch -M main
   git remote add origin https://github.com/<utente>/<repo>.git && git push -u origin main
   ```
2. Scheda **Actions** -> workflow **Build**. Parte da solo a ogni push.
3. Quando e' verde apri l'esecuzione -> **Artifacts** -> scarica **SwapMoveset**.
   Dentro ci sono i file gia' alla radice (`SKSE/Plugins/SwapMoveset.dll`): in MO2 *Installa mod* e basta.
4. Se un passaggio e' rosso, copia il testo dell'errore del passaggio che e' fallito.

Il workflow scarica CommonLibSSE-NG a una versione fissata (`COMMONLIB_SHA` in `.github/workflows/build.yml`)
e compila con le impostazioni standard di CommonLib (SE, AE e VR insieme, come la sua versione
precompilata, che scarica da sola quando puo': cosi' la build e' molto piu' veloce). Il job **tests** esegue anche tutti i test descritti sotto.

## Installazione

1. Installa **SwapMoveset** (il plugin compilato) come mod.
2. Installa il **pack convertito** come mod separata.
3. **Non lasciare attivo il mod originale del pack**: sarebbe sempre attivo e la modalita' non servirebbe a niente.
   La versione convertita ha un'altra cartella (`... [SwapMoveset]`) proprio per non sovrascriverla.
4. Gioco -> menu **F1** -> **Swap Moveset**.

## Uso

**Moveset** - stato, pulsanti *Entra ora / Esci ora* (per provare subito), elenco dei pack. Cliccare un pack
lo seleziona; se la modalita' e' gia' attiva cambia al volo.

**Controlli** - quattro comandi, ognuno con la sua combinazione:

| Comando | |
|---|---|
| Entra nella modalita' | di default funziona anche da interruttore (stessi tasti per uscire) |
| Esci dalla modalita' | usato solo se togli la spunta *Usa gli stessi tasti per entrare e uscire* |
| Pack successivo / precedente | facoltativi |

Per ogni comando: **Assegna**, poi premi i tasti (fino a 4 insieme, tastiera, mouse e gamepad anche mescolati)
e rilasciali. `Esc` annulla (o il pulsante *Annulla*, o aspetta 8 secondi).
Tipi di pressione:

- **Pressione**: appena i tasti sono giu'
- **Tocco breve**: al rilascio, se tenuti al massimo N ms
- **Pressione prolungata**: dopo N ms di pressione continua (default 600)
- **Doppio tocco**: due tocchi brevi entro N ms

**Opzioni** - messaggio a schermo, "aggiorna subito le animazioni" (reset dell'idle dopo il cambio, saltato
durante attacchi, ragdoll, cavalcatura...), uscita automatica quando rinfoderi l'arma, blocco dei comandi
durante un attacco, lingua (it / en).

Tutto e' salvato in `Data/SKSE/Plugins/SwapMoveset/SwapMoveset.json` (con MO2 finisce in *Overwrite*).
Le combinazioni non partono mai mentre il menu F1, un menu di gioco o il dialogo sono aperti, e dopo un
cambio di configurazione o un alt-tab i tasti gia' premuti vengono ignorati finche' non li rilasci.

## Limiti noti

- I tasti vengono **letti**, non intercettati: oltre a far scattare il comando, fanno anche la loro azione
  normale nel gioco. Scegli combinazioni che non danno fastidio (per esempio una pressione prolungata).
- Un comando a *Pressione* su un singolo tasto parte prima che tu finisca di premere una combinazione che lo
  contiene (X parte prima di X+Y). Per le combinazioni usa *Prolungata* o *Tocco breve*.
- Le animazioni in loop cambiano al loop successivo; l'opzione di aggiornamento le cambia subito.
- Non e' salvato nello stato del gioco: a ogni caricamento o nuova partita la modalita' parte spenta.
- Se `SwapMoveset.dll` manca, un pack convertito viene segnalato da OAR come "plugin mancante" e non si attiva.

## Convertire un altro pack

```
python3 -I tools/swapmoveset_convert.py <cartella del mod> -o <uscita> --zip pack.zip
        [--id mio_pack] [--name "Mio Pack"] [--author qualcuno] [--order 100]
        [--drop-plugin "Altro.esp"] [--variant cartella_sottomod] [--notes "testo"] [--description "testo"]
        [--keep-equipment-conditions] [--strip-condition Nome]
```

Il mod in ingresso deve essere un pack OAR (cartella `meshes/.../OpenAnimationReplacer/<nome>/` con i
`config.json`). Lo script non modifica l'ingresso, copia le `.hkx` byte per byte e le verifica con SHA-256,
mette la condizione per prima in ogni sotto-mod, scrive il manifest del pack in
`SKSE/Plugins/SwapMoveset/Packs/<id>.json` e crea uno zip **con i file alla radice**.
Con `--drop-plugin` toglie le condizioni che richiedono un certo plugin (le considera soddisfatte) e
segnala tutti i plugin che le condizioni rimaste richiedono. I vecchi pack in stile DAR
(`_conditions.txt`) vengono rifiutati.

**Qualsiasi arma.** Di default toglie le condizioni che legano il moveset a quello che hai in mano
(`IsEquippedType`, `IsEquipped`, `IsEquippedHasKeyword`, ... anche dentro i gruppi AND/OR e anche se
negate; un gruppo che resta vuoto sparisce). Le altre condizioni (arma sfoderata, combattimento, effetti
magici, direzione...) restano. Opzioni:

- `--variant <cartella>`: per un sottomod che e' una *variante scelta dall'oggetto nella mano sinistra*
  (per esempio "con una magia a sinistra"). Toglie solo le condizioni sulla mano destra e lascia quelle
  sulla sinistra. Senza questa opzione una variante vincerebbe sempre sulla versione base (ha priorita'
  piu' alta): il convertitore lo segnala con un `WARNING` che indica anche quale opzione usare.
- `--keep-equipment-conditions`: non toglie niente (il pack resta legato all'arma per cui e' stato fatto).
- `--strip-condition Nome`: toglie anche un altro tipo di condizione (per esempio `IsWorn`).

Limite che nessuna opzione puo' togliere: OAR sostituisce un'animazione in base al *nome del file originale*.
Il pack sostituisce solo le animazioni che contiene (per esempio: stanza a una mano, magia, sprint,
salto, schivata, combo). Dove non ne ha, per esempio il movimento con arco o arma a due mani, il gioco usa le
sue.

Manifest del pack (`SKSE/Plugins/SwapMoveset/Packs/*.json`):

```json
{ "id": "mio_pack", "name": "Mio Pack", "author": "", "description": "", "notes": "", "order": 100 }
```

`id` usa solo `a-z 0-9 _ -` e deve coincidere con il campo `Pack Id` della condizione.

## Test

```
SANITIZE=1        tests/run_tests.sh            # logica pura: tasti, combinazioni, config, manifest
SANITIZE=thread   tests/run_runtime_tests.sh    # plugin contro un "gioco finto" (thread, salvataggi, menu)
python3 -I        tests/test_converter.py       # convertitore
```

Servono `g++` (C++23), `nlohmann-json` (`NLOHMANN_INCLUDE=<cartella>` se non e' installata di sistema) e Python 3.
I test del runtime non sostituiscono la compilazione vera: quella la fa la GitHub Action.

## Licenza

GPL-3.0 (vedi `COPYING`). I file in `src/oar_api` vengono dal plugin di esempio ufficiale di Open Animation
Replacer (GPL-3.0 con le eccezioni per il modding, vedi `src/oar_api/EXCEPTIONS`); l'unica modifica e'
descritta nell'intestazione di `OpenAnimationReplacerAPI-Conditions.cpp`.
