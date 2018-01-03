/*
 ============================================================================
 Name        : radio_server.c
 Author      : Chen Bary 302775333, Stas Karpanko 309427680
 ============================================================================
 */

#include <signal.h>
#include <pthread.h>
#include <netdb.h>
#include <netinet/in.h>		//internet protocol family stuff
#include <arpa/inet.h>		//definitions for internet operations
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define BUFFER 1024

typedef struct socketTCPtList{
	int fdT, invalidMsgType, stationNum;
	struct socketTCPtList *next;
	struct sockaddr_in addrTCP;
	pthread_t TCPthread;
}socketTCPtList;

volatile sig_atomic_t sig_atomic_runTime = 1;
volatile sig_atomic_t sig_atomic_listTCP = 0;
volatile sig_atomic_t sig_atomic_g_songsNames = 0;

int numOfSongs, *fdU, fd_Listen, multicastIpInt[4];	  //useful values
char **songsName;
unsigned char  multicastIpChar[16] = {0};
uint16_t TCP_port, UDP_port;
FILE **fw;											   //read data from mp3 files
pthread_t stationsUDPthread, TCP_lisiner;
struct ip_mreq *mreq;
struct sockaddr_in adrrRead, *localStation, listenTCP; //sockets the run parallel
socketTCPtList *listTCP = NULL;

//**************************   functoions  ********************************//
void *quitFromServer(int type);
void *initRadio(void *numStations);      //init radio value
void *deleteListNode(socketTCPtList *clientNode);
void *TCPlisiner(); 						   //welcome TCP connection
void *TCPcommunicateWithClient(socketTCPtList *client);  //start communicate with client
int handshake(socketTCPtList *client);
void *invalidMessage(char *msg, int msgSize, socketTCPtList *client);
void *keybord();
//*************************************************************************//

int main(int argc, char *argv[]) {
	int  i=0, j=0;
	if(argc < 5){
		printf("./radio_server <tcp port> <mc group> <udp port> <file1> <file2> <file3>\n");
		exit(EXIT_SUCCESS);
	}
	numOfSongs = argc-4;
	TCP_port = atoi(argv[1]); //copy tcp port
	UDP_port = atoi(argv[3]); //copy udp port
	for(i=0, j=0 ; i<4 ; i++){//copy the ip to int array
		multicastIpInt[i] = 0;
		for(; j<(argv[3]-argv[2]-1) && argv[2][j]!='.' ; j++){
			multicastIpInt[i] = multicastIpInt[i]*10 + (int)argv[2][j]-48;
		}
		j++;
	}
	//******************** open TCP listener socket *************************//
	memset((char *) &listenTCP, 0, sizeof(listenTCP));	// clear listenTCP mem space
	listenTCP.sin_family = AF_INET;         //start of the world
	listenTCP.sin_port = htons(TCP_port);   //set the port the user gave
	listenTCP.sin_addr.s_addr = htonl(INADDR_ANY);

	if(((fd_Listen = socket(AF_INET, SOCK_STREAM,IPPROTO_TCP)))< 0){
		printf("error create listen communication socket\n");
		exit(EXIT_SUCCESS);
	}
	if(bind(fd_Listen,(struct sockaddr*)&listenTCP, sizeof(listenTCP))==-1){
		printf("error bind listen socket\n");
		close(fd_Listen);
		exit(EXIT_SUCCESS);
	}
	//*************************************************************************//

	//*************** allocate memory songs name and copy them ****************//
	if((songsName = (char**)malloc(numOfSongs*sizeof(char*)))==0){ //allocate songs name
		printf("error allocate memory to songsName matrix\n");
		close(fd_Listen);
		exit(EXIT_SUCCESS);
	}
	for(i=0 ; i<numOfSongs ; i++)
		if((songsName[i] = (char*)malloc(8*sizeof(char*)))==0){
			printf("error allocate memory to song's name\n");
			close(fd_Listen);
			exit(EXIT_SUCCESS);
		}
	for(i=0 ; i<numOfSongs ; i++)                   //copy song's name
		for(j=0 ; j<8 ; j++)
			songsName[i][j] = (char)argv[i+4][j]; //memcpy(des, src, size,0)

	printf("start server\n");				 //print songs and port
	for(i=0;i<numOfSongs;i++)
		printf("Station num %d MC group is : %s port is %d\n",i,songsName[i],UDP_port);
	//*************************************************************************//

	//create thread for broadcast radios initRadio
	if(pthread_create(&(stationsUDPthread), NULL, (void*)initRadio, &numOfSongs)!=0){
		printf("error creating UDP thread\n");
		close(fd_Listen);
		for(i=0 ; i<numOfSongs ; i++)
			free(songsName[i]);
		free(songsName);
		exit(EXIT_FAILURE);
	}

	//create thread for listening
	if((pthread_create(&(TCP_lisiner), NULL, (void*)TCPlisiner, &numOfSongs))!=0){
		printf("error creating TCP thread\n");
		pthread_cancel(stationsUDPthread);
		pthread_join(stationsUDPthread, NULL); //close UDP thread
		close(fd_Listen);
		for(i=0 ; i<numOfSongs ; i++)
			free(songsName[i]);
		free(songsName);
		exit(EXIT_FAILURE);
	}

	keybord();  //enter to input function to receive input from the user

	quitFromServer(0);
	return 1;
}

/*
 * this function that handle with the user's input
 */
void *keybord(){
	int flag = 0, runnTime=1, index;
	char input, temp;
	uint16_t port;
	socketTCPtList *runner;
	while(runnTime == 1){
		do{
			flag = 0;
			input = getchar();
			if(input != '\n'){
				temp = getchar();
				while(temp != '\n'){
					flag = 1;
					temp = getchar();
				}
			}
			else
				flag = 1;
		}while(flag == 1);

		switch(input){
		case 'q':    //exit from program
			sig_atomic_runTime = 0;
			runnTime = 0;
			quitFromServer(0);
			break;

			//print out a list of its stations along with the song each station is currently
			//playing and a list of clients that are connected to it via the control channel (tcp).
		case 'p':
			index=0;
			while(sig_atomic_listTCP == 1)
				usleep(100);
			sig_atomic_listTCP = 1;
			while(index<numOfSongs){
				runner = listTCP;
				printf("Station %u playing \"%s\", listening:\n", index, songsName[index]);
				while(runner != NULL){
					if(runner->stationNum == index){
						port = htons(runner->addrTCP.sin_port);
						printf("%s on port %d\n", inet_ntoa(runner->addrTCP.sin_addr), port);
					}
					runner = runner->next;
				}//while
				index++;
			}//while
			sig_atomic_listTCP = 0;
		}
	}//while
	return 0;
}

/*
 * This function listen to the line and receive clients. for each client - it open thread.
 */
void *TCPlisiner(){
	struct sockaddr_in client;
	socketTCPtList *newclient = NULL;
	int numSocket;
	unsigned int size = sizeof(client);

	if((listen(fd_Listen,20)) < 0){ //listen for incoming connections
		printf("error listen() to port\n");
		quitFromServer(0);
	}
	//start listen to the line and wait for new clients arrival
	while(sig_atomic_runTime == 1){
		if((numSocket=accept(fd_Listen, (struct sockaddr*)&client,&size)) < 0){
			printf("error accept client\n");
			quitFromServer(0);
		}
		if((newclient = (socketTCPtList*)malloc(sizeof(socketTCPtList))) == NULL){
			printf("error allocate memory to new client\n");
			quitFromServer(0);
		}
		newclient->fdT = numSocket;     //mark socket number
		newclient->addrTCP = client;
		printf("session id %d: new client connected; ip: %s port is: %d\n", numSocket,inet_ntoa(newclient->addrTCP.sin_addr), ntohs(newclient->addrTCP.sin_port));

		while(sig_atomic_listTCP == 1)  //wait for safety use, let other thread finish the use of listTCP
			usleep(100);
		sig_atomic_listTCP = 1;         //raise flag
		newclient->stationNum = -1;     //don't listen to any one
		newclient->next = listTCP;      //put the new client at the head of the list
		listTCP = newclient;            //update the head of the list's pointer
		sig_atomic_listTCP = 0;

		if((pthread_create(&(listTCP->TCPthread),NULL,(void*)TCPcommunicateWithClient, newclient))!=0){
			printf("error start TCP thread function (TCPconnection)\n");
			quitFromServer(0);
		}
	}
	return 0;
}

/*
 * Communicate with client.
 */
void *TCPcommunicateWithClient(socketTCPtList *client){
	int nbytes, counter, index, rv, n, temp;
	unsigned char bufferInput[BUFFER]={0}, bufferToSend[BUFFER]={0};
	uint16_t number;
	socketTCPtList *runner;
	fd_set readfds;          					   // group for select function
	struct timeval tv;         					   // timeout for select function

	//start hand shake protocol
	temp = handshake(client);
	if(temp == 0){  	//there is error in the system - timeout, receiving or sending.
		printf("session id %d: Error hand shake protocol with client on port %d\n", client->fdT, ntohs(client->addrTCP.sin_port));
		deleteListNode(client);
		if(pthread_detach(pthread_self()) < 0){//returning the calling thread's ID and than terminate it.
			printf("error terminate TCP client thread\n");
			quitFromServer(0);
		}
		return 0;
	}
	if(temp == -1)  //system error
		quitFromServer(0);

	while(sig_atomic_runTime == 1){ 	//start official communicate with client
		FD_ZERO(&readfds);				//clear select elements
		FD_SET(client->fdT, &readfds);	//add fdt to the elements
		tv.tv_sec = 3000000;
		tv.tv_usec = 0;
		n = client->fdT + 1;        //last socket that were open + 1

		if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1){
			printf("select error\n");
			quitFromServer(0);
			return 0;
		}
		else if(rv == 0) //timeout
			continue;

		if(FD_ISSET(client->fdT, &readfds)){ //recived data from client
			if((nbytes = recv(client->fdT, &bufferInput, sizeof(bufferInput), 0))<0){
				printf("error receiving command from client"); //client disconnect
				deleteListNode(client);
				pthread_detach(pthread_self());
				quitFromServer(0);
				return 0;
			}
			else if(nbytes == 0){
				printf("session id %d: Connection closed by client on port %d\n", client->fdT, ntohs(client->addrTCP.sin_port));
				deleteListNode(client);
				return 0;
			}
			//detect AskSong message (need to confirm by the check the two other given bytes) or AskList
			else if((nbytes==1 && bufferInput[0]==2) || (nbytes==3 && bufferInput[0]==1)){
				if(nbytes==1){ //AskList
					printf("session id %d: received Ask_List\n", client->fdT);
					while(sig_atomic_listTCP == 1)
						usleep(100);
					sig_atomic_listTCP = 1;
					runner = listTCP;
					counter = 0;
					index = 2;
					while(runner != NULL){
						if(runner->stationNum != -1){ // if client listen to any station
							memcpy(&bufferToSend[index], &runner->addrTCP.sin_addr.s_addr, sizeof(uint32_t));
							bufferToSend[index+4] = (runner->addrTCP.sin_port&(0xFF));
							bufferToSend[index+5] = (runner->addrTCP.sin_port&(0xFF00))>>8;
							bufferToSend[index+6] = (runner->stationNum&(0xFF00))>>8;
							bufferToSend[index+7] = (runner->stationNum&(0xFF));
							index = index+8;
							counter++;
						}
						runner = runner->next;
					}
					sig_atomic_listTCP = 0;
					bufferToSend[0] = 2;
					bufferToSend[1] = 0xFF&counter;
					send(client->fdT, bufferToSend, 2+counter*8, 0);
				}
				else{//Announce An reply is sent after a client sends a AskSong command about a station ID.
					number = bufferInput[1]*256 + bufferInput[2];
					if(number<numOfSongs){  //the station number not in range
						printf("session id %d: received Ask_Song to station %d\n", client->fdT,number);
						while(sig_atomic_g_songsNames == 1)
							usleep(100);
						sig_atomic_g_songsNames = 1;
						memcpy(&bufferToSend[2], &songsName[number][0], 8);
						sig_atomic_g_songsNames = 0;
						bufferToSend[0] = 1;
						bufferToSend[1] = 8;
						client->stationNum = number;
						send(client->fdT, bufferToSend, 10, 0);
					}
				}
			}
			else{//it was bad message
				printf("session id %d: received invalid command\n", client->fdT);
				invalidMessage("invalid input\n", 14, client);
				deleteListNode(client);
				return 0;
			}
		}//if(FD_ISSET(client->fdT, &readfds))
	}
	return 0;
}

/*
 * hand shake protocol with client
 * return 1 everything all right
 * return 0 if the hand shake fail need to disconnect the client
 * before return 0, the function call InvalidCommand() and that function send the correct message to the client.
 * rerun -1 if there is any system error, need to shut down the server
 */
int handshake(socketTCPtList *client){
	int rv, n = client->fdT+1, nbytes;
	unsigned char bufferInput[BUFFER]={0}, bufferToSend[BUFFER]={0};
	struct timeval tv;     // timeout for select function
	fd_set readfds;       // group for select function
	uint16_t numStations = numOfSongs, portNumber = UDP_port;

	//*********Welcome message******/
	bufferToSend[0] = 0;
	bufferToSend[1] = (numStations&(0xFF00))>>8;
	bufferToSend[2] = (numStations&(0xFF));
	bufferToSend[3] = multicastIpInt[0];
	bufferToSend[4] = multicastIpInt[1];
	bufferToSend[5] = multicastIpInt[2];
	bufferToSend[6] = multicastIpInt[3];
	bufferToSend[7] = (portNumber&(0xFF00))>>8;
	bufferToSend[8] = (portNumber&(0xFF));
	tv.tv_sec = 0;
	tv.tv_usec = 62500;

	FD_ZERO(&readfds);
	FD_SET(client->fdT, &readfds);
	if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1){  //select error
		printf("select error in hand shake\n");
		quitFromServer(0);
		return -1;
	}
	else if(rv == 0){//select timeout
		printf("session id %d: Handshake protocol with client has faild\n", client->fdT);
		return 0;
	}
	else{ //read client's message, see if it hello message
		nbytes = recv(client->fdT,bufferInput,sizeof(bufferInput),0);
		if(nbytes < 0){ // recv error
			printf("error receiving command\n");
			quitFromServer(0);
			return -1;
		}
		else if(nbytes == 0){ //If the client has closed the connection
			printf("session id %d: Handshake protocol with client has faild\n", client->fdT);
			return 0;
		}
		else{ //there is some data to read
			if(nbytes==3 && bufferInput[0]==0 && bufferInput[1]==0 && bufferInput[2]==0){ // recived hello message
				send(client->fdT, bufferToSend, 9, 0);
				printf("session id %d: HELLO received; sending WELCOMO\n", client->fdT);
			}
			else{
				printf("session id %d: invalid Hello message from client\n", client->fdT);
				invalidMessage("invalid Hello message from the client\n", 38, client);
				return 0;
			}
		}
	}
	return 1;  // hand shack ended without any problem
}

/*
 * send error message to client
 */
void *invalidMessage(char *msg, int msgSize, socketTCPtList *client){
	send(client->fdT, msg, msgSize, 0);
	return 0;
}

/*
 * find the wanted client node, disconnect it and than delete it
 */
void *deleteListNode(socketTCPtList *clientNode){
	socketTCPtList *runner, *deleteMe;
	while(sig_atomic_listTCP == 1)  //wait from safety use, let other thread finish the use of listTCP
		usleep(500);
	sig_atomic_listTCP = 1;
	runner = listTCP;
	if(runner->fdT == clientNode->fdT){		//the wanted node is in the head of the list
		deleteMe = listTCP;
		listTCP = listTCP->next;
	}
	else{	//find the wanted node and disconnect it
		while(runner->next != NULL && runner->next->fdT != clientNode->fdT)
			runner = runner->next;
		deleteMe = runner->next;
		runner->next = deleteMe->next;
	}
	sig_atomic_listTCP = 0;
	free(deleteMe);		//now delete the node safety
	return 0;
}

/*
 * quit from program safely.
 * type = 0 handle with UPD and TCP, type = 1 don't handle with UPD
 * UPD fail can be: threads open, setsockopt, open sockets, open/close fail or allocate memory.
 * TCP fail can be: threads open, listen, bind, open sockets or allocate memory.
 */
void *quitFromServer(int type){
	int i=0;
	socketTCPtList *runner = listTCP;
	printf("Quit from server - ");
	sig_atomic_runTime = 0;
	while(sig_atomic_listTCP == 1)
		usleep(100);
	sig_atomic_listTCP = 1;

	while(runner != NULL){
		deleteListNode(runner);
		runner = runner->next;
	}
	for(i=0 ; i<numOfSongs ; i++)
		free(songsName[i]);
	free(songsName);

	if(type != 1){
		for(i=0 ; i<numOfSongs ; i++){
			close(fdU[i]);
			fclose(fw[i]);
		}
		free(fw);
		free(localStation);
		free(fdU);
	}
	close(fd_Listen);

	pthread_cancel(TCP_lisiner);
	pthread_join(TCP_lisiner, NULL);
	pthread_cancel(stationsUDPthread);
	pthread_join(stationsUDPthread, NULL);

	printf("Good Bye\n");
	exit(EXIT_SUCCESS);
}

/*
 * initialize radios
 */
void *initRadio(void *numStations){
	int i, k, flag, *number = (int*)numStations, TTL = 64, flag2, nbytes;
	char buffer[BUFFER] = {0};

	if((fw = (FILE**)malloc(sizeof(FILE*)*(*number))) == 0){
		printf("error allocate memory to UDP's FILE array\n");
		quitFromServer(1);
	}
	if((localStation = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in)*(*number))) == 0){
		printf("error allocate memory to UDP's localStation (sockaddr_in)\n");
		free(fw);
		quitFromServer(1);
	}
	if((fdU = (int*)malloc(sizeof(int)*(*number))) == 0){
		printf("error allocate memory to UDP's fdU array\n");
		free(fw);
		free(localStation);
		quitFromServer(1);
	}
	if((mreq = (struct ip_mreq*)malloc(sizeof(struct ip_mreq)*(*number))) == 0){
		printf("error allocate memory to UDP's ip_merq array\n");
		free(fw);
		free(localStation);
		quitFromServer(1);
	}
	for(i=0 ; i<*number ; i++){
		fw[i] = fopen(songsName[i], "rb");  										//open files
		memset(&mreq[i], 0, sizeof(struct ip_mreq));
		memset((char *) &localStation[i], 0, sizeof(localStation));
		sprintf(multicastIpChar,"%hhu.%hhu.%hhu.%hhu",multicastIpInt[0],multicastIpInt[1],multicastIpInt[2],(multicastIpInt[3]+i));
		localStation[i].sin_family = AF_INET;
		localStation[i].sin_port = htons(UDP_port);
		localStation[i].sin_addr.s_addr = inet_addr(multicastIpChar); //set the correct multicast ip
		mreq[i].imr_multiaddr.s_addr = inet_addr(multicastIpChar);
		mreq[i].imr_interface.s_addr = INADDR_ANY;

		fdU[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  					//open sockets, create new sockets

		if(bind(fdU[i], (struct sockaddr*)&localStation[i], sizeof(localStation[i]))<0){
			printf("error bind listen socket\n");
			printf("index is %d\n",i);
			quitFromServer(1);
		}

		flag =  setsockopt(fdU[i],IPPROTO_IP, IP_MULTICAST_TTL, &TTL,sizeof(TTL));
		flag2=  setsockopt(fdU[i],IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq[i],sizeof(mreq[i]));

		if(fdU[i]<0 || flag<0 || fw[i]==NULL || flag2<0){  		//if there is some error
			printf("error at UDP initialize station - open file, setsocket() or socket()\n"); //there error somewhere
			for(k=0 ; k<i-1 ; k++){
				close(fdU[k]);
				fclose(fw[k]);
			}
			if(!(fdU[i]<0))   //fdU[i] didn't make any problem, close it
				close(fdU[i]);
			if(!(fw[i]==NULL))//fw[i] didn't make any problem, close it
				fclose(fw[i]);
			free(fw);
			free(localStation);
			free(fdU);
			quitFromServer(1);
		}
	}
	/*
	 * run all radio station together.
	 * every i interval, each station send data and than the function go to sleep, until interval i+1.
	 */
	while(sig_atomic_runTime == 1){ //start the broadcast radio station
		for(i=0 ; i<*number ; i++){
			memset(&buffer, 0, BUFFER);
			if((nbytes = fread(buffer, 1, BUFFER, fw[i])) > 0){//if there is more data to send, try read 1024 data size of 1
				if((nbytes = sendto(fdU[i],buffer,nbytes,0,(struct sockaddr *)&localStation[i],sizeof(localStation[i])))<0){
					printf("failed send station's file\n"); 	//if there is no error at send the file
					quitFromServer(0);
				}
			}
			else if(nbytes == 0)//close the file and than reopen it again
				rewind(fw[i]);
		}
		usleep(62500);
	}
	return 0;
}
