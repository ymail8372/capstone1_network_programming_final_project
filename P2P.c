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

// thread에 전송할 구조체 선언
struct thread_arg {
	struct addr_index receiving_peer_addr_index;
	int* receiving_peer_socks;
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

void* receiving_peer_connection_thread_handler(void* arg) {
	struct addr_index receiving_peer_addr_index = ((struct thread_arg*)arg)->receiving_peer_addr_index;
	int* receiving_peer_socks = ((struct thread_arg*)arg)->receiving_peer_socks;
	
	printf("receiving peer IP : %s\n", inet_ntoa(receiving_peer_addr_index.addr.sin_addr));
	printf("receiving peer port : %d\n", ntohs(receiving_peer_addr_index.addr.sin_port));
	printf("receiving peer index : %d\n", receiving_peer_addr_index.index);
	
	/* thread에게 주어진 receiving peer와 connect */
	int sock;
	struct sockaddr_in addr;
	socklen_t addr_size = sizeof(addr);
	
	// socket()
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		printf("[Error] socket() in thread\n");
		exit(-1);
	}
	if (connect(sock, (struct sockaddr*) &receiving_peer_addr_index.addr, sizeof(receiving_peer_addr_index.addr)) != -1) {
		printf("%d index connected!\n", receiving_peer_addr_index.index);
	}
	
	// 연결하려는 receiving_peer에게 my_index 전달
	write(sock, &my_index, sizeof(my_index));
	
	// receiving_peer_socks 업데이트
	printf("receiving_peer_socks[%d] = %d\n", receiving_peer_addr_index.index, sock);
	receiving_peer_socks[receiving_peer_addr_index.index] = sock;
	
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
		server_addr.sin_addr.s_addr = inet_addr("203.252.112.31");
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
		
		/* accept()된 receiving peer에게 listeing_addr 수신받고 receiving_peer_num, file_name, segment_size, my_index 전송 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// accept()
			client_sock[i] = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_size);
			if (client_sock[i] == -1) {
				printf("[Error] accept()\n");
				exit(-1);
			}
			printf("client sock fd : %d\n", client_sock[i]);
			
			struct sockaddr_in receiving_peer_listening_addr;
			read(client_sock[i], &receiving_peer_listening_addr, sizeof(receiving_peer_listening_addr));
			
			// receiving_peer_num 전송
			write(client_sock[i], &receiving_peer_num, sizeof(receiving_peer_num));
			
			// file_name 크기 전송
			size_t file_name_size = strlen(file_name)+1;
			write(client_sock[i], &file_name_size, sizeof(file_name_size));
			
			// file_name 전송
			write(client_sock[i], file_name, file_name_size);
			
			// segment_size 전송
			write(client_sock[i], &segment_size, sizeof(segment_size));
			
			write(client_sock[i], &i, sizeof(i));
			
			// receiving_peer_addr_index_arr에 client의 index, addr 추가
			receiving_peer_addr_index_arr[i].index = i;
			receiving_peer_addr_index_arr[i].addr = receiving_peer_listening_addr;
		}
		
		/* 모든 receiving peer가 연결되면 모든 receiving peer에게 모든 receiving peer의 주소, index 전송 */
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// receiving_peer_addr_index_arr 전송
			write(client_sock[i], receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
		}
		
		///* 각 receiving peer와의 통신을 처리하는 thread 생성 */
		//for (int i = 0; i < receiving_peer_num; i ++) {
		//	pthread_t thread_id[receiving_peer_num];
		//	pthread_create(&thread_id[i], NULL, sending_peer_thread_handler, (void*) &client_sock[i]);
		//}
		
		///* receiving peer들 사이의 connection 대기 */
		//while (true) {
		//	if (connected_peer_num_for_sending_peer == receiving_peer_num) {
		//		printf("ALL CONNECTION ESTABLISHED\n\n");
		//	}
		//}
		
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
		
		/* listening socket 만들고 sending peer에게 전달 */
		int listening_sock;
		struct sockaddr_in listening_addr, receiving_peer_addr;
		socklen_t receiving_peer_addr_size = sizeof(receiving_peer_addr), listening_addr_size = sizeof(listening_addr);
		
		// listening_sock 생성
		listening_sock = socket(AF_INET, SOCK_STREAM, 0);
		
		// listening_addr 설정
		memset(&listening_addr, 0, sizeof(listening_addr));
		listening_addr.sin_family = AF_INET;
		listening_addr.sin_addr.s_addr = inet_addr("203.252.112.31");
		listening_addr.sin_port = htons(0);
		
		// listening_sock bind()
		if (bind(listening_sock, (struct sockaddr*) &listening_addr, sizeof(listening_addr)) == -1) {
			printf("[Error] listening_sock bind()\n");
			exit(-1);
		}
		
		// bind된 IP, 포트를 listening_addr에 저장
		getsockname(listening_sock, (struct sockaddr*) &listening_addr, &listening_addr_size);
		printf("listening_addr_ip : %s\n", inet_ntoa(listening_addr.sin_addr));
		printf("listening_addr_port : %d\n", ntohs(listening_addr.sin_port));
		
		// listening_sock listen()
		if (listen(listening_sock, receiving_peer_num) == -1) {
			printf("[Error] listening_sock listen()\n");
			exit(-1);
		}
		
		// sending peer에게 listening_addr 전달
		write(sock, &listening_addr, sizeof(listening_addr));
		
		/* receiving_peer_num, file_name, segment_size, my_index, my_index 수신 */
		// receiving_peer_num 수신
		read(sock, &receiving_peer_num, sizeof(receiving_peer_num));
		printf("receiving_peer_num : %ld\n", receiving_peer_num);
		
		// file_name_size 수신
		size_t file_name_size;
		read(sock, &file_name_size, sizeof(file_name_size));
		printf("file_name_size : %ld\n", file_name_size);
		
		// file_name 수신
		size_t received_size = 0, read_size = 0;
		char file_name[FILE_NAME_SIZE] = "";
		while (received_size < file_name_size) {
			read_size = read(sock, file_name, file_name_size);
			received_size += read_size;
		}
		printf("file_name : %s\n", file_name);
		
		// segment_size 수신
		read(sock, &segment_size, sizeof(segment_size));
		printf("segment_size : %ld\n", segment_size);
		
		// my_index 수신
		read(sock, &my_index, sizeof(my_index));
		printf("my_index : %d\n", my_index);
		
		/* receiving peer의 listening addr, index (receiving_peer_addr_index_arr) 수신 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		received_size = 0;
		read_size = 0;
		while (received_size < sizeof(receiving_peer_addr_index_arr)) {
			read_size = read(sock, receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
			received_size += read_size;
		}
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			printf("receiving_peer_addr_index_arr[%d]'s port : %d\n", i, ntohs(receiving_peer_addr_index_arr[i].addr.sin_port));
			printf("receiving_peer_addr_index_arr[%d]'s index : %d\n", i, receiving_peer_addr_index_arr[i].index);
		}
		
		// receiving_peer_socks 선언
		// receiving_peer_socks[해당 receiving_peer의 index] = 해당 receiving_peer의 sock
		int* receiving_peer_socks = (int*) malloc(sizeof(int) * receiving_peer_num);
		
		// receiving_peer_socks 초기화
		for (int i = 0; i < receiving_peer_num; i ++) {
			receiving_peer_socks[i] = 0;
		}
		
		// my_index 보다 큰 index를 가진 receiving peer에게 connect()를 보내기 위한 thread 생성
		pthread_t thread_ids[receiving_peer_num];
		struct thread_arg arg[(receiving_peer_num - 1) - my_index];
		
		for (int i = my_index + 1; i < receiving_peer_num; i ++) {
			
			// thread에 전달할 내용 저장
			arg[i - (my_index + 1)].receiving_peer_addr_index = receiving_peer_addr_index_arr[i];
			arg[i - (my_index + 1)].receiving_peer_socks = receiving_peer_socks;
			printf("create pthread %d\n", i);
			
			// thread 생성
			pthread_create(&thread_ids[i], NULL, receiving_peer_connection_thread_handler, &arg[i - (my_index + 1)]);
		}
		
		// my_index 보다 작은 index를 가진 receiving peer의 connect()를 수신
		size_t connected_receiving_peer = 0;
		while(true) {
			
			// listening socket이 받아야 하는 connect() 요청을 모두 처리한 경우
			if (connected_receiving_peer == my_index) {
				printf("listening socket complete\n");
				break;
			}
			
			// accept()
			int receivinig_peer_sock = accept(listening_sock, (struct sockaddr*) &receiving_peer_addr, &receiving_peer_addr_size);
			printf("receivinig_peer_sock : %d\n", receivinig_peer_sock);
			
			// 연결된 receiving_peer의 index 수신
			int receiving_peer_index = 0;
			read(receivinig_peer_sock, &receiving_peer_index, sizeof(receiving_peer_index));
			
			// receiving_peer_socks 업데이트
			receiving_peer_socks[receiving_peer_index] = receivinig_peer_sock;
			
			// connected_receiving_peer 업데이트
			connected_receiving_peer += 1;
		}
		
		// receiving_peer_connection_thread_handler thread 종료 대기
		for (int i = my_index + 1; i < receiving_peer_num; i ++) {
			pthread_join(thread_ids[i], NULL);
		}
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			printf("receiving_peer_socks[%d(index)] : %d\n", i, receiving_peer_socks[i]);
		}
		
		free(receiving_peer_socks);
	}
	
	return 0;
}
