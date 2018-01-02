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
#include <time.h>
#include <math.h>
#include <pthread.h>

#define Buffer_size 	1024
#define C2S_Buffer
/***************************************Structs*****************/

typedef struct {
    int num_of_station;
    char *song_name;
    char* ip_address;
    int port;
    int shutdown;
    pthread_t station_thread;
    struct UDP_DATA *next;
}UDP_DATA;

typedef struct {
    int socket;
    pthread_t client_thread;
}TCP_DATA;

/***************************************Functions*****************/
int close_connections(int client_Sock[100],pthread_t client_pid[100],int num_of_clients,UDP_DATA *root); //TODO - close all tcp and udp connections, return 0 if all is closed and -1 if failed
void* handle_client(void* info); //TODO - client thread for each connection
void* handle_station(void* my_details); //TODO - udp thread for each of the stations
int check_new_song(UDP_DATA to_check); //TODO - check if there is a new station in the linked list, return 0 if there is


/****************** Server  ****************/

int main(int argc, char* argv[])
{
	int client_Sock[100]={0};
    pthread_t client_pid[100]={0};
    int welcomeSocket,tcp_port,udp_port,i=0,songs=0;
	struct sockaddr_in serverAddr;
    struct sockaddr_storage serverStorage;
    char *root_Multi_ip, *last_Multi_ip;
    socklen_t addr_size;
    UDP_DATA* stations,*root;
    TCP_DATA* client;

    client=calloc(1, sizeof(TCP_DATA)); //allocate the client tcp to send to the threads

    stations=calloc(1, sizeof(UDP_DATA)); //allocate the stations udp for each udp multicast
    root=stations;
    //memcpy(root,stations, sizeof(UDP_DATA)); //save the header of the stations

    tcp_port = atoi(argv[1]); //for the tcp port

    udp_port = atoi(argv[3]); //for the udp port

    root_Multi_ip = argv[2]; ///the initial multicast address
    last_Multi_ip=calloc(1, sizeof(root_Multi_ip)+1);
    memcpy(last_Multi_ip,root_Multi_ip,strlen(root_Multi_ip)+1);

    //***********Get and open a thread for each station*********//
    for(songs=0;argv[songs]!=NULL;songs++){
        if(songs!=0) {
            stations->next = calloc(1, sizeof(UDP_DATA)); //allocate a new station
            stations=stations->next;
        }
        stations->ip_address=last_Multi_ip;
        last_Multi_ip+=0x1;
        stations->num_of_station=songs;
        stations->shutdown=0; // flag to inform a the thread to close
        stations->port=udp_port;
        stations->song_name=argv[songs];
        if( pthread_create( &stations->station_thread , NULL ,  &handle_station , (void*)stations) < 0)
            perror("could not create thread");
    }


    // Create the socket. The three arguments are:
    /* 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case) */
    welcomeSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (welcomeSocket == -1) { perror("Can't create welcome socket"); close(welcomeSocket); exit(1); }

    /*---- Configure settings of the server address struct ----*/
    serverAddr.sin_family = AF_INET; /* Address family = Internet */
    serverAddr.sin_port = htons(tcp_port); 	/* Set port number, using htons function to use proper byte order */
    serverAddr.sin_addr.s_addr = inet_addr(INADDR_ANY); /* Set IP address to localhost */
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
            break;
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
    if(close_connections(client_Sock,client_pid,i,root)==0)
        printf("successfully closed TCP and UDP Sockets and Threads");
	// Wait for socket to have data, Read socket data into buffer if there is Any data

	return EXIT_SUCCESS;
}


void* handle_station(void* data){
    UDP_DATA *my_data=(UDP_DATA*)data;
    FILE * song_ptr, *reset_ptr;
    int sock,i=0,bytes_sent=0,curr_sent=0, sent_err=0,j;
    char message[Buffer_size] = {0};
    u_char TTL =10;
    struct sockaddr_in serverAddr;

    //try to open the socket to send the data on
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) { perror("Can't create socket"); close(sock); exit(1); }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(my_data->ip_address);
    serverAddr.sin_port = htons(my_data->port);

    //try to bind this UDP socket
    if(bind(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))==-1)
    { perror("Can't bind UDP socket"); close(sock); exit(1); }

    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &TTL, sizeof(TTL)); //set TTL field to 10

    //try to open the text file to send
    song_ptr = fopen (my_data->song_name , "r");
    memcpy(reset_ptr,song_ptr, sizeof(FILE));
    if(!song_ptr) {
        printf("couldn't open text file to send");
        close(sock);
        exit(1);
    }

    /*---- Send message to the socket of the incoming connection ----*/
    //Read file into buffer & Send buffer to client
    //While (not all data sent || any connection error)
    while (1) // if we got here, no errors occurred.
    {
        if((fscanf(song_ptr, "%1024c", message))==EOF){
            song_ptr=reset_ptr;
        }
        curr_sent = sendto(sock, message, sizeof(message), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
        bytes_sent += curr_sent;
        if(curr_sent == -1)
        {
            sent_err++;
            perror("");
            printf("\n");
            printf("Error in sending file part number: %d\n",i+1);
        }
        for(j=0;j<Buffer_size;j++)
            message[j] = '\0';
        i++;
        if(my_data->shutdown==1)
            break;

    }
    //free all resources;
    fclose(song_ptr);
    shutdown(sock, SHUT_RDWR);
    close(sock);

    if(sent_err==0)
        printf("\nsuccessful transmission. Bye bye!\n");
}


int close_connection(int client_Sock[100],pthread_t client_pid[100],int num_of_clients,UDP_DATA *root){
    UDP_DATA *temp;
    int i,Check;
    /********Close all tcp sockets and threads*********/
    for(i=0;i<=num_of_clients;i++) {
        Check = pthread_join(client_pid[i], NULL);        //wait for the threads to end
        if (Check != 0) {
            perror("failed to close thread");
            exit(1);
        }
        Check = close(client_Sock[i]);
        if (Check != 0) {
            perror("failed to close Socket");
            exit(1);
        }
    }
    while(root->next!=NULL){
        Check = pthread_join(root->station_thread, NULL);        //wait for the threads to end
        if (Check != 0) {
            perror("failed to close thread");
            exit(1);
        }
        temp=root->next;
        free(root);
        root=temp;
    }
}

void* handle_client(void* data){
    TCP_DATA *my_data=(TCP_DATA*)data;
    int i,
    char buffer[Buffer_size+1];
    uint8_t replyType = 0;
}