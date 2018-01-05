#define CLIENT
#ifdef CLIENT
#include "Client_Control.h"

int main(int argc, char* argv[]) // Input: SERVER's IP_address/NAME, SERVER's TCP WELCOME port.

{
	int SERVER_TCP_port;
	char * SERVER_TCP_IP;
	sock_data sock_info;

	//Check that we got from the user all of the arguments to activate the server
	printf("input received: \nexe File:%s, Server IP address:%s,\nServer TCP_port num:%s,\n(total of %d args)\n", argv[0], argv[1], argv[2] , argc);
	if(argc != 3) { printf("Too few/many arguments received. Bye bye!\n"); exit(1); }

	SERVER_TCP_IP = argv[1];
	SERVER_TCP_port = atoi(argv[2]);

	//sock_info = setTCP_sock(SERVER_TCP_IP,SERVER_TCP_port);

	if(pthread_create(&t, NULL, listener, &sock_info)!=0) //create listener thread
		{printf("\n\nerror in thread. exiting."); exit(1);}

	while(1){}
	//handle_TCP_and_IO(sock_info->tcp_sock);

	//close the connection
	/*if(close(*sock_info->tcp_sock) == -1 )
	{ perror("Error in closing client socket"); exit(1); }*/
	free(mGroup_IP);
	pthread_exit(NULL);
	return EXIT_SUCCESS;
}

Welcome_msg handShake(int TCP_sock)
{
	Welcome_msg welcome_msg = {0};
	int rec_bytes = 0;
	unsigned char hello_msg [3] = {0,0,0} , welc_buff [9] ;

	if(send(TCP_sock,hello_msg,sizeof(hello_msg),0) == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}

	state = WAIT_WELCOME;

	rec_bytes = recv(TCP_sock, welc_buff, sizeof(welc_buff) , 0);
	if (rec_bytes == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}
	else if (rec_bytes != 9)
	{ printf("\nError in welcome message - size incorrect"); return welcome_msg;}

	welcome_msg.replyType = (uint8_t)welc_buff[0];
	welcome_msg.numStations = ntohs(((uint16_t*)(welc_buff+1))[0]);
	sprintf(welcome_msg.multicastGroup,"%d.%d.%d.%d",welc_buff[6], welc_buff[5], welc_buff[4], welc_buff[3]);	// get multicast address from buffer
	welcome_msg.portNumber = ntohs(((uint16_t*)(welc_buff+7))[0]);

	state = ESTABLISHED;
	return welcome_msg;
}

void* listener(void* UDP_sock_data)
{
	sock_data my_data = *(sock_data*)UDP_sock_data;
	struct ip_mreq mreq;
	int UDP_sock,UDP_pack_len =1,UDP_read_err=0, reuse = 1;
	useconds_t delay = 100000;
	struct sockaddr_in udp_serverAddr = {0};
	char  message[Buffer_size];
	FILE * fp;
	socklen_t addr_len;

	UDP_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (UDP_sock == -1) { perror("Can't create UDP socket"); close(UDP_sock); exit(1); }

	//define the port and the multicast IP to send the data on
	udp_serverAddr.sin_family = AF_INET;
	udp_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	udp_serverAddr.sin_port = htons(my_data.mGroup_port);
	addr_len = sizeof udp_serverAddr;

	setsockopt(UDP_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	//try to bind to the the UDP socket of the multicast Group
	if(bind(UDP_sock, (struct sockaddr *) &udp_serverAddr, sizeof(udp_serverAddr))==-1)
	{ perror("Can't bind UDP socket"); close(UDP_sock); exit(1); }

	mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.0"); //TODO
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup

	fp = popen("play -t mp3 -> /dev/null 2>&1", "w");//open a pipe. output is sent to dev/null (hell).
	printf("\nstarting music player..\n");

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	while(UDP_pack_len!=0) // While (not all data received || connection closed by server)
	{
		printf("got here1!");
		UDP_pack_len = recvfrom(UDP_sock, message, sizeof(message), 0, (struct sockaddr *) &udp_serverAddr, &addr_len);
		printf("got here2!");
		if(UDP_pack_len > 0)
		{
			fwrite (message , sizeof(char), Buffer_size, fp); //write a buffer of size Buffer_size into fp
			usleep(delay);
		}
		else if (UDP_pack_len < 0)
		{
			perror("Error in reading from socket");
			printf("\n");
			UDP_read_err++;
		}
		else //if (pack_len == 0) - Connection closed by server
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			break;
		}
		/*if(change_station)
		{
			setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP
			mreq.imr_multiaddr.s_addr = inet_addr(mGroup_IP);
			setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup
		}*/
	}

	setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP

	// print what we received in the buffer and statistics.
	printf("\nread errors: %d\n",UDP_read_err);

	// Free all resources;
	close(UDP_sock);
	return NULL;
}

int handle_user_input(char* user_input, int TCP_Sock)
{
	int TCP_pack_len=0;
	char buff[Buffer_size];
	uint8_t replyType = 0,permit=0;

	if(strcmp(user_input, "s\n") || strcmp(user_input, "S\n")) //upload procedure
	{

		printf(" please enter your desired song's name.\n");
		gets(user_input);

		//check if file uploadable!!!!!!!!!!!!!!!!! --TODO
		//UPLOAD SEND THE SONG!!!!!!!!! --TODO
		state = WAIT_PERMIT;

		TCP_pack_len = recv(TCP_Sock, buff, Buffer_size , 0); // Read socket data into buffer if there is Any data
		if(TCP_pack_len == 2 && replyType == 2)  //PermitSong msg
		{
			if((uint8_t)buff[1] == 1) //permitted to upload
			{
				//start uploading song --TODO
			}
			else if ((uint8_t)buff[1] == 0) //not permitted to upload)
			{

			}
		}

	}
	if(strcmp(user_input,"q\n") || strcmp(user_input ,"Q\n"))
	{

	}
	return 1;
}

sock_data setTCP_sock (char* serv_ip, int serv_port)
{
	int TCP_Sock=0;
	struct sockaddr_in serverAddr;
	sock_data sock_info;
	Welcome_msg welcome_msg = {0};
	// Create the TCP&UDP socket and set it’s properties. The three arguments are: 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case).
	TCP_Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (TCP_Sock == -1)  { perror("Can't create TCP socket"); close(TCP_Sock); exit(1); }

	/*---- Configure settings of the server address struct ----*/
	serverAddr.sin_family = AF_INET; /* Address family = Internet */
	serverAddr.sin_port = htons(serv_port); 	/* Set destination TCP_port number, using htons function to use proper byte order */
	serverAddr.sin_addr.s_addr = inet_addr(serv_ip); // set destination IP number - localhost, 127.0.0.1
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); /* Set all bits of the padding field to 0 */

	/*---- Connect the socket to the server using the address struct ----*/
	if(connect(TCP_Sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))== -1)
	{ perror("Can't connect to server"); close(TCP_Sock); exit(1); }

	welcome_msg = handShake(TCP_Sock);
	if(welcome_msg.numStations == 0 && welcome_msg.multicastGroup==0 && welcome_msg.portNumber==0) //no real data received
		printf("error in welcome message! quitting program.");
	else
		printf("\nconnection established. data received from server:\n%d stations, mGroup IP: %s, mGroup port: %d. playing"
				" 1st station.\nto change station enter: 0-%d, and then hit enter.",welcome_msg.numStations
				,welcome_msg.multicastGroup,welcome_msg.portNumber,welcome_msg.numStations);

	mGroup_IP = (char *)calloc(1,sizeof(welcome_msg.multicastGroup));
	mGroup_IP = strcpy(mGroup_IP,welcome_msg.multicastGroup);
	sock_info.mGroup_IP = welcome_msg.multicastGroup;
	sock_info.mGroup_port = welcome_msg.portNumber;
	sock_info.tcp_sock = TCP_Sock;

	return sock_info;
}

void handle_TCP_and_IO(int TCP_Sock)
{
	int connection_closed=0,TCP_read_err=0,retval=0, TCP_pack_len=0, arr_len=0,invalid=0;
	char *arr, *user_input;
	char  buffer[Buffer_size];
	fd_set rfds;
	struct timeval tv;
	uint8_t replyType = 0,permit=0;
	uint16_t numStations=0,portNumber=0,newStationNumber=0;
	uint32_t songSize = 0, multicastGroup=0;

	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds); //rfds[0] = STDIN_FILENO (0).
	FD_SET(TCP_Sock, &rfds); //rfds[1] = TCP file descriptor number.
	tv.tv_sec = 0.1; 	// Wait up to 0.1 seconds.
	tv.tv_usec = 0;

	while(!connection_closed && !invalid)
	{
		retval = select(1, &rfds, NULL, NULL, &tv); /* Watch stdin and tcp_sock to see when it has input. */
		if (retval == -1)
			perror("select()");


		if(FD_ISSET(0, &rfds)) //data ready from STDIN
		{
			gets(user_input);
			handle_user_input(user_input , TCP_Sock);  //TODO
			printf("waiting for user input! (chose a station in the range: 0-%d), and then hit enter", NumStations);
		}


		else if (FD_ISSET(1, &rfds)) //data ready from TCP socket
		{
			TCP_pack_len = recv(TCP_Sock, buffer, Buffer_size , 0); // Read socket data into buffer if there is Any data
			if(TCP_pack_len > 0)
			{
				replyType = (uint8_t)buffer[0];
				switch(replyType)
				{
				case 0: //Welcome msg - invalid at this stage
					if(TCP_pack_len == 9)
					{
						invalid =1;
						sendinvalid();
					}
					break;

				case 1: //announce msg
					arr_len = (int)buffer[1];
					arr = (char*)calloc(arr_len, sizeof(char));
					break;

				case 2: //PermitSong msg
					if(TCP_pack_len == 2)
						permit = (uint8_t)buffer[1];
					if(permit ==1)
						state = UPLOAD_SONG;
					break;

				case 3: //Invalid_command msg
					arr_len = (int)buffer[1];
					arr = (char*)calloc(arr_len, sizeof(char));
					break;

				case 4: //NewStations msg
					if(TCP_pack_len == 3)
						newStationNumber = (uint16_t)buffer[1];
					break;

				default : //	InvalidCommand or announce msg - check which one and handle accordingly
					printf("bad message from server. quitting program =(");
					break;
				}
			}
			else if (TCP_pack_len < 0)
			{
				perror("Error in reading from socket"); printf("\n");
				TCP_read_err++;
			}
			else
			{
				printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection clo"
						"sed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
				connection_closed =1;
				break;
			}
		}
	}
	free(arr);
}

void sendinvalid()
{
	//TODO
}
#endif
