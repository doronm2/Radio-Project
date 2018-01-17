/* radio_server - Final projects
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
#include <fcntl.h>

#define Buffer_size 	1024
#define DEBUG_MODE      1
#ifndef uint32_t
#define uint32_t unsigned long long int
#endif
/***************************************Structs*****************/

struct UDP_DATA{
	int num_of_station;
	char *song_name;
	int song_alloc;
	struct in_addr ip_address;
	int port;
	int shutdown;
	pthread_t station_thread;
	struct UDP_DATA *next;
};

struct TCP_DATA{
	int socket;
	int client_index;
	char *clientIP;
	int song_alloc;
	int active;
	int station_num;
	pthread_t client_thread;
};

//state defenitions
#define 	HELLO 			0
#define 	ASKSONG			1
#define 	UPSONG  		2

typedef enum {
	WELCOME,
	ANNOUNCE,
	PERMIT,
	NEW_STATION,
	INVALID_MSG,
}SERVER_RESPONSE;

static int END_SERVER=0;
static struct TCP_DATA Client_List[100];  //the list is {socket,thread,station name,active(1 for active, 0 for inactive)}
static int NUM_OF_STATIONS=0;
static int DOWNLOAD_SONG=0; //0- no song is uploading, 1- a song is uploading, 2 - a song has uploaded
static int UDP_PORT=0;
static char *root_ip;
static uint32_t MIN_SONG_SIZE=2000;
static uint32_t MAX_SONG_SIZE=10000000;
struct UDP_DATA *ROOT_ST;
/***************************************Functions******** **************/

int close_connections(struct UDP_DATA *root_station);
void* handle_client(void* client_data_ind);
void* send_new_station(void* data);
void* handle_station(void* data);
int check_new_song(char* song_name,int song_len);
int open_new_station(char* song_name);
int download_song(char* song_name,ssize_t  song_len,int socket);
int send_message(int socket,SERVER_RESPONSE ans,int str_len,char* reply_str);
int initial_connection(struct TCP_DATA my_data);
char* find_song(int channel);
void print_server_data();
void print_clients_station();

/****************** Server  ****************/
int main(int argc, char* argv[])
{
	int welcomeSocket,tcp_port,udp_port,i=0,client_ind=0;
	int reuse=1,retval,print_welcome=0;
	struct sockaddr_in serverAddr;
	struct sockaddr_storage serverStorage;
	char serverIO;
	struct in_addr ip;
	socklen_t addr_size;
	struct UDP_DATA *stations, *temp_station;
	fd_set rfds;
	struct timeval tv;
	struct sockaddr_in* pV4Addr;
	struct in_addr ipAddr;
	pthread_t new_station;

	if(argc<2) {
		perror("not enough parameters to run the server\nSERVER EXIT");
		return EXIT_FAILURE;
	}
	else if(DEBUG_MODE==1)
		printf("input received: exe File:%s, TCP port:%s,Multicast IP:%s"
				", UDP port:%s,(total of %d args)\n"
				, argv[0], argv[1], argv[2], argv[3], argc);
	for(client_ind=0;client_ind<100;client_ind++)
	{
		Client_List[client_ind].client_index=0;
		Client_List[client_ind].active=0;
		Client_List[client_ind].station_num=0;
		Client_List[client_ind].clientIP=NULL;
		Client_List[client_ind].song_alloc=0;
	}
	//init the client list
	client_ind=0;

	stations=(struct UDP_DATA*)malloc(sizeof(struct UDP_DATA)); //allocate the stations udp for each udp multicast
	ROOT_ST=stations;

	tcp_port = atoi(argv[1]); //for the tcp port

	udp_port = atoi(argv[3]); //for the udp port
	UDP_PORT=udp_port;

	root_ip = argv[2]; ///the initial multicast address
	inet_aton(root_ip,&ip);


	//***********Get and open a thread for each station*********//
	for(NUM_OF_STATIONS=0;NUM_OF_STATIONS<(argc-4);NUM_OF_STATIONS++){
		if(NUM_OF_STATIONS!=0) {
			temp_station=(struct UDP_DATA*)malloc(sizeof(struct UDP_DATA)); //allocate a new station
			stations->next= temp_station;
			stations=stations->next;
		}
		stations->ip_address.s_addr=(uint32_t)ntohl(htonl(ip.s_addr)+NUM_OF_STATIONS);
		stations->num_of_station=NUM_OF_STATIONS;
		stations->shutdown=0; // flag to inform a the thread to close
		stations->port=udp_port;
		stations->song_name=argv[4+NUM_OF_STATIONS];
		stations->song_alloc=0;
		if( pthread_create( &stations->station_thread , NULL ,  &handle_station , (void*)stations) < 0)
			perror("could not create thread");
	}
	// a thread to send new message to all the clients
	pthread_create(&new_station,NULL,&send_new_station,(void*)ROOT_ST);//TODO - fix leak

	// Create the socket. The three arguments are:
	/* 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case) */
	welcomeSocket = socket(PF_INET, SOCK_STREAM, 0);
	if (welcomeSocket == -1) { perror("Can't create welcome socket"); close(welcomeSocket); exit(1); }

	/*---- Configure settings of the server address struct ----*/
	serverAddr.sin_family = AF_INET; /* Address family = Internet */
	serverAddr.sin_port = htons((uint16_t )tcp_port); 	/* Set port number, using htons function to use proper byte order */
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Set IP address to localhost */
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); 	/* Set all bits of the padding field to 0 */

	/*---- Bind the address struct to the socket ----*/
	setsockopt(welcomeSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
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
	while(END_SERVER==0) {
		if(print_welcome==0){
			print_welcome=1;
			printf("\n**************Welcome to Radio Server!**************"
					"\nTo print the Server data press 'p'\nTo exit the program, press 'q'"
					"\n****************************************************\n");
		}
		tv.tv_sec=0;
		tv.tv_usec=100;
		FD_ZERO(&rfds);
		FD_SET(welcomeSocket, &rfds);
		FD_SET(STDIN_FILENO, &rfds); //rfds[0] = STDIN_FILENO (0). set rfds
		retval = select(welcomeSocket+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
		if(FD_ISSET(welcomeSocket, &rfds)){
			for(client_ind=0;client_ind<100;client_ind++)
				if(Client_List[client_ind].active==0)
					break;
			if(client_ind==100)
				perror("Server unable to connect new clients");
			else{

				Client_List[client_ind].socket = accept(welcomeSocket, (struct sockaddr *)&serverStorage, &addr_size);
				if (retval == -1) //error in select
					perror("select() error");

				if (Client_List[client_ind].socket == -1) {
					perror("Error in accepting new client");
					close(Client_List[client_ind].socket);
					break;
				}
				else { printf("accepted new client. \n"); }

				//****get the client IP****//
				pV4Addr = (struct sockaddr_in*)&serverStorage;
				ipAddr = pV4Addr->sin_addr;
				Client_List[client_ind].clientIP=calloc(INET_ADDRSTRLEN, sizeof(char)); //TODO - fix leak
				Client_List[client_ind].song_alloc=1;
				inet_ntop( AF_INET, &ipAddr.s_addr,Client_List[client_ind].clientIP,INET_ADDRSTRLEN);
				if(DEBUG_MODE==1)
					printf("new Client IP %s\n",Client_List[client_ind].clientIP);
				Client_List[client_ind].client_index=client_ind;
				/*---- Accept call creates a new socket for the incoming connection ----*/
				if( pthread_create(&Client_List[client_ind].client_thread,NULL,&handle_client,(void*)client_ind) < 0)//TODO - fix leak
				{
					perror("could not create thread");
					close(Client_List[client_ind].socket);
				}
				else {
					fflush(stdout);
					printf("new thread created for client %d\n", client_ind);
				}
				i++;
				//fill the client list for management;
			}
		}

		else if(FD_ISSET(STDIN_FILENO,&rfds)){
			scanf("%c",&serverIO);
			print_welcome=0;
			switch (serverIO){
			case 'p': // print all stations and clients
				print_server_data();
				print_clients_station();
				break;
			case 'q': // quit the program
				END_SERVER=1;
				break;
			default:
				perror("wrong Input, please try again");
				break;
			}
		}

	}
	if(close_connections(ROOT_ST)==0)
		printf("successfully closed TCP and UDP Sockets and Threads");
	// Wait for socket to have data, Read socket data into buffer if there is Any data

	return EXIT_SUCCESS;
}

/*A function to Handle the Stations Threads
 * the function initalize in 2 cases
 * 1.a song was received upon initialize of the Main proccess
 * 2.a new song has been uploaded
 * the function will Multicast the Song in the given Multicast IP and port,
 * and will send the song in 16Kbps.
 * the function will close the thread if
 *  1.the Main thread will change END_SERVER
 */
void* handle_station(void* data){
	struct UDP_DATA *my_data=(struct UDP_DATA*)data;
	FILE * song_ptr;
	int sock,i=0,bytes_sent=0,curr_sent=0, sent_err=0,j;
	char* Multicast_IP;
	char message[Buffer_size] = {0};
	u_char TTL =10;
	int reuse = 1;
	struct sockaddr_in serverAddr;

	//try to open the socket to send the data on
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) { perror("Can't create socket"); close(sock); pthread_exit(NULL); }
	Multicast_IP=(char*)calloc(strlen(inet_ntoa(my_data->ip_address)),sizeof(char));
	sprintf(Multicast_IP,"%s",inet_ntoa(my_data->ip_address));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr(Multicast_IP);
	serverAddr.sin_port = htons((uint16_t) my_data->port);

	//try to bind this UDP socket
	bind(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &TTL, sizeof(TTL)); //set TTL field to 10

	//try to open the text file to send
	song_ptr = fopen (my_data->song_name , "r");
	if(!song_ptr) {
		printf("couldn't open text file to send");
		close(sock);
		exit(1);
	}
	fflush(stdout);
	printf("Station %d has opened song %s\n",my_data->num_of_station,my_data->song_name);
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
	if(Multicast_IP!=NULL){
		printf("Multicast_IP - %s\n",Multicast_IP);
		free(Multicast_IP);
		printf("Multicast_IP free'ed'\n");
	}
	if(my_data->song_name!=NULL && my_data->song_alloc==1){
		printf("my_data->song_name - %s\n",my_data->song_name);
		free(my_data->song_name);
		printf("my_data->song_name free'ed'\n");
	}
	pthread_exit((void *) my_data->station_thread);
}

/*
 * A function to close all the connections
 * the function receives a UDP_DATA struct linked-list
 * the function will close all the UDP stations threads and Multicast
 * and then close all TCP sockets and client threads
 * the function will return 0 if successful, or -1 if failed
 */
int close_connections(struct UDP_DATA *root_station){
	struct UDP_DATA *temp;
	int Check,index=0;
	/********Close all tcp sockets and threads*********/
	for(index=0;index<100;index++){
		if(Client_List[index].active==1) {
			if(Client_List[index].clientIP!=NULL && Client_List[index].song_alloc==1){
				printf("ClientIP %d - %s\n",index,Client_List[index].clientIP);
				free(Client_List[index].clientIP);
				printf("ClientIP %d free'ed\n",index);
			}
			Check = pthread_join((pthread_t) Client_List[index].client_thread, NULL);
			//wait for the threads to end
			if (Check != 0) {
				perror("failed to close thread");
				exit(1);
			}
			Client_List[index].active=0;
		}
	}
	index =0;
	while(root_station->next!=NULL){
		Check = pthread_join(root_station->station_thread, NULL);        //wait for the threads to end
		if (Check != 0) {
			perror("failed to close thread");
			exit(1);
		}
		temp=root_station->next;
		if(root_station!=NULL){
			free(root_station);
			printf("Station %d free'ed\n",index);
			index++;
		}
		root_station=temp;
	}
	if(root_station!=NULL){
		free(root_station);
		printf("Station %d free'ed\n",index);
	}
	printf("\n------------All Stations and clients Free'ed---------\n");
	printf("                      Server closed                    \n");
	return 1;
}

/*A function to Handle the Client Threads
 * the function initalize after there is a connection with a client
 * the function will listen to the socket to receive the Client TCP messages,
 * and will handle them, using send_message(),check_new_song(),upload_song().
 * the function will close the Socket and thread in the following cases -
 *  1.the Main thread will change END_SERVER
 *  2.the client TCP message will not be as the protocol demands it
 *  3.there s a timeout with a reply/uploading
 *
 */
void* handle_client(void* client_data_ind){
	int index=(int)client_data_ind;
	struct TCP_DATA my_data;
	int i,msg_len,msg_err=0, Hello_flag=0,check_upload,check_song;
	int optval=1, select_on=0;
	fd_set rfds;
	struct timeval tv;
	socklen_t optlen =sizeof(optval);
	char buffer[Buffer_size+1],*song_name,*ending="mp3",*cmp_str;
	uint8_t commandType = 0,song_name_len, buf32[4];
	uint16_t client_data;
	uint32_t song_size;
	my_data.clientIP=(char*)calloc(strlen(Client_List[index].clientIP),sizeof(char));
	my_data.song_alloc=1;
	strcpy(my_data.clientIP,Client_List[index].clientIP);
	my_data.client_index=index;
	my_data.client_thread=Client_List[index].client_thread;
	my_data.socket=Client_List[index].socket;
	my_data.station_num=0;
	Hello_flag=initial_connection(my_data);
	if(Hello_flag==-1)
		msg_err=1;
	else{
		my_data.active=1;
		Client_List[my_data.client_index].active=1;
	}
	setsockopt(my_data.socket,SOL_SOCKET,SO_KEEPALIVE,&optval,optlen);
	while(END_SERVER==0 && msg_err==0){
		tv.tv_sec=0;
		tv.tv_usec=10000;
		FD_ZERO(&rfds);
		FD_SET(my_data.socket, &rfds);
		select(my_data.socket, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
		select_on=FD_ISSET(my_data.socket, &rfds);
		if(!select_on){
			msg_len= (int) recv(my_data.socket, buffer, Buffer_size , 0);
			if (msg_len==0)
				msg_err=1;
		}
		if(msg_len>0){
			commandType=(uint8_t )buffer[0];
			switch (commandType) {
			case HELLO:
				//send an invalid command to the client
				send_message(my_data.socket, INVALID_MSG,33,"Multiple Hello Message received\0");
				msg_err = 1;//there is a error with the message - close connection
				break;
			case ASKSONG:
				client_data=(uint16_t )((buffer[1]<<8)+buffer[2]);
				//check that an Hello message was received
				if(Hello_flag==1){
					//check that the client asked for a station that exist
					if((client_data>=0 && client_data<NUM_OF_STATIONS)){
						song_name=find_song(client_data);
						send_message(my_data.socket,ANNOUNCE,strlen(song_name),song_name);
						Client_List[my_data.client_index].station_num=client_data; //change station number
						my_data.station_num=client_data; //change station number
					}
					else {
						//send an invalid command to the client
						send_message(my_data.socket, INVALID_MSG,46,"Client asked for a station hat doesn't exist\0");
						msg_err = 1;//there is a error with the message - close connection
					}
				}
				else {
					//send an invalid command to the client
					send_message(my_data.socket, INVALID_MSG,31,"Hello Message wasn't received\0");
					msg_err = 1;//there is a error with the message - close connection
				}

				break;
			case UPSONG:
				for(i=0;i<4;i++)
					buf32[i]=buffer[1+i];
				song_size=ntohl(*(uint32_t*)buf32);
				//if the song size is not as permitted
				if(!(song_size>=MIN_SONG_SIZE && song_size<=MAX_SONG_SIZE)) {
					//send an invalid command to the client
					send_message(my_data.socket, INVALID_MSG,28,"Song size isn't perrmitted\0");
					msg_err = 1;//there is a error with the message - close connection
				}
				else {
					song_name_len = (uint8_t) buffer[5];
					song_name = (char *)calloc(song_name_len ,sizeof(char));
					i = 0;
					for (i = 0; i < song_name_len; i++)
						song_name[i] = buffer[6 + i]; //copy the song name to the song name buffer
					cmp_str=strstr(song_name,ending); //check that the name has "mp3" in it
					if(cmp_str==NULL){
						//send an invalid command to the client
						send_message(my_data.socket, INVALID_MSG,22,"Song name isn't .mp3\0");
						msg_err = 1;//there is a error with the message - close connection
					}
					else{
						check_song=check_new_song(song_name,song_name_len); //check that the song isn't playing already
						if (check_song == 0) {
							if(DOWNLOAD_SONG==0){
								send_message(my_data.socket, PERMIT,1,NULL); //permit the client to upload the song
								//upload the song to the server
								check_upload = download_song(song_name, song_size, my_data.socket);
								if (check_upload == -1) {
									send_message(my_data.socket, INVALID_MSG, 28, "Problem Uploading song\0");
									msg_err = 1;
								}
								else
									open_new_station(song_name);
							}
							else
								send_message(my_data.socket, PERMIT, 0, NULL);
						}
						else{
							send_message(my_data.socket, PERMIT, 0, NULL);
						}
					}
					free(song_name);
				}

				break;
			default: //if any other CommandType except 0,1,2 is receive, send a INVALID message
				//send an invalid command to the client
				send_message(my_data.socket, INVALID_MSG,28,"Wrong CommandType received\0");
				msg_err = 1;//there is a error with the message - close connection
				break;
			}
		}

	}
	if (close(my_data.socket) != 0){
		perror("failed to close Client Socket");
		pthread_exit(&(my_data.client_thread));
	}
	if(my_data.clientIP!=NULL && my_data.song_alloc==1){
		printf("Trying to free Client %d, IP - %s\n",my_data.client_index,my_data.clientIP);
		free(my_data.clientIP);
		printf("Client %d IP free'ed\n",my_data.client_index);
	}
	Client_List[my_data.client_index].active=0; //mark as inactive
	printf("Client %d is closed\n",my_data.client_index);

	pthread_exit(&(my_data.client_thread));

}
int initial_connection(struct TCP_DATA my_data){
	int msg_len=0, Hello_flag=0;
	char buffer[Buffer_size+1];
	uint8_t commandType = 0;
	fd_set rfds;
	uint16_t client_data;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 100000; //100 miliseconds timeout as defined
	//set timeout for upload at 3 seconds
	FD_ZERO(&rfds);
	FD_SET(my_data.socket, &rfds);
	select(my_data.socket+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
	if(FD_ISSET(my_data.socket,&rfds))
		msg_len= (int) recv(my_data.socket, buffer, Buffer_size , 0);
	if(msg_len>0){
		commandType=(uint8_t )buffer[0];
		if(commandType==0){
			client_data=(uint16_t )((buffer[1]<<8)+buffer[2]);
			if (client_data==0) {
				send_message(my_data.socket, WELCOME,0,NULL); //send WELCOME_MESSAGE to the client
				Hello_flag=1;
			}
			else {
				//send an invalid command to the client
				send_message(my_data.socket, INVALID_MSG,22,"wrong reserved Value\0");
				Hello_flag = -1;//there is a error with the message - close connection
			}
		}
	}
	else{
		//send an invalid command to the client
		send_message(my_data.socket, INVALID_MSG,23,"Hello Message timeout\0");
		Hello_flag = -1;//there is a error with the message - close connection
	}
	return Hello_flag;
}
void* send_new_station(void* data){
	struct UDP_DATA *my_data=(struct UDP_DATA *)data;
	int i;

	while(END_SERVER==0){
		if(DOWNLOAD_SONG==2){
			printf("/----Sending NEW STATIONS to the clients----/\n");
			while(my_data->next!=NULL)
				my_data=my_data->next;
			if(my_data->num_of_station==NUM_OF_STATIONS){
				NUM_OF_STATIONS++;
				for(i=0;i<100;i++) //ANNOUNCE to all clients on new station!
					if(Client_List[i].active==1){
						printf("Sent Message to -%s\n",Client_List[i].clientIP);
						send_message(Client_List[i].socket,NEW_STATION,NUM_OF_STATIONS,NULL);
					}
				DOWNLOAD_SONG=0;
			}
		}
	}
	pthread_exit(&(my_data->station_thread));

}


/*A function to build and send the messages according to the 'Radio protocol' defined
 * the function receives - Socket number(int), Message response(enum),
 *                          the length of the String(if exist), and the string to send(if exist)
 *  the function returns - 0 - Send success, -1 - Send Failed
 *enum options
 *WELCOME,
 *ANNOUNCE,
 *PERMIT,
 *NEW_STATION,
 *INVALID_MSG,
 * */
int send_message(int socket,SERVER_RESPONSE ans,int str_len,char* reply_str){
	int index,msg_len=0;
	uint8_t ReplyType;
	uint16_t temp_16;
	struct in_addr temp_mcast;
	unsigned char buffer[50]={'\0'},buf32[4]={'\0'},buf16[2]={'\0'};//Largest message - 50 Bytes

	switch (ans){
	case WELCOME:
		ReplyType=0;
		//Reply Type - 1 Byte
		buffer[0]=ReplyType;
		//Multicast Group - 4 Bytes
		inet_aton(root_ip,&temp_mcast);
		*(uint32_t*)&buf32=ntohl(temp_mcast.s_addr);
		buffer[3]=buf32[0];
		buffer[4]=buf32[1];
		buffer[5]=buf32[2];
		buffer[6]=buf32[3];
		//Num Stations - 2 Bytes
		temp_16=htons(NUM_OF_STATIONS);
		*(uint16_t*)&buf16=temp_16;
		buffer[1]=buf16[0];
		buffer[2]=buf16[1];
		//Port Number - 2 Bytes
		temp_16=htons(UDP_PORT);
		*(uint16_t*)&buf16=temp_16;
		buffer[7]=buf16[0];
		buffer[8]=buf16[1];
		msg_len=9;
		break;
	case ANNOUNCE:
		ReplyType=1;
		str_len=strlen(reply_str);
		//Reply Type - 1 Byte
		buffer[0]=ReplyType;
		//Song Name Size - 1 Byte
		buffer[1]=(uint8_t)str_len;
		//Song Name - str_len Bytes
		for (index = 0; index < str_len; index++)
			buffer[2+index]=(uint8_t )reply_str[index];
		msg_len=2+str_len;
		break;
	case PERMIT:
		ReplyType=2;
		//Reply Type - 1 Byte
		buffer[0]=ReplyType;
		//Song Name Size - 1 Byte
		buffer[1]=(uint8_t)str_len; //1 equals Permit
		msg_len=2;
		break;
	case NEW_STATION:
		ReplyType=4;
		//Reply Type - 1 Byte
		buffer[0]=ReplyType;
		//Song Name Size - 1 Byte
		temp_16=htons(str_len);
		*(uint16_t*)&buf16=temp_16;
		buffer[1]=buf16[0];
		buffer[2]=buf16[1];
		msg_len=3;
		break;
	case INVALID_MSG:
		ReplyType=3;
		//Reply Type - 1 Byte
		buffer[0]=ReplyType;
		//Reply String Size - 1 Byte
		buffer[1]=(uint8_t)str_len;
		//Reply String - str_len Bytes
		for (index = 0; index < str_len+2; index++)
			buffer[2+index]=(uint8_t )reply_str[index];
		msg_len=2+str_len;
		break;
	}
	//Send the message
	if(send(socket,buffer,msg_len*sizeof(char),0) == -1) {
		perror("Error in sending Message");
		return -1;
	}
	return 0;
}

/*
 * A function to upload a song from Client to the server
 * the function receive a song name,song size,and the Client socket
 * the function will create a new FILE - to save the song to
 * then the function will receive the song at 16Kbps (1024 byte each time),
 * and will write it to the FILE
 * the function will return 0 if successful, and -1 if failed
 * the failed cases -
 * 1.couldn't open the FILE
 * 2.the Upload had timed-out
 * 3.the upload size is not as given in song_len
 * 4.other Client is Uploading a song
 */
int download_song(char* song_name,ssize_t song_len,int socket){
	ssize_t checkUp=1,packet_ctr=0,count_size=0;
	FILE* song_fd;
	int ret=0;
	char buffer[Buffer_size];
	struct timeval tv;
	tv.tv_sec = 3; //3 seconds timeout as defined
	tv.tv_usec = 0;
	//set timeout for upload at 3 seconds
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));
	song_fd=fopen(song_name,"w+"); //open the *mp3 file to write to
	if(song_fd<0){
		perror("failed to open the FILE");
		ret = -1;
		goto END;
	}
	//check that there is no other proccess uploading
	if(DOWNLOAD_SONG==0){
		DOWNLOAD_SONG=1;
	}

	else {
		perror("Upload proccess is Taken");
		ret= -2;
		goto END;
	}
	printf("Starting song Download - %s\n",song_name);
	//start receiving the song from the Client
	while(checkUp>0) {
		if((checkUp=recv(socket,buffer, sizeof(buffer),0))>0){
			count_size+=checkUp;
			packet_ctr++;
			fwrite(buffer,sizeof(char),checkUp,song_fd);//write the song to the file
			memset(buffer, '\0', sizeof(buffer));
		}
		if(checkUp!=(size_t)Buffer_size && count_size==song_len){
			checkUp=0;
		}
	}
	if(checkUp<0 && packet_ctr==0){
		perror("Download timeout");
		DOWNLOAD_SONG=0;  //reset the upload flag
		ret= -1;
		goto END;
	}
	else if(count_size==song_len) { //reached EOF, upload completed
		fflush(stdout);
		printf("Download complete\n");
	}
	else{
		perror("Download failed");
		printf( "\nExpected Size %u , given Size %u\n",song_len,count_size);
		DOWNLOAD_SONG=0;  //reset the upload flag
		ret= -1;
		goto END;
	}
	END:
	if(ret==-2) // to keep the upload untouched
		ret=-1;
	else
		DOWNLOAD_SONG=0;
	fclose(song_fd);
	return ret;
}

/*
 * a function to check if the song to upload is new
 * the function receives the song name, and the song name length
 * the function then compare the song name to the rest of the stations already UP
 * the function returns 0 if there are no match(success) and -1 else
 */
int check_new_song(char* song_name,int song_len){
	int check=1,ret=-1,end_of_list=0;
	struct UDP_DATA *root=ROOT_ST;
	if(root!=NULL){
		do{
			check=strncmp(root->song_name,song_name,(size_t)song_len);
			if(root->next!=NULL)
				root=root->next;
			else
				end_of_list=1;
		}while(check!=0 && end_of_list==0);
	}
	if(check!=0){
		ret=0;
	}
	return ret;
}

/*a function to create a new station
 * the function receive a song name
 * the function will find the last node in the linked list,
 * create a new node, and copy the details from the previous node
 * and change it to fit the new station
 * then the function will open a new thread for the station to send the messages on
 * the function returns 0 if the thread was created successfuly, or -1 else
 *
 */
int open_new_station(char* song_name){
	struct UDP_DATA *temp=ROOT_ST;
	while(temp->next!=NULL)
		temp=temp->next;
	temp->next=(struct UDP_DATA*)malloc(sizeof(struct UDP_DATA));
	temp->next->ip_address.s_addr=(uint32_t)ntohl((htonl(temp->ip_address.s_addr))+1);;
	temp->next->num_of_station=temp->num_of_station+1;
	temp->next->shutdown=0; // flag to inform a the thread to close
	temp->next->port=temp->port;
	temp->next->song_name=calloc(strlen(song_name),sizeof(char));
	strcpy(temp->next->song_name,song_name);
	temp->next->song_alloc=1;
	temp=temp->next;
	if( pthread_create( &temp->station_thread , NULL ,  &handle_station , (void*)temp) < 0){ //TODO - fix leak
		perror("could not create thread");
		return -1;
	}
	DOWNLOAD_SONG=2; //change flag for Main proccess to ANNUONCE the new station
	return 0;
}

char* find_song(int channel){
	char* res=NULL;
	struct UDP_DATA *temp=ROOT_ST;
	while(channel!=(temp->num_of_station))
		if(temp->next!=NULL)
			temp=temp->next;
	if(channel==temp->num_of_station)
		res=temp->song_name;
	return res;
}

void print_server_data(){
	int i;
	struct UDP_DATA *temp=ROOT_ST;
	printf("\n***************************************\n");
	printf(	"nServer Stations list:\n");
	for(i=0;i<NUM_OF_STATIONS;i++){
		printf("\n### Station %d is playing on Multicast %s\nSong playing %s\n"
				,i,inet_ntoa(temp->ip_address),temp->song_name);
	}
	printf("Connected Client list/n");
	for(i=0;i<100;i++) {
		if (Client_List[i].active == 1)
			printf("-Client %d with ip address: %s\n",Client_List[i].client_index
					,Client_List[i].clientIP);
	}
	printf("***************************************\n\n");
}

void print_clients_station() {
	int i, j,client_on=0;
	struct UDP_DATA *temp = ROOT_ST;
	printf("\n***************************************\n");
	printf("Server Stations list with Clients listening:\n");
	for (i = 0; i < NUM_OF_STATIONS; i++) {
		printf("\n### Station %d is playing on Multicast %s\nSong playing %s\n"
				,i,inet_ntoa(temp->ip_address), temp->song_name);
		printf("Clients Listening to the Station:\n");
		client_on=0;
		for (j = 0; j < 100; j++) {
			if (Client_List[j].active == 1 && Client_List[j].station_num == i) {
				client_on++;
				printf("-Client %d, with IP address %s\n", Client_List[j].client_index, Client_List[j].clientIP);
			}
		}
		if (client_on == 0)
			printf("---No Clients listenning to the station\n");
		if(temp->next!=NULL)
			temp=temp->next;
		printf("***************************************\n\n");
	}
}

#endif

