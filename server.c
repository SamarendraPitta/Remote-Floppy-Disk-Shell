#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#define PORT 8080
#define FLOPPY_DISK_IMAGE "floppy.img"
#define MAXIMUM_CLIENTS 4
#define BUFFER_SIZE 1024
#define CONN_REQ    1
#define DISCONN_REQ 2
#define DATA_REQ    3
#define CONN_RES    4
#define DISCONN_RES 5
#define DATA_RES 6
#define FAT_ENTRIES 2848
#define MAX_RESPONSE_SIZE 512
#define SECTORS_PER_CLUSTER 1
#define ROOT_DIRECTRY_ENTRIES 224
#define SECTOR_SIZE 512
#define FAT1_START_SECTOR 1
#define FAT_SECTORS 9
#define ENTRIES_PER_PAGE 32
#define ROOT_DIR_START_SECTOR 19
#define DATA_START_SECTOR 33
#define FAT1_SECTORS 9
#define FAT_START_SECTOR 1
#define ENTRY_SIZE 32



struct udpmsg_req {
    unsigned short cmd;
    unsigned short seq;
    unsigned short handle;
    unsigned short ext;
}client_request;

struct udpmsg_reply {
    unsigned short cmd;
    unsigned short seq;
    unsigned short handle;
    unsigned short ext;
    unsigned char data[512];
}client_reply;

struct Client_Info_Struct {
    int UDP_ServerSocket;
	struct sockaddr_in CLIENT_ADDRESS;
	ssize_t Received_Request_Len;
	socklen_t Client_IPAddress_Len;
    struct udpmsg_req CLIENT_REQUEST;

};

typedef struct {
    struct sockaddr_in client_address;
	unsigned short portnumber;
    unsigned short handle;
    int active;
} Client;


Client Client_Struct[MAXIMUM_CLIENTS];
int Floppy_disk_img_pointer = -1;
pthread_mutex_t Server_Mutex = PTHREAD_MUTEX_INITIALIZER;
int  Total_clients_Connected;

void* Serve_client_req(void* client_args);
void Print_HEX_Dump_Format(const uint8_t *data, size_t size);
int FindFile_FAT12(int file_desc, const char *filename, uint32_t *StartCluster, uint32_t *file_size);
uint32_t Fetch_NextCluster_FAT(int file_desc, uint32_t CurrCluster);

int mountfloppydisk(){
	if (Floppy_disk_img_pointer == -1) {
		Floppy_disk_img_pointer = open("floppy.img", O_RDONLY);
		if (Floppy_disk_img_pointer == -1) {
			perror("Failed to mount floppy image");
			return -1;
		}
		//printf("floppy disk mounted");
	}
	return Floppy_disk_img_pointer;
}

void Send_Reponse_to_Client(struct Client_Info_Struct *client_details){
	
	sleep(1);
	int Socket_Pointer = client_details->UDP_ServerSocket;
    struct sockaddr_in client_address = client_details->CLIENT_ADDRESS;
    //truct udpmsg_req Client_Request_Buffer = client_details->CLIENT_REQUEST;
	socklen_t Client_IPAddress_Len = sizeof(client_details->CLIENT_ADDRESS);
	
	struct udpmsg_reply client_reply;

	int response_sent = sendto(Socket_Pointer, &client_reply, sizeof(client_reply), 0,
					 (struct sockaddr *)&client_address, Client_IPAddress_Len);
	if(response_sent < 0){
		int Error_Code = errno;
		fprintf(stderr, "sending response failed: %s (errno: %d)\n", strerror(Error_Code), Error_Code);
		printf("sendto failed: %s (errno: %d)\n", strerror(errno), errno);
	}
}

bool Check_Client_EXISTS(struct Client_Info_Struct *Struct_arg, unsigned short Port_Num){
	
	//int socket_pointer = Struct_arg->UDP_ServerSocket;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    struct udpmsg_req Client_Request_Buffer = Struct_arg->CLIENT_REQUEST;
	//socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
	
	//printf("handlec:%d\n", htons(Client_Request_Buffer.handle));
	for(int i=0; i< MAXIMUM_CLIENTS; i++){
		//printf("entered\n");
		if(Client_Struct[i].client_address.sin_addr.s_addr == client_address.sin_addr.s_addr && Client_Struct[i].active &&
			Client_Struct[i].portnumber == Port_Num &&Client_Struct[i].handle == htons(Client_Request_Buffer.handle)){
				return true;
			}
	} 
	return false;
}

void Remove_Client_Connection(int serv_socket, int handle, struct sockaddr_in client_address, unsigned short Port_Num, socklen_t Client_IPAddress_Len){
	
	//printf("entered into disconnected fun\n");
	pthread_mutex_lock(&Server_Mutex);
	for(int i=0; i< MAXIMUM_CLIENTS; i++){
		if(Client_Struct[i].active && Client_Struct[i].client_address.sin_addr.s_addr == client_address.sin_addr.s_addr && 
			Client_Struct[i].portnumber == Port_Num &&Client_Struct[i].handle == handle){
			
			Client_Struct[i].active = 0;
			Client_Struct[i].handle = 0;
			Total_clients_Connected--;
			pthread_mutex_unlock(&Server_Mutex);
			break;
		}
	}
	pthread_mutex_unlock(&Server_Mutex);
}


//checking the Client_Struct connections and if the client connections are lessthan maximum then allocating the Client_Struct to the server i mean establishing the connection between server and the client.
int Check_total_Client_connections(int serv_socket, struct sockaddr_in client_address, unsigned short Port_Num, socklen_t Client_IPAddress_Len){
	
	pthread_mutex_lock(&Server_Mutex);
	struct udpmsg_reply client_reply;
	//printf("entered into max Client_Struct connections \n");
	if(Total_clients_Connected > MAXIMUM_CLIENTS){
		
		perror(" maximum Clients reached, try again after some time.\n");
		int handler = 0;
		client_reply.cmd = 1;
		client_reply.seq = 0;
		client_reply.handle = htons(handler);
		client_reply.ext = 0;
		strcpy((char *)client_reply.data, "not able to connect to the server. Maximum limit reached.");
		
		//send_resp_to_client(serv_socket, &client_reply, client_address, Client_IPAddress_Len);
		pthread_mutex_unlock(&Server_Mutex);
		return -1;
	}
	 
	for (int i = 0; i < MAXIMUM_CLIENTS; i++) {
		if (!Client_Struct[i].active) {
			Client_Struct[i].client_address = client_address;
			Client_Struct[i].portnumber = Port_Num;
			Client_Struct[i].handle = i+1;
			Client_Struct[i].active = 1;
			Total_clients_Connected++;
			pthread_mutex_unlock(&Server_Mutex);
			return Client_Struct[i].handle;
		}
	}
	 
	return -1;
}

// Function to send a UDP response
void Send_Response_To_Client(int sockfd, struct sockaddr_in *client_address, socklen_t client_addr_len, 
                       uint16_t cmd, uint16_t seq, uint16_t handle, uint16_t ext, const char *data) {
    struct udpmsg_reply reply;
    memset(&reply, 0, sizeof(reply));

    reply.cmd = htons(cmd);
    reply.seq = htons(seq);
    reply.handle = handle;
    reply.ext = htons(ext); // Page number or total packets indicator
    strncpy((char*)reply.data, data, sizeof(reply.data) - 1);
    reply.data[sizeof(reply.data) - 1] = '\0'; // Ensure null termination

    sendto(sockfd, &reply, sizeof(reply), 0, (struct sockaddr*)client_address, client_addr_len);
}
//*****************************************structure*********************************************************
//This function is used to list the structure of floppy image.
//This fun tells us finding the number of FATS, number of sectors etc.
void List_Structure_Of_Floppy_Disk(struct Client_Info_Struct *Struct_arg) {
	
    //variable declaration
	char mem_buffer[512];
    struct udpmsg_reply client_reply;
    struct sockaddr_in cli_address = Struct_arg->CLIENT_ADDRESS;
    struct udpmsg_req Client_Request_Buffer = Struct_arg->CLIENT_REQUEST;
    socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
	int UDP_SOCKET = Struct_arg->UDP_ServerSocket;

    lseek(Floppy_disk_img_pointer, 0, SEEK_SET);
    read(Floppy_disk_img_pointer, mem_buffer, 512);

    int Total_sectores_per_Cluster = mem_buffer[13];
    int ReserverSectors = *(unsigned short*)(mem_buffer + 14);
    int Total_FATS = mem_buffer[16];
    int ROOT_ENTRIES = *(unsigned short*)(mem_buffer + 17);
    int Total_Sectors_Per_Fat = *(unsigned short*)(mem_buffer + 22);
    int FAT1_Start_entry = ReserverSectors;
    int FAT2_Start_entry = FAT1_Start_entry + Total_Sectors_Per_Fat;
    int RootDir_StartEntry = FAT2_Start_entry + Total_Sectors_Per_Fat;
    int RootDir_Sectors = (ROOT_ENTRIES * 32 + 511) / 512;
	
	//explicitly assiging the packets count because we know that the structure consumes less data.
    int total_packets = 2;

    // Here first sending packet count to server and then sending the data.
    char Total_Packet_Message[32];
	//Making the packet with snprintf
    snprintf(Total_Packet_Message, sizeof(Total_Packet_Message), "TOTAL Number of PACKETS: %d", total_packets);
    Send_Response_To_Client(UDP_SOCKET, &cli_address, Client_IPAddress_Len, 
                      htons(Client_Request_Buffer.cmd), Client_Request_Buffer.seq, htons(Client_Request_Buffer.handle), 
                      total_packets, Total_Packet_Message);

    printf("Sending total packet count : %d\n", total_packets);

    // Preparing and sending the packets
    snprintf((char *)client_reply.data, sizeof(client_reply.data),
             "number of FAT:                   %d\n"
             "number of sectors used by FAT:   %d\n"
             "number of sectors per cluster:   %d\n"
             "number of ROOT Entries:          %d\n"
             "number of bytes per sector:      512\n",
             Total_FATS, Total_Sectors_Per_Fat, Total_sectores_per_Cluster, ROOT_ENTRIES);
	
	printf("%s\n", client_reply.data);
	
    Send_Response_To_Client(UDP_SOCKET, &cli_address, Client_IPAddress_Len, 
                      htons(Client_Request_Buffer.cmd), 1, htons(Client_Request_Buffer.handle), 
                      1, client_reply.data);

    printf("Sent main packet to cleint\n");

    //the bewlo snprintf specifies preparing of sector type packets.
    snprintf((char *)client_reply.data, sizeof(client_reply.data),
             "---Sector #---     ---Sector Types---\n"
             "      0                  BOOT\n"
             "   %02d -- %02d              FAT1\n"
             "   %02d -- %02d              FAT2\n"
             "   %02d -- %02d              ROOT DIRECTORY\n",
             FAT1_Start_entry, FAT2_Start_entry - 1,
             FAT2_Start_entry, RootDir_StartEntry - 1,
             RootDir_StartEntry, RootDir_StartEntry + RootDir_Sectors - 1);
	
	printf("%s\n", client_reply.data);
	
    Send_Response_To_Client(UDP_SOCKET, &cli_address, Client_IPAddress_Len, 
                      htons(Client_Request_Buffer.cmd), 2, htons(Client_Request_Buffer.handle), 
                      2, client_reply.data);
	
    printf("Sent sector types packet\n");
}

//-**************************************traverse******************************************************


//*********************************Showfat**************************************************************
//Showfat lists the contents present in the FAT1.
//FAT1 consists of 558 sectors. So this command shows the data which is present in 558 sectors.
//packets sent to client is 558 packets eachn packet consist fo each sector.
void ShowFatCommand_Implementation(struct Client_Info_Struct *Struct_args) {
    
	//vaarible initialization and declaration
	uint8_t fatSector[FAT1_SECTORS * SECTOR_SIZE];
    char mem_buffer[MAX_RESPONSE_SIZE];
	struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)Struct_args;
    //struct udpmsg_req *Client_Request_Buffer = &Struct_arg->CLIENT_REQUEST;
    int sockfd = Struct_arg->UDP_ServerSocket;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
    int total_Pckts = (FAT_ENTRIES + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;
	char Total_Packet_Message[32];
    
	// Reading FAT1 sectors
	printf("totalpages:%d\n", total_Pckts);
    lseek(Floppy_disk_img_pointer, FAT1_START_SECTOR * SECTOR_SIZE, SEEK_SET);
    read(Floppy_disk_img_pointer, fatSector, FAT1_SECTORS * SECTOR_SIZE);
    snprintf(Total_Packet_Message, sizeof(Total_Packet_Message), "Total packets: %d", total_Pckts);
    //sending the respoinse to the client saying that number of packets.
	Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len, 
                      Struct_arg->CLIENT_REQUEST.cmd, Struct_arg->CLIENT_REQUEST.seq, Struct_arg->CLIENT_REQUEST.handle, 
                      total_Pckts, Total_Packet_Message);
    
    printf("Total packet count sent is : %d\n", total_Pckts);

	//the below implementation specifies that the data is packed into pages, means the packets and then sending one by one.
    for (int pckt = 0; pckt < total_Pckts; pckt++) {
        int StartEntry = pckt * ENTRIES_PER_PAGE;
        int EndEntry = (pckt + 1) * ENTRIES_PER_PAGE;
        if (EndEntry > FAT_ENTRIES) EndEntry = FAT_ENTRIES;

        int TotalBytes_Written = snprintf(mem_buffer, MAX_RESPONSE_SIZE, 
                                     "FAT1 Contents (Page %d of %d):\nEntry\tValue\tStatus/Next Cluster\n", 
                                     pckt + 1, total_Pckts);

        for (int i = StartEntry; i < EndEntry && TotalBytes_Written < MAX_RESPONSE_SIZE - 50; i++) {
            uint16_t fat_entry = (i % 2 == 0) ?
                (fatSector[i * 3 / 2] | (fatSector[i * 3 / 2 + 1] & 0x0F) << 8) :
                ((fatSector[i * 3 / 2] & 0xF0) >> 4) | (fatSector[i * 3 / 2 + 1] << 4);

            const char *status;
            if (i == 0) status = "Media descriptor";
            else if (i == 1) status = "Reserved";
            else if (fat_entry == 0x000) status = "Free";
            else if (fat_entry >= 0x002 && fat_entry <= 0xFEF) status = "Used";
            else if (fat_entry == 0xFF7) status = "Bad cluster";
            else if (fat_entry >= 0xFF8) status = "End of file";
            else status = "Reserved";

            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written, 
                                      "%d\t0x%03X\t%s", i, fat_entry, status);
            
            if (fat_entry >= 0x002 && fat_entry <= 0xFEF) {
                TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written, 
                                          " (Next: %d)", fat_entry);
            }
			
            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written, "\n");
        }
        Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len, 
                          3, pckt + 1, Struct_arg->CLIENT_REQUEST.handle, 
                          3, mem_buffer);

        printf("Showing FAT1 contents (Page %d of %d)\n%s", pckt + 1, total_Pckts, mem_buffer);
    }
}

//*********************************Showsector**************************************************************
//This show sector command will show the contents of that particular sector in hex dec format
//here also same the data is divided into packets when the data is exceeded morethan 512 bytes.
void ShowSectorCommand_Implementation(struct Client_Info_Struct *Struct_args) {
    
	//variables declaration and initialization
	unsigned char sector_data[512];
    struct udpmsg_reply Cli_reply;
	struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)Struct_args; 
	int sockfd = Struct_arg->UDP_ServerSocket;
	struct udpmsg_req *Client_Request_Buffer = &Struct_arg->CLIENT_REQUEST;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
	int Sector_Num = ntohs(Client_Request_Buffer->ext);
	char Total_Packet_Message[32];
	
    const char* SectorType = 
        (Sector_Num == 0) ? "BOOT" :
        (Sector_Num >= 1 && Sector_Num <= 9) ? "FAT1" :
        (Sector_Num >= 10 && Sector_Num <= 18) ? "FAT2" :
        (Sector_Num >= 19 && Sector_Num <= 32) ? "ROOT DIRECTORY" : "DATA";

    //Reading the sector data, if not found trigerring the send response
    off_t offset = Sector_Num * 512;
    if (lseek(Floppy_disk_img_pointer, offset, SEEK_SET) == -1 || 
        read(Floppy_disk_img_pointer, sector_data, 512) != 512) {
        snprintf((char*)Cli_reply.data, sizeof(Cli_reply.data), 
                 "Error: unable to read the sector %d", Sector_Num);
        //goto Send_Resp;
		memset(&Cli_reply, 0, sizeof(Cli_reply));
		Cli_reply.cmd = htons(6);
		Cli_reply.seq = Client_Request_Buffer->seq;
		Cli_reply.handle = Client_Request_Buffer->handle;
		Cli_reply.ext = htons(Sector_Num);
		sendto(sockfd, &Cli_reply, sizeof(Cli_reply), 0,
			   (struct sockaddr*)&client_address, Client_IPAddress_Len);
    }
	
	//writing the sector data into the mem_buffer first and then dividing the data into packets of size 512 bytes each.
	//then sending each packet one by one.
    char mem_buffer[4096];
    int TotalBytes_Written = snprintf(mem_buffer, sizeof(mem_buffer), 
                                 "Sector %d (%s) contents:\n\n", Sector_Num, SectorType);
    TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, sizeof(mem_buffer) - TotalBytes_Written,
                              "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n\n");

    for (int k = 0; k < 512; k += 16) {
        TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, sizeof(mem_buffer) - TotalBytes_Written,
                                  "%3x0", k / 16);
        for (int j = 0; j < 16; j++) {
            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, sizeof(mem_buffer) - TotalBytes_Written,
                                      " %02x", sector_data[k + j]);
        }
        TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, sizeof(mem_buffer) - TotalBytes_Written, "\n");
    }

    // the complete data of sector is written into total_byteswritten variable, now calculating the number of packets.
    int total_packets = (TotalBytes_Written + 511) / 512;
    snprintf(Total_Packet_Message, sizeof(Total_Packet_Message), "TOTAL_PACKETS: %d", total_packets);
    Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len, 
                      Struct_arg->CLIENT_REQUEST.cmd, Struct_arg->CLIENT_REQUEST.seq, Struct_arg->CLIENT_REQUEST.handle, 
                      total_packets-1, Total_Packet_Message);
					  
	printf("total packetsn to be send:%d\n", total_packets);
    for (int packet = 0; packet < total_packets; packet++) {
		//int TotalBytes_Written = snprintf(mem_buffer, MAX_RESPONSE_SIZE, 
        //                             "Sector Contents (Page %d of %d):", 
        //                             packet , total_packets - 1);
        memset(&Cli_reply, 0, sizeof(Cli_reply));
        Cli_reply.cmd = htons(3);
        Cli_reply.seq = htons(packet);
        Cli_reply.handle = (Client_Request_Buffer->handle);
        Cli_reply.ext = htons(4);

        int start_count = packet * 512;
        int length = (TotalBytes_Written - start_count > 512) ? 512 : TotalBytes_Written - start_count;
        memcpy(Cli_reply.data, mem_buffer + start_count, length);

        sendto(sockfd, &Cli_reply, sizeof(Cli_reply), 0,
               (struct sockaddr*)&client_address, Client_IPAddress_Len);
    }
    printf("%s\n", mem_buffer);
    return;

//this will be triggered when there occurs error in reading the sectors.
//Send_Resp:
   
}

//***************************************************showfile***************************************************

//this function is triggere when showfile command is traversing the floppy to find the file.
int FindFile_FAT12(int file_desc, const char *filename, uint32_t *StartCluster, uint32_t *file_size) {
    
	uint8_t Root_Directry[ROOT_DIRECTRY_ENTRIES * ENTRY_SIZE];
    lseek(file_desc, ROOT_DIR_START_SECTOR * SECTOR_SIZE, SEEK_SET);
    read(file_desc, Root_Directry, sizeof(Root_Directry));

	//the below code specifies that, it reads file in FAT and also checks the case sensitive for file names.
    //checks in all the clusters.
	//it returns 1 when the file is found.
	for (int k = 0; k < ROOT_DIRECTRY_ENTRIES; k++) {
        char Name_OF_ENTRY[12];
        memcpy(Name_OF_ENTRY, &Root_Directry[k * ENTRY_SIZE], 11);
        Name_OF_ENTRY[11] = '\0';
        printf("File Entry %d: %.11s\n", k, Name_OF_ENTRY);
        if (strncasecmp(Name_OF_ENTRY, filename, 11) == 0) {
            *StartCluster = *(uint16_t*)&Root_Directry[k * ENTRY_SIZE + 26];
            *file_size = *(uint32_t*)&Root_Directry[k * ENTRY_SIZE + 28];
            return 1;
        }
    }
    return 0;
}

uint32_t Fetch_NextCluster_FAT(int file_desc, uint32_t CurrCluster) {
    
	//variables declaration and initialization
	uint8_t fat_sector[SECTOR_SIZE];
    uint32_t OFFSET_FAT = CurrCluster * 3 / 2;
    uint32_t fat_sector_number = FAT_START_SECTOR + (OFFSET_FAT / SECTOR_SIZE);
    uint16_t FATEntry;
	
	lseek(file_desc, fat_sector_number * SECTOR_SIZE, SEEK_SET);
    read(file_desc, fat_sector, SECTOR_SIZE);
	
	//search for the next cluster in the FAT
	//if found returns the fat entry.
    if (CurrCluster % 2 == 0) {
        FATEntry = *(uint16_t*)&fat_sector[OFFSET_FAT % SECTOR_SIZE] & 0x0FFF;
    } else {
        FATEntry = *(uint16_t*)&fat_sector[OFFSET_FAT % SECTOR_SIZE] >> 4;
    }

    return FATEntry;
}

void Print_HEX_Dump_Format(const uint8_t *data, size_t hex_size) {
	
	//this function takes the data and size of hex dump.
	//and converts into hexdump and stores in the data var itself.
	size_t i;
	size_t j;
	for (i = 0; i < hex_size; i += 16) {
        printf("%041X: ", i);
        for (j = 0; j < 16; j++) {
            if (i + j < hex_size)
                printf("%02X ", data[i + j]);
            else
                printf("   ");
				if (j == 7) printf(" ");
        }
        printf(" |");
		
		//char c;
		
        for (j = 0; j < 16 && i + j < hex_size; j++) {
            //c = data[i + j];
            printf("%c", isprint(data[i + j]) ? data[i + j] : '.');
        }
        printf("|\n");
    }
}

void ShowFileCommand_Implementation(struct Client_Info_Struct *Struct_args) {
	
	int file_desc = open("floppy.img", O_RDONLY);
    if (file_desc == -1) {
        perror("Error occured while opening the floppy disk image");
        return;
    }
	
	//variable declaration and initialization
	const char *filename = "2006BH~1PDF";
    struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)Struct_args;
	int sockfd = Struct_arg->UDP_ServerSocket;
	socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
    struct udpmsg_req *Client_Request_Buffer = &Struct_arg->CLIENT_REQUEST;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    char mem_buffer[MAX_RESPONSE_SIZE];

    // Finding the file in the FAT file system.
	uint32_t file_size;
	uint32_t FAT_File_StartCluster;
	//searching the file in fat file system if the file is not found sending the response to client that file was not found.
    if (!FindFile_FAT12(file_desc, filename, &FAT_File_StartCluster, &file_size)) {
		//preparing the response.
		snprintf(mem_buffer, MAX_RESPONSE_SIZE, "File not found: %s", filename);
        Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
            Client_Request_Buffer->cmd, Client_Request_Buffer->seq, Client_Request_Buffer->handle,
            0, mem_buffer);
        printf("Error:File not found\n");
        close(file_desc);
        return;
    }
	
	//calculating the total number of packets that needs to be send.
	//it is calculated addition of file and sector sizes and divided with sector size.
    int total_packets = (file_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    char Total_Packet_Message[32];
	
    snprintf(Total_Packet_Message, sizeof(Total_Packet_Message), "TOTAL PACKETS: %d", total_packets);
    Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
                      Client_Request_Buffer->cmd, Client_Request_Buffer->seq, Client_Request_Buffer->handle,
                      total_packets, Total_Packet_Message);

    printf("Sent total packet count = %d for file: %s\n", total_packets, filename);

    // the below code specifes the packet are sent by cluster wise.
    uint32_t CurrCluster = FAT_File_StartCluster;
    uint32_t bytes_sent = 0;
    for (int packet = 0; packet < total_packets; packet++) {
        uint8_t sector_data[SECTOR_SIZE];
        uint32_t cluster_sector = DATA_START_SECTOR + (CurrCluster - 2) * SECTORS_PER_CLUSTER;
        off_t offset = lseek(file_desc, cluster_sector * SECTOR_SIZE, SEEK_SET);
        if (offset == -1) {
            perror("Error occured in seeking floppy disk image");
            close(file_desc);
            return;
        }
		//reading the sector data.
        ssize_t bytes_read = read(file_desc, sector_data, SECTOR_SIZE);
        if (bytes_read == -1) {
            perror("Error occured while reading the floppy disk image");
            close(file_desc);
            return;
        }

        //trigger the function to convert the sector data into the hex dump 
		int TotalBytes_Written = 0;
        int bytes_to_send = (bytes_sent + SECTOR_SIZE > file_size) ? file_size - bytes_sent : SECTOR_SIZE;
        for (int i = 0; i < bytes_to_send; i += 16) {
			//writing the data into total_byteswritten
            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
                                      "%04X: ", i);
            for (int j = 0; j < 16 && i + j < bytes_to_send; j++) {
                TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
                                          "%02X ", sector_data[i + j]);
            }
            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written, " ");
            for (int j = 0; j < 16 && i + j < bytes_to_send; j++) {
                char c = sector_data[i + j];
                TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
                                          "%c", (c >= 32 && c <= 126) ? c : '.');
            }
            TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written, "\n");
        }

        // send the packet to the client
        Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
                          Client_Request_Buffer->cmd, packet + 1, Client_Request_Buffer->handle,
                          Client_Request_Buffer->cmd, mem_buffer);

        printf("Sent packet %d of %d for file: %s\n", packet + 1, total_packets, filename);
		Print_HEX_Dump_Format(sector_data, bytes_to_send);
        bytes_sent += bytes_to_send;
        if (bytes_sent >= file_size) break;

        // after sending the data if the next cluster is there, then it moves to the next cluster
        if ((packet + 1) % SECTORS_PER_CLUSTER == 0) {
            CurrCluster = Fetch_NextCluster_FAT(file_desc, CurrCluster);
            if (CurrCluster >= 0xFF8) break;
        }
    }
	printf("completed reading the file contents\n");
    close(file_desc);
}

//*************************************Traverse****************************************************
 //the below function will show the long list of contents present in the root direcvtory.
//it prints the detailed information of each file or the directory by showing then modification time,
//and file name, and ID.
void Long_View_Traverse_Command_Implementation(struct Client_Info_Struct *args, int Show_Long_View)
{
	int file_desc = open("floppy.img", O_RDONLY);
    if (file_desc == -1) {
        perror("Error occured while opening floppy disk image");
        return;
    }
	
	//printf("traverseelong\n");
	//Variable initialization and declaration.
	int TotalBytes_Written = 0;
	uint8_t Root_Directry[ROOT_DIRECTRY_ENTRIES * ENTRY_SIZE];
    struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)args;
    int sockfd = Struct_arg->UDP_ServerSocket;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
    char mem_buffer[MAX_RESPONSE_SIZE];
	
	//reading the floppy disk contents using lseek() and read()
    lseek(file_desc, ROOT_DIR_START_SECTOR * SECTOR_SIZE, SEEK_SET);
    read(file_desc, Root_Directry, sizeof(Root_Directry));

	//Displaying the file notations of all hidden, archive and system file and read only files.

	if (Show_Long_View)
	{
		TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
								  "*****************************\n"
								  "** FILE ATTRIBUTE NOTATION **\n"
								  "** **\n"
								  "** R ------ READ ONLY FILE **\n"
								  "** S ------ SYSTEM FILE **\n"
								  "** H ------ HIDDEN FILE **\n"
								  "** A ------ ARCHIVE FILE **\n"
								  "*****************************\n\n");
	}

	for (int i = 0; i < ROOT_DIRECTRY_ENTRIES; i++)
	{
		uint8_t *entry = &Root_Directry[i * ENTRY_SIZE];
		char filename[13];
		memcpy(filename, entry, 11);
		filename[11] = '\0';

		//below if condition checks the empty and deleted entries. it just ignores these entries.
		if (filename[0] != 0x00 && filename[0] != 0xE5)
		{ 
			for (int j = 10; j >= 0 && filename[j] == ' '; j--)
			{
				filename[j] = '\0';
			}

			uint8_t attributes = entry[11];
			char attr[6] = "-----";

			if (attributes & 0x01)
				attr[0] = 'R';
			if (attributes & 0x02)
				attr[1] = 'H';
			if (attributes & 0x04)
				attr[2] = 'S';
			if (attributes & 0x20)
				attr[3] = 'A';

			uint16_t time = *(uint16_t *)(entry + 22);
			uint16_t date = *(uint16_t *)(entry + 24);
			int year = ((date >> 9) & 0x7F) + 1980;
			int month = (date >> 5) & 0x0F;
			int day = date & 0x1F;
			int hour = (time >> 11) & 0x1F;
			int minute = (time >> 5) & 0x3F;
			int second = (time & 0x1F) * 2;

			uint32_t file_size = *(uint32_t *)(entry + 28);
			uint16_t StartCluster = *(uint16_t *)(entry + 26);

			//the below if condition will only include archive and directory entries.
			if ((attributes & 0x20) || (attributes & 0x10))
			{
				TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
										  "%s %02d/%02d/%04d %02d:%02d:%02d %10u /%s%s%10u\n",
										  attr, month, day, year, hour, minute, second,
										  file_size, filename, (attributes & 0x10) ? " < DIR >" : "", StartCluster);
			}

			if (strcmp(filename, "CIS620") == 0)
			{
				TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
										  "----- %02d/%02d/%04d %02d:%02d:%02d < DIR > /CIS620/. %u\n",
										  month, day, year, hour, minute, second, StartCluster);
				TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
										  "----- %02d/%02d/%04d %02d:%02d:%02d < DIR > /CIS620/.. 0\n",
										  month, day, year, hour, minute, second);
			} 

			//here i am sending the dtaa in packets. because the client can receive only 512 bytes .
			//so the data is divided into multiple packets and each packet is sent to the reciever.
			//when the buffer capacity is exceeded then the packet is sent.

			if (TotalBytes_Written > MAX_RESPONSE_SIZE - 100)
			{
				Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
								  args->CLIENT_REQUEST.cmd, args->CLIENT_REQUEST.seq,
								  args->CLIENT_REQUEST.handle, 0, mem_buffer);
				TotalBytes_Written = 0;
			}
		}
	}

	 //after sending all the packets, if the data is left then that is also sent as last packet
	if (TotalBytes_Written > 0)
	{
		Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
						  args->CLIENT_REQUEST.cmd, args->CLIENT_REQUEST.seq,
						  args->CLIENT_REQUEST.handle, 0, mem_buffer);
	}

	close(file_desc);
}

//the below function will show the list of contents present in the root direcvtory.
//it just prints the contents liek files and directories present in root.
void Traverse_Command_Implementation(struct Client_Info_Struct *Struct_args) {
	
	//printf("traversee\n");
	int file_desc = open("floppy.img", O_RDONLY);
    if (file_desc == -1) {
        perror("Error occured while opening floppy disk image");
        return;
    }
	
    //Variables Declaration and Initialization
	struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)Struct_args;
    int sockfd = Struct_arg->UDP_ServerSocket;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
    char mem_buffer[MAX_RESPONSE_SIZE];
    uint8_t Root_Directry[ROOT_DIRECTRY_ENTRIES * ENTRY_SIZE];
    lseek(file_desc, ROOT_DIR_START_SECTOR * SECTOR_SIZE, SEEK_SET);
    read(file_desc, Root_Directry, sizeof(Root_Directry));

    int TotalBytes_Written = 0;

	//below for loop will read the root directory entries and prints all the filename and directories.
	//but here it iwll skip the delete entries.
    for (int i = 0; i < ROOT_DIRECTRY_ENTRIES; i++) {
		char filename[13];
        uint8_t *File_Entry = &Root_Directry[i * ENTRY_SIZE];
        memcpy(filename, File_Entry, 11);
        filename[11] = '\0';

        if (filename[0] != 0x00 && filename[0] != 0xE5) {
            for (int k = 10; k >= 0 && filename[k] == ' '; k--) {
                filename[k] = '\0';
            }

            uint8_t attr = File_Entry[11];
			//the below if condition specifies that the file entries are archive qand root directories
			//preparing the response to sent it to the client.
            if ((attr & 0x20) || (attr & 0x10)) {
                TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
                    "/%s%s\n", filename, (attr & 0x10) ? " < DIR >" : "");
				//specifying if it directory, to notfy keeping dots.
                if (attr & 0x10) {
                    TotalBytes_Written += snprintf(mem_buffer + TotalBytes_Written, MAX_RESPONSE_SIZE - TotalBytes_Written,
                        "/%s/. < DIR >\n/%s/.. < DIR >\n", filename, filename);
                } 
            }

            //here i am sending the dtaa in packets. because the client can receive only 512 bytes .
			//so the data is divided into multiple packets and each packet is sent to the reciever.
			//when the buffer capacity is exceeded then the packet is sent.
            if (TotalBytes_Written > MAX_RESPONSE_SIZE - 100) {
                Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
                    Struct_arg->CLIENT_REQUEST.cmd, Struct_arg->CLIENT_REQUEST.seq,
                    Struct_arg->CLIENT_REQUEST.handle, 0, mem_buffer);
                TotalBytes_Written = 0;
            }
        }
    }

	//after sending all the packets, if the data is left then that is also sent as last packet.
    if (TotalBytes_Written > 0) {
        Send_Response_To_Client(sockfd, &client_address, Client_IPAddress_Len,
            Struct_arg->CLIENT_REQUEST.cmd, Struct_arg->CLIENT_REQUEST.seq,
            Struct_arg->CLIENT_REQUEST.handle, 0, mem_buffer);
    }
    close(file_desc);
}


void* Serve_client_req(void* client_args){
	 
	//variables declaration
	struct Client_Info_Struct *Struct_arg = (struct Client_Info_Struct*)client_args;
	struct udpmsg_reply client_reply;
	bool client_check;
	

	memset(&client_reply, 0, sizeof(client_reply));
	
	printf("----------------------------------------------------------\n");

	//assigning the client request values into variables for easy sending of arguments intoother funcs.
    int socket_pointer = Struct_arg->UDP_ServerSocket;
    struct sockaddr_in client_address = Struct_arg->CLIENT_ADDRESS;
    struct udpmsg_req Client_Request_Buffer = Struct_arg->CLIENT_REQUEST;
	socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
	
	//fetching the request details from which IP and port number for future verification of client.
	char Client_IP_Address[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(client_address.sin_addr), Client_IP_Address, INET_ADDRSTRLEN);
	unsigned short client_port = ntohs(client_address.sin_port);
	printf("Received request from IP address: %s, and Port: %u\n", Client_IP_Address, client_port);
	
	unsigned short switch_command = ntohs(Client_Request_Buffer.cmd);
	//the below switch case is for handling the request that is connections req or disconnect from floppy or the server or datare from client.
	int flag_total_clients;
	switch(switch_command){
		case 1:
			//printf("entered into case handling\n");
			flag_total_clients = Check_total_Client_connections(socket_pointer, client_address, client_port, Client_IPAddress_Len);
			if(flag_total_clients != -1){
				
				int floppy_flag = mountfloppydisk();
				
				if(floppy_flag != -1){
					strcpy((char *)client_reply.data, "Connection is established between client and server.");
				}
				else{
					strcpy((char *)client_reply.data, "mounting to floppy disk is failed.");
				}
				
				client_reply.cmd = htons(1);
				client_reply.seq = htons(0);
				client_reply.handle = htons(flag_total_clients);
				client_reply.ext = htons(0);
				
				printf("handle is : %d\n", flag_total_clients);
				
				//send_resp_to_client(socket_pointer, &client_reply, client_address, Client_IPAddress_Len);
				printf("connection established and floppy disk is mounted.\n");
			}
			else{
				client_reply.cmd = htons(1);
				client_reply.seq = htons(0);
				client_reply.handle = htons(0);
				client_reply.ext = htons(0);
				strcpy((char *)client_reply.data, "maximum limit reached, try after some time.");
				printf("max limited reached\n");
			}
			sendto(socket_pointer, &client_reply, sizeof(client_reply), 0,
					 (struct sockaddr *)&client_address, Client_IPAddress_Len);
			//Send_Reponse_to_Client(Struct_arg);
			break;
		case 2:
			client_check = Check_Client_EXISTS(Struct_arg, client_port);
			if(client_check){
				printf("removed client connection \n");
				Remove_Client_Connection(socket_pointer, ntohs(Client_Request_Buffer.handle), client_address, client_port, Client_IPAddress_Len);
				strcpy((char *)client_reply.data, "Removed client connection.");
			}else{
				printf("client did not found \n");
				strcpy((char *)client_reply.data, "client did not found to remove the connection.");
			}
			int handler = htons(Client_Request_Buffer.handle);
			client_reply.cmd = htons(2);
			client_reply.seq = htons(0);
			client_reply.handle = ntohs(handler);
			client_reply.ext = htons(0);
			
			sendto(socket_pointer, &client_reply, sizeof(client_reply), 0,
					 (struct sockaddr *)&client_address, Client_IPAddress_Len);
			//send_resp_to_client(socket_pointer, &client_reply, client_address, Client_IPAddress_Len);
			break;
		case 3:
			//printf("Extension: %u\n", ntohs(Client_Request_Buffer.ext));
			client_check = Check_Client_EXISTS(Struct_arg, client_port);
			//int ext = htons(Client_Request_Buffer.ext);
			//printf("ext: %d\n", ext);
			if(client_check){
				switch(ntohs(Client_Request_Buffer.ext)){
					case 1:
						List_Structure_Of_Floppy_Disk(Struct_arg);
						break;
					case 2:
						//printf("traverse\n");
						Traverse_Command_Implementation(Struct_arg);
						break;
					case 3:
						//printf("traverselong\n");
						Long_View_Traverse_Command_Implementation(Struct_arg, 1);
						break;
					case 4:
						ShowFatCommand_Implementation(Struct_arg);
						break;
					case 6:
						ShowFileCommand_Implementation(Struct_arg);
						break;
					case 10:
						ShowSectorCommand_Implementation(Struct_arg);
					default:
						perror("Requesting data service where this has not capable of handling");
				}
			} 
			else{
				perror("there is no connection between the client and server");
			}
			break;
		default:
			perror("Requesting some other service where this has not capable of handling");
	}
	free(Struct_arg);
	return NULL;
}

int main(){
	
	//variables declaration
	int UDP_SERVER_SOCKET;
	struct sockaddr_in server_IP_address;
	
	// Below code is for socket creation, using socket() method. Here SOCK_DRAM defines the UDP protocol and AF_INET defines the IPV4 family.
	UDP_SERVER_SOCKET = socket(AF_INET, SOCK_DGRAM, 0);
	if(UDP_SERVER_SOCKET < 0){
		perror("Creation of UDP socket is failed \n");
		exit(EXIT_FAILURE);
	}
	printf("UDP server socket created successfully \n");
	
	memset(&server_IP_address, 0, sizeof(server_IP_address));
	//memset(Client_Struct, 0, sizeof(Client_Struct));
	//the below code defines the binding the socket.
	//binding is important before receiving the requests.
	//binding the socket with IP address and port number. When the socket is binded then the server will listen to incoming request on the specified port number.
	
	server_IP_address.sin_family = AF_INET;
	//inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);
	server_IP_address.sin_addr.s_addr = INADDR_ANY;		//accept request from any IP address.
	server_IP_address.sin_port = htons(PORT);				//htons() means convert the port to network byte order.
	
	//bind() means it assigns the address to socket.
	int binding_socket = bind(UDP_SERVER_SOCKET, (struct sockaddr *)&server_IP_address, sizeof(server_IP_address));
	if(binding_socket < 0){
		perror("failed when binding the socket. \n");
		close(UDP_SERVER_SOCKET);
		exit(EXIT_FAILURE);
	}
	
	printf("socket is binded successfully.\n");
	
	//handling the requests received from Client_Struct.
	printf("server is running on the port number: %d \n", PORT);
	
	while(1){
		
		pthread_t pthread_id;
		struct Client_Info_Struct *Struct_arg = malloc(sizeof(struct Client_Info_Struct));
		socklen_t Client_IPAddress_Len = sizeof(Struct_arg->CLIENT_ADDRESS);
		Struct_arg->UDP_ServerSocket = UDP_SERVER_SOCKET;
		
		//receive the request from Client_Struct using recvfrom().
		Struct_arg->Received_Request_Len = recvfrom(UDP_SERVER_SOCKET, (void *)&(Struct_arg->CLIENT_REQUEST), BUFFER_SIZE, 0, (struct sockaddr *)&Struct_arg->CLIENT_ADDRESS, &Client_IPAddress_Len);
		if (Struct_arg->Received_Request_Len < 0) {
			perror("Error occured in recieving the data \n");
			free(Struct_arg);	//freeing the arguments, i mean the client info, because to free the memory bcz the reqis not successful.
			continue;
		}
		
		//creating the thread and handling each request asynchronously.
		int thread_creation = pthread_create(&pthread_id, NULL, Serve_client_req, Struct_arg);
		
		if(thread_creation != 0){
			perror("error occured while creating the pthread \n");
			return 1;
		}
		printf("Client request completed\n");
		pthread_detach(pthread_id);
	}
	
	close(UDP_SERVER_SOCKET);
	return 0;
}


