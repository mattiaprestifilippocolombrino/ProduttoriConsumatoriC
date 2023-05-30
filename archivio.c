#include "xerrori.h"
#include <search.h>
#include <signal.h>
#define Num_elem 1000000 // dimensione della tabella hash
#define PC_buffer_len 10 // lunghezza dei buffer produttori/consumatori

// struct per i data dell'hash
typedef struct
{
    int valore;
    ENTRY *next;
} coppia;

// struct per i parametri di capoScrittore
typedef struct
{
    // Uso semafori per buffer prodCons tra capo e scrittori
    int *pIndexSC;
    char **bufferSC;
    sem_t *sem_free_slotsSC;
    sem_t *sem_data_itemsSC;
    int numProd; // num di scrittori, per inserire il carattere di terminazione
} capoScrittoriDati;

// struct per i parametri di capoLettore
typedef struct
{
    // Uso semafori per buffer prodCons tra capo e lettori
    int *pIndexLett; // indice nel buffer
    char **bufferLett;
    sem_t *sem_free_slotsLett;
    sem_t *sem_data_itemsLett;
    int numCons; // num di lettori, per inserire il carattere di terminazione
} capoLettoriDati;

typedef struct
{
    // Params comunicazione capo-sub scrittori
    int *cIndexSC; // indice nel buffer
    char **bufferSC;
    sem_t *sem_free_slotsSC;
    sem_t *sem_data_itemsSC;
    pthread_mutex_t *mutexConsSC;

    // Params comunicazione lettori-scrittori hash
    int *readers;
    bool *writing;
    pthread_cond_t *condHash;   // condition variable
    pthread_mutex_t *mutexHash; // mutex associato alla condition variable
} scrittoriDati;

typedef struct
{
    // Params comunicazione capo-sub lettori
    int *cIndexLett; // indice nel buffer
    char **bufferLett;
    pthread_mutex_t *mutexConsLett;
    sem_t *sem_free_slotsLett;
    sem_t *sem_data_itemsLett;
    pthread_mutex_t *mutexLog;

    // Params comunicazione lettori-scrittori hash
    int *readers;
    bool *writing;
    pthread_cond_t *condHash;   // condition variable
    pthread_mutex_t *mutexHash; // mutex associato alla condition variable
} lettoriDati;

typedef struct
{
    // Params comunicazione capo-sub lettori
    pthread_t *capoLettore;
    pthread_t *capoScrittore;
    pthread_t *lettori;
    pthread_t *scrittori;
    char **bufferLett;
    char **bufferSC;

    int numProd;
    int numCons;
    // reset
    int *readers;
    bool *writing;
    pthread_cond_t *condHash;   // condition variable
    pthread_mutex_t *mutexHash; // mutex associato alla condition variable
} gestoreDati;

ENTRY *testa_lista_entry = NULL;

void distruggi_lista(ENTRY *lis);
ENTRY *crea_entry(char *s, int n);
void aggiungi(char *s);
int conta(char *s);
void *capoLettoreBody(void *v);
void *capoScrittoreBody(void *v);
void *scrittoriBody(void *v);
void *lettoriBody(void *v);
ssize_t readn(int fd, void *ptr, size_t n);
void *signalHandlerThread(void *arg);
int len(ENTRY *lista);

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso\n\t%s m num_threads\n", argv[0]);
        exit(1);
    }
    int numCons = atoi(argv[1]);
    int numProd = atoi(argv[2]);
    if (numProd <= 0)
        termina("numero di thread produttori non valido");
    if (numCons <= 0)
        termina("numero di thread consumatori non valido");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    // Inizializzazione threads
    pthread_t capoLettore;
    pthread_t capoScrittore;
    pthread_t *lettori = (pthread_t *)malloc(numCons * sizeof(pthread_t));
    pthread_t *scrittori = (pthread_t *)malloc(numProd * sizeof(pthread_t));
    // Creazione del thread gestore dei segnali

    char **bufferSC = (char **)malloc(PC_buffer_len * sizeof(char *));
    for (int i = 0; i < PC_buffer_len; i++)
        bufferSC[i] = NULL;

    // Params comunicazione capo-sub lettori
    char **bufferLett = (char **)malloc(PC_buffer_len * sizeof(char *));
    for (int i = 0; i < PC_buffer_len; i++)
        bufferLett[i] = NULL;

    // Params comunicazione lettori-scrittori hash
    // condition variable
    int readers = 0;
    bool writing = false;
    pthread_mutex_t mutexHash = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condHash = PTHREAD_COND_INITIALIZER;

    pthread_t signalThread;
    gestoreDati signDati;
    signDati.capoLettore = &capoLettore;
    signDati.capoScrittore = &capoScrittore;
    signDati.lettori = lettori;
    signDati.bufferLett = bufferLett;
    signDati.bufferSC = bufferSC;
    signDati.scrittori = scrittori;
    signDati.numProd = numProd;
    signDati.numCons = numCons;
    signDati.readers = &readers;
    signDati.writing = &writing;
    signDati.mutexHash = &mutexHash;
    signDati.condHash = &condHash;
    xpthread_create(&signalThread, NULL, signalHandlerThread, &(signDati), __LINE__, __FILE__);
    // Inizializzazioni hash
    int ht = hcreate(Num_elem);
    if (ht == 0)
        termina("Errore creazione HT");

    // Params comunicazione capo-sub scrittori
    int pIndexSC = 0;
    int pIndexLett = 0;
    int cIndexSC = 0;
    int cIndexLett = 0;

    pthread_mutex_t mutexConsSC = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutexConsLett = PTHREAD_MUTEX_INITIALIZER;

    sem_t sem_free_slotsLett;
    sem_t sem_data_itemsLett;
    sem_t sem_free_slotsSC;
    sem_t sem_data_itemsSC;
    xsem_init(&sem_free_slotsLett, 0, PC_buffer_len, __LINE__, __FILE__);
    xsem_init(&sem_data_itemsLett, 0, 0, __LINE__, __FILE__);
    xsem_init(&sem_free_slotsSC, 0, PC_buffer_len, __LINE__, __FILE__);
    xsem_init(&sem_data_itemsSC, 0, 0, __LINE__, __FILE__);

    // params log
    pthread_mutex_t mutexLog = PTHREAD_MUTEX_INITIALIZER;

    // Sovrascrivo file cancellando vecchio contenuto
    FILE *file = fopen("lettori.log", "w");
    if (file != NULL)
    {
        fclose(file);
    }
    else
    {
        termina("Impossibile aprire il file.\n");
    }

    // lancio capo-scrittore
    capoScrittoriDati paramsCapoSC;
    paramsCapoSC.bufferSC = bufferSC;
    paramsCapoSC.pIndexSC = &pIndexSC;
    paramsCapoSC.sem_free_slotsSC = &sem_free_slotsSC;
    paramsCapoSC.sem_data_itemsSC = &sem_data_itemsSC;
    paramsCapoSC.numProd = numProd;
    xpthread_create(&capoScrittore, NULL, capoScrittoreBody, &(paramsCapoSC), __LINE__, __FILE__);

    // Lancio capo-lettori
    capoLettoriDati paramsCapoLett;
    paramsCapoLett.pIndexLett = &pIndexLett;
    paramsCapoLett.bufferLett = bufferLett;
    paramsCapoLett.pIndexLett = &pIndexLett;
    paramsCapoLett.sem_free_slotsLett = &sem_free_slotsLett;
    paramsCapoLett.sem_data_itemsLett = &sem_data_itemsLett;
    paramsCapoLett.numCons = numCons;
    xpthread_create(&capoLettore, NULL, capoLettoreBody, &(paramsCapoLett), __LINE__, __FILE__);

    // lancio scrittori
    scrittoriDati paramsSC[numProd];
    for (int i = 0; i < numProd; i++)
    {
        // Params comunicazione capo-sub
        paramsSC[i].cIndexSC = &cIndexSC;
        paramsSC[i].bufferSC = bufferSC;
        paramsSC[i].sem_free_slotsSC = &sem_free_slotsSC;
        paramsSC[i].sem_data_itemsSC = &sem_data_itemsSC;
        paramsSC[i].mutexConsSC = &mutexConsSC;

        // Params comunicazione scrittori-lettori su hash
        paramsSC[i].readers = &readers;
        paramsSC[i].writing = &writing;
        paramsSC[i].mutexHash = &mutexHash;
        paramsSC[i].condHash = &condHash;
        xpthread_create(&(scrittori[i]), NULL, scrittoriBody, &(paramsSC[i]), __LINE__, __FILE__);
    }

    // Lancio capo-lettori, Params comunicazione capo-sub
    // lancio lettori

    lettoriDati paramsLett[numCons];
    for (int i = 0; i < numCons; i++)
    {
        // Params comunicazione capo-sub
        paramsLett[i].cIndexLett = &cIndexLett;
        paramsLett[i].bufferLett = bufferLett;
        paramsLett[i].mutexConsLett = &mutexConsLett;
        paramsLett[i].sem_free_slotsLett = &sem_free_slotsLett;
        paramsLett[i].sem_data_itemsLett = &sem_data_itemsLett;

        // Params comunicazione scrittori-lettori su hash
        paramsLett[i].readers = &readers;
        paramsLett[i].writing = &writing;
        paramsLett[i].mutexHash = &mutexHash;
        paramsLett[i].condHash = &condHash;
        paramsLett[i].mutexLog = &mutexLog;

        xpthread_create(&(lettori[i]), NULL, lettoriBody, &(paramsLett[i]), __LINE__, __FILE__);
    }

    pthread_join(signalThread, NULL);
    
    /*
    sem_destroy(&sem_free_slotsLett);
    sem_destroy(&sem_data_itemsLett);
    sem_destroy(&sem_free_slotsSC);
    sem_destroy(&sem_data_itemsSC);
    xpthread_mutex_destroy(&mutexConsSC,__LINE__,__FILE__);
    xpthread_mutex_destroy(&mutexConsLett, __LINE__,__FILE__);
    xpthread_mutex_destroy(&mutexHash, __LINE__,__FILE__);
    xpthread_mutex_destroy(&mutexLog, __LINE__,__FILE__);
    xpthread_cond_destroy(&condHash, __LINE__,__FILE__);
*/
     /*
     free(lettori);
     free(scrittori);
     free(bufferLett);
     free(bufferSC);
     */
     //printf("Programma terminato\n");
     
    return 0;
}

void *capoScrittoreBody(void *v)
{
    capoScrittoriDati *paramsCapo = (capoScrittoriDati *)v;
    // apro la pipe caposc in lettura
    int fd = open("/tmp/caposc", O_RDONLY);
    if (fd == -1)
    {
        termina("Errore nell'apertura della FIFO C");
        pthread_exit(NULL);
    }

    while (true)
    {
        unsigned int msg_size;
        // leggo la dimensione del messaggio sulla pipe
        int e = readn(fd, &msg_size, sizeof(unsigned int));
        if (e == 0)
        {
            // Esco dal ciclo quando il server chiude la pipe
            break;
        }
        // leggo il messaggio sulla pipe
        char *msg = (char *)malloc((msg_size + 1) * sizeof(char));
        e = readn(fd, msg, msg_size);
        msg[msg_size] = 0;

        // tokenizzo il messaggio con strtok_r (MT-Safe)
        char *delimiters = strdup(".,:; \n\r\t");
        char *saveptr;
        char *tok = strtok_r(msg, delimiters, &saveptr);

        while (tok != NULL)
        {
            // Scrivo i token nel buffer condiviso con i thread scrittori con la tecnica prodCons
            xsem_wait(paramsCapo->sem_free_slotsSC, __LINE__, __FILE__);

            paramsCapo->bufferSC[*(paramsCapo->pIndexSC) % PC_buffer_len] = strdup(tok);

            *(paramsCapo->pIndexSC) += 1;
            xsem_post(paramsCapo->sem_data_itemsSC, __LINE__, __FILE__);

            tok = strtok_r(NULL, delimiters, &saveptr); // new token
        }
        free(delimiters);
        free(msg);
    }
    xclose(fd, __LINE__, __FILE__);
    // Inserimento valore di terminazione nel buffer prodCons
    for (int i = 0; i < paramsCapo->numProd; i++)
    {
        xsem_wait(paramsCapo->sem_free_slotsSC, __LINE__, __FILE__);

        paramsCapo->bufferSC[*(paramsCapo->pIndexSC) % PC_buffer_len] = strdup("EXIT");

        *(paramsCapo->pIndexSC) += 1;
        xsem_post(paramsCapo->sem_data_itemsSC, __LINE__, __FILE__);
    }
    pthread_exit(NULL);
}

void *capoLettoreBody(void *v)
{
    capoLettoriDati *paramsCapo = (capoLettoriDati *)v;
    // apro la pipe capolet in lettura
    int fd = open("/tmp/capolet", O_RDONLY);
    if (fd == -1)
    {
        termina("Errore nell'apertura della FIFO C");
        pthread_exit(NULL);
    }

    while (true)
    {
        // leggo la dimensione del messaggio sulla pipe
        unsigned int msg_size;
        int e = readn(fd, &msg_size, sizeof(unsigned int));
        if (e == 0)
        {
            // Esco dal ciclo quando il server chiude la pipe
            break;
        }
        // leggo il messaggio sulla pipe
        char *msg = (char *)malloc((msg_size + 1) * sizeof(char));
        e = readn(fd, msg, msg_size);
        msg[msg_size] = 0;

        // tokenizzo il messaggio con strtok_r (MT-Safe)
        char *delimiters = strdup(".,:; \n\r\t");
        char *saveptr;
        char *tok = strtok_r(msg, delimiters, &saveptr);
        while (tok != NULL)
        {
            // Scrivo i token nel buffer condiviso con i thread lettori con la tecnica prodCons
            xsem_wait(paramsCapo->sem_free_slotsLett, __LINE__, __FILE__);

            paramsCapo->bufferLett[*(paramsCapo->pIndexLett) % PC_buffer_len] = strdup(tok);

            *(paramsCapo->pIndexLett) += 1;
            xsem_post(paramsCapo->sem_data_itemsLett, __LINE__, __FILE__);

            tok = strtok_r(NULL, delimiters, &saveptr); // new token
        }
        free(delimiters);
        free(msg);
    }

    xclose(fd, __LINE__, __FILE__);

    // Inserimento valore di terminazione nel buffer prodCons
    for (int i = 0; i < paramsCapo->numCons; i++)
    {
        xsem_wait(paramsCapo->sem_free_slotsLett, __LINE__, __FILE__);

        paramsCapo->bufferLett[*(paramsCapo->pIndexLett) % PC_buffer_len] = strdup("EXIT");

        *(paramsCapo->pIndexLett) += 1;
        xsem_post(paramsCapo->sem_data_itemsLett, __LINE__, __FILE__);
    }
    pthread_exit(NULL);
}

void *scrittoriBody(void *v)
{
    scrittoriDati *paramsProd = (scrittoriDati *)v;
    char *items;
    
    //Ciclo che termina quando gli scrittori leggono dal buffer il carattere di terminazione EXIT
    while (true)
    {
        //Lettura dal buffer condiviso cono capoScrittori tramite tecnica prodCons
        xsem_wait(paramsProd->sem_data_itemsSC, __LINE__, __FILE__);
        xpthread_mutex_lock(paramsProd->mutexConsSC, __LINE__, __FILE__);

        items = paramsProd->bufferSC[*(paramsProd->cIndexSC) % PC_buffer_len];

        *(paramsProd->cIndexSC) += 1;
        xpthread_mutex_unlock(paramsProd->mutexConsSC, __LINE__, __FILE__);
        xsem_post(paramsProd->sem_free_slotsSC, __LINE__, __FILE__);

        if (strcmp(items, "EXIT") == 0)
        {
            // printf("Sono scrittore e Leggo EXIT di terminazione \n");
            free(items);
            break;
        }

        // Dopo aver letto items, usando una tecnica lettori/scrittori (per sincronizzarsi con i lettori) chiama aggiungi(items)

        pthread_mutex_lock(paramsProd->mutexHash); //acquisisce la mutua esclusione sul mutexHash, che funge da blocco per la sezione critica del codice
        while (*(paramsProd->writing) || *(paramsProd->readers) > 0) //controlla se qualcun altro sta scrivendo sulla risorsa condivisa e controlla se ci sono altri thread che stanno leggendo dalla risorsa. Se questa condizione è vera, il thread si mette in attesa
            // attende fine scrittura o lettura
            pthread_cond_wait(paramsProd->condHash, paramsProd->mutexHash);
        //Non ci sono né scrittori né lettori attivi sulla risorsa condivisa, indico che inizio a scrivere e rilascio il mutex
        *(paramsProd->writing) = true;
        pthread_mutex_unlock(paramsProd->mutexHash);

        //eseguendo le operazioni di aggiunta di items con aggiungi()
        aggiungi(items);
        free(items);
        

        assert(*(paramsProd->writing)); 
        pthread_mutex_lock(paramsProd->mutexHash); //acquisisco il lock
        *(paramsProd->writing) = false; // indico che ho terminato di scrivere sulla risorsa
        // segnalare a tutti i thread in attesa su condHash che ho finito di scrivere sulla risorsa condivisa. e rilascio il mutex
        pthread_cond_broadcast(paramsProd->condHash);
        pthread_mutex_unlock(paramsProd->mutexHash);
    }
    // printf("Sono uno scrittore e esco\n");
    pthread_exit(NULL);
}

void *lettoriBody(void *v)
{
    lettoriDati *paramsCons = (lettoriDati *)v;
    char *items;
    //Ciclo che termina quando i lettori leggono dal buffer il carattere di terminazione EXIT
    while (true)
    {
        //Lettura dal buffer condiviso cono capoLettori tramite tecnica prodCons
        xsem_wait(paramsCons->sem_data_itemsLett, __LINE__, __FILE__);
        xpthread_mutex_lock(paramsCons->mutexConsLett, __LINE__, __FILE__);

        items = paramsCons->bufferLett[*(paramsCons->cIndexLett) % PC_buffer_len];
        *(paramsCons->cIndexLett) += 1;

        xpthread_mutex_unlock(paramsCons->mutexConsLett, __LINE__, __FILE__);
        xsem_post(paramsCons->sem_free_slotsLett, __LINE__, __FILE__);

        if (strcmp(items, "EXIT") == 0)
        {
            // printf("Sono lettore e Leggo EXIT di terminazione \n");
            free(items);
            break;
        }

        // Dopo aver letto items, usando una tecnica lettori/scrittori (per sincronizzarsi con gli scrittori) chiama conta(items)

        pthread_mutex_lock(paramsCons->mutexHash);  //Aquisisco il mutexHash, che funge da blocco per la sezione critica del codice
        // controlla se ci sono scrittori attivi sulla risorsa condivisa. Se questa condizione è vera, il thread si mette in attesa sulla condition variable condHash
        while (*(paramsCons->writing) == true)
            pthread_cond_wait(paramsCons->condHash, paramsCons->mutexHash);
        *(paramsCons->readers) += 1;  //Non ci sono più scrittori attivi sulla risorsa. Il thread incrementa di 1 il numero di lettori attivi
        pthread_mutex_unlock(paramsCons->mutexHash);  //Rilascia il mutex

        int value = conta(items);

        assert(*(paramsCons->readers) > 0); // ci deve essere almeno un reader (me stesso)
        assert(!(*(paramsCons->writing)));  // non ci devono essere writer
        pthread_mutex_lock(paramsCons->mutexHash);  //acquisisce nuovamente il mutex
        *(paramsCons->readers) -= 1; //Il numero di lettori attivi viene decrementato
        //verifica se non ci sono più lettori attivi sulla risorsa condivisa. In tal caso, viene inviato un segnale a un solo scrittore
        if (*(paramsCons->readers) == 0)
            pthread_cond_signal(paramsCons->condHash); // da segnalare ad un solo writer
        pthread_mutex_unlock(paramsCons->mutexHash);   //rilascia la risorsa

        //Scrittura di value in lettori.log
        pthread_mutex_lock(paramsCons->mutexLog);
        FILE *logfile = fopen("lettori.log", "a");
        if (logfile == NULL)
        {
            termina("Errore nell'apertura del file lettori.log");
            pthread_exit(NULL);
        }
        fprintf(logfile, "%s %d\n", items, value);
        fclose(logfile);
        free(items);
        pthread_mutex_unlock(paramsCons->mutexLog);
    }
    pthread_exit(NULL);
}

// crea un oggetto di tipo entry
// con chiave s e valore n
/*
ENTRY *crea_entry(char *s, int n)
{
    ENTRY *e = malloc(sizeof(ENTRY));
    if (e == NULL)
        termina("errore malloc entry 1");
    e->key = strdup(s); // salva copia di s
    e->data = (int *)malloc(sizeof(int));
    if (e->key == NULL || e->data == NULL)
        termina("errore malloc entry 2");
    *((int *)e->data) = n;
    return e;
}
*/

ENTRY *crea_entry(char *s, int n)
{
    ENTRY *e = malloc(sizeof(ENTRY)); //alloco un elemento Entry* e
    if (e == NULL)
        termina("errore malloc entry 1");
    e->key = strdup(s); // salva copia di s in key
    e->data = malloc(sizeof(coppia));  //Alloca e->data
    if (e->key == NULL || e->data == NULL)
        termina("errore malloc entry 2");
    // inizializza la coppia assegnando come campo valore e and come campo next NULL
    coppia *c = (coppia *)e->data; // cast obbligatorio
    c->valore = n;
    c->next = NULL;
    return e;
}

void distruggi_entry(ENTRY *e)
{
    free(e->key);
    free(e->data);
    free(e);
}

void aggiungi(char *str)
{
    ENTRY *e = crea_entry(str, 1); //crea la entry
    ENTRY *r = hsearch(*e, FIND);  //cerca se gono già presenti occorrenze
    if (r == NULL)
    { 
        r = hsearch(*e, ENTER); //Se non sono presenti, inserisce nella tabella
        if (r == NULL)
        {
            termina("errore o tabella piena");
            distruggi_entry(e);
        }
        // Aggiunge in testa alla lista
        
        coppia *c = (coppia *)e->data;
        c->next = testa_lista_entry;
        testa_lista_entry = e;

    }
    else
    {
        // Se la stringa è gia' presente incremento il valore
        assert(strcmp(e->key, r->key) == 0);
        coppia *c = (coppia *)r->data;
        c->valore += 1;
        distruggi_entry(e); // questa non la devo memorizzare
    }
}

/*
void aggiungi(char *str)
{
    ENTRY *e = crea_entry(str, 1);
    ENTRY *r = hsearch(*e, FIND);
    if (r == NULL)
    {                           // la entry è nuova
        r = hsearch(*e, ENTER); // inserisco
        if (r == NULL){
            distruggi_entry(e);
            termina("errore o tabella piena");
    }
    }
    else
    {
        // la stringa è gia' presente incremento il valore
        assert(strcmp(e->key, r->key) == 0);
        int *d = (int *)r->data;
        *d += 1;
        distruggi_entry(e); // questa non la devo memorizzare
    }
}
*/
int conta(char *str)
{
    //Cerca la stringa nella tabella. Se occorre, ritorna il suo valore
    ENTRY *e = crea_entry(str, 1);
    ENTRY *r = hsearch(*e, FIND);
    if (r == NULL)
    {
        distruggi_entry(e);
        return 0;
    }
    else
    {
        distruggi_entry(e);
        coppia *c = (coppia *)r->data;
        return c->valore;
    }
}

/*
int conta(char *str)
{
    ENTRY *e = crea_entry(str, 1);
    ENTRY *r = hsearch(*e, FIND);
    if (r == NULL)
    {
        distruggi_entry(e);
        return 0;
    }
    else
    {
        distruggi_entry(e);
        return (*((int *)r->data));
    }
}
*/

//Conta quante stringhe distinte sono presenti nella linked list
int len(ENTRY *lista)
{
    if (lista == NULL)
    {
        return 0;
    }
    else
    {
        coppia *c = (coppia *)lista->data;
        ENTRY *tmp = c->next; // necessario
        return 1 + len(tmp);
    }
}

//Scorre tutta la lista e dealloca ogni elemento presente
void distruggi_lista(ENTRY *lis)
{
    while (lis != NULL)
    {
        coppia *c = (coppia *)lis->data;
        ENTRY *tmp = c->next; // necessario
        distruggi_entry(lis);
        lis = tmp;
    }
}

/* Read "n" bytes from a descriptor
   analoga alla funzione python recv_all() */
ssize_t readn(int fd, void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nread;

    nleft = n;
    while (nleft > 0)
    {
        if ((nread = read(fd, ptr, nleft)) < 0)
        {
            if (nleft == n)
                return -1; /* error, return -1 */
            else
                break; /* error, return amount read so far */
        }
        else if (nread == 0)
            break; /* EOF */
        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft); /* return >= 0 */
}


void *signalHandlerThread(void *arg)
{
    //inizializza l'insieme di segnali mask vuoto e ci includo SIGINT, SIGTERM E SIGUSR1
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    gestoreDati *data = (gestoreDati *)arg;

    int sig;
    while (1)
    {
        sigwait(&mask, &sig);   //mette in attesa il thread corrente finché uno dei segnali viene ricevuto.
        if (sig == SIGINT)
        {
            pthread_mutex_lock(data->mutexHash);
            while (*(data->writing) == true)
                pthread_cond_wait(data->condHash, data->mutexHash); // attende fine scrittura
            *(data->readers) += 1;
            pthread_mutex_unlock(data->mutexHash);

            // fprintf(stderr, "SIGINT, Numero totale di stringhe distinte: %d\n", len(testa_lista_entry));
            fprintf(stderr, "%d\n", len(testa_lista_entry));
            assert(*(data->readers) > 0); // ci deve essere almeno un reader (me stesso)
            assert(!(*(data->writing)));  // non ci devono essere writer
            pthread_mutex_lock(data->mutexHash);
            *(data->readers) -= 1; // cambio di stato
            if (*(data->readers) == 0)
                pthread_cond_signal(data->condHash); // da segnalare ad un solo writer
            pthread_mutex_unlock(data->mutexHash);
        }
        else if (sig == SIGUSR1)
        {
            pthread_mutex_lock(data->mutexHash);
            while (*(data->writing) || *(data->readers) > 0)
                // attende fine scrittura o lettura
                pthread_cond_wait(data->condHash, data->mutexHash);
            *(data->writing) = true;
            pthread_mutex_unlock(data->mutexHash);

            hdestroy();
            distruggi_lista(testa_lista_entry);
            testa_lista_entry= NULL;
            int ht = hcreate(Num_elem);
            if (ht == 0)
                termina("Errore creazione HT");
            // printf("Ho resettato la tabella\n");

            assert(*(data->writing));
            pthread_mutex_lock(data->mutexHash);
            *(data->writing) = false; // cambio stato
            // segnala a tutti quelli in attesa
            pthread_cond_broadcast(data->condHash);
            pthread_mutex_unlock(data->mutexHash);
        }
        else if (sig == SIGTERM)
        {
            xpthread_join(*(data->capoLettore), NULL, __LINE__, __FILE__);   // Attendi la terminazione del thread "capo lettore"
            xpthread_join(*(data->capoScrittore), NULL, __LINE__, __FILE__); // Attendi la terminazione del thread "capo scrittore"

            for (int i = 0; i < data->numProd; i++)
            {
                xpthread_join(data->scrittori[i], NULL, __LINE__, __FILE__);
            }
            // printf("Tutti gli scrittori hanno finito \n");

            for (int i = 0; i < data->numCons; i++)
            {
                xpthread_join(data->lettori[i], NULL, __LINE__, __FILE__);
            }

            // printf("Tutti i lettori hanno finito \n");
            // printf("Distruggo lista \n");
            // fprintf(stdout, "SIGTERM: Numero totale di stringhe distinte: %d \n", len(testa_lista_entry));
            fprintf(stdout, "%d \n", len(testa_lista_entry));
    
            hdestroy();
            distruggi_lista(testa_lista_entry);
            testa_lista_entry = NULL;
            
            free(data->lettori);
            free(data->scrittori);
            free(data->bufferLett);
            free(data->bufferSC);
            
            // fprintf(stdout, "Termina il programma\n");
            pthread_exit(NULL);
        }
    }
}