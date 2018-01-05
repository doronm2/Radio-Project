/* TCP_receiver
Author1: Victor Martinov,
ID: 307835249
Author2: Doron Maman,
ID: 302745146
 */
#define SERVER
#ifdef SERVER
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

struct UDP_DATA{
    int num_of_station;
    char *song_name;
    int ip_address[4];
    int port;
    int shutdown;
    pthread_t station_thread;
    struct UDP_DATA *next;
};

struct TCP_DATA{
    int socket;
    int client_index;
    pthread_t client_thread;
};

//state defenitions
#define 	HELLO 			0
#define 	ASKSONG			1
#define 	UPSONG  		2
#define     INVALID         10

typedef enum {
    WELCOME,
    ANNOUNCE,
    PERMIT,
    NEW_STATION,
    INVALID_MSG,
}SERVER_RESPONSE;

static int END_SERVER=0;
static int Client_List[100][4];  //the list is {socket,thread,station name,active(1 for active, 0 for inactive)}
static int NUM_OF_STATIONS=0;
static int UPLOAD_SONG=0; //0- no song is uploading, 1- a song is uploading, 2 - a song has uploaded
static int UPD_PORT=0;

static uint32_t MIN_SONG_SIZE=2000;
static uint32_t MAX_SONG_SIZE=10000000;

/***************************************Functions*****************/

int close_connections(struct UDP_DATA *root_station);//TODO CHECK - close all tcp and udp connections, return 0 if all is closed and -1 if failed
void* handle_client(void* data); //TODO - client thread for each connection
void* handle_station(void* data); //TODO CHECK- udp thread for each of the stations
int check_new_song(struct UDP_DATA *to_check); //TODO - check if there is a new station in the linked list, return 0 if there is
int remove_client(struct TCP_DATA *client); //TODO - close and delete the client, and reconnect the other linked list
int open_new_station(struct UDP_DATA *last_station,struct UDP_DATA *new_station);//TODO - open a new UDP multicast with the data given.
int upload_song(char* song_name,int socket);//TODO - build a function to receive the song from the client
int send_message(int socket,SERVER_RESPONSE ans,int str_len,char* reply_str);


/****************** Server  ****************/
//TODO - add select() to chose between IO and WelocomeSocket data
//TODO - build a manageble array for each client - and what station he is on
//TODO - pulling on UPLOAD_SONG
int main(int argc, char* argv[])
{
    int welcomeSocket,tcp_port,udp_port,i=0,client_ind=0;
    int temp,songs=0,j=0;
	struct sockaddr_in serverAddr;
    struct sockaddr_storage serverStorage;
    char *root_Multi_ip;
    socklen_t addr_size;
    struct UDP_DATA *stations,*root_station, *temp_station;
    struct TCP_DATA *client,*temp_client, *root_client;

    if(argc<2) {
        perror("not enough parameters to run the server\n\nSERVER EXIT");
        return EXIT_FAILURE;
    }
    else
    	printf("input received: \nexe File:%s, TCP port:%s,\nMulticast IP:%s"
    			", UDP port:%s, song no.1:%s.\n(total of %d args)\n"
    			, argv[0], argv[1], argv[2], argv[3] ,argv[4], argc);
    client=(struct TCP_DATA*)calloc(1, sizeof(struct TCP_DATA)); //allocate the client tcp to send to the threads

    for(client_ind=0;client_ind<100;client_ind++)
        for(i=0;i<4;i++)
            Client_List[client_ind][i]=0; //init the client list
    client_ind=0;

    root_client=client;

    stations=(struct UDP_DATA*)calloc(1, sizeof(struct UDP_DATA)); //allocate the stations udp for each udp multicast
    root_station=stations;

    //memcpy(root,stations, sizeof(UDP_DATA)); //save the header of the stations

    tcp_port = atoi(argv[1]); //for the tcp port

    udp_port = atoi(argv[3]); //for the udp port
    UPD_PORT=udp_port;

    root_Multi_ip = argv[2]; ///the initial multicast address

    //***********Get and open a thread for each station*********//
    for(NUM_OF_STATIONS=0;NUM_OF_STATIONS<(argc-4);NUM_OF_STATIONS++){
        if(NUM_OF_STATIONS!=0) {
        	temp_station=(struct UDP_DATA*)calloc(1, sizeof(struct UDP_DATA)); //allocate a new station
        	stations->next= temp_station;
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
        stations->num_of_station=NUM_OF_STATIONS;
        stations->shutdown=0; // flag to inform a the thread to close
        stations->port=udp_port;
        stations->song_name=argv[4+NUM_OF_STATIONS];
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
    while((client->socket = accept(welcomeSocket, (struct sockaddr *)&serverStorage, &addr_size)) && i<100) {
        for(client_ind=0;client_ind<100;client_ind++)
            if(Client_List[client_ind][3]==0)
                break;
        if(client_ind==100) {
            perror("Server unable to connect new clients");
            close(client->socket);
        }
        if (client->socket == -1) {
            perror("Error in accepting new client");
            close(client->socket);
            break;
        }
        else { printf("\naccepted new client. \n\n"); }

        client->client_index=client_ind;
        /*---- Accept call creates a new socket for the incoming connection ----*/
        if( pthread_create( &client->client_thread , NULL ,  handle_client , (void*)client) < 0)
        {
            perror("could not create thread");
            close(client->socket);
        }
        else {
            printf("new thread created for client %d", client_ind);
        }
        i++;
        //fill the client list for management;
        Client_List[client_ind][0]=client->socket;
        Client_List[client_ind][1]= (int) client->client_thread;
        Client_List[client_ind][3]=1;

    }
    if(close_connections(root_station)==0)
        printf("successfully closed TCP and UDP Sockets and Threads");
	// Wait for socket to have data, Read socket data into buffer if there is Any data
    free(root_Multi_ip);
	return EXIT_SUCCESS;
}


void* handle_station(void* data){
    struct UDP_DATA *my_data=(struct UDP_DATA*)data;
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
    serverAddr.sin_addr.s_addr = inet_addr(Multicast_IP);
    serverAddr.sin_port = htons((uint16_t) my_data->port);

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
    while (END_SERVER==0) // if we got here, no errors occurred.
    {
        usleep(62500);//sleep for 62500 useconds
    	if((fscanf(song_ptr, "%1024c", message))==EOF){
            rewind(song_ptr);
        }
        curr_sent = (int) sendto(sock, message, sizeof(message), 0, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
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
    pthread_exit((void *) my_data->station_thread);
}


int close_connections(struct UDP_DATA *root_station){
    struct UDP_DATA *temp;
    int Check,index=0;
    /********Close all tcp sockets and threads*********/
    for(index=0;index<100;index++){
        if(Client_List[index][3]==1) {
            Check = pthread_join((pthread_t) Client_List[index][1], NULL);
            //wait for the threads to end
            if (Check != 0) {
                perror("failed to close thread");
                exit(1);
            }
            Check = close(Client_List[index][0]);
            if (Check != 0) {
                perror("failed to close Socket");
                exit(1);
            }
        }
    }
    while(root_station->next!=NULL){
        Check = pthread_join(root_station->station_thread, NULL);        //wait for the threads to end
        if (Check != 0) {
            perror("failed to close thread");
            exit(1);
        }
        temp= (struct UDP_DATA *) root_station->next;
        free(root_station);
        root_station=temp;
    }
    free(temp);
    return 1;
}

void* handle_client(void* data){
    struct TCP_DATA *my_data=(struct TCP_DATA*)data;
    int i,msg_len,msg_err=0, Hello_flag=0;
    char buffer[Buffer_size+1],*song_name;
    uint8_t commandType = 0,song_name_len;
    uint16_t client_data;
    uint32_t song_size;

    while(END_SERVER==0 && msg_err==0){
        msg_len= (int) recv(my_data->socket, buffer, Buffer_size , 0);
        if(msg_len>0){
            commandType=(uint8_t )buffer[0];
            switch (commandType) {
                case HELLO:
                    client_data=(uint16_t )((buffer[1]<<8)+buffer[2]);
                    //check that the messsage is correct and that there is only 1 Hello message received
                    if( Hello_flag==0){
                        if (client_data==0) {
                            send_message(my_data->socket, WELCOME,0,NULL); //send WELCOME_MESSAGE to the client
                            Hello_flag=1;
                        }
                        else {
                            send_message(my_data->socket, INVALID_MSG,22,"wrong reserved Value\0"); //send an invalid command to the client
                            msg_err = 1;//there is a error with the message - close connection
                        }
                    }
                    else {
                        send_message(my_data->socket, INVALID_MSG,33,"Multiple Hello Message received\0"); //send an invalid command to the client
                        msg_err = 1;//there is a error with the message - close connection
                    }
                     break;
                case ASKSONG:
                    client_data=(uint16_t )((buffer[1]<<8)+buffer[2]);
                        //check that an Hello message was received
                    if(Hello_flag==1){
                        //check that the client asked for a station that exist
                        if((client_data>=0 && client_data<NUM_OF_STATIONS))
                            send_message(my_data->socket,PERMIT,0,NULL);
                        else {
                            //send an invalid command to the client
                            send_message(my_data->socket, INVALID_MSG,46,"Client asked for a station hat doesn't exist\0");
                            msg_err = 1;//there is a error with the message - close connection
                        }
                    }
                    else {
                        send_message(my_data->socket, INVALID_MSG,31,"Hello Message wasn't received\0"); //send an invalid command to the client
                        msg_err = 1;//there is a error with the message - close connection
                    }

                    break;
                case UPSONG: //TODO - check that the message is in the right format
                    song_size=(uint32_t )((buffer[1]<<24)+(buffer[2]<<16)+(buffer[3]<<8)+(buffer[4]));
                    //if the song size is not as permitted
                    if(!(song_size>=MIN_SONG_SIZE && song_size<=MAX_SONG_SIZE)) {
                        send_message(my_data->socket, INVALID_MSG,28,"Song size isn't perrmitted\0"); //send an invalid command to the client
                        msg_err = 1;//there is a error with the message - close connection
                    }
                    else{
                        song_name_len=(uint8_t)buffer[5];
                        song_name=(char*)calloc(1,song_name_len*sizeof(char));
                        i=0;
                        for(i=0;i<song_name_len;i++)
                            song_name[i]=buffer[6]; //copy the song name to the song name buffer
                        //TODO - add check_new_song(), add send_message(PERMIT)
                        upload_song(song_name,my_data->socket);

                    }
                    break;
                default: //if any other CommandType except 0,1,2 is receive, send a INVALID message
                    send_message(my_data->socket, INVALID_MSG,28,"Wrong CommandType received\0"); //send an invalid command to the client
                    msg_err = 1;//there is a error with the message - close connection
                    break;
            }
        }

    }
    if (close(my_data->socket) != 0) {
        perror("failed to close Socket");
        exit(1);
    }
    pthread_exit(&(my_data->client_thread));

}
//#endif
//WELCOME,
//ANNOUNCE,
//PERMIT,
//NEW_STATION,
//INVALID_MSG,
int send_message(int socket,SERVER_RESPONSE ans,int str_len,char* reply_str){
    int index;
    uint8_t ReplyType;
    char buffer[9];
    switch (ans){
        case WELCOME:
            ReplyType=0;
            buffer[0]=(char)ReplyType;
            buffer[1]=(char)(NUM_OF_STATIONS&0xFF00)>>8;
            buffer[2]=(char)(NUM_OF_STATIONS&0x00FF);
            break;
        case ANNOUNCE:
            break;
        case PERMIT:
            break;
        case NEW_STATION:
            break;
        case INVALID_MSG:
            break;
    }
    return 0;
}

//TODO - build function
int upload_song(char* song_name,int socket){
    return 0;
}
#endif