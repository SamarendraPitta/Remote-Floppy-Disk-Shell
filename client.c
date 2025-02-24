#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define MAX_PACKET_SIZE 512

struct udpmsg_req {
    unsigned short cmd;
    unsigned short seq;
    unsigned short handle;
    unsigned short ext;
};

struct udpmsg_reply {
    unsigned short cmd;
    unsigned short seq;
    unsigned short handle;
    unsigned short ext;
    unsigned char data[MAX_PACKET_SIZE];
};

unsigned short global_handle = -1;
int Socketpointer;
struct sockaddr_in Server_IP_Address;
pthread_mutex_t lock;


void *Recieve_Req_Thread(void *Input_args) {
	
	//This functions implement the responses which was sent by the server.
	
    struct udpmsg_reply Serv_Response;
    socklen_t addr_len = sizeof(Server_IP_Address);
	
	//here using the pthread for aquring the mutex lock where there wiopuld be no conflicts in reciving the responses.
    while (1) {
        memset(&Serv_Response, 0, sizeof(Serv_Response));
        int recievie_resp_bytes = recvfrom(Socketpointer, &Serv_Response, sizeof(Serv_Response), 0, (struct sockaddr *)&Server_IP_Address, &addr_len);
		
		if(recievie_resp_bytes < 0){
			perror("error occured during recievingf the responses from the server");
			return NULL;
		}
        pthread_mutex_lock(&lock);
        printf("\nResponse from server: Command: %u, Sequence: %u, Handle: %u, Extension: %u\n", 
               ntohs(Serv_Response.cmd), ntohs(Serv_Response.seq), ntohs(Serv_Response.handle), ntohs(Serv_Response.ext));
        printf("Data: %s\n\n", Serv_Response.data);
		printf("Floppy shell> ");
        fflush(stdout);
        pthread_mutex_unlock(&lock);

        if (ntohs(Serv_Response.handle) > 0) {
            global_handle = ntohs(Serv_Response.handle);
        }
    }
	printf("shell> ");
    return NULL;
}

//this function is used for sending the request to the server by preparing the request.
void *Send_Request_To_server(void *Input_args) {
	
    int *User_input = (int *)Input_args;
    int cmd_type = User_input[0];
    int ext = User_input[1];
    free(Input_args);
    
    struct udpmsg_req Request_To_Server;
    socklen_t addr_len = sizeof(Server_IP_Address);

    Request_To_Server.cmd = htons(cmd_type);
    Request_To_Server.seq = htons(1);
	if(global_handle > 0){
		Request_To_Server.handle = htons(global_handle);
	}
	else{
		Request_To_Server.handle = htons(0);
	}
    Request_To_Server.ext = htons(ext);

    int sent_bytes = sendto(Socketpointer, &Request_To_Server, sizeof(Request_To_Server), 0, (struct sockaddr *)&Server_IP_Address, addr_len);
    if(sent_bytes <0){
		perror("Request sending to server is failed.");
	}
	
	
    return NULL;
}

void showacceptablecommands(){
	printf("Acceptable commands by this floppy shell.\n");
	printf("Mounting remote floppy disk             : fmount [HOSTNAME] \n");
	printf("unmount remote floppy disk              : fumount [HOSTNAME] \n");
	printf("List structure of floppy disk           : structure \n");
	printf("List the contents in root directory     : traverse \n");
	printf("List the contents in root directory     : traverse -l\n");
	printf("Show content of FAT table()FAT1only     : showfat \n");
	printf("showing the content of specified sector : showsector 10 \n");
	printf("To show the content of specific file    : showfile 2006BH~1.PDF \n");
	
}

int main(int argc, char *argv[]) {
	
    char Input_Command[256];
    pthread_t Recevier_Thread;

    if ((Socketpointer = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed in creating the socket");
        exit(EXIT_FAILURE);
    }

    memset(&Server_IP_Address, 0, sizeof(Server_IP_Address));
    Server_IP_Address.sin_family = AF_INET;
    Server_IP_Address.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &Server_IP_Address.sin_addr);

    pthread_mutex_init(&lock, NULL);
    pthread_create(&Recevier_Thread, NULL, Recieve_Req_Thread, NULL);

    printf("This is Remote Floppy Disk Shell. Type 'help' to view accpetable commands.\n");

    while (1) {
		printf("--------------------------------------------------------------\n");
        printf("Floppy shell> ");
        fflush(stdout);
        fgets(Input_Command, sizeof(Input_Command), stdin);
        Input_Command[strcspn(Input_Command, "\n")] = 0;

        if (strcmp(Input_Command, "help") == 0) {
            //printf("Available Commands: fmount, fumount, structure, traverse, showfat, showsector, showfile, quit\n");
			showacceptablecommands();
		} else if (strcmp(Input_Command, "quit") == 0) {
            printf("Exiting...\n");
            break;
        } else {
            pthread_t pthread;
            int *User_input = malloc(2 * sizeof(int));
            if (!User_input) {
                perror("Memory allocation failed");
                continue;
            }

            if (strncmp(Input_Command, "fmount", 6) == 0) {
                User_input[0] = 1; User_input[1] = 0;
            } else if (strncmp(Input_Command, "fumount", 7) == 0) {
                User_input[0] = 2; User_input[1] = 0;
            }else if (strcmp(Input_Command, "traverse -l") == 0) {
                User_input[0] = 3; User_input[1] = 3; 				
			}else if (strncmp(Input_Command, "traverse", 8) == 0) {
                User_input[0] = 3; User_input[1] = 2;
			}else if (strncmp(Input_Command, "showfile 2006BH~1.PDF", 21) == 0) {
                User_input[0] = 3; User_input[1] = 6;
            } else if (strcmp(Input_Command, "showfat") == 0) {
                User_input[0] = 3; User_input[1] = 4;
			}
			 else if (strcmp(Input_Command, "structure") == 0) { 
                User_input[0] = 3; User_input[1] = 1;
            } else if (strncmp(Input_Command, "showsector", 10) == 0) {
                User_input[0] = 3; User_input[1] = 10;
            } else {
                printf("Unknown command. Type 'help' for a list of commands.\n");
                free(User_input);
                continue;
            }

            pthread_create(&pthread, NULL, Send_Request_To_server, User_input);
            pthread_detach(pthread);
        }
    }

    pthread_cancel(Recevier_Thread);
    pthread_join(Recevier_Thread, NULL);
    pthread_mutex_destroy(&lock);
    close(Socketpointer);
    return 0;
}
