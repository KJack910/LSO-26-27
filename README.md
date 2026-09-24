# Battleship multiplayer in C

Client/server TCP in C11 con lobby da terminale. Compilazione e test:

**Linux / macOS:**
```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

**Windows (PowerShell / Prompt dei comandi):**
```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Avviare il server (`./build/server` su Linux/macOS, `.\build\server.exe` su Windows. Indirizzo e porta opzionali predefiniti `0.0.0.0:5000`) e poi uno o più client (`./build/client` su Linux/macOS, `.\build\client.exe` su Windows). Il nome e l'ID numerico del giocatore vivono solo fino alla chiusura del server. Il protocollo TCP usa righe terminate da newline; comandi e risposte sono descritti in `FUNZIONAMENTO.md`.

Il server gestisce richieste concorrenti con thread (massimo 64 worker; timeout di rete 20 secondi) e protegge lo stato condiviso con un mutex. Non viene creato alcun registro persistente dei giocatori: nomi, ID e sessioni sono volatili e si azzerano all'arresto del server. Il nome non è autenticato: si tratta di un prototipo didattico, non di un servizio sicuro per Internet.

Ogni giocatore può avere una sola sessione attiva: dopo la creazione l'host entra direttamente nella lobby della sessione; l'ospite richiede l'accesso dalla lobby principale e l'host decide lì se accettarlo. Le sessioni hanno ID del formato `S00001` (lettera S e cinque cifre). Dopo l'accettazione, il posizionamento guidato delle navi e la conferma di prontezza avvengono prima della partita. Le due griglie sono affiancate e si aggiornano a ogni azione. `Q` è resa e sconfitta; dopo la partita `D` permette all'host la rivincita o un nuovo avversario, mentre `E` esce dalla sessione. Il server valida posizionamenti, turni, colpi ripetuti e vittoria.

Il codice è organizzato in moduli: `server.c` per socket/thread, `server_state.c` per stato e comandi, `client_net.c` per richieste TCP, `client_ui.c` per menu e griglie e `game.c` per le regole della plancia.

**Limiti:** gli ID sessione sono univoci solo durante l'esecuzione del server, non UUID standard né persistenti. Mancano autenticazione, TLS e rilevamento delle disconnessioni improvvise: il trasferimento host avviene solo con `LEAVE` esplicito dopo la partita. Non è ancora presente il flusso guidato per attendere/riconnettere un giocatore caduto. Windows/macOS non sono stati verificati in questo ambiente.
