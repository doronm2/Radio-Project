/*
 ============================================================================
 Name        : radio_control.c
 Author      : Chen Bary 302775333, Stas Karpanko 309427680
 ============================================================================
 */

#include <netinet/in.h>		//internet protocol family stuff
#include <arpa/inet.h>		//definitions for internet operations
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

#define BUFFER 1024
#define maxIPlen 15
#define LOOP_BACK_IP "127.0.0.1"
#define LOOP_BACK_PORT 12701

void invalidQuit(unsigned char bufTCP[], int fdServer, int fdClient);
int selectSet(int type, int sec, int usec);
int itIsNumber(unsigned char arr[], int size);
void error(char *msg);

struct sockaddr_in addrClient, addrServer; 	   // socket struct for TCP
struct timeval tv;
fd_set readfds;          					   // group for select function
int fdClient, fdServer, flagFD = 0;  //flag = 1 server socket open, flag = 2 server and client sockets are open, flag = 0 both sockets are close

int main(int argc, char *argv[]) {
	uint8_t  welcomCheak, songNameSize, AskList = 2,listDataSize;
	uint16_t numStations;
	int n,rv, nbytes, i=0, flag = 1, input, inputNum, loop, numUdpPort;
	unsigned char bufTCP[BUFFER]={'\0'}, ipAndPort[7]={'\0'}, userIput[BUFFER] = {'\0'}, temp;  // buffer for UDP & TCP

	if(argc != 3)
		error("Usage: ./radio_control <servername> <serverport>\n");

	flagFD = 1;
	bzero((char *)&addrServer, sizeof(addrServer));
	addrServer.sin_family = AF_INET;
	addrServer.sin_addr.s_addr = inet_addr(argv[1]);
	addrServer.sin_port = htons(atoi(argv[2]));

	//*********************** start hello to server ***********************//
	// Create a new socket - pointed to file descriptor Server
	if ((fdServer = socket(AF_INET,SOCK_STREAM,0)) < 0) 	// open TCP socket
		error("Server socket creation error\n");
	if(connect(fdServer,(struct sockaddr *) &addrServer,sizeof(addrServer)) < 0)
		error("Connection server error\n");
	if(send(fdServer,bufTCP, 3,0)<0)
		error("send to Server error\n");

	n = selectSet(0,0, 100000);
	if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1) //wait for wellcom
		error("select error\n");
	if(rv == 0)
		error("Server timeout error\n");

	if(FD_ISSET(fdServer, &readfds)){
		if((nbytes = recv(fdServer,bufTCP,sizeof(bufTCP),0)) < 0)	// listen on TCP port for first command
			error("error receiving from Server command\n");
		welcomCheak = bufTCP[0];
		if(welcomCheak != 0)
			error("invalid welcome reply\n");
	}
	numStations = bufTCP[1]*256 + bufTCP[2];         //just to print
	for(i=1 ; i<7  ; i++)           //copy the ip and port
		ipAndPort[i] = bufTCP[i+2]; //first 4 for ip, last 2 for port number
	memset(bufTCP,0,nbytes);	    // clear tcp buffer
	//*********************** end hello + wellcom to server ***********************//

	//*********************** start hello to client ***********************//
	// Create a new socket - pointed to file descriptor Server
	if ((fdClient = socket(AF_INET,SOCK_STREAM,0)) < 0) 	// open TCP socket
		error("Client socket creation error\n");
	flagFD = 2;
	bzero((char *)&addrClient, sizeof(addrClient));
	addrClient.sin_family = AF_INET;
	addrClient.sin_port = htons(LOOP_BACK_PORT);
	inet_aton(LOOP_BACK_IP, &addrClient.sin_addr);	  // define IP address to loopback interface

	if(connect(fdClient,(struct sockaddr *) &addrClient,sizeof(addrClient)) < 0)
		error("Error connection to the listener\n");
	flag = 1;
	//*********************** end hello + wellcom to client ***********************//
	numUdpPort = (int)(ipAndPort[5]*256)+(int)ipAndPort[6];
	printf("Type in 'q' or press CTRL+C to quit.\n>\nMC group is : %u.%u.%u.%u",ipAndPort[1],ipAndPort[2],ipAndPort[3],ipAndPort[4]);
	printf("\nThere are %u stations\n", numStations);
	printf("The m.c port to listen to is %d\n", numUdpPort);

	while(flag == 1){
		printf(">");
		do{
		n = selectSet(2 , 100, 100000);
		bzero(userIput,BUFFER);
		if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1) //wait for wellcom
			error("select error");
		}while(rv == 0);//time out

		if(FD_ISSET(fileno(stdin), &readfds)){ //check if there is input from the user
			i=0;
			bzero(userIput, BUFFER); //reset buffer
			do{						 //get the input from the user
				temp = getchar();
				if(temp != '\n'){
					userIput[i] = temp;
					i++;
				}
			}while(temp != '\n'); //if the user pressed q or l
			if(i == 1 && (userIput[0] == 'q' || userIput[0] == 'l'))
				input = userIput[0];
			else{				//else check if this is legal number
				inputNum = itIsNumber(userIput, i);
				input = 'T';    //Troll input - third case, the user pressed some number
				if(inputNum == -1 || inputNum >= numStations)
					continue;
			}//else
		} //if(stdin)

		//******************check the input************************//
		switch(input){
		case 'q':    //exit from program
			flag = 0;
			ipAndPort[0] = 3;
			if(send(fdClient,&ipAndPort, 7,0)<0)
				error("send good bye to Client error\n");
			printf("shut down\n");
			break;

		case 'l':   //ask for "listener list" from the server
			n = selectSet(0, 0, 100000);
			if(write(fdServer, &AskList, sizeof(AskList))<0) //send the request
				error("send l to Server error\n");
			if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1) //wait 100ms or to message from the server
				error("select error\n"); //select error
			if(rv == 0)
				error("the server disconnected\n");

			memset(bufTCP,0,nbytes);			// clear tcp buffer
			if(FD_ISSET(fdServer, &readfds)){	// if it was input from the server
				if((nbytes = recv(fdServer,bufTCP,sizeof(bufTCP),0))<0)	// read from the buffer
					error("error receiving from Server command");

				if(bufTCP[0] == 3)	 //get bad message from the server
					invalidQuit(bufTCP, fdServer, fdClient);
				if(bufTCP[0] == 2){  //good reply from server, good ACK
					listDataSize = bufTCP[1];
					loop = ((int)listDataSize);
					for(i=2; i<loop*8+2 ;i=i+8)
						printf("user ip %u.%u.%u.%u, port number %d, station number %d\n",
								bufTCP[i], bufTCP[i+1], bufTCP[i+2], bufTCP[i+3],
								bufTCP[i+4]*256+bufTCP[i+5], bufTCP[i+6]*256+bufTCP[i+7]);
				}
				else
					error("bad reply from the server\n");
			}
			break;

		default:  	//connect listener to inputNum list song
			memset(bufTCP,0,nbytes);	// clear tcp buffer
			bufTCP[0] = 1;
			bufTCP[1] = inputNum/256;    //low
			bufTCP[2] = inputNum;        //high

			n = selectSet(0, 0, 100000); //init select function
			if(send(fdServer,&bufTCP, 3,0)<0)
				error("send function error at askSong to Server\n");
			if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1) //wait for wellcom
				error("select error\n");				//select error
			if(rv == 0)
				error("The controller disconnect from the server\n");

			if(FD_ISSET(fdServer, &readfds)){ // if it was input from the server
				if((nbytes = recv(fdServer,bufTCP,sizeof(bufTCP),0))<0)	// read from the buffer
					error("error receiving from Server command\n");
				if(bufTCP[0] == 3)   //get bad message from the server
					invalidQuit(bufTCP, fdServer, fdClient);
				if(bufTCP[0] == 1){  //good reply from server, good ACK
					songNameSize = bufTCP[1];
					printf("now playing: ");
					for(i=2 ; i<(songNameSize+2) ; i++)
						printf("%c", bufTCP[i]);
					printf("\n");

 					n = selectSet(1, 0, 1000000); //init select function
					memset(bufTCP,0,nbytes);	// clear tcp buffer

					ipAndPort[4] = ipAndPort[4] + (unsigned char)inputNum; //low part
					if(send(fdClient,&ipAndPort, 7,0)<0)
						error("send station info to Client error");
					ipAndPort[4] = ipAndPort[4] - (unsigned char)inputNum; //low part

					if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1) //wait for client listener response
						error("select error\n");
					if(rv == 0)
						error("client listener disconnect from the controller\n");;

					if(FD_ISSET(fdClient, &readfds)){
						if((nbytes = recv(fdClient,bufTCP,sizeof(bufTCP),0))<0)	// read the buffer
							error("error receiving from Client command");

						if(bufTCP[0] != 1)  //good reply from server, good ACK
							error("bad ack from client listener\n");
					}
					else
						error("client listener timeout\n");
					memset(bufTCP,0,nbytes);	// clear tcp buffer
				}
			}
		}//switch

	}//while(1)
	close(fdClient); //need to tell him to shut down
	close(fdServer);
	return 1;
}//main

void error(char *msg){
	printf("%s",msg);
	if(flagFD > 0)
		close(fdServer);
	if(flagFD == 2)
		close(fdClient);
	exit(EXIT_FAILURE);
}

int itIsNumber(unsigned char arr[], int size){
	int ans = 0, i=0;
	while(i<size){
		if(arr[i]<'0' || arr[i]>'9')
			return -1;
		ans = ans*10 + arr[i]-'0';
		arr[i] = arr[i]-'0';
		i++;
	}
	return ans;
}

int selectSet(int type, int sec, int usec){
	tv.tv_sec = sec;
	tv.tv_usec = usec;   //100 ms
	FD_ZERO(&readfds);
	if(type == 0){
		FD_SET(fdServer, &readfds);
		return fdServer+1;
	}
	else if(type == 1){
		FD_SET(fdClient, &readfds);
		return fdClient+1;
	}
	else{
		FD_SET(fileno(stdin), &readfds);
		return fdServer+1;
	}
}

/*
 * This function print error message and close FD if it necessary
 */
void invalidQuit(unsigned char bufTCP[], int fdServer, int fdClient){
	uint8_t size = bufTCP[1];
	int i = 2;
	for(; i<size+2 ;i++)
		printf("%c",bufTCP[1]);
	close(fdServer);
	close(fdClient);
}
