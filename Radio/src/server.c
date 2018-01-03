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
#include <pthread.h>

#define Buffer_size 	1024
#define C2S_Buffer
/***************************************Structs*****************/

typedef struct {
    int num_of_station;
    char *song_name;
    int ip_address[4];
    int port;
    int shutdown;
    pthread_t station_thread;
    struct UDP_DATA *next;
}UDP_DATA;

typedef struct {
    int socket;
    pthread_t client_thread;
    struct TCP_DATA *next;
    struct TCP_DATA *prev;
}TCP_DATA;

typedef enum {
    WELCOME,
    ANNOUNCE,
    PERMIT,
    INVALID,
    NEW
}ANSWER;

/***************************************Functions*****************/

int close_connections(TCP_DATA *root_client,UDP_DATA *root_station); //TODO CHECK - close all tcp and udp connections, return 0 if all is closed and -1 if failed
void* handle_client(void* data); //TODO - client thread for each connection
void* handle_station(void* data); //TODO CHECK- udp thread for each of the stations
int check_new_song(UDP_DATA *to_check); //TODO - check if there is a new station in the linked list, return 0 if there is
int send_message(int socket,enum answer); //TODO -
int remove_client(TCP_DATA *client); //TODO - close and delete the client, and reconnect the other linked list
int open_new_station(UDP_DATA *last_station,UDP_DATA *new_station);//TODO - open a new UDP multicast with the data given.

/****************** Server  ****************/

int main(int argc, char* argv[])
{
    int welcomeSocket,tcp_port,udp_port,i=0;
    int temp,songs=0,j=0;
	struct sockaddr_in serverAddr;
    struct sockaddr_storage serverStorage;
    char *root_Multi_ip;
    socklen_t addr_size;
    UDP_DATA* stations,*root_station, *temp_station;
    TCP_DATA* client,*temp_client, *root_client;

    if(argc<2) {
        perror("not enough parameters to run the server\n\nSERVER EXIT");
        return EXIT_FAILURE;
    }
    else
    	printf("input received: \nexe File:%s, TCP port:%s,\nMulticast IP:%s"
    			", UDP port:%s, song no.1:%s.\n(total of %d args)\n"
    			, argv[0], argv[1], argv[2], argv[3] ,argv[4], argc);
    client=(TCP_DATA*)calloc(1, sizeof(TCP_DATA)); //allocate the client tcp to send to the threads
    client->prev=NULL;
    root_client=client;

    stations=(UDP_DATA*)calloc(1, sizeof(UDP_DATA)); //allocate the stations udp for each udp multicast
    root_station=stations;

    //memcpy(root,stations, sizeof(UDP_DATA)); //save the header of the stations

    tcp_port = atoi(argv[1]); //for the tcp port

    udp_port = atoi(argv[3]); //for the udp port

    root_Multi_ip = argv[2]; ///the initial multicast address

    //***********Get and open a thread for each station*********//
    for(songs=0;songs<(argc-4);songs++){
        if(songs!=0) {
        	temp_station=(UDP_DATA*)calloc(1, sizeof(UDP_DATA)); //allocate a new station
        	stations->next=temp_station;
        	stations=stations->next;
        }
    	for(i=0, j=0 ; i<4 ; i++){//copy the ip to int array
    		stations->ip_address[i] = 0;
    		for(; j<(argv[3]-argv[2]-1) && argv[2][j]!='.' ; j++){
    			temp=(int)(argv[2][j]-48);
    			stations->ip_address[i] = stations->ip_address[i]*10 +temp;
    		}
    		j++;
    	}
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
    while(client->socket = accept(welcomeSocket, (struct sockaddr *)&serverStorage, &addr_size) && i<100) {

        if (client->socket == -1) {
            perror("Error in accepting new client");
            close(client->socket);
            break;
        }
        else { printf("\naccepted new client. \n\n"); }

        temp_client=calloc(1, sizeof(TCP_DATA));
        client->next=temp_client;
        /*---- Accept call creates a new socket for the incoming connection ----*/
        if( pthread_create( &client->client_thread , NULL ,  handle_client , (void*)client) < 0)
        {
            perror("could not create thread");
            close(client->socket);
        }
        else {
            printf("new thread created for client %d", i);
        }
        i++;
        temp_client->prev=client;
        client=temp_client;
    }
    if(close_connections(root_client,root_station)==0)
        printf("successfully closed TCP and UDP Sockets and Threads");
	// Wait for socket to have data, Read socket data into buffer if there is Any data
    free(root_Multi_ip);
	return EXIT_SUCCESS;
}


void* handle_station(void* data){
    UDP_DATA *my_data=(UDP_DATA*)data;
    FILE * song_ptr;
    int sock,i=0,bytes_sent=0,curr_sent=0, sent_err=0,j;
    char* Multicast_IP;
    char message[Buffer_size] = {0};
    u_char TTL =10;
    struct sockaddr_in serverAddr;

    //try to open the socket to send the data on
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) { perror("Can't create socket"); close(sock); exit(1); }
    Multicast_IP=(char*)calloc(1,sizeof(char));
    sprintf(Multicast_IP,"%hhu.%hhu.%hhu.%hhu",my_data->ip_address[0]
			,my_data->ip_address[1],my_data->ip_address[2],(my_data->ip_address[3]+my_data->num_of_station));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(my_data->ip_address);
    serverAddr.sin_port = htons(my_data->port);

    //try to bind this UDP socket
    if(bind(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))==-1)
    { perror("Can't bind UDP socket"); close(sock); exit(1); }

    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &TTL, sizeof(TTL)); //set TTL field to 10

    //try to open the text file to send
    song_ptr = fopen (my_data->song_name , "r");
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
        usleep(62500);//sleep for 62500 useconds
    	if((fscanf(song_ptr, "%1024c", message))==EOF){
            rewind(song_ptr);
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
    pthread_exit(my_data->station_thread);
    return 0;
}


int close_connections(TCP_DATA *root_client,UDP_DATA *root_station){
    UDP_DATA *temp;
    int Check;
    /********Close all tcp sockets and threads*********/
    while(root_client->next!=NULL){
        Check=pthread_join(root_client->client_thread, NULL);        //wait for the threads to end
        if (Check != 0) {
            perror("failed to close thread");
            exit(1);
        }
        temp=root_client->next;
        free(root_client);
        root_client=temp;

        Check = close(root_client->socket);
        if (Check != 0) {
            perror("failed to close Socket");
            exit(1);
        }
    }
    free(temp);
    while(root_station->next!=NULL){
        Check = pthread_join(root_station->station_thread, NULL);        //wait for the threads to end
        if (Check != 0) {
            perror("failed to close thread");
            exit(1);
        }
        temp=root_station->next;
        free(root_station);
        root_station=temp;
    }
    free(temp);
    return 1;
}

void* handle_client(void* data){
    TCP_DATA *my_data=(TCP_DATA*)data;
    int i;
    char buffer[Buffer_size+1];
    uint8_t replyType = 0;
    ANSWER type;
    while(1){

    }
}
