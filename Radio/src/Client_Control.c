/* radio controller (client)
Author1: Victor Martinov,
ID: 307835249
Author2: Doron Maman,
ID: 302745146
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <dirent.h>
#include <netdb.h>

#define Buffer_size 	1024
//STATES
#define 	OFF_INIT 				0
#define 	WAIT_WELCOME			1
#define 	ESTABLISHED  			2
#define 	WAIT_ANNOUNCE 			3
#define     UPLOAD_SONG   			4
#define     WAIT_PERMIT   			5
#define     WAIT_NEW_STATIONS   	6
//Definitions
#define 	maxIPlen 				15
#define 	MIN_SONG_SIZE 			2000
#define 	MAX_SONG_SIZE 			1024*1024*10
#define 	ERROR			 		0
#define 	SUCCESS					1
#define 	upload_delay			8000 //set the upload rate at the client to 1KiB every 8000 usec.
#define 	kiB						1024
#define 	max_uint16_t			65535

#define 	server_is_idiot			1

//global variables
int state=0, upload=0, UDP_closed =0, change_station=0, current_station=0,remeinder=0 , TCP_Sock = 0 ,msg_sent = 0 ,multicastGroup;
unsigned short NumStations =0;
int uploading = 0;
char * song_file ,* group;
char user_input [50] = {0};
long fileSize = 0;
pthread_t t;
struct timeval tv;
fd_set rfds;
struct in_addr reference_mCast_addr;
struct in_addr current_mCast_addr;
uint16_t port;

/*typedef struct {
	uint8_t replyType;
	uint16_t numStations;
	char mGroup_IP [maxIPlen];
	uint16_t port;
} Welcome_msg;

typedef struct {
	char mGroup_IP [maxIPlen];
	int mGroup_port;
} UDP_sock_data;*/

int setTCP_sock(char* serv_ip, int serv_port);
int handShake();
void* listener(void* UDP);
void handle_TCP_and_IO();
int handle_TCP_message();
int handle_user_input();
int uploadSong();

int main(int argc, char* argv[]) // Input: SERVER's IP_address/NAME, SERVER's TCP WELCOME port.
{
	int SERVER_TCP_port;
	char * SERVER_TCP_IP;
	struct hostent *hp = gethostbyname(argv[1]); //get real ip address of host, from /etc/hosts file

	if (hp == NULL)
	{ printf("Hostname not found in /etc/hosts file. quitting program.\n"); exit(ERROR); }
	else
		SERVER_TCP_IP = inet_ntoa(*(struct in_addr*)( hp -> h_addr_list[0]));

	//Check that we got from the user all of the arguments to activate the server
	printf("arguments received - exe File: %s, Hostname: %s (TCP IP: %s), TCP port: %s,\n(total of %d args).\n", argv[0], argv[1],SERVER_TCP_IP, argv[2] , argc);
	if(argc != 3) { printf("Too few/many arguments received. Bye bye!\n"); exit(ERROR); }

	SERVER_TCP_port = atoi(argv[2]); //get TCP port from input
	setTCP_sock(SERVER_TCP_IP,SERVER_TCP_port);

	if(pthread_create(&t, NULL, listener, NULL)!=0) //create listener thread (udp multicast - song play)
	{printf("\n\nerror in thread. exiting.");close(TCP_Sock); exit(ERROR);}
	handle_TCP_and_IO();

	//close the connection, free resources.
	if(close(TCP_Sock) == -1 )
	{ perror("Error in closing client socket"); exit(ERROR); }
	while (UDP_closed == 0) {}
	pthread_join(t, NULL);
	return EXIT_SUCCESS;
}

Welcome_msg handShake()
{
	Welcome_msg welcome_msg = {0};
	int rec_bytes = 0, retval=0;
	unsigned char hello_msg [3] = {0,0,0} , welc_buff [9];

	FD_ZERO(&rfds);
	FD_SET(TCP_Sock, &rfds);
	//set time out's
	tv.tv_sec = 0; // wait for 0.2 sec for the WELCOME MSG
	tv.tv_usec = 200000;

	if(send(TCP_Sock,hello_msg,sizeof(hello_msg),0) == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}

	retval = select(TCP_Sock+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
	if (retval == -1) { perror("select() failed in upsong"); state = OFF_INIT; }
	else if(FD_ISSET(TCP_Sock, &rfds) && retval) //data ready from STDIN, while not in upload.
	{
		printf("\nWELCOME msg received from server! ");
	}
	else if(retval ==0) //timeout reached.
	{
		printf("\nWELCOME msg not received within 0.2 seconds. quitting program.\n");
		state = OFF_INIT;
		close (TCP_Sock);
		exit(0);
	}

	state = WAIT_WELCOME;

	rec_bytes = recv(TCP_Sock, welc_buff, sizeof(welc_buff) , 0);
	if (rec_bytes == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}
	else if (rec_bytes != 9)
	{ printf("\nError in welcome message - size incorrect"); return welcome_msg;}

	welcome_msg.replyType = (uint8_t)welc_buff[0];
	welcome_msg.numStations = ntohs(((uint16_t*)(welc_buff+1))[0]);
	NumStations = welcome_msg.numStations;

	sprintf(welcome_msg.mGroup_IP,"%d.%d.%d.%d",welc_buff[6],welc_buff[5],welc_buff[4],welc_buff[3]);	// get multicast address from buffer
	inet_aton(welcome_msg.mGroup_IP, &reference_mCast_addr);

	printf("\n%s\n", inet_ntoa(reference_mCast_addr));
	reference_mCast_addr.s_addr=(uint32_t)ntohl((htonl((reference_mCast_addr.s_addr)))+1);
	printf("%s\n", inet_ntoa(reference_mCast_addr));


	welcome_msg.port = ntohs(((uint16_t*)(welc_buff+7))[0]);

	state = ESTABLISHED;
	return welcome_msg;
}

void* listener(void* UDP)
{
	UDP_sock_data UDP_data = *(UDP_sock_data*) UDP;
	struct ip_mreq mreq;
	int UDP_sock,UDP_pack_len =1, reuse = 1, last_octet=0;
	struct sockaddr_in udp_serverAddr = {0};
	char  message[Buffer_size];
	char ip [maxIPlen];
	FILE * fp;
	socklen_t addr_len;

	UDP_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (UDP_sock == -1) { perror("Can't create UDP socket"); close(UDP_sock); exit(ERROR); }

	//define the port and the multicast IP to send the data on
	udp_serverAddr.sin_family = AF_INET;
	udp_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	udp_serverAddr.sin_port = htons(UDP_data.mGroup_port);
	addr_len = sizeof udp_serverAddr;

	setsockopt(UDP_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	//try to bind to the the UDP socket of the multicast Group
	if(bind(UDP_sock, (struct sockaddr *) &udp_serverAddr, sizeof(udp_serverAddr))==-1)
	{ perror("Can't bind UDP socket"); close(UDP_sock); exit(ERROR); }

/*sprintf(ip,"%d.%d.%d.%d",(UDP_data.mGroup_IP)[0], (UDP_data.mGroup_IP)[1], (UDP_data.mGroup_IP)[2], (UDP_data.mGroup_IP)[3]);	// get multicast address from buffer
	last_octet = (UDP_data.mGroup_IP)[3]; //TODO*/

	mreq.imr_multiaddr.s_addr = inet_addr(UDP_data.mGroup_IP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup

	fp = popen("play -t mp3 -> /dev/null 2>&1", "w");//open a pipe. output is sent to dev/null (hell).

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	while(state != OFF_INIT) // While (not all data received || connection closed by server)
	{
		if(change_station)
		{
			setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP

			current_mCast_addr.s_addr = (uint32_t)ntohl((htonl((reference_mCast_addr.s_addr))) + current_station);

			/*(UDP_data.mGroup_IP)[3] = last_octet + current_station; //todo
			sprintf(ip,"%d.%d.%d.%d",(UDP_data.mGroup_IP)[0], (UDP_data.mGroup_IP)[1], (UDP_data.mGroup_IP)[2], (UDP_data.mGroup_IP)[3]);
			*/

			mreq.imr_multiaddr.s_addr = inet_addr(ip);
			setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup
			change_station = 0;
		}
		UDP_pack_len = recvfrom(UDP_sock, message, sizeof(message), 0, (struct sockaddr *) &udp_serverAddr, &addr_len);
		if(UDP_pack_len > 0)
			fwrite (message , sizeof(char), Buffer_size, fp); //write a buffer of size Buffer_size into fp
		if (UDP_pack_len < 0)
		{
			perror("Error in reading from socket");
			printf("\n");
		}
		else if (UDP_pack_len == 0)// - Connection closed by server
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~UDP Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		}
	}

	//state == OFF_INIT
	setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP
	printf("closing streaming communication! Bye bye!\n");

	// Free all resources;
	close(UDP_sock);
	UDP_closed = 1;
	pthread_exit(&t);
	//return NULL;
}

int handle_user_input()
{
	int Song_name_size,song_size,i=1;
	uint8_t len;
	unsigned char askSong_msg [3] = {1,0,0};
	char* upSong_msg;
	FILE * songP;
	DIR *d;
	struct dirent *dir;

	if(strcmp(user_input, "s")==0 || strcmp(user_input, "S")==0) //upload procedure
	{
		printf("please enter your desired song's name to upload (up to 50 characters, including suffix '.mp3').\n");
		printf("your song upload options:");
		d = opendir(".");
		if (d)
		{
			while ((dir = readdir(d)) != NULL)
			{
				if(strstr(dir->d_name, ".mp3") != NULL)
				{
					printf(" %d) %s", i, dir->d_name);
					i++;
				}
			}
			printf(". please enter your choice.\n");
			closedir(d);
		}
		fflush(stdin);
		gets(user_input);

		songP = fopen(user_input,"r");
		if (songP != NULL)  //check entered file
		{
			Song_name_size = (unsigned short)strlen(user_input);
			fseek(songP, 0L, SEEK_END);
			song_size = ftell(songP); //file's size in bytes
			rewind(songP); // return the pointer back to start of file
			if(song_size > MIN_SONG_SIZE && song_size < MAX_SONG_SIZE)
			{
				printf("file size to upload is: %d bytes.\n",song_size);
				upSong_msg = (char*)calloc(1,(Song_name_size+6)*sizeof(char)); //+6 for command type, song size, and song name size.
				upSong_msg[0] = (uint8_t)2; //command type = 0
				upSong_msg[1] = (song_size >> 24) & 0xFF;
				upSong_msg[2] = (song_size >> 16) & 0xFF;
				upSong_msg[3] = (song_size >> 8) & 0xFF;
				upSong_msg[4] = (song_size) & 0xFF;
				upSong_msg[5] = (uint8_t)Song_name_size;
				for (i=6; i<Song_name_size+6; i++)
				{
					upSong_msg[i] = user_input[i-6];
				}
				if(send(TCP_Sock,upSong_msg,Song_name_size+6,0) == -1)
				{ perror(""); printf("\nError in sending upSong message");}
				else
					msg_sent = 1;
				free(upSong_msg);
				state = WAIT_PERMIT;

				song_file = (char*)calloc(1,Song_name_size*sizeof(char)); //+6 for command type, song size, and song name size.
				for (i=0; i<Song_name_size; i++)
				{
					song_file[i] = user_input[i];
				}
				fileSize = song_size/Buffer_size +1;
				remeinder = song_size-(fileSize-1)*Buffer_size; // doesn't send last part if remeinder == 0
				return 1;
			}
			else
			{
				printf("file size is: %d  the file has to be more then %d bytes or less then %d bytes",song_size,MIN_SONG_SIZE,MAX_SONG_SIZE);
				fclose(songP);
				songP = NULL;
			}
		}
		else //song doesn't exist or not readable.
		{
			perror("file doesn't exist, or is not readable. try again");
			fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit.\n", NumStations-1);
			return SUCCESS;
		}
	}
	else if(strcmp(user_input,"q")==0 || strcmp(user_input ,"Q")==0)
	{
		state = OFF_INIT;
		return SUCCESS;
	}
	else if ((uint16_t)atoi(user_input) >= 0 && (uint16_t)atoi(user_input) < max_uint16_t) //&& (uint8_t)atoi(user_input)<NumStations)//entered a number
	{
		if((uint16_t)atoi(user_input) == 0 && strcmp(user_input, "0") != 0) // not zero! input is LETTERS. invalid input!!
		{
			printf("bad command. please try again.\n");
		}
		else if ((uint16_t)atoi(user_input)<NumStations)//entered a good number
		{
			len = (unsigned)strlen(user_input); //TODO handle 2 byte number.
			if(len==1)// && (*user_input >= 0 && *user_input <= NumStations))
			{
				askSong_msg[2] = (uint8_t)atoi(user_input);
				current_station = (int)atoi(user_input);
				if(send(TCP_Sock,askSong_msg,sizeof(askSong_msg),0) == -1)
				{ perror(""); printf("\nError in sending askSong message");}
				else
					msg_sent = 1;
				state = WAIT_ANNOUNCE;
			}
		}
		else
			printf("station %d doesn't exist. please try again.\n",(uint16_t)atoi(user_input));
	}
	return SUCCESS;
}

UDP_sock_data setTCP_sock (char* serv_ip, int serv_port)
{
	int i;
	struct sockaddr_in serverAddr;
	UDP_sock_data sock_info; //UDP socket info
	Welcome_msg welc = {0};
	// Create the TCP&UDP socket and set it’s properties. The three arguments are: 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case).
	TCP_Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (TCP_Sock == -1)  { perror("Can't create TCP socket"); close(TCP_Sock); exit(ERROR); }

	/*---- Configure settings of the server address struct ----*/
	serverAddr.sin_family = AF_INET; /* Address family = Internet */
	serverAddr.sin_port = htons(serv_port); 	/* Set destination TCP_port number, using htons function to use proper byte order */
	serverAddr.sin_addr.s_addr = inet_addr(serv_ip); // set destination IP number - localhost, 127.0.0.1
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); /* Set all bits of the padding field to 0 */

	/*---- Connect the socket to the server using the address struct ----*/
	if(connect(TCP_Sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))== -1)
	{ perror("Can't connect to server"); close(TCP_Sock); exit(ERROR); }

	welc = handShake(TCP_Sock); //handshake with server, get relevant data from it.
	if(welc.numStations == 0 && welc.mGroup_IP==0 && welc.port==0) //no real data received
	{
		printf("error in welcome message! quitting program.");
		close(TCP_Sock);
		exit(ERROR);
	}
	else
		fprintf(stdout,"connection established. data received from server:\n%d stations, mGroup IP: %s, mGroup port: %d. playing"
				" station 0.",welc.numStations,welc.mGroup_IP,welc.port);

	strcpy(sock_info.mGroup_IP, welc.mGroup_IP);

	sock_info.mGroup_port = welc.port;

	return sock_info; //return UDP socket info
}

void handle_TCP_and_IO()
{
	int retval=0,i=0;

	fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit.\n", NumStations-1);
	while(state != OFF_INIT) //no problem occurred and user doesn't want to quit.
	{
		FD_ZERO(&rfds); //reset file descriptor reader
		FD_SET(0, &rfds); //rfds[0] = STDIN_FILENO (0). set rfds
		FD_SET(TCP_Sock, &rfds); //set rfds
		tv.tv_sec = 0; //set timeout
		tv.tv_usec = 100000;  		//100 ms

		retval = select(TCP_Sock+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
		if (retval == -1) //error in select
			perror("select() error");
		else if(retval) //some file descriptor has data
		{
			if(FD_ISSET(0, &rfds) && !uploading) //data ready from STDIN, while not in upload.
			{
				fflush(stdin); //clean input buffer
				gets(user_input);
				handle_user_input(TCP_Sock);
				for(i=0;i<50;i++) //clean user input buffer for next time
					user_input [i]=0;
			}
			if (FD_ISSET(TCP_Sock, &rfds) && (state != WAIT_NEW_STATIONS)) //data ready from TCP socket, while not waiting for newstations msg
			{
				if(msg_sent == 1)
					msg_sent = 0;
				handle_TCP_message(TCP_Sock);
				if (state != OFF_INIT) //still online
					fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit.\n", NumStations-1);
			}
		}
		if(retval==0 && msg_sent == 1) //timeout reached when sending message. quit.
		{
			state = OFF_INIT;
		}
	}
	if (state == OFF_INIT) //quitting program
		return;
}

int handle_TCP_message()
{
	int TCP_pack_len=0, arr_len=0,i;
	char *arr;
	uint8_t replyType = 0,permit=0;
	char buffer[Buffer_size];

	TCP_pack_len = recv(TCP_Sock, buffer, Buffer_size , 0); // Read socket data into buffer if there is Any data
	if(TCP_pack_len > 0)
	{
		replyType = (uint8_t)buffer[0];
		switch(replyType)
		{
		case 0: //Welcome msg - invalid at this stage
			printf("msg received with WELCOME msg CODE- invalid at this stage. exiting program");
			state = OFF_INIT;
			return ERROR;

		case 1: //announce msg
			if(TCP_pack_len >=2 && state == WAIT_ANNOUNCE)
			{
				printf("announce msg received!!");
				arr_len = (uint8_t)buffer[1];
				arr = (char*)calloc(arr_len, sizeof(char));
				for(i=0;i<arr_len;i++)
					arr[i] = buffer[i+2];
				printf(" changing to new station! new station's song name: %s.",arr);
				change_station = 1;
				state = ESTABLISHED;
				free(arr);
				return SUCCESS;
			}
			else { state = OFF_INIT; return ERROR; }
			break;

		case 2: //PermitSong msg
			if(TCP_pack_len == 2 && state == WAIT_PERMIT)
			{
				printf("PermitSong msg received!! ");
				permit = (uint8_t)buffer[1];
				if(permit == 1)
				{
					state = UPLOAD_SONG;
					printf("song permitted to upload!! uploading.. (no I/O currently possible).\n");
					uploading = 1;
					if(uploadSong(TCP_Sock) == 1) //uploaded successfully
					{
						uploading = 0; //no upload
						state = ESTABLISHED;
						fflush(stdin);
					}
					else
					{
						printf("error in uploading song or server is a dumbass. quitting program.\n");
						state = OFF_INIT;
						fflush(stdin);
					}
				}
				else if(permit == 0)
				{
					printf("song not permitted to upload =( please try a different song.\n");
					return SUCCESS;
				}
			}
			else return 1;
			break;

		case 3: //Invalid_command msg
			if(TCP_pack_len >=2)
			{
				arr_len = (uint8_t)buffer[1];
				arr = (char*)calloc(arr_len, sizeof(char));
				for(i=0;i<arr_len;i++)
					arr[i] = buffer[i+2];
				printf("invalidCommand msg recieved: ""%s"". quitting program.",arr);
				state = OFF_INIT;
				free(arr);
				return ERROR;
			}
			else { printf("invalid invalidCommand msg recievedq. quitting program."); state = OFF_INIT; return ERROR; }
			break;

		case 4: //NewStations msg
			if(TCP_pack_len == 3)
			{
				NumStations = ntohs(((uint16_t*)(buffer+1))[0]);
				printf("newstation announced by server!!! we now offer %d stations!",NumStations);
				state = ESTABLISHED;
			}
			else { printf("invalid NewStations msg recieved. quitting program."); state = OFF_INIT; return ERROR; }
			break;

		default : //	InvalidCommand or announce msg - check which one and handle accordingly
			printf("bad message from server. quitting program =(");
			state = OFF_INIT;
			return ERROR;
			break;
		}
	}
	else if (TCP_pack_len < 0)
	{ perror("Error in reading from TCP socket"); printf("\n"); }
	else if (TCP_pack_len == 0)
	{
		printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		state = OFF_INIT;
		return ERROR;
	}

	return SUCCESS;
}

int uploadSong() //, int parts, char* song_file, int song_size)  	//UPLOAD THE SONG!!!!!!!!!
{
	int retval=0,i=0,TCP_pack_len=0;
	float percent = 0;
	char  song_buff[kiB], buffer[Buffer_size];
	FILE * song;
	useconds_t usec_delay = upload_delay;
	uint8_t replyType = 0;

	song = fopen(song_file,"r");
	if(!song) { perror("uploadSong - can't open song file"); free(song_file); state = OFF_INIT; return ERROR; }

	while (fscanf(song, "%1024c", song_buff) != EOF && state != OFF_INIT && i < fileSize)
	{
		printf("file Uploading status: %.0f%% \r",percent);
		fflush(stdout);
		if (i == fileSize - 1 && remeinder != 0) //UPLOAD LAST PART
		{
			if(send(TCP_Sock,song_buff,remeinder,0) == -1)
			{ perror(""); printf("\nError in uploading last part"); state = OFF_INIT; }
			else
			{
				printf("file Uploading status: 100%%. ");
				printf("finished song uploading!!\n");
			}
		}
		else
		{
			if(send(TCP_Sock,song_buff,Buffer_size,0) == -1)
			{ perror(""); printf("\nError in uploading song"); state = OFF_INIT; break;}
			percent = (i++)*100/fileSize;
			usleep(usec_delay);
		}
	}
	if((fscanf(song, "%1024c", song_buff))==EOF && !server_is_idiot) //EOF reached.
	{
		fclose(song);
		state = WAIT_NEW_STATIONS;
		FD_ZERO(&rfds);
		FD_SET(TCP_Sock, &rfds);
		tv.tv_sec = 2; //set timeout - wait for 2 sec for the new station massage
		tv.tv_usec = 0;

		retval = select(TCP_Sock+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
		if (retval == -1) { perror("select() failed in upsong"); state = OFF_INIT; }
		else if(retval)
		{
			if(FD_ISSET(TCP_Sock, &rfds)) //data ready from STDIN, while not in upload.
			{
				TCP_pack_len = recv(TCP_Sock, buffer, Buffer_size , 0); // Read socket data into buffer if there is Any data
				if(TCP_pack_len > 0)
				{
					replyType = (uint8_t)buffer[0];
					if(replyType ==4 && TCP_pack_len == 3)  //NewStations msg
					{
						NumStations = ntohs(((uint16_t*)(buffer+1))[0]);
						printf("NewStations announced by server!!! we now offer %d stations! input possible again.",NumStations);
						return SUCCESS;
					}
					else
					{
						printf("bad message received from server! quitting program.");
						state = OFF_INIT;
					}
				}
				else
					perror("bad TCP pack length");
			}
		}
		else if(retval == 0) //timeout reached.
		{
			printf("NewStations msg not received within 2 seconds. quitting program.\n");
			state = OFF_INIT;
		}
	}
	else if (server_is_idiot)
	{
		free(song_file);
		return SUCCESS;
	}
	printf("\nError in uploading song, couldn't reach end of file.");
	free(song_file);
	return ERROR;
}


/*
 *		//if((access(user_input, F_OK) == file_exists) && (access(user_input, R_OK ) == file_readable)) // file exists and readable.send upsong message to server.
 */



