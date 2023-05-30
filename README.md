Il progetto implementa un'architettura client-server, in cui i client inviano delle sequenze al server, e il server fa processare i dati ad un programma chiamato archivio.
archivio è un programma che riceve dal server delle stringhe da server.py tramite delle pipe e gestisce la memorizzazione di stringhe in una tabella hash.

archivio.c:

Il thread capoScrittore apre la pipe caposc in lettura, ed in un ciclo che termina quando server.py chiude la pipe: legge la dimensione del messaggio ricevuto sulla pipe, legge il messaggio sulla pipe, tokenizza il messaggio con strtok_r (MT-Safe) e scrivo i token nel buffer condiviso con i thread scrittori con la tecnica prodCons (con uso di semafori). Uscito dal ciclo: Inserisce per numScrittori volte il valore di terminazione nel buffer prodCons e esce

Il thread capoLettore si comporta in modo simile: apre la pipe capolet in lettura, ed in un ciclo che termina quando server.py chiude la pipe: legge la dimensione del messaggio ricevuto sulla pipe, legge il messaggio sulla pipe, tokenizza il messaggio con strtok_r (MT-Safe) e scrivo i token nel buffer condiviso con i thread lettori con la tecnica prodCons (con uso di semafori). Uscito dal ciclo: Inserisce per numLettori volte il valore di terminazione nel buffer prodCons e esce.

Da notare che i buffer usati sono uno per la comunicazione lettori-Capo e uno per la comunicazione scrittori-Capo.

Abbiamo w #scrittori. Ogni thread scrittore entra in un ciclo in cui:
 Legge dal buffer condiviso cono capoScrittori tramite tecnica prodCons, salvando il dato letto nella variabile items. Dopo aver letto items, usando una tecnica lettori/scrittori (per sincronizzarsi con i lettori) chiama aggiungi(items) per scrivere sulla tabella hash.
 Lo schema è tale che lo scrittore:
    Acquisisce la mutua esclusione sul mutexHash, che funge da blocco per la sezione critica del codice, controlla se qualcun altro sta scrivendo sulla risorsa condivisa e controlla se ci sono altri thread che stanno leggendo dalla risorsa. Se questa condizione è vera, il thread si mette in attesa. Finita l'attesa, quindi non ci sono né scrittori né lettori attivi sulla risorsa condivisa, indica che inizio a scrivere e rilascia il mutex. Esegue quindi le operazioni di aggiunta della stringa letta (items) sulla tabella hash con aggiungi(). A questo punto, acquisisce il lock, indica che ha terminato di scrivere sulla risorsa, segnala a tutti i thread in attesa su condHash che ha finito di scrivere sulla risorsa condivisa e rilascia il mutex.
 Il ciclo termina quando legge dal buffer condiviso con capoScrittori il carattere di terminazione EXIT.


Abbiamo r #lettori. Ogni thread lettore entra in un ciclo in cui:
 Legge dal buffer condiviso cono capoLettori tramite tecnica prodCons, salvando il dato letto nella variabile items. Dopo aver letto items, usando una tecnica lettori/scrittori (per sincronizzarsi con gli scrittori) chiama conta(items), per leggere dalla tabella hash. 
 La tecnica è tale che il lettore:
    Aquisisce il mutexHash, che funge da blocco per la sezione critica del codice. Controlla se ci sono scrittori attivi sulla risorsa condivisa. Se questa condizione è vera, il thread si mette in attesa sulla condition variable condHash. Dopodichè, non essendoci sono più scrittori attivi sulla risorsa, il lettore incrementa di 1 il numero di lettori attivi (indica che inizia anche lui a leggere dall'hash) e rilascia il mutex. Esegue quindi le operazioni di lettura sulla tabella hash con conta(), ricevendo le occorrenze della stringa items nella tabella. A questo punto, acquisisce nuovamente il mutex, decrementa il numero di lettori attivi avendo lui finito, verifica se non ci sono più lettori attivi sulla risorsa condivisa. In tal caso, viene inviato un segnale a un solo scrittore e rilascia il mutex.
 I lettori hanno il compito di scrivere le occorrenze di ogni stringa items letta da capolett contate da conta() in un file di log. Quindi, tramite un'altro mutex per la sincronizzazione di questa operazione: Apro il file, scrivo le occorrenze di items e chiudo.
 Il ciclo termina quando legge dal buffer condiviso con capoLettori il carattere di terminazione EXIT.

L'apertura dei file avviene in modalità di append affinchè i dati scritti nel file vengono aggiunti alla fine senza sovrascrivere il contenuto già esistente.

Gli oggetti di tipo Entry* da inserire nella tabella hash (data) sono strutturati come una coppia : int valore (occorrenze nella tabella) e ENTRY *next (per il salvataggio della lista concatenata utile poi alla deallocazione e il conteggio delle stringhe distinte presenti).
Data una stringa s e un intero n la funzione ENTRY *crea_entry(char *s, int n):
 Alloca un elemento Entry* e, salva copia di s in e->key, alloca e->data e inizializza la coppia contenuta in e->data assegnando come campo valore e and come campo next NULL. Ritorna e.
 
La funzione di deallocazione void distruggi_entry(ENTRY *e) libera la memoria contenuta in e->key, in e->data e in  e.

Data una stringa s, la funzione void aggiungi(char *s) aggiunge una stringa nella tabella hash:
 Crea la entry con s, cerca se gono già presenti occorrenze di s, se non sono presenti, inserisce nella tabella e aggiunge in testa alla lista, altrimenti se la stringa è gia' presente incremento il suo valore nella tabella.

Data una stringa str, la funzione int conta(char *str) conta le occorrenze di una stringa str nella tabella:
 Cerca la stringa nella tabella. Se occorre, ritorna il suo valore. 

Salviamo appunto tutte i nodi contenenti le distinte stringhe aggiunte in una lista concatenata, utile poi per il conteggio e la deallocazione totale.

La funzione int len(ENTRY *lista) conta quante stringhe distinte sono presenti nella linked list. 
La funzione void distruggi_lista(ENTRY *lis) scorre tutta la lista e dealloca ogni elemento presente, garantendo la totale deallocazione della memoria.

Abbiamo un thread gestore dei segnali. Inizializza l'insieme di segnali mask vuoto e ci includo SIGINT, SIGTERM E   SIGUSR1. In un ciclo:
 Si mette in attesa finché uno dei segnali specificati nell'insieme mask viene ricevuto.
 1. Se riceve SIGINT: Accede in lettura alla tabella hash con la tecnica (lettori/scrittori) sopra descritta, e stampa il numero di stringhe distinte con len() su stderr. Non termina il programma.
 2. Se riceve SIGUSR1: Accede in scrittura alla tabella hash con la tecnica sopra descritta, chiama distruggi_lista(), seguita da hdestroy() e hcreate(). Svuota e ricrea la tabella.
 3. Se riceve SIGTERM: Aspetta capo lettore e capo scrittore, aspetta tutti i lettori e gli scrittori, stampa su stdout il numero di stringhe distinte con len(). Chiama distruggi_lista(), seguita da hdestroy() e dealloca tutta la memoria.
Il ciclo termina alla fine del blocco di gestione del segnale SIGTERM, uscendo e arrivando alla terminazione del programma.

Il main() di archivio.c alloca e istanzia tutti i parametri e fa partire i thread capoLettore, capoScrittore, i lettori e gli scrittori, il gestore dei segnali. Termina dopo aver aspettato la fine del gestore dei segnali con join().


server.py:
 Riceve dai client delle stringhe e manda queste sequenze ad archivio.c tramite pipe. Inizialmente crea un file server.log per il debug. Crea le fifo capolet e caposc, controllando se gia esistono, e le apre. Nota: Le metto nella cartella /tmp per problemi di permission denied. Crea il server socket, indica di riutilizzare l'indirizzo IP e la porta subito dopo la chiusura del socket, si mette in ascolto su HOST = "127.0.0.1" e PORT = 54861,  avvia maxThread concorrenti per gestire ogni connessione. Ogni connessione invia un byte inutile b'x' e riceve il tipo di connessione. Se la connessione è di tipo 'A':
    Riceve la lunghezza della sequenza, la sequenza, la decodifica, codifica e invia ad archivio.c tramite pipe capolet. Riceve una sequenza per connessione.
 Se la connessione è di tipo 'B':
    Entra in un ciclo in cui riceve la lunghezza della sequenza, la sequenza, la decodifica, codifica e invia ad archivio.c tramite pipe caposc. Riceve piu sequenze. Il ciclo termina quando il client manda la sequenza di lunghezza 0 per indicare che ha finito. Invia al client il numero di sequenze effettivamente ricevute.

 All'arrivo di SIGUSR1 non fa niente, all'arrivo di SIGINT: Da shutdown della socket, chiude e unlinka le pipe, manda il segnale SIGTERM ad archivio.c.
 La gestione della linea di comando è effettuata con argparse, ci pensa lui a far partire il processo archivio.

client1.c :
 Apre il file, e finchè sono presenti righe nel file:
  Imposta i caratteri finali '\n' a 0, effettua una connessione socket, riceve il byte nullo, invia il tipo di connessione (A), la lunghezza della sequenza e la riga letta. Apre una connessione per ogni riga letta del file.

client2 :
 Apre la socket, si connette, riceve il byte nullo, invia il tipo di connessione (B), e per ogni linea del file:
 Invia la lunghezza della linea, la linea. Inviate tutte le sequenze, controlla se il numero di sequenze ricevute equivale a quelle inviate. Una connessione per file, le sequenze vengono inviate una riga alla volta.

  