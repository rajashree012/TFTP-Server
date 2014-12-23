#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>

// client is differentiated based on the socket fd
struct client 
{
	int socketfd; // socket fd for each request
	int port;     // port number of client specific to a request                        
	int ack_no;   // expected ack no after sending the block
	int block_num; 
	char *filename;
	int file_position; // current file position
	float connection_time; // accumulating the total idle time 
	time_t start; // start time when the block has been sent
	char* cli_addr_ip;
	int end_of_file;      // When end of file has been reached for the current request
	int lost_ack;   // this keeps track of number of acks client sends when the packet is actually lost
	int lost_packet_ack; // stores ack no of packet before it is successfully delivered
	struct client *next;
};

// adding information of the newly generated request by the client
void insert(struct client *head, struct client *node)
{
	if (head->next == NULL)
	{
		head->next = node;
		return;
	}
	else
		insert(head->next,node);
}

// Finding information of the client given socketfd and starting of the linked list of the currently connected client requests
// getting the node before the actual one except when the node is the first one
struct client* find(int socketfd, struct client *head)  
{
	if (socketfd == head->socketfd)
		return head;
	else if (head->next == NULL)
		return NULL;
	else if (socketfd == head->next->socketfd)
		return head;
	else 
		return(find(socketfd,head->next));
}

// closing connection of client(socktfd) corresponding to request after 5 sec
struct client* delete(struct client *head, struct client *deletenode)
{
	if (deletenode == head)
		return head->next;
	else
	{
		deletenode->next = deletenode->next->next;
		return head;
	}
}

// After each iteration currently active client ports are checked for ack messages if not received then block of data is retransmitted
void retransmission(struct client *head,struct client **mainhead)
{
	
	if (head == NULL)
		return;
	else
	{
		time_t end;
		time(&end);
		double seconds = difftime(end,head->start);
		head->connection_time+=seconds;
		
		// if idle time is more than 5 sec then end the connection
		if (head->connection_time > 5 && head->end_of_file == 1)
		{
			struct client *temp = head->next;
			struct client *temp1 = find(head->socketfd,*mainhead);
			*mainhead = delete(temp1,*mainhead);
			close(head->socketfd);
			retransmission(temp,mainhead);
		}
	
		// checking if 100 ms have passed since transmission of data by getting the previous position of the file
		else if (seconds > 0.1 && head->end_of_file == 1)
		{
			int new_position = head->file_position;
			if (new_position % 512 == 0)
				new_position = new_position - 512;
			else
				new_position = new_position - new_position % 512;
			struct sockaddr_in cli_addr ;
			cli_addr.sin_family = AF_INET;
			cli_addr.sin_addr.s_addr = inet_addr(head->cli_addr_ip);
			cli_addr.sin_port = htons(head->port);
			// retransmission of packet 
			char * packet = (char *)malloc(516);
			if (packet == NULL)
			{
				fprintf(stderr,"failed to allocate memory\n");
				exit(0);
			}
			int file_position = formdatapacket(packet,head->filename,new_position,head->block_num);
			if (sendto(head->socketfd, packet, 4+head->file_position-new_position, 0, (struct sockaddr*) &cli_addr, sizeof(cli_addr)) == -1)
			{
				perror("ERROR on sending");
	 			exit(1);
			}
			time(&(head->start));
			retransmission(head->next,mainhead);
		}
		else 
			retransmission(head->next,mainhead);
	}
}

// getting opcode or packet type for the request from the tftp client
int getpackettype(char *message)
{
	uint16_t packet_type = 0;
	memcpy(&packet_type,message,2);
	packet_type = ntohs(packet_type);
	return packet_type;
}

// formation of data packet to be sent to tftp client
int formdatapacket(char *packet, char *filename, int file_position, int block_num)
{
	FILE *f;
	int ch;
	f = fopen(filename,"r");
	if(f==NULL)
	{
		puts("Can't open that file!");
		return -999;
	}
	int initial_position = file_position;
	char *copy_file = (char *)malloc(512);
	if (copy_file == NULL)
	{
		fprintf(stderr,"failed to allocate memory\n");
		exit(0);
	}
	char *main_copy_file = copy_file;
	fseek(f, initial_position, SEEK_SET);
	// 512 bytes of file are read from the requested file
	while(file_position<(512*block_num) && (ch=fgetc(f))!=EOF)
	{	
		memcpy(copy_file,&ch,1);
		copy_file+=1;
		file_position++;
	}
	fclose(f);
	uint16_t data_packet = 3;
	data_packet = htons(data_packet);
	memcpy(packet,&data_packet,2);
	packet+=2;
	uint16_t temp = 0;
	temp = htons(block_num);
	memcpy(packet,&temp,2);
	packet+=2;
	memcpy(packet,main_copy_file,file_position-initial_position);
	return file_position;
}

// form error packet corresponding to the error code message
int formerrorpacket(char *packet,int error_code,char *message)
{
	uint16_t error_packet = 5;
	error_packet = htons(error_packet);
	memcpy(packet,&error_packet,2);
	packet+=2;
	uint16_t temp = 0;
	temp = htons(error_code);
	memcpy(packet,&temp,2);
	packet+=2;
	memcpy(packet,message,strlen(message)+1);
	return 5 + strlen(message);
}

int main( int argc, char *argv[] )
{
	int sockfd, portno;
	struct sockaddr_in serv_addr, cli_addr;
	int clilen = sizeof(cli_addr);

	if (argc < 3) 
	{
		fprintf(stderr,"usage %s server_ip server_port\n", argv[0]);
		exit(0);
	}

	// First call to socket() function 
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);   
	if (sockfd < 0) 
	{
		perror("ERROR opening socket");
		exit(1);
	}

	// Initialize socket structure 
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[2]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(portno);

	/* Now bind the host address using bind() call.*/  
	if (bind(sockfd, (struct sockaddr *) &serv_addr,
		          sizeof(serv_addr)) < 0)
	{
		 perror("ERROR on binding");
		 exit(1);
	}

	fd_set master;
	fd_set read_fds;
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	int fdmax = 0, nbytes; //keeping track of maximum file descriptor
	FD_SET(sockfd,&master);
	fdmax = sockfd;
	int i,j;
	
	printf("Starting tftp server\n");
	fflush(stdout);
	int childport = portno;
	char *filename = NULL;
	struct client* temp = NULL;
	// starting of linked list of new socket connections
	struct client *head = NULL;
	struct client **mainhead = &head;
	int newsockfd = 0;

	for(;;)
	{	
		//updating readfds
		read_fds = master;
		// message from the client will be either error ack or read request
		char *message_received = (char *)malloc(sizeof(char)*512); 
		if (message_received == NULL)
		{
			fprintf(stderr,"failed to allocate memory\n");
			exit(0);
		}
		// checking if there are any retransmission timeouts		
		retransmission(head,mainhead);
		head = *mainhead;
		
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) 
		{
			perror("select");
			exit(1);
		}

		for(i = sockfd; i <= fdmax; i++) 
		{
			if (FD_ISSET(i, &read_fds)) 
			{ 
				// when new request has come
				if (i == sockfd) 
				{   
					bzero(message_received,512); 
					printf("hello : There is a new request\n");   
					fflush(stdout);
					if ((nbytes = recvfrom(sockfd, message_received, 512, 0, (struct sockaddr *) &cli_addr, &clilen)) == -1)
					{
					    	perror("ERROR on receiving");
					}
					// checking whether its ack or request packet
					int packet_type = getpackettype(message_received);

					int port = ntohs(cli_addr.sin_port);
					char *client_ip = (char *)malloc(INET_ADDRSTRLEN);
					if (client_ip == NULL)
					{
						fprintf(stderr,"failed to allocate memory\n");
						exit(0);
					}
					inet_ntop(AF_INET, &(cli_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
					// create new child connection
					newsockfd = socket(AF_INET, SOCK_DGRAM, 0);
					if (newsockfd < 0) 
					{
						perror("ERROR opening socket");
						exit(1);
					}

					// Initialize socket structure 
					bzero((char *) &serv_addr, sizeof(serv_addr));
					portno = ++childport;

					serv_addr.sin_family = AF_INET;
					serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
					serv_addr.sin_port = htons(portno);

					/* Now bind the host address using bind() call.*/  
					while(1)
					{
						if (bind(newsockfd, (struct sockaddr *) &serv_addr,
								  sizeof(serv_addr)) < 0)
						{
							 printf("SSSS");fflush(stdout);
							 portno = ++childport;
							 serv_addr.sin_port = htons(portno);
							 if (portno > 65535)
							 {
								childport = childport - 1000;
								portno = childport;
							 }
						}
						else
							break;
					}

					FD_SET(newsockfd,&master);
					if (newsockfd > fdmax) 
					{	// keep track of the max
					        fdmax = newsockfd;
					}

					// request for reading files
					if (packet_type == 1)
					{
						// getting filename
						message_received+=2;
						filename = (char *)malloc(sizeof(char)*(strlen(message_received)+1));
						if (filename == NULL)
						{
							fprintf(stderr,"failed to allocate memory\n");
							exit(0);
						}
						memcpy(filename,message_received,(strlen(message_received)+1));
						printf ("requested filename : %s  and socket %d\n",filename,newsockfd);

						// Storing the information of new client request. Each client request has unique socket file descriptor.
						if (head == NULL)
						{
							head = (struct client*)malloc(sizeof(struct client));
							if (head == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							head->socketfd = newsockfd;
							head->port = port;
							head->ack_no = 0;
							head->block_num = 0;
							head->filename = filename;
							head->file_position = 0;
							head->connection_time = 0;
							time(&(head->start));
							head->cli_addr_ip = (char *)malloc(INET_ADDRSTRLEN);
							if (head->cli_addr_ip == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							memcpy(head->cli_addr_ip,client_ip,INET_ADDRSTRLEN);
							head->end_of_file = 1;
							head->lost_ack = 0;
							head->lost_packet_ack = 0;
							head->next = NULL;
						}
						else if((temp = find(newsockfd,head))!=NULL)
						{
							if (temp == head)
							{
								head->ack_no = 0;
								head->block_num = 0;
								head->filename = filename;
								head->file_position = 0;
								head->connection_time = 0;
								time(&(head->start));
								temp->end_of_file = 1;
								temp->lost_ack = 0;
								temp->lost_packet_ack = 0;
							}
							else
							{
								temp->next->ack_no = 0;
								temp->next->block_num = 0;
								temp->next->filename = filename;
								temp->next->file_position = 0;
								temp->connection_time = 0;
								time(&(temp->start));
								temp->end_of_file = 1;
								temp->lost_ack = 0;
								temp->lost_packet_ack = 0;
							}
						}
						else
						{
							temp = (struct client*)malloc(sizeof(struct client));
							if (temp == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							temp->socketfd = newsockfd;
							temp->port = port;
							temp->ack_no = 0;
							temp->block_num = 0;
							temp->filename = filename;
							temp->file_position = 0;
							temp->connection_time = 0;
							time(&(temp->start));
							temp->cli_addr_ip = (char *)malloc(INET_ADDRSTRLEN);
							if (temp->cli_addr_ip == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							memcpy(temp->cli_addr_ip,client_ip,INET_ADDRSTRLEN);
							temp->end_of_file = 1;
							temp->next = NULL;
							temp->lost_ack = 0;
							temp->lost_packet_ack = 0;
							insert(head,temp);
						}
								
		
						// reading first 512 bytes from file
						temp = find(newsockfd,head);
						// taking care of the case where client socket fd is not created
						if (temp == NULL)
						{
							char * packet = (char *)malloc(516);
							if (packet == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							bzero(packet,516);
							int file_position = formerrorpacket(packet,0,"Invalid socketfd");
							if (sendto(newsockfd, packet, file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
							{
								perror("ERROR on sending");
					 			exit(1);
							}
						}
						else 
						{
							if (temp!=head || newsockfd!=temp->socketfd)
								temp = temp->next;
							// updating the values which will be used when the packet sent t tftp client is lost
							temp->lost_ack = 0;
							temp->lost_packet_ack = temp->ack_no;
							// increasing block number and expected ack no
							temp->block_num+=1;
							temp->ack_no+=1;
							temp->connection_time = 0;
							int initial_position = temp->file_position;
							char * packet = (char *)malloc(516);
							if (packet == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							temp->file_position = formdatapacket(packet,temp->filename,temp->file_position,temp->block_num);
							// sending error packet if the file is not present on the server
							if (temp->file_position == -999)
							{
								bzero(packet,516);
								temp->file_position = formerrorpacket(packet,1,"file is not present on the server");
								if (sendto(newsockfd, packet, temp->file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
								{
									perror("ERROR on sending");
						 			exit(1);
								}
								temp->end_of_file = 0;
							}
							else
							{
								// sending the first data block
								if (sendto(newsockfd, packet, 4+temp->file_position-initial_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
								{
									perror("ERROR on sending");
						 			exit(1);
								}
							}
						}
					}
					else 
					{
						char * packet = (char *)malloc(516);
						if (packet == NULL)
						{
							fprintf(stderr,"failed to allocate memory\n");
							exit(0);
						}
						bzero(packet,516);
						int file_position = formerrorpacket(packet,0,"Currently the server does not support WRQ");
						if (sendto(newsockfd, packet, file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
						{
							perror("ERROR on sending");
				 			exit(1);
						}
					}
				}
				else
				{
					bzero(message_received,512); 
					// receiving acks
					if ((nbytes = recvfrom(i, message_received, 512, 0, (struct sockaddr *) &cli_addr, &clilen)) == -1)
					{
					    	perror("ERROR on receiving");
					}
					// checking whether its ack or request packet
					int packet_type = getpackettype(message_received);
					if (packet_type == 4)
					{
						message_received+=2;
						uint16_t ackno = 0;
						memcpy(&ackno,message_received,2);
						ackno = ntohs(ackno);
						temp = find(i,head);
						
						// taking care of the case of unknown socket fd
						if (temp == NULL)
						{
							char * packet = (char *)malloc(516);
							if (packet == NULL)
							{
								fprintf(stderr,"failed to allocate memory\n");
								exit(0);
							}
							bzero(packet,516);
							int file_position = formerrorpacket(packet,0,"unknown socketfd");
							if (sendto(i, packet, file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
							{
								perror("ERROR on sending");
					 			exit(1);
							}
						}
						else
						{
							if (temp!=head || i!=head->socketfd)
								temp = temp->next;
							
							// checking whether the acknowledgement arrived is expected one or not. This is the case where the packet is actually lost unlike the timeout
							// in case a packet is lost the server repeatedly sends the ack. In case the number of acks sent is greater than 4 then the packet is resent
							if (ackno < temp->ack_no)
							{	
								if (temp->lost_packet_ack == ackno)
									temp->lost_ack += 1;
								if (temp->lost_ack >= 4)
								{
									temp->connection_time = 0;								
									time(&(temp->start));
									int new_position = temp->file_position;
									if (new_position % 512 == 0)
										new_position = new_position - 512;
									else
										new_position = new_position - new_position % 512;
				
									char * packet = (char *)malloc(516);
									if (packet == NULL)
									{
										fprintf(stderr,"failed to allocate memory\n");
										exit(0);
									}
									// resending previous packet once again
									int file_position = formdatapacket(packet,temp->filename,new_position,temp->block_num);
									if (sendto(temp->socketfd, packet, 4+temp->file_position-new_position, 0, (struct sockaddr*) &cli_addr, sizeof(cli_addr)) == -1)
									{
										perror("ERROR on sending");
							 			exit(1);
									}
								}
							}
							else
							{
								// sending the successive blocks of data
								temp->lost_ack = 0;
								temp->lost_packet_ack = temp->ack_no;
								temp->connection_time = 0;
								temp->block_num+=1;
								temp->ack_no+=1;
								// resetting the time for sending new block of data
								//temp->start = clock();
								time(&(temp->start));
								int initial_position = temp->file_position;
				
								char * packet = (char *)malloc(516);
								if (packet == NULL)
								{
									fprintf(stderr,"failed to allocate memory\n");
									exit(0);
								}
								temp->file_position = formdatapacket(packet,temp->filename,temp->file_position,temp->block_num);
								// in case the file is not present on the server
								if (temp->file_position == -999)
								{
									bzero(packet,516);
									temp->file_position = formerrorpacket(packet,1,"file is not present on the server");
									if (sendto(i, packet, temp->file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
									{
										perror("ERROR on sending");
							 			exit(1);
									}
									temp->end_of_file = 0;
								}
								else
								{
									// Identifying end of the file so that there is no need to retransmit any block even though the client is idle
									if(((temp->file_position) - initial_position) <= 0)
										{temp->end_of_file = 0;}
									if (sendto(i, packet, 4+temp->file_position-initial_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
									{
										perror("ERROR on sending");
							 			exit(1);
									}
								}
							}
						}
					}
					// if other than RRQ lets say WWQ is sent the send an error packet
					else 
					{
						char * packet = (char *)malloc(516);
						if (packet == NULL)
						{
							fprintf(stderr,"failed to allocate memory\n");
							exit(0);
						}
						bzero(packet,516);
						int file_position = formerrorpacket(packet,0,"Currently the server does not support WRQ");
						if (sendto(i, packet, file_position, 0, (struct sockaddr*) &cli_addr, clilen) == -1)
						{
							perror("ERROR on sending");
				 			exit(1);
						}
					}
				}
			}// if i is in ready to read state			
		} //end of i for loop
	}// end of for
	return 0;
}// ens of main

