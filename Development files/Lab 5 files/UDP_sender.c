/* UDP_sender
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

/****************** UDP multicast Source CODE ****************/

//input : The server’s IP (i.e. the sender's local IP address), The TCP port, The number of parts to send from the file, The file to send.
//void TCP_sender(char * server_IP_address ,short port ,int num_parts , const char* file_name)

int main(int argc, char* argv[])
{
	FILE * alice_ptr;
	int sock,i=0,bytes_sent=0,curr_sent=0,port=0 ,parts=0, sent_err=0,j;
	char message[Buffer_size] = {0};
	char * IP;
	u_char TTL =10;
	struct sockaddr_in serverAddr;

	printf("input received: \nexe File:%s, Server IP address:%s,\nServer port num:%s, Num of parts:%s, Text file to send:%s.\n(total of %d args)\n", argv[0], argv[1], argv[2], argv[3] ,argv[4], argc);
	if(argc != 5)
	{ printf("Too few arguments. Bye bye!\n"); exit(1); }

	IP = argv[1];
	port = atoi(argv[2]);
	parts = atoi(argv[3]);

	//try to open the socket to send the data on
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) { perror("Can't create socket"); close(sock); exit(1); }

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr(IP);
	serverAddr.sin_port = htons(port);

	//try to bind this UDP socket
	if(bind(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))==-1)
	{ perror("Can't bind UDP socket"); close(sock); exit(1); }

	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &TTL, sizeof(TTL)); //set TTL field to 10

	//try to open the text file to send
	alice_ptr = fopen (argv[4] , "r");
	if(!alice_ptr) { printf("couldn't open text file to send"); close(sock); exit(1); }

	/*---- Send message to the socket of the incoming connection ----*/
	//Read file into buffer & Send buffer to client
	//While (not all data sent || any connection error)
	while (((fscanf(alice_ptr, "%1024c", message))!=EOF) && i < parts) // if we got here, no errors occurred.
	{
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
	}
	if (i < parts)
		printf("~~~~EOF reached in file.~~~~\n\n");

	//print stats
	printf("\nbytes sent:%d (out of %d bytes), in %d parts.\nsend errors: %d\n", bytes_sent, Buffer_size*parts , parts,sent_err);

	//free all resources;
	fclose(alice_ptr);
	shutdown(sock, SHUT_RDWR);
	close(sock);

	if(sent_err==0)
		printf("\nsuccessful transmission. Bye bye!\n");

	return EXIT_SUCCESS;
}





