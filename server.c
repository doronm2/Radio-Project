/* TCP_receiver
Author1: Victor Martinov,
ID: 307835249
Author2: Doron Maman,
ID: 302745146
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#define Buffer_size 	1024
/***************************************Structs*****************/

typedef struct {
    int num_of_station;
    char *song_name;
    char* ip_address;
    int port;
    pthread_t station_thread;
    UDP_DATA next;
}UDP_DATA;

typedef struct {
    int socket;
    pthread_t client_thread;
}TCP_DATA;

/***************************************Functions*****************/
int close_connections(int client_Sock[100],int client_pid[100],int num_of_clients); //TODO - close all tcp and udp connections, return 0 if all is closed and -1 if failed
void handle_client(void* info); //TODO - client thread for each connection
void handle_station(void* my_details); //TODO - udp thread for each of the stations
bool check_new_song(UDP_DATA to_check); //TODO - check if there is a new station in the linked list, return true if there is


/****************** Server  ****************/

int main(int argc, char* argv[])
{
	int client_Sock[100]={0};
    pthread_t client_pid[100]={0};
    int welcomeSocket, parts, tcp_port,udp_port, read_err=0,i=0;
	struct sockaddr_in serverAddr;
	char buffer[Buffer_size+1];
    char* Multi_ip;
    UDP_DATA* stations;
    TCP_DATA* client;

    client=calloc(1, sizeof(TCP_DATA));

    stations=calloc(1, sizeof(UDP_DATA));

    tcp_port = atoi(argv[1]); //for the tcp port
    Multi_ip = argv[2]; ///the initiail multicast address
    udp_port = atoi(argv[3]);
    //TODO - add for() to read all args and open threads for each station

    // Create the socket. The three arguments are:
    /* 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case) */
    welcomeSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (welcomeSocket == -1) { perror("Can't create welcome socket"); close(welcomeSocket); exit(1); }

    /*---- Configure settings of the server address struct ----*/
    serverAddr.sin_family = AF_INET; /* Address family = Internet */
    serverAddr.sin_port = htons(port); 	/* Set port number, using htons function to use proper byte order */
    serverAddr.sin_addr.s_addr = inet_addr(IP); /* Set IP address to localhost */
    memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); 	/* Set all bits of the padding field to 0 */

    /*---- Bind the address struct to the socket ----*/
    if (bind(welcomeSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1)
    { perror("Can't bind welcome socket, try again later"); close(welcomeSocket); exit(1); }

    /*---- Listen on the socket, with 1 max connection requests queued ----*/
    //Wait for a connection:
    if(listen(welcomeSocket,1)==0)
        printf("\nListening to new client requests..\n");
    else
    { perror("Error in listen"); close(welcomeSocket); exit(1); }

    /*---- Accept call creates a new socket for the incoming connection ----*/
    addr_size = sizeof(serverStorage);
    while(client_Sock[i] = accept(welcomeSocket, (struct sockaddr *)&serverStorage, &addr_size) && i<100) {

        if (client_Sock[i] == -1) {
            perror("Error in accepting new client");
            close(welcomeSocket);
            close_connections(client_Sock,client_pid,i);
            exit(1);
        }
        else { printf("\naccepted new client. \n\n"); }

        /*---- Accept call creates a new socket for the incoming connection ----*/
        client->socket=client_Sock[1];
        if( pthread_create( &client->client_thread , NULL ,  handle_client , (void*)client) < 0)
        {
            perror("could not create thread");
            close(client->socket);
        }
        else {
            printf("new thread created for client %d", i);
            client_pid[i]=client->client_thread;
        }
        i++;
    }
	// Wait for socket to have data, Read socket data into buffer if there is Any data

	return EXIT_SUCCESS;
}
