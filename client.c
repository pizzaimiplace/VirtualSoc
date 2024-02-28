#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sqlite3.h>

#define LENGTH 2048
#define RED_TEXT "\x1B[31m"
#define GREEN_TEXT "\x1B[32m"
#define YELLOW_TEXT "\x1B[33m"
#define CYAN_TEXT "\x1B[36m"
#define RESET_TEXT "\x1B[0m"

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[32];

bool logged_account = false;
bool logged_chat = false;

void str_overwrite_stdout(){
    printf(CYAN_TEXT "\n%s", "> " RESET_TEXT);
    fflush(stdout);
}

void str_trim_lf (char* arr, int length){
    int i;
    for (i = 0; i < length; i++){
        if (arr[i] == '\n'){
            arr[i] = '\0';
            break;
        }
    }
}

void catch_ctrl_c_and_exit(int sig){
    send(sockfd, "Closing by ctrl+c", strlen("Closing by ctrl+c"), 0);
    flag = 1;
}

void send_msg_handler(){
    char message[LENGTH] = {};
	char buffer[LENGTH + 36] = {};
    while(1){
        str_overwrite_stdout();
        fgets(message, LENGTH, stdin);
        str_trim_lf(message, LENGTH);

        if(logged_account != true) {
            send(sockfd, message, strlen(message), 0);
        }
        else if(logged_account == true && logged_chat != true){
            send(sockfd, message, strlen(message), 0);
        }
        else if(logged_account == true && logged_chat == true) {
            if (strcmp(message, "exit") == 0) {
                send(sockfd, "exit", strlen("exit"), 0);
            } 
            else {
                sprintf(buffer, "%s: %s\n", name, message);
                send(sockfd, buffer, strlen(buffer), 0);
            }
        }
        memset(message, 0, LENGTH);
        memset(buffer, 0, LENGTH + 36);
    }
    catch_ctrl_c_and_exit(2);
}

void recv_msg_handler() {
	char message[LENGTH] = {};
    while (1) {
	    int receive = recv(sockfd, message, LENGTH, 0);

        if (receive > 0) {
            printf(YELLOW_TEXT "%s" RESET_TEXT, message);

            if(strcmp(message, "Quitting VirtualSoc...") != 0 || logged_chat == true){
                str_overwrite_stdout();
            }
        } 
        else if (receive == 0) {
            logged_chat = false;
            break;
        }
        if(strcmp(message, "Please insert password:\n") == 0){
            memset(message, 0, sizeof(message));

            int receive = recv(sockfd, message, LENGTH, 0);
            if (receive > 0) {
                strcpy(name, message);
            } 
            else if (receive == 0) {
                break;
            }
        }
        else if(strcmp(message, "User logged!\n") == 0){
            logged_account = true;
        }
        else if(strcmp(message, "Joined in chat!\n") == 0){
            logged_chat = true;
        }
        else if(strcmp(message, "You left the chat.\n") == 0){
            logged_chat = false;
        }
        else if(strcmp(message, "Quitting VirtualSoc...") == 0 && logged_chat == false){
            flag = 1;
        }
        else if(strcmp(message, "Logged out succesfully.\n") == 0){
            logged_account = false;
            memset(name, 0, sizeof(name));
        }
	    memset(message, 0, sizeof(message));
    }
}

int main(int argc, char **argv){

	if(argc != 2){
		printf("Syntax: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1";
	int port = atoi(argv[1]);

	signal(SIGINT, catch_ctrl_c_and_exit);

	struct sockaddr_in server_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);


    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1) {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }

	printf(RED_TEXT "============== Welcome To VirtualSoc ==============\n" RESET_TEXT);
    printf(RED_TEXT "===== Type 'help' to see the list of commands =====\n" RESET_TEXT);

	pthread_t send_msg_thread;
    if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0){
		printf("ERROR: pthread\n");
        return EXIT_FAILURE;
	}

	pthread_t recv_msg_thread;
    if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0){
		printf("ERROR: pthread\n");
		return EXIT_FAILURE;
	}

	while (1){
		if(flag){
			printf("\nClosed the program.\n");
			break;
        }
	}

	close(sockfd);

	return EXIT_SUCCESS;
}