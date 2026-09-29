# Battleship multiplayer in C

Client/server TCP in C11 con lobby da terminale.

Compilazione e test:

Linux / macOS:
```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows:
```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Avviare il server (`./build/server` oppure `.\build\server.exe`) e poi uno o più client (`./build/client` oppure `.\build\client.exe`). Indirizzo e porta sono opzionali e usano `0.0.0.0:5000` per il server e `127.0.0.1:5000` per il client. Il protocollo è documentato in `FUNZIONAMENTO.md`.

Ogni giocatore riceve un PID e un token segreto. Il nome visualizzato non è univoco e non può essere usato per impersonare un'altra identità. Il client conserva il token in un file locale per poter tentare la riconnessione dopo un riavvio; il server conserva identità e sessioni soltanto in memoria.

Il server gestisce richieste concorrenti con un massimo di 64 worker TCP e un timeout di rete di 20 secondi. Durante una sessione il client invia un heartbeat ogni 5 secondi. Dopo due heartbeat mancanti la partita entra in pausa; il giocatore può rientrare entro 60 secondi. Se solo l'avversario resta connesso, allo scadere del termine la vittoria viene assegnata a lui. Se entrambi spariscono, la sessione viene rimossa dopo il timeout.

Le lobby abbandonate e le sessioni terminate vengono ripulite dal thread di manutenzione. Se l'host perde la connessione durante il posizionamento, l'ospite connesso diventa host, la flotta viene azzerata e il client richiede nuovamente il posizionamento quando arriva un nuovo avversario.

`Q` è resa e assegna la vittoria all'avversario. Dopo la partita `D` consente all'host di scegliere rivincita o nuovo avversario; sono accettati soltanto i comandi validi. Le griglie bersaglio mostrano soltanto `X` per un colpo a segno, senza rivelare il tipo di nave colpita.

Il codice è organizzato in moduli: `server.c` per listener/thread e manutenzione, `server_state.c` per autenticazione, comandi e sessioni, `client_net.c` per richieste TCP e heartbeat, `client_ui.c` per menu e griglie e `game.c` per le regole della plancia.
