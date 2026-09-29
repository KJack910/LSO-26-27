# Funzionamento e protocollo

## Avvio

Linux / macOS:
```sh
cmake -S . -B build && cmake --build build
./build/server [indirizzo-bind] [porta]
./build/client [host] [porta]
ctest --test-dir build --output-on-failure
```

Windows:
```powershell
cmake -S . -B build
cmake --build build
.\build\server.exe [indirizzo-bind] [porta]
.\build\client.exe [host] [porta]
ctest --test-dir build --output-on-failure
```

Il server usa thread separati per le richieste e un thread di manutenzione. Il limite è di 64 worker TCP; il timeout del socket è 20 secondi. Il mutex protegge giocatori, lobby e sessioni. Ogni richiesta è una riga UTF-8 terminata da newline e riceve una singola risposta. Gli ID sessione hanno forma `S` + cinque cifre.

## Identità e autenticazione

I campi sono separati da `|`; le coordinate sono 0-9. `pid` è l'ID numerico assegnato dal server e `token` è un segreto esadecimale generato dal server.

`HELLO|nome` crea una nuova identità e restituisce `OK|pid|nome|token`. Il nome non è un identificatore univoco: due giocatori con lo stesso nome ricevono due PID e token distinti. Per riprendere un'identità già salvata si usa `HELLO|nome|token`; senza il token corretto il server rifiuta la richiesta.

Tutti i comandi che accedono a un giocatore richiedono il token come ultimo campo. Il client salva il token in un file locale `.battleship_<host>_<porta>_<nome>.id`, così una nuova esecuzione può tentare la riconnessione senza consentire l'accesso a chi conosce soltanto il nome.

## Comandi di rete

- `LIST` → `OK|sid,nome-host,pending|...` per sessioni aperte.
- `CREATE|pid|token` → `OK|sid`; rifiuta se il giocatore ha già una sessione attiva.
- `JOIN|pid|sid|token` → registra la richiesta; l'host usa `DECIDE|pid|sid|1|token` per accettare o `...|0|token` per rifiutare.
- `STATUS|pid|sid|token` → restituisce host, ospite e indicatori pending/accepted/started/over.
- `RESUME|pid|token` → `OK|sid` per una sessione ancora assegnata al giocatore, anche se è già `FINISHED`; questo permette di vedere una sconfitta a tavolino e scegliere la rivincita.
- `HEARTBEAT|pid|sid|token` → mantiene online il giocatore e rimuove `PAUSED` quando entrambi sono rientrati.
- `VIEW|pid|sid|token` → `OK|fase|ruolo|turno|griglia-propria|bersagli|vincitore`.
- `PLACE|pid|sid|riga|colonna|H-or-V|lunghezza|token` → colloca la flotta 5,4,3,3,2.
- `READY|pid|sid|token` → dichiara pronta la flotta e avvia la partita quando entrambi sono pronti.
- `SHOT|pid|sid|riga|colonna|token` → effettua un tiro e restituisce `MISS`, `HIT`, `SUNK` o `WIN`.
- `SURRENDER|pid|sid|token` → assegna immediatamente la vittoria all'avversario e mantiene la sessione.
- `REMATCH|pid|sid|same|token` → l'host reimposta la partita mantenendo l'ospite.
- `REMATCH|pid|sid|new|token` → l'host rimuove l'ospite e riapre la lobby.
- `LEAVE|pid|sid|token` → esce da una partita terminata; l'ospite viene promosso a host se l'host esce.
- `QUIT|pid|token` → libera il giocatore. In una lobby non iniziata rimuove correttamente la sessione o trasferisce l'host; durante una partita attiva viene rifiutato.

In `VIEW`, entrambe le griglie hanno 100 simboli. La griglia propria mostra le navi intatte con `P`, `C`, `S`, `I`, `K`, i colpi sull'acqua con `o` e l'acqua inesplorata con `.`. La griglia bersaglio mostra soltanto `X` per un colpo a segno, `o` per un colpo mancato e `.` per una cella inesplorata: il tipo della nave avversaria non viene trasmesso.

## Presenza, pausa e pulizia

Il client invia heartbeat ogni 5 secondi. Dopo due heartbeat mancanti, una partita iniziata passa a `PAUSED`. La riconnessione tramite `RESUME`/`HEARTBEAT` riprende la partita. Se soltanto un giocatore rimane connesso per 60 secondi dall'ultimo heartbeat dell'avversario, gli viene assegnata la vittoria e la fase diventa `FINISHED`. Se entrambi risultano disconnessi, la sessione viene rimossa dopo lo stesso timeout, senza lasciare una sessione fantasma.

La manutenzione controlla anche le lobby non ancora iniziate e le sessioni terminate. Se l'host abbandona una lobby con un ospite connesso, l'ospite viene promosso a host, la flotta viene azzerata e la sessione torna in attesa di un nuovo avversario. Se entrambi i membri di una sessione terminata risultano disconnessi, la sessione viene liberata.

Il client resetta il contatore locale delle navi quando rileva una nuova partita o il passaggio da ospite a host, evitando il softlock del posizionamento. Le scelte post-partita accettano soltanto `1` o `2`; un input vuoto o diverso non espelle l'avversario.

`server.c` gestisce listener e thread; `server_state.c` gestisce autenticazione, comandi, heartbeat, timeout e sessioni; `client_net.c` incapsula le richieste TCP e il monitor di presenza; `client_ui.c` gestisce menu e griglie; `game.c` contiene le regole della plancia 10x10.
