#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/epoll.h>

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

// receiving_peer_connection_thread_handler에 전송할 구조체 선언
struct receiving_peer_connection_thread_arg {
	struct addr_index receiving_peer_addr_index;
	int* receiving_peer_socks;
};

// sending_peer_thread_handler에 전송할 구조체 선언
struct sending_peer_data_thread_arg {
	int index;
	int sock;
};

// receiving_peer_data_thread에 전송할 구조체 선언
struct receiving_peer_data_thread_arg {
	int index;
	int sock;
	char* file_name_index;
};

// sending_peer_data_thread가 모두 끝났는지 확인하기 위한 용도
int sending_peer_data_thread_end_counter = 0;

void* sending_peer_data_thread_handler(void* arg) {
	int receiving_peer_index = ((struct sending_peer_data_thread_arg*)arg)->index;
	int receiving_peer_sock = ((struct sending_peer_data_thread_arg*)arg)->sock;
	
	printf("receiving_peer_sock : %d\n", receiving_peer_sock);
	printf("receiving_peer_index : %d\n", receiving_peer_index);
	
	/* receiving peer의 index에 맞는 file의 부분을 읽어서 전송 */
	FILE* fp = fopen(file_name, "rb");
	char buffer[segment_size];
	int file_size = 0, current_location = 0;
	
	// file_size 구하기
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	
	fclose(fp);
	fp = fopen(file_name, "rb");
	
	while(current_location <= file_size) {
		size_t read_size = 0;
		
		// buffer 초기화
		for(int i = 0; i < segment_size; i ++) {
			buffer[i] = 0;
		}
		
		// receiving peer의 index에 맞는 위치로 fp 이동
		fseek(fp, segment_size * receiving_peer_index, SEEK_CUR);
		current_location += segment_size * receiving_peer_index;
		
		// file 읽기
		read_size = fread(buffer, 1, segment_size, fp);
		current_location += read_size;
		
		// read_size 전송
		write(receiving_peer_sock, &read_size, sizeof(read_size));
		
		// file 전송
		write(receiving_peer_sock, buffer, read_size);
		
		// 다음 전송에서 알맞는 receiving peer의 index에 맞는 위치로 fp를 이동시키기 위해 fp 초기화 이동
		fseek(fp, segment_size * ((receiving_peer_num - 1) - receiving_peer_index), SEEK_CUR);
		current_location += segment_size * ((receiving_peer_num - 1) - receiving_peer_index);
	}
	
	printf("[%d] All Data Sended\n", receiving_peer_index);
	fclose(fp);
	close(receiving_peer_sock);
	
	sending_peer_data_thread_end_counter += 1;
	
	pthread_exit(NULL);
}

void* receiving_peer_connection_thread_handler(void* arg) {
	struct addr_index receiving_peer_addr_index = ((struct receiving_peer_connection_thread_arg*)arg)->receiving_peer_addr_index;
	int* receiving_peer_socks = ((struct receiving_peer_connection_thread_arg*)arg)->receiving_peer_socks;
	
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

// receiving_peer_data_thread가 모두 끝났는지 확인하기 위한 용도
int receiving_peer_data_thread_end_count = 0;

void* receiving_peer_data_thread_handler(void* arg) {
	int receiving_peer_index = ((struct receiving_peer_data_thread_arg*)arg)->index;
	int receiving_peer_sock = ((struct receiving_peer_data_thread_arg*)arg)->sock;
	char* file_name_index = ((struct receiving_peer_data_thread_arg*)arg)->file_name_index;
	
	printf("receiving_peer_sock : %d\n", receiving_peer_sock);
	printf("receiving_peer_index : %d\n", receiving_peer_index);
	printf("file_name_index : %s\n", file_name_index);
	
	/* receiving peer로부터 data 받아서 파일에 쓰기 */
	FILE* fp = fopen(file_name_index, "wb");
	char buffer[segment_size];
	
	while(true) {
		size_t total_size = 0, received_size = 0, read_size = 0;
		
		// buffer 초기화
		for(int i = 0; i < segment_size; i ++) {
			buffer[i] = 0;
		}
		
		// receiving_peer으로부터 파일 크기 수신
		read_size = read(receiving_peer_sock, &total_size, sizeof(total_size));
		
		// receiving_peer으로부터 파일 수신
		while(received_size < total_size) {
			read_size = read(receiving_peer_sock, buffer, total_size - received_size);
			received_size += read_size;
		}
		
		// end_signal을 수신한 경우
		if (strcmp(buffer, "^&*No More Data*&^") == 0) {
			printf("[%d] end signal\n", receiving_peer_index);
			fclose(fp);
			
			// receiving_peer_data_thread_end_count 업데이트
			receiving_peer_data_thread_end_count += 1;
			
			pthread_exit(NULL);
		}
		
		// 수신한 데이터를 file에 쓰기
		fwrite(buffer, 1, received_size, fp);
	}
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
					segment_size = segment_size * 1024;
					printf("segment_size : %ld\n", segment_size);
					break;
			}
		}
		
		/* receiving_peer_num 만큼 connect() 요청 받기 */
		int listening_sock, receiving_peer_sock[receiving_peer_num];
		struct sockaddr_in server_addr, client_addr;
		socklen_t server_addr_size = sizeof(server_addr), client_addr_size = sizeof(client_addr);
		
		// socket()
		listening_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (listening_sock == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// server_addr 세팅
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr("203.252.112.31");
		server_addr.sin_port = htons(0);
		
		// bind()
		if (bind(listening_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// bind된 IP, 포트 표시
		getsockname(listening_sock, (struct sockaddr*) &server_addr, &server_addr_size);
		printf("Usage port : %d\n", ntohs(server_addr.sin_port));
		
		// listen
		if (listen(listening_sock, receiving_peer_num) == -1) {
			printf("[Error] listen()\n");
			exit(-1);
		}
		
		/* accept()된 receiving peer에게 listeing_addr 수신받고 receiving_peer_num, file_name, segment_size, my_index 전송 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// accept()
			receiving_peer_sock[i] = accept(listening_sock, (struct sockaddr*) &client_addr, &client_addr_size);
			if (receiving_peer_sock[i] == -1) {
				printf("[Error] accept()\n");
				exit(-1);
			}
			printf("client sock fd : %d\n", receiving_peer_sock[i]);
			
			struct sockaddr_in receiving_peer_listening_addr;
			read(receiving_peer_sock[i], &receiving_peer_listening_addr, sizeof(receiving_peer_listening_addr));
			
			// receiving_peer_num 전송
			write(receiving_peer_sock[i], &receiving_peer_num, sizeof(receiving_peer_num));
			
			// file_name 크기 전송
			size_t file_name_size = strlen(file_name)+1;
			write(receiving_peer_sock[i], &file_name_size, sizeof(file_name_size));
			
			// file_name 전송
			write(receiving_peer_sock[i], file_name, file_name_size);
			
			// segment_size 전송
			write(receiving_peer_sock[i], &segment_size, sizeof(segment_size));
			
			write(receiving_peer_sock[i], &i, sizeof(i));
			
			// receiving_peer_addr_index_arr에 client의 index, addr 추가
			receiving_peer_addr_index_arr[i].index = i;
			receiving_peer_addr_index_arr[i].addr = receiving_peer_listening_addr;
		}
		
		/* 모든 receiving peer가 연결되면 모든 receiving peer에게 모든 receiving peer의 주소, index 전송 */
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// receiving_peer_addr_index_arr 전송
			write(receiving_peer_sock[i], receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
		}
		
		/* receiving peer의 connection_complete 수신 */
		for (int i = 0; i < receiving_peer_num; i ++) {
			bool connection_complete;
			read(receiving_peer_sock[i], &connection_complete, sizeof(connection_complete));
		}
		printf("All Connection Established!!!\n");
		
		/* 각 receiving peer와의 통신을 처리하는 thread 생성 */
		pthread_t thread_id[receiving_peer_num];
		
		struct sending_peer_data_thread_arg arg[receiving_peer_num];
		for (int i = 0; i < receiving_peer_num; i ++) {
			arg[i].index = i;
			arg[i].sock = receiving_peer_sock[i];
			
			pthread_create(&thread_id[i], NULL, sending_peer_data_thread_handler, (void*) &arg[i]);
		}
		
		while (true) {
			if (sending_peer_data_thread_end_counter == receiving_peer_num) {
				printf("Every threads end\n");
				break;
			}
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
		int sending_peer_sock;
		struct sockaddr_in server_addr;
		
		// socket()
		sending_peer_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sending_peer_sock == -1) {
			printf("[Error] socket()\n");
			exit(-1);
		}
		
		// server_addr 세팅
		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr(ip_addr);
		server_addr.sin_port = htons(port);
		
		// connet()
		if (connect(sending_peer_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
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
		write(sending_peer_sock, &listening_addr, sizeof(listening_addr));
		
		/* receiving_peer_num, file_name, segment_size, my_index, my_index 수신 */
		// receiving_peer_num 수신
		read(sending_peer_sock, &receiving_peer_num, sizeof(receiving_peer_num));
		printf("receiving_peer_num : %ld\n", receiving_peer_num);
		
		// file_name_size 수신
		size_t file_name_size;
		read(sending_peer_sock, &file_name_size, sizeof(file_name_size));
		printf("file_name_size : %ld\n", file_name_size);
		
		// file_name 수신
		size_t received_size = 0, read_size = 0;
		char file_name[FILE_NAME_SIZE] = "";
		while (received_size < file_name_size) {
			read_size = read(sending_peer_sock, file_name, file_name_size);
			received_size += read_size;
		}
		printf("file_name : %s\n", file_name);
		
		// segment_size 수신
		read(sending_peer_sock, &segment_size, sizeof(segment_size));
		printf("segment_size : %ld\n", segment_size);
		
		// my_index 수신
		read(sending_peer_sock, &my_index, sizeof(my_index));
		printf("my_index : %d\n", my_index);
		
		/* receiving peer의 listening addr, index (receiving_peer_addr_index_arr) 수신 */
		struct addr_index receiving_peer_addr_index_arr[receiving_peer_num];
		received_size = 0;
		read_size = 0;
		while (received_size < sizeof(receiving_peer_addr_index_arr)) {
			read_size = read(sending_peer_sock, receiving_peer_addr_index_arr, sizeof(receiving_peer_addr_index_arr));
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
		pthread_t connection_thread_ids[receiving_peer_num];
		struct receiving_peer_connection_thread_arg connection_arg[(receiving_peer_num - 1) - my_index];
		
		for (int i = my_index + 1; i < receiving_peer_num; i ++) {
			
			// thread에 전달할 내용 저장
			connection_arg[i - (my_index + 1)].receiving_peer_addr_index = receiving_peer_addr_index_arr[i];
			connection_arg[i - (my_index + 1)].receiving_peer_socks = receiving_peer_socks;
			printf("create pthread %d\n", i);
			
			// thread 생성
			pthread_create(&connection_thread_ids[i], NULL, receiving_peer_connection_thread_handler, &connection_arg[i - (my_index + 1)]);
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
			
			// 연결된 receiving_peer의 index 수신
			int receiving_peer_index = 0;
			read(receivinig_peer_sock, &receiving_peer_index, sizeof(receiving_peer_index));
			
			// receiving_peer_socks 업데이트
			receiving_peer_socks[receiving_peer_index] = receivinig_peer_sock;
			printf("receiving_peer_socks[%d] = %d\n", receiving_peer_index, receivinig_peer_sock);
			
			// connected_receiving_peer 업데이트
			connected_receiving_peer += 1;
		}
		printf("\n\n");
		
		// receiving_peer_connection_thread_handler thread 종료 대기
		for (int i = my_index + 1; i < receiving_peer_num; i ++) {
			pthread_join(connection_thread_ids[i], NULL);
		}
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			printf("receiving_peer_socks[%d(index)] : %d\n", i, receiving_peer_socks[i]);
		}
		
		/* sending peer에게 연결 완료 전송 */
		bool connection_complete = true;
		write(sending_peer_sock, &connection_complete, sizeof(connection_complete));
		
		/* 데이터를 저장할 파일의 이름 생성 */
		// 파일 이름 구조 설계 "temp<my_index>_<receiving_peer_index>.tmp"
		char file_name_list[receiving_peer_num][FILE_NAME_SIZE];
		
		char my_index_str[64] = "";
		sprintf(my_index_str, "%d", my_index);
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			char temp[FILE_NAME_SIZE] = "temp";
			
			strcat(temp, my_index_str);
			strcat(temp, "_");
			
			char index_str[64] = "";
			sprintf(index_str, "%d", i);
			
			strcat(temp, index_str);
			strcat(temp, ".tmp");
			
			strcpy(file_name_list[i], temp);
		}
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			printf("file_name_list[%d] %s\n", i, file_name_list[i]);
		}
		
		/* 각 receiving peer마다 thread 생성 */
		struct receiving_peer_data_thread_arg data_arg[receiving_peer_num];
		pthread_t data_thread_ids[receiving_peer_num];
		
		for (int i = 0; i < receiving_peer_num; i ++) {
			
			// my_index가 아닌 경우
			if (receiving_peer_socks[i] != 0) {
				
				data_arg[i].index = i;
				data_arg[i].sock = receiving_peer_socks[i];
				data_arg[i].file_name_index = file_name_list[i];
				// my_index일 경우 continue
				if (receiving_peer_socks[i] == 0) {
					continue;
				}
				
				// receiving_peer_data_thread 생성
				pthread_create(&data_thread_ids[i], NULL, receiving_peer_data_thread_handler, (void*) &data_arg[i]);
				pthread_detach(data_thread_ids[i]);
			}
		}
		
		FILE* fp = fopen(file_name_list[my_index], "wb");
		char buffer[segment_size];
		
		while(true) {
			size_t total_size = 0, received_size = 0, read_size = 0;
			
			// buffer 초기화
			for(int i = 0; i < segment_size; i ++) {
				buffer[i] = 0;
			}
			
			// sending_peer으로부터 파일 크기 수신
			read_size = read(sending_peer_sock, &total_size, sizeof(total_size));
			
			// sending_peer가 모든 데이터를 전송해서 socket close한 경우
			if (read_size == 0) {
				printf("Sending Peer closed\n");
				fclose(fp);
				close(sending_peer_sock);
				
				// receiving peer에게 end_signal 전송
				for(int j = 0; j < receiving_peer_num; j ++) {
					if (j != my_index) {
						char end_signal[25] = "^&*No More Data*&^";
						size_t end_signal_len = strlen(end_signal) + 1;
						
						// end_signal의 크기 전송
						write(receiving_peer_socks[j], &end_signal_len, sizeof(end_signal_len));
						
						// end_signal 전송
						write(receiving_peer_socks[j], end_signal, end_signal_len);
						
						printf("Send end signal to [%d]\n", receiving_peer_socks[j]);
					}
				}
				
				break;
			}
			
			// sending_peer으로부터 파일 data 수신
			while(received_size < total_size) {
				read_size = read(sending_peer_sock, buffer, total_size);
				received_size += read_size;
			}
			
			// 수신한 데이터를 file에 쓰기
			fwrite(buffer, 1, total_size, fp);
			
			// receiving peer들에게 echo
			for(int i = 0; i < receiving_peer_num; i ++) {
				
				// my_index를 제외한 모든 receiving_peer에게 data전송
				if (receiving_peer_socks[i] != 0) {
					
					// 파일 크기 전송
					write(receiving_peer_socks[i], &total_size, sizeof(total_size));
					
					// 파일 data 전송
					write(receiving_peer_socks[i], buffer, total_size);
				}
			}
		}
		
		printf("Enter to while()\n");
		
		/* 모든 receiving_peer_data_thread가 끝날 때까지 대기 */
		while(true) {
			if (receiving_peer_data_thread_end_count == receiving_peer_num - 1) {
				printf("Every threads end\n");
				break;
			}
		}
		
		/* temp 파일 하나로 합치기 */
		FILE* fp_list[receiving_peer_num];
		
		// temp 파일 fopen()
		for (int i = 0; i < receiving_peer_num; i ++) {
			fp_list[i] = fopen(file_name_list[i], "rb");
		}
		
		// original 파일 fopen()
		char new_file_name[FILE_NAME_SIZE] = "";
		strcat(new_file_name, file_name);
		strcat(new_file_name, "new");
		FILE* original_fp = fopen(new_file_name, "wb");
		
		// temp 파일에서 데이터를 읽어서 original 파일에 쓰기
		int original_file_write_end_counter = 0;
		
		/* temp 파일을 순서대로 하나씩 읽어서 original 파일에 쓰기 */
		while (original_file_write_end_counter < receiving_peer_num) {
			
			for (int i = 0; i < receiving_peer_num; i ++) {
				
				// buffer 초기화
				for (int j = 0; j < segment_size; j ++) {
					buffer[j] = 0;
				}
				
				// temp 파일로부터 segment_size만큼 데이터를 읽어 buffer에 저장
				size_t read_size = fread(buffer, 1, segment_size, fp_list[i]);
				fwrite(buffer, 1, read_size, original_fp);
				printf("read_size : %ld\n", read_size);
				
				// temp 파일을 읽는 fp가 EOF을 만나면 original_file_write_end_counter 1 증가
				if (feof(fp_list[i]) != 0) {
					fclose(fp_list[i]);
					original_file_write_end_counter += 1;
					
					if (original_file_write_end_counter == receiving_peer_num) {
						break;
					}
				}
			}
		}
		
		printf("COMPLETE\n");
		
		fclose(original_fp);
		free(receiving_peer_socks);
	}
	
	return 0;
}
