# Funzionamento e protocollo

## Avvio

**Linux / macOS:**
```sh
cmake -S . -B build && cmake --build build
./build/server [indirizzo-bind] [porta] # predefiniti 0.0.0.0:5000
./build/client [host] [porta]             # predefiniti 127.0.0.1:5000
ctest --test-dir build --output-on-failure
```

**Windows (PowerShell / Prompt dei comandi):**
```powershell
cmake -S . -B build
cmake --build build
.\build\server.exe [indirizzo-bind] [porta] # predefiniti 0.0.0.0:5000
.\build\client.exe [host] [porta]           # predefiniti 127.0.0.1:5000
ctest --test-dir build --output-on-failure
```

Il server accetta richieste TCP in thread separati (massimo 64 worker, timeout socket 20 secondi); il mutex protegge ID, lobby e sessioni. Ogni connessione trasporta una richiesta UTF-8 terminata da newline e riceve una singola risposta. Nessun registro giocatori viene scritto su disco: nomi, ID e sessioni sono in memoria e si azzerano alla chiusura del server. Gli ID sessione hanno forma `S` + cinque cifre, per esempio `S00001`.

## Comandi di rete

Campi separati da `|`; coordinate 0-9. `pid` è l'ID numerico assegnato dal server.

- `HELLO|nome` → `OK|pid|nome`; lo stesso nome riutilizza l'ID solo durante l'esecuzione corrente del server.
- `LIST` → `OK|sid,nome-host,pending|...` per sessioni non ancora avviate/accettate.
- `CREATE|pid` → `OK|sid`; rifiuta se il giocatore ha già una sessione attiva.
- `JOIN|pid|sid` → registra la richiesta; il proprietario deve inviare `DECIDE|host-pid|sid|1` (accetta) o `...|0` (rifiuta). Un giocatore già occupato non può unirsi a un'altra sessione.
- `STATUS|pid|sid` → host, ospite e indicatori pending/accepted/started/over.
- `VIEW|pid|sid` → `OK|fase|ruolo|turno|griglia-propria|bersagli`; le due stringhe griglia hanno 100 simboli ciascuna, in ordine per riga. Nella griglia propria ogni cella nave intatta è indicata con una lettera minuscola (`a`=portaerei/5, `b`=corazzata/4, `c`=incrociatore/3, `d`=sottomarino/3, `e`=cacciatorpediniere/2); la stessa lettera in maiuscolo indica una cella colpita. `o` acqua colpita, `.` cella non colpita. Nella griglia bersaglio le navi avversarie non vengono esposte: solo `X` colpito, `o` mancato, `.` inesplorato. Il client interpreta le lettere e disegna ogni nave con frecce e lunghezza: `<L … L>` orizzontale, `^L / |L / vL` verticale, `[L]` isolata (dove L è la lunghezza della nave).
- `PLACE|pid|sid|riga|colonna|H-or-V|lunghezza` → colloca in ordine la flotta 5,4,3,3,2. Il server rifiuta sovrapposizioni, orientamenti/coordinate non validi e lunghezze fuori ordine.
- `READY|pid|sid` → avvia quando entrambi hanno collocato la flotta e sono pronti.
- `SHOT|pid|sid|riga|colonna` → consentito solo al giocatore di turno; rifiuta colpi ripetuti e coordinate errate, restituisce MISS/HIT/SUNK e WIN a fine partita.
- `SURRENDER|pid|sid` → termina una partita in corso con sconfitta del giocatore che si ritira.
- `REMATCH|pid|sid|same` → su decisione dell'host reimposta la partita mantenendo l'ospite.
- `REMATCH|pid|sid|new` → su decisione dell'host rimuove l'ospite e rende nuovamente visibile la sessione in attesa.
- `LEAVE|pid|sid` → esce soltanto a partita terminata; se esce l'host mentre l'ospite è presente, quest'ultimo viene promosso a host.
- `QUIT|pid` → autorizza l'uscita dal client solo se non ci sono sessioni attive.

## Ambito e limitazioni

Nomi e ID giocatore sono volatili e non vengono registrati su file. Ogni giocatore può appartenere a una sola sessione attiva e non può crearne un'altra né uscire finché la partita non è terminata. Dopo la creazione il client entra direttamente nella lobby della sessione; il client ospite vi entra subito dopo aver richiesto l'accesso. Il posizionamento guidato e la dichiarazione di prontezza avvengono prima dei turni. Le due griglie sono stampate affiancate e aggiornate dopo ogni azione. `Q` comporta resa e sconfitta; a partita conclusa `D` presenta all'host le scelte di rivincita/nuovo avversario e `E` esce dalla sessione. Le sessioni si perdono al riavvio. Il valore `sid` è un codice breve `S` seguito da cinque cifre e non è durevole.

Il trasferimento host avviene su `LEAVE` esplicito a partita terminata, non su disconnessione improvvisa. Gli ID player vengono riassegnati a ogni avvio del server; il nome non è autenticato e non protegge dall'impersonificazione. Non sono ancora presenti una procedura guidata per riconnettere un giocatore dopo una caduta, né build verificate Windows/macOS.

`server.c` gestisce listener e thread; `server_state.c` gestisce i comandi e le sessioni; `client_net.c` incapsula le richieste TCP e `client_ui.c` gestisce menu e griglie; `game.c` contiene le regole della griglia 10x10. `ctest` esegue i test unitari delle regole e il test d'integrazione del protocollo.
