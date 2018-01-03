/* TCP_sender
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

/****************** TCP SERVER CODE ****************/

//input : The server’s IP (i.e. the sender's local IP address), The TCP port, The number of parts to send from the file, The file to send.
//void TCP_sender(char * server_IP_address ,short port ,int num_parts , const char* file_name)
int main(int argc, char* argv[])
{
	int welcomeSocket, newSocket,i=0,j=0,bytes_sent=0,curr_sent=0,port=0 ,parts=0 , send_err =0;
	char buffer[Buffer_size] = {0};
	char * IP;
	struct sockaddr_in serverAddr;
	struct sockaddr_storage serverStorage;
	FILE * alice_ptr;
	socklen_t addr_size;

	printf("input received: \nexe File:%s, Server IP address:%s,\nServer port num:%s, Num of parts:%s, Text file to send:%s.\n(total of %d args)\n", argv[0], argv[1], argv[2], argv[3] ,argv[4], argc);
	if(argc != 5) { printf("Too few/many arguments received. Bye bye!\n"); exit(1); }

	IP = argv[1];
	port = atoi(argv[2]);
	parts = atoi(argv[3]);

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
	addr_size = sizeof serverStorage;
	newSocket = accept(welcomeSocket, (struct sockaddr *)&serverStorage, &addr_size);
	if(newSocket==-1)
	{ perror("Error in accepting new client"); close(welcomeSocket); close(newSocket); exit(1); }
	else
	{ printf("\naccepted new client. \n\n"); }

	alice_ptr = fopen (argv[4] , "r");
	if(!alice_ptr) { close(newSocket); close(welcomeSocket); exit(1); }

	/*---- Send message to the socket of the incoming connection ----*/
	//Read file into buffer & Send buffer to client
	//While (not all data sent || any connection error)
	while (((fscanf(alice_ptr, "%1024c", buffer))!=EOF) && i < parts) // if we got here, no errors occurred.
	{
		curr_sent = send(newSocket,buffer,Buffer_size,0);
		if(curr_sent > 0)
			bytes_sent += curr_sent;
		else if(curr_sent == -1)
		{
			perror("");
			printf("\n");
			printf("Error in sending file part number: %d\n",i+1);
			send_err++;
		}
		for(j=0;j<Buffer_size;j++)
			buffer[j] = '\0';
		i++;
	}
	if (i < parts)
		printf("~~~~EOF reached in file.~~~~\n\n");

	//print stats
	printf("bytes sent:%d (out of %d bytes), in %d parts.\nsend errors: %d\n", bytes_sent, Buffer_size*parts , parts,send_err);

	//free all resources;
	fclose(alice_ptr);
	shutdown(newSocket, SHUT_RDWR);
	shutdown(welcomeSocket, SHUT_RDWR);
	close(newSocket);
	close(welcomeSocket);
	if(send_err==0)
		printf("successful transmission. Bye bye!\n");

	return EXIT_SUCCESS;
}







