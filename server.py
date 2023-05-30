#! /usr/bin/env python3
# server che fornisce l'elenco dei primi in un dato intervallo 
# gestisce più clienti contemporaneamente usando i thread
# invia il byte inutile all'inizio della connessione
import sys, struct, socket, threading, concurrent.futures, os, logging, argparse, subprocess, time, signal, stat, errno

Description = """Server python"""

# host e porta di default
HOST = "127.0.0.1"  # Standard loopback interface address (localhost)
PORT = 54861  # Port to listen on (non-privileged ports are > 1023)

# configurazione del logging
# il logger scrive su un file con nome uguale al nome del file eseguibile
logging.basicConfig(filename='server.log',
                    level=logging.DEBUG, datefmt='%d/%m/%y %H:%M:%S',
                    format='%(asctime)s - %(levelname)s - %(message)s')


def main(max_threads, host=HOST, port=PORT, process=None):
    signal.signal(signal.SIGUSR1, handle_sigusr1)
    # Percorso delle pipe FIFO
    directory_corrente = os.getcwd()
    #print(directory_corrente)

    pathSc="/tmp/caposc"
    pathLet="/tmp/capolet"

    # Creazione della pipe FIFO se non esistono con gestione errori
    if not os.path.exists(pathLet):
        try:
            os.mkfifo(pathLet)
            #print(f"Pipe creata con successo: {pathLet}")
        except OSError as e:
            print(f"Errore durante la creazione della pipe: {e}")
            logging.exception(f"Errore durante la creazione della FIFO capolet: {e}")
            sys.exit(1)
    else:
        print(f"La pipe esiste già: {pathLet}")

    if not os.path.exists(pathSc):
        try:
            os.mkfifo(pathSc)
            #print(f"Pipe creata con successo: {pathSc}")
        except OSError as e:
            print(f"Errore durante la creazione della pipe: {e}")
            logging.exception(f"Errore durante la creazione della FIFO capolet: {e}")
            sys.exit(1)
    else:
        print(f"La pipe esiste già: {pathSc}")

    # Apro le fifo
    try:
        fdLet = os.open(pathLet, os.O_WRONLY)
    except OSError as e:
        print(f"Errore durante l'apertura della FIFO capolet: {e}")
        logging.exception(f"Errore durante l'apertura della FIFO capolet: {e}")
        sys.exit(1)

    try:
        fdSc = os.open(pathSc, os.O_WRONLY)
    except OSError as e:
        print(f"Errore durante l'apertura della FIFO caposc: {e}")
        logging.exception(f"Errore durante l'apertura della FIFO caposc: {e}")
        sys.exit(1)


    # creiamo il server socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            # setsockopt() permette di riutilizzare l'indirizzo IP e la porta subito dopo la chiusura del socket.
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((host, port))
            # Il socket si mette in ascolto
            s.listen()
            with concurrent.futures.ThreadPoolExecutor(max_threads) as executor:
                while True:
                    #print("In attesa di un client....")
                    # mi metto in attesa di una connessione
                    conn, addr = s.accept()
                    # Faccio partire i thread per la gestione della connessione
                    executor.submit(gestisci_connessione, conn, addr, fdLet, fdSc)
        except KeyboardInterrupt:
            pass
        # In caso di SIGINT eseguo le operazioni indicate
        #print('Va bene smetto...')
        s.shutdown(socket.SHUT_RDWR)
        os.close(fdLet)
        #print('Ho chiuso Pipe Let...')
        os.close(fdSc)
        #print('Ho chiuso Pipe SC...')
        os.unlink(pathSc)
        os.unlink(pathLet)
        process.send_signal(signal.SIGTERM)


def gestisci_connessione(conn, addr, fdLet, fdSc):

    with conn:
        #print(f"{threading.current_thread().name} contattato da {addr}")
        # ---- invio un byte inutile (il codice ascii di x)
        conn.sendall(b'x')
        # Ricevo il tipo di comunicazione
        tipo_comunicazione = conn.recv(1).decode()
        if tipo_comunicazione == "A":
            gestisci_comunicazione_tipo_A(conn, fdLet)
        elif tipo_comunicazione == "B":
            gestisci_comunicazione_tipo_B(conn, fdSc)
    #print(f"{threading.current_thread().name} finito con {addr}")


def gestisci_comunicazione_tipo_A(conn, fd):
    data = recv_all(conn, 4)  # Ricevi la lunghezza della sequenza
    lunghezza = struct.unpack("!i", data)[0]
    data = recv_all(conn, lunghezza)  # Ricevi la sequenza
    # Codifiche e decodifiche per il C
    tmp = data.decode()
    sequenza = tmp.encode()
    numBytes = 0
    # Invio la sequenza in pipe
    try:
        numBytes += os.write(fd, struct.pack("I", len(sequenza)))
        numBytes += os.write(fd, sequenza)
        assert (numBytes <= 2052)
    except OSError as e:
        print(f"Errore durante la scrittura sulla FIFO: {e}")
        logging.exception(f"Errore durante la scrittura sulla FIFO: {e}")
        sys.exit(1)
    #logging.debug(f"Connessione di tipo A. Numero di byte scritto nella pipe: {numBytes}")
    logging.debug(f"A {numBytes}")
    # Elabora i dati ricevuti inviati nel log, chiudo la pipe


def gestisci_comunicazione_tipo_B(conn, fd):
    sequenze_ricevute = 0
    numBytes = 0
    while True:
        data = recv_all(conn, 4)  # Ricevi la lunghezza della sequenza
        lunghezza = struct.unpack("!i", data)[0]
        if lunghezza == 0:
            break  # Ricevuta sequenza di lunghezza 0, interrompi il ciclo
        sequenze_ricevute += 1
        data = recv_all(conn, lunghezza)  # Ricevi la sequenza
        # Codifiche e decodifiche per il C
        tmp = data.decode()
        sequenza = tmp.encode()
        # Invio la sequenza in pipe
        try:
            os.write(fd, struct.pack("I", len(sequenza)))
            os.write(fd, sequenza)
            numBytes += len(sequenza) + 4  # Aggiorna il numero di byte scritti
        except OSError as e:
            print(f"Errore durante la scrittura sulla FIFO: {e}")
            logging.exception(f"Errore durante la scrittura sulla FIFO: {e}")
            sys.exit(1)
    #logging.debug(f"Connessione di tipo B. Numero di byte scritto nella pipe: {numBytes}")
    logging.debug(f"B {numBytes}")
    conn.sendall(struct.pack("!i", sequenze_ricevute))
    # Elabora i dati ricevuti inviati nel log, chiudo la pipe e invio la risposta al server


# riceve esattamente n byte e li restituisce in un array di byte
# il tipo restituto è "bytes": una sequenza immutabile di valori 0-255
# analoga alla readn che abbiamo visto nel C
def recv_all(conn, n):
    chunks = b''
    bytes_recd = 0
    while bytes_recd < n:
        chunk = conn.recv(min(n - bytes_recd, 1024))
        if len(chunk) == 0:
            raise RuntimeError("socket connection broken")
        chunks += chunk
        bytes_recd = bytes_recd + len(chunk)
    return chunks

def handle_sigusr1(signum, frame):
    print("SIGUSR1")
    process.send_signal(signal.SIGUSR1)

if __name__ == '__main__':
    # parsing della linea di comando vedere la guida
    #    https://docs.python.org/3/howto/argparse.html
    parser = argparse.ArgumentParser()
    parser.add_argument('max_threads', type=int, help='Numero massimo di thread per la gestione dei client')
    parser.add_argument('-r', type=int, default=3, help='Numero di thread lettori')
    parser.add_argument('-w', type=int, default=3, help='Numero di thread scrittori')
    parser.add_argument('-v', action='store_true', help='Esegue con Valgrind')
    parser.add_argument('--host', default=HOST, help='Indirizzo host del server')
    parser.add_argument('--port', type=int, default=PORT, help='Porta del server')
    args = parser.parse_args()
    command = ["./archivio", str(args.r), str(args.w)]
    if args.max_threads is None:
        parser.error('Il numero massimo di thread è obbligatorio. Specificare il numero di thread.')

    if args.v:  # opzione valgrind
        command = ["valgrind", "-s", "--leak-check=full",
                   "--show-leak-kinds=all",
                   "--log-file=valgrind-%p.log",
                   "./archivio", str(args.r), str(args.w)]
    #  command = ["valgrind","-s","--leak-check=full", "--show-leak-kinds=all", "--log-file=valgrind-%p.log", "archivio", str(args.r), str(args.w)]
    # faccio partire archivio.c
    process = subprocess.Popen(command)
    # faccio partire server.py
    main(args.max_threads, args.host, args.port, process)