#include "xerrori.h"
#include <arpa/inet.h>
#include <sys/socket.h>

// host e port a cui connettersi
#define HOST "127.0.0.1"
#define PORT 54861

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

/* Write "n" bytes to a descriptor
   analoga alla funzione python sendall() */
ssize_t writen(int fd, void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nwritten;

    nleft = n;
    while (nleft > 0)
    {
        if ((nwritten = write(fd, ptr, nleft)) < 0)
        {
            if (nleft == n)
                return -1; /* error, return -1 */
            else
                break; /* error, return amount written so far */
        }
        else if (nwritten == 0)
            break;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n - nleft); /* return >= 0 */
}

int main(int argc, char const *argv[])
{
    if (argc != 2)
    {
        printf("Uso\n\t%s inizio fine\n", argv[0]);
        exit(1);
    }
    // legge estremi intervallo
    const char *nomeFile = argv[1];
    int fd_skt = 0; // file descriptor associato al socket
    struct sockaddr_in serv_addr;
    size_t e;
    int tmp;
    char *tipo = strdup("A");

    // apre connessione per ogni riga del file
    char *line = NULL;
    size_t len = 0;
    ssize_t lenLetti;
    FILE *f = fopen(nomeFile, "r");
    if (f == NULL)
    {
        termina("Impossibile aprire il file.\n");
    }

    // Leggi le righe del file
    while ((lenLetti = getline(&line, &len, f)) != -1)
    {
        if (line[0] != '\n' || !isspace(line[0]))
        {
            // Rimuovi il carattere di nuova riga (\n) dalla riga letta
            if (line[lenLetti - 1] == '\n')
                line[lenLetti - 1] = '\0';
            // Effettua la connessione al socket per la riga letta
            // crea socket
            if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                termina("Errore creazione socket");
            // assegna indirizzo
            serv_addr.sin_family = AF_INET;
            // il numero della porta deve essere convertito
            // in network order
            serv_addr.sin_port = htons(PORT);
            serv_addr.sin_addr.s_addr = inet_addr(HOST);
            // Mi connetto
            if (connect(fd_skt, &serv_addr, sizeof(serv_addr)) < 0)
                termina("Errore apertura connessione");
            // Ricezione bye nullo
            char received_byte;
            ssize_t received = recv(fd_skt, &received_byte, sizeof(received_byte), 0);
            if (received == -1)
            {
                termina("Errore nella ricezione del byte inutile");
            }
            // Invio dati
            //puts("Invio il tipo di connessione A");
            e = writen(fd_skt, tipo, strlen(tipo));
            if (e != strlen(tipo))
                termina("Errore write");
            //puts("Invio Lunghezza sequenza");
            tmp = htonl(strlen(line));
            e = writen(fd_skt, &tmp, sizeof(tmp));
            if (e != sizeof(tmp))
                termina("Errore write");
            //puts("Invio la riga");
            assert(strlen(line)<= 2048);
            e = writen(fd_skt, line, strlen(line));
            if (e != strlen(line))
                termina("Errore write");
            if (close(fd_skt) < 0)
                perror("Errore chiusura socket");
            //puts("Connessione terminata");
        }
    }
    free(line);
    line = NULL;
    free(tipo);
    return 0;
}
