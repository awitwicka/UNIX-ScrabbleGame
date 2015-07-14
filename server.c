#define _GNU_SOURCE 
#include "common.h"

#define SEM_NAME_SIZE 32
#define SHMSIZE sizeof(gameInfo_t)
#define SHM1SIZE 2*sizeof(int)
#define MAXGAMES 10
#define TILE_START 5
#define STARTING_SEMAPHORE 0
#define TILES_NO 25
#define LOG_MSG_SIZE 200
#define LOG_INFO_SIZE 50
#define LOG_LINE_SIZE LOG_MSG_SIZE+LOG_INFO_SIZE

#define RESOLVED 1
#define UNRESOLVED 0
//TODO: handle read better
//TODO: wrapper wywolan child_exit_on_opponent_disconnect
//TODO: sprawdzic semafory na exicie czy sie undouja
//TODO: reset semafor po skonczeniu gry
//TODO: po skonczeniu gry wyczysccic shm id -> 0
typedef struct {
	int gameId;
	int lastMove[3];
	int lastLetterIndex;
	int score[2];
	int status; //wyzeruj
	char letters[TILES_NO];
} gameInfo_t;

volatile sig_atomic_t do_work=1;
volatile sig_atomic_t do_work_child=1;

void sigint_handler(int sig) {
	do_work=0;
	if(kill(0, SIGUSR1)<0) ERR("kill");
}

void child_handler(int sig) {
	fprintf(stderr, "[%i] Sigint, closing...\n", getpid());
	exit(EXIT_FAILURE);	
}

void shuffle_array(char* array, int n) {
	srand(getpid());
	int i = 0, j;
	char tmp;
	if(n>1) {
		for(; i<n-1; i++) {
			j = (i+rand())%n;
			tmp = array[j];
			array[j] = array[i];
			array[i] = tmp;
		}
	} 
}
 
/****************************************************************************/
//Function: operation_on_semaphore
//	performs operation on specific semaphore in the set
//Arguments:
//	int sem_id - id of the semaphore set
//	int sme_no - index of semaphore
//	short operation  
//		if positive - add resources
//		if negative - wait until x resources are avaible and take them
//		if zero		- wait until is 0
void operation_on_semaphore(int sem_id, int sem_no, short operation) {
	struct sembuf sbuf;
	sbuf.sem_num = sem_no;
	sbuf.sem_op = operation;
	sbuf.sem_flg = SEM_UNDO;
	if(TEMP_FAILURE_RETRY(semop(sem_id, &sbuf, 1))<0) ERR("semop");
	//fprintf(stderr, "->[%i] (%d)  id: %d  n: %d\n", getpid(), operation, sem_id, sem_no);
}

void operation_on_semaphore_noundo(int sem_id, int sem_no, short operation) {
	struct sembuf sbuf;
	sbuf.sem_num = sem_no;
	sbuf.sem_op = operation;
	sbuf.sem_flg = 0;
	if(TEMP_FAILURE_RETRY(semop(sem_id, &sbuf, 1))<0) ERR("semop");
	//fprintf(stderr, "->[%i] (%d)  id: %d  n: %d\n", getpid(), operation, sem_id, sem_no);
}

int operation_on_semaphore_with_timeout(int sem_id, int sem_no, short operation, int timeout_ms) {
	struct timespec timespan;
	struct sembuf sbuf;
	timespan.tv_sec = timeout_ms / 1000;
	timespan.tv_nsec = (timeout_ms % 1000) * 1000;
	sbuf.sem_num = sem_no;
	sbuf.sem_op = operation;
	sbuf.sem_flg = SEM_UNDO;
	if(TEMP_FAILURE_RETRY(semtimedop(sem_id, &sbuf, 1, &timespan))<0){	
		if (errno == EAGAIN)
			return -1; 
		else 
			ERR("semop");
	}else return 0;
}

void lock_correct_semaphore(int player_no, int sem_id, int sem_id2, int sem_index) {
	if(player_no == 1) {
		operation_on_semaphore(sem_id, sem_index, -1);
	} else if(player_no == 2) {
		operation_on_semaphore(sem_id2, sem_index, -1);
	}
}

void unlock_correct_semaphore(int player_no, int sem_id, int sem_id2, int sem_index) {
	if(player_no == 1) {
		operation_on_semaphore(sem_id2, sem_index, 1);
	} else if(player_no == 2) {
		operation_on_semaphore(sem_id, sem_index, 1);
	}
}

void unlock_correct_semaphore_noundo(int player_no, int sem_id, int sem_id2, int sem_index) {
	if(player_no == 1) {
		operation_on_semaphore_noundo(sem_id2, sem_index, 1);
	} else if(player_no == 2) {
		operation_on_semaphore_noundo(sem_id, sem_index, 1);
	}
}
/**********************************************************************************/

void usage(char * name){
	fprintf(stderr,"USAGE: %s port\n",name);
}

void append_to_log(int cfd, int game_no, const char line_to_append[LOG_MSG_SIZE]) {
	int fd;
	char file_name[PATH_MAX];
	time_t t = time(NULL);
	struct tm tInfo = *localtime(&t);
	char buffer[LOG_LINE_SIZE] = {0};
	
	char ip[INET6_ADDRSTRLEN];
	struct sockaddr_storage addr;
	
	//get ip
	socklen_t addrlen = sizeof(addr);
	if(getpeername(cfd, (struct sockaddr*)&addr, &addrlen)<0) ERR("getpeername");
	struct sockaddr_in *s = (struct sockaddr_in*)&addr;
	inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));
	//open file
	snprintf(file_name, PATH_MAX, "game%i_log", game_no);
	if((fd = TEMP_FAILURE_RETRY(open(file_name, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))) < 0) ERR("open");
	//dopisz linie
	snprintf(buffer, LOG_LINE_SIZE, "Date: %02d/%02d/%02d, Time: %02d:%02d:%02d IP:%s %s", 
		tInfo.tm_year+1900, tInfo.tm_mon+1, tInfo.tm_mday, tInfo.tm_hour, tInfo.tm_min, tInfo.tm_sec, ip, line_to_append);
		
	if(bulk_write_line(fd, buffer,LOG_LINE_SIZE)<0) ERR("write:");	
	if(TEMP_FAILURE_RETRY(close(fd)) < 0) ERR("close");
}

void clear_game(int* shm) {
	//TODO: set resolved
	gameInfo_t* gameInfo;
	int status;
	char tiles[TILES_NO] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
	gameInfo = (gameInfo_t*)shm;
	status = gameInfo->status;
	memset(shm, 0, SHMSIZE);
	shuffle_array(tiles, TILES_NO);
	strncpy(gameInfo->letters, tiles, TILES_NO);
	gameInfo->status = status;
}

void child_exit(int cfd, int player_no, int game_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	char log_line[LOG_MSG_SIZE];
	gameInfo_t* gameInfo;
	
	if(player_no == 1) {
		gameInfo = (gameInfo_t*)shm;
		snprintf(log_line, LOG_MSG_SIZE, "END OF GAME RESULT: PLAYER1:%i    PLAYER2:%i\n", gameInfo->score[0], gameInfo->score[1]);
		append_to_log(cfd, game_no, log_line);
		clear_game(shm);
	}
	if(shmdt(shm) != 0) ERR("shmdt");	
	fprintf(stderr, "[%i] Exiting...\n", getpid());
	exit(EXIT_SUCCESS);
}

void child_exit_on_client_disconnect(int player_no, int cfd, int sem_id, int sem_id2, int sem_index, int* shm) {
	gameInfo_t* gameInfo;
	
	gameInfo = (gameInfo_t*)shm;
	gameInfo->status = RESOLVED;
	fprintf(stderr, "[%i] Exiting, connection to client lost!\n", getpid());
	unlock_correct_semaphore_noundo(player_no, sem_id, sem_id2, sem_index);
	if(shmdt(shm) != 0) ERR("shmdt");	
	exit(EXIT_SUCCESS);
}	

void child_exit_on_opponent_disconnect(int player_no, int game_no, int cfd, int sem_id, int sem_id2, int sem_index, int* shm) {
	int32_t error[3] = {htonl(-1), htonl(-1), htonl(-1)};
	char log_line[LOG_MSG_SIZE];
	
	fprintf(stderr, "[%i] Exiting, connection to opponent lost!\n", getpid());
	if(bulk_write(cfd,(char *)error,sizeof(int32_t[3]))<0&&errno!=EPIPE) ERR("write");
	unlock_correct_semaphore_noundo(player_no, sem_id, sem_id2, sem_index);
	//append
	snprintf(log_line, LOG_MSG_SIZE, "END OF GAME RESULT: UNRESOLVED\n");
	append_to_log(cfd, game_no, log_line);
	clear_game(shm);
	if(shmdt(shm) != 0) ERR("shmdt");
	exit(EXIT_SUCCESS);
}	

/*************************************************************************************/

char get_next_letter(int* shm) {
	gameInfo_t* gameInfo;
	char letter; 
	int letter_index;
	//operation_on_semaphore(sem_id, sem_index, -1);
	gameInfo = (gameInfo_t*)shm;
	letter_index = gameInfo->lastLetterIndex;
	if(letter_index<TILES_NO) {
		letter = gameInfo->letters[letter_index];
		gameInfo->lastLetterIndex++;
	} else {
		letter = 0;
	}	
	//operation_on_semaphore(sem_id, sem_index, +1);
	return letter;
}

void send_letter(int cfd, int game_no, int player_no, char letter, int sem_id, int sem_id2, int sem_index, int* shm) {
	int32_t l = htonl(letter);
	gameInfo_t* gameInfo;
	
	gameInfo = (gameInfo_t*)shm;
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if (bulk_write(cfd,(char *)&l,sizeof(int32_t))<0) {
		if(errno == EPIPE) {
			child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
		} else {
			ERR("write");
		}
	}
}

void send_player_no(int cfd, int game_no, int player_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	int32_t player_no_send = htonl(player_no);
	gameInfo_t* gameInfo;
	
	gameInfo = (gameInfo_t*)shm;
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if(bulk_write(cfd,(char *)&player_no_send,sizeof(int32_t))<0) {
		if(errno == EPIPE) {
			child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
		} else {
			ERR("write");
		}
	}
}

void receive_move(int cfd, int player_no, int game_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	gameInfo_t* gameInfo;
	char log_line[LOG_MSG_SIZE];
	int32_t move[3];
	memset(move,0,sizeof(move));
	gameInfo = (gameInfo_t*)shm;
	
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if(bulk_read(cfd,(char *)move,sizeof(int32_t[3]))<(int)sizeof(int32_t[3])) {//ERR("read:");
		child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
	}
	//operation_on_semaphore(sem_id, sem_index, -1);
	gameInfo->lastMove[0] = ntohl(move[0]);
	gameInfo->lastMove[1] = ntohl(move[1]);
	gameInfo->lastMove[2] = ntohl(move[2]);
	snprintf(log_line, LOG_MSG_SIZE, "Player:%i x=%i y=%i tile=%c\n", player_no, ntohl(move[0]), ntohl(move[1]), ntohl(move[2]));
	append_to_log(cfd, game_no, log_line);
	//operation_on_semaphore(sem_id, sem_index, +1);
}

void send_move(int cfd, int game_no, int player_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	gameInfo_t* gameInfo;
	int32_t move[3];
	//operation_on_semaphore(sem_id, sem_index, -1);
	gameInfo = (gameInfo_t*)shm;
	move[0] = htonl(gameInfo->lastMove[0]);
	move[1] = htonl(gameInfo->lastMove[1]);
	move[2] = htonl(gameInfo->lastMove[2]);
	//operation_on_semaphore(sem_id, sem_index, +1);
	
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if(bulk_write(cfd,(char *)move,sizeof(int32_t[3]))<0) {
		if(errno == EPIPE) {
			child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
		} else {
			ERR("write");
		}
	}
}

void send_opponents_score(int cfd, int game_no, int player_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	fprintf(stderr, "[%i] Sending opponents score!\n", getpid());
	gameInfo_t* gameInfo;
	int32_t s;
	gameInfo = (gameInfo_t*)shm;
	
	if (player_no==1) 
		s = htonl(gameInfo->score[1]);
	if (player_no==2)
		s = htonl(gameInfo->score[0]);
		
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if(bulk_write(cfd,(char *)&s,sizeof(int32_t))<0) {
		if(errno == EPIPE) {
			child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
		} else {
			ERR("write");
		}
	}
}

void receive_score(int cfd, int game_no, int player_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	fprintf(stderr, "[%i] Receiving score!\n", getpid());
	gameInfo_t* gameInfo;
	int32_t s;
	
	gameInfo = (gameInfo_t*)shm;
	if(gameInfo->status == RESOLVED) 
		child_exit_on_opponent_disconnect(player_no, game_no, cfd, sem_id, sem_id2, sem_index, shm);
	if(bulk_read(cfd,(char *)&s,sizeof(int32_t))<(int)sizeof(int32_t)) {//ERR("read");
		child_exit_on_client_disconnect(player_no, cfd, sem_id, sem_id2, sem_index, shm);
	}
	gameInfo->score[player_no-1] = ntohl(s);
	fprintf(stderr, "[%i] Received score: player:%i score:%i\n", getpid(), player_no, ntohl(s));
}

/***********************************************************************************/
void child_start_game(int cfd, int game_no, int player_no, int sem_id, int sem_id2, int sem_index, int* shm) {
	int i = 0;
	char next_letter;
	//distribute first 5 letters
	//operation_on_semaphore(sem_id, sem_index, -1);
	fprintf(stderr, "[%i] Distributing 5 letters\n", getpid());
	for (; i<TILE_START; i++) {
		next_letter = get_next_letter(shm);
		//fprintf(stderr, "Sending %c\n", next_letter);
		send_letter(cfd, game_no, player_no, next_letter, sem_id, sem_id2, sem_index, shm);
	}
	//operation_on_semaphore(sem_id, sem_index, +1);
}

void child_wait_for_game(int sem_id_awaiting_players, int sem_id, int sem_id2, int* sem_index, int** shm_game, int32_t* player_no) {
	int shm_id_start, shm_id;
	int* shm_start; 
	key_t key;
	
	//get access to start shm
	if((shm_id_start = shmget(1, SHM1SIZE, 0777))<0) ERR("shmget");
	if((shm_start = (int*) shmat(shm_id_start, NULL, 0))==(int*)-1) ERR("shmat");
	//wait on semaphore, get data
	operation_on_semaphore_noundo(sem_id_awaiting_players, 0, +1);
	fprintf(stderr, "[%i] Connection established! Waiting for an opponent..\n", getpid());
	operation_on_semaphore_noundo(sem_id, STARTING_SEMAPHORE, -1); 
	fprintf(stderr, "[%i] Opponent matched!\n", getpid());
	//get player number and key
	*player_no=*(shm_start+1);
	key = *shm_start;
	*sem_index = (*shm_start)-1;
	if(*player_no==1) {
		*(shm_start+1) = 2;
		operation_on_semaphore_noundo(sem_id, STARTING_SEMAPHORE, +1);
	} else if(*player_no==2){
		*(shm_start+1) = 1;
	} else {ERR("reading player number");}
	//dettach start shm
	if(shmdt(shm_start) != 0) ERR("shmdt");	
	//get access to game shm
	fprintf(stderr, "[%i] Got my SHM name: %i\n", getpid(), key);
	if((shm_id = shmget(key, SHMSIZE, 0777))<0) ERR("shmget");
	if((*shm_game = (int*) shmat(shm_id, NULL, 0))==(int*)-1) ERR("shmat");
}

void child_work(int cfd, int game_no, int sem_id_awaiting_players, int sem_id, int sem_id2){
	int* shm_game;
	int sem_index;
	int player_no;
	char next_letter;
	int move_no = 0;
	int max_move_no;
	
	child_wait_for_game(sem_id_awaiting_players, sem_id, sem_id2, &sem_index, &shm_game, &player_no);
	fprintf(stderr, "[%i] SEM=%i PLAYER_NO:%i\n", getpid(), sem_index, player_no);
	
	lock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
	child_start_game(cfd, game_no, player_no, sem_id, sem_id2, sem_index, shm_game);//->S
	send_player_no(cfd, game_no, player_no, sem_id, sem_id2, sem_index, shm_game);//S
	unlock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
		
	max_move_no = (TILES_NO/2)+(player_no%2);
	fprintf(stderr, "[%i] Moves: %i...\n", getpid(), max_move_no);
	while(move_no<max_move_no) {
		lock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
		send_move(cfd, game_no, player_no, sem_id, sem_id2, sem_index, shm_game); //S
		next_letter = get_next_letter(shm_game);
		send_letter(cfd, game_no, player_no, next_letter, sem_id, sem_id2, sem_index, shm_game);//S
		receive_move(cfd, player_no, game_no, sem_id, sem_id2, sem_index, shm_game);//R
		unlock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
		move_no++;
	}
	fprintf(stderr, "[%i] Processing scores...\n", getpid());
	lock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
	receive_score(cfd, game_no, player_no, sem_id, sem_id2, sem_index, shm_game);//R
	unlock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
	lock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
	send_opponents_score(cfd, game_no, player_no, sem_id, sem_id2, sem_index, shm_game);//S
	unlock_correct_semaphore(player_no, sem_id, sem_id2, sem_index);
	
	child_exit(cfd, player_no, game_no, sem_id, sem_id2, sem_index, shm_game);
}

/********************************************MAIN PROCESS***************************************************/

void manage_new_client(pid_t child, int* game_no, int sem_id_awaiting_players, int sem_id, int sem_id2, int * shm_start, int* sharedMemory[MAXGAMES]) {
	int i;
	gameInfo_t* gameInfo;
	union semun {
		int val;
		struct semid_ds *buf;
		ushort *array;
	} arg1, arg2;
	
	if(operation_on_semaphore_with_timeout(sem_id_awaiting_players, 0, -2, 1000) == 0) {
		fprintf(stderr, "[S] Matched 2 players together, looking for avaible space.\n");
		for(i=0; i<MAXGAMES; i++) {
			//TODO: give 1 charge of apfel
			//operation_on_semaphore(sem_id, i+1, -1); //TODO: nowait?
			gameInfo = (gameInfo_t*)sharedMemory[i];
			if(gameInfo->gameId == 0) {
				gameInfo->gameId = (*game_no)++;
				//memset(gameInfo->lastMove, 0, sizeof(gameInfo->lastMove)); //works?
				gameInfo->status = UNRESOLVED;
				arg1.val = 1;
				arg2.val = 0;
				if((semctl(sem_id, i+1, SETVAL, arg1))==-1) ERR("semctl");
				if((semctl(sem_id2, i+1, SETVAL, arg2))==-1) ERR("semctl");
				*shm_start = i+2;
				operation_on_semaphore(sem_id, STARTING_SEMAPHORE, +1);
				//operation_on_semaphore(sem_id, i+1, +1);
				break;
			}
			//operation_on_semaphore(sem_id, i+1, +1);
		}
	} 
	
	//what to do if not game gefunden?
	
	//if(shmdt(shm) != 0) ERR("shmdt");
	//if(shmdt(shm_start) != 0) ERR("shmdt");	
}

void reserveResources(int* sem_id_awaiting_players, int* sem_id, int* sem_id2, int** shm_start, int* sharedMemory[MAXGAMES], int* shm_start_id, int sharedMemory_id[MAXGAMES]) { 
	int i = 0;
	//char sem_name[SEM_NAME_SIZE];
	char tiles[TILES_NO] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
	key_t key = 123, key2 = 124, key3 = 125;
	gameInfo_t* gameInfo;
	
	//create semaphores
	if ((*sem_id = semget(key, MAXGAMES+1, IPC_CREAT | 0666))<0) ERR("semget");
	if ((*sem_id2 = semget(key2, MAXGAMES+1, IPC_CREAT | 0666))<0) ERR("semget");
	if ((*sem_id_awaiting_players = semget(key3, 1, IPC_CREAT | 0666))<0) ERR("semget");
	//create start shared memory 
	if((*shm_start_id = shmget(1, SHM1SIZE, IPC_CREAT | 0777))<0) ERR("shmget");
	if((*shm_start = (int*) shmat(*shm_start_id, NULL, 0))==(int*)-1) ERR("shmat");
	memset(*shm_start,0,SHM1SIZE);
	*((*shm_start)+1)=1;
	//create shared memory for games
	for(; i<MAXGAMES; i++) {
		key= i+2; //od 2 do Maxgames+2
		operation_on_semaphore(*sem_id, i+1, 1);
		//sharedmemory
		fprintf(stderr, "[S] Allocating memory for both players!\n"); 
		if((sharedMemory_id[i] = shmget(key, SHMSIZE, IPC_CREAT/* | IPC_EXCL*/ | 0777))<0) ERR("shmget");
		if((sharedMemory[i] = (int*) shmat(sharedMemory_id[i], NULL, 0))==(int*)-1) ERR("shmat");
		memset(sharedMemory[i],0,SHMSIZE);
		gameInfo = (gameInfo_t*)sharedMemory[i];
		gameInfo->gameId = 0;
		gameInfo->lastLetterIndex = 0;
		gameInfo->status = 0;
		shuffle_array(tiles, TILES_NO);
		strncpy(gameInfo->letters, tiles, TILES_NO);
		//fprintf(stderr, "SENDING %i\n", tiles[0]);
		fprintf(stderr, "[S] Memory allocated!\n");
	}
}

void dispatchResources(int* sem_id_awaiting_players, int* sem_id, int* sem_id2, int** shm, int* sharedMemory[MAXGAMES], int* shm_start_id, int sharedMemory_id[MAXGAMES]) {
	 int i;
	 //dispatch semaphores
	 if((semctl(*sem_id, 0, IPC_RMID, 0))==-1) ERR("semctl");
	 if((semctl(*sem_id2, 0, IPC_RMID, 0))==-1) ERR("semctl");
	 if((semctl(*sem_id_awaiting_players, 0, IPC_RMID, 0))==-1) ERR("semctl");
	 //dispatch shm
	 if(shmdt(*shm) != 0) ERR("shmdt");
	 if(shmctl(*shm_start_id , IPC_RMID , 0)<0) ERR("shmctl");
	 for(i=0; i<MAXGAMES; i++) {
	 	if(shmdt(sharedMemory[i]) != 0) ERR("shmdt");
		if(shmctl(sharedMemory_id[i], IPC_RMID , 0)<0) ERR("shmctl");
	 }
}

void doServer(int fdT){
	int clientfd;
	pid_t child;
	int game_no = 1;
	int sem_id, sem_id2, sem_id_awaiting_players;
	int* shm;
	int* sharedMemory[MAXGAMES];
	
	int shm_start_id;
	int sharedMemory_id[MAXGAMES];

	fd_set base_rfds, rfds ;
	sigset_t mask, oldmask;
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

	reserveResources(&sem_id_awaiting_players, &sem_id, &sem_id2, &shm, sharedMemory, &shm_start_id, sharedMemory_id);
	
	while(do_work){
		rfds=base_rfds;
		if(pselect(fdT+1,&rfds,NULL,NULL,NULL,&oldmask)>0){ //use pselect?
			clientfd=add_new_client(fdT);
			/*COMMUNICATION - HERE PROCESSES*/ 
			if(clientfd>=0) {
				switch(child=fork()) { 
					case 0:
						if(sethandler(child_handler,SIGUSR1)) ERR("Seting child SIGUSR1:");
						child_work(clientfd, game_no, sem_id_awaiting_players, sem_id, sem_id2);
						//exit(EXIT_SUCCESS); //TODO: end of process, need to add pid remove
					case -1:
						perror("Fork:");
						exit(EXIT_FAILURE); //handle?
				}
				manage_new_client(child, &game_no, sem_id_awaiting_players, sem_id, sem_id2, shm, sharedMemory);
			}
		}else{
			if(EINTR==errno) continue;
			ERR("pselect");
		}
	}
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
	dispatchResources(&sem_id_awaiting_players, &sem_id, &sem_id2, &shm, sharedMemory, &shm_start_id, sharedMemory_id);
}	

int main(int argc, char** argv) {
	int fdT;
	int new_flags;
	if(argc!=2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if(sethandler(SIG_IGN,SIGUSR1)) ERR("Seting SIGUSR1");
	if(sethandler(SIG_IGN,SIGPIPE)) ERR("Seting SIGPIPE:");
	if(sethandler(sigint_handler,SIGINT)) ERR("Seting SIGINT:");
	/*TCP CONNECTION*/
	fdT=bind_tcp_socket(atoi(argv[1]));
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	if(fcntl(fdT, F_SETFL, new_flags) == -1)ERR("fcntl");
	/*SERVER WORK*/
	fprintf(stderr, "[%i][S] Server is running!\n", getpid());
	doServer(fdT);    
	/*CLOSE*/                                                                              	
	if(TEMP_FAILURE_RETRY(close(fdT))<0)ERR("close");
	fprintf(stderr,"Server has terminated.\n");
	return EXIT_SUCCESS;
}
