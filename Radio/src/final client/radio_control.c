/*
~~~ radio controller (client) ~~~
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

#define 	Buffer_size 		1024
//STATES
#define 	OFF_INIT 			0
#define 	WAIT_WELCOME		1
#define 	ESTABLISHED  		2
#define 	WAIT_ANNOUNCE 		3
#define     UPLOAD_SONG   		4
#define     WAIT_PERMIT   		5
#define     WAIT_NEW_STATIONS   6
//Definitions
#define 	maxIPlen 			15 //all octets "full"
#define 	MIN_SONG_SIZE 		2000 //2kb
#define 	MAX_SONG_SIZE 		1024*1024*10 //10Mibs
#define 	ERROR			 	0 // func FAIL return value
#define 	SUCCESS				1  // func SUCCESSFULL return value
#define 	upload_delay		11000 //set the upload rate at the client to 1KiB every 8000 usec.
#define 	kiB					1024
#define 	max_num_stations	65535
#define 	max_uInput_len		100 //user input length

#define 	server_is_idiot		0//server that doesn't send newStations msg to uploading client


//global variables
int state=0, upload=0, change_station=0, current_station=0,remeinder=0 , TCP_Sock = 0 ,msg_sent = 0 ,multicastGroup , COUNT=0;
unsigned short NumStations =0;
int uploading = 0; //to indicate we are in upload process
char * song_file; //uploaded song file name
char user_input [max_uInput_len] = {0};
long fileSize = 0; //size to upload.
pthread_t t; //single thread
struct timeval tv; //for timeouts
fd_set rfds; //file descriptor reader
struct in_addr reference_mCast_addr; //reference point to first Mcast address.
struct in_addr current_mCast_addr;

typedef struct {
	char mGroup_IP [maxIPlen];
	uint16_t mGroup_port;
} UDP_sock_data;


UDP_sock_data setTCP_sock(char* serv_ip, int serv_port);
UDP_sock_data handShake();
void* listener(void* UDP);
void handle_TCP_and_IO();
int handle_TCP_message();
int handle_user_input();
int uploadSong();

int main(int argc, char* argv[]) // Input: SERVER's IP_address/NAME, SERVER's TCP WELCOME port.
{
	UDP_sock_data udpData; //mCast data
	int SERVER_TCP_port;
	char * SERVER_TCP_IP;
	struct hostent *hp = gethostbyname(argv[1]); //get real ip address of host, from /etc/hosts file. hp == host pointer

	if (hp == NULL) //host pointer == null
	{ printf("Hostname not found in /etc/hosts file. quitting program.\n"); exit(ERROR); }
	else
		SERVER_TCP_IP = inet_ntoa(*(struct in_addr*)( hp -> h_addr_list[0])); //string ip address

	/*Check that we got from the user all of the arguments to activate the program
	printf("arguments received - exe File: %s, Hostname: %s (TCP IP: %s), TCP port: %s,\n(total of %d args)."
			, argv[0], argv[1],SERVER_TCP_IP, argv[2] , argc);*/
	if(argc != 3) { printf("Too few/many arguments received. Bye bye!\n"); exit(ERROR); }

	printf("connecting to radio server..\n");
	SERVER_TCP_port = atoi(argv[2]); //get TCP port from input
	udpData = setTCP_sock(SERVER_TCP_IP,SERVER_TCP_port); //get UDP port and ip (multicast data)

	if(pthread_create(&t, NULL, listener, &udpData)!=0) //create listener thread (udp multicast - song play)
	{printf("\n\nerror in thread. exiting.");close(TCP_Sock); exit(ERROR);}
	handle_TCP_and_IO(); //with select()

	//close the connection, free resources.
	if(close(TCP_Sock) == -1 )
	{ perror("Error in closing client TCP socket"); exit(ERROR); }
	//while (UDP_closed != 1) {} //wait for thread to finish
	pthread_join(t, NULL);
	return EXIT_SUCCESS;
}

UDP_sock_data setTCP_sock (char* serv_ip, int serv_port)
{
	struct sockaddr_in serverAddr; //for socket conf
	UDP_sock_data udp_data; //mCast data
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
	{ perror("TCP Can't connect to server"); close(TCP_Sock); exit(ERROR); }

	udp_data = handShake(TCP_Sock); //handshake with server, get relevant data from it.
	if(strcmp(udp_data.mGroup_IP,"000000000000000") == 0 || udp_data.mGroup_port == 0 || !NumStations) //no real data received
	{ printf("\nerror in welcome message! quitting program.\n"); close(TCP_Sock); exit(ERROR); }
	else
		fprintf(stdout,"connection established. data received from server:\n%d stations, mGroup IP: %s, mGroup port: %d. playing"
				" station 0.",NumStations,udp_data.mGroup_IP,udp_data.mGroup_port);
	return udp_data; //return UDP socket info
}

UDP_sock_data handShake() //perform handshake and get mCast data.
{
	UDP_sock_data welc_msg; //mast data received in welc msg.
	uint8_t replyType;
	int rec_bytes = 0, retval=0,i;
	unsigned char hello_msg [3] = {0,0,0} , welc_buff [9];

	//reset the struct.
	for (i=0;i<maxIPlen ; i++)
		welc_msg.mGroup_IP[i] = '0';
	welc_msg.mGroup_port =0;

	//reset fds reader and timeout struct.
	FD_ZERO(&rfds);
	FD_SET(TCP_Sock, &rfds);
	tv.tv_sec = 0; // set timeouts -wait for 0.1 sec for the WELCOME MSG
	tv.tv_usec = 100000;

	if(send(TCP_Sock,hello_msg,sizeof(hello_msg),0) == -1)
	{ perror(""); printf("\nError in sending hello message"); return welc_msg;} //returning empty struct

	retval = select(TCP_Sock+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
	if (retval == -1) { perror("select() failed in handshake"); state = OFF_INIT; }
	else if(FD_ISSET(TCP_Sock, &rfds) && retval) //data ready from STDIN, while not in upload.
		state = WAIT_WELCOME;
	else if(retval ==0) //timeout reached.
	{
		printf("\nWELCOME msg not received within 0.2 seconds. quitting program.\n");
		state = OFF_INIT;
		close (TCP_Sock);
		exit(ERROR);
	}

	rec_bytes = recv(TCP_Sock, welc_buff, sizeof(welc_buff) , 0); //receive welcome msg
	if (rec_bytes == -1)
	{ perror(""); printf("\nError in sending hello message"); return welc_msg; state = OFF_INIT;}
	else if (rec_bytes != 9) // expected welcome msg size
	{ printf("\nError in welcome message - size incorrect"); return welc_msg; state = OFF_INIT;}

	// rec_bytes == 9
	replyType = (uint8_t)welc_buff[0];
	if (replyType == 0)
	{
		NumStations = ntohs(((uint16_t*)(welc_buff+1))[0]); //SET NUM OF RADIO STATIONS.
		sprintf(welc_msg.mGroup_IP,"%d.%d.%d.%d",welc_buff[6],welc_buff[5],welc_buff[4],welc_buff[3]);	// get multicast address from buffer
		inet_aton(welc_msg.mGroup_IP, &reference_mCast_addr); //put ip string in welc_msg.mGroup_IP
		current_mCast_addr = reference_mCast_addr; //same Mcast addr at first
		welc_msg.mGroup_port = ntohs(((uint16_t*)(welc_buff+7))[0]); //get port num
		printf("\n********Welcome msg received!******** ");
		state = ESTABLISHED;
	}
	else
	{ printf("\nreceived unexpected msg (NOT A WELCOME MSG). quitting program.\n"); return welc_msg; state = OFF_INIT;}
	return welc_msg;
}

void* listener(void* UDP)
{
	UDP_sock_data udp_data = *(UDP_sock_data*)UDP; //get struct sent to thread.
	struct ip_mreq mreq; //mCast request
	int UDP_sock,UDP_pack_len =1, reuse = 1;
	struct sockaddr_in udp_serverAddr = {0};
	char  message[Buffer_size];
	FILE * fp; //to throw audio to
	socklen_t addr_len;
	struct timeval timeout;

	UDP_sock = socket(AF_INET, SOCK_DGRAM, 0); //create udp sock
	if (UDP_sock == -1) { perror("Can't create UDP socket"); close(UDP_sock); pthread_exit(&t); exit(ERROR); }

	//define the port and the multicast IP to send the data on
	udp_serverAddr.sin_family = AF_INET;
	udp_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	udp_serverAddr.sin_port = htons(udp_data.mGroup_port);
	addr_len = sizeof udp_serverAddr;

	setsockopt(UDP_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)); //reuse udp sock.
	//try to bind to the the UDP socket of the multicast Group
	if(bind(UDP_sock, (struct sockaddr *) &udp_serverAddr, sizeof(udp_serverAddr))==-1)
	{ perror("Can't bind UDP socket"); close(UDP_sock); pthread_exit(&t); exit(ERROR); }

	mreq.imr_multiaddr.s_addr = inet_addr(inet_ntoa(reference_mCast_addr));
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup

	fp = popen("play -t mp3 -> /dev/null 2>&1", "w");//open a pipe. output is sent to dev/null (hell).

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	while(state != OFF_INIT) // While (not all data received || connection closed by server)
	{
		if(change_station)
		{
			setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP
			current_mCast_addr.s_addr = (uint32_t)ntohl((htonl((reference_mCast_addr.s_addr))) + current_station); //update mGroup IP
			mreq.imr_multiaddr.s_addr = inet_addr(inet_ntoa(current_mCast_addr));
			setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup
			change_station = 0; //now changed.
		}
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		if (setsockopt (UDP_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		{ perror("setsockopt failed\n"); state = OFF_INIT; break; }
		UDP_pack_len = recvfrom(UDP_sock, message, sizeof(message), 0, (struct sockaddr *) &udp_serverAddr, &addr_len); //receive song
		if(UDP_pack_len > 0)
			fwrite (message , sizeof(char), Buffer_size, fp); //write a buffer of size Buffer_size into fp >> play  the song!!
		if (UDP_pack_len < 0)
		{ perror("Error in reading from UDP socket"); printf("\n"); }
		else if (UDP_pack_len == 0)// - Connection closed by server
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~UDP Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			state = OFF_INIT;
		}
	}

	//state == OFF_INIT - finalize connection
	setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP
	printf("closing streaming communication! Bye bye!\n");

	// Free all resources; close the connection
	if(close(UDP_sock) == -1 )
	{ perror("Error in closing client UDP socket"); exit(ERROR); }
	pclose(fp);
	pthread_exit(&t); //terminate thread.
}

void handle_TCP_and_IO()
{
	int retval=0,i=0;

	fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit, then press Enter.\n", NumStations-1); //menu.
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
				gets(user_input); //get user input.
				handle_user_input(TCP_Sock);
				for(i=0;i<max_uInput_len;i++) //clean user input buffer for next time
					user_input [i]=0;
			}
			if (FD_ISSET(TCP_Sock, &rfds) && (state != WAIT_NEW_STATIONS)) //data ready from TCP socket, while not waiting for newstations msg
			{
				if(msg_sent == 1)
					msg_sent = 0; //got response before timeout.
				handle_TCP_message(TCP_Sock);
				if (state != OFF_INIT) //still online
					fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit, then press Enter.\n", NumStations-1);
			}
		}
		if(retval==0 && msg_sent == 1) //timeout reached when sending message, response not on time. quit.
		{
			printf ("no response received from server for 100ms. quitting program.\n");
			state = OFF_INIT;
		}
	}
	if (state == OFF_INIT) //quitting program
		return;
}

int handle_user_input()
{
	int Song_name_size,song_size,i=1;
	char askSong_msg [3] = {1,0,0};
	char* upSong_msg;
	FILE * songP; //song file pointer
	DIR *d; //directory pointer
	struct dirent *dir; // dirent * directory pointer

	if(strcmp(user_input, "s")==0 || strcmp(user_input, "S")==0) //upload procedure
	{
		printf("please enter song name to upload (up to 100 characters, with suffix '.mp3').\n");
		printf("upload options:");
		d = opendir(".");
		if (d)
		{
			while ((dir = readdir(d)) != NULL)
			{
				if(strstr(dir->d_name, ".mp3") != NULL) //file name contains ".mp3"
				{
					printf(" (%d) %s", i, dir->d_name); //present the songs possible to upload from current dir.
					i++;
				}
			}
			if(i>1)
				printf(". enter your choice.\n");
			else
				printf("none.");
			closedir(d);
		}
		fflush(stdin);
		gets(user_input); //get user input

		songP = fopen(user_input,"r");
		if (songP != NULL)  //check entered file
		{
			Song_name_size = (unsigned short)strlen(user_input);
			fseek(songP, 0L, SEEK_END);
			song_size = ftell(songP); //file's size in bytes
			rewind(songP); // return the pointer back to start of file
			if(song_size > MIN_SONG_SIZE && song_size < MAX_SONG_SIZE) // good size
			{
				printf("file size to upload is: %d bytes.\n",song_size); //only for us to be sure. todo
				upSong_msg = (char*)calloc(1,(Song_name_size+6)*sizeof(char)); //+6 for command type, song size, and song name size.
				//fill upSong buffer
				upSong_msg[0] = (uint8_t)2; //command type = 0
				*(uint32_t*)&upSong_msg[1] = htonl(song_size); // host to network order, as requested.
				upSong_msg[5] = (uint8_t)Song_name_size;
				for (i=6; i<Song_name_size+6; i++) //fill song name.
					upSong_msg[i] = user_input[i-6];

				if(send(TCP_Sock,upSong_msg,Song_name_size+6,0) == -1)
				{ perror(""); printf("\nError in sending upSong message");}
				else
					msg_sent = 1; //set flag to wait for timeout.
				free(upSong_msg);
				state = WAIT_PERMIT;

				//remember which song to upload.
				song_file = (char*)calloc(1,Song_name_size*sizeof(char)); //+6 for command type, song size, and song name size.
				for (i=0; i<Song_name_size; i++)
					song_file[i] = user_input[i];

				fileSize = song_size/Buffer_size +1; //calc how many iterations to send full song.
				remeinder = song_size-(fileSize-1)*Buffer_size; // (doesn't send last part if remeinder == 0)
				fclose(songP);
				return 1;
			}
			else //improper file
			{
				printf("file size is: %d  the file has to be more then %d bytes or less then %d bytes",song_size,MIN_SONG_SIZE,MAX_SONG_SIZE);
				fclose(songP);
				songP = NULL;
			}
		}
		else //song doesn't exist or not readable.
		{
			perror("file doesn't exist, or is not readable. try again");
			fprintf(stdout,"\nplease enter 0-%d to change station, 's' to upload a song, or 'q' to quit, then press Enter.\n", NumStations-1);
			return SUCCESS; //func still successful
		}
	}
	else if(strcmp(user_input,"q")==0 || strcmp(user_input ,"Q")==0) //user wants to quit.
	{
		state = OFF_INIT;
		return SUCCESS;
	}
	else if ((uint16_t)atoi(user_input) >= 0 && (uint16_t)atoi(user_input) < max_num_stations) //&& (uint8_t)atoi(user_input)<NumStations)//entered a number
	{
		if((uint16_t)atoi(user_input) == 0 && strcmp(user_input, "0") != 0) // not zero! input is LETTERS. invalid input!!
			printf("bad command. please try again.\n");
		else if ((uint16_t)atoi(user_input) < NumStations)//entered a good number
		{
			*(uint16_t*)&askSong_msg[1] = htons(atoi(user_input)); //insert requested station number to upSong msg
			current_station = (int)atoi(user_input);
			if(send(TCP_Sock,askSong_msg,sizeof(askSong_msg),0) == -1)
			{ perror(""); printf("\nError in sending askSong message");}
			else
				msg_sent = 1; //flag to check for timeout
			state = WAIT_ANNOUNCE;
		}
		else
			printf("station %d doesn't exist. please try again.\n",(uint16_t)atoi(user_input));
	}
	return SUCCESS;
}

int handle_TCP_message()
{
	int TCP_pack_len=0, arr_len=0,i;
	char *arr; //for invalid msg or announce msg
	uint8_t replyType = 0,permit=0;
	char buffer[Buffer_size];

	TCP_pack_len = recv(TCP_Sock, buffer, Buffer_size , 0); // Read socket data into buffer if there is Any data
	if(TCP_pack_len > 0) // msg received
	{
		replyType = (uint8_t)buffer[0];
		switch(replyType)
		{
		case 0: //Welcome msg - invalid at this stage
			printf("WELCOME msg received- invalid at this stage. exiting program.\n");
			state = OFF_INIT;
			return ERROR;

		case 1: //announce msg
			if(TCP_pack_len >=2 && state == WAIT_ANNOUNCE) //all good
			{
				printf("********announce msg received!!********");
				arr_len = (uint8_t)buffer[1]; //length of song received
				arr = (char*)calloc(arr_len, sizeof(char));
				for(i=0;i<arr_len;i++) //fill song name to arr.
					arr[i] = buffer[i+2];
				printf(" changed station's song name: %s.",arr); //present new song
				change_station = 1; //change staion in listener thread
				state = ESTABLISHED;
				free(arr);
				return SUCCESS;
			}
			else { state = OFF_INIT; return ERROR; } //quit

		case 2: //PermitSong msg
			if(TCP_pack_len == 2 && state == WAIT_PERMIT) //all good
			{
				printf("********PermitSong msg received!!******** ");
				permit = (uint8_t)buffer[1];
				if(permit == 1)
				{
					state = UPLOAD_SONG;
					printf("permitted to upload!! uploading.. (no I/O currently possible).\n");
					uploading = 1; //flag not to disable IO
					if(uploadSong(TCP_Sock) == 1) //uploaded successfully
					{
						uploading = 0; //finished upload. IO availible again
						state = ESTABLISHED;
						fflush(stdin); //flush user input, if there was any.
					}
					else
					{
						printf("error in uploadSong() function.\n");
						state = OFF_INIT;
						fflush(stdin); //flush user input, if there was any.
					}
				}
				else if(permit == 0)
				{
					printf("not permitted to upload =( try a different song.\n");
					return SUCCESS;
				}
			}
			else return ERROR;
			break;

		case 3: //Invalid_command msg
			if(TCP_pack_len >=2)
			{
				arr_len = (uint8_t)buffer[1];
				arr = (char*)calloc(arr_len, sizeof(char)); //alloc msg size
				for(i=0;i<arr_len;i++) //fill invalid msg to arr
					arr[i] = buffer[i+2];
				printf("********invalidCommand msg recieved********:\n ""%s"". quitting program.\n",arr);
				state = OFF_INIT;
				free(arr);
				return ERROR;
			}
			else { printf("invalid invalidCommand msg recieved. quitting program.\n"); state = OFF_INIT; return ERROR; }

		case 4: //NewStations msg
			if(TCP_pack_len == 3)
			{
				NumStations = ntohs(((uint16_t*)(buffer+1))[0]);
				printf("********newstations!!!******** we now offer %d stations!",NumStations);
				state = ESTABLISHED;
			}
			else { printf("invalid NewStations msg recieved. quitting program.\n"); state = OFF_INIT; return ERROR; }
			break;

		default : //	InvalidCommand or announce msg - check which one and handle accordingly
			printf("********bad message from server.******** quitting program =( \n");
			state = OFF_INIT;
			return ERROR;
		}
	}
	else if (TCP_pack_len < 0)
	{ perror("Error in reading from TCP socket"); printf("\n"); }
	else if (TCP_pack_len == 0)
	{
		printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~TCP Connection closed by server.~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		state = OFF_INIT;
		return ERROR;
	}
	return SUCCESS;
}

int uploadSong() //UPLOAD THE SONG!!!!!!!!!
{
	int retval=0,part_num=0,TCP_pack_len=0;
	float percent = 0;
	char  song_buff[kiB], buffer[Buffer_size];
	FILE * song;
	useconds_t usec_delay = upload_delay; //8000 usec
	uint8_t replyType = 0;
	struct timeval timeout;

	song = fopen(song_file,"r"); //try to read song file
	if(!song)  //can't open file
	{ perror("uploadSong - can't open song file. quitting program.\n"); free(song_file); state = OFF_INIT; return ERROR; }

	while (fscanf(song, "%1024c", song_buff) != EOF && state != OFF_INIT && part_num < fileSize)
	{
		printf("Song Uploading status: %.0f%% \r",percent); //show upload precentage
		fflush(stdout);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		if (setsockopt (TCP_Sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		{
			perror("setsockopt failed\n"); state = OFF_INIT;
			free(song_file);
			fclose(song);
			break;
		}

		if (part_num == fileSize - 1 && remeinder != 0) //UPLOAD LAST PART
		{
			TCP_pack_len = send(TCP_Sock,song_buff,remeinder,0);
			COUNT +=TCP_pack_len; //todo
			if(TCP_pack_len == -1)
			{ perror(""); printf("\nError in uploading last part"); state = OFF_INIT; }
			else if (TCP_pack_len == 0)
			{
				printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~TCP Connection closed by server.~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
				state = OFF_INIT;
				free(song_file);
				fclose(song);
				break;
			}
			else
			{
				printf("Song Uploading status: 100%%. ");
				printf("finished song uploading!!\n");
			}

		}
		else //upload all other parts.
		{
			TCP_pack_len = send(TCP_Sock,song_buff,Buffer_size,0);
			COUNT +=TCP_pack_len; //todo
			if(TCP_pack_len == -1)
			{ perror(""); printf("\nError in uploading song, TCP_pack_len == -1\n"); state = OFF_INIT; break;}
			else if (TCP_pack_len == 0)
			{
				printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~TCP Connection closed by server.~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
				state = OFF_INIT;
				free(song_file);
				fclose(song);
				break;
			}
			percent = (part_num++)*100/fileSize; //increment percentage
			usleep(usec_delay); //delay sending by 8000 usec
		}
	}
	fclose(song);
	printf("total bytes sent: %d\n", COUNT);
	if((fscanf(song, "%1024c", song_buff))==EOF && !server_is_idiot) //EOF reached. expecting newStations msg.
	{
		state = WAIT_NEW_STATIONS;
		FD_ZERO(&rfds);
		FD_SET(TCP_Sock, &rfds);
		tv.tv_sec = 2; //set timeout - wait for 2 sec for the new station massage
		tv.tv_usec = 0;

		retval = select(TCP_Sock+1, &rfds, NULL, NULL, &tv); // Watch stdin and tcp_sock to see when it has input.
		if (retval == -1) { perror("select() failed in upsong"); state = OFF_INIT; }
		else if(retval) //data ready.
		{
			if(FD_ISSET(TCP_Sock, &rfds)) //data ready from tcp socket. check if new stations msg.
			{
				TCP_pack_len = recv(TCP_Sock, buffer, Buffer_size , 0); // Read socket data into buffer if there is Any data
				if(TCP_pack_len > 0)
				{
					replyType = (uint8_t)buffer[0];
					if(replyType == 4 && TCP_pack_len == 3)  //NewStations msg
					{
						NumStations = ntohs(((uint16_t*)(buffer+1))[0]);
						printf("********NewStations!!!******** we now offer %d stations! I/O possible again.",NumStations);
						free(song_file);
						return SUCCESS;
					}
					else
					{ printf("bad message received from server! quitting program.\n"); state = OFF_INIT; }
				}
				else
					perror("bad TCP pack length");
			}
		}
		else if(retval == 0) //timeout reached.
		{
			printf("NewStations msg not received within 2 seconds. quitting program.\n");
			state = OFF_INIT;
			return ERROR;
		}
	}
	else if (server_is_idiot)
	{ free(song_file); return SUCCESS; }

	printf("\nError in uploading song, couldn't reach end of file."); //newstations wasn't received.
	return ERROR; //newstations wasn't received.
}
