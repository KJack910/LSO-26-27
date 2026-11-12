# Documentazione Laboratorio di Sistemi Operativi
# Battaglia navale multiplayer
## Impostazione tecnica, strutturale e matrice di verifica

**Gruppo di progetto:**
* Casucci Christian — N86004569
* Leandro Esposito — N86005400

*Data di allineamento: 12 novembre 2026*

---

## 1. Introduzione

### 1.1 Obiettivo della traccia
La traccia «Progetto 2: Battaglia navale» richiede la realizzazione di un server multi-client per partite tra due giocatori con:
* Creazione ed identificazione della sessione tramite codice univoco visibile a tutti i client.
* Richiesta di ingresso da parte di un partecipante e decisione (accettazione o rifiuto) dell'host.
* Fase di posizionamento delle navi con validazione su server di limiti griglia, orientamento e assenza di sovrapposizioni.
* Fase di combattimento a turni alternati con validazione di mosse e colpi.
* Notifica dell'esito finale della partita a entrambi i partecipanti distinguendo tra vittoria e sconfitta.
* Gestione post-partita con facoltà per i giocatori di avviare una nuova sfida (rivincita) o uscire definitivamente dalla sessione.

### 1.2 Soluzione realizzata
Il prototipo è sviluppato in standard **C11**, con client interattivo da terminale e server TCP concorrente. Il server mantiene in memoria l'intero stato delle lobby, delle griglie e l'avanzamento dei turni, garantendo la totale assenza di arbitraggio da parte dei client. Sono inoltre implementati:
* Sistema di autenticazione stateless mediante PID numerico e token crittograficamente sicuro a 128 bit.
* Monitoraggio della presenza con heartbeat periodico (5s).
* Gestione della disconnessione temporanea con pausa automatica e ripristino (`RESUME`).
* Risoluzione per timeout (60s) con assegnazione della vittoria a tavolino al giocatore superstite.

### 1.3 Versione analizzata e allineata
La presente documentazione descrive il sistema aggiornato alla revisione corrente di `main`.
Tutti i test automatizzati sono stati verificati ed eseguiti con successo:
* Build `Debug` compilata con GCC (`-Wall -Wextra -Wpedantic`) **senza alcun warning**.
* Suite CTest: **100% superata** (`game_rules`, `server_state_rules` e `server_protocol`).

---

## 2. Documento dei requisiti

### 2.1 Mappatura requisiti funzionali (RF)
* **RF-01 — Server multi-client:** `server.c` ascolta su porta TCP configurabile, accetta connessioni concorrenti e delega la gestione delle richieste a worker dedicati all'interno di un pool thread-safe (massimo 64 worker simultanei).
* **RF-02 — Creazione ed elenco sessioni:** Il comando `CREATE` genera una sessione con identificativo univoco progressivo (es. `S00001`). Il comando `LIST` restituisce l'elenco delle partite aperte e il loro stato.
* **RF-03 — Richiesta e decisione d'accesso:** Un secondo client richiede l'adesione con `JOIN`. L'host riceve la notifica e decide con `DECIDE` (1 per accettare, 0 per rifiutare). Solo dopo l'accettazione la lobby avanza alla fase successiva.
* **RF-04 — Posizionamento della flotta:** Ogni giocatore colloca le 5 navi previste (Portaerei da 5, Corazzata da 4, Sottomarino da 3, Incrociatore da 3, Cacciatorpediniere da 2) tramite comando `PLACE`. Il server valida rigidamente orientamento (H/V), adiacenza, sovrapposizione e bordi. Il comando `READY` conferma la flotta; quando entrambi sono pronti, la partita passa a `PLAYING`.
* **RF-05 — Combattimento a turni:** Tramite comando `SHOT`, i giocatori dichiarano le coordinate di tiro a turni alternati. Il server valida il turno, applica il colpo sulla plancia avversaria e risponde con `MISS`, `HIT`, `SUNK` o `WIN`.
* **RF-06 — Comunicazione dell'esito:** Al colpo che affonda l'ultima nave, il server assegna il vincitore e la partita passa a `FINISHED`. Il colpo vincente riceve `OK|...|WIN`. Entrambi i partecipanti interrogano `VIEW` e ricevono il nome del vincitore; il client confronta l'identità e mostra in modo distinto ed esplicito la schermata di **VITTORIA** o **SCONFITTA**.
* **RF-07 — Post-partita e rivincita:** A fine partita, l'host può richiedere la rivincita (`REMATCH|same`) o aprire la stanza a un nuovo avversario (`REMATCH|new`). In alternativa, i giocatori possono abbandonare definitivamente la sessione tramite `LEAVE`.

### 2.2 Casi d'uso e flusso operativo
1. **Identità:** All'avvio, il client effettua `HELLO|nome` ottenendo un PID numerico e un token esadecimale, salvato localmente su file per eventuali riconnessioni.
2. **Lobby:** L'host crea la partita; l'ospite si unisce; l'host accetta.
3. **Flotta:** Entrambi i giocatori inseriscono le 5 navi secondo le regole classiche.
4. **Gioco:** L'host effettua il primo tiro. Ad ogni mossa valida il turno si alterna. In caso di resa (`SURRENDER`), la vittoria è immediatamente conferita all'avversario.
5. **Esito:** Il client stampa la notifica personalizzata di vittoria/sconfitta e mostra il menu per nuova sfida o uscita.

---

## 3. System Design

### 3.1 Architettura
L'architettura adotta il paradigma client-server con protocollo a riga di comando testuale UTF-8 terminato da newline. Ogni transazione apre una connessione TCP breve (request/response), garantendo scalabilità e totale disaccoppiamento dei worker dallo stato globale, che risiede in memoria protetto da un mutex dedicato.

### 3.2 Moduli e responsabilità
* **`server.c`**: Listener TCP, pool worker concorrenti, thread periodico di manutenzione heartbeat/timeout.
* **`server_state.c`**: Gestione atomica di identità, sessioni, token, autorizzazioni, logica delle lobby e avanzamento stati.
* **`game.c`**: Motore matematico e di validazione della scacchiera 10x10 (posizionamento, sovrapposizioni, collisioni, tiri, affondamenti).
* **`client_net.c`**: Incapsulamento delle comunicazioni di rete TCP e thread in background per l'invio degli heartbeat e il rilevamento dello stato di pausa.
* **`client_ui.c`**: Interfaccia a caratteri (CLI con supporto colori ANSI), acquisizione input utente, rendering grafico affiancato delle scacchiere e messaggi di stato.

---

## 4. Software Design e Raffinamenti Implementativi

### 4.1 Risoluzione del Warning GCC (`-Wrestrict` su `server_state.c:242`)
Nella precedente stesura del codice, la promozione dell'ospite a host eseguiva una copia diretta tra campi appartenenti alla stessa istanza della struct `Session`:
```c
snprintf(session->host_name, sizeof(session->host_name), "%s", session->guest_name);
```
Poiché il primo argomento di `snprintf` è qualificato con `restrict`, l'analizzatore statico di GCC con `-Wall -Wextra` segnalava un potenziale aliasing di memoria (`warning: 'snprintf' argument 4 overlaps destination object 'sessions'`).
**Soluzione applicata:** È stato introdotto un buffer temporaneo locale sullo stack:
```c
char promoted_name[NAME_LEN];
snprintf(promoted_name, sizeof(promoted_name), "%s", session->guest_name);
snprintf(session->host_name, sizeof(session->host_name), "%s", promoted_name);
```
Il disaccoppiamento tra sorgente e destinazione elimina l'aliasing e consente la compilazione con `-Werror` a zero avvisi.

### 4.2 Allineamento Griglie e Flotta dopo Timeout
In caso di promozione dell'ospite a host dopo il timeout dell'avversario, la scacchiera dell'ospite (`board[1]`) e i relativi contatori vengono copiati nello slot `board[0]`. In questo modo la vista personale dell'utente promosso rispecchia fedelmente la propria flotta senza inversioni di prospettiva.

### 4.3 Distinzione Soggettiva Esito Finale (Vittoria / Sconfitta)
In ossequio al requisito della traccia (*«il server comunica l’esito finale ai due partecipanti, distinguendo tra vittoria e sconfitta»*), il client in `client_ui.c` estrae il vincitore dal campo 6 di `VIEW` e confronta la stringa con il nome del giocatore corrente:
* Se `strcmp(winner, player_name) == 0`: stampa a schermo `*** VITTORIA! Congratulazioni, hai vinto la partita! ***` (in verde).
* Altrimenti: stampa `*** SCONFITTA! Tutte le tue navi sono state affondate. ***` (in rosso).

---

## 5. Matrice di Test Completa

### 5.1 Regole di Gioco (`game_rules`)
Tutti i test verificano le regole di piazzamento e combattimento della scacchiera 10x10 in `tests/test_game.c`.

| ID | Caso Verificato | Comportamento Atteso / Evidenza | Esito |
|---|---|---|---|
| **T01** | Piazzamento valido | Nave collocata con successo all'interno dei limiti | **PASS** |
| **T02** | Sovrapposizione navi | Rifiuto piazzamento se collide con nave esistente | **PASS** |
| **T03** | Nave oltre bordo | Rifiuto piazzamento se sporge dalla griglia 10x10 | **PASS** |
| **T04** | Colpo ripetuto | Secondo tiro sulle medesime coordinate rifiutato (`GAME_INVALID`) | **PASS** |
| **T05** | Acqua e colpo a segno | Ritorno di `GAME_MISS` su acqua e `GAME_HIT` su scafo | **PASS** |
| **T06** | Affondamento nave e flotta | Notifica `GAME_SUNK` e rilevamento flotta interamente distrutta | **PASS** |

### 5.2 Stato e Protocollo di Rete (`server_state_rules` & `server_protocol`)
Verifica dei comandi del protocollo, gestione sessioni e autenticazione in `tests/test_server_state.c` e `tests/test_server.py`.

| ID | Caso Osservato | Evidenza nel Test | Esito |
|---|---|---|---|
| **T07** | Resa e assegnazione vittoria | `SURRENDER` assegna la vittoria all'avversario; `VIEW` riporta vincitore | **PASS** |
| **T08** | Token e nomi duplicati | Nomi identici ricevono PID/token distinti; token errati respinti | **PASS** |
| **T09** | Identità, adesione e flotta | Sequenza `HELLO`, `JOIN`, `DECIDE`, `PLACE`, `READY` pienamente validata | **PASS** |
| **T10** | Riservatezza griglia bersagli | Griglia nemica rivela solo `X` e `o`, senza svelare navi intatte | **PASS** |
| **T11** | RESUME dopo timeout | Host disconnesso per >60s rimosso dalla sessione; risponde `ERR\|no active session` | **PASS** |
| **T12** | Pausa e promozione | Disconnessione temporanea pone in `PAUSED`; mancato rientro promuove l'ospite | **PASS** |
| **T13** | Rientro e ripresa | `HEARTBEAT` entro il timeout rimuove `PAUSED` e riprende la partita | **PASS** |
| **T14** | Client interattivo | Smoke test di avvio, handshake, consultazione menu e uscita pulita (`QUIT`) | **PASS** |

### 5.3 Scenari di Integrazione e Flusso Completo

| ID | Scenario | Obiettivo e Verifica | Esito |
|---|---|---|---|
| **P01** | RESUME dopo timeout | Conferma del rilascio sessione e notifica errore all'host rimosso | **PASS** |
| **P02** | Host assente pre-gioco | Pausa, mancato rientro e promozione ospite durante la lobby | **PASS** |
| **P03** | Rivincita completa | Reset totale flotte, nuovo `PLACE` e transizione `FINISHED -> PLACEMENT -> PLAYING` | **PASS** |
| **P04** | Risultato e uscita finale | Trasmissione vincitore a entrambi; gestione uscita ospite e rilascio finale con `LEAVE` | **PASS** |

> **Nota di completezza:**
> I test P03 e P04 sono integrati direttamente nella suite C (`test_server_state`) e verificati via protocollo TCP in `test_server.py`. I requisiti accessori non previsti dalla traccia d'esame (come crittografia TLS o test cross-platform non-Linux) sono stati esclusi dal perimetro di verifica per mantenere l'aderenza puntuale alle specifiche di progetto.

---

## 6. Istruzioni di Compilazione e Riproduzione Test

### Ambiente Linux / macOS
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Ambiente Windows (PowerShell)
```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
Risultato atteso: **100% tests passed, 0 failures**.

