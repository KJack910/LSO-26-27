# Socket Hello World in C

Il funzionamento dettagliato e gli esempi per comunicare tra macchine diverse sono descritti in [FUNZIONAMENTO.md](FUNZIONAMENTO.md).

Esempio minimale di comunicazione TCP tra un client e un server scritto in C.
Il codice usa Winsock su Windows e i socket POSIX su Linux/macOS.

## Requisiti

- CMake 3.15 o superiore
- Un compilatore C11
- Linux, Windows o macOS

## Compilazione

Da questa cartella:

```text
cmake -S . -B build
cmake --build build
```

Gli eseguibili vengono creati nella cartella `build/` (su Visual Studio, nella relativa sottocartella di configurazione).

## Esecuzione su Linux/macOS

Terminale 1:

```text
./build/server
```

Terminale 2:

```text
./build/client
```

## Esecuzione su Windows

Prompt dei comandi o PowerShell:

```text
build\\Debug\\server.exe
build\\Debug\\client.exe
```

Con generatori che producono direttamente gli eseguibili, usare il percorso creato da CMake.

## Parametri

Server:

```text
server [host] [porta] [--once]
```

Il server ascolta di default su `0.0.0.0:5000` e continua ad accettare client.
Con `--once` gestisce una connessione e termina; è utile per i test.

Client:

```text
client [host] [porta] [messaggio]
```

Il client usa di default `127.0.0.1:5000` e invia `Hello from client!`.

Per collegare due computer nella stessa rete:

```text
# Computer server
server 0.0.0.0 5000

# Computer client: sostituire l'IP
client 192.168.1.100 5000
```

Su Windows potrebbe essere necessario consentire l'eseguibile nel firewall per la porta TCP scelta.

## Protocollo

- TCP su IPv4
- Testo UTF-8
- Ogni messaggio termina con `\\n`
- Risposta del server: `Hello from server!`
