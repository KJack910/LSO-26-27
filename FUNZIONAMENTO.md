# Funzionamento attuale: Socket Hello World

Questo progetto implementa una comunicazione TCP molto semplice tra due programmi C:

- `server`: apre una porta TCP, attende connessioni e riceve una riga di testo.
- `client`: si collega all'indirizzo indicato, invia una riga e attende la risposta.

Il codice usa la libreria socket nativa del sistema operativo:

- Linux/macOS: socket POSIX.
- Windows: Winsock2, con collegamento alla libreria `ws2_32` tramite CMake.

## Flusso della comunicazione

1. Il server inizializza la rete e si mette in ascolto su `0.0.0.0:5000`.
2. Il client risolve l'indirizzo del server e apre una connessione TCP.
3. Il client invia il messaggio, terminato da `\\n`.
4. Il server legge la riga e stampa il messaggio ricevuto.
5. Il server risponde con `Hello from server!`.
6. Il client stampa la risposta e chiude la connessione.

TCP gestisce la consegna ordinata dei dati, ma non conserva il concetto di "messaggio". Per questo esempio il progetto usa il carattere newline come delimitatore della riga.

## Client e server sulla stessa macchina

Il caso più semplice usa l'interfaccia locale (`127.0.0.1`). I dati non escono dalla macchina.

Terminale 1:

```text
./build/server 127.0.0.1 5000
```

Terminale 2:

```text
./build/client 127.0.0.1 5000
```

Su Windows, usare gli eseguibili generati da CMake, ad esempio:

```text
build\\Debug\\server.exe 127.0.0.1 5000
build\\Debug\\client.exe 127.0.0.1 5000
```

Per una prova automatica a singola connessione:

```text
./build/server 127.0.0.1 5000 --once
./build/client 127.0.0.1 5000
```

Con `--once` il server termina dopo aver gestito un client; senza questa opzione continua ad accettare connessioni.

## Client e server su macchine diverse

Le due macchine devono essere raggiungibili nella stessa rete o tramite routing configurato.

Sul computer che esegue il server:

```text
./build/server 0.0.0.0 5000
```

Sul computer client, usare l'indirizzo IPv4 del server, non `127.0.0.1`:

```text
./build/client 192.168.1.100 5000
```

In questo esempio `192.168.1.100` deve essere sostituito con l'IP reale del computer server.

Requisiti di rete:

- porta TCP 5000 aperta sul firewall del server;
- indirizzo IP del server raggiungibile dal client;
- nessun NAT o router che blocchi la porta;
- stesso numero di porta su server e client.

Su Windows è necessario consentire l'eseguibile o la porta TCP nel Windows Firewall. Su Linux, verificare anche `ufw`, `firewalld` o eventuali regole `iptables/nftables`.

## Parametri disponibili

Server:

```text
server [host] [porta] [--once]
```

- `host`: indirizzo locale su cui ascoltare; il valore predefinito è tutte le interfacce (`0.0.0.0`).
- `porta`: predefinita `5000`.
- `--once`: chiude il server dopo il primo client.

Client:

```text
client [host] [porta] [messaggio]
```

- `host`: indirizzo del server; predefinito `127.0.0.1`.
- `porta`: predefinita `5000`.
- `messaggio`: predefinito `Hello from client!`.

Esempio con messaggio personalizzato:

```text
./build/client 192.168.1.100 5000 "Messaggio da un altro computer"
```

## Limiti attuali

- Il server gestisce i client in sequenza, non in parallelo.
- La comunicazione non è cifrata e non prevede autenticazione.
- Il protocollo gestisce una riga per richiesta.
- Il programma è un esempio didattico, non un servizio pronto per Internet.
- È configurato per IPv4; non è stata aggiunta la gestione IPv6.

Per uso reale andrebbero aggiunti almeno autenticazione, TLS, gestione concorrente dei client, timeout e validazione più completa degli input.
