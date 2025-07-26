#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/epoll.h>

#define BUFSIZE 4096
#define FILE_NAME_SIZE 516

/* receiving peer에서 사용하는 전역 변수 */
size_t receiving_peer_num = 0;
char file_name[FILE_NAME_SIZE] = "";
size_t segment_size = 0;
int my_index = 0;

// addr_index struct 선언
struct addr_index {
	struct sockaddr_in addr;
	int index;
};

// sending peer가 하위 thread들이 다른 receiver들과 연결이 완료되었는지 확인하기 위한 변수
unsigned short connected_peer_num_for_sending_peer = 0;

void* sending_peer_thread_handler(void* argv) {
	int client_sock = *((int*)argv);
	
	printf("client sock : %d\n", client_sock);
	
	/* 이 thread가 관리하는 receiving peer의 연결 완료 여부 수신 + index 수신 */
	int my_index = 0;
	read(client_sock, &my_index, sizeof(my_index));
	connected_peer_num_for_sending_peer += 1;
	printf("[sending peer thread] all connection established\n");
	
	pthread_exit(NULL);
}

// receiving peer가 하위 thread들이 다른 receiver들과 연결이 완료되었는지 확인하기 위한 변수
unsigned short connected_peer_num_for_receiving_peer = 0;

void* receiving_peer_thread_handler(void* argv) {
	struct addr_index receiving_peer_addr_index = *((struct addr_index*)argv);
	
	printf("receiving peer IP : %s\n", inet_ntoa(receiving_peer_addr_index.addr.sin_addr));
	printf("receiving peer port : %d\n", ntohs(receiving_peer_addr_index.addr.sin_port));
	printf("receiving peer index : %d\n", receiving_peer_addr_index.index);
	
	/* thread에게 주어진 receiving peer와 connect */
	int server_sock, client_sock, sock;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	
	// 연결을 하는 socket
	if ((my_index + 1) % receiving_peer_num == receiving_peer_addr_index.index) {
		
		// socket()
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == -1) {
			printf("[Error] socket() in thread\n");
			exit(-1);
		}
		while (true) {
			if (connect(sock, (struct sockaddr*) &receiving_peer_addr_index.addr, sizeof(receiving_peer_addr_index.addr)) != -1) {
				printf("connected!\n");
				break;
			}
		}
	}
	// 연결을 기다리는 socket
	else {
		
		// socket()
		server_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (server_sock == -1) {
			printf("[Error] socket() in thread\n");
			exit(-1);
		}

		// server_addr 세팅
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(0);
		server_addr.sin_port = htons(0);
		
		// bind()
		if(bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
			printf("[Error] bind() in thread\n");
			exit(-1);
		}
		
		// listen()
		if(listen(server_sock, 1) == -1) {
			printf("[Error] listen() in thread\n");
			exit(-1);
		}
		
		// accept()
		client_sock = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_size);
		if (client_sock == -1) {
			printf("[Error] accept() in thread\n");
			exit(-1);
		}
		printf("accept\n");
	}
	
	// connected_peer_num 업데이트
	printf("thread connection established\n");
	connected_peer_num_for_receiving_peer += 1;
	printf("connected_peer_num_for_receiving_peer : %d\n", connected_peer_num_for_receiving_peer);
	
	while(true) {
		
	}
	
	pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    extern char *optarg;
    extern int optind; 
	int option = 0;
	
	/* option 받기 */
	// sending peer, receiving peer 구분
	bool isSendingPeer = false;
	option = getopt(argc, argv, "sr");
	
	// -s, -r 옵션을 받지 못했을 경우
	if (option == -1) {
		printf("[Error] Send argument\n");
		exit(-1);
	}
	switch(option) {
		case 's' :
			printf("Sending peer start\n");
			isSendingPeer = true;
			break;
			
		case 'r' :
			printf("Receiving peer start\n");
			isSendingPeer = false;
			break;
	}
	
	/* sending peer */
	if (isSendingPeer) {
		
		/* option 받기 */
		// sending peer option 받기
		while((option = getopt(argc, argv, "n:f:g:")) != -1) {
			switch(option) {
				case 'n' :
					if (isSendingPeer == false) {
						printf("[Error] Wrong option\n");
						exit(-1);
					}
					receiving_peer_num = atoi(optarg);
					printf("receiving_peer_num : %ld\n", receiving_peer_num);
					break;
					
				case 'f' :
					if (isSendingPeer == false) {
						printf("[Error] Wrong option\n");
						exit(-1);
					}
					strcpy(file_name, optarg);
					printf("file_name : %s\n", file_name);
					break;
					
				case 'g' :
					if (isSendingPeer == false) {
						printf("[Error] Wrong option\n");
						exit(-1);
					}
					segment_size = atoi(optarg);
					printf("segment_size : %ld\n", segment_size);
					break;
			}
		}
		
		/* receiving_peer_num 만큼 connect() 요청 받기 */
		int server_sock, client_sock[receiving_peer_num];
		struct sockaddr_in server_addr, client_addr;
		socklen_t server_addr_size = sizeof(server_addr), client_addr_size = sizeof(client_addr);
		
		// socket()
		server_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (server_sock == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// server_addr 세팅
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(0);
		server_addr.sin_port = htons(0);
		
		// bind()
		if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// bind된 IP, 포트 표시
		getsockname(server_sock, (struct sockaddr*) &server_addr, &server_addr_size);
		printf("Usage port : %d\n", ntohs(server_addr.sin_port));
		
		// listen
		if (listen(server_sock, receiving_peer_num) == -1) {
			printf("[Error] listen()\n");
			exit(-1);
		}
		
		/* accept()된 receiving peer에게 receiving_peer_num, file_name, segment_size 전송 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// accept()
			client_sock[i] = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_size);
			if (client_sock[i] == -1) {
				printf("[Error] accept()\n");
				exit(-1);
			}
			printf("client sock fd : %d\n", client_sock[i]);
			
			// receiving_peer_num 전송
			write(client_sock[i], &receiving_peer_num, sizeof(receiving_peer_num));
			
			// file_name 크기 전송
			size_t file_name_size = strlen(file_name);
			write(client_sock[i], &file_name_size, sizeof(file_name_size));
			
			// file_name 전송
			write(client_sock[i], file_name, file_name_size);
			
			// segment_size 전송
			write(client_sock[i], &segment_size, sizeof(segment_size));
			
			// receiving_peer_addr_index_arr에 client의 index, addr 추가
			receiving_peer_addr_index_arr[i].index = i;
			receiving_peer_addr_index_arr[i].addr = client_addr;
		}
		
		/* 모든 receiving peer가 연결되면 모든 receiving peer에게 모든 receiving peer의 주소, index 전송 */
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// receiving_peer_addr_index_arr 전송
			write(client_sock[i], receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
		}
		
		/* 각 receiving peer와의 통신을 처리하는 thread 생성 */
		for (int i = 0; i < receiving_peer_num; i ++) {
			pthread_t thread_id[receiving_peer_num];
			pthread_create(&thread_id[i], NULL, sending_peer_thread_handler, (void*) &client_sock[i]);
		}
		
		/* receiving peer들 사이의 connection 대기 */
		while (true) {
			if (connected_peer_num_for_sending_peer == receiving_peer_num) {
				printf("ALL CONNECTION ESTABLISHED\n\n");
			}
		}
		
		while (true) {
			
		}
	}
	
	/* receiving peer */
	else {
	
		/* option 받기 */
		// receiving peer 변수
		char ip_addr[16] = "";
		unsigned short port = 0;
		
		// receiving peer option 받기
		while((option = getopt(argc, argv, "a:p:")) != -1) {
			switch(option) {
				case 'a' :
					if (isSendingPeer == true) {
						printf("[Error] Wrong option\n");
						exit(-1);
					}
					strcpy(ip_addr, optarg);
					printf("ip_addr : %s\n", ip_addr);
					break;
					
				case 'p' :
					if (isSendingPeer == true) {
						printf("[Error] Wrong option\n");
						exit(-1);
					}
					port = atoi(optarg);
					printf("port : %d\n", port);
					break;
			}
		}
		
		/* socket 생성 및 sending peer로 connect */
		int sock;
		struct sockaddr_in server_addr;
		
		// socket()
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// server_addr 세팅
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr(ip_addr);
		server_addr.sin_port = htons(port);
		
		// connet()
		if (connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
			printf("[Error] connect()\n");
			exit(-1);
		}
		
		/* receiving_peer_num, file_name, segment_size 수신 */
		// receiving_peer_num 수신
		read(sock, &receiving_peer_num, sizeof(receiving_peer_num));
		printf("receiving_peer_num : %ld\n", receiving_peer_num);
		
		// file_name_size 수신
		size_t file_name_size;
		read(sock, &file_name_size, sizeof(file_name_size));
		printf("file_name_size : %ld\n", file_name_size);
		
		// file_name 수신
		size_t received_size, read_size;
		char file_name[FILE_NAME_SIZE] = "";
		while (received_size < file_name_size) {
			read_size = read(sock, file_name, file_name_size);
			received_size += read_size;
		}
		printf("file_name : %s\n", file_name);
		
		// segment_size 수신
		read(sock, &segment_size, sizeof(segment_size));
		printf("segment_size : %ld\n", segment_size);
		
		/* receiving peer의 IP, port, index(receiving_peer_addr_index_arr) 수신 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		received_size = 0;
		read_size = 0;
		while (received_size < sizeof(receiving_peer_addr_index_arr)) {
			read_size = read(sock, receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
			received_size += read_size;
		}
		
		/*
		sending peer가 전해주는 주소는 "thread"의 socket 주소가 아니라 "receiving peer"의 주소이기 때문에 thread를 만들기 전에
		다른 receiving peer들과의 연결을 하고 socket 번호를 thread에게 주어야 한다.
		*/
	
		/* 각 receiving peer와 통신할 thread 생성 및 각 receiving peer와 connect */
		// my_addr에 자신의 socket이 bind된 주소 저장
		struct sockaddr_in my_addr;
		socklen_t my_addr_size = sizeof(my_addr);
		getsockname(sock, (struct sockaddr*) &my_addr, &my_addr_size);
		printf("my_addr IP : %s\n", inet_ntoa(my_addr.sin_addr));
		printf("my_addr port : %d\n\n", ntohs(my_addr.sin_port));
		
		// thread 생성
		pthread_t thread_id[receiving_peer_num - 1];
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// 자신의 socket이 bind된 주소일 경우는 제외하고 thread 생성
			if ((receiving_peer_addr_index_arr[i].addr.sin_addr.s_addr != my_addr.sin_addr.s_addr) || (receiving_peer_addr_index_arr[i].addr.sin_port != my_addr.sin_port)) {
				printf("IP : %s\n", inet_ntoa(receiving_peer_addr_index_arr[i].addr.sin_addr));
				printf("port : %d\n\n", receiving_peer_addr_index_arr[i].addr.sin_port);
				pthread_create(&thread_id[i], NULL, receiving_peer_thread_handler, (void*) &receiving_peer_addr_index_arr[i]);
			}
			
			// 자신의 index 저장
			else {
				my_index = receiving_peer_addr_index_arr[i].index;
			}
		}
		
		/* 하위 thread들이 각자에게 주어진 receiver peer와 연결이 완료된 경우 sending peer에 알림 */
		while(true) {
			if (connected_peer_num_for_receiving_peer == receiving_peer_num - 1) {
				write(sock, &my_index, sizeof(my_index));
				printf("connect complete!\n");
				break;
			}
		}
		
	
		while(true) {
			
		}
	}
	
	return 0;
}
