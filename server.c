#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <sqlite3.h>

#define CYAN_TEXT "\x1B[36m"
#define RESET_TEXT "\x1B[0m"

#define MAX_CLIENTS 100
#define BUF_SIZE 2048

static _Atomic unsigned int cli_count = 0;
static int uid = 10;

typedef struct{
	sqlite3 *db;
	bool logged_account[2];
	bool logged_in_chat;
	struct sockaddr_in address;
	int sockfd;
	int uid;
	int room_code;
	char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *select_username = "SELECT username FROM users";

char update_logged[200];
char update_profile_status[200];
char update_friend[200];
char select_password[200];
char select_logged_status[200];
char select_notifications[200];
char select_user_id[200];
char select_user[60];
char select_friend[200];
char select_friend_id[10000];
char select_user_data[200];
char delete_notif[200];
char friends[200][20];
char status_friends[200][20];
char send_notif[200];
char post_text[280];
char friend_name[40];
char user_status[10];
char user_profile[10000];
char profile[10000];

int user_id;
int friends_ids[200];

bool user_found = false;
bool friend_found = false;
bool is_user_logged = false;
bool password_found = false;

int isNumber(char *str){
	return strspn(str, "0123456789") == strlen(str);
}

void sql_errors(int return_code, char *error_msg){
	if(return_code != SQLITE_OK){
		fprintf(stderr, "SQL error: %s\n", error_msg);
		sqlite3_free(error_msg);
	}
}

char* encrypt(char* password){
	char first_char = password[0];

	for(int i = 0; i < strlen(password) - 1; i++){
		password[i] = password[i + 1];
	}

	password[strlen(password) - 1] = first_char;
	password[strlen(password)] = '\0';

	return password;
}

int select_name(void *data, int argc, char **argv, char **col_names){
	
	strcpy(friend_name, argv[0]);

	return 0;
}

int show_user(void *data, int argc, char **argv, char **col_names){
	client_t *cl = (client_t *)data;

	sprintf(profile, "POSTS: %s\n", argv[0]);
	write(cl->sockfd, profile, strlen(profile));
	sleep(0.1);
	return 0;
}

int check_user_data(void *data, int argc, char **argv, char **col_names){
	client_t *cl = (client_t *)data;

	user_id = atoi(argv[0]);
	strcpy(user_status, argv[1]);

	int return_code;

	char *error_msg;

	if(strcmp("private", argv[1]) == 0)
	{
		write(cl->sockfd, "This user has a private account.\n", strlen("This user has a private account.\n"));
	}
	else{
		sprintf(user_profile, "NAME: %s\n", argv[2]);
		write(cl->sockfd, user_profile, strlen(user_profile));
		sprintf(user_profile, "SELECT posts.post_content FROM users JOIN posts ON users.user_id = posts.user_id WHERE users.user_id = %d", user_id);
		return_code = sqlite3_exec(cl->db, user_profile, show_user, cl, &error_msg);
		sql_errors(return_code, error_msg);
	}
	return 0;
}

int check_username(void *data, int argc, char **argv, char **col_names){
	char* username = (char *)data;

	if(strcmp(username, argv[0]) == 0){
		user_found = true;
	}	
	return 0;
}

int check_friend(void *data, int argc, char **argv, char **col_names){
	char* friend = (char *)data;
	
	if(strcmp(friend, argv[0]) == 0){
		friend_found = true;
	}
	return 0;
}

int check_password(void *data, int argc, char **argv, char **col_names){
	char* password = (char *)data;
	char encrypted_password[50];

	strcpy(encrypted_password, encrypt(password));
	
	if(strcmp(encrypted_password, argv[0]) == 0){
		password_found = true;
	}
	return 0;
}

int check_logged_status(void *data, int argc, char **argv, char **col_names){

	if(strcmp(argv[0], "online") == 0){
		is_user_logged = true;
	}
	return 0;
}

int find_user_id(void *data, int argc, char **argv, char **col_names){
	
	user_id = atoi(argv[0]);

	return 0;
}

int select_friends_ids(void *data, int argc, char **argv, char **col_names){
	int i = 0;

	while(friends_ids[i] != -1){
		i++;
	}
	
	friends_ids[i] = atoi(argv[0]);

	return 0;
}

int select_ids(void *data, int argc, char **argv, char **col_names){
	
	client_t *cl = (client_t *)data;

	char *error_msg;

	int return_code;

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", argv[0]);
	return_code = sqlite3_exec(cl->db, select_user_id, select_friends_ids, NULL, &error_msg);
	sql_errors(return_code, error_msg);
	
	return 0;
}

int select_friends(void *data, int argc, char **argv, char **col_names){
	
	int i = 0;

	while(friends[i][0] != '\0'){
		i++;
	}

	strcpy(friends[i], argv[0]);
	strcpy(status_friends[i], argv[1]);
	
	return 0;
}

int check_notifications(void *data, int argc, char **argv, char **col_names){
	
	char notification[200];
	char *first_space = strchr(argv[2], ' ');
	char buffer[20];
	char copy[200];
	char *error_msg;
	char full_notification[300];

	int return_code;

	client_t *cl = (client_t *)data;

	memset(full_notification, 0, strlen(full_notification));

	strcpy(copy, argv[2]);

	if(first_space != NULL){
		strcpy(notification, first_space + 1);
		sprintf(full_notification, "Time: %s\n %s\n", argv[3], argv[2]);

		sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
		return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		if(strcmp(notification, "wants to be your friend. Accept/Decline:\n") == 0){
			while(1){
				memset(buffer, 0, sizeof(buffer));
				write(cl->sockfd, full_notification, strlen(full_notification));

				recv(cl->sockfd, buffer, 20, 0);

				if(strcmp(buffer, "accept") == 0){
					char *name = strtok(copy, " \n");

					sprintf(update_friend, "INSERT INTO friends (user_id, friend_name) VALUES (%d, '%s')", user_id, name);
					return_code = sqlite3_exec(cl->db, update_friend, NULL, NULL, &error_msg);
					sql_errors(return_code, error_msg);

					sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", name);
					return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
					sql_errors(return_code, error_msg);

					sprintf(update_friend, "INSERT INTO friends (user_id, friend_name) VALUES (%d, '%s')", user_id, cl->name);
					return_code = sqlite3_exec(cl->db, update_friend, NULL, NULL, &error_msg);
					sql_errors(return_code, error_msg);

					sprintf(delete_notif, "DELETE FROM notifications WHERE notif_id = %d", atoi(argv[0]));

					break;
				}
				else if(strcmp(buffer, "decline") == 0){
					sprintf(delete_notif, "DELETE FROM notifications WHERE notif_id = %d", atoi(argv[0]));

					break;
				}
			}
		}
		else if(strcmp(notification, "made a new post!\n") == 0){
			write(cl->sockfd, full_notification, strlen(full_notification));

			sprintf(delete_notif, "DELETE FROM notifications WHERE notif_id = %d", atoi(argv[0]));
		}
	}

	return 0;
}

void str_trim_lf (char* arr, int length) {
	int i;
	for (i = 0; i < length; i++){
		if (arr[i] == '\n'){
			arr[i] = '\0';
			break;
		}
	}
}

void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i = 0; i < MAX_CLIENTS; i++){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

void send_message(client_t *cl, char *s, int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i = 0; i < MAX_CLIENTS; i++){
		if(clients[i]){
			if(clients[i]->uid != uid && clients[i]->logged_in_chat == true && clients[i]->room_code == cl->room_code){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

void send_request(char *user, client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	char *error_msg;

	int return_code;

	return_code = sqlite3_exec(cl->db, select_username, check_username, user, &error_msg);
	sql_errors(return_code, error_msg);

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	sprintf(select_friend, "SELECT friend_name FROM friends WHERE user_id = %d", user_id);
	return_code = sqlite3_exec(cl->db, select_friend, check_friend, user, &error_msg);
	sql_errors(return_code, error_msg);

	if(user_found == true){
		if(friend_found == true){
			write(cl->sockfd, "This user is already your friend!\n", strlen("This user is already your friend!\n"));
			friend_found = false;
		}
		else{
			sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", user);
			return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
			sql_errors(return_code, error_msg);

			sprintf(send_notif, "INSERT INTO notifications (user_id, text) VALUES (%d, '%s wants to be your friend. Accept/Decline:\n')", user_id, cl->name);
			return_code = sqlite3_exec(cl->db, send_notif, NULL, NULL, &error_msg);
			sql_errors(return_code, error_msg);

			write(cl->sockfd, "Request sent!\n", strlen("Request sent!\n"));

			for(int i = 0; i < MAX_CLIENTS; ++i){
				if(clients[i]){
					if(strcmp(clients[i]->name, user) == 0 && clients[i]->logged_account[1] == true){
						if(write(clients[i]->sockfd, "Server: You have a new notification!\n", strlen("Server: You have a new notification!\n")) < 0){
							perror("ERROR: write to file descriptor failed");
						}
						break;
					}
				}
			}
		}
		user_found = false;
	}
	else{
		write(cl->sockfd, "This user doesn't exist.\n", strlen("This user doesn't exist.\n"));
	}

	pthread_mutex_unlock(&clients_mutex);
}

void update_friend_status(char *friend_name, char* status, client_t *cl){

	pthread_mutex_lock(&clients_mutex);

	char *error_msg;

	int return_code;

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	sprintf(select_friend, "SELECT friend_name FROM friends WHERE user_id = %d", user_id);
	return_code = sqlite3_exec(cl->db, select_friend, check_friend, friend_name, &error_msg);
	sql_errors(return_code, error_msg);

	if(friend_found == true){
		if(strcmp(status, "friend") == 0 || strcmp(status, "close_friend") == 0 || strcmp(status, "family_member") == 0 || strcmp(status, "wife") == 0 || strcmp(status, "girlfriend") == 0 || strcmp(status, "husband") == 0 || strcmp(status, "boyfriend") == 0){
			sprintf(update_friend, "UPDATE friends SET friend_status = '%s' WHERE user_id = %d AND friend_name = '%s'", status, user_id, friend_name);
			return_code = sqlite3_exec(cl->db, update_friend, NULL, NULL, &error_msg);
			sql_errors(return_code, error_msg);

			write(cl->sockfd, "Status updated!\n", strlen("Status updated!\n"));
		}
		else{
			write(cl->sockfd, "The status you typed is invalid!\n", strlen("The status you typed is invalid!\n"));
		}
		friend_found = false;
	}
	else{
		write(cl->sockfd, "This user is not your friend!\n", strlen("This user is not your friend!\n"));
	}

	pthread_mutex_unlock(&clients_mutex);

}

void show_notifications(client_t *cl){
	
	int return_code;

	char *error_msg;

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	sprintf(select_notifications, "SELECT * FROM notifications WHERE user_id = %d", user_id);
	return_code = sqlite3_exec(cl->db, select_notifications, check_notifications, (void*)cl, &error_msg);
	sql_errors(return_code, error_msg);

	return_code = sqlite3_exec(cl->db, delete_notif, NULL, NULL, &error_msg);
	sql_errors(return_code, error_msg);
}

void show_friends(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	char *error_msg;
	char friend_name[100];

	int return_code;
	int i = 0;

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	sprintf(select_friend, "SELECT friend_name, friend_status FROM friends WHERE user_id = %d", user_id);
	return_code = sqlite3_exec(cl->db, select_friend, select_friends, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	while(friends[i][0] != '\0'){
		sprintf(friend_name, "=== NAME: %s === STATUS: %s ===\n", friends[i], status_friends[i]);
		write(cl->sockfd, friend_name, strlen(friend_name));
		sleep(0.1);
		i++;
	}

	for(int j = 0; j < i; j++){
		friends[j][0] = '\0';
		status_friends[j][0] = '\0';
	}

	pthread_mutex_unlock(&clients_mutex);
}

void str_overwrite_stdout() {
    printf(CYAN_TEXT "\n%s", "> " RESET_TEXT);
    fflush(stdout);
}

void command_list(client_t *cl){

	const char *commands[] = {
		"1. register (Create an account)\n", 
		"2. login *username* (Login the site)\n",
		"3. logout (Logout if you are logged in)\n",
		"4. profile *public/private* (Make your profile viewable to anybody/friends only)\n",
		"5. add_friend *username* (Send a friend request to someone)\n",
		"6. update_friend_status *username* *friend/close_friend/family_member/wife/husband/girlfriend/boyfriend* (Update your relationship status with someone)\n",
		"7. post (Post something on your profile)\n",
		"8. view_friends (View your friends)\n",
		"9. view (View the profile of a user)\n",
		"10. chat (Join a chatroom)\n",
		"11. notifications (See your notifications)\n",
		"12. exit (Exit the chat/the app)\n"
	};

	for(int i = 0; i < sizeof(commands)/sizeof(commands[0]); i++){
		const char *command = commands[i];
		size_t command_length = strlen(command);
		ssize_t bytes_written;
		size_t total_bytes_written = 0;

		while(total_bytes_written < command_length){
			
			bytes_written = write(cl->sockfd, command + total_bytes_written, command_length - total_bytes_written);
			sleep(0.1);

			if(bytes_written == -1){
				perror("ERROR: write inside 'help'");
				break;
			}

			total_bytes_written = total_bytes_written + bytes_written;
		}
		fflush(stdout);
	}

}

void update_profile(client_t *cl, char *word){

	int return_code;

	char *error_msg;

	if(word != NULL && strcmp(word, "private") == 0){
		sprintf(update_profile_status, "UPDATE users SET account_status = 'private' WHERE username = '%s'", cl->name);
		return_code = sqlite3_exec(cl->db, update_profile_status, NULL, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		write(cl->sockfd, "Profile updated!\n", strlen("Profile updated!\n"));
	}
	else if(word != NULL && strcmp(word, "public") == 0){
		sprintf(update_profile_status, "UPDATE users SET account_status = 'public' WHERE username = '%s'", cl->name);
		return_code = sqlite3_exec(cl->db, update_profile_status, NULL, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		write(cl->sockfd, "Profile updated!\n", strlen("Profile updated!\n"));
	}
	else
		write(cl->sockfd, "Server: Write a valid command...\n", strlen("Server: Write a valid command...\n"));
}

void logout(client_t *cl){

	int return_code;

	char *error_msg;

	write(cl->sockfd, "Logged out succesfully.\n", strlen("Logged out succesfully.\n"));

	cl->logged_account[0] = false;
	cl->logged_account[1] = false;

	sprintf(update_logged, "UPDATE users SET logged_status = 'offline' WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, update_logged, NULL, NULL, &error_msg);
	sql_errors(return_code, error_msg);
}

void insert_password(client_t *cl, char *buffer){

	int return_code;

	char *error_msg;

	sprintf(select_password, "SELECT password FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_password, check_password, (void *)buffer, &error_msg);	
	sql_errors(return_code, error_msg);

	sprintf(select_logged_status, "SELECT logged_status FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_logged_status, check_logged_status, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	if(password_found == true && is_user_logged == false){
		sprintf(update_logged, "UPDATE users SET logged_status = 'online' WHERE username = '%s'", cl->name);
		return_code = sqlite3_exec(cl->db, update_logged, NULL, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		write(cl->sockfd, "User logged!\n", strlen("User logged!\n"));
		cl->logged_account[1] = true;
	}
	else if(password_found == false){
		write(cl->sockfd, "Wrong password, try again:\n", strlen("Wrong password, try again:\n"));
	}
	else if(is_user_logged == true){
		write(cl->sockfd, "This user is already logged in.\n", strlen("This user is already logged in.\n"));
		cl->logged_account[0] = false;
	}

	password_found = false;
	is_user_logged = false;
}

void login_account(client_t *cl, char *command){

	int return_code;

	char *error_msg;

	if(command != NULL){
		return_code = sqlite3_exec(cl->db, select_username, check_username, (void *)command, &error_msg);
		sql_errors(return_code, error_msg);
	}
	if (user_found == true) {
        cl->logged_account[0] = true;
		strcpy(cl->name, command);
		write(cl->sockfd, "Please insert password:\n", strlen("Please insert password:\n"));
		sleep(0.1);
		write(cl->sockfd, cl->name, strlen(cl->name));
    }
	else if(user_found == false){
		write(cl->sockfd, "Username doesn't exist.\n", strlen("Username doesn't exist.\n"));
	}

	user_found = false;
}

void register_account(client_t *cl, char *buffer){
	
	int return_code;

	char *error_msg;
	char user[50];
	char password[50];
	char create_account[300];

	write(cl->sockfd, "Please insert username:\n", strlen("Please insert username:\n"));
	while(1){
		memset(buffer, 0, BUF_SIZE);
		recv(cl->sockfd, buffer, BUF_SIZE, 0);

		if(strlen(buffer) >= 4 && strlen(buffer) <= 32 && strcmp(buffer, "exit") && strcmp(buffer, "chat")){

			return_code = sqlite3_exec(cl->db, select_username, check_username, (void *)buffer, &error_msg);
			sql_errors(return_code, error_msg);

			if(user_found == true){
				write(cl->sockfd, "Username taken. Insert another username:\n", strlen("Username taken. Insert another username:\n"));
				user_found = false;
			}
			else{
				strcpy(user, buffer);
				break;
			}
		}
		else if(strcmp(buffer, "exit") == 0 || strcmp(buffer, "chat") == 0){
			write(cl->sockfd, "This is not a valid username. Insert another username:\n", strlen("This is not a valid username. Insert another username:\n"));
		}
		else{
			write(cl->sockfd, "Username needs to be at least 4 characters and at most 32 characters. Insert another username:\n", strlen("Username needs to be at least 4 characters and at most 32 characters. Insert another username:\n"));
		}
	}
	write(cl->sockfd, "Please insert new password:\n", strlen("Please insert new password:\n"));
	while(1){
		memset(buffer, 0, BUF_SIZE);

		recv(cl->sockfd, buffer, BUF_SIZE, 0);
		if(strlen(buffer) >= 4 && strlen(buffer) <= 20){
			strcpy(password, encrypt(buffer));

			sprintf(create_account, "INSERT INTO users (username, password, account_type, account_status, logged_status) VALUES ('%s', '%s', 'normal_user', 'public', 'offline')", user, password);				
			return_code = sqlite3_exec(cl->db, create_account, NULL, NULL, &error_msg);
			sql_errors(return_code, error_msg);

			write(cl->sockfd, "Account registered succesfully!\n", strlen("Account registered succesfully!\n"));

			break;
		}
		else if(strlen(buffer) < 4)
			write(cl->sockfd, "Password is too short. Insert another password:\n", strlen("Password is too short. Insert another password:\n"));
		else
			write(cl->sockfd, "Password is too long. Insert another password:\n", strlen("Password is too long. Insert another password:\n"));
	}
}

void connect_to_chatroom(client_t *cl, char *word, char *buffer){
	if(word != NULL && isNumber(word)){
		cl->room_code = atoi(word);

		write(cl->sockfd, "Joined in chat!\n", strlen("Joined in chat!\n"));

		sprintf(buffer, "Server: %s has joined\n", cl->name);
		send_message(cl, buffer, cl->uid);

		cl->logged_in_chat = true;	
	}
	else{
		write(cl->sockfd, "The room code needs to be a number. Insert the command again:\n", strlen("The room code needs to be a number. Insert the command again:\n"));
	}
}

void create_post(client_t *cl){
	
	int i = 0;
	int return_code;

	char *error_msg;
	char post[200];

	write(cl->sockfd, "Write your post:\n", strlen("Write your post:\n"));
	memset(post, 0, strlen(post));
	recv(cl->sockfd, post, 200, 0);

	sprintf(select_user_id, "SELECT user_id FROM users WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, select_user_id, find_user_id, NULL, &error_msg);
	sql_errors(return_code, error_msg);
	

	sprintf(post_text, "INSERT INTO posts (user_id, post_content) VALUES (%d, '%s')", user_id, post);
	return_code = sqlite3_exec(cl->db, post_text, NULL, NULL, &error_msg);
	sql_errors(return_code, error_msg);

	write(cl->sockfd, "Post created!\n", strlen("Post created!\n"));

	sprintf(select_friend_id, "SELECT friend_name FROM friends WHERE user_id = %d", user_id);
	return_code = sqlite3_exec(cl->db, select_friend_id, select_ids, cl, &error_msg);
	sql_errors(return_code, error_msg);

	while(friends_ids[i] != -1){
		sprintf(select_user, "SELECT username FROM users WHERE user_id = %d", friends_ids[i]);
		return_code = sqlite3_exec(cl->db, select_user, select_name, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		for(int i = 0; i < MAX_CLIENTS; ++i){
			if(clients[i]){
				if(strcmp(clients[i]->name, friend_name) == 0 && clients[i]->logged_account[1] == true){
					if(write(clients[i]->sockfd, "Server: You have a new notification!\n", strlen("Server: You have a new notification!\n")) < 0){
						perror("ERROR: write to file descriptor failed");
					}
				}
			}
		}

		sprintf(send_notif, "INSERT INTO notifications (user_id, text) VALUES (%d, '%s made a new post!\n')", friends_ids[i], cl->name);
		return_code = sqlite3_exec(cl->db, send_notif, NULL, NULL, &error_msg);
		sql_errors(return_code, error_msg);

		i++;
	}

	for (int i = 0; i < 200; ++i) {
    	friends_ids[i] = -1;
	}
}

void view(char *buffer, client_t *cl){
	char *error_msg;

	int return_code;

	return_code = sqlite3_exec(cl->db, select_username, check_username, buffer, &error_msg);
	sql_errors(return_code, error_msg);
	if(user_found == true){
		sprintf(select_user_data, "SELECT user_id, account_status, username FROM users WHERE username = '%s'", buffer);
		return_code = sqlite3_exec(cl->db, select_user_data, check_user_data, cl, &error_msg);
		sql_errors(return_code, error_msg);
	}
	else{
		write(cl->sockfd, "This user doesn't exit.\n", strlen("This user doesn't exist.\n"));
	}
}

void closing_the_client(client_t *cl, int cli_count){

	int return_code;

	char *error_msg;

	sprintf(update_logged, "UPDATE users SET logged_status = 'offline' WHERE username = '%s'", cl->name);
	return_code = sqlite3_exec(cl->db, update_logged, NULL, NULL, &error_msg);
	sql_errors(return_code, error_msg);
	sqlite3_close(cl->db);
	memset(cl->name, 0, sizeof(cl->name));
	close(cl->sockfd);
	queue_remove(cl->uid);
	free(cl);
	cli_count--;
	pthread_detach(pthread_self());
}

void *handle_client(void *arg){
	char buffer[BUF_SIZE];

	int leave_flag = 0;

	cli_count++;
	client_t *cl = (client_t *)arg;

	char *error_msg;

	int return_code = sqlite3_open("users_data.db", &cl->db);

	if(return_code != SQLITE_OK){
		fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(cl->db));
		sqlite3_close(cl->db);
	}

	memset(buffer, 0, BUF_SIZE);

	while(1){

		if (leave_flag) {
			break;
		}

		int receive = recv(cl->sockfd, buffer, BUF_SIZE, 0);

		if(strcmp(buffer, "Closing by ctrl+c") == 0){
			closing_the_client(cl, cli_count);

			return NULL;
		}
		else if(strcmp(buffer, "view") == 0 && cl->logged_in_chat == false){
			write(cl->sockfd, "Write a user to view:\n", strlen("Write a user to view:\n"));
			recv(cl->sockfd, buffer, BUF_SIZE, 0);
			view(buffer, cl); 
		}
		else if(strcmp(buffer, "exit") == 0 && cl->logged_in_chat == false){
			write(cl->sockfd, "Quitting VirtualSoc...", strlen("Quitting VirtualSoc..."));
			leave_flag = 1;
		}
		else if(strcmp(buffer, "help") == 0 && cl->logged_in_chat == false){
			command_list(cl);
		}
		else if(cl->logged_in_chat == true){
			if (receive > 0 && strcmp(buffer, "exit") != 0){
				if(strlen(buffer) > 0){
					send_message(cl, buffer, cl->uid);
					str_trim_lf(buffer, strlen(buffer));
				}
			} else if (receive == 0 || strcmp(buffer, "exit") == 0){
				sprintf(buffer, "Server: %s has left\n", cl->name);
				send_message(cl, buffer, cl->uid);
				write(cl->sockfd, "You left the chat.\n", strlen("You left the chat.\n"));
				cl->room_code = 0;
				cl->logged_in_chat = false;
				
			} else {
				printf("ERROR: -1\n");
				leave_flag = 1;
			}
		}
		else if(cl->logged_account[0] == false){
				char *command = strtok(buffer, " \n");

				if(command != NULL && strcmp(command, "login") == 0){
					command = strtok(NULL, " \n");
					login_account(cl, command);
				}
				else if(command != NULL && strcmp(command, "register") == 0){
					register_account(cl, buffer);
				}
				else{
					write(cl->sockfd, "Please register/login.\n", strlen("Please register/login.\n"));
				}
		}
		else if(cl->logged_account[0] == true && cl->logged_account[1] == false){
				insert_password(cl, buffer);
		}
		else if(cl->logged_account[1] == true && cl->logged_in_chat != true){
			char *word = strtok(buffer, " \n");
			
			if(word != NULL && strcmp(word, "logout") == 0){
				logout(cl);
			}
			else if(word != NULL && strcmp(word, "chat") == 0){
				word = strtok(NULL, " \n");

				if(word != NULL && strcmp(word, "cod") == 0){
					word = strtok(NULL, " \n");
					connect_to_chatroom(cl, word, buffer);
				}
				else{
					write(cl->sockfd, "Server: Write a valid command.\n", strlen("Server: Write a valid command.\n"));
				}
			}
			else if(word != NULL && strcmp(word, "profile") == 0){
				word = strtok(NULL, " \n");
				update_profile(cl, word);
			}
			else if(word != NULL && strcmp(word, "add_friend") == 0){
				word = strtok(NULL, " \n");

				if(word != NULL){
					return_code = sqlite3_exec(cl->db, select_username, check_username, (void *)word, &error_msg);		
					sql_errors(return_code, error_msg);

					if(user_found == true){
						send_request(word, cl);
					}
					else{
						write(cl->sockfd, "This user doesn't exist.\n", strlen("This user doesn't exist.\n"));
					}
				}
			}
			else if(word != NULL && strcmp(word, "notifications") == 0){
				show_notifications(cl);
			}
			else if(word != NULL && strcmp(word, "update_friend_status") == 0){
				char status[20];
				char name[20];

				word = strtok(NULL, " \n");
				strcpy(name, word);
				word = strtok(NULL, " \n");
				strcpy(status, word);
				if(name != NULL && status != NULL){
					update_friend_status(name, status, cl);
				}
			}
			else if(word != NULL && strcmp(word, "view_friends") == 0){
				show_friends(cl);
			}
			else if(word != NULL && strcmp(word, "post") == 0){
				create_post(cl);
			}
			else{
				write(cl->sockfd, "Server: Write a valid command.\n", strlen("Server: Write a valid command.\n"));
			}
		}
		memset(buffer, 0, BUF_SIZE);
	}

	closing_the_client(cl, cli_count);

	return NULL;
}

int main(int argc, char **argv){

	for(int i = 0; i < 200; i++){
		friends[i][0] = '\0';
		status_friends[i][0] = '\0';
	}

	for (int i = 0; i < 200; ++i) {
        friends_ids[i] = -1;
	}

	if(argc != 2){
		printf("Syntax: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1";
	int port = atoi(argv[1]);
	int option = 1;
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ip);
	serv_addr.sin_port = htons(port);

	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char*)&option, sizeof(option)) < 0){
		perror("ERROR: setsockopt failed");
    	return EXIT_FAILURE;
	}

  	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    	perror("ERROR: Socket binding failed");
    	return EXIT_FAILURE;
  	}

  	if (listen(listenfd, 10) < 0) {
    	perror("ERROR: Socket listening failed");
    	return EXIT_FAILURE;
	}

	printf("=== VirtualSoc Server ===\n");

	while(1){
		
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		if((cli_count + 1) == MAX_CLIENTS){
			printf("Max clients reached. Rejected: ");
			print_client_addr(cli_addr);
			printf(":%d\n", cli_addr.sin_port);
			close(connfd);
			continue;
		}

		client_t *cl = (client_t *)malloc(sizeof(client_t));
		cl->logged_account[0] = false;
		cl->logged_account[1] = false;
		cl->logged_in_chat = false;
		cl->address = cli_addr;
		cl->sockfd = connfd;
		cl->uid = uid++;
		cl->room_code = 0;
		memset(cl->name, 0, sizeof(cl->name));

		queue_add(cl);
		pthread_create(&tid, NULL, &handle_client, (void*)cl);

		sleep(1);
	}

	return EXIT_SUCCESS;
}